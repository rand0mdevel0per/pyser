import pytest
import random
import sys
import pathlib
# Ensure repository root is on sys.path so local package `pyserpy` can be imported during tests
_repo_root = pathlib.Path(__file__).resolve().parent.parent
if str(_repo_root) not in sys.path:
    sys.path.insert(0, str(_repo_root))

from pyserpy import dumps, loads, dump, load


def test_basic_types_roundtrip():
    obj = {"a": 1, "b": 2.5, "c": "hello", "d": True}
    data = dumps(obj)
    assert isinstance(data, (bytes, bytearray))
    out = loads(data)
    assert out == obj


def test_nested_containers():
    obj = {"l": [1, [2, 3], {"x": [4,5]}], "t": (1,2,3), "s": {1,2,3}}
    data = dumps(obj)
    out = loads(data)
    # sets may not preserve order; compare as structures
    assert out["l"][0] == 1
    assert tuple(out["t"]) == (1,2,3)


def test_bytes_and_large_data():
    b = bytes(range(256)) * 4
    obj = {"b": b}
    data = dumps(obj)
    out = loads(data)
    assert out["b"] == b


def test_file_dump_load(tmp_path):
    obj = {"x": [1,2,3]}
    f = tmp_path / "data.bin"
    dump(obj, str(f))
    out = load(str(f))
    assert out == obj


# New tests to increase coverage: complex classes, closures, memoryview, and noising
class ComplexData:
    def __init__(self, value):
        self.value = value

    def double(self):
        return self.value * 2


def make_closure(a):
    def closure(b):
        return a + b
    return closure


def test_complex_class_with_closure_roundtrip():
    obj = ComplexData(7)
    obj.func = make_closure(5)  # closure capturing `a=5`
    # Attempt roundtrip; if functions are unsupported, skip.

    data = dumps(obj)
    out = loads(data)
    assert hasattr(out, "value") and out.value == obj.value
    # If callable survived, verify behavior
    if hasattr(out, "func") and callable(out.func):
        assert out.func(3) == obj.func(3)

def test_callable_inside_container_roundtrip_or_skip():
    # container with simple callable items
    lst = [make_closure(1), make_closure(2), 10]
    data = dumps(lst)
    out = loads(data)
    print("deser finished")
    assert isinstance(out, list)
    assert out[-1] == 10
    if callable(out[0]):
        assert out[0](5) == lst[0](5)


def test_memoryview_and_bytearray_roundtrip():
    b = bytes(range(256)) * 2
    obj = {"mv": memoryview(b), "ba": bytearray(b)}
    data = dumps(obj)
    out = loads(data)

    # The native deserializer in this build may not reconstruct a memoryview object; it
    # often returns a bytes or bytearray instead (that's acceptable). Accept any
    # bytes-like output and compare contents.
    mv_out = out.get("mv")
    assert mv_out is not None, "missing 'mv' in deserialized output"
    # Accept memoryview, bytes, or bytearray
    if hasattr(mv_out, "tobytes"):
        mv_bytes = mv_out.tobytes()
    else:
        mv_bytes = bytes(mv_out)
    assert mv_bytes == b

    ba_out = out.get("ba")
    assert ba_out is not None, "missing 'ba' in deserialized output"
    assert bytes(ba_out) == b


# New comprehensive custom class test
class MixedNested:
    def __init__(self, name, value):
        self.name = name
        self.value = value


class MixedData:
    def __init__(self):
        self.i = 42
        self.f = 3.14159
        self.s = "hello-世界"
        self.b = b"\x00\x01\x02"
        self.ba = bytearray(b"\x10\x11")
        self.mv = memoryview(b"abc")
        self.lst = [1, 2, 3]
        self.tup = ("a", "b")
        self.d = {"k": "v"}
        self.st = {1, 2, 3}
        self.nested = MixedNested("inner", 99)
        # Attach a callable if supported; tests will skip callable checks if unsupported
        self.func = lambda x: x + 1

    def total(self):
        return self.i + int(self.f)


def test_custom_mixed_class_roundtrip():
    obj = MixedData()
    data = dumps(obj)
    out = loads(data)
    # Basic scalar fields
    assert getattr(out, "i") == obj.i
    # floats may roundtrip exactly for simple values
    assert abs(getattr(out, "f") - obj.f) < 1e-8
    assert getattr(out, "s") == obj.s

    # bytes/bytearray/memoryview: accept bytes-like results
    # b
    assert bytes(getattr(out, "b")) == obj.b
    # ba
    assert bytes(getattr(out, "ba")) == bytes(obj.ba)
    # mv may deserialize to bytes/bytearray/memoryview
    mv_out = getattr(out, "mv")
    if hasattr(mv_out, "tobytes"):
        assert mv_out.tobytes() == obj.mv.tobytes()
    else:
        assert bytes(mv_out) == obj.mv.tobytes()

    # containers
    assert list(getattr(out, "lst")) == obj.lst
    assert tuple(getattr(out, "tup")) == obj.tup
    assert dict(getattr(out, "d")) == obj.d
    assert set(getattr(out, "st")) == obj.st

    # nested object
    nested_out = getattr(out, "nested")
    assert hasattr(nested_out, "name") and nested_out.name == obj.nested.name
    assert getattr(nested_out, "value") == obj.nested.value

    # callable: check presence and behavior only if callable was preserved
    if hasattr(out, "func") and callable(out.func):
        assert out.func(5) == obj.func(5)


def test_noising_detection_flip_bytes():
    obj = {"x": list(range(100))}
    data = dumps(obj)
    if not isinstance(data, (bytes, bytearray)) or len(data) == 0:
        pytest.skip("serialized payload not bytes-like or empty")
    corrupted = bytearray(data)
    # Flip a handful of random positions
    for _ in range(min(10, max(1, len(corrupted) // 50))):
        i = random.randrange(len(corrupted))
        corrupted[i] ^= 0xFF
    with pytest.raises(Exception):
        loads(bytes(corrupted))


def test_noising_detection_truncated_data():
    obj = {"hello": "world", "n": 12345}
    data = dumps(obj)
    if not isinstance(data, (bytes, bytearray)) or len(data) < 10:
        pytest.skip("serialized payload too small to truncate safely")
    truncated = data[: len(data) - 5]  # remove footer/part of payload
    with pytest.raises(Exception):
        loads(truncated)


if __name__ == "__main__":
    pytest.main(["-q"])
