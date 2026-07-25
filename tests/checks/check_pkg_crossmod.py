import sys

sys.path.insert(0, ".")
import proj  # single combined extension

# submodules are module attributes and registered in sys.modules, so both
# attribute access and from-imports work
assert sys.modules["proj.vec"] is proj.vec
assert sys.modules["proj.ops"] is proj.ops
from proj.vec import Vec2

p = Vec2()
p.x = 1.0
p.y = 2.0

proj.ops.translate(p, 10.0, 20.0)  # cross-module type, one .so
assert (p.x, p.y) == (11.0, 22.0), (p.x, p.y)

p.scale(2.0)  # vec's own method still works
assert (p.x, p.y) == (22.0, 44.0), (p.x, p.y)

# bad scalar arg fails fast: TypeError before translate runs, p unmutated
try:
    proj.ops.translate(p, "x", 1.0)
    raise AssertionError("translate with bad scalar did not raise")
except TypeError:
    pass
assert (p.x, p.y) == (22.0, 44.0), (p.x, p.y)

proj.ops.describe(p)  # generic dispatch -> describe_vec
proj.ops.describe(7)  # generic dispatch -> describe_int

# scalar dispatch across all four probe kinds; bool must win over int
assert proj.ops.tagof(7) == 1, proj.ops.tagof(7)
assert proj.ops.tagof(1.5) == 2, proj.ops.tagof(1.5)
assert proj.ops.tagof(True) == 3, proj.ops.tagof(True)
assert proj.ops.tagof("x") == 4, proj.ops.tagof("x")

print("OK")
