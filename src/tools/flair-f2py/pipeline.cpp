#include <fstream>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "codegen/codegen.hpp"
#include "codegen/functions.hpp"
#include "flu/diagnostics.hpp"
#include "pipeline.hpp"
#include "traversal.hpp"

using namespace codegen;

bool run_wrap_pipeline(sema::SemanticsContext &context,
                       std::shared_ptr<wdata_t> wdata, llvm::raw_ostream &out) {
  traverse_global_scope(context.globalScope(), wdata, context);

  note_run_modules(wdata->modules);
  std::vector<std::pair<std::string, std::string>> outputs;
  for (auto const &mi : wdata->modules) {
    if (not has_wrappable(mi))
      continue;
    outputs.emplace_back("py_" + module_pyname(mi.name) + ".F90",
                         codegen_module(mi));
  }
  wdata->modules.clear();

  bool const failed =
      context.messages().AnyFatalError(context.warningsAreErrors());
  flu::flush_messages(context, out);
  if (failed)
    return false;

  for (auto const &[outfile, content] : outputs) {
    std::ofstream(outfile) << content;
    std::cout << "Generated " << outfile << std::endl;
  }
  return true;
}
