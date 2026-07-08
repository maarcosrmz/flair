#pragma once

#include <map>
#include <vector>

#include <flang/Semantics/symbol.h>

#include "../wdata.hpp"
#include "utils.hpp"

namespace Fortran::semantics {
class DeclTypeSpec;
}

namespace codegen {

// Derived types wrapped in other files that this module's wrappers reference:
// folded defining type name -> defining type symbol. Drives the `use ...,
// only:` imports and the external-converter interface blocks of the generated
// module.
using ext_types_t = std::map<str_t, Fortran::semantics::Symbol const *>;

// Classification of a declared type for wrapper codegen. `sym` is the defining
// type symbol (never a use-association proxy); `owner` its defining module
// (Foreign only).
enum class dtype_class { NotDerived, Local, Foreign, Unsupported };
struct dtype_class_t {
  dtype_class cls;
  Fortran::semantics::Symbol const *sym = nullptr;
  str_t owner;
};

// Classify by symbol identity (robust against use-renames and case): Local if
// the defining symbol is one of `m`'s wrapped types; Unsupported for PDTs,
// intrinsic-module types (c_ptr & friends), and types defined in `m` but not
// wrapped (their converters would never be generated); Foreign otherwise.
dtype_class_t classify_dtype(Fortran::semantics::DeclTypeSpec const &t,
                             module_info_t const &m);

// Record a Foreign type for import/interface emission. False (with a real
// diagnostic) when the folded name collides with a different type already
// recorded (the name-keyed FLAIR_* linker symbols would collide) or the
// converter name would exceed Fortran's 63-char identifier limit; warns once
// per type that its defining module must be wrapped and linked separately.
bool note_ext_type(ext_types_t &ext_types,
                   Fortran::semantics::Symbol const &tsym);

// Resolve each dummy from the positional `args` tuple, emitting locals into
// `decls`, fetch/convert statements into `fetch`, and the Fortran actuals into
// `call_args`. Post-call cleanup (e.g. Py_DecRef of coerced numpy arrays) is
// emitted into `cleanup` when non-null. Supports intrinsic scalar inputs,
// intent(in) intrinsic arrays, intent(out)/intent(inout) intrinsic arrays (with
// write-back), and wrapped derived-type inputs (isinstance-checked against the
// wrapped type, raising TypeError on mismatch); returns false (procedure
// skipped) for anything else. An intent(out)/intent(inout) intrinsic scalar
// cannot be written back and emits an error via `ctx`. Shared with method
// codegen.
//
// A derived-type argument whose type is wrapped in another file (Foreign) is
// unwrapped via the external `FLAIR_<t>_from_PyObject` converter (which
// isinstance-checks and sets the exception itself; a disassociated result
// signals failure) -- but only when `ext_types` is non-null, in which case the
// type is recorded so the module can emit the matching import + interface
// block. With `ext_types` null, a Foreign derived type is an error (procedure
// skipped).
bool parse_args(std::vector<Fortran::semantics::Symbol *> const &dummies,
                module_info_t const &m, str_t const &fail_return, str_t &decls,
                str_t &fetch, str_t &call_args, string_pool_t &strings,
                str_t *cleanup = nullptr, ext_types_t *ext_types = nullptr);

// Set `r` from a Fortran call: function result (rt != null) -> to_py;
// subroutine -> None.
str_t build_result(Fortran::semantics::DeclTypeSpec const *rt,
                   str_t const &call_expr);

// Dummy args without the passed-object (first dummy).
std::vector<Fortran::semantics::Symbol *>
drop_self(std::vector<Fortran::semantics::Symbol *> const &dummies);

// One free module function. Appends its module-table row to `fills` (bumps
// `n`). "" if skipped. `call_name`, if non-empty, overrides the Fortran
// procedure invoked in the body (the wrapper symbol is still named after `fn`):
// used for interface specifics, which call the public generic name so that
// resolution picks the specific from the typed actuals even when the specific
// itself is private.
str_t gen_module_function(Fortran::semantics::Symbol const &fn,
                          module_info_t const &m, string_pool_t &strings,
                          str_t *fills, int &n, ext_types_t &ext_types,
                          str_t const &call_name = {});

} // namespace codegen
