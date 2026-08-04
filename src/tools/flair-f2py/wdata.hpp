#pragma once
#include <map>
#include <optional>
#include <set>
#include <vector>

#include "flang/Semantics/symbol.h"

#include "directive_collector.hpp"

using namespace Fortran;

using sym_ptr_t = semantics::Symbol const *;
using str_t = std::string;

struct fnt_info_t {
  sym_ptr_t ptr = nullptr; // may have ProcBindingDetails or SubprogramDetails
  bool rewrite = false;
  // For a type-bound procedure: the derived type whose scope declares the
  // binding. Differs from the type it was collected for when the binding is
  // inherited through `extends(...)`.
  sym_ptr_t declared_in = nullptr;
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

// Combined-package output (compdb mode): all wrapped modules become
// submodules of one extension named `name`, built by a generated script.
struct pkg_info_t {
  str_t name;        // sanitized package name
  str_t runtime_src; // path to fortran_python_api.F90 for the build script
  // folded module name -> module-search dirs (-I/-J/-module-dir values) of
  // its defining database entry, for compiling that module's wrapper
  std::map<str_t, std::vector<str_t>> module_search_dirs;
  // folded module names in dependency-first (depgraph post-order) order;
  // wrappers must be compiled in this order because consumer wrappers
  // use-associate the producer wrappers' converter procedures
  std::vector<str_t> module_order;
};

struct wdata_t {
  std::vector<module_info_t> modules;
  std::unique_ptr<directive_collector> collector;

  // Source files whose modules get wrapped (--wrap). Empty: wrap all inputs.
  std::vector<str_t> wrap_files;

  // Folded names of modules added to the wrap set because a module already in
  // it exchanges one of their derived types across its API: the consumer
  // wrapper use-associates the converters from the producer's wrapper, so the
  // producer must be wrapped too. Only consulted while `wrap_files` narrows.
  std::set<str_t> wrap_modules;

  // Folded names of modules named by --external: nothing can ever wrap them,
  // so a type of theirs crossing a wrapped API skips the referencing entity
  // instead of being wired to a converter that will never exist.
  std::set<str_t> external_modules;

  // In compdb mode every database entry is parsed from source, so a module
  // that still arrives as a precompiled .mod is outside the project and is
  // treated as external on top of the names above.
  bool compdb_mode = false;

  // Engaged only in compdb mode: combined-package codegen.
  std::optional<pkg_info_t> pkg;

  explicit wdata_t() = default;
};
