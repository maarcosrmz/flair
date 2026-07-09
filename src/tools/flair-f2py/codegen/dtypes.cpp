#include "dtypes.hpp"

#include <fmt/core.h>
#include <fmt/format.h>

#include "flang/Semantics/symbol.h"
#include "flang/Semantics/type.h"

#include "flu/diagnostics.hpp"
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
static constexpr char tpl_getset_dtype[] = {
#embed "templates/getset_dtype.txt"
    , '\0'};
static constexpr char tpl_getset_dtype_ext[] = {
#embed "templates/getset_dtype_ext.txt"
    , '\0'};
static constexpr char tpl_method[] = {
#embed "templates/method.txt"
    , '\0'};
#pragma clang diagnostic pop

sema::SymbolVector public_fields(dtype_info_t const &dt,
                                 module_info_t const &m) {
  sema::SymbolVector out;
  for (auto const &sym : flu::public_components(*dt.ptr)) {
    auto const *t = sym->GetType();
    if (auto const c =
            t != nullptr ? classify_dtype(*t, m)
                         : dtype_class_t{dtype_class::NotDerived, nullptr, {}};
        c.cls != dtype_class::NotDerived) {
      if (c.cls == dtype_class::Unsupported) {
        flu::emit_warning(*sym,
                          "flair-f2py: cannot expose component '" +
                              sym->name().ToString() + "': derived type '" +
                              flu::derived_name(*t) +
                              "' is not a wrapped type; property skipped");
        continue;
      }
      if (flu::rank_of(sym) != 0 || flu::is_pointer(sym) ||
          flu::is_allocatable(sym)) {
        flu::emit_warning(
            *sym, "flair-f2py: cannot expose component '" +
                      sym->name().ToString() +
                      "': only inline scalar derived-type components are "
                      "supported; property skipped");
        continue;
      }
      out.push_back(sym);
      continue;
    }
    if (t == nullptr || !intrinsic_supported(*t)) {
      // intrinsic real/integer only for now
      flu::emit_warning(*sym, "flair-f2py: cannot expose component '" +
                                  sym->name().ToString() +
                                  "': unsupported type; property skipped");
      continue;
    }
    int const rank = flu::rank_of(sym);
    if (rank == 0) {
      out.push_back(sym);
      continue;
    }
    // rank-1 arrays and allocatable/pointer for now
    if (rank == 1 && (flu::is_allocatable(sym) || flu::is_pointer(sym))) {
      out.push_back(sym);
      continue;
    }
    flu::emit_warning(
        *sym, "flair-f2py: cannot expose component '" + sym->name().ToString() +
                  "': only scalars and rank-1 pointer/allocatable "
                  "arrays are supported; property skipped");
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
  b += "        pt%owner = c_null_ptr\n";
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
  b += "        pt%owner = c_null_ptr\n";
  return b;
}

// Local declarations shared by the two tp_init bodies: the kwarg scratch
// pointer, one typed kw_<name> / logical got_<name> per accepted keyword, and
// the scratch used by the unknown-keyword scan. A same-module derived-type
// keyword gets a wrapper-struct pointer (kwpt_<name>) plus a Fortran pointer
// kw_<name>; a foreign one only the Fortran pointer (unwrapped via the external
// converter).
static str_t arg_check_decls(std::vector<sema::Symbol const *> const &accepted,
                             module_info_t const &m) {
  str_t d;
  d += "        type(c_ptr) :: arg\n";
  for (sema::Symbol const *s : accepted) {
    str_t const nm = s->name().ToString();
    if (auto const c = classify_dtype(*s->GetType(), m);
        c.cls == dtype_class::Local) {
      d += fmt::format("        type({}), pointer :: kwpt_{}\n",
                       struct_name(*c.sym), nm);
      d += fmt::format("        type({}), pointer :: kw_{}\n", tname(*c.sym),
                       nm);
    } else if (c.cls == dtype_class::Foreign) {
      d += fmt::format("        type({}), pointer :: kw_{}\n", tname(*c.sym),
                       nm);
    } else {
      d += fmt::format("        {} :: kw_{}\n", ftype(*s->GetType()), nm);
    }
    d += fmt::format("        logical :: got_{}\n", nm);
  }
  d += "        integer(c_ptrdiff_t) :: kw_pos\n";
  d += "        type(c_ptr) :: kw_key, kw_val, kw_msg\n";
  d += "        logical :: kw_known\n";
  return d;
}

// Argument validation shared by the two tp_init bodies. Emits, in order: a
// guard rejecting positional args, a scan raising TypeError on the first
// unexpected keyword (named CPython-style), the read loop that records
// presence, and a missing-required-argument check per accepted keyword. Any
// failure sets r = -1 and returns early; success falls through to the
// template's trailing r = 0.
static str_t arg_check_stmts(std::vector<sema::Symbol const *> const &accepted,
                             module_info_t const &m, str_t const &pyname,
                             string_pool_t &strings) {
  str_t b;

  // Reject positional arguments: tp_init is keyword-only here.
  str_t const s_pos =
      strings.intern(pyname + "() takes no positional arguments");
  b += "        if (c_associated(args)) then\n";
  b += "            if (PyTuple_Size(args) > 0_c_ptrdiff_t) then\n";
  b += fmt::format(
      "                call PyErr_SetString(PyExc_TypeError, c_loc({}))\n",
      s_pos);
  b += "                r = -1\n";
  b += "                return\n";
  b += "            end if\n";
  b += "        end if\n\n";

  for (sema::Symbol const *s : accepted)
    b += fmt::format("        got_{} = .false.\n", s->name().ToString());
  if (!accepted.empty())
    b += "\n";

  // Scan every key: raise on the first one that is not an accepted keyword,
  // then read the accepted ones and mark them present. Both walk the same dict.
  str_t const s_q = strings.intern("'");
  str_t const s_badkw =
      strings.intern("' is an invalid keyword argument for " + pyname + "()");
  b += "        if (c_associated(kwds)) then\n";
  b += "            kw_pos = 0_c_ptrdiff_t\n";
  b +=
      "            do while (PyDict_Next(kwds, kw_pos, kw_key, kw_val) /= 0)\n";
  b += "                kw_known = .false.\n";
  for (sema::Symbol const *s : accepted)
    b += fmt::format("                if (PyUnicode_CompareWithASCIIString(kw_"
                     "key, c_loc({})) == 0) kw_known = .true.\n",
                     strings.intern(s->name().ToString()));
  b += "                if (.not. kw_known) then\n";
  b += fmt::format("                    kw_msg = PyUnicode_Concat(PyUnicode_"
                   "FromString(c_loc({})), kw_key)\n",
                   s_q);
  b += fmt::format("                    kw_msg = PyUnicode_Concat(kw_msg, "
                   "PyUnicode_FromString(c_loc({})))\n",
                   s_badkw);
  // kw_msg leaks by one ref on this error path (PyErr_SetObject takes its own);
  // acceptable on an error exit, consistent with existing generated code.
  b += "                    call PyErr_SetObject(PyExc_TypeError, kw_msg)\n";
  b += "                    r = -1\n";
  b += "                    return\n";
  b += "                end if\n";
  b += "            end do\n";
  for (sema::Symbol const *s : accepted) {
    str_t const nm = s->name().ToString();
    b +=
        fmt::format("            arg = PyDict_GetItemString(kwds, c_loc({}))\n",
                    strings.intern(nm));
    b += "            if (c_associated(arg)) then\n";
    if (auto const c = classify_dtype(*s->GetType(), m);
        c.cls == dtype_class::Local) {
      str_t const s_argtype =
          strings.intern(pyname + "() argument '" + nm + "' must be a " +
                         clsname(*c.sym) + " instance");
      b += fmt::format("                if (PyObject_IsInstance(arg, "
                       "py_{}_type_obj) /= 1) then\n",
                       tname(*c.sym));
      // IsInstance may return -1 with its own exception set; don't clobber it.
      b += "                    if (.not. c_associated(PyErr_Occurred())) "
           "then\n";
      b += fmt::format("                        call "
                       "PyErr_SetString(PyExc_TypeError, c_loc({}))\n",
                       s_argtype);
      b += "                    end if\n";
      b += "                    r = -1\n";
      b += "                    return\n";
      b += "                end if\n";
      b += fmt::format("                call c_f_pointer(arg, kwpt_{})\n", nm);
      b += fmt::format("                call c_f_pointer(kwpt_{}%{}, kw_{})\n",
                       nm, ptr_field(*c.sym), nm);
    } else if (c.cls == dtype_class::Foreign) {
      // The external converter isinstance-checks and sets the exception; a
      // disassociated result signals failure.
      b += fmt::format("                kw_{} => {}(arg)\n", nm,
                       from_pyobject_fn(tname(*c.sym)));
      b += fmt::format("                if (.not. associated(kw_{})) then\n",
                       nm);
      b += "                    r = -1\n";
      b += "                    return\n";
      b += "                end if\n";
    } else {
      b += fmt::format("                kw_{} = {}\n", nm,
                       from_py(*s->GetType(), "arg"));
    }
    b += fmt::format("                got_{} = .true.\n", nm);
    b += "            end if\n";
  }
  b += "        end if\n\n";

  // Every accepted keyword is required (no optional-dummy support): missing one
  // would otherwise leave kw_<name> uninitialized and pass garbage to Fortran.
  for (sema::Symbol const *s : accepted) {
    str_t const nm = s->name().ToString();
    str_t const s_miss =
        strings.intern(pyname + "() missing required argument '" + nm + "'");
    b += fmt::format("        if (.not. got_{}) then\n", nm);
    b += fmt::format(
        "            call PyErr_SetString(PyExc_TypeError, c_loc({}))\n",
        s_miss);
    b += "            r = -1\n";
    b += "            return\n";
    b += "        end if\n";
  }
  if (!accepted.empty())
    b += "\n";

  return b;
}

// A dummy usable as a keyword-only __init__ argument: an intrinsic scalar, or
// an inline scalar of a wrapped (local or recordable-foreign) derived type.
// Pointer/allocatable dummies are rejected — a plain kw_<name> local can't be
// the actual for a pointer dummy, and associating a user-held pointer with
// wrapper-owned storage has unmanageable lifetime anyway.
static bool kwarg_supported(sema::Symbol const &d, module_info_t const &m,
                            ext_types_t &ext_types) {
  auto const *t = d.GetType();
  if (t == nullptr)
    return false;
  if (flu::rank_of(d) != 0 || flu::is_pointer(d) || flu::is_allocatable(d))
    return false;
  if (intrinsic_supported(*t))
    return true;
  auto const c = classify_dtype(*t, m);
  return c.cls == dtype_class::Local ||
         (c.cls == dtype_class::Foreign && note_ext_type(ext_types, *c.sym));
}

bool ctor_kwargs(dtype_info_t const &dt, module_info_t const &m,
                 ext_types_t &ext_types,
                 std::vector<sema::Symbol const *> &out) {
  sema::SymbolVector const specifics = flu::get_specific_procs(*dt.ctor.ptr);
  if (specifics.size() != 1)
    return false;
  sema::Symbol const &spec = specifics.front();
  if (!spec.has<sema::SubprogramDetails>())
    return false;
  auto const &sub = spec.get<sema::SubprogramDetails>();
  if (!sub.isFunction())
    return false;
  // Every dummy is a real constructor argument (no passed-object to skip) and
  // required for the generated call to compile, so one unsupported dummy makes
  // the whole constructor -- and with it the type -- unwrappable.
  for (sema::Symbol *d : sub.dummyArgs()) {
    if (d == nullptr || !kwarg_supported(*d, m, ext_types))
      return false;
    out.push_back(d);
  }
  return true;
}

// tp_init for ctor case: parses the constructor specific's dummy args as
// kwargs, then calls the pointer-returning constructor interface (keyed on the
// dummy names, which is what generic resolution needs) and stores the result.
static str_t ctor_init_body(dtype_info_t const &dt, module_info_t const &m,
                            string_pool_t &strings, ext_types_t &ext_types) {
  sema::Symbol const &tsym = *dt.ptr;
  std::vector<sema::Symbol const *> accepted;
  // Cannot fail here: codegen_module's pre-pass already skipped types whose
  // constructor is not wrappable.
  ctor_kwargs(dt, m, ext_types, accepted);

  str_t const pyname = tname(tsym);

  str_t b;
  b += fmt::format("        type({}), pointer :: pt\n", struct_name(tsym));
  b += fmt::format("        type({}), pointer :: p\n", tname(tsym));
  b += arg_check_decls(accepted, m);
  b += "\n";

  b += "        call c_f_pointer(self, pt)\n\n";

  b += arg_check_stmts(accepted, m, pyname, strings);

  str_t ctor_args;
  for (sema::Symbol const *f : accepted) {
    if (!ctor_args.empty())
      ctor_args += ", ";
    str_t const nm = f->name().ToString();
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
                            module_info_t const &m, string_pool_t &strings,
                            ext_types_t &ext_types) {
  auto const &sub = init_fi.ptr->get<sema::SubprogramDetails>();
  auto const &all = sub.dummyArgs();

  // Collect supported dummies (skip first — it's the object being initialized).
  std::vector<sema::Symbol const *> accepted;
  bool first = true;
  for (sema::Symbol *d : all) {
    if (first) {
      first = false;
      continue;
    }
    if (d == nullptr)
      continue;
    if (kwarg_supported(*d, m, ext_types)) {
      accepted.push_back(d);
      continue;
    }
    flu::emit_error(*d, "flair-f2py: cannot wrap init argument '" +
                            d->name().ToString() +
                            "': unsupported type; annotate the initializer '" +
                            init_fi.ptr->name().ToString() +
                            "' with a '!flair$ ignore' directive to skip it");
  }

  str_t const pyname = tname(tsym);

  str_t b;
  b += fmt::format("        type({}), pointer :: pt\n", struct_name(tsym));
  b += fmt::format("        type({}), pointer :: p\n", tname(tsym));
  b += arg_check_decls(accepted, m);
  b += "\n";

  b += "        call c_f_pointer(self, pt)\n";
  b += fmt::format("        call c_f_pointer(pt%{}, p)\n\n", ptr_field(tsym));

  b += arg_check_stmts(accepted, m, pyname, strings);

  str_t call_args;
  for (sema::Symbol const *d : accepted) {
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

str_t gen_lifecycle(dtype_info_t const &dt, module_info_t const &m,
                    string_pool_t &strings, ext_types_t &ext_types) {
  sema::Symbol const &tsym = *dt.ptr;
  str_t const tn = tname(tsym);
  str_t const pf = ptr_field(tsym);

  str_t new_body, init_body;
  if (dt.ctor.ptr != nullptr) {
    new_body = ctor_new_body(pf);
    init_body = ctor_init_body(dt, m, strings, ext_types);
  } else if (dt.init.ptr != nullptr) {
    new_body = default_new_body(pf);
    init_body = init_init_body(tsym, dt.init, m, strings, ext_types);
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
                 int &n, ext_types_t &ext_types) {
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
  if (!parse_args(args, m, tn, "r = c_null_ptr", decls, fetch, call_args,
                  strings, &cleanup, &ext_types))
    return fmt::format("    ! TODO: unsupported argument(s): {}%{}\n\n", tn,
                       pyname);

  sema::DeclTypeSpec const *rt =
      sub.isFunction() ? sub.result().GetType() : nullptr;
  if (sub.isFunction() && (rt == nullptr || !intrinsic_supported(*rt))) {
    flu::emit_error(binding,
                    "flair-f2py: cannot wrap method '" + tn + "%" + pyname +
                        "': unsupported result type; annotate the type '" + tn +
                        "' with a '!flair$ ignore' directive to skip "
                        "it");
    return fmt::format("    ! TODO: unsupported result type: {}%{}\n\n", tn,
                       pyname);
  }

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
                 module_info_t const &m, string_pool_t &strings, str_t &fills,
                 int &n, ext_types_t &ext_types) {
  sema::Symbol const &tsym = *dt.ptr;
  str_t const tn = tname(tsym);
  str_t const field = comp.name().ToString();
  str_t const getter = fmt::format("py_{}_get_{}", tn, field);
  str_t const setter = fmt::format("py_{}_set_{}", tn, field);
  str_t const s_del = strings.intern("cannot delete " + field);
  auto const *t =
      comp.GetType(); // non-null: public_fields only returns typed components

  // Derived components are guaranteed wrapped (Local/Foreign) and inline
  // scalar: public_fields filters everything else. Classify before emitting
  // the table row so a non-recordable foreign type skips the property cleanly.
  auto const c = classify_dtype(*t, m);
  if (c.cls == dtype_class::Foreign && !note_ext_type(ext_types, *c.sym))
    return fmt::format("    ! TODO: unsupported component: {}%{}\n\n", tn,
                       field);

  ++n;
  fills += getset_row(tn + "_getset", n, strings.intern(field), getter, setter);

  if (c.cls == dtype_class::Local) {
    str_t const stn = tname(*c.sym);
    return render(tpl_getset_dtype,
                  {{"get_fn", getter},
                   {"set_fn", setter},
                   {"struct", struct_name(tsym)},
                   {"tname", tn},
                   {"ptr_field", ptr_field(tsym)},
                   {"field", field},
                   {"sub_struct", struct_name(*c.sym)},
                   {"sub_tname", stn},
                   {"sub_ptr_field", ptr_field(*c.sym)},
                   {"type_obj", "py_" + stn + "_type_obj"},
                   {"s_del", s_del},
                   {"s_type", strings.intern(field + " must be a " +
                                             clsname(*c.sym) + " instance")}}) +
           "\n";
  }

  if (c.cls == dtype_class::Foreign) {
    str_t const stn = tname(*c.sym);
    return render(tpl_getset_dtype_ext, {{"get_fn", getter},
                                         {"set_fn", setter},
                                         {"struct", struct_name(tsym)},
                                         {"tname", tn},
                                         {"ptr_field", ptr_field(tsym)},
                                         {"field", field},
                                         {"sub_tname", stn},
                                         {"view_fn", view_pyobject_fn(stn)},
                                         {"from_fn", from_pyobject_fn(stn)},
                                         {"s_del", s_del}}) +
           "\n";
  }

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
  // The module keeps the type object reachable (and strongly referenced) for
  // view getters (PyType_GenericAlloc) and isinstance checks.
  s += fmt::format("        py_{}_type_obj = type_ptr\n", tn);
  s += "        if (rc < 0) then\n";
  s += "            call Py_DecRef(type_ptr)\n";
  s += fmt::format("            py_{}_type_obj = c_null_ptr\n", tn);
  s += "            call Py_DecRef(mod_ptr)\n            r = c_null_ptr\n      "
       "      return\n        end if\n";
  return s;
}

} // namespace codegen
