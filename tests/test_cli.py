"""The command-line surface itself: --help, --version, and the flag
combinations that are rejected before any input is parsed."""

import subprocess


def run(flair_bin, *args) -> subprocess.CompletedProcess:
    return subprocess.run([str(flair_bin), *args], capture_output=True,
                          text=True)


def test_help_documents_every_option(flair_bin):
    """-h and --help print the same usage block, naming every flair option
    (flang's own flags are documented by flang)."""
    proc = run(flair_bin, "--help")
    assert proc.returncode == 0, proc.stderr
    for option in ("--wrap", "@entry", "--compdb", "--entry", "--pkg",
                   "--verbose", "--version"):
        assert option in proc.stdout, option

    assert run(flair_bin, "-h").stdout == proc.stdout


def test_version_reports_flair_and_flang(flair_bin):
    """--version names flair's own version and the flang it was built
    against, since wrappers only work with that same toolchain."""
    proc = run(flair_bin, "--version")
    assert proc.returncode == 0, proc.stderr
    assert "flair-f2py version" in proc.stdout
    assert "flang version" in proc.stdout


def test_verbose_is_off_by_default(flair_bin):
    """Progress notes stay behind -v: a default run's stderr carries
    diagnostics only, so tests and scripts can assert on it."""
    assert run(flair_bin, "--version").stderr == ""
    assert run(flair_bin).stderr == ""
    assert "Based on" in run(flair_bin, "-v").stderr


def test_bad_flag_combinations_fail_early(flair_bin):
    """Each combination is rejected with its own message and a nonzero exit,
    before any input file is opened -- note none of these name a source."""
    for args, message in [
        (["--compdb", "."], "--compdb and --entry must be used together."),
        (["--entry", "a.F90"], "--compdb and --entry must be used together."),
        (["--pkg", "x"], "--pkg requires --compdb mode."),
        (["--wrap", "@entry"], "--wrap @entry requires --compdb mode."),
        (["--wrap"], "--wrap requires an argument."),
    ]:
        proc = run(flair_bin, *args)
        assert proc.returncode != 0, args
        assert message in proc.stderr, args
