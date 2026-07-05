#include "flu/symbols.hpp"

#include <flang/Semantics/attr.h>
#include <flang/Semantics/scope.h>
#include <flang/Semantics/symbol.h>
#include <flang/Semantics/type.h>

namespace flu {

using namespace Fortran;

semantics::Symbol const *binding_actual(semantics::Symbol const &binding) {
  if (auto const *pb = binding.detailsIf<semantics::ProcBindingDetails>())
    return &pb->symbol();
  return nullptr;
}

int rank_of(semantics::Symbol const &sym) {
  if (auto const *obj = sym.detailsIf<semantics::ObjectEntityDetails>())
    return obj->IsArray() ? obj->shape().Rank() : 0;
  return 0;
}

bool is_pointer(semantics::Symbol const &sym) {
  return sym.attrs().test(semantics::Attr::POINTER);
}

bool is_allocatable(semantics::Symbol const &sym) {
  return sym.attrs().test(semantics::Attr::ALLOCATABLE);
}

sema::SymbolVector public_components(sema::Symbol const &type_sym) {
  sema::SymbolVector out;
  auto const *scope = type_sym.scope();
  auto const *dtd = type_sym.detailsIf<sema::DerivedTypeDetails>();
  if (scope == nullptr || dtd == nullptr)
    return out;
  for (auto const &cn : dtd->componentNames()) {
    auto it = scope->find(cn);
    if (it == scope->end())
      continue;
    sema::Symbol const &comp = it->second.get();
    if (comp.attrs().test(sema::Attr::PRIVATE))
      continue;
    if (not comp.has<sema::ObjectEntityDetails>())
      continue; // skip parent comp / procs
    out.push_back(comp);
  }
  return out;
}

// We skip checking for the private attribute, as we need to generate a wrapper
// for each specific procedure, which can be called within the interface
// wrapper. The individual procedure wrappers are not exposed to the python
// module.
sema::SymbolVector get_specific_procs(const sema::Symbol &iface_sym) {
  auto const *gtd = iface_sym.detailsIf<sema::GenericDetails>();
  if (gtd == nullptr)
    return sema::SymbolVector{};
  return gtd->specificProcs();
}

} // namespace flu
