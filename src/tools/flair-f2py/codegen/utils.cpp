#include "utils.hpp"

#include <algorithm>
#include <cctype>
#include <utility>

#include <fmt/core.h>
#include <fmt/format.h>

namespace codegen {

using namespace Fortran;

str_t fold_lower(str_t s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return s;
}

static str_t lower(str_t s) { return fold_lower(std::move(s)); }

str_t from_pyobject_fn(str_t const &type_name) {
  return fold_lower(type_name) + "_from_PyObject";
}

str_t view_pyobject_fn(str_t const &type_name) {
  return fold_lower(type_name) + "_view_PyObject";
}

bool ends_with(str_t const &s, str_t const &suf) {
  return s.size() >= suf.size() &&
         s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
}

str_t render(str_t tpl, std::initializer_list<std::pair<str_t, str_t>> subs) {
  for (auto const &[key, val] : subs) {
    str_t const hole = "{" + key + "}";
    for (size_t pos = tpl.find(hole); pos != str_t::npos;
         pos = tpl.find(hole, pos + val.size()))
      tpl.replace(pos, hole.size(), val);
  }
  return tpl;
}

str_t string_pool_t::intern(str_t const &lit) {
  if (auto it = seen.find(lit); it != seen.end())
    return it->second;
  str_t var = fmt::format("s_{}", entries.size());
  seen.emplace(lit, var);
  entries.emplace_back(var, lit);
  return var;
}

str_t string_pool_t::decls() const {
  str_t out;
  for (auto const &[var, lit] : entries)
    out += fmt::format("    character(kind=c_char, len={}), target, save :: {} "
                       "= \"{}\"//c_null_char\n",
                       lit.size() + 1, var, lit);
  return out;
}

str_t tname(semantics::Symbol const &s) { return lower(s.name().ToString()); }

// Python class name: folded Fortran name with an uppercase initial (PEP 8),
// e.g. "point" -> "Point".
str_t clsname(semantics::Symbol const &s) {
  str_t n = tname(s);
  if (!n.empty())
    n[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(n[0])));
  return n;
}

str_t struct_name(semantics::Symbol const &s) {
  return "py_" + tname(s) + "_object_t";
}
str_t ptr_field(semantics::Symbol const &s) { return tname(s) + "_ptr"; }

str_t method_row(str_t const &tbl, int idx, str_t const &name_var,
                 str_t const &wrapper, str_t const &flags) {
  return fmt::format(
      "        {0}({1})%ml_name  = c_loc({2})\n"
      "        {0}({1})%ml_meth  = transfer(c_funloc({3}), c_null_ptr)\n"
      "        {0}({1})%ml_flags = {4}\n"
      "        {0}({1})%ml_pad   = 0\n"
      "        {0}({1})%ml_doc   = c_null_ptr\n",
      tbl, idx, name_var, wrapper, flags);
}

str_t method_sentinel(str_t const &tbl, int idx) {
  return fmt::format("        {0}({1})%ml_name  = c_null_ptr\n"
                     "        {0}({1})%ml_meth  = c_null_ptr\n"
                     "        {0}({1})%ml_flags = 0\n"
                     "        {0}({1})%ml_pad   = 0\n"
                     "        {0}({1})%ml_doc   = c_null_ptr\n",
                     tbl, idx);
}

str_t getset_row(str_t const &tbl, int idx, str_t const &name_var,
                 str_t const &getter, str_t const &setter) {
  return fmt::format(
      "        {0}({1})%name    = c_loc({2})\n"
      "        {0}({1})%get     = transfer(c_funloc({3}), c_null_ptr)\n"
      "        {0}({1})%set     = transfer(c_funloc({4}), c_null_ptr)\n"
      "        {0}({1})%doc     = c_null_ptr\n"
      "        {0}({1})%closure = c_null_ptr\n",
      tbl, idx, name_var, getter, setter);
}

str_t getset_sentinel(str_t const &tbl, int idx) {
  return fmt::format("        {0}({1})%name    = c_null_ptr\n"
                     "        {0}({1})%get     = c_null_ptr\n"
                     "        {0}({1})%set     = c_null_ptr\n"
                     "        {0}({1})%doc     = c_null_ptr\n"
                     "        {0}({1})%closure = c_null_ptr\n",
                     tbl, idx);
}

} // namespace codegen
