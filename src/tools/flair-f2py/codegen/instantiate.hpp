#pragma once

#include <map>

#include "../wdata.hpp"
#include "functions.hpp" // ext_types_t, poly_overrides_t
#include "utils.hpp"

namespace codegen {

// Method/getset table accumulators of one derived type. Kept per type across
// the whole derived-type loop because an '!flair$ instantiate'd type-bound
// procedure contributes its dispatcher row to *several* types' method tables
// (generated Python classes do not inherit, so each instantiated type must
// carry the method itself).
struct type_tables_t {
  str_t method_fills;
  int nm = 0;
  str_t getset_fills;
  int ng = 0;
  // spec/create fills are rendered (and their strings interned) in the first
  // pass to keep the string pool in per-type order; emitted in the second.
  str_t spec;
  str_t create;
};

// '!flair$ instantiate'd free module function: one hidden specific wrapper per
// element of the cartesian product of the listed types over the polymorphic
// dummy positions, plus a dispatcher exposed under the function's own name
// that classifies those positions by runtime tp_name and forwards to the
// matching specific (TypeError on no match). Appends the dispatcher's
// module-table row to `fills` (bumps `n`). "" (after emitted diagnostics) if
// the directive or any specific is invalid.
str_t gen_instantiated_function(fnt_info_t const &fi, module_info_t const &m,
                                string_pool_t &strings, str_t &fills, int &n,
                                ext_types_t &ext_types);

// '!flair$ instantiate'd type-bound procedure of `home`: the passed-object
// self is always one polymorphic dimension (plus any polymorphic dummies), so
// each combo yields a hidden specific unwrapping self as the concrete type.
// The dispatcher is registered in every listed type's method table, except
// where a type declares its own same-named binding (wrapped normally; a
// second row would shadow it). "" (after emitted diagnostics) on failure.
str_t gen_instantiated_method(dtype_info_t const &home, fnt_info_t const &mth,
                              module_info_t const &m, string_pool_t &strings,
                              std::map<str_t, type_tables_t> &tables,
                              ext_types_t &ext_types);

} // namespace codegen
