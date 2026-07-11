import sys

sys.path.insert(0, ".")
import ops
import vec

p = vec.Vec2()
p.x = 1.0
p.y = 2.0

ops.translate(p, 10.0, 20.0)  # in-place mutation across modules
assert (p.x, p.y) == (11.0, 22.0), (p.x, p.y)

p.scale(2.0)  # vec's own method still works
assert (p.x, p.y) == (22.0, 44.0), (p.x, p.y)

# bad scalar arg fails fast: TypeError before translate runs, p unmutated
try:
    ops.translate(p, "x", 1.0)
    raise AssertionError("translate with bad scalar did not raise")
except TypeError:
    pass
assert (p.x, p.y) == (22.0, 44.0), (p.x, p.y)

ops.describe(p)  # generic dispatch -> describe_vec
ops.describe(7)  # generic dispatch -> describe_int

print("OK")
