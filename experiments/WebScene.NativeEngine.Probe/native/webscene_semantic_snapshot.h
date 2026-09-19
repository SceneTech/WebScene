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

struct semantic_delta_data_v1 final {
    uint64_t base_snapshot_generation{};
    uint64_t new_snapshot_generation{};
    uint64_t base_top_document_generation{};
    uint64_t new_top_document_generation{};
    uint64_t base_layout_generation{};
    uint64_t new_layout_generation{};
    uint32_t flags{};
    std::vector<webscene_semantic_delta_operation_v1> operations;
    std::string strings;

    void require_full_snapshot(uint32_t reason)
    {
        flags |= WEBSCENE_SEMANTIC_DELTA_FULL_SNAPSHOT_REQUIRED_V1 | reason;
        std::vector<webscene_semantic_delta_operation_v1>().swap(operations);
        std::string().swap(strings);
    }
};

struct semantic_live_event_data_v1 final {
    uint64_t sequence{};
    uint64_t top_document_generation{};
    uint64_t frame_generation{};
    uint64_t semantic_id{};
    uint32_t frame_owner_dom_node_id{};
    uint32_t dom_node_id{};
    uint32_t role{};
    uint32_t politeness{};
    uint32_t flags{};
    std::string text;
};

struct semantic_live_capture_data_v1 final {
    bool complete{};
    uint64_t top_document_generation{};
    std::vector<uint64_t> live_region_semantic_ids;
    std::vector<semantic_live_event_data_v1> events;
};

struct semantic_live_fragment_v1 final {
    uint32_t dom_node_id{};
    std::string text;
};

struct semantic_live_region_state_v1 final {
    uint64_t top_document_generation{};
    uint64_t frame_generation{};
    uint64_t semantic_id{};
    uint32_t frame_owner_dom_node_id{};
    uint32_t dom_node_id{};
    uint32_t role{};
    uint32_t politeness{};
    uint32_t relevant{};
    bool atomic{};
    bool busy{};
    bool text_truncated{};
    std::vector<semantic_live_fragment_v1> fragments;
    std::string text;
    uint64_t last_event_fingerprint{};
};

struct semantic_live_batch_data_v1 final {
    uint64_t batch_generation{};
    uint32_t flags{};
    uint32_t dropped_event_count{};
    std::vector<webscene_semantic_live_event_v1> events;
    std::string strings;

    void append(semantic_live_event_data_v1 event)
    {
        webscene_semantic_live_event_v1 view{};
        view.struct_size = sizeof(view);
        view.version = 1U;
        view.sequence = event.sequence;
        view.top_document_generation = event.top_document_generation;
        view.frame_generation = event.frame_generation;
        view.semantic_id = event.semantic_id;
        view.frame_owner_dom_node_id = event.frame_owner_dom_node_id;
        view.dom_node_id = event.dom_node_id;
        view.role = event.role;
        view.politeness = event.politeness;
        view.flags = event.flags;
        view.text.offset = static_cast<uint32_t>(strings.size());
        view.text.length = static_cast<uint32_t>(event.text.size());
        strings.append(event.text);
        events.push_back(view);
    }
};

struct semantic_action_target_v1 final {
    uint64_t semantic_id{};
    uint64_t top_document_generation{};
    uint64_t frame_generation{};
    uint32_t frame_owner_dom_node_id{};
    uint32_t dom_node_id{};
    uint32_t supported_actions{};
};

struct semantic_action_request_data_v1 final {
    uint64_t snapshot_generation{};
    semantic_action_target_v1 target;
    uint32_t action{};
    uint32_t selection_start{};
    uint32_t selection_end{};
    std::string value;
};

enum class semantic_action_dispatch_result_v1 : uint8_t {
    discarded,
    performed,
    failed
};

struct semantic_snapshot_data_v1 final {
    uint64_t snapshot_generation{};
    uint64_t top_document_generation{};
    uint64_t layout_generation{};
    uint32_t flags{};
    uint32_t focused_node_index{WEBSCENE_SEMANTIC_NONE_INDEX_V1};
    std::vector<webscene_semantic_document_v1> documents;
    std::vector<webscene_semantic_node_v1> nodes;
    std::vector<webscene_semantic_relationship_v1> relationships;
    std::vector<semantic_action_target_v1> action_targets;
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
