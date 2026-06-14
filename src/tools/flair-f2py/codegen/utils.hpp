#pragma once

#include <initializer_list>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include <flang/Semantics/symbol.h>

namespace codegen {

using str_t = std::string;

// True if `s` ends with `suf`.
bool ends_with(str_t const &s, str_t const &suf);

// Fill `{key}` holes in a template. Generated Fortran contains no other braces,
// so plain delimited substitution is unambiguous.
str_t render(str_t tpl, std::initializer_list<std::pair<str_t, str_t>> subs);

// Persistent, deduplicated null-terminated C strings. Each becomes a
// `character(..., target, save)` so c_loc stays valid for the module lifetime.
struct string_pool_t {
  std::vector<std::pair<str_t, str_t>> entries; // (var, literal)
  std::map<str_t, str_t> seen;

  str_t intern(str_t const &lit);
  str_t decls() const;
};

// Wrapper names derived from a flang type Symbol.
str_t tname(Fortran::semantics::Symbol const &s);       // folded lowercase, e.g. "point"
str_t clsname(Fortran::semantics::Symbol const &s);     // Python class name, e.g. "Point"
str_t struct_name(Fortran::semantics::Symbol const &s); // "py_<t>_object_t"
str_t ptr_field(Fortran::semantics::Symbol const &s);   // "<t>_ptr"

// PyInit table-fill rows.
str_t method_row(str_t const &tbl, int idx, str_t const &name_var, str_t const &wrapper, str_t const &flags);
str_t method_sentinel(str_t const &tbl, int idx);
str_t getset_row(str_t const &tbl, int idx, str_t const &name_var, str_t const &getter, str_t const &setter);
str_t getset_sentinel(str_t const &tbl, int idx);

} // namespace codegen
