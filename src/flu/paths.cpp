#include "flu/paths.hpp"

#include <flang/Semantics/semantics.h>
#include <flang/Semantics/symbol.h>
#include <llvm/ADT/SmallString.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/Path.h>

namespace flu {

std::string normalized_path(std::string const &path) {
  llvm::SmallString<256> buf(path);
  if (llvm::sys::fs::make_absolute(buf))
    return path; // cwd unavailable; compare the path as given
  llvm::sys::path::remove_dots(buf, /*remove_dot_dot=*/true);
  return std::string(buf);
}

std::optional<std::string> defining_path(sema::SemanticsContext &context,
                                         sema::Symbol const &mod_sym) {
  auto const *scope = mod_sym.get<sema::ModuleDetails>().scope();
  Fortran::parser::CharBlock const src =
      scope != nullptr ? scope->sourceRange() : mod_sym.name();
  if (auto const pr = context.allCookedSources().GetProvenanceRange(src))
    return context.allCookedSources().allSources().GetPath(pr->start(),
                                                           /*topLevel=*/true);
  return std::nullopt;
}

} // namespace flu
