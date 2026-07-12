"""NumPy array arguments: intent(in)/inout/out, rank 1 and 2, dtype mapping,
and writeback-if-copy semantics (case: arrays)."""

import re


def test_arrays(builder):
    b = builder.build(sources=["arrays_mod.f90"])

    src = b.generated("arrays")

    # mutating intents request writeback and resolve it in cleanup
    assert "NPY_ARRAY_WRITEBACKIFCOPY" in src
    assert "PyArray_ResolveWritebackIfCopy" in src

    # read-only intent(in) paths must NOT request writeback
    readonly = re.findall(
        r"PyArray_FromAny\(.*NPY_ARRAY_F_CONTIGUOUS, c_null_ptr\)", src
    )
    assert readonly, "expected read-only PyArray_FromAny conversions"

    # dtype mapping
    assert "NPY_FLOAT64" in src
    assert "NPY_INT32" in src
    assert "NPY_INT64" in src
    assert "NPY_COMPLEX128" in src

    # rank-2 shape wiring
    assert "shp0(2) = PyArray_DIM(arr0, 1_c_int)" in src

    b.run_check("check_arrays.py")
