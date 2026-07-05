#include <flang/Frontend/FrontendActions.h>
#include <flang/Parser/options.h>
#include <iostream>
#include <memory>
#include <stdexcept>

#include "flang/Frontend/CompilerInstance.h"
#include "flang/Frontend/FrontendAction.h"
#include "flang/Frontend/TextDiagnosticBuffer.h"
#include "flang/Parser/parsing.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/DiagnosticOptions.h"
#include "llvm/Support/TargetSelect.h"

#include "parser/custom_action.hpp"
#include "semantics/custom_action.hpp"
#include "tools/flair-f2py/parser/directive_collector.hpp"

//====================   main    ==========================================

int main(int argc, const char **argv) try {

  // TODO: CMD line options, compilation database, logger, etc.

  bool with_sema = false;
  if (argc > 1) {
    std::string arg = argv[1];
    if (arg == "-s")
      with_sema = true;
  }

  // ------- main tool

  std::unique_ptr<Fortran::frontend::CompilerInstance> flang =
      std::make_unique<Fortran::frontend::CompilerInstance>();

  flang->createDiagnostics();
  if (!flang->hasDiagnostics())
    return 1;

  auto diagsBuffer =
      std::make_unique<Fortran::frontend::TextDiagnosticBuffer>();

  clang::DiagnosticOptions diagOpts;
  clang::DiagnosticsEngine diags(clang::DiagnosticIDs::create(), diagOpts,
                                 diagsBuffer.get(), /*ShouldOwnClient=*/false);

  llvm::SmallVector<const char *, 256> args(argv, argv + argc);
  bool success = Fortran::frontend::CompilerInvocation::createFromArgs(
      flang->getInvocation(), llvm::ArrayRef(args).slice(with_sema ? 2 : 1),
      diags, args[0]);
  if (!success)
    throw std::runtime_error("Failed creating compiler invocation.");

  llvm::InitializeAllTargets();
  llvm::InitializeAllTargetMCs();
  llvm::InitializeAllAsmPrinters();

  diagsBuffer->flushDiagnostics(flang->getDiagnostics());

  Fortran::parser::Options &fortran_opts =
      flang->getInvocation().getFortranOpts();
  fortran_opts.compilerDirectiveSentinels.push_back(FLAIR_DIRECTIVE);

  std::unique_ptr<Fortran::frontend::FrontendAction> act;
  if (with_sema)
    act = std::make_unique<flair::semantics::custom_action>();
  else
    act = std::make_unique<flair::parser::custom_action>();

  success = flang->executeAction(*act);
  flang->clearOutputFiles(false);

  if (!success)
    throw std::runtime_error("Failed to run custom_action.");
} catch (const std::exception &error) {
  std::cerr << error.what() << '\n';
  return EXIT_FAILURE;
}
