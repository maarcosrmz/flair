"""!flair$ directives: ignore, instantiate, abort-on-unwrappable-procedure.

The abort fixtures define the same module abort_mod (python module: abort);
one has the offending subroutine annotated with !flair$ ignore, one does not.
poly_mod / poly_bad cover the instantiate directive on polymorphic (class)
arguments and its diagnostics.
"""

import re


def test_unwrappable_procedure_aborts(builder):
    builder.add_sources("abort_mod.f90")
    builder.compile("abort_mod.f90")

    proc = builder.flair("abort_mod.f90", expect_error=True)
    assert "Generated" not in proc.stdout
    assert "cannot wrap scalar argument 'y'" in proc.stderr
    assert "!flair$ ignore" in proc.stderr  # actionable hint
    assert builder.generated_missing("abort")


def test_ignore_directive_skips_procedure(builder):
    builder.add_sources("abort_mod_ignored.f90")
    builder.compile("abort_mod_ignored.f90")

    proc = builder.flair("abort_mod_ignored.f90")
    assert "Generated py_abort.F90" in proc.stdout
    assert proc.stderr == ""

    src = builder.generated("abort")
    assert re.search(r"\bgood\b", src)
    assert not re.search(r"py_mod_bad\b", src)

    builder.link_lib("case", "abort_mod_ignored.o")
    builder.extension("abort", "case")
    builder.run_check("check_directives.py")


def test_instantiate_directive(builder):
    b = builder.build(sources=["poly_mod.f90"])
    src = b.generated("poly")

    # the dispatcher owns the function's exposed name; the per-type specifics
    # exist alongside and are classified by runtime tp_name
    assert "function py_mod_area_of(self, args, kwds)" in src
    assert "py_mod_area_of__shape_t" in src
    assert "py_mod_area_of__circle_t" in src
    assert 'c_string_eq(pytype%tp_name, "poly.Circle_t")' in src
    assert "unexpected argument type for area_of" in src

    # class(*) dispatches the same way
    assert "function py_mod_type_code(self, args, kwds)" in src

    # the TBP dispatcher is registered in the derived type's method table too
    # (whoami is declared on shape_t only; Python classes do not inherit)
    assert re.search(
        r"FLAIR_set_method\(circle_t_methods, \d+, [^,]+, "
        r"c_funloc\(py_shape_t_whoami\)",
        src,
    )

    # two polymorphic args (self + other) -> cartesian product of specifics
    assert src.count("function py_shape_t_meet__") == 4

    b.run_check("check_poly.py")


def test_instantiate_diagnostics(builder):
    builder.add_sources("poly_bad.f90")
    builder.compile("poly_bad.f90")

    proc = builder.flair("poly_bad.f90", expect_error=True)
    assert "Generated" not in proc.stdout
    assert "no polymorphic (class) dummy argument" in proc.stderr
    assert "'nosuch_t' is not a wrapped derived type" in proc.stderr
    assert "'pb_other_t' does not extend 'pb_base_t'" in proc.stderr
    assert builder.generated_missing("poly_bad")
