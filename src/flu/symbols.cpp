#include "flu/symbols.hpp"

#include <algorithm>

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

bool extends_or_is(sema::Symbol const &derived, sema::Symbol const &base) {
  for (sema::Symbol const *t = &derived; t != nullptr;) {
    if (&t->GetUltimate() == &base.GetUltimate())
      return true;
    auto const *parent = t->GetParentTypeSpec();
    t = parent ? &parent->typeSymbol() : nullptr;
  }
  return false;
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

std::vector<sema::Symbol const *> ancestors(sema::Symbol const &type_sym) {
  std::vector<sema::Symbol const *> out;
  for (sema::Symbol const *t = &type_sym;;) {
    auto const *parent = t->GetParentTypeSpec();
    if (parent == nullptr)
      break;
    t = &parent->typeSymbol();
    out.push_back(t);
  }
  // Collected child-first; the callers want base-first so that inherited
  // entries precede the ones that may override them.
  std::reverse(out.begin(), out.end());
  return out;
}

sema::SymbolVector all_public_components(sema::Symbol const &type_sym) {
  sema::SymbolVector out;
  for (sema::Symbol const *a : ancestors(type_sym))
    for (auto const &c : public_components(*a))
      out.push_back(c);
  for (auto const &c : public_components(type_sym))
    out.push_back(c);
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

std::string owning_module_name(sema::Symbol const &sym) {
  for (semantics::Scope const *s = &sym.owner(); s != nullptr && !s->IsGlobal();
       s = &s->parent()) {
    if (s->IsModule()) {
      if (auto const *msym = s->symbol())
        return msym->name().ToString();
      return "";
    }
  }
  return "";
}

bool in_intrinsic_module(sema::Symbol const &sym) {
  for (semantics::Scope const *s = &sym.owner(); s != nullptr && !s->IsGlobal();
       s = &s->parent())
    if (s->IsModule())
      return s->parent().IsIntrinsicModules();
  return false;
}

} // namespace flu
