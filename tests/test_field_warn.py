"""Unwrappable derived-type field warns and is skipped; the rest of the type
is still wrapped (case: field, rank-2 inline array component)."""

import re


def test_field_warn_not_abort(builder):
    b = builder.build(sources=["field_mod.f90"])

    proc = b.flair_results["field_mod.f90"]
    assert "Generated py_field.F90" in proc.stdout
    assert "cannot expose component 'grid'" in proc.stderr
    assert "property skipped" in proc.stderr

    src = b.generated("field")
    assert "py_point_get_x" in src
    assert "py_point_get_y" in src
    assert not re.search(r"\bgrid\b", src)
    assert re.search(r"\bpoint_norm\b", src)

    b.run_check("check_field_warn.py")
