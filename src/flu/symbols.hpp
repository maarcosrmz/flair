#pragma once

#include <string>
#include <vector>

#include "flang/Common/reference.h"

namespace Fortran::semantics {
class Symbol;
using SymbolVector = std::vector<common::Reference<const Symbol>>;
} // namespace Fortran::semantics

namespace sema = Fortran::semantics;

namespace flu {

// Resolve a type-bound binding to the actual subprogram it binds; nullptr if
// not a binding.
sema::Symbol const *binding_actual(sema::Symbol const &binding);

// True if derived type `derived` is `base` or extends it (transitively).
bool extends_or_is(sema::Symbol const &derived, sema::Symbol const &base);

// Rank of an object entity (0 = scalar / non-object).
int rank_of(sema::Symbol const &sym);

// Whether a symbol has the POINTER / ALLOCATABLE attribute.
bool is_pointer(sema::Symbol const &sym);
bool is_allocatable(sema::Symbol const &sym);

// Public object-entity components of a derived type, in declaration order.
sema::SymbolVector public_components(sema::Symbol const &type_sym);

// Same, flattened over the whole `extends(...)` chain, most-distant ancestor
// first. Fortran forbids redeclaring an inherited component name, so the
// result needs no deduplication. Each level's parent component (the one named
// after its base type) stays in the list: it is what a wrapper exposes as the
// upcast view property.
sema::SymbolVector all_public_components(sema::Symbol const &type_sym);

// The `extends(...)` chain of `type_sym`, most-distant ancestor first,
// excluding `type_sym` itself. Empty for a type that extends nothing.
std::vector<sema::Symbol const *> ancestors(sema::Symbol const &type_sym);

sema::SymbolVector get_specific_procs(const sema::Symbol &iface_sym);

// The module symbol that encloses `sym`'s definition (nullptr if none). Works
// across use-association, so a derived type reached through `use` still
// reports its defining module.
sema::Symbol const *owning_module(sema::Symbol const &sym);

// Source name of the owning module ("" if none).
std::string owning_module_name(sema::Symbol const &sym);

// True if `sym`'s owning module was read from a precompiled .mod file rather
// than built from a source file in this invocation. The same flag traversal
// uses to stop at transitively USEd modules.
bool from_module_file(sema::Symbol const &sym);

// True if `sym` is defined inside an intrinsic module (iso_c_binding,
// ieee_arithmetic, __fortran_builtins, ...) rather than user code.
bool in_intrinsic_module(sema::Symbol const &sym);

} // namespace flu
