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

std::vector<semantics::Symbol const *> public_components(semantics::Symbol const &type_sym) {
  std::vector<semantics::Symbol const *> out;
  auto const *scope = type_sym.scope();
  auto const *dtd   = type_sym.detailsIf<semantics::DerivedTypeDetails>();
  if (scope == nullptr || dtd == nullptr) return out;
  for (auto const &cn : dtd->componentNames()) {
    auto it = scope->find(cn);
    if (it == scope->end()) continue;
    semantics::Symbol const &comp = it->second.get();
    if (comp.attrs().test(semantics::Attr::PRIVATE)) continue;
    if (!comp.has<semantics::ObjectEntityDetails>()) continue; // skip parent comp / procs
    out.push_back(&comp);
  }
  return out;
}

} // namespace flu
