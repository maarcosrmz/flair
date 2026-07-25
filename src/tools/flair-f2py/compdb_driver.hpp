#pragma once

#include <string>
#include <vector>

#include "llvm/ADT/ArrayRef.h"

// Compilation-database mode: discover the USE closure of `entry_file` from
// the database at `compdb_path`, resolve it (dependency-first, each file
// parsed with its own recorded flags) into one shared semantics scope, and
// wrap the closure's modules (restricted to `wrap_files` when non-empty)
// into one combined package extension named `pkg_name` (default: derived
// from the entry's file stem). `passthrough_args` are the remaining
// command-line flags; they are applied on top of the entry's recorded flags
// and win on conflicts. Returns a process exit code.
int run_compdb_mode(std::string const &compdb_path,
                    std::string const &entry_file, std::string pkg_name,
                    std::vector<std::string> wrap_files,
                    llvm::ArrayRef<const char *> passthrough_args,
                    const char *argv0);
