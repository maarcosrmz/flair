import sys

sys.path.insert(0, ".")
import bad

failures = []


def check(name, cond):
    print(("PASS" if cond else "FAIL"), name)
    if not cond:
        failures.append(name)


check("private type hidden", not hasattr(bad, "Hidden_t"))

o = bad.Outer_t()
check("type with skipped fields constructible", isinstance(o, bad.Outer_t))
check("wrapped field works", o.ok_field.v == 0.0)

inner = bad.Inner_t()
inner.v = 1.5
o.ok_field = inner
check("wrapped field setter works", o.ok_field.v == 1.5)

for skipped in ("handle", "bad_field", "ptr_field", "alloc_field", "arr_field"):
    check(f"skipped field '{skipped}' absent", not hasattr(o, skipped))

print("---")
if failures:
    print("FAILURES:", failures)
    sys.exit(1)
print("all bad-field tests passed")
