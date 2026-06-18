#include <flang/Frontend/CompilerInstance.h>
#include <flang/Parser/char-block.h>
#include <flang/Semantics/symbol.h>
#include <fstream>
#include <iostream>
#include <llvm/ADT/STLExtras.h>
#include <memory>

#include "../codegen/codegen.hpp"
#include "custom_action.hpp"
#include "traversal.hpp"

namespace sema = Fortran::semantics;

namespace flair::semantics {

void custom_action::executeAction() {
  // If error after parsing, exit immediately
  if (getInstance().getDiagnostics().hasErrorOccurred())
    return;

  wdata = std::make_shared<wdata_t>(&getInstance());

  // Traverse semantics
  sema::Scope &root = getInstance().getSemanticsContext().globalScope();
  for (auto const &[name, sym_ref] : root) {
    sema::Symbol const &sym = sym_ref.get();
    if (not sym.has<sema::ModuleDetails>())
      continue;
    // If the origin of the module is a .mod file, skip it.
    // Avoids transitive traversal of USEd modules.
    if (sym.test(sema::Symbol::Flag::ModFile))
      continue;

    module_info_t mi(name.ToString());
    traverse_module(sym, mi);
    wdata->modules.push_back(std::move(mi));
  }

  // Codegen: one py_<module>.F90 wrapper per module with wrappable entities.
  for (auto const &mi : wdata->modules) {
    if (not codegen::has_wrappable(mi))
      continue;
    std::string const outfile =
        "py_" + codegen::module_pyname(mi.name) + ".F90";
    std::ofstream(outfile) << codegen::codegen_module(mi);
    std::cout << "Generated " << outfile << std::endl;
  }
}

} // namespace flair::semantics
