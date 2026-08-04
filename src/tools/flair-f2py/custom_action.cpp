#include <cstdlib>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>

#include "flang/Frontend/CompilerInstance.h"
#include "flang/Frontend/TextDiagnosticBuffer.h"
#include "flang/Parser/options.h"
#include "flang/Parser/parse-tree-visitor.h"
#include "flang/Parser/parse-tree.h"
#include "flang/Parser/parsing.h"
#include "flang/Semantics/semantics.h"
#include "flang/Support/Fortran-features.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/DiagnosticOptions.h"

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

int run_single_mode(llvm::ArrayRef<const char *> args,
                    std::vector<std::string> wrap_files,
                    std::set<std::string> external_modules,
                    const char *argv0) {
  auto flang = std::make_unique<Fortran::frontend::CompilerInstance>();

  flang->createDiagnostics();
  if (not flang->hasDiagnostics())
    return EXIT_FAILURE;

  auto diags_buffer =
      std::make_unique<Fortran::frontend::TextDiagnosticBuffer>();

  clang::DiagnosticOptions diag_opts;
  clang::DiagnosticsEngine diags(clang::DiagnosticIDs::create(), diag_opts,
                                 diags_buffer.get(), /*ShouldOwnClient=*/false);

  if (not Fortran::frontend::CompilerInvocation::createFromArgs(
          flang->getInvocation(), args, diags, argv0))
    throw std::runtime_error("Failed creating compiler invocation.");

  // Without input files flang substitutes stdin, which for a wrapper generator
  // means parsing an empty program and reporting success having generated
  // nothing. Take it as the misuse it almost always is.
  {
    auto const &inputs = flang->getInvocation().getFrontendOpts().inputs;
    if (inputs.empty() || (inputs.size() == 1 && inputs.front().isFile() &&
                           inputs.front().getFile() == "-"))
      throw std::runtime_error("No input files. See --help for usage.");
  }

  // -fintrinsic-modules-path lands in the preprocessor options; the semantics
  // context reads intrinsic modules from the Fortran options, so mirror it
  // there. The distro path is a last resort for an unconfigured invocation.
  {
    auto &pp_opts = flang->getInvocation().getPreprocessorOpts();
    auto &fortran_opts = flang->getInvocation().getFortranOpts();
    for (auto const &dir : pp_opts.searchDirectoriesFromIntrModPath)
      fortran_opts.intrinsicModuleDirectories.emplace_back(dir);
    fortran_opts.intrinsicModuleDirectories.emplace_back("/usr/include/flang");
  }

  diags_buffer->flushDiagnostics(flang->getDiagnostics());

  flang->getInvocation().getFortranOpts().compilerDirectiveSentinels.push_back(
      FLAIR_DIRECTIVE);

  auto action = std::make_unique<custom_action>();
  action->set_wrap_files(std::move(wrap_files));
  action->set_external_modules(std::move(external_modules));
  bool const success = flang->executeAction(*action);
  flang->clearOutputFiles(/*EraseFiles=*/true);

  if (not success)
    throw std::runtime_error("Failed to run custom_action.");

  return action->failed() ? EXIT_FAILURE : EXIT_SUCCESS;
}
