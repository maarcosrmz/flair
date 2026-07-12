"""Derived-type wrapping: fields, constructors, views, keep-alive (case: geom).

geom.f90 covers the three constructor styles (default-new point_t, generic
interface segment_t -> make_segment, box_t_init subroutine), scalar and
rank-1-allocatable components, nested derived components, and a type-bound
procedure.
"""

import re


def test_geom(builder):
    b = builder.build(sources=["geom.f90"])

    # tool output contract
    assert "Generated py_geom.F90" in b.flair_results["geom.f90"].stdout

    src = b.generated("geom")

    # module skeleton
    assert "module py_geom_mod" in src
    assert "use python_api_mod" in src

    # scalar getsets and the numpy-copy property for the allocatable component
    assert "py_point_t_get_x" in src
    assert "py_point_t_get_y" in src
    assert "py_point_t_get_phase" in src
    assert "py_point_t_get_tags" in src
    assert "py_point_t_get_modes" in src
    # complex(8) elements are 16 bytes in the numpy-property stride math
    assert "NPY_COMPLEX128" in src
    assert "/ 16_c_ptrdiff_t" in src

    # keyword-only __init__: positional args rejected with TypeError
    assert "takes no positional arguments" in src

    # ctor paths: generic interface called by name; <type>_init subroutine
    assert re.search(r"p => segment_t\(id=", src)
    assert re.search(r"call box_t_init\(p,", src)

    # derived-type kwarg/setter type errors carry the class name
    assert "must be a Point_t instance" in src

    # type-bound procedure wrapped as a method
    assert re.search(r"\bdist_to\b", src)

    # runtime behavior in an isolated interpreter
    b.run_check("check_geom.py")
