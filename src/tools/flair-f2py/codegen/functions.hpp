#pragma once

#include <string>
#include <vector>

#include <flang/Semantics/symbol.h>

#include "../wdata.hpp"
#include "utils.hpp"

namespace Fortran::semantics {
class DeclTypeSpec;
}

namespace codegen {

// Resolve each dummy from the positional `args` tuple, emitting locals into
// `decls`, fetch/convert statements into `fetch`, and the Fortran actuals into
// `call_args`. Supports intrinsic scalar inputs and wrapped derived-type inputs;
// returns false (procedure skipped) for anything else. Shared with method codegen.
bool parse_args(std::vector<Fortran::semantics::Symbol *> const &dummies, module_info_t const &m,
                str_t const &fail_return, str_t &decls, str_t &fetch, str_t &call_args);

// Set `r` from a Fortran call: function result (rt != null) -> to_py; subroutine -> None.
str_t build_result(Fortran::semantics::DeclTypeSpec const *rt, str_t const &call_expr);

// Dummy args without the passed-object (first dummy).
std::vector<Fortran::semantics::Symbol *> drop_self(
    std::vector<Fortran::semantics::Symbol *> const &dummies);

// One free module function. Appends its module-table row to `fills` (bumps `n`). "" if skipped.
str_t gen_module_function(Fortran::semantics::Symbol const &fn, module_info_t const &m,
                          string_pool_t &strings, str_t &fills, int &n);

} // namespace codegen
