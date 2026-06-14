#include "flu/types.hpp"

#include <flang/Evaluate/type.h>
#include <flang/Semantics/type.h>

namespace flu {

using namespace Fortran;

std::optional<common::TypeCategory> category(semantics::DeclTypeSpec const &t) {
  if (t.AsDerived()) return std::nullopt;
  auto dt = evaluate::DynamicType::From(t);
  if (!dt || dt->IsUnlimitedPolymorphic()) return std::nullopt;
  return dt->category();
}

int kind_of(semantics::DeclTypeSpec const &t) {
  // Only intrinsic categories carry a meaningful kind (DynamicType::kind()
  // asserts otherwise), so gate on `category` first.
  if (!category(t)) return 0;
  return evaluate::DynamicType::From(t)->kind();
}

std::string derived_name(semantics::DeclTypeSpec const &t) {
  if (auto const *d = t.AsDerived()) return d->name().ToString();
  return "";
}

} // namespace flu
