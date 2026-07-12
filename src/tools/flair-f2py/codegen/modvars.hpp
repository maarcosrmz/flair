#pragma once

#include "functions.hpp" // ext_types_t, classify_dtype, module_info_t

namespace codegen {

// Expose the module's public variables as live module attributes.
//
// Derived-type variables (of wrapped types) and intrinsic arrays become view
// objects created in PyInit and registered with PyModule_AddObjectRef: reads
// and writes go straight through to the Fortran storage (module variables
// have static lifetime, so the views can never dangle). Intrinsic scalars are
// served by a module-level `__getattr__` (PEP 562), so each access converts
// the current value; they are read-only from Python (assigning the attribute
// rebinds it in the module dict and detaches it from the Fortran variable).
//
// Returns the wrapper procedures (address helpers + `__getattr__`, registered
// via `fills`/`n` in the module method table) and appends the PyInit locals
// and attribute-creation statements to `init_decls`/`init_creates` (the
// latter must run after the type objects are created). Variables flair cannot
// expose (unwrapped derived types, pointer/allocatable arrays, ...) are
// skipped with a warning.
str_t gen_module_vars(module_info_t const &m, string_pool_t &strings,
                      str_t *fills, int &n, ext_types_t &ext_types,
                      str_t &init_decls, str_t &init_creates);

} // namespace codegen
