"""!flair$ ignore directive and abort-on-unwrappable-procedure.

Both fixtures define the same module abort_mod (python module: abort); one
has the offending subroutine annotated with !flair$ ignore, one does not.
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
