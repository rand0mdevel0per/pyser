Marshal port plan

Goal
----
Replace usage of Python's marshal for function code object serialization with a
pure C/C++ implementation that reconstructs CodeType fields and produces a
serialized representation compatible with our deserializer.

Why
---
- Removes dependency on Python's marshal which may have security or portability
  concerns.
- Allows finer control over code object serialization and potential
  cross-version compatibility handling.

Approach
--------
1. Understand current marshal usage and the structure of code objects stored in
   node.meta.func_code (base64 marshalled).
2. Implement a C++ serializer that visits code object attributes (co_code,
   co_consts, co_names, co_varnames, co_freevars, co_cellvars, flags, argcounts,
   filename, name, lnotab/lines) and encodes them into a compact JSON/binary
   representation we control.
3. Replace marshal.dumps in serialize_function with the new serializer and
   make deserialize_function decode our representation into a PyCodeObject
   using the C-API (PyCode_New or PyCode_NewWithPosOnlyArgs depending on
   availability).
4. Add comprehensive tests covering different kinds of functions, closures,
   default args, annotations, co_flags, and freevars/cellvars.

Risks and mitigations
---------------------
- Complexity of code object variants across Python versions. Mitigation: start
  by supporting the Python version used for development (currently the
  interpreter that builds this extension) and expand later.
- Many edge cases (bytecode formats, lnotab vs co_lines). Mitigation: reuse
  Python C-API constructors and keep the representation minimal.

Next steps
----------
- Create a working branch (done).
- Add a minimal serializer that encodes co_code and co_consts (start small).
- Iterate on tests, expanding coverage and fixing issues as they arise.


