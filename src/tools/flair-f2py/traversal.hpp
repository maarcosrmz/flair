#pragma once
#include <flang/Semantics/attr.h>
#include <flang/Semantics/scope.h>
#include <flang/Semantics/symbol.h>

#include "wdata.hpp"

using namespace Fortran;

void traverse_module(semantics::Symbol const &mod_sym, module_info_t &mi);
