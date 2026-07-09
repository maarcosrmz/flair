"""Scalar arguments and results: numeric kinds, None returns, METH_NOARGS
(case: scalars)."""


def test_scalars(builder):
    b = builder.build(sources=["scalars_mod.f90"])

    src = b.generated("scalars")
    assert "PyFloat_FromDouble" in src
    assert "PyLong_FromLongLong" in src
    # subroutine wrapper returns None
    assert "Py_GetConstant(Py_CONSTANT_NONE)" in src
    # no-arg function uses METH_NOARGS, the rest METH_VARARGS
    assert "METH_NOARGS" in src
    assert "METH_VARARGS" in src

    b.run_check("check_scalars.py")
