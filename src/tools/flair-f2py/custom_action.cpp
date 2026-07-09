#include <fstream>
#include <iostream>
#include <optional>
#include <utility>
#include <vector>

#include "flang/Frontend/CompilerInstance.h"
#include "flang/Parser/parse-tree-visitor.h"
#include "flang/Parser/parse-tree.h"
#include "flang/Parser/parsing.h"
#include "flang/Semantics/semantics.h"
#include "flang/Support/Fortran-features.h"

#include "codegen/codegen.hpp"
#include "custom_action.hpp"
#include "directive_collector.hpp"
#include "flu/diagnostics.hpp"
#include "traversal.hpp"

using namespace codegen;
namespace sema = Fortran::semantics;
namespace parse = Fortran::parser;

// Forward declaration
namespace Fortran::semantics {

bool ResolveNames(SemanticsContext &, const parser::Program &, Scope &top);

} // namespace Fortran::semantics

bool custom_action::initSemantics() {
  Fortran::frontend::CompilerInstance &Ci = getInstance();

  // If error after parsing, exit immediately
  if (Ci.getDiagnostics().hasErrorOccurred())
    return false;

  const std::optional<parse::Program> &ParseTree{Ci.getParsing().parseTree()};
  assert(ParseTree && "Cannot populate global scope without a parse tree!");

  auto &SemanticsCtx{Ci.createNewSemanticsContext()};

  // Disable compiler warnings about ignored compiler directives.
  // We will only emit warnings for wrong flair directives.
  // Other (ignored) directives will receive warnings during regular
  // compilation anyway.
  auto &lang_features = const_cast<common::LanguageFeatureControl &>(
      SemanticsCtx.languageFeatures());
  lang_features.EnableWarning(common::UsageWarning::IgnoredDirective, false);

  // Populate globalScope based on the parse tree, without
  // performing semantic analysis.
  sema::ResolveNames(SemanticsCtx, ParseTree.value(),
                     SemanticsCtx.globalScope());

  // Collect `flair$` compiler directives
  wdata->collector = std::make_unique<directive_collector>(SemanticsCtx);
  parse::Walk(ParseTree.value(), *wdata->collector.get());

  context = &SemanticsCtx;

  return true;
}

void custom_action::traverseSemantics() {
  // Traverse global scope
  sema::Scope &root = context->globalScope();
  traverse_global_scope(root, wdata);
}

void custom_action::codegen() {
  std::vector<std::pair<std::string, std::string>> outputs;
  for (auto const &mi : wdata->modules) {
    if (not has_wrappable(mi))
      continue;
    outputs.emplace_back("py_" + module_pyname(mi.name) + ".F90",
                         codegen_module(mi));
  }
  wdata->modules.clear();

  if (context->messages().AnyFatalError(context->warningsAreErrors()))
    failed_ = true;

  flu::flush_messages(*context, getInstance().getSemaOutputStream());

  if (not failed_) {
    for (auto const &[outfile, content] : outputs) {
      std::ofstream(outfile) << content;
      std::cout << "Generated " << outfile << std::endl;
    }
  }
}

void custom_action::executeAction() {
  if (initSemantics()) {
    traverseSemantics();
    codegen();
  }
}
