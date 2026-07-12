#include "functions.hpp"

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

bool note_ext_type(ext_types_t &ext_types, semantics::Symbol const &tsym) {
  str_t const n = tname(tsym);
  if (view_pyobject_fn(n).size() > 63) {
    flu::emit_error(tsym, "flair-f2py: derived type name '" + n +
                              "' is too long for the external converter names "
                              "(63-char identifier limit); annotate '" +
                              n +
                              "' with a '!flair$ ignore' directive to skip it");
    return false;
  }
  auto const [it, inserted] = ext_types.emplace(n, &tsym);
  if (!inserted) {
    if (it->second != &tsym) {
      // Same folded name, different type: the name-keyed FLAIR_* linker
      // symbols of the two producers would collide.
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
  flu::emit_warning(tsym, "flair-f2py: derived type '" + n +
                              "' is defined in module '" +
                              flu::owning_module_name(tsym) +
                              "'; its wrapper must be generated separately and "
                              "linked with this one");
  return true;
}

bool parse_args(std::vector<semantics::Symbol *> const &dummies,
                module_info_t const &m, str_t const &owner_name,
                str_t const &fail_return, str_t &decls, str_t &fetch,
                str_t &call_args, string_pool_t &strings, str_t *cleanup,
                ext_types_t *ext_types, poly_overrides_t const *overrides) {
  str_t const ignore_hint = "; annotate '" + owner_name +
                            "' with a '!flair$ ignore' directive to skip it";
  auto add_actual = [&](str_t const &actual) {
    if (!call_args.empty())
      call_args += ", ";
    call_args += actual;
  };

  int i = 0; // unique local suffix == positional index
  for (semantics::Symbol *d : dummies) {
    if (d == nullptr)
      return false;
    auto const *t = d->GetType();
    if (t == nullptr)
      return false;
    str_t const obj = fmt::format("a{}", i);

    decls += fmt::format("        type(c_ptr) :: {}\n", obj);
    fetch += fmt::format("        {} = PyTuple_GetItem(args, {}_c_ptrdiff_t)\n",
                         obj, i);
    fetch += fmt::format("        if (.not. c_associated({})) then\n           "
                         " {}\n            return\n        end if\n",
                         obj, fail_return);

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
      if (c.cls == dtype_class::Local) {
        str_t const s_argtype =
            strings.intern("argument '" + d->name().ToString() +
                           "' must be a " + clsname(*c.sym) + " instance");
        decls += fmt::format("        type({}), pointer :: pt{}\n",
                             struct_name(*c.sym), i);
        decls += fmt::format("        type({}), pointer :: {}\n", tname(*c.sym),
                             val);
        fetch += fmt::format("        if (PyObject_IsInstance({}, "
                             "py_{}_type_obj) /= 1) then\n",
                             obj, tname(*c.sym));
        // IsInstance may return -1 with its own exception set; don't clobber
        // it.
        fetch += "            if (.not. c_associated(PyErr_Occurred())) then\n";
        fetch += fmt::format("                call "
                             "PyErr_SetString(PyExc_TypeError, c_loc({}))\n",
                             s_argtype);
        fetch += "            end if\n";
        fetch += fmt::format("            {}\n            return\n        end "
                             "if\n",
                             fail_return);
        fetch += fmt::format("        call c_f_pointer({}, pt{})\n", obj, i);
        fetch += fmt::format("        call c_f_pointer(pt{}%{}, {})\n", i,
                             ptr_field(*c.sym), val);
      } else if (c.cls == dtype_class::Foreign && ext_types != nullptr &&
                 note_ext_type(*ext_types, *c.sym)) {
        // Unwrap via the external converter emitted by the file that wraps the
        // defining module. The converter isinstance-checks and sets the
        // exception itself; a disassociated result signals failure. The
        // returned pointer targets the same heap object the Python wrapper
        // owns, so intent(out)/inout mutations propagate back for free.
        str_t const n = tname(*c.sym);
        decls += fmt::format("        type({}), pointer :: {}\n", n, val);
        fetch += fmt::format("        {} => {}({})\n", val, from_pyobject_fn(n),
                             obj);
        fetch += fmt::format("        if (.not. associated({})) then\n         "
                             "   {}\n            return\n        end if\n",
                             val, fail_return);
      } else {
        flu::emit_error(*d, "flair-f2py: cannot wrap argument '" +
                                d->name().ToString() + "': derived type '" +
                                flu::derived_name(*t) +
                                "' is not a wrapped type" + ignore_hint);
        return false;
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
      fetch += fmt::format("        {} = {}({}, ok{})\n", val, py_helper(*t),
                           obj, i);
      fetch += fmt::format("        if (.not. ok{}) then\n            {}\n     "
                           "       return\n        end if\n",
                           i, fail_return);
      if (auto const cl = flu::char_len(*t)) {
        // Explicit-length character dummy: the actual must meet the declared
        // length, so copy into a fixed local (blank-padding shorter strings)
        // and reject longer ones instead of truncating silently.
        str_t const fix = fmt::format("xf{}", i);
        str_t const s_len = strings.intern(
            fmt::format("argument '{}' exceeds character length {}",
                        d->name().ToString(), *cl));
        decls += fmt::format("        character({}) :: {}\n", *cl, fix);
        fetch += fmt::format("        if (len({}) > {}) then\n", val, *cl);
        fetch += fmt::format("            call PyErr_SetString(PyExc_"
                             "ValueError, c_loc({}))\n",
                             s_len);
        fetch += fmt::format("            {}\n            return\n        end "
                             "if\n",
                             fail_return);
        fetch += fmt::format("        {} = {}\n", fix, val);
        add_actual(fix);
      } else {
        add_actual(narrow(*t, val));
      }
    } else if (int const rr = flu::rank_of(*d); rr > 0 && array_supported(*t)) {
      // Intrinsic array: coerce to an F-contiguous numpy array of the exact
      // dtype and point a Fortran array at its data. For intent(out)/inout the
      // WRITEBACKIFCOPY flag arranges any coercion copy to be flushed back into
      // the caller's array via PyArray_ResolveWritebackIfCopy in cleanup (a
      // no-op when no copy was made, i.e. the zero-copy fast path).
      bool const writeback = d->attrs().test(semantics::Attr::INTENT_OUT) ||
                             d->attrs().test(semantics::Attr::INTENT_INOUT);
      str_t const reqs =
          writeback ? "NPY_ARRAY_F_CONTIGUOUS + NPY_ARRAY_WRITEBACKIFCOPY"
                    : "NPY_ARRAY_F_CONTIGUOUS";
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
      if (writeback)
        decls += fmt::format("        integer(c_int) :: wb{}\n", i);
      fetch += fmt::format(
          "        {} = PyArray_FromAny({}, PyArray_DescrFromType({}), "
          "{}_c_int, {}_c_int, {}, c_null_ptr)\n",
          arr, obj, npy(*t), rr, rr, reqs);
      fetch += fmt::format("        if (.not. c_associated({})) then\n         "
                           "   {}\n            return\n        end if\n",
                           arr, fail_return);
      for (int k = 0; k < rr; ++k)
        fetch += fmt::format("        {}({}) = PyArray_DIM({}, {}_c_int)\n",
                             shp, k + 1, arr, k);
      fetch +=
          fmt::format("        call c_f_pointer(PyArray_DATA({}), {}, {})\n",
                      arr, val, shp);
      if (cleanup != nullptr) {
        // Resolve before decref: an unresolved writeback array warns and drops
        // the write on decref.
        if (writeback)
          *cleanup += fmt::format(
              "        wb{0} = PyArray_ResolveWritebackIfCopy({1})\n", i, arr);
        *cleanup += fmt::format("        call Py_DecRef({})\n", arr);
      }
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

  str_t decls, fetch, call_args, cleanup;
  if (!parse_args(sub.dummyArgs(), m, pyname, "r = c_null_ptr", decls, fetch,
                  call_args, strings, &cleanup, &ext_types, overrides))
    return "";

  semantics::DeclTypeSpec const *rt =
      sub.isFunction() ? sub.result().GetType() : nullptr;
  if (sub.isFunction() && (rt == nullptr || !intrinsic_supported(*rt))) {
    flu::emit_error(fn, "flair-f2py: cannot wrap function '" + pyname +
                            "': unsupported result type; annotate '" + pyname +
                            "' with a '!flair$ ignore' directive to skip it");
    return "";
  }

  if (fills != nullptr)
    *fills +=
        method_row("module_methods", ++n, strings.intern(pyname), wrapper,
                   sub.dummyArgs().empty() ? "METH_NOARGS" : "METH_VARARGS");

  str_t body = decls + fetch;
  body += build_result(rt, fmt::format("{}({})", callee, call_args));
  body += cleanup;
  return render(tpl_module_function, {{"fn", wrapper}, {"body", body}}) + "\n";
}

} // namespace codegen
