#pragma once
#include "webscene_css_selectors.h"
#include "webscene_css_declarations.h"
#include "webscene_css_candidates.h"
#include "webscene_css_rule_operations.h"

namespace webscene_native::css {
// Index a selector by a necessary subject key. Matching still verifies the full
// selector. The caller owns storage and must rebuild it when rules are removed.
inline void index_selector_subject(size_t index, std::string_view selector,
    const compiled_css_selector& prepared,
    css_index_string_map<std::vector<size_t>>& css_rules_by_id,
    css_class_index_map<std::vector<size_t>>& css_rules_by_class,
    css_index_string_map<std::vector<size_t>>& css_rules_by_tag,
    css_index_string_map<std::vector<size_t>>& css_rules_by_attribute,
    std::vector<size_t>& css_focus_rules,
    std::vector<size_t>& unindexed_css_rules)
    {
        // Use mandatory features of the *subject's outer compound*. The text
        // before its first pseudo is not the compound: :not(.x).target still
        // requires .target, and escaped punctuation is part of an identifier.
        // Never pick a feature from a functional arm (:is(.a,.b), :not(.x),
        // :has(.child)), nor from an ancestor/sibling compound.
        if (!prepared.compiled_compounds.empty()) {
            const auto& compound = prepared.compiled_compounds.back();
            if (compound.valid) {
                for (const auto& [marker, key] : compound.identities) {
                    if (marker == '#') { css_rules_by_id[key].push_back(index); return; }
                }
                for (const auto& [marker, key] : compound.identities) {
                    if (marker == '.') { css_rules_by_class[key].push_back(index); return; }
                }
                // HTML matching folds names, XML matching does not. Both keys
                // are conservative candidates; the matcher checks the mode.
                const auto append_name = [&](auto& names, const std::string& name) {
                    names[name].push_back(index);
                    const auto folded = ascii_lower(name);
                    if (folded != name) names[folded].push_back(index);
                };
                if (!compound.tag.empty() && compound.tag != "*") {
                    append_name(css_rules_by_tag, compound.tag);
                    return;
                }
                for (const auto& attribute : compound.attributes) {
                    if (!attribute.local_name.empty()) {
                        append_name(css_rules_by_attribute, attribute.local_name);
                        return;
                    }
                }
            }
        }
        if (trim_css_view(selector) == ":root") {
            css_rules_by_tag["html"].push_back(index);
            return;
        }
        if (trim_css_view(selector) == ":focus") {
            css_focus_rules.push_back(index);
            return;
        }
        unindexed_css_rules.push_back(index);
    }

// Keep legacy dependency collection separate from prepared subject indexing.
// A transient changed-rule index needs only the latter, not another scan of
// every selector's ancestor/functional syntax.
inline void index_selector(size_t index, const std::string& selector,
    const compiled_css_selector& prepared,
    css_index_string_map<std::vector<size_t>>& css_rules_by_id,
    css_class_index_map<std::vector<size_t>>& css_rules_by_class,
    css_index_string_map<std::vector<size_t>>& css_rules_by_tag,
    css_index_string_map<std::vector<size_t>>& css_rules_by_attribute,
    std::vector<size_t>& css_focus_rules,
    std::vector<size_t>& unindexed_css_rules,
    css_index_string_set& css_descendant_attribute_dependencies)
{
    size_t compound_begin = 0;
    int bracket_depth = 0;
    int parenthesis_depth = 0;
    for (size_t offset = selector.size(); offset > 0; --offset) {
        const auto position = offset - 1U;
        const auto character = selector[position];
        if (character == ']') ++bracket_depth;
        else if (character == '[') --bracket_depth;
        else if (character == ')') ++parenthesis_depth;
        else if (character == '(') --parenthesis_depth;
        else if (bracket_depth == 0 && parenthesis_depth == 0
            && (std::isspace(static_cast<unsigned char>(character))
                || character == '>' || character == '+' || character == '~')) {
            compound_begin = position + 1U;
            break;
        }
    }
    for (size_t open = 0;
         (open = selector.find('[', open)) != std::string_view::npos;) {
        auto cursor = open + 1U;
        while (cursor < selector.size()
            && std::isspace(static_cast<unsigned char>(selector[cursor]))) ++cursor;
        const auto start = cursor;
        while (cursor < selector.size()
            && (std::isalnum(static_cast<unsigned char>(selector[cursor]))
                || selector[cursor] == '-' || selector[cursor] == '_')) ++cursor;
        if (cursor > start
            && (open < compound_begin || selector.find(":has(") != std::string_view::npos)) {
            css_descendant_attribute_dependencies.emplace(selector.substr(start, cursor - start));
        }
        open = cursor;
    }
    index_selector_subject(index,selector,prepared,css_rules_by_id,css_rules_by_class,
        css_rules_by_tag,css_rules_by_attribute,css_focus_rules,unindexed_css_rules);
}

// An immutable rule subset, using original cascade indices. Building once per
// media transition costs O(changed rule features); visiting an unrelated node
// no longer invokes every changed keyed selector. Universal candidates still
// require matching. No node result or media truth survives this local object.
struct rule_subject_index final {
    css_index_string_map<std::vector<size_t>> by_id,by_tag,by_attribute;
    css_class_index_map<std::vector<size_t>> by_class;
    std::vector<size_t> focus,universal;

    rule_subject_index(std::span<const css_rule> rules,std::span<const size_t> subset) {
        for(const auto index:subset) {
            const auto& rule=rules[index];
            index_selector_subject(index,rule.selector(),
                rule.payload->compiled_pseudo_origin.compounds.empty()
                    ? rule.compiled_selector() : rule.payload->compiled_pseudo_origin,
                by_id,by_class,by_tag,by_attribute,focus,universal);
        }
    }
    std::vector<size_t> candidates(const dom_node& node,bool focused) const {
        auto result=collect_candidates(node,focused,by_tag,by_id,by_attribute,focus,universal,
            [&](auto& candidates,std::string_view name) {
                if(const auto found=by_class.find(name);found!=by_class.end())
                    candidates.insert(candidates.end(),found->second.begin(),found->second.end());
            });
        deduplicate_candidates(result);
        return result;
    }
};
} // namespace webscene_native::css
