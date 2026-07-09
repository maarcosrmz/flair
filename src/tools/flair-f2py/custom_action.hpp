#pragma once

#include "flang/Frontend/FrontendActions.h"

#include "wdata.hpp"

// ---------------------------------------------------------------------
// custom_action: FrontendAction that is executed after parsing
// and drives the full binding-generation pipeline.
//
// executeAction() traverses the global semantics scope while filling wdata,
// before writing the generated .wrap.f90 files.
// ---------------------------------------------------------------------
class custom_action : public Fortran::frontend::PrescanAndParseAction {
  std::shared_ptr<wdata_t> wdata;
  sema::SemanticsContext *context = nullptr;
  bool failed_ = false; // a fatal codegen diagnostic aborted generation

public:
  explicit custom_action() { wdata = std::make_shared<wdata_t>(); }

  void executeAction() override;

  [[nodiscard]] bool failed() const { return failed_; }

private:
  bool initSemantics();
  void traverseSemantics();
  void codegen();
};
