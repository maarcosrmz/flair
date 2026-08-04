import sys

sys.path.insert(0, ".")
import proj  # single combined extension, ops only

# vec_mod is external: it contributes no submodule to the package
assert not hasattr(proj, "vec"), dir(proj)
assert "proj.vec" not in sys.modules

# the procedures carrying vec2 are gone, the rest of the module is intact
assert not hasattr(proj.ops, "translate"), dir(proj.ops)
assert not hasattr(proj.ops, "describe_vec"), dir(proj.ops)

# the generic survives, dispatching only to the specific that remained
proj.ops.describe(7)
try:
    proj.ops.describe(1.5)  # no reachable specific for a real
    raise AssertionError("describe accepted an unhandled type")
except TypeError:
    pass

# an optional dummy of an external type costs only that argument: the
# procedure survives, called without it, and the remaining actuals still bind
# to the right dummies even though the dropped one sat between them
assert proj.ops.biased(1.0, 2.0) == 3.0, proj.ops.biased(1.0, 2.0)
assert proj.ops.biased(f=1.0, bias=0.5) == 1.5, proj.ops.biased(f=1.0, bias=0.5)
try:
    proj.ops.biased(1.0, 2.0, v=None)  # not in the argument table at all
    raise AssertionError("biased accepted the omitted external argument")
except TypeError:
    pass

assert proj.ops.tagof(7) == 1, proj.ops.tagof(7)
assert proj.ops.tagof(1.5) == 2, proj.ops.tagof(1.5)
assert proj.ops.tagof(True) == 3, proj.ops.tagof(True)
assert proj.ops.tagof("x") == 4, proj.ops.tagof("x")

print("OK")
