"""Negative and diagnostic cases: skipped fields (warn only), unwrappable
constructors, intrinsic-module arguments, and string arguments (abort)."""

import re


def test_skipped_fields_warn_only(builder):
    """bad.f90: every unwrappable component warns and is skipped, but the
    module still wraps and works."""
    builder.add_sources("bad.f90")
    builder.compile("bad.f90")

    proc = builder.flair("bad.f90")
    assert "Generated py_bad.F90" in proc.stdout
    for comp in ("handle", "bad_field", "ptr_field", "alloc_field", "arr_field"):
        assert f"cannot expose component '{comp}'" in proc.stderr
    assert proc.stderr.count("property skipped") == 5

    src = builder.generated("bad")
    assert re.search(r"\bok_field\b", src)
    for comp in ("handle", "bad_field", "ptr_field", "alloc_field", "arr_field"):
        assert not re.search(rf"\b{comp}\b", src)
    # the private type is not wrapped either
    assert not re.search(r"\bhidden_t\b", src)

    builder.link_lib("case", "bad.o")
    builder.extension("bad", "case")
    builder.run_check("check_badfields.py")


def test_unwrappable_ctor_aborts(builder):
    builder.add_sources("badctor.f90")
    builder.compile("badctor.f90")

    proc = builder.flair("badctor.f90", expect_error=True)
    assert "cannot wrap derived type 'over_t'" in proc.stderr
    assert "cannot wrap derived type 'cls_t'" in proc.stderr
    assert "!flair$ ignore" in proc.stderr
    assert builder.generated_missing("badctor")


def test_intrinsic_module_arg_aborts(builder):
    builder.add_sources("cptr_arg_mod.f90")
    builder.compile("cptr_arg_mod.f90")

    proc = builder.flair("cptr_arg_mod.f90", expect_error=True)
    assert "cannot wrap argument 'h'" in proc.stderr
    assert "!flair$ ignore" in proc.stderr
    assert builder.generated_missing("cptr_arg")


def test_string_arg_aborts(builder):
    """Pins that character arguments are currently unwrappable: flair must
    abort instead of silently emitting broken marshalling code."""
    builder.add_sources("strarg_mod.f90")
    builder.compile("strarg_mod.f90")

    proc = builder.flair("strarg_mod.f90", expect_error=True)
    assert "cannot wrap argument 'name'" in proc.stderr
    assert "unsupported type or rank" in proc.stderr
    assert builder.generated_missing("strarg")
