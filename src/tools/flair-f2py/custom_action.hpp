#include "flang/Frontend/FrontendActions.h"
#include "wdata.hpp"

// -----------------------------------------------
// custom_action: FrontendAction that drives the full binding-generation pipeline.
// executeAction() traverses the Semantics object while filling wdata,
// before writing the generated .wrap.f90 files.
class custom_action : public Fortran::frontend::PrescanAndSemaAction {
  std::shared_ptr<wdata_t> wdata;

  public:
  custom_action() = default;

  void executeAction() override;
};
