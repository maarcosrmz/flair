#pragma once

#include <vector>

#include <flang/Semantics/symbol.h>

#include "../wdata.hpp"
#include "utils.hpp"

namespace codegen {

// One dispatching wrapper for a generic interface, exposed under the generic's
// name. Inspects the incoming arguments at runtime and forwards `(self, args)`
// to the matching specific-procedure wrapper (`py_mod_<specific>`). `specifics`
// must list only the specifics whose wrappers were actually generated. Appends
// its module-table row to `fills` (bumps `n`). "" if there is nothing to
// expose.
str_t gen_interface_wrapper(
    Fortran::semantics::Symbol const &iface,
    std::vector<Fortran::semantics::Symbol const *> const &specifics,
    module_info_t const &m, string_pool_t &strings, str_t *fills, int &n);

} // namespace codegen
