#include "functions.hpp"

#include <fmt/core.h>
#include <fmt/format.h>

#include <flang/Semantics/attr.h>
#include <flang/Semantics/symbol.h>
#include <flang/Semantics/type.h>

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

// Look up a wrapped derived type by source name; nullptr if not wrapped.
static sym_ptr_t wrapped_type(module_info_t const &m, str_t const &name) {
  auto it = m.derived_types.find(name);
  return it != m.derived_types.end() ? it->second.ptr : nullptr;
}

bool parse_args(std::vector<semantics::Symbol *> const &dummies,
                module_info_t const &m, str_t const &fail_return, str_t &decls,
                str_t &fetch, str_t &call_args, str_t *cleanup) {
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

    if (str_t const dn = flu::derived_name(*t); !dn.empty()) {
      sym_ptr_t wt = wrapped_type(m, dn);
      if (wt == nullptr)
        return false; // a type we don't wrap
      str_t const val = fmt::format("v{}", i);
      decls += fmt::format("        type({}), pointer :: pt{}\n",
                           struct_name(*wt), i);
      decls +=
          fmt::format("        type({}), pointer :: {}\n", tname(*wt), val);
      fetch += fmt::format("        call c_f_pointer({}, pt{})\n", obj, i);
      fetch += fmt::format("        call c_f_pointer(pt{}%{}, {})\n", i,
                           ptr_field(*wt), val);
      add_actual(val);
    } else if (flu::rank_of(*d) == 0 && intrinsic_supported(*t)) {
      add_actual(from_py(*t, obj));
    } else if (int const rr = flu::rank_of(*d);
               rr > 0 && intrinsic_supported(*t)) {
      // intent(in) intrinsic array: coerce to an F-contiguous numpy array of
      // the exact dtype and point a Fortran array at its data. out/inout would
      // need write-back, which is not handled yet.
      if (d->attrs().test(semantics::Attr::INTENT_OUT) ||
          d->attrs().test(semantics::Attr::INTENT_INOUT))
        return false;
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
      fetch += fmt::format(
          "        {} = PyArray_FromAny({}, PyArray_DescrFromType({}), "
          "{}_c_int, {}_c_int, NPY_ARRAY_F_CONTIGUOUS, c_null_ptr)\n",
          arr, obj, npy(*t), rr, rr);
      fetch += fmt::format("        if (.not. c_associated({})) then\n         "
                           "   {}\n            return\n        end if\n",
                           arr, fail_return);
      for (int k = 0; k < rr; ++k)
        fetch += fmt::format("        {}({}) = PyArray_DIM({}, {}_c_int)\n", shp,
                             k + 1, arr, k);
      fetch += fmt::format("        call c_f_pointer(PyArray_DATA({}), {}, {})\n",
                           arr, val, shp);
      if (cleanup != nullptr)
        *cleanup += fmt::format("        call Py_DecRef({})\n", arr);
      add_actual(val);
    } else {
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
                          str_t const &call_name) {
  if (!fn.has<semantics::SubprogramDetails>())
    return "";
  auto const &sub = fn.get<semantics::SubprogramDetails>();

  str_t const pyname = fn.name().ToString();
  str_t const wrapper = fmt::format("py_mod_{}", pyname);
  str_t const callee = call_name.empty() ? pyname : call_name;

  str_t decls, fetch, call_args, cleanup;
  if (!parse_args(sub.dummyArgs(), m, "r = c_null_ptr", decls, fetch, call_args,
                  &cleanup))
    return "";

  semantics::DeclTypeSpec const *rt =
      sub.isFunction() ? sub.result().GetType() : nullptr;
  if (sub.isFunction() && (rt == nullptr || !intrinsic_supported(*rt)))
    return "";

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
