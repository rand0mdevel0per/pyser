import traceback
from pyserpy import dumps, loads

b = bytes(range(256)) * 2
obj = {"mv": memoryview(b), "ba": bytearray(b)}
print("serializing")
data = dumps(obj)
print("data len", len(data))
try:
    print("calling loads()")
    out = loads(data)
    print("returned", type(out), type(out.get("mv")))
except Exception:
    print("Exception from loads:")
    traceback.print_exc()
