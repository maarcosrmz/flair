#include <memory>
#include <stdexcept>
#include <iostream>

#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/DiagnosticOptions.h"
#include "flang/Frontend/CompilerInstance.h"
#include "flang/Frontend/FrontendAction.h"
#include "flang/Frontend/TextDiagnosticBuffer.h"
#include "llvm/Support/TargetSelect.h"

#include "custom_action.hpp"

using namespace Fortran::frontend;

//====================   main    ==========================================

int main(int argc, const char **argv) try {

  // TODO: CMD line options, compilation database, logger, etc.

  // ------- main tool
  
  std::unique_ptr<CompilerInstance> flang = std::make_unique<CompilerInstance>();

  flang->createDiagnostics();
  if (!flang->hasDiagnostics())
    return 1;

  auto diagsBuffer = std::make_unique<TextDiagnosticBuffer>();

  clang::DiagnosticOptions diagOpts;
  clang::DiagnosticsEngine diags(clang::DiagnosticIDs::create(), diagOpts, diagsBuffer.get(), /*ShouldOwnClient=*/false);

  llvm::SmallVector<const char *, 256> args(argv, argv + argc);
  bool success = CompilerInvocation::createFromArgs(
          flang->getInvocation(), llvm::ArrayRef(args).slice(1), diags, args[0]);
  if (!success)
    throw std::runtime_error("Failed creating compiler invocation.");

  // Workaround for flang 22.1.5 bugs in intrinsic module dir handling:
  // 1. -fintrinsic-modules-path dirs are only added to searchDirectories, not
  //    intrinsicModuleDirectories (use,intrinsic:: uses the latter).
  // 2. Default intrinsic dir is derived from /proc/self/exe (clair-f2py), not args[0].
  {
    auto &ppOpts      = flang->getInvocation().getPreprocessorOpts();
    auto &fortranOpts = flang->getInvocation().getFortranOpts();
    for (auto const &dir : ppOpts.searchDirectoriesFromIntrModPath)
      fortranOpts.intrinsicModuleDirectories.emplace_back(dir);
    llvm::SmallString<128> defaultIntrDir("/usr/include/flang");
    fortranOpts.intrinsicModuleDirectories.emplace_back(std::string(defaultIntrDir));
  }

  llvm::InitializeAllTargets();
  llvm::InitializeAllTargetMCs();
  llvm::InitializeAllAsmPrinters();

  diagsBuffer->flushDiagnostics(flang->getDiagnostics());

  const std::unique_ptr<custom_action> act = std::make_unique<custom_action>();
  success = flang->executeAction(*act);

  flang->clearOutputFiles(false);

  if (!success)
    throw std::runtime_error("Failed to run custom_action.");
} catch (const std::exception &error) {
  std::cerr << error.what() << '\n';
  return EXIT_FAILURE;
}
