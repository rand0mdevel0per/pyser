import sys, pathlib
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
from pyserpy import serialize

class ComplexData:
    def __init__(self,v):
        self.value=v
    def double(self):
        return self.value*2

def make_closure(a):
    def closure(b):
        return a+b
    return closure

obj = ComplexData(7)
obj.func = make_closure(5)
print('calling serialize')
try:
    data = serialize(obj)
    print('serialize returned len', len(data))
except Exception as e:
    import traceback
    traceback.print_exc()
    print('exception class', type(e))

