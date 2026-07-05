#include "flu/diagnostics.hpp"

#include "flang/Parser/char-block.h"
#include "flang/Parser/message.h"
#include "flang/Semantics/semantics.h"
#include "flang/Semantics/symbol.h"

namespace flu {

using namespace Fortran::parser::literals; // "..."_err_en_US

void emit_error(Fortran::semantics::SemanticsContext &ctx,
                Fortran::parser::CharBlock const &at, std::string const &msg) {
  ctx.Say(at, "%s"_err_en_US, msg);
}

void emit_error(sema::Symbol const &at, std::string const &msg) {
  emit_error(at.GetSemanticsContext(), at.name(), msg);
}

void emit_warning(Fortran::semantics::SemanticsContext &ctx,
                  Fortran::parser::CharBlock const &at,
                  std::string const &msg) {
  ctx.Say(at, "%s"_warn_en_US, msg);
}

void emit_warning(sema::Symbol const &at, std::string const &msg) {
  emit_warning(at.GetSemanticsContext(), at.name(), msg);
}

// Mirrors Semantics::EmitMessages
void flush_messages(sema::SemanticsContext &ctx, llvm::raw_ostream &os) {
  ctx.messages().ResolveProvenances(ctx.allCookedSources());
  ctx.messages().Emit(os, ctx.allCookedSources(), /*echoSourceLines=*/true,
                      &ctx.languageFeatures(), ctx.maxErrors(),
                      ctx.warningsAreErrors());
  ctx.messages().clear();
}

} // namespace flu
