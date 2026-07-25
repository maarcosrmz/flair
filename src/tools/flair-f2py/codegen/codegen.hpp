#pragma once
#include "../wdata.hpp"
#include <string>

namespace codegen {

using str_t = std::string;

// Python module name for a Fortran module: strip a trailing `_mod`.
str_t module_pyname(str_t const &module_name);

// True if the module has any derived type or module function worth wrapping.
bool has_wrappable(module_info_t const &m);

// Full source of the generated `py_<modpy>.F90` wrapper for a module. With
// `internal_init` the init function is exported as FLAIR_init_<modpy> instead
// of PyInit_<modpy>, for linking into a combined package extension whose
// PyInit calls it (see codegen_package).
str_t codegen_module(module_info_t const &m, bool internal_init = false);

} // namespace codegen
