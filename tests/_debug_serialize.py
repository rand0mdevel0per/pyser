import json
import sys, pathlib

import zstandard as zstd

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
from pyserpy import serialize, deserialize


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
    print(obj)
    data = serialize(obj)
    print('serialize returned len', len(data))
    print(data)
    j = json.loads(zstd.ZstdDecompressor().decompress(data))
    print('\nserialized json:', j)
    print('root_id', j.get('root_id'))
    print('nodes:')
    for n in j.get('nodes', []):
        print(' id', n.get('id'), 'type', n.get('type'), 'meta.type_name', n.get('meta', {}).get('type_name'))
        if 'chunk_ids' in n:
            print('  chunk_ids', n.get('chunk_ids'))
        if 'meta' in n and n['meta'].get('attr_names'):
            print('  attr_names', n['meta'].get('attr_names'))
    print('\nall pointers:')
    for p in j.get('pointers', []):
        print(p)
    print('\nchunks:')
    for c in j.get('chunks', []):
        print(' id', c['id'], 'size', c['size'])
    dest = deserialize(data)
    print(dest)
except Exception as e:
    import traceback
    traceback.print_exc()
    print('exception class', type(e))

