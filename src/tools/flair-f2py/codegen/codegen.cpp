#include "codegen.hpp"

#include <set>

#include <fmt/core.h>
#include <fmt/format.h>

#include <flang/Semantics/symbol.h>

#include "dtypes.hpp"
#include "functions.hpp"
#include "flu/symbols.hpp"
#include "utils.hpp"

namespace codegen {

using namespace Fortran;

// ===========================================================================
// Module skeleton template (compiled in via C23 #embed; works at C++17 with clang).
// ===========================================================================
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc23-extensions"
static constexpr char tpl_module[] = {
#embed "templates/module.txt"
  , '\0'};
#pragma clang diagnostic pop

str_t module_pyname(str_t const &m) {
  if (ends_with(m, "_mod")) return m.substr(0, m.size() - 4);
  return m;
}

bool has_wrappable(module_info_t const &m) {
  return !m.derived_types.empty() || !m.functions.empty();
}

str_t codegen_module(module_info_t const &m) {
  str_t const modpy = module_pyname(m.name);
  string_pool_t strings;
  std::set<sym_ptr_t> bound; // type-bound actuals, excluded from module functions

  str_t structs, procedures, table_decls, pyinit_decls, pyinit_fills, pyinit_creates;

  // ---- derived types -------------------------------------------------------
  for (auto const &[name, dt] : m.derived_types) {
    if (dt.ptr == nullptr) continue;
    semantics::Symbol const &tsym = *dt.ptr;
    str_t const tn = tname(tsym), cls = clsname(tsym);

    structs += gen_object_struct(dt);

    std::vector<sym_ptr_t> fields = public_fields(dt);
    procedures += gen_lifecycle(dt, fields, strings) + "\n";

    str_t method_fills;
    int nm = 0;
    for (fnt_info_t const &mth : dt.methods) {
      if (mth.ptr == nullptr) continue;
      if (auto const *act = flu::binding_actual(*mth.ptr)) bound.insert(act);
      procedures += gen_method(dt, *mth.ptr, m, strings, method_fills, nm);
    }
    method_fills += method_sentinel(tn + "_methods", nm + 1);

    str_t getset_fills;
    int ng = 0;
    for (sym_ptr_t f : fields)
      procedures += gen_getset(dt, *f, strings, getset_fills, ng);
    getset_fills += getset_sentinel(tn + "_getset", ng + 1);

    table_decls += fmt::format("    type(PyMethodDef_t), target, save :: {}_methods({})\n", tn, nm + 1);
    table_decls += fmt::format("    type(PyGetSetDef_t), target, save :: {}_getset({})\n", tn, ng + 1);
    table_decls += fmt::format("    type(PyType_Slot_t), target, save :: {}_slots(6)\n", tn);
    table_decls += fmt::format("    type(PyType_Spec_t), target, save :: {}_spec\n", tn);

    pyinit_decls   += fmt::format("        type({}) :: dummy_{}\n", struct_name(tsym), tn);
    pyinit_fills   += fmt::format("        ! --- {} method table ---\n", tn) + method_fills;
    pyinit_fills   += fmt::format("        ! --- {} getset table ---\n", tn) + getset_fills;
    pyinit_fills   += slot_fills(tn);
    pyinit_fills   += spec_fills(tn, cls, modpy, strings);
    pyinit_creates += create_fills(tn, cls, strings);
  }

  // ---- module-level functions (excluding type-bound actuals) ---------------
  str_t modfn_fills;
  int nmod = 0;
  for (auto const &fn : m.functions) {
    if (fn.ptr == nullptr || bound.count(fn.ptr)) continue;
    procedures += gen_module_function(*fn.ptr, m, strings, modfn_fills, nmod);
  }
  modfn_fills += method_sentinel("module_methods", nmod + 1);

  table_decls += fmt::format("    type(PyMethodDef_t), target, save :: module_methods({})\n", nmod + 1);
  table_decls += fmt::format("    type(PyModuleDef_t), target, save :: {}_moddef\n", modpy);

  // ---- module method table + module def fills ------------------------------
  str_t const md = modpy + "_moddef";
  pyinit_fills += "        ! --- module method table ---\n" + modfn_fills;
  pyinit_fills += "        ! --- module def ---\n";
  pyinit_fills += fmt::format("        {0}%m_name     = c_loc({1})\n", md, strings.intern(modpy));
  pyinit_fills += fmt::format("        {0}%m_doc      = c_null_ptr\n", md);
  pyinit_fills += fmt::format("        {0}%m_size     = -1_c_ptrdiff_t\n", md);
  pyinit_fills += fmt::format("        {0}%m_methods  = c_loc(module_methods(1))\n", md);
  pyinit_fills += fmt::format("        {0}%m_slots    = c_null_ptr\n", md);
  pyinit_fills += fmt::format("        {0}%m_traverse = c_null_ptr\n", md);
  pyinit_fills += fmt::format("        {0}%m_clear    = c_null_ptr\n", md);
  pyinit_fills += fmt::format("        {0}%m_free     = c_null_ptr\n", md);

  // ---- assemble PyInit -----------------------------------------------------
  str_t pyinit;
  pyinit += fmt::format("    function PyInit_{0}() bind(C, name=\"PyInit_{0}\") result(r)\n", modpy);
  pyinit += "        type(c_ptr) :: r\n";
  pyinit += "        type(c_ptr) :: mod_ptr, type_ptr\n";
  pyinit += "        integer(c_int) :: rc\n";
  pyinit += pyinit_decls;
  pyinit += "\n        if (import_array() < 0) then\n            r = c_null_ptr\n            return\n        end if\n\n";
  pyinit += pyinit_fills;
  pyinit += fmt::format("\n        mod_ptr = PyModule_Create2(c_loc({}), PYTHON_ABI_VERSION)\n", md);
  pyinit += "        if (.not. c_associated(mod_ptr)) then\n            r = c_null_ptr\n            return\n        end if\n\n";
  pyinit += pyinit_creates;
  pyinit += "        r = mod_ptr\n";
  pyinit += "    end function\n";

  return render(tpl_module, {
    {"modpy", modpy},
    {"wrapped_module", m.name},
    {"structs", structs},
    {"cstrings", strings.decls()},
    {"tables", table_decls},
    {"procedures", procedures},
    {"pyinit", pyinit},
  });
}

} // namespace codegen
