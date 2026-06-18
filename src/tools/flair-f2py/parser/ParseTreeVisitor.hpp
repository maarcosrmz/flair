#include <flang/Parser/parsing.h>

namespace flair::parser {

// Visitor struct that defines Pre/Post functions for different types of nodes
struct ParseTreeVisitor {
  template <typename A> bool Pre(const A &) { return true; }
  template <typename A> void Post(const A &) {}

  bool Pre(const Fortran::parser::FunctionSubprogram &);
  void Post(const Fortran::parser::FunctionStmt &f);

  bool Pre(const Fortran::parser::SubroutineSubprogram &);
  void Post(const Fortran::parser::SubroutineStmt &s);

  int fcounter{0};
  int scounter{0};

private:
  bool isInSubprogram_{false};
};

} // namespace flair::parser
