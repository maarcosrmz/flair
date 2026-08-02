"""Type extension: an extending type carries the bindings and components it
inherits, including from a parent defined in another file.

The generated body for an inherited binding belongs to the extending type, not
to the parent's wrapper: it unwraps into a `type(<extending>), pointer`, so a
parent body that dispatches on its passed object still sees the true dynamic
type -- which is what plain Fortran does. That also means the wrappers compose
purely by symbol, never by binder call, so one file can be re-generated alone.
"""

import json
import re


def write_compdb(builder, entries) -> None:
    for e in entries:
        e.setdefault("directory", str(builder.tmp))
    (builder.tmp / "compile_commands.json").write_text(json.dumps(entries))


def assert_child_local_bodies(src: str) -> None:
    """Every inherited binding is wrapped against the extending type."""
    for tn, cls in [("derived_t", "derived_t"), ("leaf_t", "leaf_t")]:
        for binding in ["reduce", "scale", "bump"]:
            fn = re.search(
                rf"function py_{tn}_{binding}\(.*?end function|"
                rf"subroutine py_{tn}_{binding}\(.*?end subroutine",
                src,
                re.S,
            )
            assert fn, f"expected a wrapper for {tn}%{binding}"
            assert f"type({cls}), pointer :: p" in fn.group(0)
            assert f"p%{binding}(" in fn.group(0)

    # a private binding of the parent is not accessible through the child
    assert "secret" not in src

    # the binders never compose across files: the extending type's wrapper
    # installs its own rows, so nothing here refers to base_mod's binders
    assert "flair_bindm_base_mod_" not in src
    assert "flair_bindg_base_mod_" not in src
    assert "subroutine flair_bindm_ext_mod_derived_t(" in src
    assert "subroutine flair_bindm_ext_mod_leaf_t(" in src

    # tables are sized by the binder's counting pass rather than by codegen
    assert "type(PyMethodDef_t), allocatable, target, save :: leaf_t_methods(:)" in src
    assert "call flair_bindm_ext_mod_leaf_t(FLAIR_probe_methods, nb, 0_c_int)" in src
    assert "allocate(leaf_t_methods(nb + 1))" in src


def test_inheritance_across_files(builder):
    """Separate flair-f2py runs: ext_mod's wrapper recovers everything its
    types inherit from base_mod's symbols, resolved from base_mod.mod."""
    b = builder.build(sources=["base_mod.F90", "ext_mod.F90"])

    src = b.generated("ext")
    assert_child_local_bodies(src)

    # inherited components become properties of the extending type, while the
    # parent component stays available as the upcast view
    getset = re.search(
        r"subroutine flair_bindg_ext_mod_derived_t\b.*?end subroutine", src, re.S
    ).group(0)
    for field in ["tag", "weight", "extra", "base_t"]:
        assert f"c_funloc(py_derived_t_get_{field})" in getset

    b.run_check("check_inherit.py")


def test_inheritance_across_files_single_run(builder):
    """One invocation over both files: the parent resolves from source rather
    than from a .mod, and nothing about the emitted wrappers changes."""
    b = builder.build_single_run(sources=["base_mod.F90", "ext_mod.F90"])

    assert_child_local_bodies(b.generated("ext"))
    b.run_check("check_inherit.py")


def test_inheritance_rewrap_child_only(builder):
    """The requirement the binder protocol exists for: after wrapping a whole
    project, regenerating one file and relinking only its object keeps the
    extension working. The child's wrapper is self-contained, so nothing it
    inherited goes stale."""
    b = builder.build(sources=["base_mod.F90", "ext_mod.F90"])
    b.run_check("check_inherit.py")

    before = b.generated("ext")
    b.generated_path("ext").unlink()
    b.flair("ext_mod.F90")
    assert b.generated("ext") == before, "re-wrapping one file must be stable"

    # rebuild only that extension, against the untouched base.so
    b.extension("ext", "case", "base.so")
    b.run_check("check_inherit.py")


def test_inheritance_rewrap_parent_only(builder):
    """The other direction: regenerating and recompiling only the parent's
    wrapper leaves the child's tables alone, since they were never filled from
    the parent's translation unit."""
    b = builder.build(sources=["base_mod.F90", "ext_mod.F90"])

    b.generated_path("base").unlink()
    b.flair("base_mod.F90")
    b.extension("base", "case")

    b.run_check("check_inherit.py")


def test_inheritance_wrap_closure(builder):
    """Naming only the extending file still wraps the parent's module: the
    upcast property exchanges base_t across the API, so base_mod is promoted
    into the wrap set as a converter producer."""
    builder.add_sources("base_mod.F90", "ext_mod.F90")
    proc = builder.flair_all("base_mod.F90", "ext_mod.F90",
                             wrap=["ext_mod.F90"])

    assert not builder.generated_missing("base")
    assert "use py_base_mod, only: base_t_from_PyObject" in \
        builder.generated("ext")
    assert "generated separately, compiled before this one" not in proc.stderr


def test_inheritance_compdb_package(builder):
    """compdb mode reaches the same result through the combined package: the
    entry's parent module is discovered from the database and both wrappers
    land in one extension."""
    b = builder
    b.add_sources("base_mod.F90", "ext_mod.F90")
    write_compdb(b, [
        {"command": "gfortran -c -o ext_mod.o ext_mod.F90",
         "file": "ext_mod.F90"},
        {"command": "gfortran -c -o base_mod.o base_mod.F90",
         "file": "base_mod.F90"},
    ])

    b.flair_compdb("compile_commands.json", "ext_mod.F90", pkg="proj")
    assert_child_local_bodies(b.generated("ext"))

    # the parent's wrapper is compiled first: the child use-associates its
    # converters for the upcast property
    script = (b.tmp / "build_proj.sh").read_text()
    assert script.index("py_base.F90") < script.index("py_ext.F90")

    b.compile("base_mod.F90", "ext_mod.F90")
    b.run_build_script("proj", ["base_mod.o", "ext_mod.o"])
    b.run_check("check_inherit_pkg.py")
