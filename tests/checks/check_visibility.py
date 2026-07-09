import sys

sys.path.insert(0, ".")
import visibility

failures = []


def check(name, cond):
    print(("PASS" if cond else "FAIL"), name)
    if not cond:
        failures.append(name)


names = dir(visibility)
check("public function exposed", "pub_fn" in names)
check("public type exposed", "Pub_t" in names)
check("private function hidden", "priv_fn" not in names)
check("private type hidden", not any("priv" in n.lower() for n in names))
check("private helper hidden", "helper" not in names)

check("public function works", visibility.pub_fn(2.0) == 5.0)  # 2*x + 1
p = visibility.Pub_t()
check("public type default", p.val == 0.0)

print("---")
if failures:
    print("FAILURES:", failures)
    sys.exit(1)
print("all visibility tests passed")
