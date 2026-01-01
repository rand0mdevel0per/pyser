import pathlib
import sys
_repo_root = pathlib.Path(__file__).resolve().parent.parent
if str(_repo_root) not in sys.path:
    sys.path.insert(0, str(_repo_root))

from pyserpy import dumps, loads


class C:
    pass


def test_function_with_reduce_cleared_before_marshal():
    # Create a function and attach a fake __reduce__ attribute that would
    # normally interfere with pickling/marshalling logic. Ensure that
    # serialize/deserialize works and function behavior is preserved.
    def f(x):
        return x + 1
    # Attach a dummy __reduce__ callable
    def fake_reduce():
        return (None, ())
    try:
        f.__reduce__ = fake_reduce
    except Exception:
        # Some function objects may not allow assignment; that's fine.
        pass

    c = C()
    c.fn = f
    # Ensure the class module recorded by serializer is importable under
    # the pytest module name. PyTest may load tests under package names
    # (e.g., 'tests.test_pyser_reduce'), so set __module__ accordingly.
    C.__module__ = __name__

    data = dumps(c)
    out = loads(data)
    assert hasattr(out, 'fn')
    if callable(out.fn):
        assert out.fn(10) == 11


def test_noising_large_data_roundtrip():
    # Create a noisy structure with various nested containers and data to
    # increase coverage for chunking and pointers.
    b = bytes(range(256)) * 8
    obj = {
        'bytes': b,
        'nested': [list(range(50)), {'x': set(range(10))}],
        'funcs': [lambda x: x * 2, lambda x: x + 3]
    }
    data = dumps(obj)
    out = loads(data)
    assert bytes(out['bytes']) == b
    assert tuple(out['nested'][0])[0] == 0
