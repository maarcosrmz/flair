#pragma once
#include <flang/Semantics/attr.h>
#include <flang/Semantics/scope.h>
#include <flang/Semantics/symbol.h>

#include "wdata.hpp"

namespace sema = Fortran::semantics;

void traverse_global_scope(sema::Scope const &global_scope,
                           std::shared_ptr<wdata_t> wdata);
