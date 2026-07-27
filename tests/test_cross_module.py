"""Cross-module derived types: foreign fields (scene), foreign args and
generic-interface dispatch (crossmod)."""


def test_scene_foreign_field(builder):
    """scene_t holds a point_t defined in another module; the wrapper must
    import the foreign type and use-associate the FLAIR_point_t_* converters
    from the geom wrapper module."""
    b = builder.build(sources=["geom.f90", "scene.f90"])

    # flair warns that the foreign type's wrapper is generated separately
    assert "generated separately, compiled before this one" in \
        b.flair_results["scene.f90"].stderr

    src = b.generated("scene")
    assert "use geom_mod, only: point_t" in src
    assert "use py_geom_mod, only: FLAIR_point_t_from_PyObject, " \
        "FLAIR_point_t_view_PyObject" in src
    # runtime null guard for using scene before importing geom
    assert "is not initialized" in src

    # the converters are module procedures of the producer wrapper
    g = b.generated("geom")
    assert g.index("function FLAIR_point_t_from_PyObject") \
        < g.index("end module py_geom_mod")
    assert g.rstrip().endswith("end module py_geom_mod")

    b.run_check("check_scene.py")


def test_crossmod_args_and_dispatch(builder):
    """ops_mod takes a vec2 from vec_mod by intent(inout) and overloads a
    generic interface over the foreign type and an intrinsic."""
    b = builder.build(sources=["vec_mod.F90", "ops_mod.F90"])

    src = b.generated("ops")
    assert "use py_vec_mod, only: FLAIR_vec2_from_PyObject" in src
    # specific procedures wrapped as internal helpers plus one dispatcher
    assert "py_mod_describe_vec" in src
    assert "py_mod_describe_int" in src
    assert "unexpected argument type for describe" in src

    b.run_check("check_crossmod.py")


def test_scene_foreign_field_single_run(builder):
    """Both fixtures in one invocation: USE'd modules resolve from source (no
    .mod exists when flair-f2py runs), the cross-module converter wiring is
    unchanged, and the separate-generation warning is gone because the
    producer wrapper is emitted by the same run."""
    b = builder.build_single_run(sources=["geom.f90", "scene.f90"])

    assert "must be generated separately" not in \
        b.flair_results["scene.f90"].stderr

    src = b.generated("scene")
    assert "use geom_mod, only: point_t" in src
    assert "use py_geom_mod, only: FLAIR_point_t_from_PyObject, " \
        "FLAIR_point_t_view_PyObject" in src

    b.run_check("check_scene.py")


def test_crossmod_args_and_dispatch_single_run(builder):
    """Single-invocation variant of the crossmod case: wrappers for both
    modules come out of one run and behave like the per-file builds."""
    b = builder.build_single_run(sources=["vec_mod.F90", "ops_mod.F90"])

    src = b.generated("ops")
    assert "use py_vec_mod, only: FLAIR_vec2_from_PyObject" in src
    assert "py_mod_describe_vec" in src
    assert "py_mod_describe_int" in src

    b.run_check("check_crossmod.py")


def test_crossmod_wrap_filter(builder):
    """--wrap restricts wrapping to the designated inputs: dependencies are
    resolved for their symbols only, and the separate-generation warning
    stays because the producer wrapper is not part of this run."""
    builder.add_sources("vec_mod.F90", "ops_mod.F90")
    proc = builder.flair_all("vec_mod.F90", "ops_mod.F90", wrap=["ops_mod.F90"])

    assert builder.generated_missing("vec")
    assert "use py_vec_mod, only: FLAIR_vec2_from_PyObject" in \
        builder.generated("ops")
    assert "generated separately, compiled before this one" in proc.stderr
