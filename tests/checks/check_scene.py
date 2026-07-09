import gc
import sys

sys.path.insert(0, ".")

failures = []


def check(name, cond):
    print(("PASS" if cond else "FAIL"), name)
    if not cond:
        failures.append(name)


# --- (a) null guard: scene imported alone, geom not initialized ---
import scene

try:
    scene.origin_x(object())
    check("null guard on function arg", False)
except RuntimeError as e:
    check("null guard on function arg", "not initialized" in str(e))

try:
    scene.Scene_t(id=1, origin=object())
    check("null guard on init kwarg", False)
except RuntimeError as e:
    check("null guard on init kwarg", "not initialized" in str(e))

# --- (b) initialize the producer, everything works ---
import geom

p = geom.Point_t()
p.x = 7.5

s2 = scene.Scene_t(id=3, origin=p)  # init kwarg with foreign type
check("init foreign kwarg", s2.id == 3 and s2.origin.x == 7.5)

# view semantics across modules
s2.origin.y = 42.0
check("mutation through foreign view", s2.origin.y == 42.0)

v = s2.origin
check("foreign view is a Point_t", isinstance(v, geom.Point_t))
del s2
gc.collect()
check("foreign view keeps parent alive", v.x == 7.5 and v.y == 42.0)
del v
gc.collect()

# setter: copy-in + type error from converter
s3 = scene.Scene_t(id=1, origin=geom.Point_t())
src = geom.Point_t()
src.x = 99.0
s3.origin = src
src.x = 0.0
check("foreign setter deep copy", s3.origin.x == 99.0)

try:
    s3.origin = 5
    check("foreign setter type error", False)
except TypeError as e:
    check("foreign setter type error", "expected a Point_t instance" in str(e))

try:
    scene.Scene_t(id=1, origin="nope")
    check("foreign init kwarg type error", False)
except TypeError as e:
    check("foreign init kwarg type error", "expected a Point_t instance" in str(e))

# self-assignment via a view of the same field
s3.origin = s3.origin
check("foreign self-assignment safe", s3.origin.x == 99.0)

# module function with foreign arg, incl. passing a view
check("foreign function arg", scene.origin_x(src) == 0.0)
check("foreign function arg (view)", scene.origin_x(s3.origin) == 99.0)
try:
    scene.origin_x(1)
    check("foreign function arg type error", False)
except TypeError as e:
    check("foreign function arg type error", "expected a Point_t instance" in str(e))

print("---")
if failures:
    print("FAILURES:", failures)
    sys.exit(1)
print("all cross-module tests passed")
