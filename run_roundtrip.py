import importlib, sys
import pyserpy

obj = {"hello": "world", "nums": [1,2,3]}
print('using pyserpy', pyserpy)
try:
    b = pyserpy.dumps(obj)
    print('dumps ok len', len(b))
except Exception as e:
    print('dumps failed', e); sys.exit(1)

# Try pyserpy.loads
try:
    o = pyserpy.loads(b)
    print('pyserpy.loads ok', o)
except Exception as e:
    print('pyserpy.loads failed', type(e), e)

# Try access native module if present
try:
    native = pyserpy._ensure_native()
    print('native module', native)
    try:
        o2 = native.deserialize(b)
        print('native.deserialize ok', o2)
    except Exception as e:
        print('native.deserialize failed', type(e), e)
except Exception as e:
    print('no native module via pyserpy:', e)
    try:
        py = importlib.import_module('pyser')
        print('imported pyser', py)
        try:
            o3 = py.deserialize(b)
            print('py.deserialize ok', o3)
        except Exception as e:
            print('py.deserialize failed', type(e), e)
    except Exception as e2:
        print('direct pyser import failed', e2)

