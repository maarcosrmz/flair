#include "interfaces.hpp"

#include <algorithm>
#include <map>
#include <utility>

#include <fmt/core.h>
#include <fmt/format.h>

#include <flang/Semantics/symbol.h>
#include <flang/Semantics/type.h>

#include "codegen.hpp"
#include "flu/symbols.hpp"
#include "flu/types.hpp"
#include "pytypes.hpp"

namespace codegen {

using namespace Fortran;

// ===========================================================================
// Template (compiled in via C23 #embed; works at C++17 with clang). The
// dispatcher is structurally a `function(self, args) -> r`, so it reuses the
// module-function skeleton.
// ===========================================================================
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc23-extensions"
static constexpr char tpl_dispatch[] = {
#embed "templates/module_function.txt"
    , '\0'};
#pragma clang diagnostic pop

namespace {

// Runtime-distinguishable classification of one dummy argument. Numeric
// scalars are distinguished only by category (not kind): Python cannot see a
// Fortran integer kind, so int32/int64 overloads collapse. Arrays keep their
// numpy dtype + rank, both of which *are* visible at runtime.
struct arg_tag_t {
  enum kind_t { Int, Real, Cmplx, Bool, Str, Derived, Array } kind;
  str_t derived;         // Derived: tp_name literal "<modpy>.<Class>"
  str_t npy;             // Array: numpy type code constant
  int rank = 0;          // Array: rank
  int skind = 0;         // scalar: Fortran kind (for widest-overload selection)
  bool optional = false; // absent/None at this position still matches
};

// Stable per-position key, ignoring scalar kind, used for grouping overloads
// and for detecting which positions actually discriminate.
str_t tag_key(arg_tag_t const &t) {
  switch (t.kind) {
  case arg_tag_t::Int:
    return "i";
  case arg_tag_t::Real:
    return "r";
  case arg_tag_t::Cmplx:
    return "z";
  case arg_tag_t::Bool:
    return "b";
  case arg_tag_t::Str:
    return "s";
  case arg_tag_t::Derived:
    return "d:" + t.derived;
  case arg_tag_t::Array:
    return "a:" + t.npy + ":" + std::to_string(t.rank);
  }
  return "?";
}

struct cand_t {
  semantics::Symbol const *sym;
  std::vector<arg_tag_t> tags;
};

// Build the argument descriptor for a specific procedure, mirroring the
// accept/reject rule in parse_args. Returns false if any dummy is unsupported.
bool build_tags(semantics::Symbol const &spec, std::vector<arg_tag_t> &out) {
  if (!spec.has<semantics::SubprogramDetails>())
    return false;
  auto const &sub = spec.get<semantics::SubprogramDetails>();
  for (semantics::Symbol *d : sub.dummyArgs()) {
    if (d == nullptr)
      return false;
    auto const *t = d->GetType();
    if (t == nullptr)
      return false;
    arg_tag_t tag;
    tag.optional = d->attrs().test(semantics::Attr::OPTIONAL);
    if (auto const *ds = t->AsDerived()) {
      // The tp_name the object carries at runtime is set by the file that wraps
      // the *defining* module, so key the discriminator on that module's python
      // name (== `pyqual` when the type is local), not the current one. This
      // lets overloads on types wrapped in other files dispatch correctly. It
      // must be the package-qualified name, matching what spec_fills emits.
      semantics::Symbol const &tsym = ds->typeSymbol();
      str_t const owner = module_pyqual(flu::owning_module_name(tsym));
      tag.kind = arg_tag_t::Derived;
      tag.derived = owner + "." + clsname(tsym);
    } else if (flu::rank_of(*d) > 0 && array_supported(*t)) {
      tag.kind = arg_tag_t::Array;
      tag.npy = npy(*t);
      tag.rank = flu::rank_of(*d);
    } else if (flu::rank_of(*d) == 0 && intrinsic_supported(*t)) {
      switch (*flu::category(*t)) {
      case Fortran::common::TypeCategory::Real:
        tag.kind = arg_tag_t::Real;
        break;
      case Fortran::common::TypeCategory::Complex:
        tag.kind = arg_tag_t::Cmplx;
        break;
      case Fortran::common::TypeCategory::Logical:
        tag.kind = arg_tag_t::Bool;
        break;
      case Fortran::common::TypeCategory::Character:
        tag.kind = arg_tag_t::Str;
        break;
      default:
        tag.kind = arg_tag_t::Int;
        break;
      }
      tag.skind = flu::kind_of(*t);
    } else {
      return false;
    }
    out.push_back(tag);
  }
  return true;
}

int kind_width(std::vector<arg_tag_t> const &tags) {
  int w = 0;
  for (auto const &t : tags)
    w += t.skind;
  return w;
}

str_t descriptor_key(std::vector<arg_tag_t> const &tags) {
  str_t k;
  for (auto const &t : tags)
    k += tag_key(t) + "|";
  return k;
}

} // namespace

str_t gen_interface_wrapper(
    semantics::Symbol const &iface,
    std::vector<semantics::Symbol const *> const &specifics,
    string_pool_t &strings, str_t *fills, int &n, str_t *table_decls) {
  // ---- candidates, collapsing kind-only overloads to the widest -----------
  std::vector<cand_t> uniq;
  std::map<str_t, size_t> seen; // descriptor key -> index into uniq
  for (semantics::Symbol const *s : specifics) {
    cand_t c{s, {}};
    if (!build_tags(*s, c.tags))
      continue;
    str_t const key = descriptor_key(c.tags);
    auto it = seen.find(key);
    if (it == seen.end()) {
      seen.emplace(key, uniq.size());
      uniq.push_back(std::move(c));
    } else if (kind_width(c.tags) > kind_width(uniq[it->second].tags)) {
      uniq[it->second] = std::move(c); // prefer the widest-kind overload
    }
  }
  if (uniq.empty())
    return "";

  str_t const pyname = iface.name().ToString();
  str_t const wrapper = fmt::format("py_mod_{}", pyname);
  auto fwd = [](semantics::Symbol const *s) {
    return fmt::format("py_mod_{}", s->name().ToString());
  };

  if (fills != nullptr)
    *fills += method_row("module_methods", ++n, strings.intern(pyname), wrapper,
                         "METH_VARARGS + METH_KEYWORDS");

  // ---- single candidate: unconditional forward ----------------------------
  // A specific without dummies has the two-parameter METH_NOARGS signature.
  if (uniq.size() == 1) {
    str_t body = fmt::format("        r = {}(self, args{})\n",
                             fwd(uniq.front().sym),
                             uniq.front().tags.empty() ? "" : ", kwds");
    return render(tpl_dispatch, {{"fn", wrapper}, {"body", body}}) + "\n";
  }

  // ---- discriminating positions (where the overloads differ) --------------
  size_t arity = uniq.front().tags.size();
  for (auto const &c : uniq)
    arity = std::min(arity, c.tags.size());
  std::vector<size_t> disc;
  for (size_t p = 0; p < arity; ++p) {
    str_t first = tag_key(uniq.front().tags[p]);
    for (auto const &c : uniq)
      if (tag_key(c.tags[p]) != first) {
        disc.push_back(p);
        break;
      }
  }

  // ---- assign an integer code to each distinct tag at a disc position ------
  std::map<str_t, int> code;
  int next_code = 1;
  auto code_of = [&](arg_tag_t const &t) {
    str_t const k = tag_key(t);
    auto it = code.find(k);
    return it != code.end() ? it->second : (code[k] = next_code++);
  };

  // ---- one saved tag table per discriminating position ---------------------
  // The probe order lives in FLAIR_classify; the wrapper only says which
  // kinds may appear where, and which code each stands for.
  auto kind_const = [](arg_tag_t::kind_t k) -> char const * {
    switch (k) {
    case arg_tag_t::Int:
      return "FLAIR_K_INT";
    case arg_tag_t::Real:
      return "FLAIR_K_REAL";
    case arg_tag_t::Cmplx:
      return "FLAIR_K_CMPLX";
    case arg_tag_t::Bool:
      return "FLAIR_K_BOOL";
    case arg_tag_t::Str:
      return "FLAIR_K_STR";
    case arg_tag_t::Derived:
      return "FLAIR_K_DERIVED";
    case arg_tag_t::Array:
      return "FLAIR_K_ARRAY";
    }
    return "0";
  };

  str_t decls;
  decls += "        integer(c_ptrdiff_t) :: nargs\n";
  for (size_t p : disc)
    decls += fmt::format("        integer :: tag{}\n", p);
  str_t const s_err = strings.intern("unexpected argument type for " + pyname);

  str_t cls;
  cls += "        nargs = PyTuple_Size(args)\n";
  for (size_t p : disc) {
    // Distinct tags at this position, in candidate order.
    std::vector<arg_tag_t const *> here;
    for (auto const &c : uniq) {
      arg_tag_t const &t = c.tags[p];
      bool dup = false;
      for (arg_tag_t const *e : here)
        if (tag_key(*e) == tag_key(t))
          dup = true;
      if (!dup)
        here.push_back(&t);
    }
    str_t const tbl = fmt::format("{}_tags{}", wrapper, p);
    if (table_decls != nullptr)
      *table_decls +=
          fmt::format("    type(FLAIR_tag_t), save :: {}({})\n", tbl,
                      here.size());
    if (fills != nullptr) {
      *fills += fmt::format("        ! --- {} dispatch tags, argument {} ---\n",
                            pyname, p);
      for (size_t k = 0; k < here.size(); ++k) {
        arg_tag_t const &t = *here[k];
        *fills += fmt::format(
            "        {}({}) = FLAIR_tag_t({}, {}, {}, {}, {})\n", tbl, k + 1,
            kind_const(t.kind), code_of(t), t.rank,
            t.kind == arg_tag_t::Array ? t.npy : str_t("0"),
            t.kind == arg_tag_t::Derived
                ? fmt::format("c_loc({})", strings.intern(t.derived))
                : str_t("c_null_ptr"));
      }
    }
    cls += fmt::format("        tag{0} = 0\n", p);
    cls += fmt::format(
        "        if (nargs > {0}) tag{0} = FLAIR_classify("
        "PyTuple_GetItem(args, {0}_c_ptrdiff_t), {1})\n",
        p, tbl);
  }

  // ---- forward to the matching specific wrapper ----------------------------
  str_t fwdc;
  for (auto const &c : uniq) {
    // Too many positional arguments rule the candidate out; an absent (or
    // None) value at a discriminating position matches when the candidate's
    // dummy there is optional.
    str_t guard =
        fmt::format("nargs <= {}_c_ptrdiff_t", c.tags.size());
    for (size_t p : disc) {
      guard += " .and. ";
      if (c.tags[p].optional)
        guard += fmt::format("(tag{0} == {1} .or. tag{0} == 0)", p,
                             code_of(c.tags[p]));
      else
        guard += fmt::format("tag{} == {}", p, code_of(c.tags[p]));
    }
    fwdc += fmt::format("        if ({}) then\n", guard);
    fwdc += fmt::format("            r = {}(self, args{})\n", fwd(c.sym),
                        c.tags.empty() ? "" : ", kwds");
    fwdc += "            return\n";
    fwdc += "        end if\n";
  }
  fwdc += fmt::format(
      "        call PyErr_SetString(PyExc_TypeError, c_loc({}))\n", s_err);
  fwdc += "        r = c_null_ptr\n";

  str_t body = decls + cls + fwdc;
  return render(tpl_dispatch, {{"fn", wrapper}, {"body", body}}) + "\n";
}

} // namespace codegen
