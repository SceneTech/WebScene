#pragma once
#include "webscene_css_selectors.h"
#include "webscene_css_declarations.h"

namespace webscene_native::css {
// Index a selector by a necessary subject key. Matching still verifies the full
// selector. The caller owns storage and must rebuild it when rules are removed.
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
        // An attribute in an ancestor compound can change which descendants
        // match. Attribute selectors in the rightmost compound only require
        // recascading the mutated subject.
        for (size_t open = 0;
             (open = selector.find('[', open)) != std::string_view::npos;) {
            auto cursor = open + 1U;
            while (cursor < selector.size()
                && std::isspace(static_cast<unsigned char>(selector[cursor]))) {
                ++cursor;
            }
            const auto start = cursor;
            while (cursor < selector.size()
                && (std::isalnum(static_cast<unsigned char>(selector[cursor]))
                    || selector[cursor] == '-' || selector[cursor] == '_')) {
                ++cursor;
            }
            if (cursor > start
                && (open < compound_begin
                    || selector.find(":has(") != std::string_view::npos)) {
                css_descendant_attribute_dependencies.emplace(
                    selector.substr(start, cursor - start));
            }
            open = cursor;
        }
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
                    size_t cursor = 0;
                    const auto name = read_css_identifier(trim_css_view(attribute), cursor);
                    if (!name.empty()) { append_name(css_rules_by_attribute, name); return; }
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
} // namespace webscene_native::css
