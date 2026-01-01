import zstandard as zstd

import pyserpy

obj = {"l": [1, [2, 3], {"x": [4, 5]}], "t": (1, 2, 3), "s": {1, 2, 3}}
print("object:", obj)
b = pyserpy.dumps(obj)
print("bytes len", len(b))

js = zstd.ZstdDecompressor().decompress(b).decode("utf-8")
print(js)
