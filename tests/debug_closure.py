import traceback
from pyserpy import dumps, loads


class ComplexData:
    def __init__(self, value):
        self.value = value


def make_closure(a):
    def closure(b):
        return a + b

    return closure


obj = ComplexData(7)
obj.func = make_closure(5)

try:
    data = dumps(obj)
    print("dumps len", len(data))
    out = loads(data)
    print("loaded type", type(out), hasattr(out, "value"), getattr(out, "value", None))
    if hasattr(out, "func") and callable(out.func):
        print("func ok ->", out.func(3))
except Exception:
    traceback.print_exc()
