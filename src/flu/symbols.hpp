#pragma once

#include <flang/Common/reference.h>
#include <vector>

namespace Fortran::semantics {
class Symbol;
using SymbolVector = std::vector<common::Reference<const Symbol>>;
}

namespace sema = Fortran::semantics;

namespace flu {

// Resolve a type-bound binding to the actual subprogram it binds; nullptr if not a binding.
sema::Symbol const *binding_actual(sema::Symbol const &binding);

// Rank of an object entity (0 = scalar / non-object).
int rank_of(sema::Symbol const &sym);

// Whether a symbol has the POINTER / ALLOCATABLE attribute.
bool is_pointer(sema::Symbol const &sym);
bool is_allocatable(sema::Symbol const &sym);

// Public object-entity components of a derived type, in declaration order.
sema::SymbolVector public_components(sema::Symbol const &type_sym);

sema::SymbolVector get_specific_procs(const sema::Symbol &iface_sym);

} // namespace flu
