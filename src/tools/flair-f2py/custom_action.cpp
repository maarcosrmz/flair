#include <flang/Frontend/CompilerInstance.h>
#include <flang/Parser/char-block.h>
#include <flang/Semantics/symbol.h>
#include <llvm/ADT/STLExtras.h>
#include <memory>
#include <fstream>
#include <iostream>

#include "custom_action.hpp"
#include "traversal.hpp"
#include "codegen/codegen.hpp"

void custom_action::executeAction() {
  // If error after parsing, exit immediately
  if (getInstance().getDiagnostics().hasErrorOccurred()) return;

  wdata = std::make_shared<wdata_t>(&getInstance());

  // Traverse semantics
  semantics::Scope &root = getInstance().getSemanticsContext().globalScope();
  for (auto const &[name, sym_ref] : root) {
    semantics::Symbol const &sym = sym_ref.get();
    if (not sym.has<semantics::ModuleDetails>()) continue;
    // If the origin of the module is a .mod file, skip it.
    // Avoids transitive traversal of USEd modules.
    if (sym.test(semantics::Symbol::Flag::ModFile)) continue;

    module_info_t mi(name.ToString());
    traverse_module(sym, mi);
    wdata->modules.push_back(std::move(mi));
  }

  // Codegen: one py_<module>.F90 wrapper per module with wrappable entities.
  for (auto const &mi : wdata->modules) {
    if (not codegen::has_wrappable(mi)) continue;
    std::string const outfile = "py_" + codegen::module_pyname(mi.name) + ".F90";
    std::ofstream(outfile) << codegen::codegen_module(mi);
    std::cout << "Generated " << outfile << std::endl;
  }
}
