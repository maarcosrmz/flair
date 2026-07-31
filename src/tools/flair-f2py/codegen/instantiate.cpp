#include "instantiate.hpp"

#include <fmt/core.h>
#include <fmt/format.h>

#include <flang/Semantics/attr.h>
#include <flang/Semantics/symbol.h>
#include <flang/Semantics/type.h>

#include "codegen.hpp"
#include "dtypes.hpp"
#include "flu/diagnostics.hpp"
#include "flu/symbols.hpp"
#include "flu/types.hpp"

namespace codegen {

using namespace Fortran;

// ===========================================================================
// Template (compiled in via C23 #embed; works at C++17 with clang). The
// dispatcher is structurally a `function(self, args) -> r`, so it reuses the
// module-function skeleton, like the interface dispatcher.
// ===========================================================================
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc23-extensions"
static constexpr char tpl_dispatch[] = {
#embed "templates/module_function.txt"
    , '\0'};
#pragma clang diagnostic pop

namespace {

// Resolved '!flair$ instantiate' directive: which dummy positions to
// instantiate, and with which wrapped types.
struct inst_plan_t {
  std::vector<size_t> poly_pos; // polymorphic indices into the parsed dummies
  std::vector<sema::Symbol const *> types; // listed types, sorted by name
};

// One element of the cartesian product: the chosen type index per dimension
// (self first for TBPs, then poly_pos order) and the specific to forward to.
struct combo_t {
  std::vector<size_t> idx;
  str_t fwd;
};

// The tp_name a wrapped instance carries at runtime, set by the file that
// wraps the *defining* module (same keying as the interface dispatcher):
// the package-qualified module name, matching what spec_fills emits.
str_t tp_name_of(sema::Symbol const &tsym) {
  return module_pyqual(flu::owning_module_name(tsym)) + "." + clsname(tsym);
}

// Advance the odometer over |types|^dims tuples (last dimension fastest).
// False once all combinations were visited.
bool next_combo(std::vector<size_t> &idx, size_t ntypes) {
  for (size_t d = idx.size(); d-- > 0;) {
    if (++idx[d] < ntypes)
      return true;
    idx[d] = 0;
  }
  return false;
}

bool check_wrapper_name(sema::Symbol const &anchor, str_t const &owner,
                        str_t const &wrapper) {
  if (wrapper.size() <= 63)
    return true;
  flu::emit_error(anchor, "flair-f2py: instantiated wrapper name '" + wrapper +
                              "' exceeds Fortran's 63-char identifier limit; "
                              "annotate '" +
                              owner +
                              "' with a '!flair$ ignore' directive to skip it");
  return false;
}

// Validate the directive carried by `fi` against `dummies` (the vector the
// specific wrappers will parse: post-drop_self for TBPs) and fill `plan`.
// `self_base` is the home type of a pass TBP (its passed object is always a
// polymorphic dimension), null for free functions. Diagnostics are anchored at
// `anchor` (the module function / binding) or the offending listed type.
bool resolve_instantiate(fnt_info_t const &fi, sema::Symbol const &anchor,
                         std::vector<sema::Symbol *> const &dummies,
                         sema::Symbol const *self_base, module_info_t const &m,
                         inst_plan_t &plan) {
  str_t const name = anchor.name().ToString();
  if (fi.instantiate.empty()) {
    flu::emit_error(anchor, "flair-f2py: '!flair$ instantiate' on '" + name +
                                "' lists no types");
    return false;
  }
  for (str_t const &tn : fi.instantiate) {
    auto const it = m.derived_types.find(tn);
    if (it == m.derived_types.end() || it->second.ptr == nullptr) {
      flu::emit_error(anchor, "flair-f2py: instantiate type '" + tn +
                                  "' is not a wrapped derived type of module "
                                  "'" +
                                  m.name + "'");
      return false;
    }
    plan.types.push_back(it->second.ptr);
  }

  for (size_t i = 0; i < dummies.size(); ++i) {
    sema::Symbol const *d = dummies[i];
    auto const *t = d != nullptr ? d->GetType() : nullptr;
    if (t == nullptr || !flu::is_polymorphic(*t))
      continue; // untyped dummies are rejected by parse_args
    // The specifics substitute a non-polymorphic type(t) actual, which a
    // polymorphic dummy only accepts when it is neither a pointer nor an
    // allocatable and has rank 0.
    if (flu::rank_of(*d) > 0 || flu::is_pointer(*d) ||
        flu::is_allocatable(*d)) {
      flu::emit_error(*d, "flair-f2py: cannot instantiate polymorphic dummy '" +
                              d->name().ToString() +
                              "': arrays, pointers and allocatables cannot "
                              "receive a concrete actual; annotate '" +
                              name +
                              "' with a '!flair$ ignore' directive to skip it");
      return false;
    }
    plan.poly_pos.push_back(i);
  }
  if (self_base == nullptr && plan.poly_pos.empty()) {
    flu::emit_error(anchor,
                    "flair-f2py: '!flair$ instantiate' on '" + name +
                        "': no polymorphic (class) dummy argument; remove the "
                        "directive or declare a dummy as class(...)");
    return false;
  }

  // Each class(base_t) position accepts only listed types extending that
  // base (the passed object's base is the home type); class(*) accepts any.
  auto check_base = [&](sema::Symbol const &base, str_t const &dummy) {
    bool ok = true;
    for (auto const *ti : plan.types)
      if (!flu::extends_or_is(*ti, base)) {
        flu::emit_error(
            *ti, "flair-f2py: instantiate type '" + ti->name().ToString() +
                     "' does not extend '" + base.name().ToString() +
                     "' declared for dummy '" + dummy + "' of '" + name + "'");
        ok = false;
      }
    return ok;
  };
  bool ok = true;
  if (self_base != nullptr)
    ok &= check_base(*self_base, "the passed object");
  for (size_t p : plan.poly_pos)
    if (auto const *base = flu::poly_base(*dummies[p]->GetType()))
      ok &= check_base(*base, dummies[p]->name().ToString());
  return ok;
}

// The runtime dispatcher: classify self (TBPs) and each polymorphic argument
// position by tp_name into an integer tag (type's index in `types` + 1), then
// forward `(self, args)` to the combo whose tags all match; TypeError
// otherwise. `arg_pos` holds args-tuple indices (post-drop_self for TBPs).
str_t gen_dispatcher(str_t const &wrapper, str_t const &pyname, bool self_poly,
                     std::vector<size_t> const &arg_pos,
                     std::vector<sema::Symbol const *> const &types,
                     std::vector<combo_t> const &combos,
                     string_pool_t &strings, bool fwd_kwds) {
  str_t decls;
  decls += "        type(PyObject_t), pointer :: pyobj\n";
  decls += "        type(PyTypeObject_t), pointer :: pytype\n";
  if (self_poly)
    decls += "        integer :: tags\n";
  for (size_t p : arg_pos) {
    decls += fmt::format("        type(c_ptr) :: a{}\n", p);
    decls += fmt::format("        integer :: tag{}\n", p);
  }
  str_t const s_err = strings.intern("unexpected argument type for " + pyname);

  auto classify = [&](str_t const &tagvar, str_t const &ind) {
    str_t s = fmt::format("{}call c_f_pointer(pyobj%ob_type, pytype)\n", ind);
    for (size_t t = 0; t < types.size(); ++t) {
      s += fmt::format("{}{} (c_string_eq(pytype%tp_name, \"{}\")) then\n", ind,
                       t == 0 ? "if" : "else if", tp_name_of(*types[t]));
      s += fmt::format("{}    {} = {}\n", ind, tagvar, t + 1);
    }
    s += fmt::format("{}end if\n", ind);
    return s;
  };

  str_t cls;
  if (self_poly) {
    cls += "        tags = 0\n";
    cls += "        call c_f_pointer(self, pyobj)\n";
    cls += classify("tags", "        ");
  }
  for (size_t p : arg_pos) {
    cls += fmt::format(
        "        a{0} = PyTuple_GetItem(args, {0}_c_ptrdiff_t)\n", p);
    cls += fmt::format("        tag{} = 0\n", p);
    cls += fmt::format("        if (c_associated(a{})) then\n", p);
    cls += fmt::format("            call c_f_pointer(a{}, pyobj)\n", p);
    cls += classify(fmt::format("tag{}", p), "            ");
    cls += "        end if\n";
  }

  str_t fwdc;
  for (combo_t const &c : combos) {
    str_t guard;
    size_t d = 0;
    if (self_poly)
      guard = fmt::format("tags == {}", c.idx[d++] + 1);
    for (size_t p : arg_pos) {
      if (!guard.empty())
        guard += " .and. ";
      guard += fmt::format("tag{} == {}", p, c.idx[d++] + 1);
    }
    fwdc += fmt::format("        if ({}) then\n", guard);
    fwdc += fmt::format("            r = {}(self, args{})\n", c.fwd,
                        fwd_kwds ? ", kwds" : "");
    fwdc += "            return\n";
    fwdc += "        end if\n";
  }
  fwdc += fmt::format(
      "        call PyErr_SetString(PyExc_TypeError, c_loc({}))\n", s_err);
  fwdc += "        r = c_null_ptr\n";

  return render(tpl_dispatch, {{"fn", wrapper}, {"body", decls + cls + fwdc}}) +
         "\n";
}

// True if the wrapped type `tsym` declares (or overrides) a binding named
// `pyname` itself, in which case that binding is wrapped normally and must
// not be shadowed by a dispatcher row.
bool has_own_binding(module_info_t const &m, sema::Symbol const &tsym,
                     str_t const &pyname) {
  auto const it = m.derived_types.find(tsym.name().ToString());
  if (it == m.derived_types.end())
    return false;
  for (fnt_info_t const &mth : it->second.methods)
    if (mth.ptr != nullptr && mth.ptr->name().ToString() == pyname)
      return true;
  return false;
}

} // namespace

str_t gen_instantiated_function(fnt_info_t const &fi, module_info_t const &m,
                                string_pool_t &strings, str_t &fills, int &n,
                                ext_types_t &ext_types) {
  sema::Symbol const &fn = *fi.ptr;
  if (!fn.has<sema::SubprogramDetails>())
    return "";
  auto const &dummies = fn.get<sema::SubprogramDetails>().dummyArgs();

  inst_plan_t plan;
  if (!resolve_instantiate(fi, fn, dummies, nullptr, m, plan))
    return "";

  str_t const pyname = fn.name().ToString();
  str_t const dispatcher = fmt::format("py_mod_{}", pyname);
  size_t const dims = plan.poly_pos.size();

  str_t procedures;
  std::vector<combo_t> combos;
  std::vector<size_t> idx(dims, 0);
  int hidden_n = 0;
  do {
    poly_overrides_t ov;
    str_t wname = dispatcher + "_";
    for (size_t d = 0; d < dims; ++d) {
      ov[plan.poly_pos[d]] = plan.types[idx[d]];
      wname += "_" + tname(*plan.types[idx[d]]);
    }
    if (!check_wrapper_name(fn, pyname, wname))
      return "";
    str_t const w = gen_module_function(fn, m, strings, nullptr, hidden_n,
                                        ext_types, {}, wname, &ov);
    if (w.empty())
      return ""; // some other dummy / the result is unsupported: diagnosed
    procedures += w;
    combos.push_back({idx, wname});
  } while (next_combo(idx, plan.types.size()));

  fills += method_row("module_methods", ++n, strings.intern(pyname), dispatcher,
                      "METH_VARARGS + METH_KEYWORDS");
  procedures += gen_dispatcher(dispatcher, pyname, /*self_poly=*/false,
                               plan.poly_pos, plan.types, combos, strings,
                               /*fwd_kwds=*/true);
  return procedures;
}

str_t gen_instantiated_method(dtype_info_t const &home, fnt_info_t const &mth,
                              module_info_t const &m, string_pool_t &strings,
                              std::map<str_t, type_tables_t> &tables,
                              ext_types_t &ext_types) {
  sema::Symbol const &binding = *mth.ptr;
  str_t const pyname = binding.name().ToString();
  str_t const home_tn = tname(*home.ptr);
  if (binding.attrs().test(sema::Attr::NOPASS)) {
    flu::emit_error(binding, "flair-f2py: '!flair$ instantiate' on '" +
                                 home_tn + "%" + pyname +
                                 "' requires a pass type-bound procedure");
    return "";
  }
  sema::Symbol const *actual = flu::binding_actual(binding);
  if (actual == nullptr || !actual->has<sema::SubprogramDetails>())
    return "";
  std::vector<sema::Symbol *> const args =
      drop_self(actual->get<sema::SubprogramDetails>().dummyArgs());

  inst_plan_t plan;
  if (!resolve_instantiate(mth, binding, args, home.ptr, m, plan))
    return "";

  str_t const dispatcher = fmt::format("py_{}_{}", home_tn, pyname);
  size_t const dims = 1 + plan.poly_pos.size(); // self is dimension 0

  str_t procedures;
  std::vector<combo_t> combos;
  std::vector<size_t> idx(dims, 0);
  int hidden_n = 0;
  do {
    poly_overrides_t ov;
    str_t wname = dispatcher + "_";
    wname += "_" + tname(*plan.types[idx[0]]);
    for (size_t d = 1; d < dims; ++d) {
      ov[plan.poly_pos[d - 1]] = plan.types[idx[d]];
      wname += "_" + tname(*plan.types[idx[d]]);
    }
    if (!check_wrapper_name(binding, home_tn, wname))
      return "";
    str_t const w = gen_method(home, binding, m, strings, nullptr, hidden_n,
                               ext_types, plan.types[idx[0]], &ov, wname);
    if (w.empty())
      return "";
    procedures += w;
    combos.push_back({idx, wname});
  } while (next_combo(idx, plan.types.size()));

  // No-arg specifics keep the two-parameter METH_NOARGS signature.
  procedures += gen_dispatcher(dispatcher, pyname, /*self_poly=*/true,
                               plan.poly_pos, plan.types, combos, strings,
                               /*fwd_kwds=*/!args.empty());

  str_t const flags =
      args.empty() ? "METH_NOARGS" : "METH_VARARGS + METH_KEYWORDS";
  for (sema::Symbol const *ti : plan.types) {
    if (ti != home.ptr && has_own_binding(m, *ti, pyname))
      continue;
    str_t const tn = tname(*ti);
    type_tables_t &tt = tables[tn];
    tt.method_fills += method_row(tn + "_methods", ++tt.nm,
                                  strings.intern(pyname), dispatcher, flags);
  }
  return procedures;
}

} // namespace codegen
