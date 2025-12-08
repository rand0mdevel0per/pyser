import json

import zstandard as zstd

import pyserpy

obj = {"a":1, "b":2.5, "c":"hello", "d": True}
print('object:', obj)
b = pyserpy.dumps(obj)
print('bytes len', len(b))

j = json.loads(zstd.ZstdDecompressor().decompress(b))
print('root_id', j.get('root_id'))
print('nodes:')
for n in j.get('nodes', []):
    print(' id', n.get('id'), 'type', n.get('type'), 'meta.type_name', n.get('meta',{}).get('type_name'))
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

