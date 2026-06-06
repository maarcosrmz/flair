#include "flang/Semantics/symbol.h"
#include "flang/Frontend/CompilerInstance.h"

using namespace Fortran;

using sym_ref = semantics::Symbol const &;
using str_t = std::string;

struct fnt_info_t {
  sym_ref ref;
  bool rewrite = false;
  sym_ref parent;
  [[nodiscard]] semantics::ProcBindingDetails const &prog_binding_details() const { return ref.get<semantics::ProcBindingDetails>(); } 
  [[nodiscard]] semantics::SubprogramDetails const &subprogram_details() const { return ref.get<semantics::SubprogramDetails>(); } 
};

struct dtype_info_t {
  sym_ref ref;
  sym_ref base;
  // (vector of) methods (?)
  fnt_info_t constructor; // p => dtype_t()
  fnt_info_t initializer; // dtype_t :: p; dtype_t_init(p)
  
  // synthetize_init_from_pydict()
  // -> generate dummy constructor if has no constructor or initializer
};

struct module_info_t {
  str_t name;
  // source file
  // source file full stem 
  // documentation

  std::vector<fnt_info_t> functions;
  std::vector<dtype_info_t> derived_types;
};

struct wdata_t {
  std::unique_ptr<frontend::CompilerInstance> ci;
  std::vector<module_info_t> modules;

  wdata_t(frontend::CompilerInstance *ci) : ci(ci) {};
};
