Rkyv
======

Rkyv is a thin Python wrapper around the high-performance C++ "pyser" serialization library.
It exposes a compact Python API (dumps/loads/dump/load) and is intended to provide efficient
binary serialization of Python objects with chunking and per-chunk SHA256 checksums.

Features and advantages
-----------------------
- Fast native implementation in C++ with Python bindings.
- Chunked representation: large objects are split into chunks to reduce peak memory usage and
  allow partial processing in future extensions.
- Per-chunk SHA256 checksums provide corruption detection during deserialize.
- Optional file-based helpers to write/read serialized data.
- Designed to be packaged as a binary wheel that contains the compiled extension and its
  runtime dependencies (via vcpkg on Windows).

How it works (high level)
-------------------------
1. The serializer walks the Python object graph and extracts nodes (ints, strings, containers, etc.).
2. Large payload bytes are split into fixed-size chunks. Each chunk is base64-encoded in the JSON
   structure and carries a SHA256 hash of the raw bytes.
3. The whole representation (JSON) is compressed with Zstd for compact transport.
4. On deserialize, the JSON is decompressed, each chunk is base64-decoded and its SHA256 re-computed
   and compared with the stored value. If all checks pass, the Python objects are reconstructed.

Quick start
-----------
You can now install PySer directly using the setup.py script:

```bash
# Clone the repository
git clone https://github.com/rand0mdevel0per/pyser.git
cd pyser

# Install the package directly
./setup.py install
```

Alternatively, you can use the provided installation scripts:

**For Windows (PowerShell):**
```powershell
# Download and run the PowerShell installation script
curl -o install.ps1 https://raw.githubusercontent.com/rand0mdevel0per/pyser/main/install.ps1
.\install.ps1
```

**For Linux/Mac (Bash):**
```bash
# Download and run the Bash installation script
curl -o install.sh https://raw.githubusercontent.com/rand0mdevel0per/pyser/main/install.sh
chmod +x install.sh
./install.sh
```

You can also install the package using pip after cloning:

```bash
git clone https://github.com/rand0mdevel0per/pyser.git
cd pyser
python -m pip install .
```

Basic usage
-----------
```python
from pyserpy import dumps, loads, dump, load
obj = {"a": [1, 2, 3]}
data = dumps(obj)       # returns bytes
obj2 = loads(data)

# file based
dump(obj, "data.bin")
obj3 = load("data.bin")
```

Packaging notes
----------------
- This package bundles a compiled extension. The recommended way to produce a distributable wheel
  is to compile the native extension for the target platform and then build a wheel (e.g. using
  `python -m build`) including the compiled shared library inside the `pyserpy/` package directory.
- On Windows, use vcpkg to install consistent versions of OpenSSL and Zstd used by the project.

Developer notes
---------------
- The repository contains C++ sources in the `cpp/` directory and the Python wrapper in `pyserpy/`.
- A CMakeLists.txt is provided in `cpp/` — the `setup.py` in the repository root includes a CMake-backed
  build helper that tries to detect and use a vcpkg toolchain when available.

License
-------
MIT
