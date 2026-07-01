#include "flu/diagnostics.hpp"

#include <flang/Parser/message.h>
#include <flang/Semantics/semantics.h>
#include <flang/Semantics/symbol.h>

namespace flu {

using namespace Fortran::parser::literals; // "..."_err_en_US

void emit_error(sema::Symbol const &at, std::string const &msg) {
  at.GetSemanticsContext().Say(at.name(), "%s"_err_en_US, msg);
}

// Mirrors Semantics::EmitMessages
void flush_messages(sema::SemanticsContext &ctx, llvm::raw_ostream &os) {
  ctx.messages().ResolveProvenances(ctx.allCookedSources());
  ctx.messages().Emit(os, ctx.allCookedSources(), /*echoSourceLines=*/true,
                      &ctx.languageFeatures(), ctx.maxErrors(),
                      ctx.warningsAreErrors());
}

} // namespace flu
