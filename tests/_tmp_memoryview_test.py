import sys, pathlib
sys.path.insert(0, r'E:\pyscr\pyser')
from pyserpy import dumps, loads

b = bytes(range(256)) * 2
obj = {"mv": memoryview(b), "ba": bytearray(b)}
print('calling dumps')
data = dumps(obj)
print('dumps returned len', len(data))
import traceback
try:
    out = loads(data)
    print('loads returned, mv type:', type(out['mv']))
except Exception:
    traceback.print_exc()
