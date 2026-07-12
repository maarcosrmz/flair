import sys

import numpy as np

sys.path.insert(0, ".")
import generic

failures = []


def check(name, cond):
    print(("PASS" if cond else "FAIL"), name)
    if not cond:
        failures.append(name)


# scalar category dispatch; ints probe first even though floats accept ints,
# and floats before complex even though complex accepts both
check("int overload", generic.describe(3) == 1)
check("real overload", generic.describe(2.5) == 2)
check("complex overload", generic.describe(1 + 2j) == 7)

# derived-type dispatch via tp_name
check("derived overload", generic.describe(generic.Thing_t()) == 3)

# array dispatch on dtype + rank
check("float64 rank-1 overload", generic.describe(np.zeros(3)) == 4)
check("float64 rank-2 overload", generic.describe(np.zeros((2, 2))) == 5)
check("int32 rank-1 overload", generic.describe(np.zeros(3, dtype=np.int32)) == 6)

# kind-only overloads collapse to the widest kind
check("kind-collapse picks widest", generic.total(2**40, 1) == 2**40 + 1)

# single specific: unconditional forward
check("single-specific forward", generic.area(2.0) == 4.0)

# only the second argument discriminates
check("second-position dispatch (int)", generic.pick(1, 2) == 1)
check("second-position dispatch (real)", generic.pick(1, 2.5) == 2)

# only the generic names are exposed; specifics stay internal
check("specifics not exposed", not hasattr(generic, "describe_int"))
check("kind overloads not exposed", not hasattr(generic, "total_i8"))

# no overload matches -> TypeError with the dispatcher's message
try:
    generic.describe("nope")
    check("mismatch raises TypeError", False)
except TypeError as e:
    check("mismatch raises TypeError", "unexpected argument type" in str(e))

# plain lists do not dispatch through array overloads (needs a real ndarray)
try:
    generic.describe([1.0, 2.0])
    check("list does not dispatch as array", False)
except TypeError:
    check("list does not dispatch as array", True)

print("---")
if failures:
    print("FAILURES:", failures)
    sys.exit(1)
print("all generic-interface tests passed")
