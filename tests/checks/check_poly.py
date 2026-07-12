import sys

sys.path.insert(0, ".")
import poly

failures = []


def check(name, cond):
    print(("PASS" if cond else "FAIL"), name)
    if not cond:
        failures.append(name)


s = poly.Shape_t()
c = poly.Circle_t()

# free function over class(base): the concrete actual carries the dynamic type
check("free fn class(base): base", abs(poly.area_of(s) - 1.0) < 1e-12)
check("free fn class(base): derived", abs(poly.area_of(c) - 6.0) < 1e-12)

# free function over class(*)
check("free fn class(*): base", poly.type_code(s) == 1)
check("free fn class(*): derived", poly.type_code(c) == 2)

# type-bound procedure: self dispatches through the per-class dispatcher row;
# circle_t does not override whoami, so this exercises the inherited binding
# observing the dynamic type
check("tbp self: base", s.whoami() == 1)
check("tbp self: derived (inherited binding)", c.whoami() == 2)

# two polymorphic args (self + other): all four specifics reachable
check("tbp meet: base/base", s.meet(s) == 11)
check("tbp meet: base/derived", s.meet(c) == 12)
check("tbp meet: derived/base", c.meet(s) == 21)
check("tbp meet: derived/derived", c.meet(c) == 22)

# unknown dynamic type -> dispatcher TypeError
for name, call in [
    ("class(base) arg", lambda: poly.area_of(3)),
    ("class(*) arg", lambda: poly.type_code(3.5)),
    ("tbp arg", lambda: c.meet(42)),
]:
    try:
        call()
        check(f"TypeError on unknown type ({name})", False)
    except TypeError as e:
        check(
            f"TypeError on unknown type ({name})",
            "unexpected argument type" in str(e),
        )

# only the dispatchers are exposed; per-type specifics stay internal
check("specifics not exposed", not hasattr(poly, "area_of__circle_t"))

print("---")
if failures:
    print("FAILURES:", failures)
    sys.exit(1)
print("all instantiate-directive tests passed")
