#include <flang/Semantics/symbol.h>
#include <flang/Semantics/type.h>
#include <llvm/ADT/STLExtras.h>

#include <algorithm>

#include "flu/paths.hpp"
#include "flu/symbols.hpp"
#include "traversal.hpp"

// --- state ---
bool state::ignore(sema::Symbol const &sym) {
  return ignored.find(sym.name().ToString()) != ignored.end() or
         (default_private and not sym.attrs().test(sema::Attr::PUBLIC));
}

std::vector<str_t> state::instantiate_types(sema::Symbol const &sym) const {
  auto const it = instantiate.find(sym.name().ToString());
  if (it == instantiate.end())
    return {};
  std::vector<str_t> types(it->second.begin(), it->second.end());
  std::sort(types.begin(), types.end());
  return types;
}
// -------------

// --- Forward declarations ---
dtype_info_t *get_dtype_of_initializer(sema::Symbol const &sym,
                                       module_info_t const &mi);
void traverse_module(sema::Symbol const &mod_sym, state &s);
// ----------------------------

void traverse_global_scope(const sema::Scope &root,
                           std::shared_ptr<wdata_t> wdata,
                           sema::SemanticsContext &context) {
  std::unordered_set<std::string> ignore = wdata->collector->ignore;
  // NOTE: ignore `!flair$ callback` annotated symbols for now
  ignore.insert(wdata->collector->callbacks.begin(),
                wdata->collector->callbacks.end());

  std::unordered_set<std::string> wrap_set;
  for (auto const &f : wdata->wrap_files)
    wrap_set.insert(flu::normalized_path(f));

  for (auto const &[name, sym_ref] : root) {
    sema::Symbol const &sym = sym_ref.get();
    if (not sym.has<sema::ModuleDetails>() or
        ignore.find(sym.name().ToString()) != ignore.end())
      continue;
    // If the origin of the module is a .mod file, skip it.
    // Avoids transitive traversal of USEd modules.
    if (sym.test(sema::Symbol::Flag::ModFile))
      continue;

    // With --wrap, only modules defined in the designated files are wrapped;
    // the remaining inputs are resolved for their symbols only.
    if (not wrap_set.empty()) {
      auto const path = flu::defining_path(context, sym);
      if (not path or
          wrap_set.find(flu::normalized_path(*path)) == wrap_set.end())
        continue;
    }

    module_info_t mi(name.ToString());
    state s{mi, ignore, wdata->collector->instantiate};
    traverse_module(sym, s);
    wdata->modules.push_back(std::move(mi));
  }
}

void traverse_module(sema::Symbol const &mod_sym, state &s) {
  s.default_private = mod_sym.get<sema::ModuleDetails>().isDefaultPrivate();
  auto mod_scope = mod_sym.get<sema::ModuleDetails>().scope();
  if (mod_scope == nullptr)
    return;

  auto const match_subprogram = [&s](sema::Symbol const &sym) {
    if (s.ignore(sym))
      return;
    if (not sym.has<sema::SubprogramDetails>())
      return;

    fnt_info_t fi{&sym, false, nullptr, s.instantiate_types(sym)};
    if (auto dt = get_dtype_of_initializer(sym, s.mi))
      dt->init = std::move(fi); // We have found the subroutine in charge of
                                // initializing a derived_type
    else
      s.mi.functions.emplace_back(fi);
  };

  auto const match_interface = [&s](sema::Symbol const &sym) {
    if (s.ignore(sym))
      return;
    if (not sym.has<sema::GenericDetails>())
      return;

    iface_info_t iface{&sym};
    if (sym.get<sema::GenericDetails>().derivedType()) {
      if (auto const &it = s.mi.derived_types.find(sym.name().ToString());
          it != s.mi.derived_types.end()) {
        // We have found an interface with the same name as a previously
        // matched derived type => interface-as-constructor pattern match
        it->second.ctor = std::move(iface);
        return;
      }
    }

    s.mi.interfaces.emplace_back(iface);
  };

  auto const match_dtype = [&s](sema::Symbol const &sym) {
    if (s.ignore(sym))
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
        s.mi.derived_types.emplace(type_sym->name().ToString(), type_sym);
    dtype_info_t &dt = it->second;
    if (auto const *parent =
            type_sym->GetParentTypeSpec()) // extends(...) base type
      dt.base = &parent->typeSymbol();

    auto const *dtype_scope = type_sym->scope();
    if (dtype_scope == nullptr)
      return;

    auto const match_proc_binding = [&s, &dt](sema::Symbol const &sym) {
      if (not sym.has<sema::ProcBindingDetails>())
        return;
      // Type-bound procedure accesibility is public by default
      if (sym.attrs().test(sema::Attr::PRIVATE))
        return;

      // The instantiate directive sits on the actual subprogram in the
      // module's contains section, so look it up by the actual's name.
      std::vector<str_t> inst;
      if (auto const *act = flu::binding_actual(sym))
        inst = s.instantiate_types(*act);
      dt.methods.emplace_back(fnt_info_t{&sym, false, dt.ptr, std::move(inst)});
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

// Returns a pointer to the already matched derived type in module_info_t,
// which matches the first argument of the initializer subroutine.
// Returns nullptr if there is no match.
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
