#pragma once
#include "flang/Semantics/scope.h"
#include "flang/Semantics/semantics.h"
#include "flang/Semantics/symbol.h"

#include "wdata.hpp"

#include <unordered_set>

namespace sema = Fortran::semantics;

struct state {
  module_info_t &mi;
  const std::unordered_set<const sema::Symbol *> &ignored;
  const std::map<std::string, std::unordered_set<std::string>> &instantiate;
  bool default_private = false;

  bool ignore(sema::Symbol const &sym);
  std::vector<str_t> instantiate_types(sema::Symbol const &sym) const;
};

void traverse_global_scope(sema::Scope const &global_scope,
                           std::shared_ptr<wdata_t> wdata,
                           sema::SemanticsContext &context);
