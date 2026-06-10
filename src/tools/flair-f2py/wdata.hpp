#pragma once
#include "flang/Semantics/symbol.h"
#include "flang/Frontend/CompilerInstance.h"

using namespace Fortran;

using sym_ptr_t = semantics::Symbol const *; 
using str_t = std::string;

struct fnt_info_t {
  sym_ptr_t ptr = nullptr; // may have ProcBindingDetails or SubprogramDetails
  bool rewrite  = false;
  sym_ptr_t parent;
  // [[nodiscard]] semantics::ProcBindingDetails const &prog_binding_details() const { return ref.get<semantics::ProcBindingDetails>(); } 
  // [[nodiscard]] semantics::SubprogramDetails const &subprogram_details() const { return ref.get<semantics::SubprogramDetails>(); } 
};

struct dtype_info_t {
  sym_ptr_t ptr  = nullptr;
  sym_ptr_t base = nullptr;

  std::vector<fnt_info_t> methods; // type-bound procedures 
                                   // all have ProcBindingDetails (SourceName - rename) -> SubprogramDetails (actual subprogram)

  fnt_info_t ctor; // p => dtype_t()
  fnt_info_t init; // dtype_t :: p; dtype_t_init(p)
  
  // finalizers are handled automatically when deallcoate
  // is called or the respective object goes out of scope

  // synthetize_init_from_pydict()
  // -> generate dummy constructor if has no constructor or initializer

  dtype_info_t(sym_ptr_t ptr) : ptr(ptr) {}
};

struct module_info_t {
  str_t name;
  // source file
  // source file full stem 
  // documentation

  std::vector<fnt_info_t> functions;
  std::map<str_t, dtype_info_t> derived_types;

  module_info_t(str_t const &name) : name(name) {}
};

struct wdata_t {
  frontend::CompilerInstance const *ci;
  std::vector<module_info_t> modules;

  wdata_t(frontend::CompilerInstance *ci) : ci(ci) {};
};
