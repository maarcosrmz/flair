import sys

sys.path.insert(0, ".")
import optionals

failures = []


def check(name, cond):
    print(("PASS" if cond else "FAIL"), name)
    if not cond:
        failures.append(name)


# --- omitted trailing optionals ---
check("all absent", optionals.describe(5) == 5)
check("one present", optionals.describe(5, 2.0) == 25)
check("two present", optionals.describe(5, 2.0, True) == 125)
check("all present", optionals.describe(5, 2.0, True, "ab") == 2125)

# --- None counts as absent at any position ---
check("None placeholder", optionals.describe(5, None, True) == 105)

# --- keyword arguments ---
check("kw optional", optionals.describe(5, c=True) == 105)
check("kw everything", optionals.describe(a=5, b=2.0) == 25)

try:
    optionals.describe(5, a=5)
    check("duplicate positional+kw raises", False)
except TypeError as e:
    check("duplicate positional+kw raises", "'a'" in str(e))
try:
    optionals.describe(5, nope=1)
    check("unknown kw raises", False)
except TypeError as e:
    # the offending keyword is named, as CPython does
    check("unknown kw names the key", "nope" in str(e))
try:
    optionals.describe()
    check("missing required raises", False)
except TypeError as e:
    check("missing required names the argument", "'a'" in str(e))
try:
    optionals.describe(5, 2.0, True, "ab", 99)
    check("surplus positional raises", False)
except TypeError:
    check("surplus positional raises", True)

# --- optional arrays and explicit-length characters ---
check("array absent", optionals.osum([1.0, 2.0]) == 3.0)
check("array present", optionals.osum([1.0, 2.0], [3.0]) == 9.0)
check("char after None", optionals.osum([1.0, 2.0], None, "abc") == 6.0)
check("short char pads", optionals.osum([1.0, 2.0], tag3="ab") == 6.0)
try:
    optionals.osum([1.0], tag3="toolong")
    check("long char raises", False)
except ValueError:
    check("long char raises", True)

# --- single optional: zero-argument call ---
check("no args at all", optionals.nopt() == -1)
check("positional", optionals.nopt(5) == 5)
check("keyword", optionals.nopt(n=7) == 7)

# --- optional derived-type dummy ---
check("derived absent", optionals.pval() == 0)
check("derived None", optionals.pval(None) == 0)
check("derived present", optionals.pval(optionals.Pair_t()) == 1)

# --- generic dispatch with optionals (and kwds forwarded through) ---
check("generic int full", optionals.combine(3, 4) == 7)
check("generic int short", optionals.combine(3) == 3)
check("generic str short", optionals.combine("hey") == 3)
check("generic str full", optionals.combine("hey", True) == -3)
check("generic str kw", optionals.combine("hey", upper=True) == -3)

print("---")
if failures:
    print("FAILURES:", failures)
    sys.exit(1)
print("all tests passed")
