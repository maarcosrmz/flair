#include "pytypes.hpp"

#include <string>

#include "flu/types.hpp"

namespace codegen {

using Fortran::common::TypeCategory;

str_t npy(Fortran::semantics::DeclTypeSpec const &t) {
  auto cat = flu::category(t);
  if (!cat)
    return "NPY_NOTYPE";
  int const k = flu::kind_of(t);
  if (*cat == TypeCategory::Integer)
    switch (k) {
    case 1:
      return "NPY_INT8";
    case 2:
      return "NPY_INT16";
    case 4:
      return "NPY_INT32";
    case 8:
      return "NPY_INT64";
    }
  if (*cat == TypeCategory::Real)
    switch (k) {
    case 4:
      return "NPY_FLOAT32";
    case 8:
      return "NPY_FLOAT64";
    }
  return "NPY_NOTYPE";
}

bool intrinsic_supported(Fortran::semantics::DeclTypeSpec const &t) {
  return npy(t) != "NPY_NOTYPE";
}

str_t to_py(Fortran::semantics::DeclTypeSpec const &t, str_t const &expr) {
  auto cat = flu::category(t);
  if (cat)
    switch (*cat) {
    case TypeCategory::Real:
      return "PyFloat_FromDouble(real(" + expr + ", c_double))";
    case TypeCategory::Integer:
      return "PyLong_FromLongLong(int(" + expr + ", c_long_long))";
    default:
      break;
    }
  return "c_null_ptr";
}

str_t from_py(Fortran::semantics::DeclTypeSpec const &t, str_t const &obj) {
  auto cat = flu::category(t);
  if (cat)
    switch (*cat) {
    case TypeCategory::Real:
      return "real(PyFloat_AsDouble(" + obj + "), " +
             std::to_string(flu::kind_of(t)) + ")";
    case TypeCategory::Integer:
      return "int(PyLong_AsLongLong(" + obj + "), " +
             std::to_string(flu::kind_of(t)) + ")";
    default:
      break;
    }
  return "0";
}

str_t ftype(Fortran::semantics::DeclTypeSpec const &t) {
  auto cat = flu::category(t);
  if (cat)
    switch (*cat) {
    case TypeCategory::Real:
      return "real(" + std::to_string(flu::kind_of(t)) + ")";
    case TypeCategory::Integer:
      return "integer(" + std::to_string(flu::kind_of(t)) + ")";
    default:
      break;
    }
  return "";
}

} // namespace codegen
