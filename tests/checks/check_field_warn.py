import sys

sys.path.insert(0, ".")
import field

failures = []


def check(name, cond):
    print(("PASS" if cond else "FAIL"), name)
    if not cond:
        failures.append(name)


p = field.Point()
p.x = 3.0
p.y = 4.0
check("wrapped fields work", abs(field.point_norm(p) - 5.0) < 1e-6)
check("skipped field absent", not hasattr(p, "grid"))

print("---")
if failures:
    print("FAILURES:", failures)
    sys.exit(1)
print("all field-warn tests passed")
