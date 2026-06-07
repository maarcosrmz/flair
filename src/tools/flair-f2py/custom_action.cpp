#include <flang/Frontend/CompilerInstance.h>
#include <flang/Parser/char-block.h>
#include <flang/Semantics/symbol.h>
#include <llvm/ADT/STLExtras.h>
#include <memory>

#include "custom_action.hpp"
#include "traversal.hpp"

void custom_action::executeAction() {
  // If error after parsing, exit immediately
  if (getInstance().getDiagnostics().hasErrorOccurred()) return;

  wdata = std::make_shared<wdata_t>(&getInstance());

  // Traverse semantics
  semantics::Scope &root = getInstance().getSemanticsContext().globalScope();
  for (auto const &[name, sym_ref] : root) {
    semantics::Symbol const &sym = sym_ref.get();
    if (not sym.has<semantics::ModuleDetails>()) continue;

    module_info_t mi(name.ToString());
    traverse_module(sym, mi);
    wdata->modules.push_back(std::move(mi));
  }

  // Codegen
  for (auto const &mi : wdata->modules) {
    // str_t code = codegen_module(mi);
    // str_t outfilename = mi.source_file_full_stem + .wrap.f90 
    // std::ofstream(outfilename) << code;
  }
}
