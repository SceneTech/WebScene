#pragma once
#include "webscene_css_property_mask.h"
#include "webscene_css_rule_operations.h"
#include <array>
#include <span>

namespace webscene_native::css {
// Input rules are already matched and sorted in ascending cascade precedence.
// Preserve runtime ordering: custom properties, ordinary values, dependent inline
// values, then inline transitions. The callback applies one declaration and origin.
template<typename Apply>
void apply_matched_declarations(dom_node& node,const cascaded_rule_order& order,
    Apply&& apply)
{
        // CSS custom properties are cascaded before dependent declarations
        // are computed. Applying each rule eagerly made a base button height
        // resolve before a later size token was available.
        for_each_cascaded_declaration(
            order,
            true,
            [&](const css_declaration& declaration) { apply(declaration, false); });
        for_each_cascaded_declaration(
            order,
            false,
            [&](const css_declaration& declaration) { apply(declaration, false); });
        // An inline declaration can be authored before the stylesheet that
        // defines one of its var() references is connected. Its first
        // computed-value attempt is then invalid, but the authored tokens must
        // remain live and be recomputed when the variable becomes available.
        // Replay variable-dependent inline declarations and explicit dimension
        // inheritance, whose computed values can change with the parent.
        // Temporarily removing the inline guard lets a declaration update its own
        // computed value while the important-origin mask still prevents a
        // normal inline declaration from overriding an author !important rule.
        uint64_t inline_groups = 0U;
        bool ordered_inline_replay =
            node.authored_style().requires_full_replay;
        node.authored_style().for_each_declaration(
            [&](const std::string& name, const std::string&) {
                if (name.starts_with("--")
                    || node.authored_style().important_declarations.contains(name)) {
                    return;
                }
                const auto group = css::property_mask(name);
                ordered_inline_replay = ordered_inline_replay || name == "all"
                    || (group != 0U && (inline_groups & group) != 0U);
                inline_groups |= group;
            });
        node.authored_style().for_each_declaration(
            [&](const std::string& name, const std::string& value) {
            const auto inline_important =
                node.authored_style().important_declarations.contains(name);
            const auto inherited_dimension = (name == "width" || name == "height")
                && trim_css_view(value) == "inherit";
            if (name.starts_with("--") || inline_important
                || (!ordered_inline_replay
                    && value.find("var(") == std::string::npos
                    && !inherited_dimension)) return;
            const auto property_mask = css::property_mask(name);
            const auto retained_inline_mask = node.style.inline_property_mask;
            node.style.inline_property_mask &= ~property_mask;
            apply({name, value, false}, true);
            node.style.inline_property_mask = retained_inline_mask;
        });
        // Inline transition declarations are stored separately from the hot
        // style object. Recascade clears cold animation state before applying
        // stylesheet rules, so restore the inline origin afterward while
        // retaining per-longhand !important precedence.
        constexpr std::array transition_properties{
            "transition",
            "transition-property",
            "transition-duration",
            "transition-delay",
            "transition-timing-function"
        };
        for (const auto* property : transition_properties) {
            const auto authored = node.authored_style().declarations.find(property);
            if (authored == node.authored_style().declarations.end()) continue;
            const auto inline_important =
                node.authored_style().important_declarations.contains(property);
            if (inline_important) continue;
            const auto property_mask = css::property_mask(property);
            if ((node.style.important_property_mask & property_mask) != 0U) continue;
            apply({property, authored->second, false},false);
        }
        // Inline !important is the highest author-origin tier. The hot style
        // fields retain ordinary inline declarations between cascades, but an
        // author !important rule is allowed to replace those fields while the
        // rule list is applied. Replaying only the authored important tier
        // restores the CSS cascade order without reapplying every ordinary
        // inline declaration or adding a mutation-lived winner cache.
        node.authored_style().for_each_declaration(
            [&](const std::string& name, const std::string& value) {
            if (name.starts_with("--")
                || !node.authored_style().important_declarations.contains(name)) {
                return;
            }
            const auto property_mask = css::property_mask(name);
            const auto retained_inline_mask = node.style.inline_property_mask;
            node.style.inline_property_mask &= ~property_mask;
            apply({name, value, true}, true);
            node.style.inline_property_mask = retained_inline_mask;
        });
}

template<typename Apply>
void apply_matched_declarations(dom_node& node,std::span<const css_rule* const> matched_rules,
    Apply&& apply)
{
        const cascaded_rule_order order(matched_rules);
        apply_matched_declarations(node,order,std::forward<Apply>(apply));
}
} // namespace webscene_native::css
