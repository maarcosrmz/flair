#include <flang/Frontend/CompilerInstance.h>
#include <flang/Parser/char-block.h>
#include <flang/Parser/parse-tree-visitor.h>
#include <flang/Parser/parse-tree.h>
#include <flang/Parser/parsing.h>
#include <flang/Semantics/semantics.h>
#include <fstream>
#include <iostream>
#include <llvm/ADT/STLExtras.h>
#include <optional>

#include "../codegen/codegen.hpp"
#include "../traversal.hpp"
#include "custom_action.hpp"

// Forward declaration
namespace Fortran::semantics {

bool ResolveNames(SemanticsContext &, const parser::Program &, Scope &top);

} // namespace Fortran::semantics

namespace sema = Fortran::semantics;

namespace flair::parser {

void custom_action::executeAction() {
  Fortran::frontend::CompilerInstance &Ci = getInstance();

  // If error after parsing, exit immediately
  if (Ci.getDiagnostics().hasErrorOccurred())
    return;

  std::optional<Fortran::parser::Program> &ParseTree{
      Ci.getParsing().parseTree()};
  assert(ParseTree && "Cannot run semantic checks without a parse tree!");

  auto &SemanticsCtx{Ci.createNewSemanticsContext()};

  // Populate globalScope based on the parse tree, without
  // performing semantic analysis.
  sema::ResolveNames(SemanticsCtx, ParseTree.value(),
                     SemanticsCtx.globalScope());

  // Traverse global scope
  sema::Scope &root = SemanticsCtx.globalScope();
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
}

} // namespace flair::parser
