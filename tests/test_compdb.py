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


def test_compdb_wrap_entry(builder):
    """'--wrap @entry' expands to the entry's own modules plus the ones it
    USEs directly -- a deliberately shallow set: top_mod and mid_mod, but not
    leaf_mod two hops away, which the bare closure would wrap. Further --wrap
    arguments compose with it rather than replacing it.

    leaf_mod stays out only because its types never cross mid_mod's API; one
    that did would promote it as a converter producer (see
    test_compdb_wrap_converter_closure).
    """
    b = builder
    b.add_sources("leaf_mod.F90", "mid_mod.F90", "top_mod.F90")
    write_compdb(b, [
        {"command": "gfortran -c -o top_mod.o top_mod.F90",
         "file": "top_mod.F90"},
        {"command": "gfortran -c -o mid_mod.o mid_mod.F90",
         "file": "mid_mod.F90"},
        {"command": "gfortran -c -o leaf_mod.o leaf_mod.F90",
         "file": "leaf_mod.F90"},
    ])

    b.flair_compdb("compile_commands.json", "top_mod.F90", pkg="proj",
                   wrap=["@entry"])
    assert not b.generated_missing("top")
    assert not b.generated_missing("mid")
    assert b.generated_missing("leaf")
    pkg = b.generated("proj_pkg")
    assert "FLAIR_init_top" in pkg and "FLAIR_init_leaf" not in pkg

    # the token composes with explicitly named files
    b.clear_generated()
    b.flair_compdb("compile_commands.json", "top_mod.F90", pkg="proj",
                   wrap=["@entry", "leaf_mod.F90"])
    assert not b.generated_missing("leaf")

    # without --wrap the whole closure is wrapped, as before
    b.clear_generated()
    b.flair_compdb("compile_commands.json", "top_mod.F90", pkg="proj")
    assert not b.generated_missing("leaf")


def test_compdb_wrap_entry_needs_compdb(builder):
    """The @entry token has nothing to derive from outside compdb mode."""
    b = builder
    b.add_sources("leaf_mod.F90")
    proc = b.flair_all("leaf_mod.F90", wrap=["@entry"], expect_error=True)
    assert "requires --compdb mode" in proc.stderr


def test_compdb_wrap_converter_closure(builder):
    """A wrap set naming only ops_mod still yields a package that builds: the
    vec2 crossing ops_mod's API pulls its producer vec_mod into the wrap set,
    ahead of ops_mod in the build script. Modules whose types stay out of the
    wrapped API remain resolution-only (see test_compdb_wrap_entry)."""
    b = builder
    b.add_sources("vec_mod.F90", "ops_mod.F90")
    write_compdb(b, [
        {"command": "gfortran -c -o ops_mod.o ops_mod.F90",
         "file": "ops_mod.F90"},
        {"command": "gfortran -c -o vec_mod.o vec_mod.F90",
         "file": "vec_mod.F90"},
    ])

    proc = b.flair_compdb("compile_commands.json", "ops_mod.F90", pkg="proj",
                          wrap=["ops_mod.F90"])

    assert not b.generated_missing("vec")
    assert "FLAIR_vec2_from_PyObject" in b.generated("ops")
    assert "generated separately, compiled before this one" not in proc.stderr
    pkg = b.generated("proj_pkg")
    assert "FLAIR_init_ops" in pkg and "FLAIR_init_vec" in pkg

    script = (b.tmp / "build_proj.sh").read_text()
    assert script.index("py_vec.F90") < script.index("py_ops.F90")

    # the promoted producer makes the narrowed wrap set actually buildable
    b.compile("vec_mod.F90", "ops_mod.F90")
    b.run_build_script("proj", ["vec_mod.o", "ops_mod.o"])
    b.run_check("check_pkg_crossmod.py")
