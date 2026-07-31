#pragma once
#include "../wdata.hpp"
#include <string>

namespace codegen {

using str_t = std::string;

// Python module name for a Fortran module: strip a trailing `_mod`. This is
// the identifier form: the wrapper file name, the Fortran wrapper module and
// the init symbol are built from it, so it is never package-qualified.
str_t module_pyname(str_t const &module_name);

// Package prefix for python-visible names; empty outside compdb mode. Set
// once per run, before emission.
void set_package_prefix(str_t const &pkg);

// The name python sees for a module: `<pkg>.<modpy>` under a package prefix,
// plain `<modpy>` otherwise. Feeds PyModuleDef%m_name and every tp_name, so
// a module's `__name__` agrees with the sys.modules key the package registers
// it under, and its types' `__module__` agrees with both.
str_t module_pyqual(str_t const &module_name);

// True if the module has any derived type or module function worth wrapping.
bool has_wrappable(module_info_t const &m);

// Full source of the generated `py_<modpy>.F90` wrapper for a module. With
// `internal_init` the init function is exported as FLAIR_init_<modpy> instead
// of PyInit_<modpy>, for linking into a combined package extension whose
// PyInit calls it (see codegen_package).
str_t codegen_module(module_info_t const &m, bool internal_init = false);

} // namespace codegen
