#include "modvars.hpp"

#include <fmt/format.h>

#include "flu/diagnostics.hpp"
#include "flu/symbols.hpp"
#include "flu/types.hpp"
#include "pytypes.hpp"

namespace codegen {

using namespace Fortran;
using Fortran::common::TypeCategory;

namespace {

// How a module variable is exposed (Skip carries a warning already emitted).
enum class var_kind { Scalar, DerivedLocal, DerivedForeign, Array, CharArray, Skip };

struct var_plan_t {
  var_kind kind = var_kind::Skip;
  semantics::Symbol const *sym = nullptr;
  semantics::DeclTypeSpec const *type = nullptr;
  semantics::Symbol const *tsym = nullptr; // derived: defining type symbol
  int rank = 0;
  std::int64_t char_len = 0; // CharArray only
};

var_plan_t plan_var(semantics::Symbol const &v, module_info_t const &m,
                    ext_types_t &ext_types) {
  var_plan_t p;
  p.sym = &v;
  auto const skip = [&](str_t const &why) {
    flu::emit_warning(v, "flair-f2py: module variable '" + v.name().ToString() +
                             "' is not exposed: " + why);
    return p;
  };

  p.type = v.GetType();
  if (p.type == nullptr)
    return p;
  p.rank = flu::rank_of(v);

  if (("flr_loc_" + v.name().ToString()).size() > 63)
    return skip("name too long for the generated helper");

  if (auto const c = classify_dtype(*p.type, m);
      c.cls != dtype_class::NotDerived) {
    if (p.rank != 0 || flu::is_pointer(v) || flu::is_allocatable(v))
      return skip("only inline scalar derived-type variables are supported");
    if (c.cls == dtype_class::Local) {
      p.kind = var_kind::DerivedLocal;
      p.tsym = c.sym;
    } else if (c.cls == dtype_class::Foreign &&
               note_ext_type(ext_types, *c.sym)) {
      p.kind = var_kind::DerivedForeign;
      p.tsym = c.sym;
    } else {
      return skip("its derived type is not wrapped");
    }
    return p;
  }

  if (!intrinsic_supported(*p.type))
    return skip("unsupported type");

  if (p.rank == 0) {
    p.kind = var_kind::Scalar; // pointer/allocatable handled with guards
    return p;
  }

  if (flu::is_pointer(v) || flu::is_allocatable(v))
    return skip("a pointer/allocatable array may be reallocated under a live "
                "view");
  if (flu::category(*p.type) == TypeCategory::Character) {
    auto const cl = flu::char_len(*p.type);
    if (!cl)
      return skip("character element length is not a constant");
    p.kind = var_kind::CharArray;
    p.char_len = *cl;
    return p;
  }
  if (!array_supported(*p.type))
    return skip("unsupported array element type");
  p.kind = var_kind::Array;
  return p;
}

// c_loc needs TARGET, which the wrapped module's variables usually lack: a
// helper with a TARGET dummy takes the address instead (scalars pass by
// reference, contiguous arrays associate without a copy, so it is stable).
str_t loc_helper(var_plan_t const &p) {
  str_t const nm = p.sym->name().ToString();
  str_t dummy;
  switch (p.kind) {
  case var_kind::DerivedLocal:
  case var_kind::DerivedForeign:
    dummy = fmt::format("type({}), target, intent(in) :: x", tname(*p.tsym));
    break;
  case var_kind::Array:
    // Sequence association: any shape (and rank) lands on x(1)'s base address.
    dummy = ftype(*p.type) + ", target, intent(in) :: x(1)";
    break;
  case var_kind::CharArray:
    dummy = "character(kind=c_char, len=1), target, intent(in) :: x(1)";
    break;
  default:
    return "";
  }
  str_t s;
  s += fmt::format("    function flr_loc_{}(x) result(r)\n", nm);
  s += fmt::format("        {}\n", dummy);
  s += "        type(c_ptr) :: r\n";
  s += "        r = c_loc(x)\n";
  s += "    end function\n\n";
  return s;
}

} // namespace

str_t gen_module_vars(module_info_t const &m, string_pool_t &strings,
                      str_t *fills, int &n, ext_types_t &ext_types,
                      str_t &init_decls, str_t &init_creates) {
  std::vector<var_plan_t> plans;
  for (sym_ptr_t v : m.variables) {
    if (v == nullptr)
      continue;
    var_plan_t const p = plan_var(*v, m, ext_types);
    if (p.kind != var_kind::Skip)
      plans.push_back(p);
  }
  if (plans.empty())
    return "";

  str_t procedures, getattr_cases;
  int max_rank = 0;
  bool any_attr = false;

  for (var_plan_t const &p : plans) {
    str_t const nm = p.sym->name().ToString();

    if (p.kind == var_kind::Scalar) {
      // Guard pointer/allocatable scalars; an absent value reads as None.
      str_t guard;
      if (flu::is_allocatable(*p.sym))
        guard = fmt::format("allocated({})", nm);
      else if (flu::is_pointer(*p.sym))
        guard = fmt::format("associated({})", nm);
      getattr_cases += fmt::format("        if (c_string_eq(cs, \"{}\")) then\n", nm);
      if (guard.empty()) {
        getattr_cases += fmt::format("            r = {}\n", to_py(*p.type, nm));
      } else {
        getattr_cases += fmt::format("            if ({}) then\n", guard);
        getattr_cases +=
            fmt::format("                r = {}\n", to_py(*p.type, nm));
        getattr_cases += "            else\n";
        getattr_cases +=
            "                r = Py_GetConstant(Py_CONSTANT_NONE)\n";
        getattr_cases += "            end if\n";
      }
      getattr_cases += "            return\n        end if\n";
      continue;
    }

    procedures += loc_helper(p);
    any_attr = true;
    str_t const s_nm = strings.intern(nm);
    str_t create;
    create += fmt::format("        ! --- module variable {} ---\n", nm);

    switch (p.kind) {
    case var_kind::DerivedLocal: {
      str_t const tn = tname(*p.tsym);
      init_decls += fmt::format("        type({}), pointer :: flr_v_{}\n",
                                obj_struct, nm);
      create += fmt::format(
          "        flr_attr = PyType_GenericAlloc(py_{}_type_obj, "
          "0_c_ptrdiff_t)\n",
          tn);
      create += "        if (.not. c_associated(flr_attr)) then\n";
      create += "            call PyErr_Clear()\n";
      create += "        else\n";
      create += fmt::format("            call c_f_pointer(flr_attr, "
                            "flr_v_{})\n",
                            nm);
      create += fmt::format("            flr_v_{}%data = flr_loc_{}({})\n", nm,
                            nm, nm);
      create += fmt::format("            flr_v_{}%owner = mod_ptr\n", nm);
      create += "            call Py_IncRef(mod_ptr)\n";
      create += fmt::format("            rc = PyModule_AddObjectRef(mod_ptr, "
                            "c_loc({}), flr_attr)\n",
                            s_nm);
      create += "            call Py_DecRef(flr_attr)\n";
      create += "        end if\n";
      break;
    }
    case var_kind::DerivedForeign: {
      create += fmt::format("        flr_attr = {}(flr_loc_{}({}), mod_ptr)\n",
                            view_pyobject_fn(tname(*p.tsym)), nm, nm);
      create += "        if (.not. c_associated(flr_attr)) then\n";
      create += "            ! producer module not imported yet: skip the "
                "attribute\n";
      create += "            call PyErr_Clear()\n";
      create += "        else\n";
      create += fmt::format("            rc = PyModule_AddObjectRef(mod_ptr, "
                            "c_loc({}), flr_attr)\n",
                            s_nm);
      create += "            call Py_DecRef(flr_attr)\n";
      create += "        end if\n";
      break;
    }
    case var_kind::Array:
    case var_kind::CharArray: {
      max_rank = std::max(max_rank, p.rank);
      create += fmt::format(
          "        flr_dims(1:{0}) = int(shape({1}), c_ptrdiff_t)\n", p.rank,
          nm);
      if (p.kind == var_kind::Array)
        create += fmt::format(
            "        flr_attr = PyArray_NewFromDescr(PyArray_Type_ptr, "
            "PyArray_DescrFromType({0}), {1}_c_int, c_loc(flr_dims), "
            "c_null_ptr, flr_loc_{2}({2}), NPY_ARRAY_F_CONTIGUOUS + "
            "NPY_ARRAY_BEHAVED, c_null_ptr)\n",
            npy(*p.type), p.rank, nm);
      else
        create += fmt::format(
            "        flr_attr = PyArray_New(PyArray_Type_ptr, {0}_c_int, "
            "c_loc(flr_dims), NPY_STRING, c_null_ptr, flr_loc_{1}({1}), "
            "{2}_c_int, NPY_ARRAY_F_CONTIGUOUS + NPY_ARRAY_BEHAVED, "
            "c_null_ptr)\n",
            p.rank, nm, p.char_len);
      create += "        if (.not. c_associated(flr_attr)) then\n";
      create += "            call PyErr_Clear()\n";
      create += "        else\n";
      create += fmt::format("            rc = PyModule_AddObjectRef(mod_ptr, "
                            "c_loc({}), flr_attr)\n",
                            s_nm);
      create += "            call Py_DecRef(flr_attr)\n";
      create += "        end if\n";
      break;
    }
    default:
      break;
    }
    init_creates += create;
  }

  if (any_attr)
    init_decls += "        type(c_ptr) :: flr_attr\n";
  if (max_rank > 0)
    init_decls += fmt::format(
        "        integer(c_ptrdiff_t), target :: flr_dims({})\n", max_rank);

  if (!getattr_cases.empty()) {
    procedures += "    ! ===== module __getattr__: live intrinsic-scalar "
                  "module variables (PEP 562) =====\n";
    // name="" suppresses the binding label: the function is only reached via
    // c_funloc, and a global `py_mod_getattr` symbol would collide when
    // several wrappers are linked into one combined package extension.
    procedures += "    function py_mod_getattr(self, obj) bind(C, name=\"\") "
                  "result(r)\n";
    procedures += "        type(c_ptr), value :: self, obj\n";
    procedures += "        type(c_ptr) :: r\n";
    procedures += "        type(c_ptr) :: cs\n";
    procedures += "        integer(c_ptrdiff_t) :: csn\n";
    procedures += "        r = c_null_ptr\n";
    procedures += "        cs = PyUnicode_AsUTF8AndSize(obj, csn)\n";
    procedures += "        if (.not. c_associated(cs)) return\n";
    procedures += getattr_cases;
    procedures +=
        "        call PyErr_SetObject(PyExc_AttributeError, obj)\n";
    procedures += "    end function\n\n";
    if (fills != nullptr)
      *fills += method_row("module_methods", ++n,
                           strings.intern("__getattr__"), "py_mod_getattr",
                           "METH_O");
  }

  return procedures;
}

} // namespace codegen
