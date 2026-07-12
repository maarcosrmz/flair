#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "flang/Common/Fortran-consts.h"

namespace Fortran::semantics {
class DeclTypeSpec;
class Symbol;
} // namespace Fortran::semantics

// ---------------------------------------------------------------------------
// flu -- flang utilities. Generic queries over flang semantic entities,
// returning plain facts (no Python-binding / codegen knowledge).
// ---------------------------------------------------------------------------
namespace flu {

// Intrinsic type category of a declared type, or nullopt for
// derived/unsupported.
std::optional<Fortran::common::TypeCategory>
category(Fortran::semantics::DeclTypeSpec const &t);

// Intrinsic kind value (== element byte size for numeric types); 0 if
// non-intrinsic.
int kind_of(Fortran::semantics::DeclTypeSpec const &t);

// Explicit constant length of a character type; nullopt for assumed (len=*),
// deferred (len=:), non-constant, or non-character types.
std::optional<std::int64_t> char_len(Fortran::semantics::DeclTypeSpec const &t);

// Folded name of a derived type, or "" if `t` is not a derived type.
std::string derived_name(Fortran::semantics::DeclTypeSpec const &t);

// class(t) or class(*) declaration.
bool is_polymorphic(Fortran::semantics::DeclTypeSpec const &t);

// class(*) / type(*) (unlimited polymorphic / assumed type).
bool is_unlimited_polymorphic(Fortran::semantics::DeclTypeSpec const &t);

// Declared base type symbol of a class(base_t) declaration; nullptr for
// class(*) and non-polymorphic types.
Fortran::semantics::Symbol const *
poly_base(Fortran::semantics::DeclTypeSpec const &t);

} // namespace flu
