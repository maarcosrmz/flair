#include <flang/Frontend/CompilerInstance.h>
#include <flang/Parser/char-block.h>
#include <flang/Parser/parsing.h>
#include <flang/Semantics/symbol.h>
#include <fstream>
#include <iostream>
#include <llvm/ADT/STLExtras.h>
#include <memory>

#include "../codegen/codegen.hpp"
#include "../traversal.hpp"
#include "custom_action.hpp"
#include "flu/diagnostics.hpp"

namespace sema = Fortran::semantics;

namespace flair::semantics {

void custom_action::executeAction() {
  Fortran::frontend::CompilerInstance &Ci = getInstance();

  // If error after parsing or semantic analysis, exit immediately
  if (Ci.getDiagnostics().hasErrorOccurred())
    return;

  // Traverse global scope
  sema::Scope &root = Ci.getSemanticsContext().globalScope();
  traverse_global_scope(root, wdata);

  // Codegen: one py_<module>.F90 wrapper per module with wrappable entities.
  for (auto const &mi : wdata->modules) {
    if (not codegen::has_wrappable(mi))
      continue;
    std::string const outfile =
        "py_" + codegen::module_pyname(mi.name) + ".F90";
    std::ofstream(outfile) << codegen::codegen_module(mi);
    std::cout << "Generated " << outfile << std::endl;
  }

  // TODO: Generation should fail if unsupported arguments are detected.
  // Method must be explicitly ignored with compiler directive !FLAIR_IGNORE

  // Flush any diagnostics queued during codegen (e.g. unsupported arguments).
  flu::flush_messages(Ci.getSemanticsContext(), Ci.getSemaOutputStream());
}

} // namespace flair::semantics
