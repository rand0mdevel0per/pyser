# quick debug runner
from pyserpy import dumps, loads

b = bytes(range(256)) * 2
obj = {"mv": memoryview(b), "ba": bytearray(b)}
print("original types:", type(obj["mv"]), type(obj["ba"]))
data = dumps(obj)
print("serialized len", len(data))
out = loads(data)
print("deserialized types:", type(out["mv"]), type(out["ba"]))
try:
    mv = out["mv"]
    if hasattr(mv, "tobytes"):
        print("mv.tobytes len", len(mv.tobytes()))
    else:
        print("mv as bytes len", len(bytes(mv)))
except Exception as e:
    print("error examining mv:", e)
