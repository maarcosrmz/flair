#pragma once

#include <string>

namespace Fortran::semantics {
class DeclTypeSpec;
}

namespace codegen {

using str_t = std::string;

// Fortran -> Python/NumPy type mapping, built on the flu type queries.

// NumPy element code for a supported intrinsic real/integer; "NPY_NOTYPE"
// otherwise.
str_t npy(Fortran::semantics::DeclTypeSpec const &t);

// real(4/8) or integer(1/2/4/8) -- the intrinsic scalar/element types we wrap.
bool intrinsic_supported(Fortran::semantics::DeclTypeSpec const &t);

// PyObject* (Fortran expression) from a Fortran value `expr`.
str_t to_py(Fortran::semantics::DeclTypeSpec const &t, str_t const &expr);

// Fortran value of this type/kind from a PyObject* `obj`.
str_t from_py(Fortran::semantics::DeclTypeSpec const &t, str_t const &obj);

// Fortran declaration type, e.g. "integer(8)".
str_t ftype(Fortran::semantics::DeclTypeSpec const &t);

} // namespace codegen
