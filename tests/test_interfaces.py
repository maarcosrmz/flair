"""Generic-interface wrapping: one dispatcher per generic, discrimination on
scalar category / derived type / array dtype+rank, kind-collapse, and
single-specific forwarding (case: generic)."""

import re


def test_generic_interfaces(builder):
    b = builder.build(sources=["generic_mod.f90"])

    src = b.generated("generic")

    # one dispatcher exposed under the generic name
    assert "function py_mod_describe(self, args, kwds)" in src

    # derived-type discrimination compares the runtime tp_name
    assert 'c_string_eq(pytype%tp_name, "generic.Thing_t")' in src

    # array discrimination probes rank and dtype
    assert "PyArray_NDIM" in src
    assert "PyArray_DESCR" in src

    # scalar probes convert with the checked helpers, complex last (its
    # converter also accepts ints and floats)
    assert re.search(
        r"FLAIR_double_from_PyObject.*\n(.*\n)*?.*FLAIR_dcomplex_from_PyObject",
        src,
    )

    # fallback when no overload matches
    assert "unexpected argument type for describe" in src

    # kind-only overloads collapse: total dispatches to the widest specific
    assert re.search(r"r = py_mod_total_i8\(self, args, kwds\)", src)
    assert not re.search(r"r = py_mod_total_i4\(self, args, kwds\)", src)

    # single specific: unconditional forward without probing
    assert re.search(r"r = py_mod_area_circle\(self, args, kwds\)", src)

    b.run_check("check_generic.py")
