import sys
import pathlib
# Ensure repository root is on sys.path so local package `pyserpy` can be imported during tests
_repo_root = pathlib.Path(__file__).resolve().parent.parent
if str(_repo_root) not in sys.path:
    sys.path.insert(0, str(_repo_root))

from pyserpy import dumps, loads


def test_roundtrip():
    obj = {"hello": "world", "nums": [1, 2, 3]}
    data = dumps(obj)
    assert isinstance(data, (bytes, bytearray))
    obj2 = loads(data)
    assert obj2 == obj

if __name__ == "__main__":
    test_roundtrip()
    print("smoke test passed")
