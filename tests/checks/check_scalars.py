import sys

sys.path.insert(0, ".")
import scalars

failures = []


def check(name, cond):
    print(("PASS" if cond else "FAIL"), name)
    if not cond:
        failures.append(name)


check("real(8) function", scalars.add_d(1.5, 2.25) == 3.75)
check("integer(4) function", scalars.addi(2, 3) == 5)
check("real(4) function", abs(scalars.half(1.0) - 0.5) < 1e-6)
check("integer(8) round-trip", scalars.big_id(2**40 + 7) == 2**40 + 7)
check("subroutine returns None", scalars.noop(3) is None)
check("no-arg function", scalars.answer() == 42)

try:
    scalars.add_d(1.0)
    check("missing argument raises", False)
except Exception:
    check("missing argument raises", True)

try:
    scalars.answer(1)
    check("METH_NOARGS rejects argument", False)
except TypeError:
    check("METH_NOARGS rejects argument", True)

print("---")
if failures:
    print("FAILURES:", failures)
    sys.exit(1)
print("all scalar tests passed")
