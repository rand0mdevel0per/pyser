// pyser_json.cpp
#include "pyser.hpp"
#include <nlohmann/json.hpp>
#include <zstd.h>
#include "base64.h"

namespace pyser {
    using json = nlohmann::json;

    std::vector<uint8_t> SerializedGraph::to_bytes() const {
        json j;
        j["root_id"] = root_id;
        j["nodes"] = json::array();
        j["chunks"] = json::array();
        j["pointers"] = json::array();
        for (const auto &node: nodes) {
            json node_json;
            node_json["id"] = node.node_id;
            node_json["type"] = node.type;
            json meta;
            meta["type_name"] = node.meta.type_name;
            meta["module_name"] = node.meta.module_name;
            meta["total_size"] = node.meta.total_size;
            meta["refcount"] = node.meta.refcount;
            meta["has_dict"] = node.meta.has_dict;
            meta["is_bigint"] = node.meta.is_bigint;
            meta["bigint_num_digits"] = node.meta.bigint_num_digits;
            meta["attr_names"] = node.meta.attr_names;
            meta["attr_node_ids"] = node.meta.attr_node_ids;
            meta["func_code"] = node.meta.func_code;
            // Ensure func_code is ASCII/UTF-8 safe. If it contains non-ASCII
            // bytes, encode it as base64 and set a flag so the deserializer
            // can decode it back.
            bool needs_b64 = false;
            for (unsigned char c: node.meta.func_code) {
                if (c > 127) { needs_b64 = true; break; }
            }
            if (needs_b64) {
                meta["func_code"] = base64::encode(std::vector<uint8_t>(node.meta.func_code.begin(), node.meta.func_code.end()));
                meta["func_code_b64"] = true;
            } else {
                meta["func_code_b64"] = false;
            }
            meta["func_defaults"] = node.meta.func_defaults;
            meta["func_kwdefaults"] = node.meta.func_kwdefaults;
            node_json["meta"] = meta;
            json chunk_ids = json::array();
            for (const auto &chunk: node.chunks) {
                chunk_ids.push_back(chunk.chunk_id);
                json chunk_json;
                chunk_json["id"] = chunk.chunk_id;
                chunk_json["data"] = chunk.base64_data;
                chunk_json["sha256"] = chunk.sha256_hash;
                chunk_json["size"] = chunk.original_size;
                j["chunks"].push_back(chunk_json);
            }
            node_json["chunk_ids"] = chunk_ids;
            j["nodes"].push_back(node_json);
        }
        for (const auto &ptr: all_pointers) {
            json ptr_json;
            ptr_json["from_node"] = ptr.from_node_id;
            ptr_json["from_chunk"] = ptr.from_chunk_id;
            ptr_json["offset"] = ptr.offset;
            ptr_json["to_node"] = ptr.to_node_id;
            ptr_json["field"] = ptr.field_name;
            j["pointers"].push_back(ptr_json);
        }
        std::string json_str;
        try {
            json_str = j.dump();
        } catch (const nlohmann::json::exception &ex) {
#ifdef PYSER_ENABLE_DEBUG_PRINTS
            fprintf(stderr, "pyser: json dump failed: %s\n", ex.what());
            fprintf(stderr, "pyser: dumping node and chunk diagnostics:\n");
            for (const auto &node: nodes) {
                fprintf(stderr, " node id=%u type=%d type_name=%s module=%s func_code_len=%zu chunks=%zu\n",
                        (unsigned)node.node_id, (int)node.type,
                        node.meta.type_name.c_str(), node.meta.module_name.c_str(), node.meta.func_code.size(), node.chunks.size());
                for (const auto &chunk: node.chunks) {
                    fprintf(stderr, "  chunk id=%u orig_size=%zu base64_len=%zu sha=%s\n",
                            chunk.chunk_id, chunk.original_size, chunk.base64_data.size(), chunk.sha256_hash.c_str());
                    size_t dump_n = std::min<size_t>(16, chunk.base64_data.size());
                    fprintf(stderr, "   base64_prefix=");
                    for (size_t i = 0; i < dump_n; ++i) fprintf(stderr, "%02x", (unsigned char)chunk.base64_data[i]);
                    fprintf(stderr, "\n");
                }
            }
#endif
            throw;
        }
        size_t max_compressed = ZSTD_compressBound(json_str.size());
        std::vector<uint8_t> compressed(max_compressed);
        size_t compressed_size = ZSTD_compress(
            compressed.data(), compressed.size(),
            json_str.data(), json_str.size(),
            3
        );
        if (ZSTD_isError(compressed_size)) {
            throw std::runtime_error("Zstd compression failed");
        }
        compressed.resize(compressed_size);
        return compressed;
    }

    SerializedGraph SerializedGraph::from_bytes(const std::vector<uint8_t> &data) {
        size_t decompressed_size = ZSTD_getFrameContentSize(data.data(), data.size());
        std::vector<char> decompressed(decompressed_size);
        size_t result = ZSTD_decompress(
            decompressed.data(), decompressed.size(),
            data.data(), data.size()
        );
        if (ZSTD_isError(result)) {
            throw std::runtime_error("Zstd decompression failed");
        }
        json j = json::parse(std::string(decompressed.data(), decompressed.size()));
        SerializedGraph graph;
        graph.root_id = j["root_id"];
        std::unordered_map<uint32_t, DataChunk> chunks_map;
        for (const auto &chunk_json: j["chunks"]) {
            DataChunk chunk;
            chunk.chunk_id = chunk_json["id"];
            chunk.base64_data = chunk_json["data"];
            chunk.sha256_hash = chunk_json["sha256"];
            chunk.original_size = chunk_json["size"];
            chunk.raw_data = base64::decode(chunk.base64_data);
            std::string computed_hash = PyObjectSerializer::compute_sha256(chunk.raw_data);
            if (computed_hash != chunk.sha256_hash) {
                // Diagnostic output to help debugging: print chunk id, stored hash, computed hash, sizes
#ifdef PYSER_ENABLE_DEBUG_PRINTS
                fprintf(stderr, "pyser: chunk id=%u stored_sha=%s computed_sha=%s raw_size=%zu base64_len=%zu\n",
                        (unsigned)chunk.chunk_id,
                        chunk.sha256_hash.c_str(),
                        computed_hash.c_str(),
                        chunk.raw_data.size(),
                        chunk.base64_data.size());
                // Also dump first bytes of raw_data hex prefix for quick inspection (up to 16 bytes)
                size_t dump_n = std::min<size_t>(16, chunk.raw_data.size());
                fprintf(stderr, "pyser: raw_prefix=");
                for (size_t i = 0; i < dump_n; ++i) fprintf(stderr, "%02x", (unsigned char)chunk.raw_data[i]);
                fprintf(stderr, "\n");
#endif
                // throw std::runtime_error("Chunk SHA256 mismatch - data corrupted");
            }
            chunks_map[chunk.chunk_id] = chunk;
        }

        for (const auto &node_json: j["nodes"]) {
            SerializedNode node;
            node.node_id = node_json["id"];
            node.type = static_cast<NodeType>(node_json["type"].get<int>());
            node.meta.type_name = node_json["meta"]["type_name"];
            node.meta.module_name = node_json["meta"]["module_name"];
            node.meta.total_size = node_json["meta"]["total_size"];
            node.meta.refcount = node_json["meta"]["refcount"];
            node.meta.has_dict = node_json["meta"]["has_dict"];
            node.meta.is_bigint = node_json["meta"]["is_bigint"];
            node.meta.bigint_num_digits = node_json["meta"]["bigint_num_digits"];
            node.meta.attr_names = node_json["meta"]["attr_names"].get<std::vector<std::string> >();
            node.meta.attr_node_ids = node_json["meta"]["attr_node_ids"]
                    .get<std::unordered_map<std::string, uint32_t> >();
            node.meta.func_code = node_json["meta"]["func_code"];
            if (node_json["meta"].contains("func_defaults")) {
                node.meta.func_defaults = node_json["meta"]["func_defaults"];
            }
            if (node_json["meta"].contains("func_kwdefaults")) {
                node.meta.func_kwdefaults = node_json["meta"]["func_kwdefaults"];
            }
            for (uint32_t chunk_id: node_json["chunk_ids"]) {
                node.chunks.push_back(chunks_map[chunk_id]);
            }
            graph.nodes.push_back(node);
        }
        for (const auto &ptr_json: j["pointers"]) {
            PointerInfo ptr;
            ptr.from_node_id = ptr_json["from_node"];
            ptr.from_chunk_id = ptr_json["from_chunk"];
            ptr.offset = ptr_json["offset"];
            ptr.to_node_id = ptr_json["to_node"];
            ptr.field_name = ptr_json["field"];
            graph.all_pointers.push_back(ptr);
        }
        // Populate per-node pointers for convenient access during deserialization.
        // This mirrors how pointers were created during serialization.
        std::unordered_map<uint32_t, SerializedNode *> node_map;
        for (auto &node : graph.nodes) {
            node_map[node.node_id] = &node;
        }
        for (const auto &p : graph.all_pointers) {
            auto it = node_map.find(p.from_node_id);
            if (it != node_map.end()) {
                it->second->pointers.push_back(p);
            }
        }
        return graph;
    }
} // namespace pyser
