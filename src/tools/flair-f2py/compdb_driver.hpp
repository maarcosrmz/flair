#pragma once

#include <set>
#include <string>
#include <vector>

#include "llvm/ADT/ArrayRef.h"

// `--wrap` token standing for the entry's own modules plus the modules it
// USEs directly, expanded once the closure is known.
inline constexpr char WRAP_ENTRY_TOKEN[] = "@entry";

// Compilation-database mode: discover the USE closure of `entry_file` from
// the database at `compdb_path`, resolve it (dependency-first, each file
// parsed with its own recorded flags) into one shared semantics scope, and
// wrap the closure's modules (when `wrap_files` is non-empty, only the
// modules defined in those files, with WRAP_ENTRY_TOKEN expanded) into one
// combined package extension named `pkg_name` (default: derived from the
// entry's file stem). `passthrough_args` are the remaining command-line
// flags; they are applied on top of the entry's recorded flags and win on
// conflicts. `external_modules` (folded) names modules whose types can never
// be converted; this mode also treats every module resolved from a
// precompiled .mod as external, since each database entry is parsed from
// source. Returns a process exit code.
int run_compdb_mode(std::string const &compdb_path,
                    std::string const &entry_file, std::string pkg_name,
                    std::vector<std::string> wrap_files,
                    std::set<std::string> external_modules,
                    llvm::ArrayRef<const char *> passthrough_args,
                    const char *argv0);
