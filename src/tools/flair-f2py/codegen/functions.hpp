#pragma once

#include <map>
#include <set>
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
// (Foreign and External only).
enum class dtype_class { NotDerived, Local, Foreign, External, Unsupported };
struct dtype_class_t {
  dtype_class cls;
  Fortran::semantics::Symbol const *sym = nullptr;
  str_t owner;
};

// Classify by symbol identity (robust against use-renames and case): Local if
// the defining symbol is one of `m`'s wrapped types; Unsupported for PDTs,
// intrinsic-module types (c_ptr & friends), and types defined in `m` but not
// wrapped (their converters would never be generated); External if the
// defining module is one no invocation can wrap (see note_external_modules);
// Foreign otherwise.
dtype_class_t classify_dtype(Fortran::semantics::DeclTypeSpec const &t,
                             module_info_t const &m);

// Record the modules whose types can never be converted, so classify_dtype
// reports them as External and every site that would reference a converter
// skips the entity instead. `names` are folded module names (--external);
// `auto_modfile` additionally treats any module read from a precompiled .mod
// as external, which is sound only in compdb mode, where every database entry
// is parsed from source. Must be called before the wrap set is closed over its
// converter producers: an external module must not be promoted into it.
void note_external_modules(std::set<str_t> names, bool auto_modfile);

// Concrete wrapped types substituted for polymorphic (class) dummies of an
// '!flair$ instantiate'd procedure: positional dummy index -> type symbol.
using poly_overrides_t = std::map<size_t, Fortran::semantics::Symbol const *>;

// Record which modules (and their wrapped types) this invocation generates
// wrappers for. note_ext_type consults this to skip the separate-generation
// warning when the producer of a Foreign type is emitted by the same run.
void note_run_modules(std::vector<module_info_t> const &modules);

// Record a Foreign type for import/interface emission. False (with a real
// diagnostic) when the folded name collides with a different type already
// recorded (the name-keyed converters would be ambiguous) or the
// converter name would exceed Fortran's 63-char identifier limit; warns once
// per type that its defining module must be wrapped and linked separately
// (unless that happens in this very run, see note_run_modules).
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
// unwrapped via the producer wrapper's `<t>_from_PyObject` converter (which
// isinstance-checks and sets the exception itself; a disassociated result
// signals failure) -- but only when `ext_types` is non-null, in which case the
// type is recorded so the module can emit the matching import + interface
// block. With `ext_types` null, a Foreign derived type is an error (procedure
// skipped).
//
// `owner_name` is the wrappable enclosing procedure (module function or method
// binding); it is named in the "add '!flair$ ignore ...'" hint of any error.
//
// A dummy listed in `overrides` is unwrapped as the given concrete wrapped
// type instead of its declared one, skipping classification entirely: this is
// how a class(base_t)/class(*) dummy of an instantiated procedure receives a
// `type(t), pointer` actual (and thereby its dynamic type).
//
// Emits into three streams, which the caller assembles with wrap_body():
// `decls` (declarations), `pre` (the presets that make the cleanup safe to
// run unconditionally) and `fetch` (the executable binding, which goes inside
// the failure block and whose checks `exit fetch`).
bool parse_args(std::vector<Fortran::semantics::Symbol *> const &dummies,
                module_info_t const &m, str_t const &owner_name, str_t &decls,
                str_t &pre, str_t &fetch, str_t &call_args,
                string_pool_t &strings, str_t *cleanup = nullptr,
                ext_types_t *ext_types = nullptr,
                poly_overrides_t const *overrides = nullptr);

// Shift every non-empty line of `s` right by one indentation level.
str_t indent_lines(str_t const &s);

// Assemble a wrapper body from the parse_args streams plus the call and its
// cleanup. Argument binding and the call run inside a named block so every
// failure path shares one cleanup: `r` is preset to `fail_value` and each
// check exits the block, which is what keeps an already-acquired array from
// leaking when a later argument fails.
str_t wrap_body(str_t const &decls, str_t const &pre, str_t const &fetch,
                str_t const &result, str_t const &cleanup,
                str_t const &fail_value);

// Set `r` from a Fortran call: function result (rt != null) -> to_py;
// subroutine -> None.
str_t build_result(Fortran::semantics::DeclTypeSpec const *rt,
                   str_t const &call_expr);

// Dummy args without the passed-object (first dummy).
std::vector<Fortran::semantics::Symbol *>
drop_self(std::vector<Fortran::semantics::Symbol *> const &dummies);

// One free module function. Appends its module-table row to `fills` (bumps
// `n`); a null `fills` generates the wrapper without exposing it. "" if
// skipped. `call_name`, if non-empty, overrides the Fortran procedure invoked
// in the body (the wrapper symbol is still named after `fn`): used for
// interface specifics, which call the public generic name so that resolution
// picks the specific from the typed actuals even when the specific itself is
// private. `wrapper_name`, if non-empty, overrides the wrapper symbol itself
// (per-type instantiate specifics); `overrides` is forwarded to parse_args.
str_t gen_module_function(Fortran::semantics::Symbol const &fn,
                          module_info_t const &m, string_pool_t &strings,
                          str_t *fills, int &n, ext_types_t &ext_types,
                          str_t const &call_name = {},
                          str_t const &wrapper_name = {},
                          poly_overrides_t const *overrides = nullptr);

} // namespace codegen
