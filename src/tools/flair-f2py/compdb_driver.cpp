#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <utility>

#include "flang/Frontend/CompilerInstance.h"
#include "flang/Frontend/CompilerInvocation.h"
#include "flang/Frontend/FrontendOptions.h"
#include "flang/Frontend/TextDiagnosticBuffer.h"
#include "flang/Parser/parse-tree-visitor.h"
#include "flang/Semantics/semantics.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/DiagnosticOptions.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"

#include "compdb.hpp"
#include "compdb_driver.hpp"
#include "depgraph.hpp"
#include "directive_collector.hpp"
#include "flu/paths.hpp"
#include "pipeline.hpp"
#include "wdata.hpp"

// Forward declaration (as in custom_action.cpp)
namespace Fortran::semantics {

bool ResolveNames(SemanticsContext &, const parser::Program &, Scope &top);

} // namespace Fortran::semantics

// createFromArgs + the option fixups CompilerInstance::executeAction would
// apply (defaults, preprocessor merge) + the flair directive sentinel.
static void configure_invocation(Fortran::frontend::CompilerInvocation &invoc,
                                 llvm::ArrayRef<const char *> args,
                                 clang::DiagnosticsEngine &diags,
                                 const char *argv0) {
  if (not Fortran::frontend::CompilerInvocation::createFromArgs(invoc, args,
                                                                diags, argv0))
    throw std::runtime_error("Failed creating compiler invocation.");
  invoc.setDefaultFortranOpts();
  invoc.setFortranOpts();
  invoc.getFortranOpts().compilerDirectiveSentinels.push_back(FLAIR_DIRECTIVE);
}

int run_compdb_mode(std::string const &compdb_path,
                    std::string const &entry_file,
                    std::vector<std::string> wrap_files,
                    llvm::ArrayRef<const char *> passthrough_args,
                    const char *argv0) {
  auto const entries = compdb::load(compdb_path);

  std::string const entry_abs = flu::normalized_path(entry_file);
  compdb::entry_t const *entry = nullptr;
  for (auto const &e : entries)
    if (e.file == entry_abs)
      entry = &e;
  if (entry == nullptr)
    throw std::runtime_error("entry file '" + entry_file +
                             "' is not in the compilation database");

  auto flang = std::make_unique<Fortran::frontend::CompilerInstance>();
  flang->createDiagnostics();
  if (not flang->hasDiagnostics())
    return EXIT_FAILURE;

  auto diagsBuffer =
      std::make_unique<Fortran::frontend::TextDiagnosticBuffer>();
  clang::DiagnosticOptions diagOpts;
  clang::DiagnosticsEngine diags(clang::DiagnosticIDs::create(), diagOpts,
                                 diagsBuffer.get(), /*ShouldOwnClient=*/false);

  // Invocation-wide settings for semantics (module search directories,
  // default kinds, intrinsic paths): the entry's recorded flags, then the
  // explicit command-line flags, which win on conflicts.
  llvm::SmallVector<const char *, 64> master_args;
  for (auto const &arg : entry->args)
    master_args.push_back(arg.c_str());
  for (const char *arg : passthrough_args)
    master_args.push_back(arg);
  configure_invocation(flang->getInvocation(), master_args, diags, argv0);
  diagsBuffer->flushDiagnostics(flang->getDiagnostics());
  if (not flang->setUpTargetMachine())
    return EXIT_FAILURE;

  // Parser options must stay per-file (a -D recorded for the entry must not
  // leak into the preprocessing of its dependencies), so the per-entry base
  // carries only the explicit command-line flags; each entry's own recorded
  // flags are applied on top.
  Fortran::frontend::CompilerInvocation cli_invocation;
  configure_invocation(cli_invocation, passthrough_args, diags, argv0);
  diagsBuffer->flushDiagnostics(flang->getDiagnostics());
  parse::Options const base_opts = cli_invocation.getFortranOpts();

  flang->getAllCookedSources().allSources().set_encoding(base_opts.encoding);

  auto const options_for = [&](compdb::entry_t const &e) {
    parse::Options opts = base_opts;
    llvm::StringRef const ext = llvm::sys::path::extension(e.file);
    if (not ext.empty())
      opts.isFixedForm = Fortran::frontend::isFixedFormSuffix(ext.drop_front());
    compdb::apply_parser_flags(e.args, opts);
    return opts;
  };

  auto files = depgraph::closure_of(entry_abs, entries,
                                    flang->getAllCookedSources(), options_for);

  auto &SemanticsCtx = flang->createNewSemanticsContext();

  // As in custom_action: only flair directives get warnings here.
  auto &lang_features = const_cast<common::LanguageFeatureControl &>(
      SemanticsCtx.languageFeatures());
  lang_features.EnableWarning(common::UsageWarning::IgnoredDirective, false);

  auto wdata = std::make_shared<wdata_t>();
  wdata->collector = std::make_unique<directive_collector>(SemanticsCtx);

  for (auto &pf : files) {
    parse::Program &tree =
        SemanticsCtx.SaveParseTree(std::move(*pf.parsing->parseTree()));
    SemanticsCtx.messages().Annex(std::move(pf.parsing->messages()));
    sema::ResolveNames(SemanticsCtx, tree, SemanticsCtx.globalScope());
    parse::Walk(tree, *wdata->collector.get());
  }

  // Empty wrap set: wrap every module of the closure.
  wdata->wrap_files = std::move(wrap_files);

  return run_wrap_pipeline(SemanticsCtx, wdata, llvm::errs()) ? EXIT_SUCCESS
                                                              : EXIT_FAILURE;
}
