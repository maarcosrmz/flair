#pragma once
#include <string>
#include <vector>

namespace codegen {

using str_t = std::string;

// Full source of the generated `py_<pkg>_pkg.F90`: the combined package
// extension's PyInit_<pkg>, which creates every wrapped module (via its
// FLAIR_init_<modpy> internal init) as a submodule attribute and registers
// it in sys.modules as "<pkg>.<modpy>". `submods` are the Python module
// names (module_pyname) in emission order.
str_t codegen_package(str_t const &pkg, std::vector<str_t> const &submods);

} // namespace codegen
