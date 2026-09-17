#pragma once
#include "webscene_css_selectors.h"
#include <algorithm>

namespace webscene_native::css {

// Anchor every relative arm before passing it to the ordinary selector parser.
// Commas within strings, attributes and functional arguments are not separators.
inline std::string anchor_relative_selector_list(std::string_view text)
{
    std::string result = ":scope ";
    int brackets = 0, parentheses = 0;
    char quote = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        const auto c = text[i];
        if (c == '\\') {
            const auto end = skip_css_escape_sequence(text, i);
            result.append(text.substr(i, end - i));
            i = end - 1U;
            continue;
        }
        result.push_back(c);
        if (quote != 0) { if (c == quote) quote = 0; continue; }
        if (c == '\'' || c == '"') { quote = c; continue; }
        if (c == '[') ++brackets;
        else if (c == ']') --brackets;
        else if (c == '(') ++parentheses;
        else if (c == ')') --parentheses;
        else if (c == ',' && brackets == 0 && parentheses == 0)
            result += ":scope ";
    }
    return result;
}

// This compiler consumes parsed compounds, including escaped identifiers and
// recursively parsed selector-list arguments. Matching a mutation must never
// rediscover these dependencies by scanning selector text.
inline std::vector<css_compound_dependencies> compile_invalidation_plan(
    const compiled_css_selector& selector)
{
    std::vector<css_compound_dependencies> result(selector.compiled_compounds.size());
    const auto collect = [&](const auto& self, const compiled_css_compound& compound,
                             css_compound_dependencies& output, uint8_t scope) -> void {
        for (const auto& [marker, name] : compound.identities) {
            if (marker == '.') output.classes[name] |= scope;
            else if (marker == '#') output.attributes["id"] |= scope;
        }
        for (const auto& attribute : compound.attributes) {
            auto text = trim_css_view(attribute);
            size_t cursor = 0;
            auto name = read_css_identifier(text, cursor);
            if (!name.empty()) output.attributes[std::move(name)] |= scope;
        }
        for (const auto& pseudo : compound.pseudos) {
            if (pseudo.name == "disabled" || pseudo.name == "enabled") {
                // A fieldset/optgroup attribute also changes descendant
                // controls. Keep the conservative route until that inherited
                // HTML state has a dedicated invalidation scope.
                output.attributes["disabled"] |= invalidation_fallback;
            } else if (pseudo.name == "checked") {
                for (const auto* name : {"checked", "selected", "type"})
                    output.attributes[name] |= scope;
            } else if (pseudo.name == "required" || pseudo.name == "optional"
                || pseudo.name == "valid" || pseudo.name == "invalid") {
                output.attributes["required"] |= scope;
                output.attributes["value"] |= scope;
            }
            if (pseudo.argument.empty()) continue;
            const bool has = pseudo.name == "has";
            const bool selector_list = has || pseudo.name == "is"
                || pseudo.name == "where" || pseudo.name == "not";
            if (!selector_list) continue;
            auto nested = compile_selector_list(has
                ? anchor_relative_selector_list(pseudo.argument) : pseudo.argument);
            for (const auto& arm : nested.selectors) {
                auto nested_scope = scope;
                if (has) {
                    nested_scope = invalidation_ancestors;
                    if ((scope & invalidation_fallback) != 0U
                        || std::any_of(arm.combinators.begin(), arm.combinators.end(),
                            [](char c) { return c == '+' || c == '~'; })) {
                        nested_scope = invalidation_fallback;
                    }
                } else if (arm.compounds.size() > 1U) {
                    // A feature inside :is(.ancestor .subject) may be on a
                    // different element. Retain a measured conservative path
                    // until a full reverse/forward route is compiled for it.
                    nested_scope = invalidation_fallback;
                }
                for (const auto& child : arm.compiled_compounds)
                    self(self, child, output, nested_scope);
            }
        }
    };
    for (size_t i = 0; i < result.size(); ++i)
        collect(collect, selector.compiled_compounds[i], result[i],
            invalidation_subject);
    return result;
}
} // namespace webscene_native::css
