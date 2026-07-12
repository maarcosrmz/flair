#pragma once
#include "flang/Semantics/symbol.h"

#include "directive_collector.hpp"

using namespace Fortran;

using sym_ptr_t = semantics::Symbol const *;
using str_t = std::string;

struct fnt_info_t {
  sym_ptr_t ptr = nullptr; // may have ProcBindingDetails or SubprogramDetails
  bool rewrite = false;
  sym_ptr_t parent;
  // '!flair$ instantiate' type names, sorted for deterministic codegen
  std::vector<str_t> instantiate;
  // [[nodiscard]] semantics::ProcBindingDetails const &prog_binding_details()
  // const { return ref.get<semantics::ProcBindingDetails>(); }
  // [[nodiscard]] semantics::SubprogramDetails const &subprogram_details()
  // const { return ref.get<semantics::SubprogramDetails>(); }
};

struct iface_info_t {
  sym_ptr_t ptr = nullptr;
};

struct dtype_info_t {
  sym_ptr_t ptr = nullptr;
  sym_ptr_t base = nullptr;

  std::vector<fnt_info_t>
      methods; // type-bound procedures
               // all have ProcBindingDetails (SourceName - rename) ->
               // SubprogramDetails (actual subprogram)

  iface_info_t ctor; // p => dtype_t()
  fnt_info_t init;   // dtype_t :: p; dtype_t_init(p)

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

  std::map<str_t, dtype_info_t> derived_types;
  std::vector<fnt_info_t> functions;
  std::vector<iface_info_t> interfaces;
  std::vector<sym_ptr_t> variables; // public module variables (non-parameter)

  module_info_t(str_t const &name) : name(name) {}
};

struct wdata_t {
  std::vector<module_info_t> modules;
  std::unique_ptr<directive_collector> collector;

  // Source files whose modules get wrapped (--wrap). Empty: wrap all inputs.
  std::vector<str_t> wrap_files;

  explicit wdata_t() = default;
};
