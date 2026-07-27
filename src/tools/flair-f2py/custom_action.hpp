#pragma once

#include <string>
#include <vector>

#include "flang/Frontend/FrontendActions.h"
#include "llvm/ADT/ArrayRef.h"

#include "wdata.hpp"

// ---------------------------------------------------------------------
// custom_action: FrontendAction that is executed after parsing
// and drives the full binding-generation pipeline.
//
// executeAction() runs once per input file. Every file is name-resolved
// into one shared SemanticsContext, so modules defined by earlier inputs
// satisfy USE statements of later ones without .mod files (inputs must be
// given in dependency order). After the last input it traverses the global
// semantics scope while filling wdata, before writing the generated
// .wrap.f90 files.
// ---------------------------------------------------------------------
class custom_action : public Fortran::frontend::PrescanAndParseAction {
  std::shared_ptr<wdata_t> wdata;
  sema::SemanticsContext *context = nullptr; // shared by all input files
  std::size_t files_seen = 0;
  bool failed_ = false; // a fatal codegen diagnostic aborted generation

public:
  explicit custom_action() { wdata = std::make_shared<wdata_t>(); }

  void set_wrap_files(std::vector<std::string> files) {
    wdata->wrap_files = std::move(files);
  }

  void executeAction() override;

  [[nodiscard]] bool failed() const { return failed_; }

private:
  bool initSemantics();
};

// Single-invocation mode: run custom_action over the inputs named in `args`
// (which are also the flang flags, passed through untouched), wrapping the
// modules of the `wrap_files` set -- or every input module when it is empty.
// Inputs must be given in dependency order, since USE statements resolve
// against the modules earlier inputs contributed to the shared scope rather
// than against .mod files. Returns a process exit code.
int run_single_mode(llvm::ArrayRef<const char *> args,
                    std::vector<std::string> wrap_files, const char *argv0);
