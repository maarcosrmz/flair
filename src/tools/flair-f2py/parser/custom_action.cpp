#include <flang/Frontend/CompilerInstance.h>
#include <flang/Parser/char-block.h>
#include <flang/Parser/parse-tree-visitor.h>
#include <flang/Parser/parsing.h>
#include <iostream>
#include <llvm/ADT/STLExtras.h>

#include "ParseTreeVisitor.hpp"
#include "custom_action.hpp"

namespace flair::parser {

void custom_action::executeAction() {
  // If error after parsing, exit immediately
  if (getInstance().getDiagnostics().hasErrorOccurred())
    return;

  wdata = std::make_shared<wdata_t>(&getInstance());

  auto const &parse_tree = getInstance().getParsing().parseTree();

  ParseTreeVisitor visitor;
  if (!parse_tree)
    std::cout << "Empty parseTree" << std::endl;
  Fortran::parser::Walk(parse_tree, visitor);

  std::cout << "\n====   Functions: " << visitor.fcounter << " ====\n";
  std::cout << "==== Subroutines: " << visitor.scounter << " ====\n";
}

} // namespace flair::parser
