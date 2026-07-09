"""Only public symbols are wrapped (case: visibility)."""

import re


def test_visibility(builder):
    b = builder.build(sources=["visibility_mod.f90"])

    src = b.generated("visibility")

    # public symbols wrapped
    assert re.search(r"\bpub_fn\b", src)
    assert re.search(r"\bpub_t\b", src)

    # private symbols must not leak into the wrapper in any form
    assert not re.search(r"\bpriv_fn\b", src)
    assert not re.search(r"\bpriv_t\b", src)
    assert not re.search(r"\bhelper\b", src)

    b.run_check("check_visibility.py")
