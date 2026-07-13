#include <algorithm>
#include <cctype>
#include <map>
#include <optional>
#include <stdexcept>

#include "flang/Parser/parse-tree-visitor.h"
#include "flang/Parser/parse-tree.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"

#include "depgraph.hpp"

namespace depgraph {

namespace parse = Fortran::parser;

static std::string folded(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return s;
}

static std::string folded(parse::Name const &n) { return folded(n.ToString()); }

// Collects the module names a parse tree defines and USEs.
struct module_scan_visitor {
  std::vector<std::string> &defined;
  std::set<std::string> &used;

  template <typename T> bool Pre(const T &) { return true; }
  template <typename T> void Post(const T &) {}

  void Post(parse::ModuleStmt const &stmt) {
    defined.push_back(folded(stmt.v));
  }

  void Post(parse::UseStmt const &stmt) {
    if (stmt.nature && *stmt.nature == parse::UseStmt::ModuleNature::Intrinsic)
      return;
    used.insert(folded(stmt.moduleName));
  }

  // A submodule depends on its ancestor module; a parent submodule is
  // reached through the ancestor's file as well.
  void Post(parse::SubmoduleStmt const &stmt) {
    auto const &parent = std::get<parse::ParentIdentifier>(stmt.t);
    used.insert(folded(std::get<0>(parent.t)));
  }
};

static parsed_file_t parse_entry(compdb::entry_t const &entry,
                                 parse::AllCookedSources &all_cooked,
                                 options_factory_t const &options_for) {
  parsed_file_t pf;
  pf.entry = &entry;
  pf.parsing = std::make_unique<parse::Parsing>(all_cooked);

  pf.parsing->Prescan(entry.file, options_for(entry));
  if (not pf.parsing->messages().AnyFatalError()) {
    llvm::raw_null_ostream null_stream;
    pf.parsing->Parse(null_stream);
  }
  if (pf.parsing->messages().AnyFatalError() ||
      not pf.parsing->consumedWholeFile() || not pf.parsing->parseTree()) {
    pf.parsing->messages().Emit(llvm::errs(), all_cooked);
    throw std::runtime_error("failed to parse '" + entry.file + "'");
  }

  module_scan_visitor visitor{pf.defined, pf.used};
  parse::Walk(*pf.parsing->parseTree(), visitor);
  for (auto const &def : pf.defined) // same-file USEs are not dependencies
    pf.used.erase(def);
  return pf;
}

std::vector<parsed_file_t> closure_of(
    std::vector<std::string> const &root_files,
    std::vector<compdb::entry_t> const &entries,
    parse::AllCookedSources &all_cooked, options_factory_t const &options_for) {
  std::vector<std::size_t> root_indices;
  for (auto const &root : root_files) {
    std::size_t idx = entries.size();
    for (std::size_t i = 0; i < entries.size(); ++i)
      if (entries[i].file == root)
        idx = i;
    if (idx == entries.size())
      throw std::runtime_error("file '" + root +
                               "' is not in the compilation database");
    root_indices.push_back(idx);
  }

  std::map<std::size_t, parsed_file_t> parsed;
  std::map<std::string, std::size_t> module_to_entry;
  std::size_t scan_next = 0; // next entry to parse when a module is unlocated

  auto const ensure_parsed = [&](std::size_t idx) -> parsed_file_t & {
    auto const [it, inserted] = parsed.try_emplace(idx);
    if (inserted) {
      it->second = parse_entry(entries[idx], all_cooked, options_for);
      for (auto const &mod : it->second.defined)
        module_to_entry.try_emplace(mod, idx);
    }
    return it->second;
  };

  // Which entry defines `mod`: consult the index, then the filename-stem
  // convention, then parse remaining entries in database order until found.
  auto const locate =
      [&](std::string const &mod) -> std::optional<std::size_t> {
    if (auto const it = module_to_entry.find(mod); it != module_to_entry.end())
      return it->second;
    for (std::size_t i = 0; i < entries.size(); ++i) {
      if (parsed.count(i) != 0 ||
          folded(llvm::sys::path::stem(entries[i].file).str()) != mod)
        continue;
      ensure_parsed(i);
      if (auto const it = module_to_entry.find(mod);
          it != module_to_entry.end())
        return it->second;
    }
    while (scan_next < entries.size()) {
      std::size_t const i = scan_next++;
      if (parsed.count(i) != 0)
        continue;
      ensure_parsed(i);
      if (auto const it = module_to_entry.find(mod);
          it != module_to_entry.end())
        return it->second;
    }
    return std::nullopt;
  };

  enum class visit_state { InProgress, Done };
  std::map<std::size_t, visit_state> state;
  std::vector<std::size_t> order; // post-order == dependency-first
  std::function<void(std::size_t)> const visit = [&](std::size_t idx) {
    if (auto const it = state.find(idx); it != state.end()) {
      if (it->second == visit_state::InProgress)
        throw std::runtime_error("cycle in module dependencies involving '" +
                                 entries[idx].file + "'");
      return;
    }
    state[idx] = visit_state::InProgress;
    parsed_file_t &pf = ensure_parsed(idx);
    for (auto const &mod : pf.used)
      if (auto const dep = locate(mod))
        visit(*dep);
    // Unlocated modules are left for semantics (intrinsics and
    // external-library .mod files resolve through the search directories).
    state[idx] = visit_state::Done;
    order.push_back(idx);
  };
  for (std::size_t const idx : root_indices)
    visit(idx);

  std::vector<parsed_file_t> result;
  result.reserve(order.size());
  for (std::size_t const idx : order)
    result.push_back(std::move(parsed[idx]));
  return result;
}

} // namespace depgraph
