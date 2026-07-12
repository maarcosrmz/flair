"""Optional dummy arguments and keyword arguments (case: optionals).

Optionals may be omitted (trailing), passed as None (any position), or given
by keyword; an absent optional is passed as a disassociated pointer (F2008
absent-actual semantics). All argument-taking wrappers accept keywords, with
CPython-style errors for missing/duplicate/unknown ones.
"""


def test_optionals(builder):
    b = builder.build(sources=["optionals_mod.f90"])

    src = b.generated("optionals")

    # arguments arrive positionally or by keyword
    assert "nargs = PyTuple_Size(args)" in src
    assert "PyDict_GetItemString(kwds" in src
    assert "METH_VARARGS + METH_KEYWORDS" in src

    # argument-error diagnostics
    assert "missing required argument 'a'" in src
    assert "given by name and position" in src
    assert "unexpected keyword argument" in src

    # absent optionals travel as disassociated pointers
    assert "xo1 => null()" in src
    assert "v1 => null()" in src

    b.run_check("check_optionals.py")
