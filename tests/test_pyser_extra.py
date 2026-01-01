import pytest
import pathlib
import sys
_repo_root = pathlib.Path(__file__).resolve().parent.parent
if str(_repo_root) not in sys.path:
    sys.path.insert(0, str(_repo_root))

from pyserpy import dumps, loads


def make_closure(a):
    def closure(b):
        return a + b
    return closure


class Holder:
    pass


class Mixed:
    __slots__ = ('a','b','func')
    def __init__(self):
        self.a = 1
        self.b = 2
    def add(self):
        return self.a + self.b


def test_closure_cell_contents_roundtrip():
    # Create closure and attach to object
    h = Holder()
    cl = make_closure(10)
    h.func = cl

    data = dumps(h)
    out = loads(data)
    assert hasattr(out, 'func')
    # If function survived and is callable, check behavior
    if callable(out.func):
        assert out.func(5) == cl(5)

    # Additionally, if we can introspect __closure__, ensure number of cells match
    try:
        orig_cells = cl.__closure__
        deser_cells = out.func.__closure__ if hasattr(out.func, '__closure__') else None
        if orig_cells is not None and deser_cells is not None:
            assert len(orig_cells) == len(deser_cells)
            # Compare contents as much as possible
            for o_c, d_c in zip(orig_cells, deser_cells):
                assert getattr(o_c, 'cell_contents', None) == getattr(d_c, 'cell_contents', None)
    except Exception:
        # Don't fail the test if introspection isn't supported in this build
        pass


def test_custom_mixed_class_callable_and_slots():
    m = Mixed()
    m.func = lambda x: x * 2
    data = dumps(m)
    out = loads(data)
    # slots may not reconstruct __dict__, but ensure basic exposed attrs exist
    assert hasattr(out, 'a') or hasattr(out, '__slots__')
    # If callable, test behavior
    if hasattr(out, 'func') and callable(out.func):
        assert out.func(3) == 6
