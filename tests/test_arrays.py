"""NumPy array arguments: intent(in)/inout/out, rank 1 and 2, dtype mapping,
and writeback-if-copy semantics (case: arrays)."""

import re


def test_arrays(builder):
    b = builder.build(sources=["arrays_mod.f90"])

    src = b.generated("arrays")

    # acquisition and release are single runtime calls; the coercion flags and
    # the resolve-before-decref ordering live in the runtime
    acquire = re.findall(r"FLAIR_array_from_PyObject\(objs\(\d+\), (\w+), "
                         r"(\d+)_c_int, (\.true\.|\.false\.), shp\d+\)", src)
    assert acquire, "expected FLAIR_array_from_PyObject acquisitions"
    assert "call FLAIR_array_release(arr0)" in src

    # mutating intents ask for writeback, read-only intent(in) ones do not
    assert any(wb == ".true." for _, _, wb in acquire)
    assert any(wb == ".false." for _, _, wb in acquire)

    # dtype mapping
    dtypes = {d for d, _, _ in acquire}
    assert {"NPY_FLOAT64", "NPY_INT32", "NPY_INT64", "NPY_COMPLEX128"} <= dtypes

    # rank reaches the runtime, which fills the shape
    assert any(rank == "2" for _, rank, _ in acquire)
    assert "call c_f_pointer(PyArray_DATA(arr0), v0, shp0)" in src

    b.run_check("check_arrays.py")
