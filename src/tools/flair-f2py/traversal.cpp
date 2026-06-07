#include <flang/Semantics/symbol.h>
#include <llvm/ADT/STLExtras.h>

#include "traversal.hpp"

void traverse_module(semantics::Symbol const &mod_sym, module_info_t &mi) {
  bool default_private = mod_sym.get<semantics::ModuleDetails>().isDefaultPrivate();
  auto mod_scope = mod_sym.get<semantics::ModuleDetails>().scope();
  if (mod_scope == nullptr) return;

  // Tracks the derived type currently being traversed, so the shared
  // subprogram / proc-binding lambdas can tell whether they were reached at
  // module scope (-> mi.functions) or inside a type (-> dt.methods).
  dtype_info_t *current_dtype = nullptr;

  // TODO: recognize init function pattern for __init__
  auto match_subprogram = [&](semantics::Symbol const &sym) {
    if (not default_private or sym.attrs().test(semantics::Attr::PUBLIC)) return;
    if (not sym.has<semantics::SubprogramDetails>()) return;
    fnt_info_t fi{&sym, false, current_dtype ? current_dtype->ptr : nullptr};
    if (current_dtype) current_dtype->methods.emplace_back(fi);
    else               mi.functions.emplace_back(fi);
  };

  // TODO: recognize interface as constructor pattern
  auto match_proc_binding= [&](semantics::Symbol const &sym) {
    if (not default_private or sym.attrs().test(semantics::Attr::PUBLIC)) return;
    if (not sym.has<semantics::SubprogramDetails>()) return;
    auto const &actual = sym.get<semantics::ProcBindingDetails>().symbol();
    fnt_info_t fi{&actual, false, current_dtype ? current_dtype->ptr : nullptr};
    if (current_dtype) current_dtype->methods.emplace_back(fi);
    else               mi.functions.emplace_back(fi);
  };

  auto match_dtype = [&](semantics::Symbol const &sym) {
    if (not default_private or sym.attrs().test(semantics::Attr::PUBLIC)) return;
    if (not sym.has<semantics::SubprogramDetails>()) return;
    auto const *dtype_scope = sym.scope(); // the type's own member scope
    if (dtype_scope == nullptr) return;

    dtype_info_t &dt = mi.derived_types.emplace_back(&sym);
    if (auto const *parent = sym.GetParentTypeSpec()) // extends(...) base type
      dt.base = &parent->typeSymbol();

    current_dtype = &dt;
    llvm::for_each(dtype_scope->GetSymbols(), match_subprogram);
    llvm::for_each(dtype_scope->GetSymbols(), match_proc_binding);
    current_dtype = nullptr;
  };

  llvm::for_each(mod_scope->GetSymbols(), match_dtype);
  llvm::for_each(mod_scope->GetSymbols(), match_subprogram);
  llvm::for_each(mod_scope->GetSymbols(), match_proc_binding);
}

