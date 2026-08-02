"""Type extension across files: an extending type carries everything it
inherits, and an inherited body still observes the true dynamic type.

The reference values are what plain Fortran produces:

    type(derived_t) :: d
    d%reduce()   ! base_reduce -> 10 * self%scale() -> derived_scale -> 20

`main` is reused by check_inherit_pkg.py, which reaches the same two modules
as submodules of a combined package extension.
"""

import sys


def main(base, ext) -> int:
    failures = []

    def check(name, cond):
        print(("PASS" if cond else "FAIL"), name)
        if not cond:
            failures.append(name)

    b = base.Base_t()
    d = ext.Derived_t()
    lf = ext.Leaf_t()

    # --- the property that motivates generating the body against the
    # extending type: base_reduce dispatches on its class(base_t) passed
    # object, and the actual it receives must carry the dynamic type of the
    # Python instance.
    check("base: own binding", b.reduce() == 10)
    check("derived: inherited binding sees the override", d.reduce() == 20)
    check("leaf: inherited two levels up sees the override", lf.reduce() == 20)

    # --- overrides win over what they shadow, at every level
    check("derived: overrides scale", d.scale() == 2 and b.scale() == 1)
    check("leaf: inherits the nearer override", lf.scale() == 2)
    check("derived: own binding", d.total() == 10)
    check("leaf: overrides an inherited binding", lf.total() == 12)

    # --- an inherited binding taking arguments marshals them the same way
    d.bump(5)
    lf.bump(1)
    check("derived: inherited binding with arguments",
          d.tag == 12 and d.total() == 15)
    check("leaf: inherited two levels up, with arguments", lf.tag == 8)

    # --- inherited data components are properties of the extending type
    check("derived: inherited components", d.weight == 1.5 and d.extra == 3)
    check("leaf: inherited from both ancestors", lf.extra == 3 and lf.depth == 2)
    lf.weight = 4.25
    check("leaf: inherited component is writable", lf.weight == 4.25)

    # --- the parent-component property stays available as the upcast view,
    # and aliases the very same storage as the flattened properties
    u = d.base_t
    check("upcast view type", isinstance(u, base.Base_t))
    check("upcast view aliases the flattened component", u.tag == 12)
    u.tag = 30
    check("upcast view writes through", d.tag == 30 and d.total() == 33)
    # the view dispatches as what it actually is: a base_t
    check("upcast view dispatches as base", u.reduce() == 10)

    # --- accessibility is not widened by inheritance
    for name, obj in [("base", b), ("derived", d), ("leaf", lf)]:
        check(f"{name}: private binding stays unexposed",
              not hasattr(obj, "secret"))

    # --- the exposed surface is exactly the union
    check(
        "derived: attribute set",
        sorted(n for n in dir(d) if not n.startswith("_"))
        == ["base_t", "bump", "extra", "reduce", "scale", "tag", "total",
            "weight"],
    )

    print("---")
    if failures:
        print("FAILURES:", failures)
        return 1
    print("all inheritance tests passed")
    return 0


if __name__ == "__main__":
    sys.path.insert(0, ".")
    import base
    import ext

    sys.exit(main(base, ext))
