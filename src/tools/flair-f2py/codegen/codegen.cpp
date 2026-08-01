#include "codegen.hpp"

#include <unordered_set>

#include <fmt/core.h>
#include <fmt/format.h>

#include "flang/Semantics/symbol.h"

#include "dtypes.hpp"
#include "flu/diagnostics.hpp"
#include "flu/symbols.hpp"
#include "functions.hpp"
#include "instantiate.hpp"
#include "interfaces.hpp"
#include "modvars.hpp"
#include "utils.hpp"

namespace codegen {

using namespace Fortran;

// ===========================================================================
// Module skeleton template (compiled in via C23 #embed; works at C++17 with
// clang).
// ===========================================================================
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc23-extensions"
static constexpr char tpl_module[] = {
#embed "templates/module.txt"
    , '\0'};
#pragma clang diagnostic pop

str_t module_pyname(str_t const &m) {
  // TODO: Add command line arg to specify suffix to ignore
  // e.g. in Octopus '_oct_m'
  if (ends_with(m, "_mod"))
    return m.substr(0, m.size() - 4);
  return m;
}

// Run-global: the prefix is a property of the whole invocation, and the sites
// that need it (module def, type specs, the dispatchers' tp_name literals)
// sit at very different depths of the codegen call tree.
static str_t package_prefix;

void set_package_prefix(str_t const &pkg) { package_prefix = pkg; }

str_t module_pyqual(str_t const &m) {
  str_t const modpy = module_pyname(m);
  return package_prefix.empty() ? modpy : package_prefix + "." + modpy;
}

bool has_wrappable(module_info_t const &m) {
  return !m.derived_types.empty() || !m.functions.empty() ||
         !m.variables.empty();
}

str_t codegen_module(module_info_t const &m_in, bool internal_init) {
  str_t const modpy = module_pyname(m_in.name);
  // What python sees; differs from modpy only in a combined package build.
  str_t const pyqual = module_pyqual(m_in.name);
  string_pool_t strings;
  std::unordered_set<sym_ptr_t>
      bound;             // type-bound actuals, excluded from module functions
  ext_types_t ext_types; // derived types wrapped elsewhere, referenced by this
                         // module's procedures (folded name -> defining symbol)

  // Local copy so unwrappable types can be dropped: everything downstream then
  // classifies references to them as Unsupported (existing error+skip paths)
  // instead of pointing at a type object / converters that are never generated.
  module_info_t m = m_in;

  // ---- validate constructors ------------------------------------------------
  // A type whose ctor generic can't be wrapped faithfully (overloaded, or a
  // specific with unsupported dummies) is skipped entirely: every ctor arg is
  // required, so no correct __init__ can be generated for it.
  for (auto it = m.derived_types.begin(); it != m.derived_types.end();) {
    dtype_info_t const &dt = it->second;
    std::vector<sema::Symbol const *> kwargs;
    if (dt.ptr != nullptr && dt.ctor.ptr != nullptr &&
        !ctor_kwargs(dt, m, ext_types, kwargs)) {
      flu::emit_error(*dt.ptr,
                      "flair-f2py: cannot wrap derived type '" + it->first +
                          "': its constructor interface is not wrappable "
                          "(overloaded or unsupported arguments); annotate the "
                          "type '" +
                          it->first +
                          "' and its constructor interface with '!flair$ "
                          "ignore' directives to skip it");
      it = m.derived_types.erase(it);
    } else {
      ++it;
    }
  }

  str_t procedures, table_decls, pyinit_decls, pyinit_fills, pyinit_creates;

  // Every wrapper instance has the same layout, so one dummy of the shared
  // runtime type supplies the basicsize for every type spec.
  if (!m.derived_types.empty())
    pyinit_decls +=
        fmt::format("        type({}) :: dummy_obj\n", obj_struct);

  // ---- derived types -------------------------------------------------------
  // Two passes over the types: the first generates the procedures and
  // accumulates each type's method/getset rows, the second emits the (now
  // fully sized) tables and PyInit fills. The rows of one type cannot be
  // finalized inside its own iteration because an '!flair$ instantiate'd
  // type-bound procedure registers its dispatcher in every listed type's
  // method table.
  std::map<str_t, type_tables_t> tables;
  for (auto const &[name, dt] : m.derived_types) {
    if (dt.ptr == nullptr)
      continue;
    str_t const tn = tname(*dt.ptr);

    sema::SymbolVector fields = public_fields(dt, m);
    procedures += gen_lifecycle(dt, m, strings, ext_types) + "\n";

    for (fnt_info_t const &mth : dt.methods) {
      if (mth.ptr == nullptr)
        continue;
      if (auto const *act = flu::binding_actual(*mth.ptr))
        bound.insert(act);
      if (!mth.instantiate.empty())
        procedures +=
            gen_instantiated_method(dt, mth, m, strings, tables, ext_types);
      else
        procedures +=
            gen_method(dt, *mth.ptr, m, strings, &tables[tn].method_fills,
                       tables[tn].nm, ext_types);
    }

    for (const sema::Symbol &f : fields)
      procedures += gen_getset(dt, f, m, strings, tables[tn].getset_fills,
                               tables[tn].ng, ext_types);

    tables[tn].spec = spec_fills(tn, clsname(*dt.ptr), pyqual, strings);
    tables[tn].create = create_fills(tn, clsname(*dt.ptr), strings);
  }

  for (auto const &[name, dt] : m.derived_types) {
    if (dt.ptr == nullptr)
      continue;
    semantics::Symbol const &tsym = *dt.ptr;
    str_t const tn = tname(tsym);
    type_tables_t const &tt = tables[tn];

    str_t const method_fills =
        tt.method_fills + method_sentinel(tn + "_methods", tt.nm + 1);
    str_t const getset_fills =
        tt.getset_fills + getset_sentinel(tn + "_getset", tt.ng + 1);

    table_decls +=
        fmt::format("    type(PyMethodDef_t), target, save :: {}_methods({})\n",
                    tn, tt.nm + 1);
    table_decls +=
        fmt::format("    type(PyGetSetDef_t), target, save :: {}_getset({})\n",
                    tn, tt.ng + 1);
    table_decls += fmt::format(
        "    type(PyType_Slot_t), target, save :: {}_slots(6)\n", tn);
    table_decls +=
        fmt::format("    type(PyType_Spec_t), target, save :: {}_spec\n", tn);
    table_decls += fmt::format(
        "    type(c_ptr), save :: py_{}_type_obj = c_null_ptr\n", tn);

    pyinit_fills +=
        fmt::format("        ! --- {} method table ---\n", tn) + method_fills;
    pyinit_fills +=
        fmt::format("        ! --- {} getset table ---\n", tn) + getset_fills;
    pyinit_fills += slot_fills(tn);
    pyinit_fills += tt.spec;
    pyinit_creates += tt.create;
  }

  // Interface specifics are exposed only through their generic's dispatcher, so
  // exclude them from the standalone module-function loop (a public specific
  // would otherwise be wrapped twice -> duplicate definition).
  for (auto const &iface : m.interfaces) {
    if (iface.ptr == nullptr)
      continue;
    for (auto const &proc : flu::get_specific_procs(*iface.ptr))
      bound.insert(&static_cast<semantics::Symbol const &>(proc));
  }

  // ---- module-level functions (excluding type-bound actuals) ---------------
  str_t modfn_fills;
  int nmod = 0;
  for (auto const &fn : m.functions) {
    if (fn.ptr == nullptr || bound.count(fn.ptr))
      continue;
    if (!fn.instantiate.empty())
      procedures += gen_instantiated_function(fn, m, strings, modfn_fills, nmod,
                                              ext_types);
    else
      procedures += gen_module_function(*fn.ptr, m, strings, &modfn_fills, nmod,
                                        ext_types);
  }
  for (auto const &iface : m.interfaces) {
    if (iface.ptr == nullptr /* TODO: or bound to dtype (?) */)
      continue;

    // Generate the specific-procedure wrappers (internal helpers, not exposed),
    // remembering which actually generated so the interface wrapper only
    // dispatches to reachable ones. Each calls the public generic name so that
    // resolution picks the specific from the typed actuals even when the
    // specific itself is private.
    str_t const gname = iface.ptr->name().ToString();
    sema::SymbolVector specific_procs = flu::get_specific_procs(*iface.ptr);
    std::vector<semantics::Symbol const *> generated;
    for (auto const &proc : specific_procs) {
      semantics::Symbol const &ps = proc;
      str_t const w =
          gen_module_function(ps, m, strings, nullptr, nmod, ext_types, gname);
      if (!w.empty()) {
        procedures += w;
        generated.push_back(&ps);
      }
    }
    // ...then the dispatching wrapper exposed under the generic's name.
    procedures += gen_interface_wrapper(*iface.ptr, generated, strings,
                                        &modfn_fills, nmod);
  }

  // ---- module variables (live views / __getattr__ values) -------------------
  str_t var_init_decls, var_creates;
  procedures += gen_module_vars(m, strings, &modfn_fills, nmod, ext_types,
                                var_init_decls, var_creates);

  modfn_fills += method_sentinel("module_methods", nmod + 1);

  table_decls += fmt::format(
      "    type(PyMethodDef_t), target, save :: module_methods({})\n",
      nmod + 1);
  table_decls += fmt::format(
      "    type(PyModuleDef_t), target, save :: {}_moddef\n", modpy);

  // ---- module method table + module def fills ------------------------------
  str_t const md = modpy + "_moddef";
  pyinit_fills += "        ! --- module method table ---\n" + modfn_fills;
  pyinit_fills += "        ! --- module def ---\n";
  pyinit_fills += fmt::format("        {0}%m_name     = c_loc({1})\n", md,
                              strings.intern(pyqual));
  pyinit_fills += fmt::format("        {0}%m_doc      = c_null_ptr\n", md);
  pyinit_fills += fmt::format("        {0}%m_size     = -1_c_ptrdiff_t\n", md);
  pyinit_fills +=
      fmt::format("        {0}%m_methods  = c_loc(module_methods(1))\n", md);
  pyinit_fills += fmt::format("        {0}%m_slots    = c_null_ptr\n", md);
  pyinit_fills += fmt::format("        {0}%m_traverse = c_null_ptr\n", md);
  pyinit_fills += fmt::format("        {0}%m_clear    = c_null_ptr\n", md);
  pyinit_fills += fmt::format("        {0}%m_free     = c_null_ptr\n", md);

  // ---- assemble PyInit -----------------------------------------------------
  // Combined-package builds export the init as FLAIR_init_<modpy>: the single
  // .so may expose only one PyInit (the package's), which calls these.
  str_t pyinit;
  pyinit += internal_init ? fmt::format("    function flair_init_{0}() bind(C, "
                                        "name=\"FLAIR_init_{0}\") result(r)\n",
                                        modpy)
                          : fmt::format("    function PyInit_{0}() bind(C, "
                                        "name=\"PyInit_{0}\") result(r)\n",
                                        modpy);
  pyinit += "        type(c_ptr) :: r\n";
  pyinit += "        type(c_ptr) :: mod_ptr, type_ptr\n";
  pyinit += "        integer(c_int) :: rc\n";
  pyinit += pyinit_decls;
  pyinit += var_init_decls;
  pyinit += "\n        if (import_array() < 0) then\n            r = "
            "c_null_ptr\n            return\n        end if\n\n";
  pyinit += pyinit_fills;
  pyinit += fmt::format(
      "\n        mod_ptr = PyModule_Create2(c_loc({}), PYTHON_ABI_VERSION)\n",
      md);
  pyinit += "        if (.not. c_associated(mod_ptr)) then\n            r = "
            "c_null_ptr\n            return\n        end if\n\n";
  pyinit += pyinit_creates;
  pyinit += var_creates;
  pyinit += "        r = mod_ptr\n";
  pyinit += "    end function\n";

  // ---- imports for types wrapped in other files ----------------------------
  // The only-import of the wrapped module makes the foreign type name
  // available for wrapper locals regardless of that module's default
  // accessibility. The converters are module procedures of the producer's
  // wrapper module, so this wrapper needs the producer wrapper's .mod at
  // compile time and its object at link time (never duplicate its objects:
  // each copy has its own type state, and the converters' not-initialized
  // guard would then fire forever).
  str_t imports;
  for (auto const &[n, tsym] : ext_types) {
    str_t const owner = fold_lower(flu::owning_module_name(*tsym));
    imports += fmt::format("    use {}, only: {}\n", owner, n);
    imports += fmt::format("    use py_{}_mod, only: {}, {}\n",
                           module_pyname(owner), from_pyobject_fn(n),
                           view_pyobject_fn(n));
  }

  // ---- converter definitions for this module's wrapped types ---------------
  // from: isinstance-checked unwrap; sets the exception and returns a
  //       disassociated pointer on failure.
  // view: wraps a component address + owning PyObject into a new view instance
  //       (numpy 'base' pattern; takes a reference on the owner).
  // Module procedures, use-associated by the wrappers of consuming modules.
  // Both guard against the module not being initialized yet (its PyInit sets
  // py_<t>_type_obj): the consumer module must be imported after this one.
  str_t converters;
  if (!m.derived_types.empty()) {
    str_t const s_noinit =
        strings.intern("python module '" + pyqual +
                       "' is not initialized; import it before "
                       "using its wrapped types");
    for (auto const &[name, dt] : m.derived_types) {
      if (dt.ptr == nullptr)
        continue;
      semantics::Symbol const &tsym = *dt.ptr;
      str_t const tn = tname(tsym);
      str_t const s_type =
          strings.intern("expected a " + clsname(tsym) + " instance");
      str_t const guard =
          fmt::format("        if (.not. c_associated(py_{}_type_obj)) then\n"
                      "            call PyErr_SetString(PyExc_RuntimeError, "
                      "c_loc({}))\n            return\n        end if\n",
                      tn, s_noinit);

      converters +=
          fmt::format("    function {}(p) result(r)\n", from_pyobject_fn(tn));
      converters += "        type(c_ptr), value :: p\n";
      converters += fmt::format("        type({}), pointer :: r\n", tn);
      converters +=
          fmt::format("        type({}), pointer :: obj\n", obj_struct);
      converters += "        r => null()\n";
      converters += guard;
      converters += fmt::format(
          "        if (PyObject_IsInstance(p, py_{}_type_obj) /= 1) then\n",
          tn);
      converters += "            ! IsInstance may return -1 with its own "
                    "exception set; don't clobber it\n";
      converters +=
          "            if (.not. c_associated(PyErr_Occurred())) then\n";
      converters += fmt::format(
          "                call PyErr_SetString(PyExc_TypeError, c_loc({}))\n",
          s_type);
      converters += "            end if\n";
      converters += "            return\n";
      converters += "        end if\n";
      converters += "        call c_f_pointer(p, obj)\n";
      converters +=
          str_t("        call c_f_pointer(obj%data, r)\n");
      converters += "    end function\n\n";

      converters += fmt::format("    function {}(data, owner) result(r)\n",
                                view_pyobject_fn(tn));
      converters += "        type(c_ptr), value :: data, owner\n";
      converters += "        type(c_ptr) :: r\n";
      converters +=
          fmt::format("        type({}), pointer :: obj\n", obj_struct);
      converters += "        r = c_null_ptr\n";
      converters += guard;
      converters += fmt::format(
          "        r = PyType_GenericAlloc(py_{}_type_obj, 0_c_ptrdiff_t)\n",
          tn);
      converters += "        if (.not. c_associated(r)) return\n";
      converters += "        call c_f_pointer(r, obj)\n";
      converters += "        obj%data = data\n";
      converters += "        obj%owner = owner\n";
      converters += "        call Py_IncRef(owner)\n";
      converters += "    end function\n\n";
    }
    if (!converters.empty())
      converters = "    ! ===== PyObject converters for this module's wrapped "
                   "types (use-associated by other wrappers) =====\n" +
                   converters;
  }

  return render(tpl_module, {
                                {"modpy", modpy},
                                {"wrapped_module", m.name},
                                {"imports", imports},
                                {"cstrings", strings.decls()},
                                {"tables", table_decls},
                                {"procedures", procedures},
                                {"pyinit", pyinit},
                                {"converters", converters},
                            });
}

} // namespace codegen
