#pragma once

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
// `call_args`. Post-call cleanup (e.g. Py_DecRef of coerced numpy arrays) is
// emitted into `cleanup` when non-null. Supports intrinsic scalar inputs,
// intent(in) intrinsic arrays, intent(out)/intent(inout) intrinsic arrays (with
// write-back), and wrapped derived-type inputs; returns false (procedure
// skipped) for anything else. An intent(out)/intent(inout) intrinsic scalar
// cannot be written back and emits an error via `ctx`. Shared with method
// codegen.
bool parse_args(std::vector<Fortran::semantics::Symbol *> const &dummies,
                module_info_t const &m, str_t const &fail_return, str_t &decls,
                str_t &fetch, str_t &call_args, str_t *cleanup = nullptr);

// Set `r` from a Fortran call: function result (rt != null) -> to_py;
// subroutine -> None.
str_t build_result(Fortran::semantics::DeclTypeSpec const *rt,
                   str_t const &call_expr);

// Dummy args without the passed-object (first dummy).
std::vector<Fortran::semantics::Symbol *>
drop_self(std::vector<Fortran::semantics::Symbol *> const &dummies);

// One free module function. Appends its module-table row to `fills` (bumps
// `n`). "" if skipped. `call_name`, if non-empty, overrides the Fortran
// procedure invoked in the body (the wrapper symbol is still named after `fn`):
// used for interface specifics, which call the public generic name so that
// resolution picks the specific from the typed actuals even when the specific
// itself is private.
str_t gen_module_function(Fortran::semantics::Symbol const &fn,
                          module_info_t const &m, string_pool_t &strings,
                          str_t *fills, int &n, str_t const &call_name = {});

} // namespace codegen
