#pragma once

#include "flang/Semantics/symbol.h"

#include "../wdata.hpp"
#include "functions.hpp" // ext_types_t, classify_dtype (shared with methods)
#include "utils.hpp"

namespace sema = Fortran::semantics;

namespace codegen {

// Public intrinsic scalar / rank-1-numeric-array / inline wrapped derived-type
// (same-module or foreign) components, in declaration order.
sema::SymbolVector public_fields(dtype_info_t const &dt,
                                 module_info_t const &m);

// `type, bind(C) :: py_<t>_object_t` instance layout.
str_t gen_object_struct(dtype_info_t const &dt);

// Resolve the ctor generic's specific and collect its dummies as __init__
// kwargs into `out`. False if the ctor cannot be wrapped faithfully: not
// exactly one specific, a specific that isn't a function, or an unsupported
// dummy (codegen_module then skips the whole type).
bool ctor_kwargs(dtype_info_t const &dt, module_info_t const &m,
                 ext_types_t &ext_types,
                 std::vector<sema::Symbol const *> &out);

// new / init / dealloc lifecycle procedures.
str_t gen_lifecycle(dtype_info_t const &dt, module_info_t const &m,
                    string_pool_t &strings, ext_types_t &ext_types);

// One type-bound method. Appends its PyInit method-table row to `fills` (bumps
// `n`); a null `fills` generates the wrapper without exposing it ("" instead
// of a placeholder comment if skipped). `self_type`, if non-null, replaces the
// home type for the self unwrap (a `type(self_type), pointer` receiver): used
// by instantiate specifics, together with `overrides` (post-drop_self indices,
// forwarded to parse_args) and `wrapper_name` (overrides "py_<t>_<binding>").
str_t gen_method(dtype_info_t const &dt, sema::Symbol const &binding,
                 module_info_t const &m, string_pool_t &strings, str_t *fills,
                 int &n, ext_types_t &ext_types,
                 sema::Symbol const *self_type = nullptr,
                 poly_overrides_t const *overrides = nullptr,
                 str_t const &wrapper_name = {});

// One getset property (scalar, rank-1 numpy array, or derived-type view).
// Appends its getset-table row.
str_t gen_getset(dtype_info_t const &dt, sema::Symbol const &comp,
                 module_info_t const &m, string_pool_t &strings, str_t &fills,
                 int &n, ext_types_t &ext_types);

// PyInit slot / spec / type-creation fill statements (pure functions of the
// names).
str_t slot_fills(str_t const &tn);
str_t spec_fills(str_t const &tn, str_t const &cls, str_t const &modpy,
                 string_pool_t &strings);
str_t create_fills(str_t const &tn, str_t const &cls, string_pool_t &strings);

} // namespace codegen
