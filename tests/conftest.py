"""Shared pytest harness for the flair-f2py regression suite.

Each test case builds Python extension modules from Fortran fixtures in an
isolated tmp_path: compile the wrapped sources with flang, run flair-f2py on
them, compile the generated py_*.F90 wrappers, and link everything into
importable .so files. Runtime behavior is then asserted by a per-case script
from tests/checks/ executed in a fresh Python subprocess (extension modules
cannot be unloaded, several cases reuse module names, and a crash in generated
code must fail one test instead of killing the pytest session).
"""

import os
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

REPO_ROOT = Path(__file__).resolve().parents[1]
FIXTURES = Path(__file__).parent / "fixtures"
CHECKS = Path(__file__).parent / "checks"
RUNTIME_SRC = REPO_ROOT / "share" / "flair" / "fortran_python_api.F90"


def _fmt(proc: subprocess.CompletedProcess) -> str:
    return (
        f"command: {' '.join(map(str, proc.args))}\n"
        f"exit code: {proc.returncode}\n"
        f"--- stdout ---\n{proc.stdout}\n--- stderr ---\n{proc.stderr}"
    )


@pytest.fixture(scope="session")
def flair_bin() -> Path:
    """flair-f2py binary: $FLAIR, then the sibling <repo>.build tree
    (flair.src -> flair.build convention), then a legacy in-tree build/."""
    if "FLAIR" in os.environ:
        candidates = [Path(os.environ["FLAIR"])]
    else:
        sibling_build = REPO_ROOT.parent / REPO_ROOT.name.replace(".src", ".build")
        candidates = [
            sibling_build / "src/tools/flair-f2py",
            REPO_ROOT / "build/src/tools/flair-f2py",
        ]
    for path in candidates:
        if path.is_file():
            return path
    pytest.skip(
        "flair-f2py not found at "
        + " or ".join(str(c) for c in candidates)
        + "; build it (cmake -S flair.src -B flair.build && "
        "cmake --build flair.build --target flair-f2py) or set FLAIR=<path>"
    )


@pytest.fixture(scope="session")
def flang() -> Path:
    """LLVM flang: $FLANG -> ~/.local/llvm/bin/flang -> PATH."""
    candidates = [
        os.environ.get("FLANG"),
        Path.home() / ".local/llvm/bin/flang",
        shutil.which("flang"),
    ]
    for cand in candidates:
        if cand and Path(cand).is_file():
            probe = subprocess.run(
                [str(cand), "--version"], capture_output=True, text=True
            )
            if probe.returncode == 0 and "flang" in probe.stdout.lower():
                return Path(cand)
    pytest.skip(
        "LLVM flang not found; set FLANG=<path> (must be the same toolchain "
        "used to build flair-f2py: generated FLAIR_* converter symbols rely "
        "on matching Fortran name mangling)"
    )


@pytest.fixture(scope="session")
def runtime_dir(tmp_path_factory, flang: Path) -> Path:
    """Compile the python_api_mod runtime once per session.

    Yields a directory containing fortran_python_api.o and python_api_mod.mod;
    per-case builds pass it via -I and link the object file.
    """
    rt = tmp_path_factory.mktemp("runtime")
    proc = subprocess.run(
        [str(flang), "-fPIC", "-c", str(RUNTIME_SRC)],
        cwd=rt,
        capture_output=True,
        text=True,
    )
    if proc.returncode != 0:
        pytest.fail(f"failed to compile {RUNTIME_SRC.name}:\n{_fmt(proc)}")
    return rt


@pytest.fixture(scope="session")
def intrinsic_mod_dir(flang: Path):
    """flang's intrinsic .mod files (iso_c_binding, ...): flair-f2py does not
    locate them on its own, so the harness passes -fintrinsic-modules-path."""
    prefix = flang.resolve().parents[1]
    hits = sorted(prefix.glob("lib/clang/*/finclude/flang/*"))
    return hits[-1] if hits else None


@pytest.fixture()
def builder(tmp_path, flair_bin, flang, runtime_dir, intrinsic_mod_dir):
    pytest.importorskip("numpy")
    return CaseBuilder(tmp_path, flair_bin, flang, runtime_dir, intrinsic_mod_dir)


def pymod(source_name: str) -> str:
    """Python module name for a fixture: strip extension and _mod suffix."""
    stem = Path(source_name).stem
    return stem[: -len("_mod")] if stem.endswith("_mod") else stem


class CaseBuilder:
    """Drives the flair build recipe inside one test's tmp_path."""

    def __init__(self, tmp: Path, flair: Path, flang: Path, runtime_dir: Path,
                 intrinsic_mod_dir: Path | None = None):
        self.tmp = tmp
        self.flair_bin = flair
        self.flang = flang
        self.runtime_dir = runtime_dir
        self.intrinsic_mod_dir = intrinsic_mod_dir
        self.flair_results: dict[str, subprocess.CompletedProcess] = {}
        self._extensions: list[str] = []

    def _run(self, cmd, check=True) -> subprocess.CompletedProcess:
        proc = subprocess.run(
            [str(c) for c in cmd], cwd=self.tmp, capture_output=True, text=True
        )
        if check and proc.returncode != 0:
            pytest.fail(f"build step failed:\n{_fmt(proc)}")
        return proc

    def add_sources(self, *names: str) -> None:
        for name in names:
            shutil.copy(FIXTURES / name, self.tmp / name)

    def compile(self, *names: str) -> None:
        """flang-compile fixture sources, in dependency order."""
        for name in names:
            self._run(
                [self.flang, "-fPIC", "-I", self.runtime_dir, "-c", name]
            )

    def flair(self, name: str, expect_error: bool = False) -> subprocess.CompletedProcess:
        cmd = [self.flair_bin]
        if self.intrinsic_mod_dir is not None:
            cmd += ["-fintrinsic-modules-path", self.intrinsic_mod_dir]
        proc = self._run([*cmd, name], check=False)
        self.flair_results[name] = proc
        if expect_error and proc.returncode == 0:
            pytest.fail(f"flair-f2py unexpectedly succeeded on {name}:\n{_fmt(proc)}")
        if not expect_error and proc.returncode != 0:
            pytest.fail(f"flair-f2py failed on {name}:\n{_fmt(proc)}")
        return proc

    def flair_compdb(self, compdb: str, entry: str,
                     wrap: list[str] | None = None,
                     pkg: str | None = None,
                     expect_error: bool = False) -> subprocess.CompletedProcess:
        """flair-f2py in compilation-database mode: the USE closure of
        `entry` is discovered from compile_commands.json, each file parsed
        with its own recorded flags, and the closure's modules wrapped
        (restricted by --wrap when given) into one combined package
        extension named --pkg (default: the entry's stem)."""
        cmd = [self.flair_bin, "--compdb", compdb, "--entry", entry]
        if pkg is not None:
            cmd += ["--pkg", pkg]
        if self.intrinsic_mod_dir is not None:
            cmd += ["-fintrinsic-modules-path", self.intrinsic_mod_dir]
        for w in wrap or []:
            cmd += ["--wrap", w]
        proc = self._run(cmd, check=False)
        self.flair_results[entry] = proc
        if expect_error and proc.returncode == 0:
            pytest.fail(f"flair-f2py unexpectedly succeeded on {entry}:\n{_fmt(proc)}")
        if not expect_error and proc.returncode != 0:
            pytest.fail(f"flair-f2py failed on {entry}:\n{_fmt(proc)}")
        return proc

    def flair_all(self, *names: str, wrap: list[str] | None = None,
                  expect_error: bool = False) -> subprocess.CompletedProcess:
        """One flair-f2py invocation over several sources (dependency order).

        USE'd modules are resolved from the sources of earlier inputs, so no
        .mod files are needed. --wrap restricts which inputs get wrappers.
        """
        cmd = [self.flair_bin]
        if self.intrinsic_mod_dir is not None:
            cmd += ["-fintrinsic-modules-path", self.intrinsic_mod_dir]
        for w in wrap or []:
            cmd += ["--wrap", w]
        proc = self._run([*cmd, *names], check=False)
        for name in names:
            self.flair_results[name] = proc
        if expect_error and proc.returncode == 0:
            pytest.fail(
                f"flair-f2py unexpectedly succeeded on {' '.join(names)}:\n{_fmt(proc)}"
            )
        if not expect_error and proc.returncode != 0:
            pytest.fail(f"flair-f2py failed on {' '.join(names)}:\n{_fmt(proc)}")
        return proc

    def generated_path(self, mod: str) -> Path:
        return self.tmp / f"py_{mod}.F90"

    def generated(self, mod: str) -> str:
        path = self.generated_path(mod)
        assert path.is_file(), f"expected generated file {path.name} is missing"
        return path.read_text()

    def generated_missing(self, mod: str) -> bool:
        return not self.generated_path(mod).exists()

    def link_lib(self, libname: str, *objs: str) -> None:
        """Link runtime + wrapped-module objects into one shared library."""
        self._run(
            [self.flang, "-fPIC", "-shared",
             self.runtime_dir / "fortran_python_api.o", *objs,
             "-o", f"lib{libname}.so"]
        )

    def extension(self, mod: str, lib: str, *extra_sos: str) -> None:
        """Compile py_<mod>.F90 and link the importable <mod>.so.

        extra_sos: previously built extensions whose FLAIR_* converter
        symbols this one needs (cross-module cases).
        """
        self._run(
            [self.flang, "-fPIC", "-I", self.runtime_dir, "-c", f"py_{mod}.F90"]
        )
        self._run(
            [self.flang, "-fPIC", "-shared", f"py_{mod}.o", f"lib{lib}.so",
             *extra_sos, "-o", f"{mod}.so", "-Wl,-rpath,$ORIGIN"]
        )
        self._extensions.append(f"{mod}.so")

    def package_extension(self, pkg: str, submods: list[str], lib: str) -> None:
        """Compile the wrappers + package init of a compdb run and link the
        single combined <pkg>.so."""
        for mod in [*submods, f"{pkg}_pkg"]:
            self._run(
                [self.flang, "-fPIC", "-I", self.runtime_dir, "-c", f"py_{mod}.F90"]
            )
        objs = [f"py_{mod}.o" for mod in [*submods, f"{pkg}_pkg"]]
        self._run(
            [self.flang, "-fPIC", "-shared",
             self.runtime_dir / "fortran_python_api.o", *objs, f"lib{lib}.so",
             "-o", f"{pkg}.so", "-Wl,-rpath,$ORIGIN"]
        )
        self._extensions.append(f"{pkg}.so")

    def run_build_script(self, pkg: str, proj_libs: list[str]) -> None:
        """Run the generated build_<pkg>.sh as shipped, supplying only the
        toolchain and link inputs it documents."""
        env = dict(
            os.environ,
            FC=str(self.flang),
            FLAIR_RUNTIME=str(RUNTIME_SRC),
            PROJ_LIBS=" ".join(proj_libs),
        )
        proc = subprocess.run(
            ["sh", f"build_{pkg}.sh"],
            cwd=self.tmp,
            capture_output=True,
            text=True,
            env=env,
        )
        if proc.returncode != 0:
            pytest.fail(f"build_{pkg}.sh failed:\n{_fmt(proc)}")
        self._extensions.append(f"{pkg}.so")

    def build(self, sources: list[str], wrap: list[str] | None = None,
              lib: str = "case") -> "CaseBuilder":
        """Full recipe: copy, compile, link shared lib, wrap, link extensions.

        sources must be in dependency order; each wrapped extension links all
        previously built extensions (crossmod recipe: ops.so links vec.so so
        the FLAIR_<type>_from_PyObject symbol resolves).
        """
        self.add_sources(*sources)
        self.compile(*sources)
        objs = [Path(s).stem + ".o" for s in sources]
        self.link_lib(lib, *objs)
        for src in wrap or sources:
            self.flair(src)
            mod = pymod(src)
            if self.generated_missing(mod):
                pytest.fail(
                    f"flair-f2py exited 0 on {src} but wrote no py_{mod}.F90 "
                    "and no diagnostic"
                )
            self.extension(mod, lib, *self._extensions)
        return self

    def build_single_run(self, sources: list[str], wrap: list[str] | None = None,
                         lib: str = "case") -> "CaseBuilder":
        """Like build, but with one flair-f2py invocation over all sources.

        flair-f2py runs before anything is compiled, proving that USE'd
        modules are resolved from source: no .mod file exists at that point.
        """
        self.add_sources(*sources)
        self.flair_all(*sources, wrap=wrap)
        self.compile(*sources)
        objs = [Path(s).stem + ".o" for s in sources]
        self.link_lib(lib, *objs)
        for src in wrap or sources:
            mod = pymod(src)
            if self.generated_missing(mod):
                pytest.fail(
                    f"flair-f2py exited 0 but wrote no py_{mod}.F90 "
                    "and no diagnostic"
                )
            self.extension(mod, lib, *self._extensions)
        return self

    def run_check(self, script_name: str) -> None:
        """Run a behavior script from tests/checks/ in a fresh interpreter."""
        env = dict(os.environ, PYTHONPATH=str(self.tmp))
        proc = subprocess.run(
            [sys.executable, str(CHECKS / script_name)],
            cwd=self.tmp,
            capture_output=True,
            text=True,
            env=env,
        )
        if proc.returncode != 0:
            pytest.fail(f"behavior check {script_name} failed:\n{_fmt(proc)}")
