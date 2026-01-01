import importlib
import traceback

try:
    m = importlib.import_module("pyser")
    print("pyser module:", m, getattr(m, "__file__", None))
except Exception as e:
    print("import pyser failed:", e)

import pyserpy

print("pyserpy._native:", getattr(pyserpy, "_native", None))
print("pyserpy._import_error:", getattr(pyserpy, "_import_error", None))

from pyserpy import dumps, loads

b = bytes(range(256)) * 2
obj = {"mv": memoryview(b), "ba": bytearray(b)}
print("calling dumps..")
data = dumps(obj)
print("dumps len", len(data))
try:
    print("calling loads..")
    out = loads(data)
    print("loads returned types:", type(out.get("mv")), type(out.get("ba")))
except Exception:
    traceback.print_exc()
