#include "flu/types.hpp"

#include <flang/Evaluate/fold.h>
#include <flang/Evaluate/type.h>
#include <flang/Semantics/type.h>

namespace flu {

using namespace Fortran;

std::optional<common::TypeCategory> category(semantics::DeclTypeSpec const &t) {
  if (t.AsDerived())
    return std::nullopt;
  auto dt = evaluate::DynamicType::From(t);
  if (!dt || dt->IsUnlimitedPolymorphic())
    return std::nullopt;
  return dt->category();
}

int kind_of(semantics::DeclTypeSpec const &t) {
  // Only intrinsic categories carry a meaningful kind (DynamicType::kind()
  // asserts otherwise), so gate on `category` first.
  if (!category(t))
    return 0;
  return evaluate::DynamicType::From(t)->kind();
}

std::optional<std::int64_t> char_len(semantics::DeclTypeSpec const &t) {
  if (category(t) != common::TypeCategory::Character)
    return std::nullopt;
  semantics::ParamValue const &len = t.characterTypeSpec().length();
  if (len.isAssumed() || len.isDeferred() || !len.GetExplicit())
    return std::nullopt;
  return evaluate::ToInt64(*len.GetExplicit());
}

std::string derived_name(semantics::DeclTypeSpec const &t) {
  if (auto const *d = t.AsDerived())
    return d->name().ToString();
  return "";
}

bool is_polymorphic(semantics::DeclTypeSpec const &t) {
  return t.IsPolymorphic();
}

bool is_unlimited_polymorphic(semantics::DeclTypeSpec const &t) {
  return t.IsUnlimitedPolymorphic();
}

semantics::Symbol const *poly_base(semantics::DeclTypeSpec const &t) {
  if (t.category() != semantics::DeclTypeSpec::ClassDerived)
    return nullptr;
  return &t.derivedTypeSpec().typeSymbol();
}

} // namespace flu
