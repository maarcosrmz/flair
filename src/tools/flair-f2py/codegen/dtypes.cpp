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
static constexpr char tpl_getset_numpy_ptr[] = {
#embed "templates/getset_numpy_ptr.txt"
    , '\0'};
static constexpr char tpl_method[] = {
#embed "templates/method.txt"
    , '\0'};
#pragma clang diagnostic pop

sema::SymbolVector public_fields(dtype_info_t const &dt) {
  sema::SymbolVector out;
  for (auto const &sym : flu::public_components(*dt.ptr)) {
    auto const *t = sym->GetType();
    if (t == nullptr || !intrinsic_supported(*t))
      continue; // intrinsic real/integer only for now
    int const rank = flu::rank_of(sym);
    if (rank == 0) {
      out.push_back(sym);
      continue;
    }
    // rank-1 arrays and allocatable/pointer for now
    if (rank == 1 && (flu::is_allocatable(sym) || flu::is_pointer(sym)))
      out.push_back(sym);
  }
  return out;
}

str_t gen_object_struct(dtype_info_t const &dt) {
  sema::Symbol const &tsym = *dt.ptr;
  return render(tpl_struct, {{"struct", struct_name(tsym)},
                             {"ptr_field", ptr_field(tsym)},
                             {"tname", tname(tsym)}});
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

// tp_new for ctor case: allocates Python wrapper but leaves the Fortran ptr
// null. The actual Fortran object is created in tp_init by the
// pointer-returning constructor.
static str_t ctor_new_body(str_t const &pf) {
  str_t b;
  b += "        r = PyType_GenericAlloc(type_ptr, 0_c_ptrdiff_t)\n";
  b += "        if (.not. c_associated(r)) return\n";
  b += "        call c_f_pointer(r, pt)\n";
  b += fmt::format("        pt%{} = c_null_ptr\n", pf);
  return b;
}

// tp_init for ctor case: parses public scalar fields as kwargs, then calls the
// pointer-returning constructor interface and stores the result.
static str_t ctor_init_body(sema::Symbol const &tsym,
                            sema::SymbolVector const &fields,
                            string_pool_t &strings) {
  sema::SymbolVector scalars;
  for (const sema::Symbol &f : fields)
    if (flu::rank_of(f) == 0)
      scalars.push_back(f);

  str_t b;
  b += fmt::format("        type({}), pointer :: pt\n", struct_name(tsym));
  b += fmt::format("        type({}), pointer :: p\n", tname(tsym));
  b += "        type(c_ptr) :: arg\n";
  for (const sema::Symbol &f : scalars) {
    str_t const nm = f.name().ToString();
    b += fmt::format("        {} :: kw_{}\n", ftype(*f.GetType()), nm);
  }
  b += "\n";

  b += "        call c_f_pointer(self, pt)\n\n";

  if (!scalars.empty()) {
    b += "        if (c_associated(kwds)) then\n";
    for (const sema::Symbol &f : scalars) {
      str_t const nm = f.name().ToString();
      b += fmt::format(
          "            arg = PyDict_GetItemString(kwds, c_loc({}))\n",
          strings.intern(nm));
      b += fmt::format("            if (c_associated(arg)) kw_{} = {}\n", nm,
                       from_py(*f.GetType(), "arg"));
    }
    b += "        end if\n\n";
  }

  str_t ctor_args;
  for (const sema::Symbol &f : scalars) {
    if (!ctor_args.empty())
      ctor_args += ", ";
    str_t const nm = f.name().ToString();
    ctor_args += nm + "=kw_" + nm;
  }
  b += fmt::format("        p => {}({})\n", tname(tsym), ctor_args);
  b += fmt::format("        pt%{} = c_loc(p)\n", ptr_field(tsym));
  return b;
}

// tp_init for init case: skips the first dummy arg (the type itself), parses
// the remaining intrinsic-typed dummies as kwargs, then calls the init
// subroutine.
static str_t init_init_body(sema::Symbol const &tsym, fnt_info_t const &init_fi,
                            string_pool_t &strings) {
  auto const &sub = init_fi.ptr->get<sema::SubprogramDetails>();
  auto const &all = sub.dummyArgs();

  // Collect supported dummies (skip first — it's the object being initialized).
  std::vector<sema::Symbol *> dummies;
  bool first = true;
  for (sema::Symbol *d : all) {
    if (first) {
      first = false;
      continue;
    }
    if (d == nullptr)
      continue;
    auto const *t = d->GetType();
    if (t != nullptr && intrinsic_supported(*t))
      dummies.push_back(d);
  }

  str_t b;
  b += fmt::format("        type({}), pointer :: pt\n", struct_name(tsym));
  b += fmt::format("        type({}), pointer :: p\n", tname(tsym));
  b += "        type(c_ptr) :: arg\n";
  for (sema::Symbol *d : dummies) {
    str_t const nm = d->name().ToString();
    b += fmt::format("        {} :: kw_{}\n", ftype(*d->GetType()), nm);
  }
  b += "\n";

  b += "        call c_f_pointer(self, pt)\n";
  b += fmt::format("        call c_f_pointer(pt%{}, p)\n\n", ptr_field(tsym));

  if (!dummies.empty()) {
    b += "        if (c_associated(kwds)) then\n";
    for (sema::Symbol *d : dummies) {
      str_t const nm = d->name().ToString();
      b += fmt::format(
          "            arg = PyDict_GetItemString(kwds, c_loc({}))\n",
          strings.intern(nm));
      b += fmt::format("            if (c_associated(arg)) kw_{} = {}\n", nm,
                       from_py(*d->GetType(), "arg"));
    }
    b += "        end if\n\n";
  }

  str_t call_args;
  for (sema::Symbol *d : dummies) {
    if (!call_args.empty())
      call_args += ", ";
    str_t const nm = d->name().ToString();
    call_args += nm + "=kw_" + nm;
  }
  str_t const init_name = init_fi.ptr->name().ToString();
  b += call_args.empty()
           ? fmt::format("        call {}(p)\n", init_name)
           : fmt::format("        call {}(p, {})\n", init_name, call_args);
  return b;
}

str_t gen_lifecycle(dtype_info_t const &dt, sema::SymbolVector const &fields,
                    string_pool_t &strings) {
  sema::Symbol const &tsym = *dt.ptr;
  str_t const tn = tname(tsym);
  str_t const pf = ptr_field(tsym);

  str_t new_body, init_body;
  if (dt.ctor.ptr != nullptr) {
    new_body = ctor_new_body(pf);
    init_body = ctor_init_body(tsym, fields, strings);
  } else if (dt.init.ptr != nullptr) {
    new_body = default_new_body(pf);
    init_body = init_init_body(tsym, dt.init, strings);
  } else {
    new_body = default_new_body(pf);
    init_body = "";
  }

  return render(tpl_lifecycle, {
                                   {"tname", tn},
                                   {"struct", struct_name(tsym)},
                                   {"ptr_field", pf},
                                   {"new_fn", "py_" + tn + "_new"},
                                   {"init_fn", "py_" + tn + "_init"},
                                   {"dealloc_fn", "py_" + tn + "_dealloc"},
                                   {"new_body", new_body},
                                   {"init_body", init_body},
                               });
}

str_t gen_method(dtype_info_t const &dt, sema::Symbol const &binding,
                 module_info_t const &m, string_pool_t &strings, str_t &fills,
                 int &n) {
  sema::Symbol const &tsym = *dt.ptr;
  sema::Symbol const *actual = flu::binding_actual(binding);
  if (actual == nullptr || !actual->has<sema::SubprogramDetails>())
    return "";
  auto const &sub = actual->get<sema::SubprogramDetails>();

  str_t const tn = tname(tsym);
  str_t const pyname = binding.name().ToString();
  str_t const wrapper = fmt::format("py_{}_{}", tn, pyname);

  std::vector<sema::Symbol *> args = drop_self(sub.dummyArgs());
  str_t decls, fetch, call_args, cleanup;
  if (!parse_args(args, m, "r = c_null_ptr", decls, fetch, call_args, &cleanup))
    return fmt::format("    ! TODO: unsupported argument(s): {}%{}\n\n", tn,
                       pyname);

  sema::DeclTypeSpec const *rt =
      sub.isFunction() ? sub.result().GetType() : nullptr;
  if (sub.isFunction() && (rt == nullptr || !intrinsic_supported(*rt)))
    return fmt::format("    ! TODO: unsupported result type: {}%{}\n\n", tn,
                       pyname);

  ++n;
  fills += method_row(tn + "_methods", n, strings.intern(pyname), wrapper,
                      args.empty() ? "METH_NOARGS" : "METH_VARARGS");

  str_t body = decls;
  body += "        call c_f_pointer(self, pt)\n";
  body += fmt::format("        call c_f_pointer(pt%{}, p)\n", ptr_field(tsym));
  body += fetch;
  body += build_result(rt, fmt::format("p%{}({})", pyname, call_args));
  body += cleanup;
  return render(tpl_method, {{"fn", wrapper},
                             {"struct", struct_name(tsym)},
                             {"tname", tn},
                             {"body", body}}) +
         "\n";
}

str_t gen_getset(dtype_info_t const &dt, sema::Symbol const &comp,
                 string_pool_t &strings, str_t &fills, int &n) {
  sema::Symbol const &tsym = *dt.ptr;
  str_t const tn = tname(tsym);
  str_t const field = comp.name().ToString();
  str_t const getter = fmt::format("py_{}_get_{}", tn, field);
  str_t const setter = fmt::format("py_{}_set_{}", tn, field);
  str_t const s_del = strings.intern("cannot delete " + field);
  auto const *t =
      comp.GetType(); // non-null: public_fields only returns typed components

  ++n;
  fills += getset_row(tn + "_getset", n, strings.intern(field), getter, setter);

  if (flu::rank_of(comp) == 1) {
    if (flu::is_pointer(comp))
      return render(
                 tpl_getset_numpy_ptr,
                 {{"get_fn", getter},
                  {"set_fn", setter},
                  {"struct", struct_name(tsym)},
                  {"tname", tn},
                  {"ptr_field", ptr_field(tsym)},
                  {"field", field},
                  {"npy", npy(*t)},
                  {"raw_type", ftype(*t)},
                  {"elem_bytes", std::to_string(flu::kind_of(*t))},
                  {"s_del", s_del},
                  {"s_unassoc",
                   strings.intern("cannot set unassociated " + field)},
                  {"s_size", strings.intern("size mismatch for " + field)}}) +
             "\n";

    return render(tpl_getset_numpy,
                  {{"get_fn", getter},
                   {"set_fn", setter},
                   {"struct", struct_name(tsym)},
                   {"tname", tn},
                   {"ptr_field", ptr_field(tsym)},
                   {"field", field},
                   {"npy", npy(*t)},
                   {"raw_type", ftype(*t)},
                   {"elem_bytes", std::to_string(flu::kind_of(*t))},
                   {"s_del", s_del}}) +
           "\n";
  }

  return render(tpl_getset_scalar, {{"get_fn", getter},
                                    {"set_fn", setter},
                                    {"struct", struct_name(tsym)},
                                    {"tname", tn},
                                    {"ptr_field", ptr_field(tsym)},
                                    {"field", field},
                                    {"to_py_field", to_py(*t, "p%" + field)},
                                    {"from_py_value", from_py(*t, "value")},
                                    {"s_del", s_del}}) +
         "\n";
}

str_t slot_fills(str_t const &tn) {
  str_t const sl = tn + "_slots", mt = tn + "_methods", gt = tn + "_getset";
  str_t s = fmt::format("        ! --- {} slots ---\n", tn);
  auto slot = [&](int idx, str_t const &num, str_t const &pfunc) {
    s += fmt::format("        {0}({1})%slot  = {2}\n", sl, idx, num);
    s += fmt::format("        {0}({1})%pad   = 0\n", sl, idx);
    s += fmt::format("        {0}({1})%pfunc = {2}\n", sl, idx, pfunc);
  };
  slot(1, "Py_tp_new",
       fmt::format("transfer(c_funloc(py_{}_new), c_null_ptr)", tn));
  slot(2, "Py_tp_init",
       fmt::format("transfer(c_funloc(py_{}_init), c_null_ptr)", tn));
  slot(3, "Py_tp_dealloc",
       fmt::format("transfer(c_funloc(py_{}_dealloc), c_null_ptr)", tn));
  slot(4, "Py_tp_methods", fmt::format("c_loc({}(1))", mt));
  slot(5, "Py_tp_getset", fmt::format("c_loc({}(1))", gt));
  slot(6, "0", "c_null_ptr");
  return s;
}

str_t spec_fills(str_t const &tn, str_t const &cls, str_t const &modpy,
                 string_pool_t &strings) {
  str_t const sp = tn + "_spec";
  str_t s = fmt::format("        ! --- {} spec ---\n", tn);
  s += fmt::format("        {0}%name      = c_loc({1})\n", sp,
                   strings.intern(modpy + "." + cls));
  s += fmt::format("        {0}%basicsize = int(c_sizeof(dummy_{1}), c_int)\n",
                   sp, tn);
  s += fmt::format("        {0}%itemsize  = 0\n", sp);
  s += fmt::format("        {0}%flags     = Py_TPFLAGS_DEFAULT\n", sp);
  s += fmt::format("        {0}%pad       = 0\n", sp);
  s +=
      fmt::format("        {0}%slots     = c_loc({1}(1))\n", sp, tn + "_slots");
  return s;
}

str_t create_fills(str_t const &tn, str_t const &cls, string_pool_t &strings) {
  str_t s;
  s += fmt::format("        type_ptr = PyType_FromSpec(c_loc({}))\n",
                   tn + "_spec");
  s += "        if (.not. c_associated(type_ptr)) then\n";
  s += "            call Py_DecRef(mod_ptr)\n            r = c_null_ptr\n      "
       "      return\n        end if\n";
  s += fmt::format(
      "        rc = PyModule_AddObjectRef(mod_ptr, c_loc({}), type_ptr)\n",
      strings.intern(cls));
  s += "        call Py_DecRef(type_ptr)\n";
  s += "        if (rc < 0) then\n            call Py_DecRef(mod_ptr)\n        "
       "    r = c_null_ptr\n            return\n        end if\n";
  return s;
}

} // namespace codegen
