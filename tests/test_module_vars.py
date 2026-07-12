"""Module-level variables exposed as live module attributes (case: state).

Derived-type variables and intrinsic arrays become views created in PyInit
(reads and writes alias the Fortran storage); intrinsic scalars are served
per-access by a module-level __getattr__ (PEP 562). Annotated and
non-exposable variables are skipped.
"""


def test_module_vars(builder):
    b = builder.build(sources=["state_mod.f90"])

    src = b.generated("state")

    # scalars go through the module __getattr__
    assert "py_mod_getattr" in src
    assert "__getattr__" in src
    assert "PyErr_SetObject(PyExc_AttributeError" in src

    # address helpers use a TARGET dummy (module variables lack TARGET)
    assert "flr_loc_config" in src
    assert "flr_loc_grid" in src

    # view attributes are registered on the module object
    assert "PyModule_AddObjectRef(mod_ptr, c_loc" in src

    # character arrays map to NPY_STRING with the element length as itemsize
    assert "NPY_STRING" in src
    assert "6_c_int" in src

    # the annotated variable is not exposed anywhere
    assert "hidden" not in src

    b.run_check("check_state.py")
