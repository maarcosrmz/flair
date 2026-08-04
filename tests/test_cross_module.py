"""Cross-module derived types: foreign fields (scene), foreign args and
generic-interface dispatch (crossmod)."""


def test_scene_foreign_field(builder):
    """scene_t holds a point_t defined in another module; the wrapper must
    import the foreign type and use-associate the point_t_* converters
    from the geom wrapper module."""
    b = builder.build(sources=["geom.f90", "scene.f90"])

    # flair warns that the foreign type's wrapper is generated separately
    assert "generated separately, compiled before this one" in \
        b.flair_results["scene.f90"].stderr

    src = b.generated("scene")
    assert "use geom_mod, only: point_t" in src
    assert "use py_geom_mod, only: point_t_from_PyObject, " \
        "point_t_view_PyObject" in src
    # runtime null guard for using scene before importing geom
    assert "is not initialized" in src

    # the converters are module procedures of the producer wrapper
    g = b.generated("geom")
    assert g.index("function point_t_from_PyObject") \
        < g.index("end module py_geom_mod")
    assert g.rstrip().endswith("end module py_geom_mod")

    b.run_check("check_scene.py")


def test_crossmod_args_and_dispatch(builder):
    """ops_mod takes a vec2 from vec_mod by intent(inout) and overloads a
    generic interface over the foreign type and an intrinsic."""
    b = builder.build(sources=["vec_mod.F90", "ops_mod.F90"])

    src = b.generated("ops")
    assert "use py_vec_mod, only: vec2_from_PyObject" in src
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
    assert "use py_geom_mod, only: point_t_from_PyObject, " \
        "point_t_view_PyObject" in src

    b.run_check("check_scene.py")


def test_crossmod_args_and_dispatch_single_run(builder):
    """Single-invocation variant of the crossmod case: wrappers for both
    modules come out of one run and behave like the per-file builds."""
    b = builder.build_single_run(sources=["vec_mod.F90", "ops_mod.F90"])

    src = b.generated("ops")
    assert "use py_vec_mod, only: vec2_from_PyObject" in src
    assert "py_mod_describe_vec" in src
    assert "py_mod_describe_int" in src

    b.run_check("check_crossmod.py")


def test_crossmod_wrap_converter_closure(builder):
    """A wrap set naming only the consumer still gets its producer: ops_mod
    takes a vec2 across its API and use-associates that type's converters, so
    vec_mod is added to the wrap set and no separate generation is needed."""
    builder.add_sources("vec_mod.F90", "ops_mod.F90")
    proc = builder.flair_all("vec_mod.F90", "ops_mod.F90", wrap=["ops_mod.F90"])

    assert not builder.generated_missing("vec")
    assert "use py_vec_mod, only: vec2_from_PyObject" in \
        builder.generated("ops")
    assert "generated separately, compiled before this one" not in proc.stderr


def test_crossmod_wrap_closure_is_transitive(builder):
    """world_mod -> scene_mod -> geom_mod: promoting scene_mod exposes the
    point_t it embeds, so closing the wrap set takes a second round."""
    builder.add_sources("geom.f90", "scene.f90", "world.f90")
    proc = builder.flair_all("geom.f90", "scene.f90", "world.f90",
                             wrap=["world.f90"])

    assert not builder.generated_missing("scene")  # direct producer
    assert not builder.generated_missing("geom")   # producer of the producer
    assert "use py_scene_mod, only: scene_t_from_PyObject" in \
        builder.generated("world")
    assert "use py_geom_mod, only: point_t_from_PyObject" in \
        builder.generated("scene")
    assert "generated separately, compiled before this one" not in proc.stderr


def test_external_flag_skips_producer(builder):
    """Single mode cannot tell an external library from a producer wrapped by
    a separate run -- both resolve from a .mod -- so --external is how the
    caller says which. Named modules are excluded from the wrap set and their
    types skip the entities that carry them."""
    b = builder
    b.add_sources("vec_mod.F90", "ops_mod.F90")
    b.compile("vec_mod.F90")

    proc = b.flair("ops_mod.F90", external=["vec_mod"])

    assert "must be generated separately" not in proc.stderr
    assert "external module 'vec_mod'" in proc.stderr
    assert "'translate' is skipped" in proc.stderr

    ops = b.generated("ops")
    assert "py_vec_mod" not in ops
    assert "vec2_from_PyObject" not in ops
    # the untouched parts of the module are unaffected
    assert "py_mod_describe_int" in ops
    assert "py_mod_tagof_str" in ops


def test_modfile_producer_is_not_external_in_single_mode(builder):
    """The auto-detect must stay scoped to compdb mode: in single mode a
    producer resolved from a .mod is the normal cross-module workflow, so it
    keeps its converters and the separate-generation warning."""
    b = builder
    b.add_sources("vec_mod.F90", "ops_mod.F90")
    b.compile("vec_mod.F90")

    proc = b.flair("ops_mod.F90")

    assert "generated separately, compiled before this one" in proc.stderr
    assert "external module" not in proc.stderr
    assert "use py_vec_mod, only: vec2_from_PyObject" in b.generated("ops")
