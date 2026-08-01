"""Generic-interface wrapping: one dispatcher per generic, discrimination on
scalar category / derived type / array dtype+rank, kind-collapse, and
single-specific forwarding (case: generic)."""

import re


def test_generic_interfaces(builder):
    b = builder.build(sources=["generic_mod.f90"])

    src = b.generated("generic")

    # one dispatcher exposed under the generic name, classifying each
    # discriminating position against a table of the kinds accepted there
    assert "function py_mod_describe(self, args, kwds)" in src
    assert "type(FLAIR_tag_t), save :: py_mod_describe_tags0(" in src
    assert re.search(
        r"tag0 = FLAIR_classify\(PyTuple_GetItem\(args, 0_c_ptrdiff_t\), "
        r"py_mod_describe_tags0\)",
        src,
    )

    # derived-type discrimination carries the qualified runtime tp_name
    s_var = re.search(
        r'(s_\d+) = "generic\.Thing_t"//c_null_char', src
    )
    assert s_var, "expected the qualified tp_name in the string pool"
    assert re.search(
        rf"FLAIR_tag_t\(FLAIR_K_DERIVED, \d+, 0, 0, c_loc\({s_var.group(1)}\)\)",
        src,
    )

    # array discrimination carries rank and dtype; scalar kinds carry neither
    assert re.search(r"FLAIR_tag_t\(FLAIR_K_ARRAY, \d+, 1, NPY_FLOAT64, ", src)
    assert re.search(r"FLAIR_tag_t\(FLAIR_K_ARRAY, \d+, 2, NPY_FLOAT64, ", src)
    assert re.search(r"FLAIR_tag_t\(FLAIR_K_REAL, \d+, 0, 0, c_null_ptr\)", src)
    assert re.search(r"FLAIR_tag_t\(FLAIR_K_CMPLX, \d+, 0, 0, c_null_ptr\)", src)

    # fallback when no overload matches
    assert "unexpected argument type for describe" in src

    # kind-only overloads collapse: total dispatches to the widest specific
    assert re.search(r"r = py_mod_total_i8\(self, args, kwds\)", src)
    assert not re.search(r"r = py_mod_total_i4\(self, args, kwds\)", src)

    # single specific: unconditional forward without probing
    assert re.search(r"r = py_mod_area_circle\(self, args, kwds\)", src)

    b.run_check("check_generic.py")
