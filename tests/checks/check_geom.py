import gc
import sys

import numpy as np

sys.path.insert(0, ".")
import geom

failures = []


def check(name, cond):
    print(("PASS" if cond else "FAIL"), name)
    if not cond:
        failures.append(name)


# --- default-new type with defaults ---
p = geom.Point_t()
check("point defaults", p.x == 1.0 and p.y == 2.0)

# --- ctor case with derived kwargs ---
a = geom.Point_t()
a.x = 10.0
b = geom.Point_t()
b.x = 20.0
s = geom.Segment_t(id=7, a=a, b=b)
check("ctor derived kwargs (deep copy)", s.id == 7 and s.a.x == 10.0 and s.b.x == 20.0)

# ctor copies: mutating the original does not affect the segment
a.x = -1.0
check("ctor kwarg copied, not aliased", s.a.x == 10.0)

# --- view semantics: mutation through the view is visible on re-get ---
s.a.y = 42.0
check("mutation through view visible", s.a.y == 42.0)

v = s.a
v.x = 11.0
check("held view mutates parent", s.a.x == 11.0)

# --- keep-alive: view outlives parent ---
ref = sys.getrefcount(s)
v2 = s.b
check("view holds parent ref", sys.getrefcount(s) == ref + 1)
del s
gc.collect()
check("view alive after parent deleted", v2.x == 20.0)
del v, v2, ref
gc.collect()

# --- setter: copy-in, deep copy independence ---
s2 = geom.Segment_t(id=1, a=geom.Point_t(), b=geom.Point_t())
src = geom.Point_t()
src.x = 99.0
s2.a = src
check("setter copies value", s2.a.x == 99.0)
src.x = 0.0
check("setter deep copy independent", s2.a.x == 99.0)

# --- setter type error ---
try:
    s2.a = 3
    check("setter type error", False)
except TypeError as e:
    check("setter type error", "must be a Point_t instance" in str(e))

# --- scalar setter type error leaves the field untouched ---
pv = geom.Point_t()
pv.x = 5.0
try:
    pv.x = "east"
    check("scalar setter type error", False)
except TypeError:
    check("scalar setter type error", True)
check("failed scalar set leaves field", pv.x == 5.0)

# --- character field: raw declared length, padded round-trip ---
check("character field default", pv.name == "origin  ")
pv.name = "north"
check("character field round-trip", pv.name == "north   ")
try:
    pv.name = "northeast"  # 9 > len 8
    check("oversized string set rejected", False)
except ValueError:
    check("oversized string set rejected", True)
check("failed string set leaves field", pv.name == "north   ")
try:
    pv.name = 3
    check("character setter type error", False)
except TypeError:
    check("character setter type error", True)

# --- logical field: strict bool ---
check("logical field default", pv.visible is True)
pv.visible = False
check("logical field round-trip", pv.visible is False)
try:
    pv.visible = 1
    check("logical setter type error", False)
except TypeError:
    check("logical setter type error", True)
check("failed logical set leaves field", pv.visible is False)

# --- complex field: round-trip, failed set leaves the field ---
check("complex field default", pv.phase == 0j)
pv.phase = 2 - 3j
check("complex field round-trip", pv.phase == 2 - 3j)
try:
    pv.phase = "east"
    check("complex setter type error", False)
except TypeError:
    check("complex setter type error", True)
check("failed complex set leaves field", pv.phase == 2 - 3j)

# --- complex allocatable component: numpy-copy property ---
pv.modes = np.array([1j, 2, 3 - 1j], dtype=np.complex128)
check("complex array field round-trip",
      np.array_equal(pv.modes, [1j, 2, 3 - 1j]))
check("complex array field dtype", pv.modes.dtype == np.complex128)
# negative-stride source: exercises the byte->element stride math (16-byte
# elements) in the setter
rev = np.array([1 + 1j, 2 + 2j, 3 + 3j], dtype=np.complex128)[::-1]
pv.modes = rev
check("complex array field reversed stride",
      np.array_equal(pv.modes, [3 + 3j, 2 + 2j, 1 + 1j]))

# --- ctor kwarg type error ---
try:
    geom.Segment_t(id=1, a=1, b=geom.Point_t())
    check("ctor kwarg type error", False)
except TypeError as e:
    check("ctor kwarg type error", "must be a Point_t instance" in str(e))

# --- ctor intrinsic kwarg type error ---
try:
    geom.Segment_t(id="seven", a=geom.Point_t(), b=geom.Point_t())
    check("ctor intrinsic kwarg type error", False)
except TypeError:
    check("ctor intrinsic kwarg type error", True)

# --- self-assignment via a view of the same field (no corruption) ---
s2.a = s2.a
check("self-assignment safe", s2.a.x == 99.0)

# --- init case: box_t_init with derived dummy ---
box = geom.Box_t(corner=src, w=2.5, label="crate")
check("init derived kwarg", box.w == 2.5 and box.corner.x == 0.0)
check("init character kwarg", box.label == "crate" + " " * 11)

# --- ctor case: constructor specific returning the type by value ---
circle = geom.Circle_t(r=2.5)
check("value ctor", circle.r == 2.5)

# --- delete guard ---
try:
    del s2.a
    check("delete guard", False)
except TypeError:
    check("delete guard", True)

print("---")
if failures:
    print("FAILURES:", failures)
    sys.exit(1)
print("all tests passed")
