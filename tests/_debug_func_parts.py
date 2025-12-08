import sys
import pathlib
import pytest

# Ensure the project root is on sys.path so tests can import the local package.
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))

# Try to import the C-extension serialize function. If it's not available,
# skip these tests (useful for CI when the extension hasn't been built).
try:
    from pyserpy import serialize
except Exception as exc:  # pragma: no cover - skip when extension not built
    pytest.skip(
        "pyserpy.serialize not available — build the extension first. "
        "Example PowerShell (single line): mkdir build; python -m pip install -e .",
        allow_module_level=True,
    )


def make_closure(a):
    """Create a simple closure used to obtain a code object for testing."""
    def closure(b):
        return a + b
    return closure


# Prepare a variety of code-object related attributes to serialize.
f = make_closure(5)
code = f.__code__
attrs = [
    ("co_consts", code.co_consts),
    ("co_names", code.co_names),
    ("co_varnames", code.co_varnames),
    ("co_filename", code.co_filename),
    ("co_name", code.co_name),
    ("co_lnotab", getattr(code, "co_lnotab", None)),
    ("co_freevars", code.co_freevars),
    ("co_cellvars", code.co_cellvars),
    ("qualname", f.__qualname__),
]


def _is_nonempty_buffer(obj):
    """Return True if obj exposes a buffer-like view with length > 0."""
    try:
        mv = memoryview(obj)
    except TypeError:
        return False
    return len(mv) > 0


def test_serialize_code_object_parts():
    """Serialize each code-object part and assert we get a non-empty buffer back.

    The test will fail if serialization raises an exception or returns a non-buffer
    or an empty buffer. This keeps the assertions strict but helpful for debugging.
    """
    for name, val in attrs:
        try:
            data = serialize(val)
        except Exception as e:
            pytest.fail(f"serialize({name!r}) raised {type(e).__name__}: {e}")

        assert _is_nonempty_buffer(data), (
            f"serialize({name!r}) returned a non-buffer or empty buffer: {type(data)!r}"
        )
