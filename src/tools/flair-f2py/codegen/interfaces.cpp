#include "interfaces.hpp"

#include <algorithm>
#include <functional>
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
      // name (== `modpy` when the type is local), not the current one. This
      // lets overloads on types wrapped in other files dispatch correctly.
      semantics::Symbol const &tsym = ds->typeSymbol();
      str_t const owner = module_pyname(flu::owning_module_name(tsym));
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
  auto code_kind = [&](arg_tag_t::kind_t k) {
    arg_tag_t t;
    t.kind = k;
    return code_of(t);
  };

  bool any_derived = false, any_array = false, any_int = false,
       any_real = false, any_cmplx = false, any_str = false;
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
      case arg_tag_t::Cmplx:
        any_cmplx = true;
        break;
      case arg_tag_t::Str:
        any_str = true;
        break;
      case arg_tag_t::Bool: // identity test against the singletons; no locals
        break;
      }

  // ---- declarations -------------------------------------------------------
  str_t decls;
  decls += "        integer(c_ptrdiff_t) :: nargs\n";
  for (size_t p : disc) {
    decls += fmt::format("        type(c_ptr) :: a{}\n", p);
    decls += fmt::format("        integer :: tag{}\n", p);
  }
  if (any_derived || any_array || any_str)
    decls += "        type(PyObject_t), pointer :: pyobj\n";
  if (any_derived || any_str)
    decls += "        type(PyTypeObject_t), pointer :: pytype\n";
  if (any_int)
    decls += "        integer(c_long_long) :: val_i\n";
  if (any_real)
    decls += "        real(c_double) :: val_r\n";
  if (any_cmplx)
    decls += "        complex(c_double_complex) :: val_z\n";
  if (any_int || any_real || any_cmplx)
    decls += "        logical :: ok\n";
  if (any_array) {
    decls += "        integer(c_int) :: nd\n";
    decls += "        type(c_ptr) :: dsc\n";
  }
  str_t const s_err = strings.intern("unexpected argument type for " + pyname);

  // ---- classify each discriminating position into tag<p> -------------------
  str_t cls;
  cls += "        nargs = PyTuple_Size(args)\n";
  for (size_t p : disc) {
    // present tags at this position
    bool d = false, a = false, ii = false, rr = false, zz = false, bb = false,
         ss = false;
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
      case arg_tag_t::Cmplx:
        zz = true;
        break;
      case arg_tag_t::Bool:
        bb = true;
        break;
      case arg_tag_t::Str:
        ss = true;
        break;
      }
    }

    cls += fmt::format("        a{} = c_null_ptr\n", p);
    cls += fmt::format(
        "        if (nargs > {0}) a{0} = PyTuple_GetItem(args, "
        "{0}_c_ptrdiff_t)\n",
        p);
    cls += fmt::format("        tag{} = 0\n", p);
    cls += fmt::format("        if (c_associated(a{})) then\n", p);
    str_t const I = "            ";

    if (d || a || ss)
      cls += fmt::format("{}call c_f_pointer(a{}, pyobj)\n", I, p);

    // Scalar probe chain, one nested block per present kind. Exact-type
    // tests (bool identity, str tp_name) need no exception dance and come
    // first; bool must precede the int probe because PyLong accepts
    // True/False. Then int before real before complex: each converter down
    // the chain also accepts everything the earlier ones do. The str probe
    // relies on pytype, set by derived_or_prim below.
    enum probe_t { PBool, PStr, PInt, PReal, PCmplx };
    std::vector<probe_t> probes;
    if (bb)
      probes.push_back(PBool);
    if (ss)
      probes.push_back(PStr);
    if (ii)
      probes.push_back(PInt);
    if (rr)
      probes.push_back(PReal);
    if (zz)
      probes.push_back(PCmplx);
    std::function<str_t(size_t, str_t const &)> chain =
        [&](size_t j, str_t const &ind) -> str_t {
      if (j >= probes.size())
        return "";
      str_t s;
      switch (probes[j]) {
      case PBool:
      case PStr: {
        str_t const cond =
            probes[j] == PBool
                ? fmt::format(
                      "c_ptr_eq(a{0}, Py_GetConstant(Py_CONSTANT_TRUE)) .or. "
                      "c_ptr_eq(a{0}, Py_GetConstant(Py_CONSTANT_FALSE))",
                      p)
                : str_t{"c_string_eq(pytype%tp_name, \"str\")"};
        s += fmt::format("{}if ({}) then\n", ind, cond);
        s += fmt::format(
            "{}    tag{} = {}\n", ind, p,
            code_kind(probes[j] == PBool ? arg_tag_t::Bool : arg_tag_t::Str));
        if (str_t const rest = chain(j + 1, ind + "    "); !rest.empty()) {
          s += fmt::format("{}else\n", ind);
          s += rest;
        }
        s += fmt::format("{}end if\n", ind);
        break;
      }
      case PInt:
      case PReal:
      case PCmplx: {
        struct probe_conv_t {
          char const *var, *helper;
          arg_tag_t::kind_t kind;
        };
        probe_conv_t const conv = probes[j] == PInt
                                      ? probe_conv_t{"i", "int64", arg_tag_t::Int}
                                  : probes[j] == PReal
                                      ? probe_conv_t{"r", "double", arg_tag_t::Real}
                                      : probe_conv_t{"z", "dcomplex",
                                                     arg_tag_t::Cmplx};
        s += fmt::format("{}val_{} = FLAIR_{}_from_PyObject(a{}, ok)\n", ind,
                         conv.var, conv.helper, p);
        s += fmt::format("{}if (ok) then\n", ind);
        s += fmt::format("{}    tag{} = {}\n", ind, p, code_kind(conv.kind));
        s += fmt::format("{}else\n", ind);
        s += fmt::format("{}    call PyErr_Clear()\n", ind);
        s += chain(j + 1, ind + "    ");
        s += fmt::format("{}end if\n", ind);
        break;
      }
      }
      return s;
    };
    auto prim = [&](str_t const &ind) { return chain(0, ind); };

    // derived / primitive block (no array)
    auto derived_or_prim = [&](str_t const &ind) {
      str_t s;
      if (d || ss)
        s += fmt::format("{}call c_f_pointer(pyobj%ob_type, pytype)\n", ind);
      if (d) {
        bool first = true;
        for (arg_tag_t const *t : derived_tags) {
          s += fmt::format("{}{} (c_string_eq(pytype%tp_name, \"{}\")) then\n",
                           ind, first ? "if" : "else if", t->derived);
          s += fmt::format("{}    tag{} = {}\n", ind, p, code_of(*t));
          first = false;
        }
        if (ii || rr || zz || bb || ss) {
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
