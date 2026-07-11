"""Scalar arguments and results: numeric kinds, None returns, METH_NOARGS
(case: scalars)."""


def test_scalars(builder):
    b = builder.build(sources=["scalars_mod.f90"])

    src = b.generated("scalars")
    assert "PyFloat_FromDouble" in src
    assert "PyLong_FromLongLong" in src
    # arguments go through the checked converters and bail on failure
    assert "x0 = FLAIR_double_from_PyObject(a0, ok0)" in src
    assert "x0 = FLAIR_int64_from_PyObject(a0, ok0)" in src
    assert "x0 = FLAIR_logical_from_PyObject(a0, ok0)" in src
    assert "x0 = FLAIR_str_from_PyObject(a0, ok0)" in src
    assert "if (.not. ok0) then" in src
    # converter results are narrowed to the dummy's kind at the call
    assert "real(x0, 4)" in src
    assert "int(x0, 4)" in src
    # logical results go out as real bools
    assert "PyBool_FromLong" in src
    # explicit-length character dummy: fixed local + length guard
    assert "character(3) :: xf0" in src
    assert "PyErr_SetString(PyExc_ValueError" in src
    # subroutine wrapper returns None
    assert "Py_GetConstant(Py_CONSTANT_NONE)" in src
    # no-arg function uses METH_NOARGS, the rest METH_VARARGS
    assert "METH_NOARGS" in src
    assert "METH_VARARGS" in src

    b.run_check("check_scalars.py")
