#pragma once
#include "webscene_css_pseudo_application.h"
#include "webscene_css_container_queries.h"
#include <span>

namespace webscene_native::css {
struct rule_matches {
    std::vector<const css_rule*> ordinary;
    std::vector<std::pair<int,const css_rule*>> pseudo;
};
// Candidates are unique valid indices in any order. Only successful matches
// need cascade precedence sorting. Results borrow the rule vector and must be
// consumed before its owner mutates/replaces that storage.
template<typename MatchSelector,typename MatchRule>
rule_matches match_candidates(native_document& document,const dom_node& node,
    std::span<const css_rule> rules,std::span<const size_t> candidates,
    MatchSelector&& match_selector,MatchRule&& match_rule,
    bool active_media_only = true)
{
        const auto* node_shadow_root = document.containing_shadow_root(node);
        const auto rule_is_in_scope = [&](const css_rule& rule) {
            if (rule.shadow_scope_root_id == 0U) return node_shadow_root == nullptr;
            auto* scope_root = document.find_by_native_id(rule.shadow_scope_root_id);
            if (scope_root == nullptr) return false;
            const auto* scope = document.shadow_dom(*scope_root);
            if (rule.payload->host_selector) return scope != nullptr && scope->host == &node;
            if (scope != nullptr && scope->host == &node) return false;
            return node_shadow_root == scope_root;
        };
        rule_matches result;
        for (const auto index : candidates) {
            const auto& rule = rules[index];
            if (active_media_only && !rule.media_matches) continue;
            if (active_media_only && !std::all_of(
                    rule.media_queries().begin(), rule.media_queries().end(),
                    [&](const auto& query) {
                        return !is_container_query(query)
                            || container_query_matches(document, node, query);
                    })) continue;
            if (!rule_is_in_scope(rule)) continue;
            const auto pseudo_kind = rule.payload->pseudo_kind;
            if (pseudo_kind != 0) {
                const auto& origin = rule.payload->compiled_pseudo_origin;
                if (!origin.compounds.empty() && match_selector(node,rule,origin)) {
                    result.pseudo.emplace_back(pseudo_kind, &rule);
                }
                continue;
            }
            if (!rule.payload->host_selector
                && !match_rule(node,rule)) continue;
            result.ordinary.push_back(&rule);
        }
        const auto precedes = [](const css_rule* left, const css_rule* right) {
            const auto a = left->specificity(), b = right->specificity();
            // Both pointers belong to the same contiguous rule span: address
            // order is original stylesheet/source order, not discovery order.
            return a != b ? a < b : left < right;
        };
        if (result.ordinary.size() > 1U)
            std::sort(result.ordinary.begin(), result.ordinary.end(), precedes);
        if (result.pseudo.size() > 1U)
            std::sort(result.pseudo.begin(), result.pseudo.end(),
                [&](const auto& left, const auto& right) {
                    return precedes(left.second, right.second);
                });
    return result;
}
} // namespace webscene_native::css
