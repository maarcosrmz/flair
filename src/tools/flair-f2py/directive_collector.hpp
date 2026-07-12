#pragma once

#include "flang/Parser/parse-tree.h"
#include "flang/Semantics/semantics.h"

#include <unordered_set>

#define FLAIR_DIRECTIVE "flair$"

namespace sema = Fortran::semantics;
namespace parse = Fortran::parser;

struct directive_collector {
public:
  // Namea of symbols to be ignored
  std::unordered_set<std::string> ignore;

  // Names of callbacks to be wrapped
  std::unordered_set<std::string> callbacks;

  // Maps a procedure name to all the names of (polymorphic) types,
  // for which the procedure should be instantiated in the wrapper
  std::map<std::string, std::unordered_set<std::string>> instantiate;

private:
  enum FlairDirective { NONE, IGNORE, CALLBACK, INSTANTIATE };
  const std::map<std::string, FlairDirective> kind_of = {
      {"ignore", FlairDirective::IGNORE},
      {"callback", FlairDirective::CALLBACK},
      {"instantiate", FlairDirective::INSTANTIATE}};

  sema::SemanticsContext &context;
  std::unordered_set<std::string> types;
  FlairDirective kind = NONE;
  bool first = false;
  bool inside_directive = false;

public:
  directive_collector(sema::SemanticsContext &context) : context{context} {}

  template <typename T> bool Pre(const T &) { return true; }
  template <typename T> void Post(const T &) {}

  bool Pre(const parse::CompilerDirective &cd);
  void Post(const parse::CompilerDirective &cd);
  bool Pre(const parse::Name &name);
  void Post(const parse::Name &name);
  // Declarations whose first parse::Name in tree order is not the declared
  // entity (a function's result-type prefix, a type's extends(...) attribute)
  // attach a pending directive to the entity name explicitly.
  bool Pre(const parse::FunctionStmt &stmt);
  bool Pre(const parse::DerivedTypeStmt &stmt);

private:
  void attach(const parse::Name &name);

  static std::string directive_sentinel(const parse::CharBlock &body);
};
