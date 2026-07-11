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

// Runtime-distinguishable classification of one dummy argument. Integer/real
// scalars are distinguished only by category (not kind): Python cannot see a
// Fortran integer kind, so int32/int64 overloads collapse. Arrays keep their
// numpy dtype + rank, both of which *are* visible at runtime.
struct arg_tag_t {
  enum kind_t { Int, Real, Derived, Array } kind;
  str_t derived; // Derived: tp_name literal "<modpy>.<Class>"
  str_t npy;     // Array: numpy type code constant
  int rank = 0;  // Array: rank
  int skind = 0; // Int/Real: Fortran kind (for widest-overload selection)
};

// Stable per-position key, ignoring scalar kind, used for grouping overloads
// and for detecting which positions actually discriminate.
str_t tag_key(arg_tag_t const &t) {
  switch (t.kind) {
  case arg_tag_t::Int:
    return "i";
  case arg_tag_t::Real:
    return "r";
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
    if (auto const *ds = t->AsDerived()) {
      // The tp_name the object carries at runtime is set by the file that wraps
      // the *defining* module, so key the discriminator on that module's python
      // name (== `modpy` when the type is local), not the current one. This
      // lets overloads on types wrapped in other files dispatch correctly.
      semantics::Symbol const &tsym = ds->typeSymbol();
      str_t const owner = module_pyname(flu::owning_module_name(tsym));
      tag.kind = arg_tag_t::Derived;
      tag.derived = owner + "." + clsname(tsym);
    } else if (flu::rank_of(*d) > 0 && intrinsic_supported(*t)) {
      tag.kind = arg_tag_t::Array;
      tag.npy = npy(*t);
      tag.rank = flu::rank_of(*d);
    } else if (flu::rank_of(*d) == 0 && intrinsic_supported(*t)) {
      auto cat = flu::category(*t);
      tag.kind = (cat && *cat == Fortran::common::TypeCategory::Real)
                     ? arg_tag_t::Real
                     : arg_tag_t::Int;
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
    string_pool_t &strings, str_t *fills, int &n) {
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
                         "METH_VARARGS");

  // ---- single candidate: unconditional forward ----------------------------
  if (uniq.size() == 1) {
    str_t body =
        fmt::format("        r = {}(self, args)\n", fwd(uniq.front().sym));
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
  auto code_kind = [&](arg_tag_t::kind_t k) {
    arg_tag_t t;
    t.kind = k;
    return code_of(t);
  };

  bool any_derived = false, any_array = false, any_int = false,
       any_real = false;
  for (size_t p : disc)
    for (auto const &c : uniq)
      switch (c.tags[p].kind) {
      case arg_tag_t::Derived:
        any_derived = true;
        break;
      case arg_tag_t::Array:
        any_array = true;
        break;
      case arg_tag_t::Int:
        any_int = true;
        break;
      case arg_tag_t::Real:
        any_real = true;
        break;
      }

  // ---- declarations -------------------------------------------------------
  str_t decls;
  for (size_t p : disc) {
    decls += fmt::format("        type(c_ptr) :: a{}\n", p);
    decls += fmt::format("        integer :: tag{}\n", p);
  }
  if (any_derived || any_array)
    decls += "        type(PyObject_t), pointer :: pyobj\n";
  if (any_derived)
    decls += "        type(PyTypeObject_t), pointer :: pytype\n";
  if (any_int)
    decls += "        integer(c_long_long) :: val_i\n";
  if (any_real)
    decls += "        real(c_double) :: val_r\n";
  if (any_int || any_real)
    decls += "        logical :: ok\n";
  if (any_array) {
    decls += "        integer(c_int) :: nd\n";
    decls += "        type(c_ptr) :: dsc\n";
  }
  str_t const s_err = strings.intern("unexpected argument type for " + pyname);

  // ---- classify each discriminating position into tag<p> -------------------
  str_t cls;
  for (size_t p : disc) {
    // present tags at this position
    bool d = false, a = false, ii = false, rr = false;
    std::vector<arg_tag_t const *> derived_tags, array_tags;
    auto push_uniq = [](std::vector<arg_tag_t const *> &v, arg_tag_t const &t) {
      for (arg_tag_t const *e : v)
        if (tag_key(*e) == tag_key(t))
          return;
      v.push_back(&t);
    };
    for (auto const &c : uniq) {
      arg_tag_t const &t = c.tags[p];
      switch (t.kind) {
      case arg_tag_t::Derived:
        push_uniq(derived_tags, t);
        d = true;
        break;
      case arg_tag_t::Array:
        push_uniq(array_tags, t);
        a = true;
        break;
      case arg_tag_t::Int:
        ii = true;
        break;
      case arg_tag_t::Real:
        rr = true;
        break;
      }
    }

    cls += fmt::format(
        "        a{0} = PyTuple_GetItem(args, {0}_c_ptrdiff_t)\n", p);
    cls += fmt::format("        tag{} = 0\n", p);
    cls += fmt::format("        if (c_associated(a{})) then\n", p);
    str_t const I = "            ";

    if (d || a)
      cls += fmt::format("{}call c_f_pointer(a{}, pyobj)\n", I, p);

    // primitive probe (integer-first), emitted as a reusable block
    auto prim = [&](str_t const &ind) {
      str_t s;
      if (ii && rr) {
        s += fmt::format("{}val_i = FLAIR_int64_from_PyObject(a{}, ok)\n", ind,
                         p);
        s += fmt::format("{}if (ok) then\n", ind);
        s += fmt::format("{}    tag{} = {}\n", ind, p,
                         code_kind(arg_tag_t::Int));
        s += fmt::format("{}else\n", ind);
        s += fmt::format("{}    call PyErr_Clear()\n", ind);
        s += fmt::format("{}    val_r = FLAIR_double_from_PyObject(a{}, ok)\n",
                         ind, p);
        s += fmt::format("{}    if (ok) then\n", ind);
        s += fmt::format("{}        tag{} = {}\n", ind, p,
                         code_kind(arg_tag_t::Real));
        s += fmt::format("{}    else\n", ind);
        s += fmt::format("{}        call PyErr_Clear()\n", ind);
        s += fmt::format("{}    end if\n", ind);
        s += fmt::format("{}end if\n", ind);
      } else if (ii) {
        s += fmt::format("{}val_i = FLAIR_int64_from_PyObject(a{}, ok)\n", ind,
                         p);
        s += fmt::format("{}if (ok) then\n", ind);
        s += fmt::format("{}    tag{} = {}\n", ind, p,
                         code_kind(arg_tag_t::Int));
        s += fmt::format("{}else\n", ind);
        s += fmt::format("{}    call PyErr_Clear()\n", ind);
        s += fmt::format("{}end if\n", ind);
      } else if (rr) {
        s += fmt::format("{}val_r = FLAIR_double_from_PyObject(a{}, ok)\n",
                         ind, p);
        s += fmt::format("{}if (ok) then\n", ind);
        s += fmt::format("{}    tag{} = {}\n", ind, p,
                         code_kind(arg_tag_t::Real));
        s += fmt::format("{}else\n", ind);
        s += fmt::format("{}    call PyErr_Clear()\n", ind);
        s += fmt::format("{}end if\n", ind);
      }
      return s;
    };

    // derived / primitive block (no array)
    auto derived_or_prim = [&](str_t const &ind) {
      str_t s;
      if (d) {
        s += fmt::format("{}call c_f_pointer(pyobj%ob_type, pytype)\n", ind);
        bool first = true;
        for (arg_tag_t const *t : derived_tags) {
          s += fmt::format("{}{} (c_string_eq(pytype%tp_name, \"{}\")) then\n",
                           ind, first ? "if" : "else if", t->derived);
          s += fmt::format("{}    tag{} = {}\n", ind, p, code_of(*t));
          first = false;
        }
        if (ii || rr) {
          s += fmt::format("{}else\n", ind);
          s += prim(ind + "    ");
        }
        s += fmt::format("{}end if\n", ind);
      } else {
        s += prim(ind);
      }
      return s;
    };

    if (a) {
      cls += fmt::format("{}if (c_associated(numpy_api_ptr) .and. "
                         "c_ptr_eq(pyobj%ob_type, PyArray_Type_ptr)) then\n",
                         I);
      cls += fmt::format("{}    nd  = PyArray_NDIM(a{})\n", I, p);
      cls += fmt::format("{}    dsc = PyArray_DESCR(a{})\n", I, p);
      bool first = true;
      for (arg_tag_t const *t : array_tags) {
        cls += fmt::format("{}    {} (nd == {} .and. c_ptr_eq(dsc, "
                           "PyArray_DescrFromType({}))) then\n",
                           I, first ? "if" : "else if", t->rank, t->npy);
        cls += fmt::format("{}        tag{} = {}\n", I, p, code_of(*t));
        first = false;
      }
      if (!array_tags.empty())
        cls += fmt::format("{}    end if\n", I);
      if (str_t const dop = derived_or_prim(I + "    "); !dop.empty()) {
        cls += fmt::format("{}else\n", I);
        cls += dop;
      }
      cls += fmt::format("{}end if\n", I);
    } else {
      cls += derived_or_prim(I);
    }
    cls += "        end if\n";
  }

  // ---- forward to the matching specific wrapper ----------------------------
  str_t fwdc;
  for (auto const &c : uniq) {
    str_t guard;
    for (size_t p : disc) {
      if (!guard.empty())
        guard += " .and. ";
      guard += fmt::format("tag{} == {}", p, code_of(c.tags[p]));
    }
    fwdc += fmt::format("        if ({}) then\n", guard);
    fwdc += fmt::format("            r = {}(self, args)\n", fwd(c.sym));
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
