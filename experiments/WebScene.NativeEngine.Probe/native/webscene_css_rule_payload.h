#pragma once
#include "webscene_css_pseudo_application.h"
#include "webscene_css_property_mask.h"
#include "webscene_css_rule_operations.h"
#include "webscene_css_state.h"
#include "webscene_css_invalidation.h"
#include <algorithm>
#include <cctype>
#include <mutex>

namespace webscene_native::css {
inline std::vector<std::string> referenced_custom_properties(
    std::string_view value)
{
    std::unordered_set<std::string> unique;
    for (size_t search = 0U;
         (search = value.find("var(", search)) != std::string_view::npos;) {
        auto cursor = search + 4U;
        while (cursor < value.size()
            && std::isspace(static_cast<unsigned char>(value[cursor]))) {
            ++cursor;
        }
        const auto start = cursor;
        while (cursor < value.size()
            && value[cursor] != ',' && value[cursor] != ')'
            && !std::isspace(static_cast<unsigned char>(value[cursor]))) {
            ++cursor;
        }
        if (cursor > start
            && value.substr(start, cursor - start).starts_with("--")) {
            unique.emplace(value.substr(start, cursor - start));
        }
        search += 4U;
    }
    std::vector<std::string> references(unique.begin(), unique.end());
    std::sort(references.begin(), references.end());
    return references;
}

using rule_payload_cache = std::unordered_map<uint64_t,
    std::vector<std::weak_ptr<const css_rule_payload>>>;
inline uint64_t rule_payload_hash(
        std::string_view selector,
        const std::vector<css_declaration>& declarations,
        const std::vector<std::string>& media_queries,
        uint32_t cascade_layer_index = 0U,
        std::string_view selector_namespace_key = {})
    {
        auto hash = uint64_t{1469598103934665603ULL};
        const auto append = [&](std::string_view value) {
            for (const auto character : value) {
                hash ^= static_cast<unsigned char>(character);
                hash *= 1099511628211ULL;
            }
            hash ^= 0xffU;
            hash *= 1099511628211ULL;
        };
        append(selector);
        append(selector_namespace_key);
        for (const auto& declaration : declarations) {
            append(declaration.name);
            append(declaration.value);
            hash ^= declaration.important ? 1U : 0U;
            hash *= 1099511628211ULL;
        }
        for (const auto& query : media_queries) append(query);
        hash ^= cascade_layer_index;
        hash *= 1099511628211ULL;
        return hash;
    }

inline bool rule_payload_matches(
        const css_rule_payload& payload,
        std::string_view selector,
        const std::vector<css_declaration>& declarations,
        const std::vector<std::string>& media_queries,
        uint32_t cascade_layer_index = 0U,
        std::string_view selector_namespace_key = {})
    {
        if (payload.selector != selector
            || payload.selector_namespace_key != selector_namespace_key
            || payload.declarations.size() != declarations.size()
            || payload.media_queries != media_queries
            || payload.cascade_layer_index != cascade_layer_index) {
            return false;
        }
        for (size_t index = 0; index < declarations.size(); ++index) {
            const auto& left = payload.declarations[index];
            const auto& right = declarations[index];
            if (left.name != right.name || left.value != right.value
                || left.important != right.important) {
                return false;
            }
        }
        return true;
    }

template<typename Compile>
std::shared_ptr<const css_rule_payload> intern_rule_payload(
    std::mutex& mutex, rule_payload_cache& payloads, Compile&& compile,
    std::string selector, const std::vector<css_declaration>& declarations,
    const std::vector<std::string>& media_queries,
    uint32_t cascade_layer_index = 0U,
    std::string selector_namespace_key = {})
{
        const auto hash = rule_payload_hash(
            selector,
            declarations,
            media_queries,
            cascade_layer_index,
            selector_namespace_key);
        std::lock_guard lock(mutex);
        auto& candidates = payloads[hash];
        for (auto iterator = candidates.begin(); iterator != candidates.end();) {
            auto candidate = iterator->lock();
            if (candidate == nullptr) {
                iterator = candidates.erase(iterator);
                continue;
            }
            if (rule_payload_matches(
                    *candidate,
                    selector,
                    declarations,
                    media_queries,
                    cascade_layer_index,
                    selector_namespace_key)) {
                return candidate;
            }
            ++iterator;
        }
        auto payload = std::make_shared<css_rule_payload>();
        payload->selector = std::move(selector);
        payload->selector_namespace_key = std::move(selector_namespace_key);
        payload->compiled_selector = compile(payload->selector);
        std::string pseudo_origin;
        payload->pseudo_kind = static_cast<uint8_t>(
            split_pseudo_element_selector(payload->selector, pseudo_origin));
        payload->host_selector = trim_css_view(payload->selector) == ":host";
        if (payload->pseudo_kind != 0 && !pseudo_origin.empty())
            payload->compiled_pseudo_origin = compile(pseudo_origin);
        payload->invalidation = compile_invalidation_plan(
            payload->compiled_pseudo_origin.compounds.empty()
                ? payload->compiled_selector : payload->compiled_pseudo_origin);
        payload->specificity = payload->compiled_selector.specificity;
        payload->cascade_layer_index = cascade_layer_index;
        payload->declarations = declarations;
        std::unordered_set<std::string> aggregate_references;
        payload->declaration_variable_references.reserve(declarations.size());
        for (const auto& declaration : declarations) {
            auto references = referenced_custom_properties(declaration.value);
            if (!declaration.name.starts_with("--")) {
                const auto mask = property_mask(declaration.name);
                if (mask != 0U
                    && (cascade_keyword_is(declaration.value, "inherit")
                        || declaration.value.find("var(")
                            != std::string::npos)) {
                    payload->inheritance_candidate_mask |= mask;
                }
            }
            aggregate_references.insert(references.begin(), references.end());
            payload->declaration_variable_references.push_back(
                std::move(references));
        }
        payload->variable_references.assign(
            aggregate_references.begin(), aggregate_references.end());
        std::sort(
            payload->variable_references.begin(),
            payload->variable_references.end());
        payload->media_queries = media_queries;
        candidates.emplace_back(payload);
        return payload;
}
} // namespace webscene_native::css
