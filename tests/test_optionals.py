"""Optional dummy arguments and keyword arguments (case: optionals).

Optionals may be omitted (trailing), passed as None (any position), or given
by keyword; an absent optional is passed as a disassociated pointer (F2008
absent-actual semantics). All argument-taking wrappers accept keywords, with
CPython-style errors for missing/duplicate/unknown ones.
"""


def test_optionals(builder):
    b = builder.build(sources=["optionals_mod.f90"])

    src = b.generated("optionals")

    # binding positional/keyword arguments is one runtime call over the
    # dummy names; the diagnostics live in the runtime, not in the wrapper
    assert "argnames = [" in src
    assert "FLAIR_parse_args(args, kwds, argnames, argreq, objs)" in src
    assert "METH_VARARGS + METH_KEYWORDS" in src

    # required vs optional is a flag table, not per-argument code
    assert "argreq = [.true., .false., .false., .false.]" in src

    # every failure unwinds to the one cleanup after the block
    assert "fetch: block" in src
    assert "exit fetch" in src
    assert "end block fetch" in src

    # absent optionals travel as disassociated pointers
    assert "xo1 => null()" in src
    assert "v1 => null()" in src

    b.run_check("check_optionals.py")
