#include "flang/Parser/parse-tree.h"

#include "directive_collector.hpp"
#include "flu/diagnostics.hpp"

bool directive_collector::Pre(const parse::CompilerDirective &cd) {
  const std::string &dir = directive_sentinel(cd.source);
  if (dir != FLAIR_DIRECTIVE)
    return false;

  inside_directive = true;
  first = true;
  return true;
}

void directive_collector::Post(const parse::CompilerDirective &) {
  inside_directive = false;
}

bool directive_collector::Pre(const parse::Name &name) {
  // first name of compiler directive is the actual flair directive
  // e.g. !flair$ ignore
  if (first) {
    if (auto k = kind_of.find(name.ToString()); k != kind_of.end()) {
      kind = k->second;
      return true;
    } else {
      flu::emit_warning(context, name.source,
                        "Unrecognized 'flair$' directive ignored here");
      return true;
    }
  }

  if (inside_directive) {
    // We only care about the names if we recognize an
    // available flair directive
    if (FlairDirective::NONE != kind)
      types.insert(name.ToString());
    return false;
  }

  // Once we reach the actual node annotated with the
  // compiler directive, save it in the according data structure
  attach(name);
  return false;
}

void directive_collector::Post(const parse::Name &) { first = false; }

bool directive_collector::Pre(const parse::FunctionStmt &stmt) {
  if (!inside_directive)
    attach(std::get<parse::Name>(stmt.t));
  return true;
}

bool directive_collector::Pre(const parse::DerivedTypeStmt &stmt) {
  if (!inside_directive)
    attach(std::get<parse::Name>(stmt.t));
  return true;
}

void directive_collector::attach(const parse::Name &name) {
  if (kind != FlairDirective::NONE && name.symbol == nullptr) {
    flu::emit_warning(context, name.source,
                      "'flair$' directive on an unresolved name ignored here");
    kind = FlairDirective::NONE;
    return;
  }

  switch (kind) {
  case FlairDirective::IGNORE:
    ignore.insert(&name.symbol->GetUltimate());
    break;
  case FlairDirective::CALLBACK:
    callbacks.insert(&name.symbol->GetUltimate());
    break;
  case FlairDirective::INSTANTIATE:
    instantiate.insert_or_assign(name.ToString(), std::move(types));
    types.clear();
    break;
  default /*FlairDirective::NONE*/:
    // ignore
    break;
  }

  // reset
  kind = FlairDirective::NONE;
}

std::string
directive_collector::directive_sentinel(const parse::CharBlock &body) {
  const char *p = body.begin();
  const char *dollar = nullptr;
  for (const char *q = p;; --q) {
    if (*q == '$' && !dollar)
      dollar = q;
    if (*q == '!')
      return dollar ? std::string(q + 1, dollar - q) : std::string{};
  }
}
