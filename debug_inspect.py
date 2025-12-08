import importlib, json, base64, hashlib, sys
import zstandard as zstd

# Use wrapper package to ensure native extension is loaded from the package
try:
    import pyserpy
    m = None
except Exception as e:
    print('import pyserpy failed:', e)
    sys.exit(2)

print('using pyserpy module:', getattr(pyserpy, '__file__', repr(pyserpy)))
obj = {'hello':'world','nums':[1,2,3,4,5,6,7,8,9,10]}
try:
    b = pyserpy.dumps(obj)
except Exception as e:
    print('serialize failed (pyserpy.dumps):', type(e), e)
    # try direct access to native module if available
    try:
        native = pyserpy._ensure_native()
        print('native module:', native)
        b = native.serialize(obj)
    except Exception as e2:
        print('native serialize failed:', e2)
        sys.exit(3)
print('len b', len(b))

try:
    dctx = zstd.ZstdDecompressor()
    json_bytes = dctx.decompress(b)
except Exception as e:
    print('zstd decompress failed:', type(e), e)
    sys.exit(4)

print('json len', len(json_bytes))
try:
    j = json.loads(json_bytes)
except Exception as e:
    print('json parse failed:', e)
    print('json snippet:', json_bytes[:200])
    sys.exit(5)

chunks = j.get('chunks', [])
print('chunks count', len(chunks))
for c in chunks:
    cid = c.get('id')
    sha = c.get('sha256')
    b64 = c.get('data')
    raw = base64.b64decode(b64)
    mysha = hashlib.sha256(raw).hexdigest()
    ok = (sha == mysha)
    print(f'chunk {cid}: stored={sha} computed={mysha} ok={ok} raw_len={len(raw)} base64_len={len(b64)}')
    if not ok:
        print('raw hex prefix', raw[:64].hex())
        break
print('done')
