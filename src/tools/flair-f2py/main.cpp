#include <cstdlib>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "flang/Support/Version.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"

#include "codegen/utils.hpp"
#include "compdb_driver.hpp"
#include "custom_action.hpp"
#include "flu/logger.hpp"

// ==========  Options of the program ======================================

static constexpr char usage[] = R"HELPDOC(
  flair-f2py generates Python bindings for Fortran.

  Usage:
   flair-f2py mod.F90 [flang options]             Wrap every module of every input
   flair-f2py a.F90 b.F90 --wrap a.F90            Wrap a.F90; b.F90 only resolves symbols
   flair-f2py --compdb DIR --entry main.F90       Wrap main.F90's USE closure, from
                                                  DIR/compile_commands.json

  Inputs must be given in dependency order: USE statements resolve against the
  modules earlier inputs contribute to one shared scope, not against .mod files.
  Unrecognized options are passed through to flang, so its flags can be mixed in
  (notably -fintrinsic-modules-path DIR, which flair-f2py does not infer).

  Wrap set:
   --wrap FILE                                    Wrap the modules FILE defines (repeatable).
                                                  Without any --wrap, every input is wrapped.
                                                  The set is then closed over its converter
                                                  producers: modules defining a derived type
                                                  that crosses a wrapped module's API, whose
                                                  converters the consumer use-associates.
   --wrap @entry                                  Stands for the entry's own modules plus the
                                                  modules it USEs directly (--compdb only).
   --external MODULE                              Treat MODULE as unwrappable (repeatable):
                                                  it never joins the wrap set, and procedures,
                                                  components and variables carrying its types
                                                  are skipped with a warning instead of being
                                                  wired to a converter that will never exist.
                                                  --compdb additionally infers this for every
                                                  module resolved from a precompiled .mod,
                                                  since it parses each database entry from
                                                  source; name third-party modules explicitly
                                                  when their sources are in the database.

  Compilation-database mode:
   --compdb DIR                                   Discover the entry's USE closure from
                                                  DIR/compile_commands.json and parse each
                                                  file with its own recorded flags. Emits one
                                                  combined package extension plus the script
                                                  that builds it.
   --entry FILE                                   Root of the closure. Required with --compdb.
   --pkg NAME                                     Package name (default: the entry's stem).

  Other options:
   -v, --verbose                                  Verbose output (also FLAIR_VERBOSE=N)
   -h, --help                                     Show this help
   --version                                      Show version information
)HELPDOC";

static void print_version(llvm::raw_ostream &os) {
  os << "flair-f2py version " << FLAIR_VERSION << " (git hash " << GIT_HASH
     << ")\n";
  os << "  Based on " << Fortran::common::getFlangToolFullVersion("flang")
     << "\n";
  os.flush();
}

// flair's own options, extracted before flang parses the command line: flang's
// option table rejects unknown flags, so they cannot simply be left in place.
// Everything not listed here is passed through to flang untouched -- which is
// also why llvm::cl is not used: flang flags take separate-word values, and a
// cl positional list would swallow those values as input files.
struct options_t {
  std::vector<std::string> wrap_files; // --wrap FILE (repeatable), @entry token
  std::set<std::string> external_modules; // --external MODULE (repeatable)
  std::string compdb_path;                // --compdb DIR
  std::string entry_file;              // --entry FILE
  std::string pkg_name;                // --pkg NAME
  llvm::SmallVector<const char *, 256> passthrough; // flang's own arguments
  bool help = false;                                // -h / --help
  bool version = false;                             // --version
};

static options_t parse_options(int argc, const char **argv) {
  options_t opts;
  int verbose = 0;
  for (int i = 1; i < argc; ++i) {
    std::string_view const arg(argv[i]);
    auto const option_value = [&](std::string_view opt) {
      if (i + 1 == argc)
        throw std::runtime_error(std::string(opt) + " requires an argument.");
      return argv[++i];
    };
    if (arg == "--wrap")
      opts.wrap_files.emplace_back(option_value(arg));
    // Folded here so the set matches however the module is spelled in source.
    else if (arg == "--external")
      opts.external_modules.insert(codegen::fold_lower(option_value(arg)));
    else if (arg == "--compdb")
      opts.compdb_path = option_value(arg);
    else if (arg == "--entry")
      opts.entry_file = option_value(arg);
    else if (arg == "--pkg")
      opts.pkg_name = option_value(arg);
    else if (arg == "-h" || arg == "--help")
      opts.help = true;
    else if (arg == "--version")
      opts.version = true;
    // Swallowed rather than passed on: flang's -v reports driver command
    // lines, and flair-f2py never spawns a driver.
    else if (arg == "-v" || arg == "--verbose")
      ++verbose;
    else
      opts.passthrough.push_back(argv[i]);
  }
  if (verbose > 0)
    flu::logger::set_verbose(verbose);
  return opts;
}

// Rejects flag combinations that have no meaning, so they fail before any
// input is parsed rather than half-way through a run.
static void validate_options(options_t const &opts) {
  if (not opts.compdb_path.empty() || not opts.entry_file.empty()) {
    if (opts.compdb_path.empty() || opts.entry_file.empty())
      throw std::runtime_error("--compdb and --entry must be used together.");
    return;
  }
  if (not opts.pkg_name.empty())
    throw std::runtime_error("--pkg requires --compdb mode.");
  for (auto const &w : opts.wrap_files)
    if (w == WRAP_ENTRY_TOKEN)
      throw std::runtime_error("--wrap " + std::string(WRAP_ENTRY_TOKEN) +
                               " requires --compdb mode.");
}

//====================   main    ==========================================

int main(int argc, const char **argv) try {

  // Only a progress reporter: every failure below throws and is reported by
  // the handler at the bottom, so that mode functions and main share one path.
  flu::logger const report = flu::logger::report();

  // Invoked with nothing to do: the usage block is the answer, but this is a
  // misuse, so it goes to stderr and fails (--help prints it on request).
  if (argc == 1) {
    llvm::errs() << usage;
    return EXIT_FAILURE;
  }

  // ----- Parse the options in the command line
  options_t opts = parse_options(argc, argv);

  if (opts.help) {
    llvm::outs() << usage;
    return EXIT_SUCCESS;
  }
  if (opts.version) {
    print_version(llvm::outs());
    return EXIT_SUCCESS;
  }

  validate_options(opts);
  report("Based on {}", Fortran::common::getFlangToolFullVersion("flang"));

  llvm::InitializeAllTargets();
  llvm::InitializeAllTargetMCs();
  llvm::InitializeAllAsmPrinters();

  // ------- compilation-database mode
  if (not opts.compdb_path.empty()) {
    report("Compilation-database mode: entry {}", opts.entry_file);
    return run_compdb_mode(opts.compdb_path, opts.entry_file,
                           std::move(opts.pkg_name), std::move(opts.wrap_files),
                           std::move(opts.external_modules),
                           llvm::ArrayRef(opts.passthrough), argv[0]);
  }

  // ------- main tool
  return run_single_mode(llvm::ArrayRef(opts.passthrough),
                         std::move(opts.wrap_files),
                         std::move(opts.external_modules), argv[0]);
} catch (const std::exception &error) {
  flu::logger::error()(error.what());
  return EXIT_FAILURE;
}
