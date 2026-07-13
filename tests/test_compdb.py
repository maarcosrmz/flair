"""Compilation-database mode: USE-closure discovery from an entry point,
per-entry compile flags, and foreign-flag tolerance."""

import json


def write_compdb(builder, entries) -> None:
    for e in entries:
        e.setdefault("directory", str(builder.tmp))
    (builder.tmp / "compile_commands.json").write_text(json.dumps(entries))


def test_compdb_crossmod(builder):
    """The vec_mod dependency of ops_mod is discovered from the database
    (entries out of dependency order, "command" and "arguments" forms mixed,
    foreign gfortran flags dropped) and both wrappers come out of one run,
    behaving like the classic per-file builds."""
    b = builder
    b.add_sources("vec_mod.F90", "ops_mod.F90")
    write_compdb(b, [
        {"command": "gfortran -c -O2 -Wall -o ops_mod.o ops_mod.F90",
         "file": "ops_mod.F90"},
        {"arguments": ["gfortran", "-c", "-ffree-form", "-o", "vec_mod.o",
                       "vec_mod.F90"],
         "file": "vec_mod.F90"},
    ])

    proc = b.flair_compdb("compile_commands.json", "ops_mod.F90")
    assert "must be generated separately and linked" not in proc.stderr
    assert "FLAIR_vec2_from_PyObject" in b.generated("ops")

    b.compile("vec_mod.F90", "ops_mod.F90")
    b.link_lib("case", "vec_mod.o", "ops_mod.o")
    b.extension("vec", "case")
    b.extension("ops", "case", "vec.so")
    b.run_check("check_crossmod.py")


def test_compdb_per_entry_defines(builder):
    """A -D recorded for one entry is honored when parsing that entry (the
    #ifdef-guarded USE pulls vec_mod into the closure) without leaking into
    other files."""
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


def test_compdb_wrap_outside_entry_closure(builder):
    """A --wrap file the entry does not USE (e.g. a bindings-only shim
    module) is a dependency-graph root of its own and still gets wrapped."""
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


def test_compdb_wrap_restriction(builder):
    """--wrap keeps dependency modules resolution-only in compdb mode."""
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
    assert "must be generated separately and linked" in proc.stderr
