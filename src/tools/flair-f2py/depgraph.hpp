#pragma once

#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "flang/Parser/parsing.h"

#include "compdb.hpp"

namespace depgraph {

// A parsed database entry. `parsing` owns the parse tree and stays alive so
// the tree can be handed to semantics without re-parsing.
struct parsed_file_t {
  compdb::entry_t const *entry = nullptr;
  std::unique_ptr<Fortran::parser::Parsing> parsing;
  std::vector<std::string> defined; // folded names of modules defined here
  std::set<std::string> used; // folded names of non-intrinsic USEd modules
};

// Parser options for one entry, built by the driver (per-entry flags applied
// on top of invocation-wide defaults).
using options_factory_t =
    std::function<Fortran::parser::Options(compdb::entry_t const &)>;

// Parse entries lazily outward from `entry_file` (an absolute path that must
// be in `entries`) and return its USE closure in dependency-first order,
// `entry_file` last. USEd modules that no database entry defines are left
// for semantics to resolve (intrinsic modules and external-library .mod
// files found via the recorded search directories; anything genuinely
// missing gets flang's regular error). Throws std::runtime_error on
// unparseable files or a USE cycle.
std::vector<parsed_file_t>
closure_of(std::string const &entry_file,
           std::vector<compdb::entry_t> const &entries,
           Fortran::parser::AllCookedSources &all_cooked,
           options_factory_t const &options_for);

} // namespace depgraph
