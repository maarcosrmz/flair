#pragma once
#include <flang/Semantics/attr.h>
#include <flang/Semantics/scope.h>
#include <flang/Semantics/symbol.h>

#include "wdata.hpp"

namespace sema = Fortran::semantics;

struct state {
  module_info_t &mi;
  const std::set<std::string> &ignored;
  bool default_private = false;

  bool ignore(sema::Symbol const &sym);
};

void traverse_global_scope(sema::Scope const &global_scope,
                           std::shared_ptr<wdata_t> wdata);
