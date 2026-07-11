#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>

#include "flang/Frontend/CompilerInstance.h"
#include "flang/Frontend/FrontendAction.h"
#include "flang/Frontend/TextDiagnosticBuffer.h"
#include "flang/Parser/options.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/DiagnosticOptions.h"
#include "llvm/Support/TargetSelect.h"

#include "custom_action.hpp"

//====================   main    ==========================================

int main(int argc, const char **argv) try {

  // TODO: CMD line options, compilation database, logger, etc.

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
      flang->getInvocation(), llvm::ArrayRef(args).slice(1), diags, args[0]);
  if (!success)
    throw std::runtime_error("Failed creating compiler invocation.");

  {
    auto &ppOpts = flang->getInvocation().getPreprocessorOpts();
    auto &fortranOpts = flang->getInvocation().getFortranOpts();
    for (auto const &dir : ppOpts.searchDirectoriesFromIntrModPath)
      fortranOpts.intrinsicModuleDirectories.emplace_back(dir);
    llvm::SmallString<128> defaultIntrDir("/usr/include/flang");
    fortranOpts.intrinsicModuleDirectories.emplace_back(
        std::string(defaultIntrDir));
  }

  llvm::InitializeAllTargets();
  llvm::InitializeAllTargetMCs();
  llvm::InitializeAllAsmPrinters();

  diagsBuffer->flushDiagnostics(flang->getDiagnostics());

  Fortran::parser::Options &fortran_opts =
      flang->getInvocation().getFortranOpts();
  fortran_opts.compilerDirectiveSentinels.push_back(FLAIR_DIRECTIVE);

  auto act = std::make_unique<custom_action>();
  success = flang->executeAction(*act);
  flang->clearOutputFiles(true);

  if (not success)
    throw std::runtime_error("Failed to run custom_action.");

  return act->failed() ? EXIT_FAILURE : EXIT_SUCCESS;
} catch (const std::exception &error) {
  std::cerr << error.what() << '\n';
  return EXIT_FAILURE;
}
