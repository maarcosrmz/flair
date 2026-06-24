#pragma once

#include <string>
#include <vector>

#include <flang/Semantics/symbol.h>

#include "../wdata.hpp"
#include "utils.hpp"

namespace sema = Fortran::semantics;

namespace codegen {

// Public intrinsic scalar / rank-1-numeric-array components, in declaration
// order.
sema::SymbolVector public_fields(dtype_info_t const &dt);

// `type, bind(C) :: py_<t>_object_t` instance layout.
str_t gen_object_struct(dtype_info_t const &dt);

// new / init / dealloc lifecycle procedures.
str_t gen_lifecycle(dtype_info_t const &dt, sema::SymbolVector const &fields,
                    string_pool_t &strings);

// One type-bound method. Appends its PyInit method-table row to `fills` (bumps
// `n`).
str_t gen_method(dtype_info_t const &dt, sema::Symbol const &binding,
                 module_info_t const &m, string_pool_t &strings, str_t &fills,
                 int &n);

// One getset property (scalar or rank-1 numpy array). Appends its getset-table
// row.
str_t gen_getset(dtype_info_t const &dt, sema::Symbol const &comp,
                 string_pool_t &strings, str_t &fills, int &n);

// PyInit slot / spec / type-creation fill statements (pure functions of the
// names).
str_t slot_fills(str_t const &tn);
str_t spec_fills(str_t const &tn, str_t const &cls, str_t const &modpy,
                 string_pool_t &strings);
str_t create_fills(str_t const &tn, str_t const &cls, string_pool_t &strings);

} // namespace codegen
