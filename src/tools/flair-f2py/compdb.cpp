#include <optional>
#include <set>
#include <stdexcept>
#include <utility>

#include "clang/Tooling/CompilationDatabase.h"
#include "clang/Tooling/JSONCompilationDatabase.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"

#include "compdb.hpp"

namespace compdb {

static std::string absolutized(llvm::StringRef path, llvm::StringRef dir) {
  llvm::SmallString<256> buf(path);
  llvm::sys::path::make_absolute(dir, buf);
  llvm::sys::path::remove_dots(buf, /*remove_dot_dot=*/true);
  return std::string(buf);
}

static bool is_fortran_source(llvm::StringRef path) {
  llvm::StringRef const ext = llvm::sys::path::extension(path);
  if (ext.empty())
    return false;
  static std::set<std::string> const fortran_exts = {
      "f", "for", "fpp", "f77", "f90", "f95", "f03", "f08", "f18"};
  return fortran_exts.count(ext.drop_front().lower()) != 0;
}

// Reduce a recorded command line to the whitelist of flags flang accepts
// and that affect parsing or semantics. Value-taking flags are normalized
// to separate `flag value` tokens; everything else (unknown flags, -c,
// -o and its value, the input file) is dropped.
static std::vector<std::string>
adapt_flags(clang::tooling::CompileCommand const &cc) {
  std::vector<std::string> out;
  auto const &argv = cc.CommandLine;
  for (std::size_t i = 1; i < argv.size(); ++i) {
    llvm::StringRef const arg(argv[i]);
    // Joined (`-Idir`) or separate (`-I dir`) option value.
    auto const value =
        [&](std::size_t opt_len) -> std::optional<llvm::StringRef> {
      if (arg.size() > opt_len)
        return arg.drop_front(opt_len);
      if (i + 1 < argv.size())
        return llvm::StringRef(argv[++i]);
      return std::nullopt;
    };
    if (arg == "-cpp" || arg == "-nocpp" || arg == "-ffree-form" ||
        arg == "-ffixed-form" || arg == "-fopenmp" || arg == "-fopenacc" ||
        arg == "-fdefault-integer-8" || arg == "-fdefault-real-8" ||
        arg == "-fdefault-double-8") {
      out.emplace_back(arg);
    } else if (arg.starts_with("-D") || arg.starts_with("-U")) {
      std::string const flag = arg.substr(0, 2).str();
      if (auto const val = value(2)) {
        out.push_back(flag);
        out.emplace_back(*val);
      }
    } else if (arg.starts_with("-I") || arg.starts_with("-J")) {
      std::string const flag = arg.substr(0, 2).str();
      if (auto const val = value(2)) {
        out.push_back(flag);
        out.push_back(absolutized(*val, cc.Directory));
      }
    } else if (arg == "-module-dir" || arg == "-fintrinsic-modules-path") {
      if (auto const val = value(arg.size())) {
        out.emplace_back(arg);
        out.push_back(absolutized(*val, cc.Directory));
      }
    }
  }
  return out;
}

std::vector<entry_t> load(std::string const &path) {
  namespace ct = clang::tooling;
  std::string error;
  std::unique_ptr<ct::CompilationDatabase> db;
  if (llvm::sys::fs::is_directory(path))
    db = ct::CompilationDatabase::loadFromDirectory(path, error);
  else
    db = ct::JSONCompilationDatabase::loadFromFile(
        path, error, ct::JSONCommandLineSyntax::AutoDetect);
  if (db == nullptr)
    throw std::runtime_error("cannot load compilation database at '" + path +
                             "': " + error);

  std::vector<entry_t> entries;
  std::set<std::string> seen;
  for (auto const &cc : db->getAllCompileCommands()) {
    std::string file = absolutized(cc.Filename, cc.Directory);
    if (not is_fortran_source(file) || not seen.insert(file).second)
      continue;
    entries.push_back(entry_t{std::move(file), cc.Directory, adapt_flags(cc)});
  }
  if (entries.empty())
    throw std::runtime_error("no Fortran entries in compilation database at '" +
                             path + "'");
  return entries;
}

void apply_parser_flags(std::vector<std::string> const &args,
                        Fortran::parser::Options &opts) {
  for (std::size_t i = 0; i < args.size(); ++i) {
    llvm::StringRef const arg(args[i]);
    if (arg == "-ffixed-form") {
      opts.isFixedForm = true;
    } else if (arg == "-ffree-form") {
      opts.isFixedForm = false;
    } else if ((arg == "-D" || arg == "-U" || arg == "-I") &&
               i + 1 < args.size()) {
      std::string const &val = args[++i];
      if (arg == "-I") {
        opts.searchDirectories.push_back(val);
      } else if (arg == "-U") {
        opts.predefinitions.emplace_back(val, std::nullopt);
      } else {
        auto const eq = val.find('=');
        if (eq == std::string::npos)
          opts.predefinitions.emplace_back(val, "1");
        else
          opts.predefinitions.emplace_back(val.substr(0, eq),
                                           val.substr(eq + 1));
      }
    }
    // Module-directory and default-kind flags act at the semantics level;
    // they are honored through the driver's CompilerInvocation instead.
  }
}

} // namespace compdb
