#include "functions.hpp"

#include <algorithm>

#include <fmt/core.h>
#include <fmt/format.h>

#include <flang/Semantics/attr.h>
#include <flang/Semantics/symbol.h>
#include <flang/Semantics/type.h>

#include "flu/diagnostics.hpp"
#include "flu/symbols.hpp"
#include "flu/types.hpp"
#include "pytypes.hpp"

namespace codegen {

using namespace Fortran;

// ===========================================================================
// Template (compiled in via C23 #embed; works at C++17 with clang).
// ===========================================================================
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc23-extensions"
static constexpr char tpl_module_function[] = {
#embed "templates/module_function.txt"
    , '\0'};
#pragma clang diagnostic pop

// METH_NOARGS calling convention passes (self, NULL) only, so those wrappers
// keep the two-parameter signature.
static constexpr char tpl_noargs_function[] =
    "    function {fn}(self, args) bind(C) result(r)\n"
    "        type(c_ptr), value :: self, args\n"
    "        type(c_ptr) :: r\n"
    "{body}\n"
    "    end function\n";

dtype_class_t classify_dtype(semantics::DeclTypeSpec const &t,
                             module_info_t const &m) {
  auto const *ds = t.AsDerived();
  if (ds == nullptr)
    return {dtype_class::NotDerived, nullptr, {}};
  semantics::Symbol const &tsym = ds->typeSymbol(); // defining symbol
  for (auto const &[name, dt] : m.derived_types)
    if (dt.ptr == &tsym)
      return {dtype_class::Local, dt.ptr, m.name};
  // Parameterized types can't be spelled as bare `type(t)` in the wrapper.
  if (!ds->parameters().empty())
    return {dtype_class::Unsupported, &tsym, {}};
  str_t const owner = flu::owning_module_name(tsym);
  // Intrinsic-module types (c_ptr & friends) have no wrapper anywhere; a type
  // defined in `m` but not wrapped would reference converters that are never
  // generated.
  if (owner.empty() || owner[0] == '_' || flu::in_intrinsic_module(tsym) ||
      owner == m.name)
    return {dtype_class::Unsupported, &tsym, {}};
  return {dtype_class::Foreign, &tsym, owner};
}

// Folded module name -> folded names of the types its wrapper converts,
// for every wrapper this invocation generates.
static std::map<str_t, std::set<str_t>> run_wrapped_types;

void note_run_modules(std::vector<module_info_t> const &modules) {
  run_wrapped_types.clear();
  for (auto const &mi : modules) {
    auto &types = run_wrapped_types[fold_lower(mi.name)];
    for (auto const &[type_name, dt] : mi.derived_types)
      types.insert(fold_lower(type_name));
  }
}

bool note_ext_type(ext_types_t &ext_types, semantics::Symbol const &tsym) {
  str_t const n = tname(tsym);
  if (view_pyobject_fn(n).size() > 63) {
    flu::emit_error(tsym, "flair-f2py: derived type name '" + n +
                              "' is too long for the generated converter names "
                              "(63-char identifier limit); annotate '" +
                              n +
                              "' with a '!flair$ ignore' directive to skip it");
    return false;
  }
  auto const [it, inserted] = ext_types.emplace(n, &tsym);
  if (!inserted) {
    if (it->second != &tsym) {
      // Same folded name, different type: the consumer would use-associate
      // identically named converters from both producer wrappers.
      flu::emit_error(
          tsym, "flair-f2py: derived type '" + n +
                    "' collides with an equally named type from "
                    "module '" +
                    flu::owning_module_name(*it->second) + "'; annotate '" + n +
                    "' with a '!flair$ ignore' directive to skip it");
      return false;
    }
    return true;
  }
  // The producer wrapper is emitted by this very invocation; nothing for the
  // user to generate separately.
  if (auto const run_it =
          run_wrapped_types.find(fold_lower(flu::owning_module_name(tsym)));
      run_it != run_wrapped_types.end()) {
    if (run_it->second.count(n) != 0)
      return true;
    // The owning module's wrapper is emitted by this very invocation but skips
    // the type (e.g. '!flair$ ignore'): its converter will never exist, so a
    // reference would only fail later, when the extension is loaded.
    flu::emit_error(tsym, "flair-f2py: derived type '" + n +
                              "' is skipped by the wrapper of its module '" +
                              flu::owning_module_name(tsym) +
                              "', so no converter for it exists");
    ext_types.erase(n);
    return false;
  }
  flu::emit_warning(tsym, "flair-f2py: derived type '" + n +
                              "' is defined in module '" +
                              flu::owning_module_name(tsym) +
                              "'; its wrapper must be generated separately, "
                              "compiled before this one, and linked with it");
  return true;
}

bool parse_args(std::vector<semantics::Symbol *> const &dummies,
                module_info_t const &m, str_t const &owner_name, str_t &decls,
                str_t &pre, str_t &fetch, str_t &call_args,
                string_pool_t &strings, str_t *cleanup, ext_types_t *ext_types,
                poly_overrides_t const *overrides) {
  str_t const ignore_hint = "; annotate '" + owner_name +
                            "' with a '!flair$ ignore' directive to skip it";
  auto add_actual = [&](str_t const &actual) {
    if (!call_args.empty())
      call_args += ", ";
    call_args += actual;
  };

  // Every failure inside the fetch block leaves `r` at the caller's preset
  // failure value and jumps to the shared cleanup after the block.
  static constexpr char fail_return[] = "exit fetch";

  // The runtime binds positional and keyword arguments in one call; the
  // wrapper carries only the dummy names and which of them are required.
  int const nargs = static_cast<int>(dummies.size());
  if (nargs > 0) {
    decls += fmt::format("        type(c_ptr) :: objs({})\n", nargs);
    decls += fmt::format("        type(c_ptr) :: argnames({})\n", nargs);
    decls += fmt::format("        logical :: argreq({})\n", nargs);
    std::vector<str_t> names, reqs;
    for (semantics::Symbol *d : dummies) {
      if (d == nullptr)
        return false;
      names.push_back(
          fmt::format("c_loc({})", strings.intern(d->name().ToString())));
      reqs.push_back(d->attrs().test(semantics::Attr::OPTIONAL) ? ".false."
                                                                : ".true.");
    }
    fetch += array_assign("        ", "argnames", names);
    fetch += array_assign("        ", "argreq", reqs);
    fetch += "        if (.not. FLAIR_parse_args(args, kwds, argnames, "
             "argreq, objs)) ";
    fetch += str_t(fail_return) + "\n";
  }

  int i = 0; // unique local suffix == positional index
  for (semantics::Symbol *d : dummies) {
    if (d == nullptr)
      return false;
    auto const *t = d->GetType();
    if (t == nullptr)
      return false;
    // The wrapper's locals (value copies and pointers) cannot legally be the
    // actual for an ALLOCATABLE dummy.
    if (flu::is_allocatable(*d)) {
      flu::emit_error(*d, "flair-f2py: cannot wrap argument '" +
                              d->name().ToString() + "': allocatable dummy" +
                              ignore_hint);
      return false;
    }
    bool const opt = d->attrs().test(semantics::Attr::OPTIONAL);
    str_t const nm = d->name().ToString();
    // FLAIR_parse_args has already bound the argument and reported a missing
    // required one; an absent optional (or one given as None) is c_null_ptr.
    str_t const obj = fmt::format("objs({})", i + 1);
    str_t const pres = fmt::format("c_associated({})", obj);

    // An instantiate override replaces the declared (polymorphic) type with a
    // concrete wrapped one, bypassing classification (which would either pick
    // the declared base of class(t) or reject class(*) outright).
    semantics::Symbol const *ov = nullptr;
    if (overrides != nullptr)
      if (auto const it = overrides->find(size_t(i)); it != overrides->end())
        ov = it->second;

    if (auto const c = ov != nullptr
                           ? dtype_class_t{dtype_class::Local, ov, m.name}
                           : classify_dtype(*t, m);
        c.cls != dtype_class::NotDerived) {
      str_t const val = fmt::format("v{}", i);
      str_t bfetch; // wrapped in `if (present)` for an optional dummy
      if (c.cls == dtype_class::Local) {
        // The runtime isinstance-checks and names both the argument and the
        // expected class in the message, from strings the wrapper already
        // carries: the dummy's name and the class name.
        str_t const raw = fmt::format("raw{}", i);
        decls += fmt::format("        type(c_ptr) :: {}\n", raw);
        decls += fmt::format("        type({}), pointer :: {}\n", tname(*c.sym),
                             val);
        bfetch += fmt::format(
            "        {} = FLAIR_unwrap_arg({}, py_{}_type_obj, c_loc({}), "
            "c_loc({}))\n",
            raw, obj, tname(*c.sym), strings.intern(nm),
            strings.intern(clsname(*c.sym)));
        bfetch += fmt::format(
            "        if (.not. c_associated({})) {}\n", raw, fail_return);
        bfetch += fmt::format("        call c_f_pointer({}, {})\n", raw, val);
      } else if (c.cls == dtype_class::Foreign && ext_types != nullptr &&
                 note_ext_type(*ext_types, *c.sym)) {
        // Unwrap via the external converter emitted by the file that wraps the
        // defining module. The converter isinstance-checks and sets the
        // exception itself; a disassociated result signals failure. The
        // returned pointer targets the same heap object the Python wrapper
        // owns, so intent(out)/inout mutations propagate back for free.
        str_t const n = tname(*c.sym);
        decls += fmt::format("        type({}), pointer :: {}\n", n, val);
        bfetch += fmt::format("        {} => {}({})\n", val,
                              from_pyobject_fn(n), obj);
        bfetch += fmt::format("        if (.not. associated({})) {}\n", val,
                              fail_return);
      } else {
        flu::emit_error(*d, "flair-f2py: cannot wrap argument '" +
                                d->name().ToString() + "': derived type '" +
                                flu::derived_name(*t) +
                                "' is not a wrapped type" + ignore_hint);
        return false;
      }
      if (opt) {
        // A disassociated pointer actual makes the optional dummy absent
        // (F2008 15.5.2.13).
        pre += fmt::format("        {} => null()\n", val);
        fetch += fmt::format("        if ({}) then\n", pres) + bfetch +
                 "        end if\n";
      } else {
        fetch += bfetch;
      }
      add_actual(val);
    } else if (flu::rank_of(*d) == 0 && intrinsic_supported(*t)) {
      // A primitive scalar is passed by value; an out/inout write cannot be
      // reflected back into the (immutable) Python object, so reject it.
      if (d->attrs().test(semantics::Attr::INTENT_OUT) ||
          d->attrs().test(semantics::Attr::INTENT_INOUT)) {
        flu::emit_error(
            *d, "flair-f2py: cannot wrap scalar argument '" +
                    d->name().ToString() +
                    "' with intent(out)/intent(inout): primitive scalars are "
                    "passed by value and cannot be written back" +
                    ignore_hint);
        return false;
      }
      str_t const val = fmt::format("x{}", i);
      decls += fmt::format("        {} :: {}\n", py_ctype(*t), val);
      decls += fmt::format("        logical :: ok{}\n", i);
      str_t bfetch; // wrapped in `if (present)` for an optional dummy
      bfetch += fmt::format("        {} = {}({}, ok{})\n", val, py_helper(*t),
                            obj, i);
      bfetch +=
          fmt::format("        if (.not. ok{}) {}\n", i, fail_return);
      auto const cl = flu::char_len(*t);
      if (cl) {
        // Explicit-length character dummy: the actual must meet the declared
        // length (blank-padding shorter strings on assignment); reject longer
        // ones instead of truncating silently.
        bfetch += fmt::format(
            "        if (.not. FLAIR_check_len({}, {}, c_loc({}))) {}\n", val,
            *cl, strings.intern(nm), fail_return);
      }
      if (!opt) {
        fetch += bfetch;
        if (cl) {
          str_t const fix = fmt::format("xf{}", i);
          decls += fmt::format("        character({}) :: {}\n", *cl, fix);
          fetch += fmt::format("        {} = {}\n", fix, val);
          add_actual(fix);
        } else {
          add_actual(narrow(*t, val));
        }
      } else {
        // An optional value goes through a pointer of the dummy's exact type:
        // disassociated means absent (F2008 15.5.2.13), allocated otherwise.
        str_t const optv = fmt::format("xo{}", i);
        bool const is_char =
            flu::category(*t) == Fortran::common::TypeCategory::Character;
        if (cl) {
          decls +=
              fmt::format("        character({}), pointer :: {}\n", *cl, optv);
          bfetch += fmt::format("        allocate({})\n", optv);
          bfetch += fmt::format("        {} = {}\n", optv, val);
        } else if (is_char) {
          decls += fmt::format("        character(:), pointer :: {}\n", optv);
          bfetch += fmt::format(
              "        allocate(character(len=len({})) :: {})\n", val, optv);
          bfetch += fmt::format("        {} = {}\n", optv, val);
        } else {
          decls +=
              fmt::format("        {}, pointer :: {}\n", ftype(*t), optv);
          bfetch += fmt::format("        allocate({})\n", optv);
          bfetch += fmt::format("        {} = {}\n", optv, narrow(*t, val));
        }
        pre += fmt::format("        {} => null()\n", optv);
        fetch += fmt::format("        if ({}) then\n", pres) + bfetch +
                 "        end if\n";
        if (cleanup != nullptr)
          *cleanup += fmt::format(
              "        if (associated({0})) deallocate({0})\n", optv);
        add_actual(optv);
      }
    } else if (int const rr = flu::rank_of(*d); rr > 0 && array_supported(*t)) {
      // Intrinsic array: coerce to an F-contiguous numpy array of the exact
      // dtype and point a Fortran array at its data. For intent(out)/inout the
      // WRITEBACKIFCOPY flag arranges any coercion copy to be flushed back into
      // the caller's array via PyArray_ResolveWritebackIfCopy in cleanup (a
      // no-op when no copy was made, i.e. the zero-copy fast path).
      bool const writeback = d->attrs().test(semantics::Attr::INTENT_OUT) ||
                             d->attrs().test(semantics::Attr::INTENT_INOUT);
      str_t const arr = fmt::format("arr{}", i);
      str_t const val = fmt::format("v{}", i);
      str_t const shp = fmt::format("shp{}", i);
      str_t colons = ":";
      for (int k = 1; k < rr; ++k)
        colons += ",:";
      decls += fmt::format("        type(c_ptr) :: {}\n", arr);
      decls += fmt::format("        {}, pointer :: {}({})\n", ftype(*t), val,
                           colons);
      decls += fmt::format("        integer(c_ptrdiff_t) :: {}({})\n", shp, rr);
      str_t bfetch; // wrapped in `if (present)` for an optional dummy
      bfetch += fmt::format(
          "        {} = FLAIR_array_from_PyObject({}, {}, {}_c_int, {}, {})\n",
          arr, obj, npy(*t), rr, writeback ? ".true." : ".false.", shp);
      bfetch += fmt::format("        if (.not. c_associated({})) {}\n", arr,
                            fail_return);
      bfetch +=
          fmt::format("        call c_f_pointer(PyArray_DATA({}), {}, {})\n",
                      arr, val, shp);
      // Preset so the shared cleanup after the block is safe to run whether or
      // not this argument was ever acquired.
      pre += fmt::format("        {} = c_null_ptr\n", arr);
      if (opt) {
        // A disassociated pointer actual makes the optional dummy absent
        // (F2008 15.5.2.13).
        pre += fmt::format("        {} => null()\n", val);
        fetch += fmt::format("        if ({}) then\n", pres) + bfetch +
                 "        end if\n";
      } else {
        fetch += bfetch;
      }
      if (cleanup != nullptr)
        *cleanup += fmt::format("        call FLAIR_array_release({})\n", arr);
      add_actual(val);
    } else {
      flu::emit_error(*d, "flair-f2py: cannot wrap argument '" +
                              d->name().ToString() +
                              "': unsupported type or rank" + ignore_hint);
      return false;
    }
    ++i;
  }

  return true;
}

// Shift every non-empty line right by one level.
static str_t indent_lines(str_t const &s) {
  str_t out;
  size_t pos = 0;
  while (pos < s.size()) {
    size_t const eol = s.find('\n', pos);
    size_t const end = eol == str_t::npos ? s.size() : eol;
    if (end > pos)
      out += "    ";
    out.append(s, pos, end - pos);
    out += "\n";
    if (eol == str_t::npos)
      break;
    pos = eol + 1;
  }
  return out;
}

str_t wrap_body(str_t const &decls, str_t const &pre, str_t const &fetch,
                str_t const &result, str_t const &cleanup,
                str_t const &fail_value) {
  // Nothing can fail before the call, so no unwinding is needed.
  if (fetch.empty() && cleanup.empty())
    return decls + pre + result;
  // Argument binding and the call run inside a named block: each check
  // `exit fetch`es, leaving r at the preset failure value, and the single
  // cleanup after the block then runs on the success and failure paths alike.
  // That is what makes an argument acquired before a later one failed still
  // get released.
  str_t body = decls + pre;
  body += fmt::format("        r = {}\n", fail_value);
  body += "        fetch: block\n";
  body += indent_lines(fetch + result);
  body += "        end block fetch\n";
  body += cleanup;
  return body;
}

str_t build_result(semantics::DeclTypeSpec const *rt, str_t const &call_expr) {
  if (rt != nullptr)
    return fmt::format("        r = {}\n", to_py(*rt, call_expr));
  str_t s;
  s += fmt::format("        call {}\n", call_expr);
  s += "        r = Py_GetConstant(Py_CONSTANT_NONE)\n";
  return s;
}

std::vector<semantics::Symbol *>
drop_self(std::vector<semantics::Symbol *> const &dummies) {
  std::vector<semantics::Symbol *> args;
  bool skip = true;
  for (semantics::Symbol *d : dummies) {
    if (d == nullptr)
      continue;
    if (skip) {
      skip = false;
      continue;
    }
    args.push_back(d);
  }
  return args;
}

str_t gen_module_function(semantics::Symbol const &fn, module_info_t const &m,
                          string_pool_t &strings, str_t *fills, int &n,
                          ext_types_t &ext_types, str_t const &call_name,
                          str_t const &wrapper_name,
                          poly_overrides_t const *overrides) {
  if (!fn.has<semantics::SubprogramDetails>())
    return "";
  auto const &sub = fn.get<semantics::SubprogramDetails>();

  str_t const pyname = fn.name().ToString();
  str_t const wrapper =
      wrapper_name.empty() ? fmt::format("py_mod_{}", pyname) : wrapper_name;
  str_t const callee = call_name.empty() ? pyname : call_name;

  str_t decls, pre, fetch, call_args, cleanup;
  if (!parse_args(sub.dummyArgs(), m, pyname, decls, pre, fetch, call_args,
                  strings, &cleanup, &ext_types, overrides))
    return "";

  semantics::DeclTypeSpec const *rt =
      sub.isFunction() ? sub.result().GetType() : nullptr;
  // Only scalar results have a Python conversion; array-valued functions
  // would silently pass a rank-1 expression to the scalar converter.
  if (sub.isFunction() && (rt == nullptr || !intrinsic_supported(*rt) ||
                           sub.result().Rank() != 0)) {
    flu::emit_error(fn, "flair-f2py: cannot wrap function '" + pyname +
                            "': unsupported result type; annotate '" + pyname +
                            "' with a '!flair$ ignore' directive to skip it");
    return "";
  }

  if (fills != nullptr)
    *fills += method_row("module_methods", ++n, strings.intern(pyname), wrapper,
                         sub.dummyArgs().empty()
                             ? "METH_NOARGS"
                             : "METH_VARARGS + METH_KEYWORDS");

  str_t const body = wrap_body(
      decls, pre, fetch,
      build_result(rt, fmt::format("{}({})", callee, call_args)), cleanup,
      "c_null_ptr");
  // METH_NOARGS wrappers are called with (self, NULL): no kwds parameter.
  if (sub.dummyArgs().empty())
    return render(tpl_noargs_function, {{"fn", wrapper}, {"body", body}}) +
           "\n";
  return render(tpl_module_function, {{"fn", wrapper}, {"body", body}}) + "\n";
}

} // namespace codegen
