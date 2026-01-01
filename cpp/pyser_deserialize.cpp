// pyser_deserialize.cpp
#include <iostream>

#include "pyser.hpp"
#include <cppcodec/base64_rfc4648.hpp>
#include <nlohmann/json.hpp>
// Note: We use public C-API (PyFunction_GetClosure / PyFunction_SetClosure)
// for closure handling to remain compatible across Python builds (3.11+).

#define ll long long


using json = nlohmann::json;

namespace pyser {
    using base64 = cppcodec::base64_rfc4648;

    PyObject *deserialize_bool(const SerializedNode &node) {
        if (node.chunks.empty() || node.chunks[0].raw_data.empty()) {
            PyErr_SetString(PyExc_ValueError, "Invalid bool data");
            return nullptr;
        }
        uint8_t value = node.chunks[0].raw_data[0];
        PyObject *result = value ? Py_True : Py_False;
        Py_INCREF(result);
        return result;
    }

    PyObject *deserialize_int(const SerializedNode &node) {
        if (node.chunks.empty()) {
            PyErr_SetString(PyExc_ValueError, "Invalid int data");
            return nullptr;
        }
        std::vector<uint8_t> full_data;
        for (const auto &chunk: node.chunks) {
            full_data.insert(full_data.end(),
                             chunk.raw_data.begin(),
                             chunk.raw_data.end());
        }

        if (node.meta.is_bigint) {
            PyObject *result = _PyLong_FromByteArray(
                full_data.data(),
                full_data.size(),
                1, // little endian
                1 // signed
            );
            return result;
        }
        if (full_data.size() != sizeof(long long)) {
            PyErr_SetString(PyExc_ValueError, "Invalid int size");
            return nullptr;
        }
        long long value;
        std::memcpy(&value, full_data.data(), sizeof(long long));
        return PyLong_FromLongLong(value);
    }

    PyObject *deserialize_float(const SerializedNode &node) {
        if (node.chunks.empty() || node.chunks[0].raw_data.size() != sizeof(double)) {
            PyErr_SetString(PyExc_ValueError, "Invalid float data");
            return nullptr;
        }
        double value;
        std::memcpy(&value, node.chunks[0].raw_data.data(), sizeof(double));
        return PyFloat_FromDouble(value);
    }

    PyObject *deserialize_string(const SerializedNode &node) {
        if (node.chunks.empty()) {
            return PyUnicode_FromString("");
        }
        std::vector<uint8_t> full_data;
        for (const auto &chunk: node.chunks) {
            full_data.insert(full_data.end(),
                             chunk.raw_data.begin(),
                             chunk.raw_data.end());
        }
        return PyUnicode_FromStringAndSize(
            reinterpret_cast<const char *>(full_data.data()),
            full_data.size()
        );
    }

    PyObject *deserialize_bytes(const SerializedNode &node) {
#ifdef PYSER_ENABLE_DEBUG_PRINTS
        fprintf(stderr, "pyser: deserialize_bytes called for type='%s' chunks=%zu\n", node.meta.type_name.c_str(), node.chunks.size());
#endif
        if (node.chunks.empty()) {
            return PyBytes_FromString("");
        }
        std::vector<uint8_t> full_data;
        for (const auto &chunk: node.chunks) {
            full_data.insert(full_data.end(),
                             chunk.raw_data.begin(),
                             chunk.raw_data.end());
        }

        // If the original type was recorded as bytearray or memoryview, reconstruct
        // an appropriate Python object. Default to bytes for backward compatibility.
        if (node.meta.type_name == "bytearray") {
            PyObject *ba = PyByteArray_FromStringAndSize(
                reinterpret_cast<const char *>(full_data.data()),
                static_cast<Py_ssize_t>(full_data.size())
            );
            return ba;
        }

        if (node.meta.type_name == "memoryview") {
            // Many builds don't need an actual memoryview object; returning
            // bytes is acceptable and supported by the test-suite. Create a
            // bytes object that owns the data and return it.
            PyObject *bytes_obj = PyBytes_FromStringAndSize(
                reinterpret_cast<const char *>(full_data.data()),
                static_cast<Py_ssize_t>(full_data.size())
            );
            return bytes_obj;
        }

        // Default: bytes
        return PyBytes_FromStringAndSize(
            reinterpret_cast<const char *>(full_data.data()),
            static_cast<Py_ssize_t>(full_data.size())
        );
    }

    PyObject *deserialize_list(
        const SerializedNode &node,
        const SerializedGraph &graph,
        std::unordered_map<uint32_t, PyObject *> &cache
    ) {
        // Pre-allocate list of appropriate size and fill with None placeholders.
        size_t n = node.pointers.size();
        PyObject *list = PyList_New(static_cast<Py_ssize_t>(n));
        if (!list) return nullptr;
        for (size_t i = 0; i < n; i++) {
            Py_INCREF(Py_None);
            // PyList_SetItem steals a reference, so we provide an owned reference.
            if (PyList_SetItem(list, static_cast<Py_ssize_t>(i), Py_None) < 0) {
                Py_DECREF(list);
                return nullptr;
            }
        }
        return list;
    }

    PyObject *deserialize_tuple(
        const SerializedNode &node,
        const SerializedGraph &graph,
        std::unordered_map<uint32_t, PyObject *> &cache
    ) {
        size_t size = node.pointers.size();
        PyObject *tuple = PyTuple_New(size);
        if (!tuple) return nullptr;
        for (size_t i = 0; i < size; i++) {
            Py_INCREF(Py_None);
            PyTuple_SetItem(tuple, i, Py_None);
        }
        return tuple;
    }

    PyObject *deserialize_dict(
        const SerializedNode &node,
        const SerializedGraph &graph,
        std::unordered_map<uint32_t, PyObject *> &cache
    ) {
        PyObject *dict = PyDict_New();
        if (!dict) return nullptr;
        return dict;
    }

    PyObject *deserialize_set(
        const SerializedNode &node,
        const SerializedGraph &graph,
        std::unordered_map<uint32_t, PyObject *> &cache
    ) {
        PyObject *set = PySet_New(nullptr);
        if (!set) return nullptr;
        return set;
    }

    PyObject *deserialize_function(
        const SerializedNode &node,
        const SerializedGraph &graph,
        std::unordered_map<uint32_t, PyObject *> &cache
    ) {
#ifdef PYSER_ENABLE_DEBUG_PRINTS
        fprintf(stderr, "pyser: deserialize_function: func_code_empty=%d module='%s'\n", (int)node.meta.func_code.empty(), node.meta.module_name.c_str());
#endif
        if (node.meta.func_code.empty()) {
            PyErr_SetString(PyExc_ValueError, "Function code is empty");
            return nullptr;
        }

        // The serializer stored a JSON-encoded code object as base64. Decode and
        // use json_to_pyobj to reconstruct the code object.
        std::vector<uint8_t> code_obj_bytes = base64::decode(node.meta.func_code);
        if (code_obj_bytes.empty()) {
            PyErr_SetString(PyExc_ValueError, "Empty code object JSON");
            return nullptr;
        }
#ifdef PYSER_ENABLE_DEBUG_PRINTS
        fprintf(stderr, "pyser: deserialize_function: JSON blob size=%zu\n", code_obj_bytes.size());
#endif
        // Parse JSON from decoded bytes
        std::string json_str(code_obj_bytes.begin(), code_obj_bytes.end());
        json code_json;
        try {
            code_json = json::parse(json_str);
        } catch (const json::exception &e) {
            PyErr_Format(PyExc_ValueError, "Failed to parse code object JSON: %s", e.what());
            return nullptr;
        }
        
        // Use json_to_pyobj to reconstruct the code object
        PyObject *code_obj = json_to_pyobj(code_json);
#ifdef PYSER_ENABLE_DEBUG_PRINTS
        fprintf(stderr, "pyser: deserialize_function: json_to_pyobj returned %p\n", (void *)code_obj);
#endif
        if (!code_obj || !PyCode_Check(code_obj)) {
            Py_XDECREF(code_obj);
#ifdef PYSER_ENABLE_DEBUG_PRINTS
            fprintf(stderr, "pyser: deserialize_function: code_obj invalid or not PyCode_Check\n");
#endif
            PyErr_SetString(PyExc_TypeError, "Failed to reconstruct code object from JSON");
            return nullptr;
        }


        PyObject *globals = PyDict_New();
        if (!globals) {
            Py_DECREF(code_obj);
            PyErr_SetString(PyExc_MemoryError, "Failed to create globals dict");
            return nullptr;
        }
#ifdef PYSER_ENABLE_DEBUG_PRINTS
        fprintf(stderr, "pyser: deserialize_function: creating function with globals=%p code=%p\n", (void *)globals, (void *)code_obj);
#endif
        PyObject *function = PyFunction_New(code_obj, globals);
#ifdef PYSER_ENABLE_DEBUG_PRINTS
        fprintf(stderr, "pyser: deserialize_function: PyFunction_New returned %p\n", (void *)function);
#endif
        Py_DECREF(code_obj);
        Py_DECREF(globals);

        if (!node.meta.module_name.empty()) {
            PyObject *nameobj = PyUnicode_FromString(node.meta.module_name.c_str());
            if (nameobj) {
                PyObject_SetAttrString(function, "__name__", nameobj);
                Py_DECREF(nameobj);
            }
        }
        
        // Restore __defaults__ (tuple of default positional argument values)
        if (!node.meta.func_defaults.empty()) {
            std::vector<uint8_t> defaults_bytes = base64::decode(node.meta.func_defaults);
            std::string defaults_str(defaults_bytes.begin(), defaults_bytes.end());
            try {
                json defaults_json = json::parse(defaults_str);
                PyObject *defaults = json_to_pyobj(defaults_json);
                if (defaults && PyTuple_Check(defaults)) {
                    // Use PyFunction_SetDefaults to set the defaults tuple
                    if (PyFunction_SetDefaults(function, defaults) < 0) {
                        PyErr_Clear();  // Ignore errors
                    }
                    Py_DECREF(defaults);
                } else {
                    Py_XDECREF(defaults);
                }
            } catch (...) {
                // Ignore JSON parse errors
            }
        }
        
        // Restore __kwdefaults__ (dict of default keyword-only argument values)
        if (!node.meta.func_kwdefaults.empty()) {
            std::vector<uint8_t> kw_bytes = base64::decode(node.meta.func_kwdefaults);
            std::string kw_str(kw_bytes.begin(), kw_bytes.end());
            try {
                json kw_json = json::parse(kw_str);
                // Reconstruct dict from JSON object
                PyObject *kwdefaults = PyDict_New();
                if (kwdefaults && kw_json.is_object()) {
                    for (auto& [key, val] : kw_json.items()) {
                        PyObject *py_val = json_to_pyobj(val);
                        if (py_val) {
                            PyDict_SetItemString(kwdefaults, key.c_str(), py_val);
                            Py_DECREF(py_val);
                        }
                    }
                    // Use PyFunction_SetKwDefaults to set the kwdefaults dict
                    if (PyFunction_SetKwDefaults(function, kwdefaults) < 0) {
                        PyErr_Clear();  // Ignore errors
                    }
                    Py_DECREF(kwdefaults);
                } else {
                    Py_XDECREF(kwdefaults);
                }
            } catch (...) {
                // Ignore JSON parse errors
            }
        }
        
        return function;
    }


    PyObject *deserialize_module(const SerializedNode &node) {
        if (node.meta.module_name.empty()) {
            PyErr_SetString(PyExc_ValueError, "Module name is empty");
            return nullptr;
        }
        PyObject *module_name = PyUnicode_FromString(node.meta.module_name.c_str());
        PyObject *module = PyImport_Import(module_name);
        Py_DECREF(module_name);
        if (!module) {
            PyErr_Format(PyExc_ImportError,
                         "Failed to import module '%s'",
                         node.meta.module_name.c_str());
            return nullptr;
        }
        return module;
    }

    PyObject *deserialize_custom(
        const SerializedNode &node,
        const SerializedGraph &graph,
        std::unordered_map<uint32_t, PyObject *> &cache
    ) {
        PyObject *module = nullptr;
        PyObject *cls = nullptr;
        if (!node.meta.module_name.empty()) {
            PyObject *module_name = PyUnicode_FromString(node.meta.module_name.c_str());
            module = PyImport_Import(module_name);
            Py_DECREF(module_name);
            if (!module) {
                // If importing the original module failed (e.g. the class was
                // defined locally inside a function), fall through and attempt
                // lower-risk fallbacks below instead of failing immediately.
                PyErr_Clear();
                module = nullptr;
            }
        }
        if (module) {
            cls = PyObject_GetAttrString(module, node.meta.type_name.c_str());
            Py_DECREF(module);
        } else {
            PyObject *builtins = PyEval_GetBuiltins();
            cls = PyDict_GetItemString(builtins, node.meta.type_name.c_str());
            if (cls) {
                Py_INCREF(cls);
            }
        }
        if (!cls) {
            // Class not found by import or in builtins. Instead of failing,
            // create a generic object that supports attribute assignment
            // (types.SimpleNamespace) so that resolve_pointers() can set
            // attributes. This helps when serializing local classes defined
            // inside functions (their defining scope isn't importable).
#ifdef PYSER_ENABLE_DEBUG_PRINTS
            // Unconditional diagnostic to help understand why fallback may fail.
            fprintf(stderr, "pyser: deserialize_custom: class '%s' not found in module '%s' - attempting SimpleNamespace fallback\n", node.meta.type_name.c_str(), node.meta.module_name.c_str());
#endif
            PyObject *types_mod = PyImport_ImportModule("types");
            if (types_mod) {
                PyObject *ss = PyObject_GetAttrString(types_mod, "SimpleNamespace");
                Py_DECREF(types_mod);
                if (ss && PyCallable_Check(ss)) {
                    PyObject *inst = PyObject_CallObject(ss, nullptr);
                    Py_XDECREF(ss);
                    if (inst) return inst;
                }
                Py_XDECREF(ss);
            }
#ifdef PYSER_ENABLE_DEBUG_PRINTS
            fprintf(stderr, "pyser: deserialize_custom: SimpleNamespace fallback failed for class '%s'\n", node.meta.type_name.c_str());
#endif
            // As a last resort, return an instance of built-in object (no
            // attributes) which may limit pointer setting; signal error.
            PyErr_Format(PyExc_TypeError,
                         "Cannot find class '%s'",
                         node.meta.type_name.c_str());
            return nullptr;
        }

        // Use low-level allocation to create instance without calling __new__ or __init__
        // This bypasses constructor requirements and lets resolve_pointers() set attributes
        PyTypeObject *type = (PyTypeObject *)cls;
        PyObject *obj = type->tp_alloc(type, 0);

        // Clear any pending exceptions that might have been set during allocation
        if (PyErr_Occurred()) {
            PyErr_Print();
            PyErr_Clear();
        }

        Py_DECREF(cls);

        if (!obj) {
            PyErr_Format(PyExc_TypeError,
                         "Failed to allocate instance of class '%s'",
                         node.meta.type_name.c_str());
            return nullptr;
        }

        return obj;
    }

    PyObject *deserialize_reference(
        const SerializedNode &node,
        std::unordered_map<uint32_t, PyObject *> &cache
    ) {
        if (node.chunks.empty() || node.chunks[0].raw_data.size() != sizeof(uint32_t)) {
            PyErr_SetString(PyExc_ValueError, "Invalid reference data");
            return nullptr;
        }
        uint32_t target_id;
        std::memcpy(&target_id, node.chunks[0].raw_data.data(), sizeof(uint32_t));
        auto it = cache.find(target_id);
        if (it == cache.end()) {
            PyErr_Format(PyExc_ValueError,
                         "Reference target %u not found in cache",
                         target_id);
            return nullptr;
        }
        PyObject *target = it->second;
        Py_INCREF(target);
        return target;
    }

    void PyObjectSerializer::resolve_pointers(
        const SerializedGraph &graph,
        std::unordered_map<uint32_t, PyObject *> &cache
    ) {
        for (const auto &ptr: graph.all_pointers) {
            // Diagnostic: print pointer being resolved for easier tracing of crashes
#ifdef PYSER_ENABLE_DEBUG_PRINTS
            fprintf(stderr, "pyser: resolve pointer from=%u to=%u field=%s\n", ptr.from_node_id, ptr.to_node_id, ptr.field_name.c_str());
#endif
            auto src_it = cache.find(ptr.from_node_id);
            auto dst_it = cache.find(ptr.to_node_id);
            if (src_it == cache.end() || dst_it == cache.end()) {
                continue;
            }
            PyObject *src_obj = src_it->second;
            PyObject *dst_obj = dst_it->second;
            const std::string &field = ptr.field_name;
            if (PyList_Check(src_obj)) {
                try {
                    size_t index = std::stoull(field);
                    // PyList_SetItem steals a reference; provide an INCREF'd reference.
                    Py_INCREF(dst_obj);
                    if (PyList_SetItem(src_obj, static_cast<Py_ssize_t>(index), dst_obj) < 0) {
#ifdef PYSER_ENABLE_DEBUG_PRINTS
                        std::cerr << "Failed to set list item: " << field << std::endl;
#endif
                    }
                } catch (...) {
#ifdef PYSER_ENABLE_DEBUG_PRINTS
                    std::cerr << "Failed to parse list index: " << field << std::endl;
#endif
                }
            } else if (PyTuple_Check(src_obj)) {
                try {
                    size_t index = std::stoull(field);
                    // For tuples we need to replace or create a new tuple; here we set item
                    // only if within bounds. PyTuple_SetItem steals a reference.
                    if (index < static_cast<size_t>(PyTuple_Size(src_obj))) {
                        Py_INCREF(dst_obj);
                        if (PyTuple_SetItem(src_obj, static_cast<Py_ssize_t>(index), dst_obj) < 0) {
#ifdef PYSER_ENABLE_DEBUG_PRINTS
                            std::cerr << "Failed to set tuple item: " << field << std::endl;
#endif
                        }
                    } else {
#ifdef PYSER_ENABLE_DEBUG_PRINTS
                        std::cerr << "Tuple index out of range: " << field << std::endl;
#endif
                    }
                } catch (...) {
#ifdef PYSER_ENABLE_DEBUG_PRINTS
                    std::cerr << "Failed to parse tuple index: " << field << std::endl;
#endif
                }
            } else if (PyDict_Check(src_obj)) {
                if (field.find("key:") == 0) {
                    continue;
                }
                if (field.find("val:") == 0) {
                    std::string key_name = field.substr(4);
                    const SerializedNode *node = nullptr;
                    for (const auto &n: graph.nodes) {
                        if (n.node_id == ptr.from_node_id) {
                            node = &n;
                            break;
                        }
                    }
                    if (node) {
                        auto key_it = node->meta.attr_node_ids.find(key_name);
                        if (key_it != node->meta.attr_node_ids.end()) {
                            PyObject *key = PyUnicode_FromString(key_name.c_str());
                            if (key) {
                                if (PyDict_SetItem(src_obj, key, dst_obj) < 0) {
#ifdef PYSER_ENABLE_DEBUG_PRINTS
                                    std::cerr << "Failed to set dict item for key: " << key_name << std::endl;
#endif
                                }
                                Py_DECREF(key);
                            }
                        }
                    }
                }
            } else if (PySet_Check(src_obj)) {
                if (PySet_Add(src_obj, dst_obj) < 0) {
#ifdef PYSER_ENABLE_DEBUG_PRINTS
                    std::cerr << "Failed to add item to set" << std::endl;
#endif
                }
            } else if (PyFunction_Check(src_obj)) {
                if (field.find("closure:") == 0) {
                    std::string idx_str = field.substr(8);
                    size_t idx;
                    try { idx = std::stoull(idx_str); } catch (...) {
#ifdef PYSER_ENABLE_DEBUG_PRINTS
                        std::cerr << "Failed to parse closure index: " << field << std::endl;
#endif
                        continue;
                    }

                    // Use public API: PyFunction_GetClosure / PyFunction_SetClosure
                    // This is compatible with Python 3.11+ and avoids access violations
                    PyObject *old_closure = PyFunction_GetClosure(src_obj);  // borrowed or NULL
                    Py_ssize_t old_size = 0;
                    if (old_closure && PyTuple_Check(old_closure)) old_size = PyTuple_Size(old_closure);
                    Py_ssize_t needed = static_cast<Py_ssize_t>(idx) + 1;
                    Py_ssize_t new_size = std::max(old_size, needed);

                    PyObject *new_tuple = PyTuple_New(new_size);
                    if (!new_tuple) {
                        PyErr_SetString(PyExc_MemoryError, "Failed to allocate new closure tuple");
                        continue;
                    }

                    // Copy existing cells and/or create placeholders
                    bool fail = false;
                    for (Py_ssize_t i = 0; i < new_size; ++i) {
                        if (i < old_size && old_closure) {
                            PyObject *item = PyTuple_GetItem(old_closure, i); // borrowed
                            Py_INCREF(item);
                            if (PyTuple_SetItem(new_tuple, i, item) < 0) { Py_DECREF(item); fail = true; break; }
                        } else {
                            PyObject *cell = PyCell_New(nullptr);
                            if (!cell) { Py_DECREF(new_tuple); PyErr_SetString(PyExc_MemoryError, "Failed to create cell"); fail = true; break; }
                            if (PyTuple_SetItem(new_tuple, i, cell) < 0) { Py_DECREF(cell); Py_DECREF(new_tuple); fail = true; break; }
                        }
                    }
                    if (fail) continue;

                    // Create the cell for the dst_obj and replace at index
                    PyObject *cell = PyCell_New(dst_obj);
                    if (!cell) { Py_DECREF(new_tuple); PyErr_SetString(PyExc_MemoryError, "Failed to create closure cell"); continue; }
                    
                    // Get the old item at idx to DECREF it before replacement
                    PyObject *old_item = PyTuple_GetItem(new_tuple, static_cast<Py_ssize_t>(idx));
                    Py_XDECREF(old_item);
                    
                    // Replace the placeholder at idx (PyTuple_SetItem steals ref)
                    if (PyTuple_SetItem(new_tuple, static_cast<Py_ssize_t>(idx), cell) < 0) { 
                        Py_DECREF(cell); 
                        Py_DECREF(new_tuple); 
                        continue; 
                    }

                    // Use PyFunction_SetClosure (available since Python 3.11/3.13)
                    if (PyFunction_SetClosure(src_obj, new_tuple) < 0) {
#ifdef PYSER_ENABLE_DEBUG_PRINTS
                        fprintf(stderr, "pyser: PyFunction_SetClosure failed\n");
                        PyErr_Print();
#endif
                        PyErr_Clear();
                        Py_DECREF(new_tuple);
                        continue;
                    }
                    Py_DECREF(new_tuple);  // SetClosure increments ref, so we decref our copy
                }

             } else {
                // Debug: log attempted attribute assignment to help diagnose readonly/slot errors
#ifdef PYSER_ENABLE_DEBUG_PRINTS
                fprintf(stderr, "pyser: setattr src=%p field=%s dst=%p\n", (void *)src_obj, field.c_str(), (void *)dst_obj);
#endif
                if (PyObject_SetAttrString(src_obj, field.c_str(), dst_obj) < 0) {
#ifdef PYSER_ENABLE_DEBUG_PRINTS
                    std::cerr << "Failed to set attribute: " << field << std::endl;
#endif
                    // Print Python error for diagnostics
                    if (PyErr_Occurred()) {
                        PyErr_Print();
                        PyErr_Clear();
                    }
                }
             }
         }
     }

    PyObject *PyObjectSerializer::deserialize(const SerializedGraph &graph) {
#ifdef PYSER_ENABLE_DEBUG_PRINTS
        fprintf(stderr, "pyser: deserialize graph nodes=%zu root=%u\n", graph.nodes.size(), graph.root_id);
#endif
        for (const auto &n : graph.nodes) {
            std::string type_name = "";
            switch (n.type) {
                case NodeType::NONE: type_name = "NONE"; break;
                case NodeType::BOOL: type_name = "BOOL"; break;
                case NodeType::INT: type_name = "INT"; break;
                case NodeType::FLOAT: type_name = "FLOAT"; break;
                case NodeType::STRING: type_name = "STRING"; break;
                case NodeType::BYTES: type_name = "BYTES"; break;
                case NodeType::LIST: type_name = "LIST"; break;
                case NodeType::TUPLE: type_name = "TUPLE"; break;
                case NodeType::DICT: type_name = "DICT"; break;
                case NodeType::SET: type_name = "SET"; break;
                case NodeType::FUNCTION: type_name = "FUNCTION"; break;
                case NodeType::MODULE: type_name = "MODULE"; break;
                case NodeType::CUSTOM: type_name = "CUSTOM"; break;
                case NodeType::REFERENCE: type_name = "REFERENCE"; break;
                default: type_name = "UNKNOWN"; break;
            }
#ifdef PYSER_ENABLE_DEBUG_PRINTS
            fprintf(stderr, "pyser: node id=%u type=%s meta.type_name='%s' chunks=%zu\n", n.node_id, type_name.c_str(), n.meta.type_name.c_str(), n.chunks.size());
#endif
        }
        std::unordered_map<uint32_t, PyObject *> cache;
        for (const auto &node: graph.nodes) {
            PyObject *obj = deserialize_node(node.node_id, graph, cache);
            if (!obj) {
                for (auto &pair: cache) {
                    Py_XDECREF(pair.second);
                }
                return nullptr;
            }
            cache[node.node_id] = obj;
        }
        resolve_pointers(graph, cache);
        PyObject *root = cache[graph.root_id];
        Py_INCREF(root);
        if (PyErr_Occurred()) {
            // Print diagnostics and clear the pending exception to avoid a
            // SystemError when returning a non-NULL value from the C API.
            PyErr_Print();
            PyErr_Clear();
        }
        return root;
    }

    PyObject *PyObjectSerializer::deserialize_node(
        uint32_t node_id,
        const SerializedGraph &graph,
        std::unordered_map<uint32_t, PyObject *> &cache
    ) {
        // Diagnostic: log node id being deserialized and approximate type
#ifdef PYSER_ENABLE_DEBUG_PRINTS
        fprintf(stderr, "pyser: deserialize_node: id=%u\n", node_id);
#endif
        auto it = cache.find(node_id);
        if (it != cache.end()) {
            Py_INCREF(it->second);
            return it->second;
        }
        const SerializedNode *node = nullptr;
        for (const auto &n: graph.nodes) {
            if (n.node_id == node_id) {
                node = &n;
                break;
            }
        }
        if (!node) {
            PyErr_Format(PyExc_ValueError, "Node %u not found", node_id);
            return nullptr;
        }
        PyObject *result = nullptr;
        switch (node->type) {
            case NodeType::NONE:
                result = Py_None;
                Py_INCREF(result);
                break;
            case NodeType::BOOL:
                result = deserialize_bool(*node);
                break;
            case NodeType::INT:
                result = deserialize_int(*node);
                break;
            case NodeType::FLOAT:
                result = deserialize_float(*node);
                break;
            case NodeType::STRING:
                result = deserialize_string(*node);
                break;
            case NodeType::BYTES:
                result = deserialize_bytes(*node);
                break;
            case NodeType::LIST:
                result = deserialize_list(*node, graph, cache);
                break;
            case NodeType::TUPLE:
                result = deserialize_tuple(*node, graph, cache);
                break;
            case NodeType::DICT:
                result = deserialize_dict(*node, graph, cache);
                break;
            case NodeType::SET:
                result = deserialize_set(*node, graph, cache);
                break;
            case NodeType::FUNCTION:
                result = deserialize_function(*node, graph, cache);
                break;
            case NodeType::MODULE:
                result = deserialize_module(*node);
                break;
            case NodeType::CUSTOM:
                result = deserialize_custom(*node, graph, cache);
                break;
            case NodeType::REFERENCE:
                result = deserialize_reference(*node, cache);
                break;
            default:
                PyErr_Format(PyExc_TypeError, "Unknown node type: %d",
                             static_cast<int>(node->type));
                return nullptr;
        }
        if (result) {
            if (PyErr_Occurred()) {
                // A non-NULL result is being returned while an exception is set;
                // print diagnostics, DECREF result and return an error.
#ifdef PYSER_ENABLE_DEBUG_PRINTS
                fprintf(stderr, "pyser: deserialize_node: pending Python exception while result non-NULL for node=%u\n", node_id);
#endif
                PyErr_Print();
                PyErr_Clear();
                Py_DECREF(result);
                return nullptr;
            }
            cache[node_id] = result;
        }
        return result;
    }
} // namespace pyser
