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

// Name of the python_api_mod checked converter for this type's category,
// e.g. "FLAIR_double_from_PyObject". The helper returns the C-typed value
// and reports failure through its logical `ok` out-argument, leaving the
// Python exception pending.
str_t py_helper(Fortran::semantics::DeclTypeSpec const &t);

// Fortran declaration of the C type the checked converter returns, e.g.
// "real(c_double)".
str_t py_ctype(Fortran::semantics::DeclTypeSpec const &t);

// Expression narrowing a converter result `var` to this type's kind, e.g.
// "real(var, 4)".
str_t narrow(Fortran::semantics::DeclTypeSpec const &t, str_t const &var);

// Fortran declaration type, e.g. "integer(8)".
str_t ftype(Fortran::semantics::DeclTypeSpec const &t);

} // namespace codegen
