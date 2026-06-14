#include <flang/Semantics/symbol.h>
#include <flang/Semantics/type.h>
#include <llvm/ADT/STLExtras.h>
#include <llvm/Support/CommandLine.h>

#include "traversal.hpp"

void traverse_module(semantics::Symbol const &mod_sym, module_info_t &mi) {
  bool default_private = mod_sym.get<semantics::ModuleDetails>().isDefaultPrivate();
  auto mod_scope = mod_sym.get<semantics::ModuleDetails>().scope();
  if (mod_scope == nullptr) return;

  // Tracks the derived type currently being traversed, so the shared
  // subprogram / proc-binding lambdas can tell whether they were reached at
  // module scope (-> mi.functions) or inside a type (-> dt.methods).
  dtype_info_t *current_dtype = nullptr;

  // TODO: hide constructor / init methods if matched by match_ctor_or_initializer
  auto const match_subprogram = [&](semantics::Symbol const &sym) {
    if (default_private and not sym.attrs().test(semantics::Attr::PUBLIC)) return;
    if (not sym.has<semantics::SubprogramDetails>()) return;
    fnt_info_t fi{&sym, false, current_dtype ? current_dtype->ptr : nullptr};
    if (current_dtype) current_dtype->methods.emplace_back(fi);
    else               mi.functions.emplace_back(fi);
  };

  auto const match_proc_binding= [&](semantics::Symbol const &sym) {
    if (not sym.has<semantics::ProcBindingDetails>()) return;
    // Type-bound procedure accessibility comes from the type's CONTAINS section,
    // which is public unless marked PRIVATE
    if (sym.attrs().test(semantics::Attr::PRIVATE)) return;
    fnt_info_t fi{&sym, false, current_dtype ? current_dtype->ptr : nullptr};
    if (current_dtype) current_dtype->methods.emplace_back(fi);
    else               mi.functions.emplace_back(fi);
  };

  auto const match_ctor_or_initializer = [&](semantics::Symbol const &sym) {
    if (default_private and not sym.attrs().test(semantics::Attr::PUBLIC)) return;

    if (sym.has<semantics::GenericDetails>()) {
      if (auto const &it = mi.derived_types.find(sym.name().ToString()); 
          it != mi.derived_types.end()) {
        // We have found an interface with the same name as a previously
        // matched derived type => interface-as-constructor pattern match
        it->second.ctor = fnt_info_t{&sym, false, nullptr};
        return;
      }
    }

    if (not sym.has<semantics::SubprogramDetails>()) return;
    auto const &subp = sym.get<semantics::SubprogramDetails>();
    std::vector<semantics::Symbol *> const &args = subp.dummyArgs();

    if (args.empty() || args.front() == nullptr) return;

    semantics::DeclTypeSpec const *type = args.front()->GetType();
    if (type == nullptr) return;

    semantics::DerivedTypeSpec const *dts = type->AsDerived();
    if (dts == nullptr) return;

    std::string const nm = sym.name().ToString();
    bool const is_init = not subp.isFunction() and nm.size() >= 5 and
                         nm.compare(nm.size() - 5, 5, "_init") == 0;
    if (not is_init) return;

    if (auto const &it = mi.derived_types.find(dts->name().ToString());
          it != mi.derived_types.end()) {
      // We have found the subroutine in charge of initializing the derived_type
      it->second.init = fnt_info_t{&sym, false, nullptr};
    }
  };

  auto const match_dtype = [&](semantics::Symbol const &sym) {
    if (default_private and not sym.attrs().test(semantics::Attr::PUBLIC)) return;

    // A derived type may be shadowed in its module scope by a generic interface
    // of the same name. In that case the module-scope symbol carries GenericDetails
    // and the real type is reached via derivedType().
    semantics::Symbol const *type_sym = &sym;
    if (auto const *gd = sym.detailsIf<semantics::GenericDetails>())
      type_sym = gd->derivedType();
    if (type_sym == nullptr or not type_sym->has<semantics::DerivedTypeDetails>())
      return;

    auto const &[it, _] = mi.derived_types.emplace(type_sym->name().ToString(), type_sym);
    dtype_info_t &dt = it->second;
    if (auto const *parent = type_sym->GetParentTypeSpec()) // extends(...) base type
      dt.base = &parent->typeSymbol();

    auto const *dtype_scope = type_sym->scope();
    if (dtype_scope == nullptr) return;

    current_dtype = &dt;
    llvm::for_each(dtype_scope->GetSymbols(), match_proc_binding);
    current_dtype = nullptr;
  };

  // Predicate to filter out compiler-generated entities
  auto const is_not_compiler_generated = [](semantics::Symbol const&s) {
    std::string const n = s.name().ToString();
    return not n.empty() and std::isalpha(static_cast<unsigned char>(n[0])) != 0;
  };

  // Lifetime of module_symbols is not kept by make_filter_range. We therefore
  // a local reference that outlives the call, to avoid lifetime issues.
  auto const module_symbols   = mod_scope->GetSymbols();
  auto const filtered_symbols = llvm::make_filter_range(module_symbols, is_not_compiler_generated);

  llvm::for_each(filtered_symbols, match_dtype);
  llvm::for_each(filtered_symbols, match_ctor_or_initializer);
  llvm::for_each(filtered_symbols, match_subprogram);
  llvm::for_each(filtered_symbols, match_proc_binding);
}

