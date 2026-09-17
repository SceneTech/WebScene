#pragma once
#include "webscene_css_pseudo_application.h"
#include <span>

namespace webscene_native::css {
struct rule_matches {
    std::vector<const css_rule*> ordinary;
    std::vector<std::pair<int,const css_rule*>> pseudo;
};
// Candidates are valid indices in ascending precedence order. Results borrow the
// rule vector and must be consumed before its owner mutates/replaces that storage.
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
    return result;
}
} // namespace webscene_native::css
