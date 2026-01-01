# pyserpy __init__.py
# This module is a thin wrapper around the compiled native extension (module name 'pyser').
# Loading strategy:
# 1) Try to import the extension by name ('pyser'). This is the preferred path when the
#    extension is installed as a proper Python extension module.
# 2) If that fails, search the package directory for a shared library filename containing
#    'pyser' (e.g. pysercp313-win_amd64.so, pyser.pyd) and dynamically load it using
#    importlib.machinery.ExtensionFileLoader. When loading a local shared library we try to
#    import it with module name 'pyser' (to match the exported PyInit symbol). If 'pyser'
#    already exists in sys.modules we fall back to a private unique module name.

# Wrapper package for the compiled `pyser` C extension.
# This module exposes a friendly Python API (dumps/loads/dump/load)
# while locating and loading the compiled extension if necessary.

from typing import Any
import importlib
import importlib.util
import importlib.machinery
import os
import sys
import contextlib

__all__ = ["dumps", "loads", "dump", "load", "serialize", "deserialize"]


def _load_native():
    """Try to find and load the compiled extension module.

    Strategy:
    - Try to import `pyser` directly (the extension's declared module name).
    - If that fails, search the current package directory for files
      that look like an extension (e.g. starting with "pyser" and ending
      with .so or .pyd) and load the first match as a private module.
    """
    errors = []
    # 1) Try direct import
    try:
        return importlib.import_module("pyser")
    except Exception as e:
        errors.append(("import", repr(e)))

    # 2) Search the package directory for extension files
    pkg_dir = os.path.dirname(__file__)
    candidates = []
    for fname in os.listdir(pkg_dir):
        lf = fname.lower()
        if lf.startswith("pyser") and (
            lf.endswith(".so") or lf.endswith(".pyd") or lf.endswith(".dll")
        ):
            candidates.append(os.path.join(pkg_dir, fname))

    # Also allow the common name produced by CMake in this repo
    # e.g. pysercp313-win_amd64.so. Consider any file that contains "pyser" and is a shared lib.
    if not candidates:
        for fname in os.listdir(pkg_dir):
            lf = fname.lower()
            if "pyser" in lf and (lf.endswith(".so") or lf.endswith(".pyd") or lf.endswith(".dll")):
                candidates.append(os.path.join(pkg_dir, fname))

    for path in candidates:
        try:
            # Use the declared module name 'pyser' when possible so the extension's
            # exported init function (PyInit_pyser) matches. If 'pyser' is already
            # present in sys.modules, fall back to a unique private name.
            if "pyser" not in sys.modules:
                name = "pyser"
            else:
                name = "_pyser_native_" + os.path.basename(path).replace(".", "_")
            loader = importlib.machinery.ExtensionFileLoader(name, path)
            spec = importlib.util.spec_from_loader(loader.name, loader)
            module = importlib.util.module_from_spec(spec)
            loader.exec_module(module)
            sys.modules[loader.name] = module
            return module
        except Exception as e:
            errors.append((path, repr(e)))
            continue

    # Build a helpful error message containing diagnostics
    msg_lines = ["Could not find or import the compiled 'pyser' extension."]
    msg_lines.append("Looked for module name 'pyser' and shared libraries in package directory.")
    msg_lines.append("Errors:")
    for what, err in errors:
        msg_lines.append(f" - {what}: {err}")

    raise ImportError("\n".join(msg_lines))


_native = None
try:
    _native = _load_native()
except Exception as e:
    # Defer raising until the functions are actually used; allow import-time introspection.
    _native = None
    _import_error = e
else:
    _import_error = None


def _ensure_native():
    if _native is None:
        raise _import_error
    return _native


# Optional sanitizer: clear __reduce__/__reduce_ex__ on callables temporarily
# when serializing to avoid reliance on marshal-based reduce behavior. This is
# an interim mitigation until the C++ side implements a full code-object
# serializer (see cpp/MARSHAL_PORT_PLAN.md). The sanitizer is opt-in and can
# be enabled by setting the environment variable PYSER_SANITIZE_REDUCE=1.


def _iter_functions(obj, seen=None):
    # Simple recursive walker that yields function objects nested in common
    # container types. Not exhaustive; used only by the sanitizer.
    if seen is None:
        seen = set()
    oid = id(obj)
    if oid in seen:
        return
    seen.add(oid)
    import types

    if isinstance(obj, types.FunctionType):
        yield obj
        # inspect closure cells for nested functions
        if obj.__closure__:
            for cell in obj.__closure__:
                try:
                    for f in _iter_functions(cell.cell_contents, seen):
                        yield f
                except Exception:
                    continue
    elif isinstance(obj, (list, tuple, set, frozenset)):
        for item in obj:
            for f in _iter_functions(item, seen):
                yield f
    elif isinstance(obj, dict):
        for k, v in obj.items():
            for f in _iter_functions(k, seen):
                yield f
            for f in _iter_functions(v, seen):
                yield f


@contextlib.contextmanager
def _temp_clear_reduce(obj):
    enabled = os.environ.get("PYSER_SANITIZE_REDUCE") == "1"
    if not enabled:
        yield
        return
    backups = []
    import types

    for f in _iter_functions(obj):
        # Only clear user-defined function attributes if present
        for attr in ("__reduce__", "__reduce_ex__"):
            if hasattr(f, attr):
                backups.append((f, attr, getattr(f, attr)))
                try:
                    delattr(f, attr)
                except Exception:
                    try:
                        setattr(f, attr, None)
                    except Exception:
                        # give up silently; sanitizer is best-effort
                        pass
    try:
        yield
    finally:
        for f, attr, val in backups:
            try:
                setattr(f, attr, val)
            except Exception:
                pass


# Exposed API (thin wrappers)


def serialize(obj: Any) -> bytes:
    """Serialize a Python object to bytes using the native pyser extension."""
    mod = _ensure_native()
    with _temp_clear_reduce(obj):
        return mod.serialize(obj)


def deserialize(data: bytes) -> Any:
    """Deserialize bytes into a Python object using the native pyser extension."""
    mod = _ensure_native()
    return mod.deserialize(data)


def dumps(obj: Any) -> bytes:
    """Alias for serialize(obj)."""
    return serialize(obj)


def loads(data: bytes) -> Any:
    """Alias for deserialize(data)."""
    return deserialize(data)


def dump(obj: Any, filename: str) -> None:
    """Serialize object and write to file (alias for serialize_to_file)."""
    mod = _ensure_native()
    # Some compiled modules provide serialize_to_file
    if hasattr(mod, "serialize_to_file"):
        with _temp_clear_reduce(obj):
            return mod.serialize_to_file(obj, filename)
    # Fallback: write bytes
    data = serialize(obj)
    with open(filename, "wb") as f:
        f.write(data)


def load(filename: str) -> Any:
    """Deserialize object from file (alias for deserialize_from_file)."""
    mod = _ensure_native()
    if hasattr(mod, "deserialize_from_file"):
        return mod.deserialize_from_file(filename)
    # Fallback: read bytes and deserialize
    with open(filename, "rb") as f:
        data = f.read()
    return deserialize(data)


# Provide backwards-compatible names
serialize_to_file = dump
deserialize_from_file = load

# Version
from ._version import __version__

# Clean up namespace
# Only delete names that are actually defined to avoid NameError during import-time failures
for _name in ("importlib", "importlib.util", "importlib.machinery"):
    if _name in globals():
        try:
            del globals()[_name]
        except Exception:
            pass
