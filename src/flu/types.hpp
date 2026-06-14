#pragma once

#include <optional>
#include <string>

#include <flang/Common/Fortran-consts.h>

namespace Fortran::semantics {
class DeclTypeSpec;
}

// ---------------------------------------------------------------------------
// flu -- flang utilities. Generic queries over flang semantic entities,
// returning plain facts (no Python-binding / codegen knowledge).
// ---------------------------------------------------------------------------
namespace flu {

// Intrinsic type category of a declared type, or nullopt for derived/unsupported.
std::optional<Fortran::common::TypeCategory> category(Fortran::semantics::DeclTypeSpec const &t);

// Intrinsic kind value (== element byte size for numeric types); 0 if non-intrinsic.
int kind_of(Fortran::semantics::DeclTypeSpec const &t);

// Folded name of a derived type, or "" if `t` is not a derived type.
std::string derived_name(Fortran::semantics::DeclTypeSpec const &t);

} // namespace flu
