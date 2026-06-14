#pragma once

#include <vector>

namespace Fortran::semantics {
class Symbol;
}

namespace flu {

// Resolve a type-bound binding to the actual subprogram it binds; nullptr if not a binding.
Fortran::semantics::Symbol const *binding_actual(Fortran::semantics::Symbol const &binding);

// Rank of an object entity (0 = scalar / non-object).
int rank_of(Fortran::semantics::Symbol const &sym);

// Public object-entity components of a derived type, in declaration order.
std::vector<Fortran::semantics::Symbol const *> public_components(
    Fortran::semantics::Symbol const &type_sym);

} // namespace flu
