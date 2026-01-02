// pyser.cpp
#include "pyser.hpp"
#include <openssl/sha.h>
#include <nlohmann/json.hpp>
#include <cppcodec/base64_rfc4648.hpp>
#include <Python.h>

namespace pyser {
    using json = nlohmann::json;
    using base64 = cppcodec::base64_rfc4648;


    std::vector<DataChunk> PyObjectSerializer::create_chunks(
        const std::vector<uint8_t> &data
    ) {
        std::vector<DataChunk> chunks;
        size_t offset = 0;

        while (offset < data.size()) {
            DataChunk chunk;
            chunk.chunk_id = next_chunk_id_++;
            size_t chunk_size = std::min(CHUNK_SIZE, data.size() - offset);
            chunk.raw_data.assign(
                data.begin() + offset,
                data.begin() + offset + chunk_size
            );
            chunk.original_size = chunk_size;
            chunk.base64_data = base64::encode(chunk.raw_data);
            chunk.sha256_hash = compute_sha256(chunk.raw_data);
            chunks.push_back(chunk);
            offset += chunk_size;
        }

        return chunks;
    }

    std::string PyObjectSerializer::compute_sha256(const std::vector<uint8_t> &data) {
        unsigned char hash[SHA256_DIGEST_LENGTH];
        SHA256(data.data(), data.size(), hash);
        char hex[SHA256_DIGEST_LENGTH * 2 + 1];
        for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
            sprintf(hex + i * 2, "%02x", hash[i]);
        }
        hex[SHA256_DIGEST_LENGTH * 2] = '\0';

        return {hex};
    }

    SerializedNode PyObjectSerializer::serialize_bigint(PyObject *obj) {
        SerializedNode node;
        node.type = NodeType::INT;
        node.meta.type_name = "int";
        node.meta.is_bigint = true;
        // Diagnostic: ensure obj is a PyLong
        if (!PyLong_Check(obj)) {
#ifdef PYSER_ENABLE_DEBUG_PRINTS
            fprintf(stderr, "pyser: serialize_bigint called with non-long type: %s\n", Py_TYPE(obj)->tp_name);
#endif
        } else {
#ifdef PYSER_ENABLE_DEBUG_PRINTS
            fprintf(stderr, "pyser: serialize_bigint called on PyLong\n");
#endif
        }
        size_t n_bits = _PyLong_NumBits(obj);
        size_t n_bytes = (n_bits + 7) / 8;
        std::vector<uint32_t> digits;
        auto *long_obj = reinterpret_cast<PyLongObject *>(obj);
        std::vector<uint8_t> raw_data(n_bytes);
        // Python 3.13+ added a 6th parameter (with_exception) to _PyLong_AsByteArray
#if PY_VERSION_HEX >= 0x030D0000
        _PyLong_AsByteArray(long_obj, raw_data.data(), n_bytes, 1, 1, 1);
#else
        _PyLong_AsByteArray(long_obj, raw_data.data(), n_bytes, 1, 1);
#endif
        node.meta.bigint_num_digits = n_bytes;
        node.chunks = create_chunks(raw_data);
        return node;

    }

    SerializedNode PyObjectSerializer::serialize_int(PyObject *obj) {
        SerializedNode node;
        node.type = NodeType::INT;
        node.meta.type_name = "int";
        node.meta.refcount = 1;
        // Diagnostic: print incoming type info
        if (obj) {
#ifdef PYSER_ENABLE_DEBUG_PRINTS
            fprintf(stderr, "pyser: serialize_int called on type: %s\n", Py_TYPE(obj)->tp_name);
#endif
        }
        int overflow;
        long long value = PyLong_AsLongLongAndOverflow(obj, &overflow);
        if (overflow != 0) {
            return serialize_bigint(obj);
        }
        node.meta.is_bigint = false;
        std::vector<uint8_t> raw_data(sizeof(long long));
        std::memcpy(raw_data.data(), &value, sizeof(long long));
        node.chunks = create_chunks(raw_data);
        return node;
    }

    SerializedNode PyObjectSerializer::serialize_string(PyObject *obj) {
        SerializedNode node;
        node.type = NodeType::STRING;
        node.meta.type_name = "str";
        node.meta.refcount = 1;
        node.meta.has_dict = false;
        Py_ssize_t size;
        const char *data = PyUnicode_AsUTF8AndSize(obj, &size);
        std::vector<uint8_t> raw_data(data, data + size);
        node.meta.total_size = size;
        node.chunks = create_chunks(raw_data);
        return node;
    }

    // Update signatures to accept owner node id for pointer population
    SerializedNode PyObjectSerializer::serialize_container(
        PyObject *obj,
        NodeType type,
        SerializedGraph &graph,
        std::unordered_map<PyObject *, uint32_t> &visited,
        int depth,
        uint32_t owner_node_id
    ) {
        SerializedNode node;
        node.type = type;
        node.meta.refcount = 1;
        Py_ssize_t size;
        if (type == NodeType::LIST) {
            size = PyList_Size(obj);
            node.meta.type_name = "list";
        } else if (type == NodeType::TUPLE) {
            size = PyTuple_Size(obj);
            node.meta.type_name = "tuple";
        } else if (type == NodeType::SET) {
            size = PySet_Size(obj);
            node.meta.type_name = "set";
        } else {
            PyErr_SetString(PyExc_TypeError, "Unsupported container type");
            node.meta.has_dict = false;
            node.meta.total_size = 0;
            return node;
        }
        node.meta.has_dict = false;
        node.meta.total_size = size;
        for (Py_ssize_t i = 0; i < size; i++) {
            PyObject *item = nullptr;
            if (type == NodeType::LIST) {
                item = PyList_GetItem(obj, i);
            } else if (type == NodeType::TUPLE) {
                item = PyTuple_GetItem(obj, i);
            } else {
                PyObject *iter = PyObject_GetIter(obj);
                for (Py_ssize_t j = 0; j <= i; j++) {
                    item = PyIter_Next(iter);
                }
                Py_DECREF(iter);
            }
            if (!item) continue;
            uint32_t child_id = serialize_recursive(item, graph, visited, depth + 1);
            if (child_id == UINT32_MAX) {
                return node;
            }
            PointerInfo ptr;
            ptr.from_node_id = owner_node_id;
            ptr.from_chunk_id = 0;
            ptr.offset = i * sizeof(void *);
            ptr.to_node_id = child_id;
            ptr.field_name = std::to_string(i);
            node.pointers.push_back(ptr);
            graph.all_pointers.push_back(ptr);
        }
        return node;
    }

    SerializedNode PyObjectSerializer::serialize_float(PyObject *obj) {
        SerializedNode node;
        node.type = NodeType::FLOAT;
        node.meta.type_name = "float";
        node.meta.refcount = 1;

        if (!PyFloat_Check(obj)) {
            PyErr_SetString(PyExc_TypeError, "Expected a float object");
            return node;
        }

        double value = PyFloat_AsDouble(obj);
        std::vector raw_data(reinterpret_cast<uint8_t *>(&value),
                             reinterpret_cast<uint8_t *>(&value) + sizeof(double));
        node.chunks = create_chunks(raw_data);

        return node;
    }

    SerializedNode PyObjectSerializer::serialize_bytes(PyObject *obj) {
        SerializedNode node;
        node.type = NodeType::BYTES;
        node.meta.type_name = "bytes";
        node.meta.refcount = 1;

        // Support multiple bytes-like objects: bytes, bytearray, memoryview, and
        // any object that supports the buffer protocol. We copy the buffer
        // contents into a local vector so it's safe to chunk and transport.
        std::vector<uint8_t> raw_data;

        // bytes
        if (PyBytes_Check(obj)) {
            char *data = nullptr;
            Py_ssize_t size = 0;
            if (PyBytes_AsStringAndSize(obj, &data, &size) != 0) {
                PyErr_SetString(PyExc_TypeError, "Failed to get bytes data");
                return node;
            }
            raw_data.assign(reinterpret_cast<uint8_t *>(data), reinterpret_cast<uint8_t *>(data) + size);
            node.meta.type_name = "bytes";
        }
        // bytearray
        else if (PyByteArray_Check(obj)) {
            char *data = PyByteArray_AsString(obj);
            Py_ssize_t size = PyByteArray_Size(obj);
            raw_data.assign(reinterpret_cast<uint8_t *>(data), reinterpret_cast<uint8_t *>(data) + size);
            node.meta.type_name = "bytearray";
        }
        // memoryview or other buffer-supporting objects
        else if (PyMemoryView_Check(obj) || PyObject_CheckBuffer(obj)) {
            Py_buffer view;
            // Request a readonly contiguous buffer view for simplicity
            if (PyObject_GetBuffer(obj, &view, PyBUF_CONTIG_RO) != 0) {
                PyErr_SetString(PyExc_TypeError, "Failed to get buffer from object");
                return node;
            }
            raw_data.assign(reinterpret_cast<uint8_t *>(view.buf), reinterpret_cast<uint8_t *>(view.buf) + view.len);
            if (PyMemoryView_Check(obj)) {
                node.meta.type_name = "memoryview";
            } else {
                node.meta.type_name = "buffer";
            }
            PyBuffer_Release(&view);
        } else {
            PyErr_SetString(PyExc_TypeError, "Expected a bytes-like object");
            return node;
        }

        node.chunks = create_chunks(raw_data);

        return node;
    }


    SerializedNode PyObjectSerializer::serialize_dict(
        PyObject *obj,
        SerializedGraph &graph,
        std::unordered_map<PyObject *, uint32_t> &visited,
        int depth,
        uint32_t owner_node_id
    ) {
        SerializedNode node;
        node.type = NodeType::DICT;
        node.meta.type_name = "dict";
        node.meta.refcount = 1;
        node.meta.has_dict = true;
        PyObject *key, *value;
        Py_ssize_t pos = 0;
        while (PyDict_Next(obj, &pos, &key, &value)) {
            // Key
            uint32_t key_id = serialize_recursive(key, graph, visited, depth + 1);
            if (key_id == UINT32_MAX) return node;
            // Value
            uint32_t value_id = serialize_recursive(value, graph, visited, depth + 1);
            if (value_id == UINT32_MAX) return node;
            PyObject *key_str = PyObject_Str(key);
            const char *key_cstr = PyUnicode_AsUTF8(key_str);
            std::string key_name(key_cstr);
            Py_DECREF(key_str);
            node.meta.attr_names.push_back(key_name);
            node.meta.attr_node_ids[key_name] = value_id;
            PointerInfo ptr_key, ptr_val;
            ptr_key.from_node_id = owner_node_id;
            ptr_key.from_chunk_id = 0;
            ptr_key.offset = 0;
            ptr_key.to_node_id = key_id;
            ptr_key.field_name = "key:" + key_name;
            ptr_val.from_node_id = owner_node_id;
            ptr_val.from_chunk_id = 0;
            ptr_val.offset = 0;
            ptr_val.to_node_id = value_id;
            ptr_val.field_name = "val:" + key_name;
            node.pointers.push_back(ptr_key);
            node.pointers.push_back(ptr_val);
            graph.all_pointers.push_back(ptr_key);
            graph.all_pointers.push_back(ptr_val);
        }
        return node;
    }

    SerializedNode PyObjectSerializer::serialize_function(
        PyObject *obj,
        SerializedGraph &graph,
        std::unordered_map<PyObject *, uint32_t> &visited,
        int depth,
        uint32_t owner_node_id
    ) {
        SerializedNode node;
        node.type = NodeType::FUNCTION;
        node.meta.type_name = "function";
        node.meta.refcount = 1;
        PyObject *name = PyObject_GetAttrString(obj, "__name__");
        if (name) {
            node.meta.module_name = PyUnicode_AsUTF8(name);
            Py_DECREF(name);
        }
        PyObject *code_obj = PyObject_GetAttrString(obj, "__code__");
        if (code_obj) {
            PyObject *code_bytes = PyObject_GetAttrString(code_obj, "co_code");
            PyObject *consts = PyObject_GetAttrString(code_obj, "co_consts");
            PyObject *names = PyObject_GetAttrString(code_obj, "co_names");
            PyObject *varnames = PyObject_GetAttrString(code_obj, "co_varnames");
            PyObject *filename = PyObject_GetAttrString(code_obj, "co_filename");
            PyObject *name_co = PyObject_GetAttrString(code_obj, "co_name");
            PyObject *lnotab = PyObject_GetAttrString(code_obj, "co_lnotab");
            PyObject *freevars = PyObject_GetAttrString(code_obj, "co_freevars");
            PyObject *cellvars = PyObject_GetAttrString(code_obj, "co_cellvars");
            PyObject *argcount = PyObject_GetAttrString(code_obj, "co_argcount");
            PyObject *posonlyargcount = PyObject_GetAttrString(code_obj, "co_posonlyargcount");
            PyObject *kwonlyargcount = PyObject_GetAttrString(code_obj, "co_kwonlyargcount");
            PyObject *nlocals = PyObject_GetAttrString(code_obj, "co_nlocals");
            PyObject *stacksize = PyObject_GetAttrString(code_obj, "co_stacksize");
            PyObject *flags = PyObject_GetAttrString(code_obj, "co_flags");
            PyObject *firstlineno = PyObject_GetAttrString(code_obj, "co_firstlineno");
            PyObject *qualname = PyObject_GetAttrString(obj, "__qualname__");
            if (code_bytes && PyBytes_Check(code_bytes)) {
                Py_ssize_t size = PyBytes_Size(code_bytes);
                json j_data{};
                const char *data = PyBytes_AsString(code_bytes);
                //                auto j_consts = serialize(consts);
                //                auto j_names = serialize(names);
                //                auto j_varnames = serialize(varnames);
                //                auto j_filename = serialize(filename);
                //                auto j_name_co = serialize(name_co);
                //                auto j_lnotab = serialize(lnotab);
                //                auto j_freevars = serialize(freevars);
                //                auto j_cellvars = serialize(cellvars);
                //                auto j_argcount = (argcount && PyLong_Check(argcount)) ? PyLong_AsLongLong(argcount) : 0;
                //                auto j_posonlyargcount = (posonlyargcount && PyLong_Check(posonlyargcount)) ? PyLong_AsLongLong(posonlyargcount) : 0;
                //                auto j_kwonlyargcount = (kwonlyargcount && PyLong_Check(kwonlyargcount)) ? PyLong_AsLongLong(kwonlyargcount) : 0;
                //                auto j_nlocals = (nlocals && PyLong_Check(nlocals)) ? PyLong_AsLongLong(nlocals) : 0;
                //                auto j_stacksize = (stacksize && PyLong_Check(stacksize)) ? PyLong_AsLongLong(stacksize) : 0;
                //                auto j_flags = (flags && PyLong_Check(flags)) ? PyLong_AsLongLong(flags) : 0;
                //                auto j_firstlineno = (firstlineno && PyLong_Check(firstlineno)) ? PyLong_AsLongLong(firstlineno) : 0;
                //                auto j_qualname = serialize(qualname);
                //                // Code bytes may contain arbitrary binary data (not valid UTF-8).
                //                // Encode the raw bytes as base64 so the JSON string is valid.
                //                std::vector<uint8_t> code_bytes_vec(reinterpret_cast<const uint8_t *>(data),
                //                                                    reinterpret_cast<const uint8_t *>(data) + size);
                //                j_data["data"] = base64::encode(code_bytes_vec);
                //                // SerializedGraph::to_bytes() returns binary data; base64-encode
                //                // to ensure the JSON fields are valid UTF-8 strings.
                //                std::string consts_b64 = base64::encode(j_consts.to_bytes());
                //                std::string names_b64 = base64::encode(j_names.to_bytes());
                //                std::string varnames_b64 = base64::encode(j_varnames.to_bytes());
                //                std::string filename_b64 = base64::encode(j_filename.to_bytes());
                //                std::string name_b64 = base64::encode(j_name_co.to_bytes());
                //                std::string lnotab_b64 = base64::encode(j_lnotab.to_bytes());
                //                std::string freevars_b64 = base64::encode(j_freevars.to_bytes());
                //                std::string cellvars_b64 = base64::encode(j_cellvars.to_bytes());
                //                std::string qualname_b64 = base64::encode(j_qualname.to_bytes());
                //
                //                // Diagnostic logging to stderr to help debug invalid UTF-8 during JSON dump
                //                fprintf(stderr, "pyser: func code bytes=%zu consts_b64_len=%zu names_b64_len=%zu varnames_b64_len=%zu filename_b64_len=%zu name_b64_len=%zu lnotab_b64_len=%zu qualname_b64_len=%zu\n",
                //                        code_bytes_vec.size(), consts_b64.size(), names_b64.size(), varnames_b64.size(), filename_b64.size(), name_b64.size(), lnotab_b64.size(), qualname_b64.size());
                //                if (!consts_b64.empty()) fprintf(stderr, "pyser: consts_b64_prefix=%c%c%c\n", consts_b64[0], consts_b64[1], consts_b64[2]);
                //
                //                j_data["consts"] = consts_b64;
                //                j_data["names"] = names_b64;
                //                j_data["varnames"] = varnames_b64;
                //                j_data["filename"] = filename_b64;
                //                j_data["name"] = name_b64;
                //                j_data["lnotab"] = lnotab_b64;
                //                j_data["freevars"] = freevars_b64;
                //                j_data["cellvars"] = cellvars_b64;
                //                j_data["argcount"] = j_argcount;
                //                j_data["posonlyargcount"] = j_posonlyargcount;
                //                j_data["kwonlyargcount"] = j_kwonlyargcount;
                //                j_data["nlocals"] = j_nlocals;
                //                j_data["stacksize"] = j_stacksize;
                //                j_data["flags"] = j_flags;
                //                j_data["firstlineno"] = j_firstlineno;
                //                j_data["qualname"] = base64::encode(j_qualname.to_bytes());
                //                std::string dump_str;
                //                try {
                //                    dump_str = j_data.dump();
                //                } catch (const nlohmann::json::exception &ex) {
                //                    fprintf(stderr, "pyser: j_data.dump() failed: %s\n", ex.what());
                //                    fprintf(stderr, "pyser: j_data keys and sizes:\n");
                //                    if (j_data.contains("data")) fprintf(stderr, " data_len=%zu\n", j_data["data"].get<std::string>().size());
                //                    if (j_data.contains("consts")) fprintf(stderr, " consts_len=%zu\n", j_data["consts"].get<std::string>().size());
                //                    if (j_data.contains("names")) fprintf(stderr, " names_len=%zu\n", j_data["names"].get<std::string>().size());
                //                    if (j_data.contains("varnames")) fprintf(stderr, " varnames_len=%zu\n", j_data["varnames"].get<std::string>().size());
                //                    if (j_data.contains("filename")) fprintf(stderr, " filename_len=%zu\n", j_data["filename"].get<std::string>().size());
                //                    if (j_data.contains("name")) fprintf(stderr, " name_len=%zu\n", j_data["name"].get<std::string>().size());
                //                    if (j_data.contains("lnotab")) fprintf(stderr, " lnotab_len=%zu\n", j_data["lnotab"].get<std::string>().size());
                //                    if (j_data.contains("qualname")) fprintf(stderr, " qualname_len=%zu\n", j_data["qualname"].get<std::string>().size());
                //                    throw;
                //                }
                //                node.meta.func_code = base64::encode(
                //                    std::vector<uint8_t>(dump_str.begin(), dump_str.end())
                //                );
                // Use JSON-based serialization instead of Python's marshal.
                // This gives us full control and cross-version compatibility.
                // Clear any custom __reduce__ attribute on the original function
                // object before serialization. Some user code installs a __reduce__
                // hook that can influence pickling semantics. Removing it ensures
                // we operate only on the code object.
                if (PyObject_HasAttrString(obj, "__reduce__")) {
                    // Try to delete the attribute; ignore failures.
                    if (PyObject_DelAttrString(obj, "__reduce__") < 0) {
                        PyErr_Clear();
                    }
                }
                node.meta.func_code.clear();
                // Serialize code object to JSON and encode as base64
                json code_json = pyobj_to_json(code_obj);
                std::string json_str = code_json.dump();
                std::vector<uint8_t> json_bytes(json_str.begin(), json_str.end());
                node.meta.func_code = base64::encode(json_bytes);

                 // DECREF attribute objects we created
                 Py_XDECREF(argcount);
                 Py_XDECREF(posonlyargcount);
                 Py_XDECREF(kwonlyargcount);
                 Py_XDECREF(nlocals);
                 Py_XDECREF(stacksize);
                 Py_XDECREF(flags);
                 Py_XDECREF(firstlineno);
                 Py_XDECREF(qualname);
            }
            Py_XDECREF(code_bytes);
            Py_XDECREF(consts);
            Py_XDECREF(names);
            Py_XDECREF(varnames);
            Py_XDECREF(filename);
            Py_XDECREF(name_co);
            Py_XDECREF(lnotab);
            Py_XDECREF(freevars);
            Py_XDECREF(cellvars);
            Py_XDECREF(code_obj);
        }
        PyObject *closure = PyObject_GetAttrString(obj, "__closure__");
        if (closure && closure != Py_None) {
            Py_ssize_t n_cells = PyTuple_Size(closure);
            for (Py_ssize_t i = 0; i < n_cells; i++) {
                PyObject *cell = PyTuple_GetItem(closure, i);
                if (PyObject *cell_contents = PyObject_GetAttrString(cell, "cell_contents")) {
                    uint32_t cell_id = serialize_recursive(
                        cell_contents, graph, visited, depth + 1
                    );
                    PointerInfo ptr;
                    ptr.from_node_id = owner_node_id;
                    ptr.from_chunk_id = 0;
                    ptr.offset = 0;
                    ptr.to_node_id = cell_id;
                    ptr.field_name = "closure:" + std::to_string(i);
                    node.pointers.push_back(ptr);
                    graph.all_pointers.push_back(ptr);
                    Py_DECREF(cell_contents);
                }
            }
        }
        Py_XDECREF(closure);
        
        // Serialize __defaults__ (tuple of default positional argument values)
        PyObject *defaults = PyObject_GetAttrString(obj, "__defaults__");
        if (defaults && defaults != Py_None && PyTuple_Check(defaults)) {
            json defaults_json = pyobj_to_json(defaults);
            std::string defaults_str = defaults_json.dump();
            std::vector<uint8_t> defaults_bytes(defaults_str.begin(), defaults_str.end());
            node.meta.func_defaults = base64::encode(defaults_bytes);
        }
        Py_XDECREF(defaults);
        
        // Serialize __kwdefaults__ (dict of default keyword-only argument values)
        PyObject *kwdefaults = PyObject_GetAttrString(obj, "__kwdefaults__");
        if (kwdefaults && kwdefaults != Py_None && PyDict_Check(kwdefaults)) {
            // Convert dict to a list of [key, value] pairs for JSON serialization
            json kw_json = json::object();
            PyObject *key, *value;
            Py_ssize_t pos = 0;
            while (PyDict_Next(kwdefaults, &pos, &key, &value)) {
                const char *key_str = PyUnicode_AsUTF8(key);
                if (key_str) {
                    kw_json[key_str] = pyobj_to_json(value);
                }
            }
            std::string kw_str = kw_json.dump();
            std::vector<uint8_t> kw_bytes(kw_str.begin(), kw_str.end());
            node.meta.func_kwdefaults = base64::encode(kw_bytes);
        }
        Py_XDECREF(kwdefaults);
        
        return node;

    }

    SerializedNode PyObjectSerializer::serialize_module(PyObject *obj) {
        SerializedNode node;
        node.type = NodeType::MODULE;
        node.meta.type_name = "module";
        node.meta.refcount = 1;
        node.meta.total_size = 0;
        node.meta.has_dict = false;
        PyObject *name = PyObject_GetAttrString(obj, "__name__");
        if (name) {
            node.meta.module_name = PyUnicode_AsUTF8(name);
            Py_DECREF(name);
        }
        return node;
    }

    SerializedNode PyObjectSerializer::serialize_custom(
        PyObject *obj,
        SerializedGraph &graph,
        std::unordered_map<PyObject *, uint32_t> &visited,
        int depth,
        uint32_t owner_node_id
    ) {
        SerializedNode node;
        node.type = NodeType::CUSTOM;
        node.meta.refcount = 1;
        PyTypeObject *type = Py_TYPE(obj);
        node.meta.type_name = type->tp_name;
        PyObject *module = PyObject_GetAttrString(reinterpret_cast<PyObject *>(type), "__module__");
        if (module && PyUnicode_Check(module)) {
            node.meta.module_name = PyUnicode_AsUTF8(module);
        }
        node.meta.total_size = 0;
        node.meta.has_dict = false;
        Py_XDECREF(module);
        if (PyObject_HasAttrString(obj, "__dict__")) {
            PyObject *dict = PyObject_GetAttrString(obj, "__dict__");
            if (dict && PyDict_Check(dict)) {
                node.meta.has_dict = true;
                PyObject *key, *value;
                Py_ssize_t pos = 0;
                while (PyDict_Next(dict, &pos, &key, &value)) {
                    if (!PyUnicode_Check(key)) continue;
                    const char *attr_name = PyUnicode_AsUTF8(key);
                    uint32_t value_id = serialize_recursive(value, graph, visited, depth + 1);
                    if (value_id != UINT32_MAX) {
                        node.meta.attr_names.emplace_back(attr_name);
                        node.meta.attr_node_ids[attr_name] = value_id;
                        PointerInfo ptr;
                        ptr.from_node_id = owner_node_id;
                        ptr.from_chunk_id = 0;
                        ptr.offset = 0;
                        ptr.to_node_id = value_id;
                        ptr.field_name = attr_name;
                        node.pointers.push_back(ptr);
                        graph.all_pointers.push_back(ptr);
                    }
                }
            }
            Py_XDECREF(dict);
        }
        return node;
    }

    SerializedGraph PyObjectSerializer::serialize(PyObject *obj) {
        SerializedGraph graph;
        std::unordered_map<PyObject *, uint32_t> visited;
        graph.root_id = serialize_recursive(obj, graph, visited, 0);
        if (graph.root_id == UINT32_MAX) {
            throw std::runtime_error("Serialization failed");
        }
        return graph;
    }

    uint32_t PyObjectSerializer::serialize_recursive(
        PyObject *obj,
        SerializedGraph &graph,
        std::unordered_map<PyObject *, uint32_t> &visited,
        int depth
    ) {
        if (depth > MAX_DEPTH) {
            PyErr_SetString(PyExc_ValueError, "Object nesting too deep");
            return UINT32_MAX;
        }
        auto it = visited.find(obj);
        if (it != visited.end()) {
            SerializedNode ref_node;
            ref_node.node_id = next_node_id_++;
            ref_node.type = NodeType::REFERENCE;
            ref_node.meta.refcount = 0;
            uint32_t ref_target = it->second;
            std::vector<uint8_t> ref_data(sizeof(uint32_t));
            std::memcpy(ref_data.data(), &ref_target, sizeof(uint32_t));
            ref_node.chunks = create_chunks(ref_data);
            graph.nodes.push_back(ref_node);
            return ref_node.node_id;
        }
        uint32_t current_id = next_node_id_++;
        visited[obj] = current_id;
        SerializedNode node;
        // Assign the canonical node id immediately so any pointers created by
        // per-type serializers use the correct from_node_id.
        node.node_id = current_id;
        if (obj == Py_None) {
            node.type = NodeType::NONE;
            node.meta.refcount = 1;
        } else if (PyBool_Check(obj)) {
            node.type = NodeType::BOOL;
            node.meta.refcount = 1;
            uint8_t val = (obj == Py_True) ? 1 : 0;
            node.chunks = create_chunks({val});
        } else if (PyLong_Check(obj)) {
            node = serialize_int(obj);
            node.node_id = current_id;
        } else if (PyFloat_Check(obj)) {
            node = serialize_float(obj);
            node.node_id = current_id;
        } else if (PyUnicode_Check(obj)) {
            node = serialize_string(obj);
            node.node_id = current_id;
        } else if (PyBytes_Check(obj)) {
            node = serialize_bytes(obj);
            node.node_id = current_id;
        } else if (PyByteArray_Check(obj) || PyMemoryView_Check(obj) || PyObject_CheckBuffer(obj)) {
            // Handle other bytes-like objects (bytearray, memoryview, and buffer-supporting objects)
            node = serialize_bytes(obj);
            node.node_id = current_id;
        } else if (PyList_Check(obj)) {
            node = serialize_container(obj, NodeType::LIST, graph, visited, depth, current_id);
            node.node_id = current_id;
        } else if (PyTuple_Check(obj)) {
            node = serialize_container(obj, NodeType::TUPLE, graph, visited, depth, current_id);
            node.node_id = current_id;
        } else if (PyDict_Check(obj)) {
            node = serialize_dict(obj, graph, visited, depth, current_id);
            node.node_id = current_id;
        } else if (PySet_Check(obj)) {
            node = serialize_container(obj, NodeType::SET, graph, visited, depth, current_id);
            node.node_id = current_id;
        } else if (PyFunction_Check(obj)) {
            node = serialize_function(obj, graph, visited, depth, current_id);
            node.node_id = current_id;
        } else if (PyModule_Check(obj)) {
            node = serialize_module(obj);
            node.node_id = current_id;
        } else if (PyObject_HasAttrString(obj, "fileno")) {
            PyErr_SetString(PyExc_TypeError,
                            "Cannot serialize file objects. Extract file descriptor manually.");
            return UINT32_MAX;
        } else {
            node = serialize_custom(obj, graph, visited, depth, current_id);
            node.node_id = current_id;
        }
        // node.node_id was set earlier to current_id; push the finalized node.
        graph.nodes.push_back(node);
        return current_id;
    }

    // Helper: convert a PyObject (simple types and code/tuple) into a JSON value.
    // Declaration is in pyser.hpp

    json pyobj_to_json(PyObject *obj) {
        json j;
        if (obj == nullptr || obj == Py_None) {
            j["type"] = "none";
            return j;
        }
        // Check bool before int because Python bool is a subclass of int
        // and PyLong_Check returns True for booleans
        if (PyBool_Check(obj)) {
            j["type"] = "bool";
            j["value"] = (obj == Py_True);
            return j;
        }
        if (PyLong_Check(obj)) {
            long long v = PyLong_AsLongLong(obj);
            j["type"] = "int";
            j["value"] = v;
            return j;
        }
        if (PyFloat_Check(obj)) {
            double v = PyFloat_AsDouble(obj);
            j["type"] = "float";
            j["value"] = v;
            return j;
        }

        if (PyUnicode_Check(obj)) {
            const char *s = PyUnicode_AsUTF8(obj);
            j["type"] = "str";
            j["value"] = s ? s : std::string("");
            return j;
        }
        if (PyBytes_Check(obj)) {
            char *data = PyBytes_AsString(obj);
            Py_ssize_t size = PyBytes_Size(obj);
            std::vector<uint8_t> vec(reinterpret_cast<unsigned char *>(data), reinterpret_cast<unsigned char *>(data) + size);
            j["type"] = "bytes";
            j["value"] = base64::encode(vec);
            return j;
        }

        if (PyTuple_Check(obj)) {
            j["type"] = "tuple";
            json arr = json::array();
            Py_ssize_t n = PyTuple_Size(obj);
            for (Py_ssize_t i = 0; i < n; ++i) {
                PyObject *it = PyTuple_GetItem(obj, i); // borrowed
                arr.push_back(pyobj_to_json(it));
            }
            j["items"] = arr;
            return j;
        }
        if (PyCode_Check(obj)) {
            // Serialize basic code object fields
            j["type"] = "code";
            PyObject *co_code = PyObject_GetAttrString(obj, "co_code");
            if (co_code && PyBytes_Check(co_code)) {
                char *cdata = PyBytes_AsString(co_code);
                Py_ssize_t csize = PyBytes_Size(co_code);
                std::vector<uint8_t> cv(reinterpret_cast<unsigned char *>(cdata), reinterpret_cast<unsigned char *>(cdata) + csize);
                j["co_code"] = base64::encode(cv);
            } else {
                j["co_code"] = "";
            }
            // consts
            PyObject *consts = PyObject_GetAttrString(obj, "co_consts");
            if (consts && PyTuple_Check(consts)) {
                json carr = json::array();
                Py_ssize_t nc = PyTuple_Size(consts);
                for (Py_ssize_t i = 0; i < nc; ++i) {
                    PyObject *c = PyTuple_GetItem(consts, i); // borrowed
                    carr.push_back(pyobj_to_json(c));
                }
                j["co_consts"] = carr;
            } else {
                j["co_consts"] = json::array();
            }
            // names, varnames, freevars, cellvars
            auto get_str_tuple = [&](const char *name) -> json {
                PyObject *o = PyObject_GetAttrString(obj, name);
                json arr = json::array();
                if (o && PyTuple_Check(o)) {
                    Py_ssize_t nn = PyTuple_Size(o);
                    for (Py_ssize_t k = 0; k < nn; ++k) {
                        PyObject *it = PyTuple_GetItem(o, k);
                        const char *s = PyUnicode_AsUTF8(it);
                        arr.push_back(s ? s : "");
                    }
                }
                Py_XDECREF(o);
                return arr;
            };
            j["co_names"] = get_str_tuple("co_names");
            j["co_varnames"] = get_str_tuple("co_varnames");
            j["co_freevars"] = get_str_tuple("co_freevars");
            j["co_cellvars"] = get_str_tuple("co_cellvars");
            // small ints
            j["co_argcount"] = (int)PyLong_AsLong(PyObject_GetAttrString(obj, "co_argcount"));
            // posonly may not exist in older versions; try attribute safely
#ifdef Py_HAVE_NEW_CODE
            j["co_posonlyargcount"] = (int)PyLong_AsLong(PyObject_GetAttrString(obj, "co_posonlyargcount"));
#else
            PyObject *posobj = PyObject_GetAttrString(obj, "co_posonlyargcount");
            if (posobj) { j["co_posonlyargcount"] = (int)PyLong_AsLong(posobj); Py_DECREF(posobj); } else j["co_posonlyargcount"] = 0;
#endif
            j["co_kwonlyargcount"] = (int)PyLong_AsLong(PyObject_GetAttrString(obj, "co_kwonlyargcount"));
            j["co_nlocals"] = (int)PyLong_AsLong(PyObject_GetAttrString(obj, "co_nlocals"));
            j["co_stacksize"] = (int)PyLong_AsLong(PyObject_GetAttrString(obj, "co_stacksize"));
            j["co_flags"] = (int)PyLong_AsLong(PyObject_GetAttrString(obj, "co_flags"));
            PyObject *fname = PyObject_GetAttrString(obj, "co_filename");
            if (fname) { j["co_filename"] = PyUnicode_AsUTF8(fname); Py_DECREF(fname); } else j["co_filename"] = "";
            PyObject *cname = PyObject_GetAttrString(obj, "co_name");
            if (cname) { j["co_name"] = PyUnicode_AsUTF8(cname); Py_DECREF(cname); } else j["co_name"] = "";
            j["py_code_v"] = 1;
            return j;
        }
        // Fallback: unknown type -> try repr
        PyObject *r = PyObject_Repr(obj);
        if (r) {
            const char *s = PyUnicode_AsUTF8(r);
            j["type"] = "repr";
            j["value"] = s ? s : "";
            Py_DECREF(r);
            return j;
        }
        j["type"] = "none";
        return j;
    }

    PyObject *json_to_pyobj(const json &j) {
        std::string type = j.value("type", "none");
        if (type == "none") return Py_None, Py_INCREF(Py_None), Py_None;
        if (type == "int") {
            long long v = j.value("value", 0LL);
            return PyLong_FromLongLong(v);
        }
        if (type == "float") {
            double v = j.value("value", 0.0);
            return PyFloat_FromDouble(v);
        }
        if (type == "bool") {
            bool v = j.value("value", false);
            if (v) {
                Py_INCREF(Py_True);
                return Py_True;
            } else {
                Py_INCREF(Py_False);
                return Py_False;
            }
        }
        if (type == "str") {
            std::string s = j.value("value", std::string(""));
            return PyUnicode_FromStringAndSize(s.c_str(), (Py_ssize_t)s.size());
        }
        if (type == "bytes") {
            std::string b64 = j.value("value", std::string(""));
            std::vector<uint8_t> vec = base64::decode(b64);
            return PyBytes_FromStringAndSize(reinterpret_cast<const char *>(vec.data()), (Py_ssize_t)vec.size());
        }

        if (type == "tuple") {
            const json &items = j.value("items", json::array());
            Py_ssize_t n = (Py_ssize_t)items.size();
            PyObject *t = PyTuple_New(n);
            for (Py_ssize_t i = 0; i < n; ++i) {
                PyObject *it = json_to_pyobj(items[(size_t)i]);
                PyTuple_SetItem(t, i, it); // steals ref
            }
            return t;
        }
        if (type == "code") {
            // Reconstruct code object using Python's types.CodeType
            // This approach is more portable across Python versions (3.8+, 3.11+, 3.13+)
            try {
                std::string b64 = j.value("co_code", std::string(""));
                std::vector<uint8_t> code_bytes = base64::decode(b64);
                
                // Build freevars and cellvars tuples from JSON
                const json &freevars_arr = j.value("co_freevars", json::array());
                const json &cellvars_arr = j.value("co_cellvars", json::array());
                
                PyObject *freevars_t = PyTuple_New((Py_ssize_t)freevars_arr.size());
                for (size_t i = 0; i < freevars_arr.size(); ++i) {
                    PyTuple_SetItem(freevars_t, (Py_ssize_t)i, 
                        PyUnicode_FromString(freevars_arr[(int)i].get<std::string>().c_str()));
                }
                
                PyObject *cellvars_t = PyTuple_New((Py_ssize_t)cellvars_arr.size());
                for (size_t i = 0; i < cellvars_arr.size(); ++i) {
                    PyTuple_SetItem(cellvars_t, (Py_ssize_t)i,
                        PyUnicode_FromString(cellvars_arr[(int)i].get<std::string>().c_str()));
                }
                
                // consts
                const json &carr = j.value("co_consts", json::array());
                PyObject *consts = PyTuple_New((Py_ssize_t)carr.size());
                for (size_t i = 0; i < carr.size(); ++i) {
                    PyObject *cv = json_to_pyobj(carr[i]);
                    PyTuple_SetItem(consts, (Py_ssize_t)i, cv);
                }
                
                // names
                const json &names = j.value("co_names", json::array());
                PyObject *names_t = PyTuple_New((Py_ssize_t)names.size());
                for (size_t i = 0; i < names.size(); ++i) {
                    PyTuple_SetItem(names_t, (Py_ssize_t)i, 
                        PyUnicode_FromString(names[(int)i].get<std::string>().c_str()));
                }
                
                const json &varnames = j.value("co_varnames", json::array());
                PyObject *varnames_t = PyTuple_New((Py_ssize_t)varnames.size());
                for (size_t i = 0; i < varnames.size(); ++i) {
                    PyTuple_SetItem(varnames_t, (Py_ssize_t)i,
                        PyUnicode_FromString(varnames[(int)i].get<std::string>().c_str()));
                }

                // Import types module and get CodeType
                PyObject *types_mod = PyImport_ImportModule("types");
                if (!types_mod) {
                    Py_XDECREF(freevars_t);
                    Py_XDECREF(cellvars_t);
                    Py_XDECREF(consts);
                    Py_XDECREF(names_t);
                    Py_XDECREF(varnames_t);
                    return nullptr;
                }
                
                PyObject *CodeType = PyObject_GetAttrString(types_mod, "CodeType");
                Py_DECREF(types_mod);
                if (!CodeType) {
                    Py_XDECREF(freevars_t);
                    Py_XDECREF(cellvars_t);
                    Py_XDECREF(consts);
                    Py_XDECREF(names_t);
                    Py_XDECREF(varnames_t);
                    return nullptr;
                }
                
                // Build arguments for CodeType constructor
                // Python 3.8+ signature:
                // CodeType(argcount, posonlyargcount, kwonlyargcount, nlocals, stacksize, flags,
                //          codestring, constants, names, varnames, filename, name, qualname,
                //          firstlineno, linetable, exceptiontable, freevars, cellvars)
                // Note: Python 3.11+ changed some parameters
                
                PyObject *co_code = PyBytes_FromStringAndSize(
                    reinterpret_cast<const char *>(code_bytes.data()), 
                    (Py_ssize_t)code_bytes.size());
                
                std::string filename = j.value("co_filename", std::string(""));
                std::string name = j.value("co_name", std::string(""));
                PyObject *filename_o = PyUnicode_FromString(filename.c_str());
                PyObject *name_o = PyUnicode_FromString(name.c_str());
                PyObject *linetable_o = PyBytes_FromStringAndSize("", 0);
                
                int argcount = j.value("co_argcount", 0);
                int posonly = j.value("co_posonlyargcount", 0);
                int kwonly = j.value("co_kwonlyargcount", 0);
                int nlocals = j.value("co_nlocals", 0);
                int stacksize = j.value("co_stacksize", 0);
                int flags = j.value("co_flags", 0);
                int firstlineno = j.value("co_firstlineno", 1);
                
                PyObject *codeobj = nullptr;

#if PY_VERSION_HEX >= 0x030B0000
                // Python 3.11+ uses different constructor signature
                // Try using .replace() method on a dummy code object
                // Or build with the new format
                PyObject *qualname_o = PyUnicode_FromString(name.c_str());
                PyObject *exceptiontable_o = PyBytes_FromStringAndSize("", 0);
                
                // Build args tuple for CodeType
                PyObject *args = PyTuple_Pack(18,
                    PyLong_FromLong(argcount),
                    PyLong_FromLong(posonly),
                    PyLong_FromLong(kwonly),
                    PyLong_FromLong(nlocals),
                    PyLong_FromLong(stacksize),
                    PyLong_FromLong(flags),
                    co_code,
                    consts,
                    names_t,
                    varnames_t,
                    filename_o,
                    name_o,
                    qualname_o,
                    PyLong_FromLong(firstlineno),
                    linetable_o,
                    exceptiontable_o,
                    freevars_t,
                    cellvars_t
                );
                
                if (args) {
                    codeobj = PyObject_Call(CodeType, args, nullptr);
                    Py_DECREF(args);
                }
                
                Py_XDECREF(qualname_o);
                Py_XDECREF(exceptiontable_o);
#else
                // Python 3.8-3.10 signature
                PyObject *lnotab_o = PyBytes_FromStringAndSize("", 0);
                
                PyObject *args = PyTuple_Pack(16,
                    PyLong_FromLong(argcount),
                    PyLong_FromLong(posonly),
                    PyLong_FromLong(kwonly),
                    PyLong_FromLong(nlocals),
                    PyLong_FromLong(stacksize),
                    PyLong_FromLong(flags),
                    co_code,
                    consts,
                    names_t,
                    varnames_t,
                    filename_o,
                    name_o,
                    PyLong_FromLong(firstlineno),
                    lnotab_o,
                    freevars_t,
                    cellvars_t
                );
                
                if (args) {
                    codeobj = PyObject_Call(CodeType, args, nullptr);
                    Py_DECREF(args);
                }
                
                Py_XDECREF(lnotab_o);
#endif
                
                Py_DECREF(CodeType);
                Py_XDECREF(co_code);
                Py_XDECREF(filename_o);
                Py_XDECREF(name_o);
                Py_XDECREF(linetable_o);
                Py_XDECREF(freevars_t);
                Py_XDECREF(cellvars_t);
                Py_XDECREF(consts);
                Py_XDECREF(names_t);
                Py_XDECREF(varnames_t);
                
                if (!codeobj && PyErr_Occurred()) {
                    PyErr_Print();
                    PyErr_Clear();
                }
                
                return codeobj;
            } catch (...) {
                PyErr_Clear();
                return nullptr;
            }
        }

        // repr fallback: create a string
        if (type == "repr") {
            std::string s = j.value("value", std::string(""));
            return PyUnicode_FromStringAndSize(s.c_str(), (Py_ssize_t)s.size());
        }
        Py_INCREF(Py_None);
        return Py_None;
    }
} // namespace pyser
