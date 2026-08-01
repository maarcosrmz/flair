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

// METH_NOARGS calling convention passes (self, NULL) only, so those wrappers
// keep the two-parameter signature.
static constexpr char tpl_noargs_method[] =
    "    function {fn}(self, args) bind(C) result(r)\n"
    "        type(c_ptr), value :: self, args\n"
    "        type(c_ptr) :: r\n"
    "        type(FLAIR_object_t), pointer :: pt\n"
    "        type({tname}), pointer :: p\n"
    "{body}\n"
    "    end function\n";

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
    if (t == nullptr) {
      flu::emit_warning(*sym, "flair-f2py: cannot expose component '" +
                                  sym->name().ToString() +
                                  "': unsupported type; property skipped");
      continue;
    }
    int const rank = flu::rank_of(sym);
    if (rank == 0) {
      if (!intrinsic_supported(*t)) {
        flu::emit_warning(*sym, "flair-f2py: cannot expose component '" +
                                    sym->name().ToString() +
                                    "': unsupported type; property skipped");
        continue;
      }
      if (flu::is_pointer(sym) || flu::is_allocatable(sym)) {
        flu::emit_warning(*sym,
                          "flair-f2py: cannot expose component '" +
                              sym->name().ToString() +
                              "': only inline intrinsic scalar components are "
                              "supported; property skipped");
        continue;
      }
      out.push_back(sym);
      continue;
    }
    // rank-1 arrays and allocatable/pointer for now
    if (rank == 1 && array_supported(*t) &&
        (flu::is_allocatable(sym) || flu::is_pointer(sym))) {
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

static str_t default_new_body() {
  str_t b;
  b += "        r = PyType_GenericAlloc(type_ptr, 0_c_ptrdiff_t)\n";
  b += "        if (.not. c_associated(r)) return\n";
  b += "        call c_f_pointer(r, pt)\n";
  b += "        allocate(p)   ! component default-initializers apply here\n";
  b += "        pt%data = c_loc(p)\n";
  b += "        pt%owner = c_null_ptr\n";
  return b;
}

// tp_new for ctor case: allocates Python wrapper but leaves the Fortran ptr
// null. The actual Fortran object is created in tp_init by the
// pointer-returning constructor.
static str_t ctor_new_body() {
  str_t b;
  b += "        r = PyType_GenericAlloc(type_ptr, 0_c_ptrdiff_t)\n";
  b += "        if (.not. c_associated(r)) return\n";
  b += "        call c_f_pointer(r, pt)\n";
  b += "        pt%data = c_null_ptr\n";
  b += "        pt%owner = c_null_ptr\n";
  return b;
}

// Local declarations shared by the two tp_init bodies: the name/required/bound
// tables FLAIR_parse_args works over, and one typed kw_<name> per accepted
// keyword. A derived-type keyword unwraps to a Fortran pointer (via the
// runtime for a same-module type, via the producer wrapper's converter for a
// foreign one); an intrinsic one converts through a shared scratch of its
// category.
static str_t arg_check_decls(std::vector<sema::Symbol const *> const &accepted,
                             module_info_t const &m) {
  str_t d;
  bool any_real = false, any_int = false, any_logical = false,
       any_char = false, any_complex = false;
  bool any_local = false;
  size_t const n = accepted.size();
  d += fmt::format("        type(c_ptr) :: objs({})\n", n);
  d += fmt::format("        type(c_ptr) :: argnames({})\n", n);
  d += fmt::format("        logical :: argreq({})\n", n);
  for (sema::Symbol const *s : accepted) {
    str_t const nm = s->name().ToString();
    if (auto const c = classify_dtype(*s->GetType(), m);
        c.cls == dtype_class::Local) {
      any_local = true;
      d += fmt::format("        type({}), pointer :: kw_{}\n", tname(*c.sym),
                       nm);
    } else if (c.cls == dtype_class::Foreign) {
      d += fmt::format("        type({}), pointer :: kw_{}\n", tname(*c.sym),
                       nm);
    } else {
      d += fmt::format("        {} :: kw_{}\n", ftype(*s->GetType()), nm);
      switch (*flu::category(*s->GetType())) {
      case Fortran::common::TypeCategory::Real:
        any_real = true;
        break;
      case Fortran::common::TypeCategory::Complex:
        any_complex = true;
        break;
      case Fortran::common::TypeCategory::Logical:
        any_logical = true;
        break;
      case Fortran::common::TypeCategory::Character:
        any_char = true;
        break;
      default:
        any_int = true;
        break;
      }
    }
  }
  if (any_local)
    d += "        type(c_ptr) :: kw_raw\n";
  // Shared scratch for the checked intrinsic converters (one conversion is
  // checked before the next starts, so a single set suffices).
  if (any_real)
    d += "        real(c_double) :: kw_vr\n";
  if (any_complex)
    d += "        complex(c_double_complex) :: kw_vz\n";
  if (any_int)
    d += "        integer(c_long_long) :: kw_vi\n";
  if (any_logical)
    d += "        logical(c_bool) :: kw_vl\n";
  if (any_char)
    d += "        character(:), allocatable :: kw_vc\n";
  if (any_real || any_complex || any_int || any_logical || any_char)
    d += "        logical :: kw_ok\n";
  return d;
}

// Argument validation shared by the two tp_init bodies: a guard rejecting
// positional args, the runtime binding of the accepted keywords, and the
// per-keyword conversion. Every failure `exit init`s, leaving r at the -1 the
// caller preset; success falls through to the trailing r = 0 inside the block.
static str_t arg_check_stmts(std::vector<sema::Symbol const *> const &accepted,
                             module_info_t const &m, str_t const &pyname,
                             string_pool_t &strings) {
  str_t b;

  // Reject positional arguments: tp_init is keyword-only here.
  b += fmt::format(
      "        if (.not. FLAIR_no_positional(args, c_loc({}))) exit init\n",
      strings.intern(pyname + "() takes no positional arguments"));

  // Bind the keywords through the same runtime entry point the module
  // functions use; passing a null argument tuple keeps this keyword-only.
  // Every accepted keyword is required (no optional-dummy support), so the
  // runtime also reports a missing one.
  std::vector<str_t> names, reqs;
  for (sema::Symbol const *s : accepted) {
    names.push_back(
        fmt::format("c_loc({})", strings.intern(s->name().ToString())));
    reqs.push_back(".true.");
  }
  b += array_assign("        ", "argnames", names);
  b += array_assign("        ", "argreq", reqs);
  b += "        if (.not. FLAIR_parse_args(c_null_ptr, kwds, argnames, "
       "argreq, objs)) exit init\n";

  int idx = 0;
  for (sema::Symbol const *s : accepted) {
    str_t const nm = s->name().ToString();
    str_t const obj = fmt::format("objs({})", ++idx);
    if (auto const c = classify_dtype(*s->GetType(), m);
        c.cls == dtype_class::Local) {
      b += fmt::format("        kw_raw = FLAIR_unwrap_arg({}, py_{}_type_obj, "
                       "c_loc({}), c_loc({}))\n",
                       obj, tname(*c.sym), strings.intern(nm),
                       strings.intern(clsname(*c.sym)));
      b += "        if (.not. c_associated(kw_raw)) exit init\n";
      b += fmt::format("        call c_f_pointer(kw_raw, kw_{})\n", nm);
    } else if (c.cls == dtype_class::Foreign) {
      // The external converter isinstance-checks and sets the exception; a
      // disassociated result signals failure.
      b += fmt::format("        kw_{} => {}({})\n", nm,
                       from_pyobject_fn(tname(*c.sym)), obj);
      b += fmt::format("        if (.not. associated(kw_{})) exit init\n", nm);
    } else {
      // Checked conversion into the shared scratch; on failure the helper
      // leaves the Python exception pending.
      auto const *t = s->GetType();
      str_t scratch;
      switch (*flu::category(*t)) {
      case Fortran::common::TypeCategory::Real:
        scratch = "kw_vr";
        break;
      case Fortran::common::TypeCategory::Complex:
        scratch = "kw_vz";
        break;
      case Fortran::common::TypeCategory::Logical:
        scratch = "kw_vl";
        break;
      case Fortran::common::TypeCategory::Character:
        scratch = "kw_vc";
        break;
      default:
        scratch = "kw_vi";
        break;
      }
      b += fmt::format("        {} = {}({}, kw_ok)\n", scratch, py_helper(*t),
                       obj);
      b += "        if (.not. kw_ok) exit init\n";
      if (auto const cl = flu::char_len(*t)) {
        // kw_<nm> is character(N): reject longer strings instead of letting
        // the assignment truncate; shorter ones blank-pad.
        b += fmt::format(
            "        if (.not. FLAIR_check_len(kw_vc, {}, c_loc({}))) exit "
            "init\n",
            *cl, strings.intern(nm));
      }
      b += fmt::format("        kw_{} = {}\n", nm, narrow(*t, scratch));
    }
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
// kwargs, then calls the constructor interface (keyed on the dummy names,
// which is what generic resolution needs) and stores the result. A
// pointer-returning specific hands over its target; a value-returning one is
// assigned into wrapper-allocated storage.
static str_t ctor_init_body(dtype_info_t const &dt, module_info_t const &m,
                            string_pool_t &strings, ext_types_t &ext_types) {
  sema::Symbol const &tsym = *dt.ptr;
  std::vector<sema::Symbol const *> accepted;
  // Cannot fail here: codegen_module's pre-pass already skipped types whose
  // constructor is not wrappable.
  ctor_kwargs(dt, m, ext_types, accepted);
  bool const ptr_result = flu::get_specific_procs(*dt.ctor.ptr)
                              .front()
                              .get()
                              .get<sema::SubprogramDetails>()
                              .result()
                              .attrs()
                              .test(sema::Attr::POINTER);

  str_t const pyname = tname(tsym);

  str_t b;
  b += "        type(FLAIR_object_t), pointer :: pt\n";
  b += fmt::format("        type({}), pointer :: p\n", tname(tsym));
  b += arg_check_decls(accepted, m);
  b += "\n";

  b += "        call c_f_pointer(self, pt)\n\n";
  b += "        r = -1\n";
  b += "        init: block\n";

  str_t body = arg_check_stmts(accepted, m, pyname, strings);

  str_t ctor_args;
  for (sema::Symbol const *f : accepted) {
    if (!ctor_args.empty())
      ctor_args += ", ";
    str_t const nm = f->name().ToString();
    ctor_args += nm + "=kw_" + nm;
  }
  if (ptr_result) {
    body += fmt::format("        p => {}({})\n", tname(tsym), ctor_args);
  } else {
    body += "        allocate(p)\n";
    body += fmt::format("        p = {}({})\n", tname(tsym), ctor_args);
  }
  body += "        pt%data = c_loc(p)\n";
  body += "        r = 0\n";
  b += indent_lines(body);
  b += "        end block init\n";
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
  b += "        type(FLAIR_object_t), pointer :: pt\n";
  b += fmt::format("        type({}), pointer :: p\n", tname(tsym));
  b += arg_check_decls(accepted, m);
  b += "\n";

  b += "        call c_f_pointer(self, pt)\n";
  b += "        call c_f_pointer(pt%data, p)\n\n";
  b += "        r = -1\n";
  b += "        init: block\n";

  str_t body = arg_check_stmts(accepted, m, pyname, strings);

  str_t call_args;
  for (sema::Symbol const *d : accepted) {
    if (!call_args.empty())
      call_args += ", ";
    str_t const nm = d->name().ToString();
    call_args += nm + "=kw_" + nm;
  }
  str_t const init_name = init_fi.ptr->name().ToString();
  body += call_args.empty()
              ? fmt::format("        call {}(p)\n", init_name)
              : fmt::format("        call {}(p, {})\n", init_name, call_args);
  body += "        r = 0\n";
  b += indent_lines(body);
  b += "        end block init\n";
  return b;
}

str_t gen_lifecycle(dtype_info_t const &dt, module_info_t const &m,
                    string_pool_t &strings, ext_types_t &ext_types) {
  sema::Symbol const &tsym = *dt.ptr;
  str_t const tn = tname(tsym);

  str_t new_body, init_body;
  if (dt.ctor.ptr != nullptr) {
    new_body = ctor_new_body();
    init_body = ctor_init_body(dt, m, strings, ext_types);
  } else if (dt.init.ptr != nullptr) {
    new_body = default_new_body();
    init_body = init_init_body(tsym, dt.init, m, strings, ext_types);
  } else {
    // No constructor and no initializer: __init__ is a no-op that succeeds.
    new_body = default_new_body();
    init_body = "        r = 0\n";
  }

  return render(tpl_lifecycle, {
                                   {"tname", tn},
                                   // tp_-prefixed so a type-bound procedure
                                   // named init/new/dealloc cannot collide
                                   {"new_fn", "py_" + tn + "_tp_new"},
                                   {"init_fn", "py_" + tn + "_tp_init"},
                                   {"dealloc_fn", "py_" + tn + "_tp_dealloc"},
                                   {"new_body", new_body},
                                   {"init_body", init_body},
                               });
}

str_t gen_method(dtype_info_t const &dt, sema::Symbol const &binding,
                 module_info_t const &m, string_pool_t &strings, str_t *fills,
                 int &n, ext_types_t &ext_types, sema::Symbol const *self_type,
                 poly_overrides_t const *overrides, str_t const &wrapper_name) {
  sema::Symbol const &tsym = self_type != nullptr ? *self_type : *dt.ptr;
  sema::Symbol const *actual = flu::binding_actual(binding);
  if (actual == nullptr || !actual->has<sema::SubprogramDetails>())
    return "";
  auto const &sub = actual->get<sema::SubprogramDetails>();

  str_t const tn = tname(tsym);
  str_t const pyname = binding.name().ToString();
  str_t const wrapper =
      wrapper_name.empty() ? fmt::format("py_{}_{}", tn, pyname) : wrapper_name;

  std::vector<sema::Symbol *> args = drop_self(sub.dummyArgs());
  str_t decls, pre, fetch, call_args, cleanup;
  if (!parse_args(args, m, tn, decls, pre, fetch, call_args, strings, &cleanup,
                  &ext_types, overrides))
    return fills == nullptr
               ? str_t{}
               : fmt::format("    ! TODO: unsupported argument(s): {}%{}\n\n",
                             tn, pyname);

  sema::DeclTypeSpec const *rt =
      sub.isFunction() ? sub.result().GetType() : nullptr;
  if (sub.isFunction() && (rt == nullptr || !intrinsic_supported(*rt) ||
                           sub.result().Rank() != 0)) {
    flu::emit_error(binding,
                    "flair-f2py: cannot wrap method '" + tn + "%" + pyname +
                        "': unsupported result type; annotate the type '" + tn +
                        "' with a '!flair$ ignore' directive to skip "
                        "it");
    return fills == nullptr
               ? str_t{}
               : fmt::format("    ! TODO: unsupported result type: {}%{}\n\n",
                             tn, pyname);
  }

  if (fills != nullptr) {
    ++n;
    *fills += method_row(tn + "_methods", n, strings.intern(pyname), wrapper,
                         args.empty() ? "METH_NOARGS"
                                      : "METH_VARARGS + METH_KEYWORDS");
  }

  // Unwrapping self cannot fail (tp_methods are only reached on an instance),
  // so it precedes the failure block.
  str_t self_unwrap;
  self_unwrap += "        call c_f_pointer(self, pt)\n";
  self_unwrap += "        call c_f_pointer(pt%data, p)\n";
  str_t const body = wrap_body(
      decls, self_unwrap + pre, fetch,
      build_result(rt, fmt::format("p%{}({})", pyname, call_args)), cleanup,
      "c_null_ptr");
  return render(args.empty() ? tpl_noargs_method : tpl_method,
                {{"fn", wrapper},
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
                   {"tname", tn},
                   {"field", field},
                   {"sub_tname", stn},
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
                                         {"tname", tn},
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
                  {"tname", tn},
                  {"field", field},
                  {"npy", npy(*t)},
                  {"raw_type", ftype(*t)},
                  {"s_del", s_del},
                  {"s_unassoc",
                   strings.intern("cannot set unassociated " + field)},
                  {"s_size", strings.intern("size mismatch for " + field)}}) +
             "\n";

    return render(tpl_getset_numpy,
                  {{"get_fn", getter},
                   {"set_fn", setter},
                   {"tname", tn},
                   {"field", field},
                   {"npy", npy(*t)},
                   {"raw_type", ftype(*t)},
                   {"s_del", s_del}}) +
           "\n";
  }

  // character(N) fields reject longer strings instead of letting the
  // assignment truncate; shorter ones blank-pad (r = -1 is already set).
  str_t check;
  if (auto const cl = flu::char_len(*t)) {
    str_t const s_len = strings.intern(
        fmt::format("{} exceeds character length {}", field, *cl));
    check += fmt::format("        if (len(tmp) > {}) then\n", *cl);
    check += fmt::format(
        "            call PyErr_SetString(PyExc_ValueError, c_loc({}))\n",
        s_len);
    check += "            return\n";
    check += "        end if\n";
  }
  return render(tpl_getset_scalar, {{"get_fn", getter},
                                    {"set_fn", setter},
                                    {"tname", tn},
                                    {"field", field},
                                    {"to_py_field", to_py(*t, "p%" + field)},
                                    {"ctype", py_ctype(*t)},
                                    {"helper", py_helper(*t)},
                                    {"narrow_tmp", narrow(*t, "tmp")},
                                    {"check", check},
                                    {"s_del", s_del}}) +
         "\n";
}

str_t slot_fills(str_t const &tn) {
  str_t const sl = tn + "_slots", mt = tn + "_methods", gt = tn + "_getset";
  str_t s = fmt::format("        ! --- {} slots ---\n", tn);
  auto slot = [&](int idx, str_t const &num, str_t const &fn) {
    s += fmt::format("        call FLAIR_set_slot({}, {}, {}, c_funloc({}))\n",
                     sl, idx, num, fn);
  };
  slot(1, "Py_tp_new", fmt::format("py_{}_tp_new", tn));
  slot(2, "Py_tp_init", fmt::format("py_{}_tp_init", tn));
  slot(3, "Py_tp_dealloc", fmt::format("py_{}_tp_dealloc", tn));
  // These two slots carry table addresses, not function pointers.
  s += fmt::format(
      "        call FLAIR_set_slot_ptr({}, 4, Py_tp_methods, c_loc({}(1)))\n",
      sl, mt);
  s += fmt::format(
      "        call FLAIR_set_slot_ptr({}, 5, Py_tp_getset, c_loc({}(1)))\n",
      sl, gt);
  s += fmt::format("        call FLAIR_end_slots({}, 6)\n", sl);
  return s;
}

// The spec now lives inside FLAIR_add_type, so creation is one call; the
// module holds the type's only strong reference, which is what keeps it alive
// for view getters (PyType_GenericAlloc) and isinstance checks.
str_t create_fills(str_t const &tn, str_t const &cls, str_t const &pyqual,
                   string_pool_t &strings) {
  str_t s;
  s += fmt::format(
      "        py_{0}_type_obj = FLAIR_add_type(mod_ptr, c_loc({1}), "
      "c_loc({2}), int(c_sizeof(dummy_obj), c_int), {0}_slots)\n",
      tn, strings.intern(pyqual + "." + cls), strings.intern(cls));
  s += fmt::format(
      "        if (.not. c_associated(py_{}_type_obj)) then\n", tn);
  s += "            call Py_DecRef(mod_ptr)\n";
  s += "            r = c_null_ptr\n";
  s += "            return\n";
  s += "        end if\n";
  return s;
}

} // namespace codegen
