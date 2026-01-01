// pyser.hpp
// Public types and declarations for the pyser serializer/deserializer.
//
// Notes:
// - The serializer walks Python object graphs and produces a SerializedGraph
//   which can be converted to compressed bytes (JSON + Zstd + base64 chunks).
// - Each DataChunk contains raw bytes, a base64 representation, and a SHA256 hash
//   which is validated during deserialization to detect corruption.
// - This header exposes structures used by both the C++ implementation and the
//   Python binding (python_binding.cpp).

#pragma once
#include <Python.h>
#include <cstdint>
#include <vector>
#include <unordered_map>
#include <string>
#include <memory>
#include <nlohmann/json.hpp>
namespace pyser {
    constexpr size_t CHUNK_SIZE = 65536; // 64KB per chunk
    constexpr size_t MAX_DEPTH = 100;

    enum class NodeType : uint8_t {
        NONE = 0,
        BOOL = 1,
        INT = 2,
        FLOAT = 3,
        BYTES = 4,
        STRING = 5,
        LIST = 6,
        TUPLE = 7,
        DICT = 8,
        SET = 9,
        FROZENSET = 10,
        FUNCTION = 11,
        METHOD = 12,
        MODULE = 13,
        CUSTOM = 99,
        REFERENCE = 100
    };

    struct PointerInfo {
        uint32_t from_node_id;
        uint32_t from_chunk_id;
        size_t offset;
        uint32_t to_node_id;
        std::string field_name;

        PointerInfo()
            : from_node_id(0), from_chunk_id(0), offset(0), to_node_id(0), field_name() {}
    };

    struct DataChunk {
        uint32_t chunk_id;
        std::vector<uint8_t> raw_data;
        std::string base64_data;
        std::string sha256_hash;
        size_t original_size;

        DataChunk() : chunk_id(0), raw_data(), base64_data(), sha256_hash(), original_size(0) {}
    };

    struct SerializedNode {
        uint32_t node_id;
        NodeType type;
        std::vector<DataChunk> chunks;
        std::vector<PointerInfo> pointers;

        struct Metadata {
            std::string type_name;
            std::string module_name;
            size_t total_size;
            uint32_t refcount;
            bool has_dict;
            std::vector<std::string> attr_names;
            std::unordered_map<std::string, uint32_t> attr_node_ids;
            bool is_bigint;
            size_t bigint_num_digits;
            std::string func_code;
            std::vector<std::string> func_closure_vars;
            std::string func_defaults;     // JSON-serialized __defaults__ tuple
            std::string func_kwdefaults;   // JSON-serialized __kwdefaults__ dict

            Metadata()
                : type_name(), module_name(), total_size(0), refcount(0), has_dict(false),
                  attr_names(), attr_node_ids(), is_bigint(false), bigint_num_digits(0),
                  func_code(), func_closure_vars(), func_defaults(), func_kwdefaults() {}
        } meta;


        SerializedNode() : node_id(0), type(NodeType::NONE), chunks(), pointers(), meta() {}
    };

    struct SerializedGraph {
        uint32_t root_id;
        std::vector<SerializedNode> nodes;
        std::vector<PointerInfo> all_pointers;

        [[nodiscard]] std::vector<uint8_t> to_bytes() const;

        static SerializedGraph from_bytes(const std::vector<uint8_t> &data);
    };

    class PyObjectSerializer {
    public:
        PyObjectSerializer() : next_node_id_(0), next_chunk_id_(0) {
        }

        SerializedGraph serialize(PyObject *obj);

        PyObject *deserialize(const SerializedGraph &graph);

        static std::string compute_sha256(const std::vector<uint8_t> &data);

    private:
        uint32_t serialize_recursive(
            PyObject *obj,
            SerializedGraph &graph,
            std::unordered_map<PyObject *, uint32_t> &visited,
            int depth
        );

        SerializedNode serialize_int(PyObject *obj);

        SerializedNode serialize_bigint(PyObject *obj);

        SerializedNode serialize_float(PyObject *obj);

        SerializedNode serialize_string(PyObject *obj);

        SerializedNode serialize_bytes(PyObject *obj);

        SerializedNode serialize_container(PyObject *obj, NodeType type,
            SerializedGraph &graph,
            std::unordered_map<PyObject *, uint32_t> &visited,
            int depth,
            uint32_t owner_node_id);

        SerializedNode serialize_dict(PyObject *obj, SerializedGraph &graph,
                                      std::unordered_map<PyObject *, uint32_t> &visited,
                                      int depth,
                                      uint32_t owner_node_id);

        SerializedNode serialize_function(PyObject *obj, SerializedGraph &graph,
                                          std::unordered_map<PyObject *, uint32_t> &visited,
                                          int depth,
                                          uint32_t owner_node_id);

        SerializedNode serialize_module(PyObject *obj);

        SerializedNode serialize_custom(PyObject *obj, SerializedGraph &graph,
                                        std::unordered_map<PyObject *, uint32_t> &visited,
                                        int depth,
                                        uint32_t owner_node_id);

        std::vector<DataChunk> create_chunks(const std::vector<uint8_t> &data);

        PyObject *deserialize_node(uint32_t node_id, const SerializedGraph &graph,
                                   std::unordered_map<uint32_t, PyObject *> &cache);

        void resolve_pointers(const SerializedGraph &graph,
                              std::unordered_map<uint32_t, PyObject *> &cache);

        uint32_t next_node_id_;
        uint32_t next_chunk_id_;
    };

    // JSON conversion helpers for code object serialization
    // These are used for marshal-free serialization of function code objects
    nlohmann::json pyobj_to_json(PyObject *obj);
    PyObject* json_to_pyobj(const nlohmann::json &j);
} // namespace pyser


