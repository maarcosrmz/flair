#pragma once
#include "../wdata.hpp"
#include "flang/Frontend/FrontendActions.h"

namespace flair::semantics {

// ---------------------------------------------------------------------
// semantics::custom_action: FrontendAction that is executed after semantic
// analysis and drives the full binding-generation pipeline.
//
// executeAction() traverses the global semantics scope while filling wdata,
// before writing the generated .wrap.f90 files.
// ---------------------------------------------------------------------
class custom_action : public Fortran::frontend::PrescanAndSemaAction {
  std::shared_ptr<wdata_t> wdata;

public:
  explicit custom_action() { wdata = std::make_shared<wdata_t>(); }

  void executeAction() override;
};

} // namespace flair::semantics
