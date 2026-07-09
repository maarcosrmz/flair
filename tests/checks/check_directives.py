import sys

sys.path.insert(0, ".")
import abort

failures = []


def check(name, cond):
    print(("PASS" if cond else "FAIL"), name)
    if not cond:
        failures.append(name)


check("kept procedure works", abs(abort.good(2.0) - 4.0) < 1e-6)
check("ignored procedure absent", not hasattr(abort, "bad"))

print("---")
if failures:
    print("FAILURES:", failures)
    sys.exit(1)
print("all directive tests passed")
