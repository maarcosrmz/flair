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

# conversion failures fail fast with the converter's own exception
try:
    scalars.add_d(1.5, "x")
    check("real arg rejects str", False)
except TypeError:
    check("real arg rejects str", True)

try:
    scalars.addi(2, "x")
    check("integer arg rejects str", False)
except TypeError:
    check("integer arg rejects str", True)

try:
    scalars.big_id(2**80)
    check("oversized int overflows", False)
except OverflowError:
    check("oversized int overflows", True)

# complex(8): args and result map to Python complex
check("complex(8) function", scalars.cmul(1 + 2j, 3 + 4j) == (1 + 2j) * (3 + 4j))
check("complex result is complex", isinstance(scalars.cmul(1j, 1j), complex))
# PyComplex_*AsDouble semantics: float/int convert, imaginary part 0
check("complex arg accepts float", scalars.cmul(2.0, 3 + 1j) == 6 + 2j)
check("complex arg accepts int", scalars.cmul(2, 1j) == 2j)
check("complex(4) round-trip", abs(scalars.cconj4(1.5 + 0.25j) - (1.5 - 0.25j)) < 1e-6)

try:
    scalars.cmul(1j, "x")
    check("complex arg rejects str", False)
except TypeError:
    check("complex arg rejects str", True)

# logical: strict bool in, bool out
check("logical round-trip", scalars.toggle(True) is False)
check("logical(1) result", scalars.is_neg(-2.0) is True)

try:
    scalars.toggle(1)
    check("logical arg rejects int", False)
except TypeError:
    check("logical arg rejects int", True)

# character: assumed-length arg, deferred-length result
check("character round-trip", scalars.shout("hey") == "hey!")

try:
    scalars.shout(3)
    check("character arg rejects int", False)
except TypeError:
    check("character arg rejects int", True)

# explicit-length dummy: shorter pads, longer raises
check("character pads to dummy length", scalars.first3("ab") == "ab ")

try:
    scalars.first3("abcd")
    check("oversized string rejected", False)
except ValueError:
    check("oversized string rejected", True)

print("---")
if failures:
    print("FAILURES:", failures)
    sys.exit(1)
print("all scalar tests passed")
