#pragma once
#include <flang/Semantics/attr.h>
#include <flang/Semantics/scope.h>
#include <flang/Semantics/symbol.h>

#include "../wdata.hpp"

namespace sema = Fortran::semantics;

namespace flair::semantics {

void traverse_module(sema::Symbol const &mod_sym, module_info_t &mi);

} // namespace flair::semantics
