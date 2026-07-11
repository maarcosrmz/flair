#include <optional>
#include <utility>

#include "flang/Frontend/CompilerInstance.h"
#include "flang/Parser/parse-tree-visitor.h"
#include "flang/Parser/parse-tree.h"
#include "flang/Parser/parsing.h"
#include "flang/Semantics/semantics.h"
#include "flang/Support/Fortran-features.h"

#include "custom_action.hpp"
#include "directive_collector.hpp"
#include "flu/diagnostics.hpp"
#include "pipeline.hpp"

namespace sema = Fortran::semantics;
namespace parse = Fortran::parser;

// Forward declaration
namespace Fortran::semantics {

bool ResolveNames(SemanticsContext &, const parser::Program &, Scope &top);

} // namespace Fortran::semantics

bool custom_action::initSemantics() {
  Fortran::frontend::CompilerInstance &Ci = getInstance();

  // If error after parsing, exit immediately
  if (Ci.getDiagnostics().hasErrorOccurred()) {
    failed_ = true;
    return false;
  }

  std::optional<parse::Program> &ParseTree{Ci.getParsing().parseTree()};
  assert(ParseTree && "Cannot populate global scope without a parse tree!");

  // One SemanticsContext is shared by all input files: ModFileReader
  // consults the global scope before searching for a .mod file, so modules
  // resolved from earlier inputs satisfy USE statements of later ones.
  if (context == nullptr) {
    auto &SemanticsCtx{Ci.createNewSemanticsContext()};

    // Disable compiler warnings about ignored compiler directives.
    // We will only emit warnings for wrong flair directives.
    // Other (ignored) directives will receive warnings during regular
    // compilation anyway.
    auto &lang_features = const_cast<common::LanguageFeatureControl &>(
        SemanticsCtx.languageFeatures());
    lang_features.EnableWarning(common::UsageWarning::IgnoredDirective, false);

    wdata->collector = std::make_unique<directive_collector>(SemanticsCtx);

    context = &SemanticsCtx;
  }

  // The Parsing object is replaced on the next input, but symbols keep
  // references into their parse tree; hand ownership to the context.
  parse::Program &Tree = context->SaveParseTree(std::move(*ParseTree));

  // Populate globalScope based on the parse tree, without
  // performing semantic analysis.
  sema::ResolveNames(*context, Tree, context->globalScope());

  // Collect `flair$` compiler directives
  parse::Walk(Tree, *wdata->collector.get());

  return true;
}

void custom_action::executeAction() {
  ++files_seen;
  if (not initSemantics()) {
    // Surface diagnostics accumulated by earlier inputs even when a later
    // input fails before reaching codegen.
    if (context != nullptr)
      flu::flush_messages(*context, getInstance().getSemaOutputStream());
    return;
  }
  // Traversal and codegen run once, after every input has been resolved
  // into the shared global scope.
  if (files_seen == getInstance().getFrontendOpts().inputs.size()) {
    if (not run_wrap_pipeline(*context, wdata,
                              getInstance().getSemaOutputStream()))
      failed_ = true;
  }
}
