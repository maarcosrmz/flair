"""Compilation-database mode: USE-closure discovery from an entry point,
per-entry compile flags, foreign-flag tolerance, and the combined package
extension (one .so for the whole closure) it emits by default."""

import json


def write_compdb(builder, entries) -> None:
    for e in entries:
        e.setdefault("directory", str(builder.tmp))
    (builder.tmp / "compile_commands.json").write_text(json.dumps(entries))


def test_compdb_crossmod(builder):
    """The vec_mod dependency of ops_mod is discovered from the database
    (entries out of dependency order, "command" and "arguments" forms mixed,
    foreign gfortran flags dropped) and both modules come out of one run as
    submodules of a single combined extension."""
    b = builder
    b.add_sources("vec_mod.F90", "ops_mod.F90")
    write_compdb(b, [
        {"command": "gfortran -c -O2 -Wall -o ops_mod.o ops_mod.F90",
         "file": "ops_mod.F90"},
        {"arguments": ["gfortran", "-c", "-ffree-form", "-o", "vec_mod.o",
                       "vec_mod.F90"],
         "file": "vec_mod.F90"},
    ])

    proc = b.flair_compdb("compile_commands.json", "ops_mod.F90", pkg="proj")
    assert "must be generated separately" not in proc.stderr
    assert "use py_vec_mod, only: FLAIR_vec2_from_PyObject" in b.generated("ops")

    # the build script compiles wrappers dependency-first (consumer wrappers
    # use-associate the producer wrapper's converters), not in entry order
    script = (b.tmp / "build_proj.sh").read_text()
    assert script.index("py_vec.F90") < script.index("py_ops.F90")

    b.compile("vec_mod.F90", "ops_mod.F90")
    b.link_lib("case", "vec_mod.o", "ops_mod.o")
    b.package_extension("proj", ["vec", "ops"], "case")
    b.run_check("check_pkg_crossmod.py")


def test_compdb_build_script(builder):
    """The generated build_<pkg>.sh compiles the runtime, the wrappers, and
    the package init and links the importable <pkg>.so with only the inputs
    it documents (FC, FLAIR_RUNTIME, PROJ_LIBS)."""
    b = builder
    b.add_sources("vec_mod.F90", "ops_mod.F90")
    write_compdb(b, [
        {"command": "gfortran -c -o vec_mod.o vec_mod.F90",
         "file": "vec_mod.F90"},
        {"command": "gfortran -c -o ops_mod.o ops_mod.F90",
         "file": "ops_mod.F90"},
    ])

    b.flair_compdb("compile_commands.json", "ops_mod.F90", pkg="proj")
    b.compile("vec_mod.F90", "ops_mod.F90")
    b.run_build_script("proj", ["vec_mod.o", "ops_mod.o"])
    b.run_check("check_pkg_crossmod.py")


def test_compdb_per_entry_defines(builder):
    """A -D recorded for one entry is honored when parsing that entry (the
    #ifdef-guarded USE pulls vec_mod into the closure) without leaking into
    other files; the package is named after the entry's stem by default."""
    b = builder
    b.add_sources("cfg_mod.F90", "vec_mod.F90")
    write_compdb(b, [
        {"command": "gfortran -c -DUSE_VEC -o cfg_mod.o cfg_mod.F90",
         "file": "cfg_mod.F90"},
        {"arguments": ["gfortran", "-c", "-o", "vec_mod.o", "vec_mod.F90"],
         "file": "vec_mod.F90"},
    ])

    b.flair_compdb("compile_commands.json", "cfg_mod.F90")

    src = b.generated("cfg")
    assert "reset" in src and "noop" not in src
    assert not b.generated_missing("vec")  # discovered through the USE

    # default package name: entry stem minus _mod; wrappers export internal
    # inits, only the package file exports a PyInit
    pkg = b.generated("cfg_pkg")
    assert "PyInit_cfg" in pkg and "FLAIR_init_vec" in pkg
    assert "FLAIR_init_cfg" in src and "PyInit_" not in src


def test_compdb_wrap_outside_entry_closure(builder):
    """A --wrap file the entry does not USE (e.g. a bindings-only shim
    module) is a dependency-graph root of its own and still gets wrapped
    into the package."""
    b = builder
    b.add_sources("cfg_mod.F90", "vec_mod.F90")
    write_compdb(b, [
        {"command": "gfortran -c -o cfg_mod.o cfg_mod.F90",  # no -DUSE_VEC
         "file": "cfg_mod.F90"},
        {"command": "gfortran -c -o vec_mod.o vec_mod.F90",
         "file": "vec_mod.F90"},
    ])

    b.flair_compdb("compile_commands.json", "cfg_mod.F90",
                   wrap=["cfg_mod.F90", "vec_mod.F90"])

    assert "noop" in b.generated("cfg")  # entry parsed without the define
    assert not b.generated_missing("vec")  # wrapped despite not being USEd
    pkg = b.generated("cfg_pkg")
    assert "FLAIR_init_cfg" in pkg and "FLAIR_init_vec" in pkg


def test_compdb_wrap_restriction(builder):
    """--wrap keeps dependency modules resolution-only in compdb mode; the
    package then contains only the wrapped module."""
    b = builder
    b.add_sources("vec_mod.F90", "ops_mod.F90")
    write_compdb(b, [
        {"command": "gfortran -c -o ops_mod.o ops_mod.F90",
         "file": "ops_mod.F90"},
        {"command": "gfortran -c -o vec_mod.o vec_mod.F90",
         "file": "vec_mod.F90"},
    ])

    proc = b.flair_compdb("compile_commands.json", "ops_mod.F90",
                          wrap=["ops_mod.F90"])

    assert b.generated_missing("vec")
    assert "FLAIR_vec2_from_PyObject" in b.generated("ops")
    assert "generated separately, compiled before this one" in proc.stderr
    pkg = b.generated("ops_pkg")
    assert "FLAIR_init_ops" in pkg and "FLAIR_init_vec" not in pkg
