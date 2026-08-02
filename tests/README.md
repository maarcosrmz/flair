# flair-f2py regression tests

Every test case runs the full pipeline in an isolated temporary directory:

1. compile the fixture Fortran modules with flang,
2. run `flair-f2py` on them,
3. assert on the *content* of the generated `py_*.F90` (targeted
   substring/regex checks, not golden files),
4. compile and link the generated wrappers into importable extension modules,
5. assert on *runtime behavior* by executing a script from `checks/` in a
   fresh Python subprocess.

Behavior checks run in a subprocess on purpose: CPython extension modules
cannot be unloaded, several cases reuse module names, refcount-based
keep-alive checks need a pristine interpreter, and a crash in generated code
must fail one test instead of killing the whole pytest session.

## Requirements

- A built `flair-f2py`. Source and build trees are separated by convention:
  the repo is `flair.src` and the build tree is the sibling `flair.build`
  (configure with `cmake -S flair.src -B flair.build ...`); the default
  binary location is therefore `../flair.build/src/tools/flair-f2py`.
- LLVM **flang** from the *same toolchain* that flair targets (LLVM >= 23).
  This matters beyond compiler compatibility: the cross-module
  `<type>_from_PyObject` converters are module procedures of the
  producer's wrapper module, so consumer wrappers need its `.mod` file at
  compile time (same module-file format) and its mangled `_QM...` symbols at
  link time — all wrappers, wrapped sources, and the runtime must be compiled
  with the same flang, and separately generated wrappers must be compiled in
  dependency order.
- Python with `pytest` and `numpy` (e.g. `python -m venv .venv &&
  .venv/bin/pip install pytest numpy`).

If `flair-f2py`, flang, or numpy are missing, the suite *skips* with a
message naming the fix; it never silently passes.

## Running

```sh
# from the repo root (flair.src)
pytest tests/ -ra

# explicit toolchain override
FLAIR=$PWD/../flair.build/src/tools/flair-f2py FLANG=$HOME/.local/llvm/bin/flang pytest tests/ -ra

# via CTest (FLAIR is injected from the build tree)
cmake --build ../flair.build && ctest --test-dir ../flair.build -R flair.pytest --output-on-failure
```

Environment variables:

- `FLAIR` — path to the `flair-f2py` binary (default: the sibling
  `<repo>.build/src/tools/flair-f2py`, falling back to an in-tree `build/`).
- `FLANG` — path to flang (default: `~/.local/llvm/bin/flang`, then `PATH`).

The harness also passes `-fintrinsic-modules-path` (derived from the flang
location) to `flair-f2py`, since the tool does not locate flang's intrinsic
`.mod` files (`iso_c_binding`, ...) on its own.

## Layout

- `conftest.py` — toolchain discovery fixtures and `CaseBuilder`, which
  drives the canonical build recipe (compile sources in dependency order,
  link one shared library, generate wrappers, link each extension with
  `-Wl,-rpath,$ORIGIN`; cross-module extensions additionally link the
  earlier extension's `.so` to resolve the converter symbols).
- `fixtures/` — Fortran input modules, shared between cases.
- `checks/` — per-case runtime assertion scripts (run in a subprocess, exit
  nonzero on failure).
- `test_*.py` — one file per feature area; each test pairs content
  assertions with a behavior check.

## Coverage

| Test | Feature |
|---|---|
| `test_cli` | command-line surface: `--help` / `--version`, `-v` off by default, rejected flag combinations |
| `test_derived_types` | derived-type fields (incl. rank-1 allocatable as NumPy property), the three ctor styles (default new, generic interface, `<type>_init`), keyword-only `__init__`, view/deep-copy semantics, keep-alive |
| `test_cross_module` | foreign derived-type fields and args across extension modules, use-associated converters, "not initialized" guard, cross-module generic dispatch, transitive converter-producer closure of the wrap set |
| `test_inheritance` | `extends(...)` across files: inherited bindings and components flattened onto the extending type, overrides at every level, an inherited body observing the true dynamic type, private bindings staying unexposed, and re-generating either file on its own |
| `test_compdb` | compilation-database mode: USE-closure discovery from `--entry`, per-entry compile flags, the combined package extension and its generated build script, `--wrap` composition (explicit files, the `@entry` token, files outside the closure, promoted converter producers) |
| `test_interfaces` | generic-interface wrapping: dispatch on scalar category / derived type / array dtype+rank, kind-only overload collapse, single-specific forward, TypeError fallback |
| `test_arrays` | NumPy array args: intent in/inout/out, rank 1/2, dtype mapping, writeback-if-copy |
| `test_scalars` | numeric kinds, None returns, `METH_NOARGS` |
| `test_visibility` | only public symbols are wrapped |
| `test_directives` | `!flair$ ignore`, abort on unwrappable procedure with actionable hint; `!flair$ instantiate`: per-type wrappers + tp_name dispatch for `class(t)`/`class(*)` args of free functions and type-bound procedures (a dispatcher on the declaring type also lands in the tables of the types that inherit it), directive diagnostics |
| `test_field_warn` | unwrappable component warns + is skipped, type still wrapped |
| `test_negative` | skipped-field warnings, unwrappable ctors, intrinsic-module args, character args all abort/warn as specified |

## Adding a case

1. Add a fixture module under `fixtures/` (keep it minimal; unique module
   name unless deliberately reusing one — each test gets its own tmp dir).
2. Add a behavior script under `checks/` following the `check(name, cond)`
   pattern (print PASS/FAIL, exit 1 on any failure).
3. Add a test function pairing 3–6 distinctive content assertions on the
   generated file (crib them from a real run; avoid full diagnostic
   sentences) with `builder.run_check(...)`.
