"""Cross-module derived types: foreign fields (scene), foreign args and
generic-interface dispatch (crossmod)."""


def test_scene_foreign_field(builder):
    """scene_t holds a point_t defined in another module; the wrapper must
    import the foreign type and route conversions through the external
    FLAIR_point_t_* converters exported by the geom extension."""
    b = builder.build(sources=["geom.f90", "scene.f90"])

    # flair warns that the foreign type's wrapper is generated separately
    assert "must be generated separately and linked" in \
        b.flair_results["scene.f90"].stderr

    src = b.generated("scene")
    assert "use geom_mod, only: point_t" in src
    assert "FLAIR_point_t_from_PyObject" in src
    assert "FLAIR_point_t_view_PyObject" in src
    # runtime null guard for using scene before importing geom
    assert "is not initialized" in src

    b.run_check("check_scene.py")


def test_crossmod_args_and_dispatch(builder):
    """ops_mod takes a vec2 from vec_mod by intent(inout) and overloads a
    generic interface over the foreign type and an intrinsic."""
    b = builder.build(sources=["vec_mod.F90", "ops_mod.F90"])

    src = b.generated("ops")
    assert "FLAIR_vec2_from_PyObject" in src
    # specific procedures wrapped as internal helpers plus one dispatcher
    assert "py_mod_describe_vec" in src
    assert "py_mod_describe_int" in src
    assert "unexpected argument type for describe" in src

    b.run_check("check_crossmod.py")
