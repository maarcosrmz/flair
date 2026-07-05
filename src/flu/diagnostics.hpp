#pragma once

#include <string>

namespace llvm {
class raw_ostream;
}

namespace Fortran::parser {
class CharBlock;
} // namespace Fortran::parser

namespace Fortran::semantics {
class SemanticsContext;
class Symbol;
} // namespace Fortran::semantics

namespace sema = Fortran::semantics;
namespace parse = Fortran::parser;

namespace flu {

// Emit an error diagnostic through the flang compiler instance, located at the
// source name of `at`. Mirrors clu::emit_error, but targets flang's
// SemanticsContext instead of clang's DiagnosticsEngine. The context is fetched
// from the symbol's owning scope (Symbol::GetSemanticsContext), so no context
// needs to be threaded to the call site. Messages are queued in the context;
// call flush_messages once codegen is done to render them.
void emit_error(sema::SemanticsContext &ctx, parse::CharBlock const &at,
                std::string const &msg);
void emit_error(sema::Symbol const &at, std::string const &msg);
void emit_warning(sema::SemanticsContext &ctx, parse::CharBlock const &at,
                  std::string const &msg);
void emit_warning(sema::Symbol const &at, std::string const &msg);

// Resolve and render every message queued on `ctx` to `os`, in source order.
// Mirrors Fortran::semantics::Semantics::EmitMessages (which is not exported by
// the installed flang library for SemanticsContext directly).
void flush_messages(sema::SemanticsContext &ctx, llvm::raw_ostream &os);

} // namespace flu
