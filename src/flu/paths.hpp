#pragma once

#include <optional>
#include <string>

namespace Fortran::semantics {
class SemanticsContext;
class Symbol;
} // namespace Fortran::semantics

namespace sema = Fortran::semantics;

namespace flu {

// Absolutized (against cwd) path with . / .. components removed.
// Returns the input unchanged if the cwd is unavailable.
std::string normalized_path(std::string const &path);

// Top-level source file a module was resolved from, or nullopt for scopes
// without file provenance (e.g. compiler-inserted sources).
std::optional<std::string> defining_path(sema::SemanticsContext &context,
                                         sema::Symbol const &mod_sym);

} // namespace flu
