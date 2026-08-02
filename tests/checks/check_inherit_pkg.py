"""Same inheritance behaviour, reached through a combined package extension:
both modules live in one .so as proj.base and proj.ext."""

import sys

sys.path.insert(0, ".")
import proj  # noqa: F401  (registers the submodules in sys.modules)
from proj import base, ext

import check_inherit

if __name__ == "__main__":
    assert base.Base_t.__module__ == "proj.base"
    assert ext.Leaf_t.__module__ == "proj.ext"
    sys.exit(check_inherit.main(base, ext))
