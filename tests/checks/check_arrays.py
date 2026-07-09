import sys

import numpy as np

sys.path.insert(0, ".")
import arrays

failures = []


def check(name, cond):
    print(("PASS" if cond else "FAIL"), name)
    if not cond:
        failures.append(name)


# intent(out): callee fills the caller's array
x = np.zeros(5, dtype=np.float64, order="F")
arrays.fill_iota(x)
check("intent(out) fill", np.array_equal(x, [1.0, 2.0, 3.0, 4.0, 5.0]))

# intent(inout), Fortran-ordered float64: zero-copy in-place mutation
y = np.asfortranarray([1.0, 2.0, 3.0], dtype=np.float64)
arrays.scale_inplace(y, 2.0)
check("inout zero-copy", np.array_equal(y, [2.0, 4.0, 6.0]))

# intent(inout), C-ordered rank-2 int32: forces a copy, writeback must
# propagate the result to the caller's array
m = np.array([[1, 2], [3, 4]], dtype=np.int32, order="C")
arrays.addk(m, 10)
check("inout writeback-if-copy", np.array_equal(m, [[11, 12], [13, 14]]))

# rank-2 orientation: Fortran m(1,2) is Python m[0,1] whatever the layout
mc = np.array([[1.0, 2.0], [3.0, 4.0]])
check("rank-2 orientation (C order)", arrays.corner12(mc) == 2.0)
mf = np.asfortranarray(mc)
check("rank-2 orientation (F order)", arrays.corner12(mf) == 2.0)

# intent(in) coercion: python list accepted
check("list coercion", arrays.sum1([1.0, 2.0, 3.5]) == 6.5)

# int64 input array
v = np.array([2**40, 1, 2], dtype=np.int64)
check("int64 array", arrays.isum(v) == 2**40 + 3)

# rank mismatch raises instead of crashing
try:
    arrays.sum1(np.zeros((2, 2)))
    check("rank mismatch raises", False)
except Exception:
    check("rank mismatch raises", True)

print("---")
if failures:
    print("FAILURES:", failures)
    sys.exit(1)
print("all array tests passed")
