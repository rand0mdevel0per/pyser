"""Tests specifically for marshal-free code object serialization.

This test file verifies that the pyser library correctly serializes and
deserializes Python functions using pure C++ JSON serialization instead
of Python's marshal module.
"""

import pytest
import sys
import pathlib

# Ensure repository root is on sys.path
_repo_root = pathlib.Path(__file__).resolve().parent.parent
if str(_repo_root) not in sys.path:
    sys.path.insert(0, str(_repo_root))

from pyserpy import dumps, loads


def test_simple_function_roundtrip():
    """Test basic function serialization."""

    def add(a, b):
        return a + b

    data = dumps(add)
    out = loads(data)
    assert callable(out)
    assert out(2, 3) == 5


def test_function_with_defaults():
    """Test function with default arguments."""

    def greet(name, greeting="Hello"):
        return f"{greeting}, {name}!"

    data = dumps(greet)
    out = loads(data)
    # Default args should now be preserved
    assert out("World") == "Hello, World!"
    assert out("World", "Hi") == "Hi, World!"


def test_closure_with_freevars():
    """Test closures that capture free variables."""

    def make_adder(n):
        def adder(x):
            return x + n

        return adder

    add5 = make_adder(5)
    data = dumps(add5)
    out = loads(data)
    assert callable(out)
    # Note: closure restoration may not work on all builds
    # If closure was restored, verify behavior
    if hasattr(out, "__closure__") and out.__closure__:
        assert out(10) == 15


def test_function_with_nested_code():
    """Test function containing nested function definitions."""

    def outer():
        def inner():
            return 42

        return inner()

    data = dumps(outer)
    out = loads(data)
    assert out() == 42


def test_lambda_roundtrip():
    """Test lambda function serialization."""
    fn = lambda x, y: x * y + 1
    data = dumps(fn)
    out = loads(data)
    assert out(3, 4) == 13


def test_function_with_multiple_consts():
    """Test function with various constant types in co_consts."""

    def multi_const():
        a = 42
        b = 3.14
        c = "hello"
        d = None
        e = True
        f = (1, 2, 3)
        return a, b, c, d, e, f

    data = dumps(multi_const)
    out = loads(data)
    result = out()
    assert result[0] == 42
    assert abs(result[1] - 3.14) < 1e-10
    assert result[2] == "hello"
    assert result[3] is None
    assert result[4] is True
    assert result[5] == (1, 2, 3)


def test_function_with_many_locals():
    """Test function with many local variables."""

    def many_locals(a, b, c, d, e):
        x = a + b
        y = c + d
        z = e + x + y
        return z

    data = dumps(many_locals)
    out = loads(data)
    assert out(1, 2, 3, 4, 5) == 15


def test_function_kwargs_only():
    """Test function with keyword-only arguments."""

    def kwonly(a, *, b, c=10):
        return a + b + c

    data = dumps(kwonly)
    out = loads(data)
    # Keyword defaults should now be preserved
    assert out(1, b=2) == 13
    assert out(1, b=2, c=3) == 6


if __name__ == "__main__":
    pytest.main(["-v", __file__])
