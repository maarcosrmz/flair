#include "package.hpp"

#include <fmt/core.h>
#include <fmt/format.h>

#include "utils.hpp"

namespace codegen {

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc23-extensions"
static constexpr char tpl_package[] = {
#embed "templates/package.txt"
    , '\0'};
#pragma clang diagnostic pop

str_t codegen_package(str_t const &pkg, std::vector<str_t> const &submods) {
  string_pool_t strings;

  str_t init_ifaces;
  for (auto const &m : submods)
    init_ifaces += fmt::format("        function flair_init_{0}() bind(C, "
                               "name=\"FLAIR_init_{0}\") result(r)\n"
                               "            import :: c_ptr\n"
                               "            type(c_ptr) :: r\n"
                               "        end function\n",
                               m);

  str_t const md = pkg + "_pkg_moddef";
  str_t moddef_fills;
  moddef_fills += fmt::format("        {0}%m_name     = c_loc({1})\n", md,
                              strings.intern(pkg));
  moddef_fills += fmt::format("        {0}%m_doc      = c_null_ptr\n", md);
  moddef_fills += fmt::format("        {0}%m_size     = -1_c_ptrdiff_t\n", md);
  moddef_fills +=
      fmt::format("        {0}%m_methods  = c_loc(pkg_methods(1))\n", md);
  moddef_fills += fmt::format("        {0}%m_slots    = c_null_ptr\n", md);
  moddef_fills += fmt::format("        {0}%m_traverse = c_null_ptr\n", md);
  moddef_fills += fmt::format("        {0}%m_clear    = c_null_ptr\n", md);
  moddef_fills += fmt::format("        {0}%m_free     = c_null_ptr\n", md);

  str_t submodules;
  for (auto const &m : submods) {
    str_t const s_name = strings.intern(m);
    str_t const s_qual = strings.intern(pkg + "." + m);
    submodules += fmt::format("        ! --- {} ---\n", m);
    submodules += fmt::format("        sub = flair_init_{}()\n", m);
    submodules += "        if (.not. c_associated(sub)) then\n"
                  "            call Py_DecRef(pkg_ptr)\n"
                  "            return\n"
                  "        end if\n";
    submodules += fmt::format(
        "        rc = PyModule_AddObjectRef(pkg_ptr, c_loc({}), sub)\n",
        s_name);
    submodules += fmt::format("        if (rc == 0) rc = "
                              "PyDict_SetItemString(modules_dict, c_loc({}), "
                              "sub)\n",
                              s_qual);
    submodules += "        call Py_DecRef(sub)\n"
                  "        if (rc /= 0) then\n"
                  "            call Py_DecRef(pkg_ptr)\n"
                  "            return\n"
                  "        end if\n\n";
  }

  return render(tpl_package,
                {
                    {"pkg", pkg},
                    {"cstrings", strings.decls()},
                    {"init_ifaces", init_ifaces},
                    {"method_fills", method_sentinel("pkg_methods", 1)},
                    {"moddef_fills", moddef_fills},
                    {"submodules", submodules},
                });
}

} // namespace codegen
