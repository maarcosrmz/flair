#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <vector>

#include "flang/Frontend/CompilerInstance.h"
#include "flang/Frontend/FrontendAction.h"
#include "flang/Frontend/TextDiagnosticBuffer.h"
#include "flang/Parser/options.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/DiagnosticOptions.h"
#include "llvm/Support/TargetSelect.h"

#include "compdb_driver.hpp"
#include "custom_action.hpp"

//====================   main    ==========================================

int main(int argc, const char **argv) try {

  // Extract flair-specific options before Flang parses the command line
  // (its option table rejects unknown flags). `--wrap <file>` (repeatable)
  // restricts wrapping to the modules defined in the given input files; the
  // remaining inputs are resolved for their symbols only. `--compdb <path>`
  // together with `--entry <file>` switches to compilation-database mode.
  std::vector<std::string> wrap_files;
  std::string compdb_path;
  std::string entry_file;
  llvm::SmallVector<const char *, 256> args;
  args.push_back(argv[0]);
  for (int i = 1; i < argc; ++i) {
    std::string_view const arg(argv[i]);
    auto const option_value = [&](std::string_view opt) {
      if (i + 1 == argc)
        throw std::runtime_error(std::string(opt) + " requires an argument.");
      return argv[++i];
    };
    if (arg == "--wrap")
      wrap_files.emplace_back(option_value(arg));
    else if (arg == "--compdb")
      compdb_path = option_value(arg);
    else if (arg == "--entry")
      entry_file = option_value(arg);
    else
      args.push_back(argv[i]);
  }

  llvm::InitializeAllTargets();
  llvm::InitializeAllTargetMCs();
  llvm::InitializeAllAsmPrinters();

  if (!compdb_path.empty() || !entry_file.empty()) {
    if (compdb_path.empty() || entry_file.empty())
      throw std::runtime_error("--compdb and --entry must be used together.");
    return run_compdb_mode(compdb_path, entry_file, std::move(wrap_files),
                           llvm::ArrayRef(args).slice(1), args[0]);
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

  diagsBuffer->flushDiagnostics(flang->getDiagnostics());

  Fortran::parser::Options &fortran_opts =
      flang->getInvocation().getFortranOpts();
  fortran_opts.compilerDirectiveSentinels.push_back(FLAIR_DIRECTIVE);

  auto act = std::make_unique<custom_action>();
  act->set_wrap_files(std::move(wrap_files));
  success = flang->executeAction(*act);
  flang->clearOutputFiles(true);

  if (not success)
    throw std::runtime_error("Failed to run custom_action.");

  return act->failed() ? EXIT_FAILURE : EXIT_SUCCESS;
} catch (const std::exception &error) {
  std::cerr << error.what() << '\n';
  return EXIT_FAILURE;
}
