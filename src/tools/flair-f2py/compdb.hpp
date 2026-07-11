#pragma once

#include <string>
#include <vector>

#include "flang/Parser/options.h"

namespace compdb {

// One compilation-database entry, reduced to what the Fortran frontend
// needs: the compiled source and the adapted (flang-compatible) flags.
struct entry_t {
  std::string file;              // absolute path of the compiled source
  std::string directory;         // working directory of the compilation
  std::vector<std::string> args; // adapted flags, see adapt_flags note below
};

// Load compile_commands.json from `path` (the file itself or a directory
// containing it). Only Fortran sources are kept; duplicate entries for the
// same file (multi-config builds) keep the first occurrence. Entries may
// record another compiler (gfortran), so flags are reduced to a whitelist
// flang accepts (-D/-U, -I, -J/-module-dir, -fintrinsic-modules-path, form
// and default-kind flags); path-valued flags are absolutized against the
// entry's directory. Throws std::runtime_error on failure.
std::vector<entry_t> load(std::string const &path);

// Apply the parser-relevant subset of an entry's adapted flags
// (predefinitions, include search directories, source form) on top of
// invocation-wide defaults.
void apply_parser_flags(std::vector<std::string> const &args,
                        Fortran::parser::Options &opts);

} // namespace compdb
