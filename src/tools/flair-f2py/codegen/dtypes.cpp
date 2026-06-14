#include "dtypes.hpp"

#include <fmt/core.h>
#include <fmt/format.h>

#include <flang/Semantics/symbol.h>
#include <flang/Semantics/type.h>

#include "flu/symbols.hpp"
#include "flu/types.hpp"
#include "functions.hpp" // parse_args, build_result, drop_self (shared with methods)
#include "pytypes.hpp"

namespace codegen {

using namespace Fortran;

// ===========================================================================
// Templates (compiled in via C23 #embed; works at C++17 with clang).
// ===========================================================================
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc23-extensions"
static constexpr char tpl_struct[] = {
#embed "templates/object_struct.txt"
  , '\0'};
static constexpr char tpl_lifecycle[] = {
#embed "templates/lifecycle.txt"
  , '\0'};
static constexpr char tpl_getset_scalar[] = {
#embed "templates/getset_scalar.txt"
  , '\0'};
static constexpr char tpl_getset_numpy[] = {
#embed "templates/getset_numpy.txt"
  , '\0'};
static constexpr char tpl_method[] = {
#embed "templates/method.txt"
  , '\0'};
#pragma clang diagnostic pop

std::vector<sym_ptr_t> public_fields(semantics::Symbol const &type_sym) {
  std::vector<sym_ptr_t> out;
  for (sym_ptr_t c : flu::public_components(type_sym)) {
    auto const *t = c->GetType();
    if (t == nullptr || !intrinsic_supported(*t)) continue; // intrinsic real/integer only (step 1)
    int const rank = flu::rank_of(*c);
    if (rank != 0 && rank != 1) continue;                   // scalar or rank-1 array only
    out.push_back(c);
  }
  return out;
}

str_t gen_object_struct(semantics::Symbol const &tsym) {
  return render(tpl_struct, {
    {"struct", struct_name(tsym)}, {"ptr_field", ptr_field(tsym)}, {"tname", tname(tsym)}});
}

static str_t default_new_body(str_t const &pf) {
  str_t b;
  b += "        r = PyType_GenericAlloc(type_ptr, 0_c_ptrdiff_t)\n";
  b += "        if (.not. c_associated(r)) return\n";
  b += "        call c_f_pointer(r, pt)\n";
  b += "        allocate(p)   ! component default-initializers apply here\n";
  b += fmt::format("        pt%{} = c_loc(p)\n", pf);
  return b;
}

// Parse public scalar fields from args (positional) and kwds (by name).
static str_t default_init_body(semantics::Symbol const &tsym,
                               std::vector<sym_ptr_t> const &fields, string_pool_t &strings) {
  std::vector<sym_ptr_t> scalars;
  for (sym_ptr_t f : fields)
    if (flu::rank_of(*f) == 0) scalars.push_back(f);
  if (scalars.empty()) return "";

  str_t b;
  b += fmt::format("        type({}), pointer :: pt\n", struct_name(tsym));
  b += fmt::format("        type({}), pointer :: p\n", tname(tsym));
  b += "        type(c_ptr) :: arg\n";
  b += "        integer(c_ptrdiff_t) :: n\n\n";
  b += "        call c_f_pointer(self, pt)\n";
  b += fmt::format("        call c_f_pointer(pt%{}, p)\n\n", ptr_field(tsym));

  b += "        if (c_associated(args)) then\n";
  b += "            n = PyTuple_Size(args)\n";
  for (size_t i = 0; i < scalars.size(); ++i) {
    str_t const nm = scalars[i]->name().ToString();
    b += fmt::format("            if (n >= {}) then\n", i + 1);
    b += fmt::format("                arg = PyTuple_GetItem(args, {}_c_ptrdiff_t)\n", i);
    b += fmt::format("                if (c_associated(arg)) p%{} = {}\n", nm,
                     from_py(*scalars[i]->GetType(), "arg"));
    b += "            end if\n";
  }
  b += "        end if\n";

  b += "        if (c_associated(kwds)) then\n";
  for (sym_ptr_t f : scalars) {
    str_t const nm = f->name().ToString();
    b += fmt::format("            arg = PyDict_GetItemString(kwds, c_loc({}))\n", strings.intern(nm));
    b += fmt::format("            if (c_associated(arg)) p%{} = {}\n", nm, from_py(*f->GetType(), "arg"));
  }
  b += "        end if\n";
  return b;
}

str_t gen_lifecycle(semantics::Symbol const &tsym, std::vector<sym_ptr_t> const &fields,
                    string_pool_t &strings) {
  str_t const tn = tname(tsym);
  return render(tpl_lifecycle, {
    {"tname", tn}, {"struct", struct_name(tsym)}, {"ptr_field", ptr_field(tsym)},
    {"new_fn", "py_" + tn + "_new"},
    {"init_fn", "py_" + tn + "_init"},
    {"dealloc_fn", "py_" + tn + "_dealloc"},
    {"new_body", default_new_body(ptr_field(tsym))},
    {"init_body", default_init_body(tsym, fields, strings)},
  });
}

str_t gen_method(semantics::Symbol const &tsym, semantics::Symbol const &binding,
                 module_info_t const &m, string_pool_t &strings, str_t &fills, int &n) {
  semantics::Symbol const *actual = flu::binding_actual(binding);
  if (actual == nullptr || !actual->has<semantics::SubprogramDetails>()) return "";
  auto const &sub = actual->get<semantics::SubprogramDetails>();

  str_t const tn      = tname(tsym);
  str_t const pyname  = binding.name().ToString();
  str_t const wrapper = fmt::format("py_{}_{}", tn, pyname);

  std::vector<semantics::Symbol *> args = drop_self(sub.dummyArgs());
  str_t decls, fetch, call_args;
  if (!parse_args(args, m, "r = c_null_ptr", decls, fetch, call_args))
    return fmt::format("    ! TODO: unsupported argument(s): {}%{}\n\n", tn, pyname);

  semantics::DeclTypeSpec const *rt = sub.isFunction() ? sub.result().GetType() : nullptr;
  if (sub.isFunction() && (rt == nullptr || !intrinsic_supported(*rt)))
    return fmt::format("    ! TODO: unsupported result type: {}%{}\n\n", tn, pyname);

  ++n;
  fills += method_row(tn + "_methods", n, strings.intern(pyname), wrapper,
                      args.empty() ? "METH_NOARGS" : "METH_VARARGS");

  str_t body = decls;
  body += "        call c_f_pointer(self, pt)\n";
  body += fmt::format("        call c_f_pointer(pt%{}, p)\n", ptr_field(tsym));
  body += fetch;
  body += build_result(rt, fmt::format("p%{}({})", pyname, call_args));
  return render(tpl_method, {
    {"fn", wrapper}, {"struct", struct_name(tsym)}, {"tname", tn}, {"body", body}}) + "\n";
}

str_t gen_getset(semantics::Symbol const &tsym, semantics::Symbol const &comp,
                 string_pool_t &strings, str_t &fills, int &n) {
  str_t const tn     = tname(tsym);
  str_t const field  = comp.name().ToString();
  str_t const getter = fmt::format("py_{}_get_{}", tn, field);
  str_t const setter = fmt::format("py_{}_set_{}", tn, field);
  str_t const s_del  = strings.intern("cannot delete " + field);
  auto const *t      = comp.GetType(); // non-null: public_fields only returns typed components

  ++n;
  fills += getset_row(tn + "_getset", n, strings.intern(field), getter, setter);

  if (flu::rank_of(comp) == 1)
    return render(tpl_getset_numpy, {
      {"get_fn", getter}, {"set_fn", setter}, {"struct", struct_name(tsym)},
      {"tname", tn}, {"ptr_field", ptr_field(tsym)}, {"field", field},
      {"npy", npy(*t)}, {"raw_type", ftype(*t)},
      {"elem_bytes", std::to_string(flu::kind_of(*t))}, {"s_del", s_del}}) + "\n";

  return render(tpl_getset_scalar, {
    {"get_fn", getter}, {"set_fn", setter}, {"struct", struct_name(tsym)},
    {"tname", tn}, {"ptr_field", ptr_field(tsym)}, {"field", field},
    {"to_py_field", to_py(*t, "p%" + field)}, {"from_py_value", from_py(*t, "value")},
    {"s_del", s_del}}) + "\n";
}

str_t slot_fills(str_t const &tn) {
  str_t const sl = tn + "_slots", mt = tn + "_methods", gt = tn + "_getset";
  str_t s = fmt::format("        ! --- {} slots ---\n", tn);
  auto slot = [&](int idx, str_t const &num, str_t const &pfunc) {
    s += fmt::format("        {0}({1})%slot  = {2}\n", sl, idx, num);
    s += fmt::format("        {0}({1})%pad   = 0\n", sl, idx);
    s += fmt::format("        {0}({1})%pfunc = {2}\n", sl, idx, pfunc);
  };
  slot(1, "Py_tp_new",     fmt::format("transfer(c_funloc(py_{}_new), c_null_ptr)", tn));
  slot(2, "Py_tp_init",    fmt::format("transfer(c_funloc(py_{}_init), c_null_ptr)", tn));
  slot(3, "Py_tp_dealloc", fmt::format("transfer(c_funloc(py_{}_dealloc), c_null_ptr)", tn));
  slot(4, "Py_tp_methods", fmt::format("c_loc({}(1))", mt));
  slot(5, "Py_tp_getset",  fmt::format("c_loc({}(1))", gt));
  slot(6, "0",             "c_null_ptr");
  return s;
}

str_t spec_fills(str_t const &tn, str_t const &cls, str_t const &modpy, string_pool_t &strings) {
  str_t const sp = tn + "_spec";
  str_t s = fmt::format("        ! --- {} spec ---\n", tn);
  s += fmt::format("        {0}%name      = c_loc({1})\n", sp, strings.intern(modpy + "." + cls));
  s += fmt::format("        {0}%basicsize = int(c_sizeof(dummy_{1}), c_int)\n", sp, tn);
  s += fmt::format("        {0}%itemsize  = 0\n", sp);
  s += fmt::format("        {0}%flags     = Py_TPFLAGS_DEFAULT\n", sp);
  s += fmt::format("        {0}%pad       = 0\n", sp);
  s += fmt::format("        {0}%slots     = c_loc({1}(1))\n", sp, tn + "_slots");
  return s;
}

str_t create_fills(str_t const &tn, str_t const &cls, string_pool_t &strings) {
  str_t s;
  s += fmt::format("        type_ptr = PyType_FromSpec(c_loc({}))\n", tn + "_spec");
  s += "        if (.not. c_associated(type_ptr)) then\n";
  s += "            call Py_DecRef(mod_ptr)\n            r = c_null_ptr\n            return\n        end if\n";
  s += fmt::format("        rc = PyModule_AddObjectRef(mod_ptr, c_loc({}), type_ptr)\n", strings.intern(cls));
  s += "        call Py_DecRef(type_ptr)\n";
  s += "        if (rc < 0) then\n            call Py_DecRef(mod_ptr)\n            r = c_null_ptr\n            return\n        end if\n";
  return s;
}

} // namespace codegen
