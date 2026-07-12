import sys

sys.path.insert(0, ".")
import numpy as np

import state

failures = []


def check(name, cond):
    print(("PASS" if cond else "FAIL"), name)
    if not cond:
        failures.append(name)


# --- intrinsic scalars: live read-only values ---
check("real scalar", state.gravity == 9.81)
check("integer scalar", state.counter == 0)
check("character scalar", state.tag.strip() == "initial")
check("logical scalar", state.flag is True)
check("complex scalar", state.phase == 1 - 1j)

# --- derived variable: live view object ---
check("derived var read", state.config.scale == 1.0)
state.config.verbose = True
check("derived var write", state.config.verbose is True)

# --- array variables: writable views aliasing Fortran storage ---
grid = state.grid
check("array view dtype", grid.dtype == np.float64)
check("array view read", np.array_equal(grid, [1.0, 2.0, 3.0]))
grid[0] = 10.0
check("array view write reaches Fortran", state.grid_sum() == 15.0)

table = state.table
check("rank-2 view shape", table.shape == (2, 2))
check("rank-2 F order", table.flags.f_contiguous and table[1, 0] == 2)

labels = state.labels
check("character array dtype", labels.dtype == np.dtype("S6"))
check("character array read", labels[0] == b"alpha ")
labels[0] = b"gamma "
check("character array write reaches Fortran", state.first_label() == "gamma ")

# --- liveness: Fortran-side mutation is visible without re-fetching ---
state.bump()
check("scalar liveness", state.counter == 1)
check("derived liveness", state.config.scale == 2.0)
check("array liveness", grid[0] == 11.0)

# --- hidden / unknown attributes ---
try:
    state.hidden
    check("ignored var raises AttributeError", False)
except AttributeError:
    check("ignored var raises AttributeError", True)
try:
    state.no_such_thing
    check("unknown attr raises AttributeError", False)
except AttributeError:
    check("unknown attr raises AttributeError", True)
check("allocatable array not exposed", not hasattr(state, "dyn"))

print("---")
if failures:
    print("FAILURES:", failures)
    sys.exit(1)
print("all tests passed")
