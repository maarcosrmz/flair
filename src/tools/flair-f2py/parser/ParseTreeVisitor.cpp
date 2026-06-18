#include <flang/Parser/parse-tree.h>
#include <iostream>

#include "ParseTreeVisitor.hpp"

namespace flair::parser {

bool ParseTreeVisitor::Pre(const Fortran::parser::FunctionSubprogram &) {
  isInSubprogram_ = true;
  return true;
}
void ParseTreeVisitor::Post(const Fortran::parser::FunctionStmt &f) {
  if (isInSubprogram_) {
    std::cout << "Function:\t"
              << std::get<Fortran::parser::Name>(f.t).ToString() << "\n";
    fcounter++;
    isInSubprogram_ = false;
  }
}

bool ParseTreeVisitor::Pre(const Fortran::parser::SubroutineSubprogram &) {
  isInSubprogram_ = true;
  return true;
}
void ParseTreeVisitor::Post(const Fortran::parser::SubroutineStmt &s) {
  if (isInSubprogram_) {
    std::cout << "Subroutine:\t"
              << std::get<Fortran::parser::Name>(s.t).ToString() << "\n";
    scounter++;
    isInSubprogram_ = false;
  }
}

}; // namespace flair::parser
