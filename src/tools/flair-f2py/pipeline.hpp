#pragma once

#include "flang/Semantics/semantics.h"
#include "llvm/Support/raw_ostream.h"

#include "wdata.hpp"

// Traversal + codegen over a fully resolved global scope: fills wdata from
// the semantics context, writes the generated py_*.F90 wrappers to the
// working directory, and flushes accumulated diagnostics to `out`. False
// when a fatal diagnostic aborted generation (nothing is written then).
// Shared by the multi-input frontend action and the compilation-database
// driver.
bool run_wrap_pipeline(sema::SemanticsContext &context,
                       std::shared_ptr<wdata_t> wdata, llvm::raw_ostream &out);
