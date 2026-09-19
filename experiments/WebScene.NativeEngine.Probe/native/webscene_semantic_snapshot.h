#pragma once

#include "webscene_native_engine.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace webscene_native {

inline constexpr uint32_t maximum_semantic_documents_v1 = 256U;
inline constexpr uint32_t maximum_semantic_nodes_v1 = 16U * 1024U;
inline constexpr uint32_t maximum_semantic_relationships_v1 = 64U * 1024U;
inline constexpr uint32_t maximum_semantic_string_bytes_v1 = 4U * 1024U * 1024U;
inline constexpr size_t maximum_semantic_text_bytes_v1 = 64U * 1024U;

struct semantic_snapshot_data_v1 final {
    uint64_t snapshot_generation{};
    uint64_t top_document_generation{};
    uint64_t layout_generation{};
    uint32_t flags{};
    uint32_t focused_node_index{WEBSCENE_SEMANTIC_NONE_INDEX_V1};
    std::vector<webscene_semantic_document_v1> documents;
    std::vector<webscene_semantic_node_v1> nodes;
    std::vector<webscene_semantic_relationship_v1> relationships;
    std::string strings;

    webscene_semantic_string_v1 append_string(std::string_view value)
    {
        if (value.empty()) return {};
        if (value.size() > maximum_semantic_text_bytes_v1
            || strings.size() + value.size() > maximum_semantic_string_bytes_v1) {
            flags |= WEBSCENE_SEMANTIC_SNAPSHOT_TRUNCATED_STRINGS_V1;
            return {};
        }
        const auto offset = static_cast<uint32_t>(strings.size());
        strings.append(value);
        return {offset, static_cast<uint32_t>(value.size())};
    }
};

} // namespace webscene_native
