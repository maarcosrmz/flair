#include <flang/Semantics/symbol.h>
#include <flang/Semantics/type.h>
#include <llvm/ADT/STLExtras.h>
#include <llvm/Support/CommandLine.h>

#include "traversal.hpp"

namespace flair::semantics {

// Returns a pointer to the derived type in module_info_t, which matches the
// first argument of the initializer subroutine. Returns nullptr if there is no
// match.
dtype_info_t *get_dtype_of_initializer(sema::Symbol const &sym,
                                       module_info_t const &mi) {
  auto const &subp = sym.get<sema::SubprogramDetails>();
  std::vector<sema::Symbol *> const &args = subp.dummyArgs();

  if (args.empty() || args.front() == nullptr)
    return nullptr;

  sema::DeclTypeSpec const *type = args.front()->GetType();
  if (type == nullptr)
    return nullptr;

  sema::DerivedTypeSpec const *dts = type->AsDerived();
  if (dts == nullptr)
    return nullptr;

  std::string const nm = sym.name().ToString();
  bool const is_init = not subp.isFunction() and nm.size() >= 5 and
                       nm.compare(nm.size() - 5, 5, "_init") == 0;
  if (not is_init)
    return nullptr;

  if (const auto &it = mi.derived_types.find(dts->name().ToString());
      it != mi.derived_types.end())
    return const_cast<dtype_info_t *>(&it->second);

  return nullptr;
}

void traverse_module(sema::Symbol const &mod_sym, module_info_t &mi) {
  bool default_private = mod_sym.get<sema::ModuleDetails>().isDefaultPrivate();
  auto mod_scope = mod_sym.get<sema::ModuleDetails>().scope();
  if (mod_scope == nullptr)
    return;

  auto const match_subprogram = [&default_private,
                                 &mi](sema::Symbol const &sym) {
    if (default_private and not sym.attrs().test(sema::Attr::PUBLIC))
      return;
    if (not sym.has<sema::SubprogramDetails>())
      return;

    fnt_info_t fi{&sym, false, nullptr};
    if (auto dt = get_dtype_of_initializer(sym, mi))
      dt->init = std::move(fi); // We have found the subroutine in charge of
                                // initializing a derived_type
    else
      mi.functions.emplace_back(fi);
  };

  auto const match_interface = [&default_private,
                                &mi](sema::Symbol const &sym) {
    if (default_private and not sym.attrs().test(sema::Attr::PUBLIC))
      return;
    if (not sym.has<sema::GenericDetails>())
      return;

    if (auto const &it = mi.derived_types.find(sym.name().ToString());
        it != mi.derived_types.end()) {
      // We have found an interface with the same name as a previously
      // matched derived type => interface-as-constructor pattern match
      it->second.ctor = fnt_info_t{&sym, false, nullptr};
    }

    // TODO: match other (procedure) interfaces
  };

  auto const match_dtype = [&default_private, &mi](sema::Symbol const &sym) {
    if (default_private and not sym.attrs().test(sema::Attr::PUBLIC))
      return;

    // A derived type may be shadowed in its module scope by a generic interface
    // of the same name. In that case the module-scope symbol carries
    // GenericDetails and the real type is reached via derivedType().
    sema::Symbol const *type_sym = &sym;
    if (auto const *gd = sym.detailsIf<sema::GenericDetails>())
      type_sym = gd->derivedType();
    if (type_sym == nullptr or not type_sym->has<sema::DerivedTypeDetails>())
      return;

    auto const &[it, _] =
        mi.derived_types.emplace(type_sym->name().ToString(), type_sym);
    dtype_info_t &dt = it->second;
    if (auto const *parent =
            type_sym->GetParentTypeSpec()) // extends(...) base type
      dt.base = &parent->typeSymbol();

    auto const *dtype_scope = type_sym->scope();
    if (dtype_scope == nullptr)
      return;

    auto const match_proc_binding = [&dt](sema::Symbol const &sym) {
      if (not sym.has<sema::ProcBindingDetails>())
        return;
      // Type-bound procedure accesibility is public by default
      if (sym.attrs().test(sema::Attr::PRIVATE))
        return;

      dt.methods.emplace_back(fnt_info_t{&sym, false, dt.ptr});
    };

    llvm::for_each(dtype_scope->GetSymbols(), match_proc_binding);
  };

  // Predicate to filter out compiler-generated entities
  auto const is_not_compiler_generated = [](sema::Symbol const &s) {
    std::string const n = s.name().ToString();
    return not n.empty() and
           std::isalpha(static_cast<unsigned char>(n[0])) != 0;
  };

  // Lifetime of module_symbols is not kept by make_filter_range. We therefore
  // keep a local reference that outlives the call, to avoid lifetime issues.
  auto const module_symbols = mod_scope->GetSymbols();
  auto const filtered_symbols =
      llvm::make_filter_range(module_symbols, is_not_compiler_generated);

  llvm::for_each(filtered_symbols, match_dtype);
  llvm::for_each(filtered_symbols, match_interface);
  llvm::for_each(filtered_symbols, match_subprogram);
}

} // namespace flair::semantics
