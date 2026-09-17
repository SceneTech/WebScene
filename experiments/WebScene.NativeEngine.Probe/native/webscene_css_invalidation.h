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
    const auto add = [](css_feature_dependency& dependency, const css_invalidation_route& route) {
        if (route.empty()) dependency.scope |= invalidation_subject;
        else if (route.size() == 1U && route.front() == css_invalidation_step::ancestors)
            dependency.scope |= invalidation_ancestors;
        else {
            dependency.scope |= invalidation_routed;
            if (std::find(dependency.routes.begin(), dependency.routes.end(), route)
                == dependency.routes.end()) dependency.routes.push_back(route);
        }
    };
    const auto forward = [](char combinator) {
        switch (combinator) {
        case '>': return css_invalidation_step::children;
        case '+': return css_invalidation_step::next_sibling;
        case '~': return css_invalidation_step::following_siblings;
        default: return css_invalidation_step::descendants;
        }
    };
    const auto reverse = [](char combinator) {
        switch (combinator) {
        case '>': return css_invalidation_step::parent;
        case '+': return css_invalidation_step::previous_sibling;
        case '~': return css_invalidation_step::preceding_siblings;
        default: return css_invalidation_step::ancestors;
        }
    };
    const auto collect = [&](const auto& self, const compiled_css_compound& compound,
                             css_compound_dependencies& output,
                             const css_invalidation_route& route) -> void {
        for (const auto& [marker, name] : compound.identities) {
            if (marker == '.') add(output.classes[name], route);
            else if (marker == '#') add(output.attributes["id"], route);
        }
        for (const auto& attribute : compound.attributes) {
            auto text = trim_css_view(attribute);
            size_t cursor = 0;
            auto name = read_css_identifier(text, cursor);
            if (!name.empty()) add(output.attributes[std::move(name)], route);
        }
        for (const auto& pseudo : compound.pseudos) {
            if (pseudo.name == "has" || pseudo.name == "empty"
                || pseudo.name == "first-child" || pseudo.name == "last-child"
                || pseudo.name == "only-child" || pseudo.name == "first-of-type"
                || pseudo.name == "last-of-type" || pseudo.name == "only-of-type"
                || pseudo.name.starts_with("nth-")) output.child_list_sensitive = true;
            if (pseudo.name == "disabled" || pseudo.name == "enabled") {
                auto inherited_route = css_invalidation_route{
                    css_invalidation_step::inclusive_descendants};
                inherited_route.insert(inherited_route.end(), route.begin(), route.end());
                add(output.attributes["disabled"], inherited_route);
            } else if (pseudo.name == "checked") {
                for (const auto* name : {"checked", "selected", "type"})
                    add(output.attributes[name], route);
            } else if (pseudo.name == "required" || pseudo.name == "optional"
                || pseudo.name == "valid" || pseudo.name == "invalid") {
                add(output.attributes["required"], route);
                add(output.attributes["value"], route);
            }
            if (pseudo.argument.empty()) continue;
            const bool has = pseudo.name == "has";
            const bool selector_list = has || pseudo.name == "is"
                || pseudo.name == "where" || pseudo.name == "not";
            if (!selector_list) continue;
            auto nested = compile_selector_list(has
                ? anchor_relative_selector_list(pseudo.argument) : pseudo.argument);
            for (const auto& arm : nested.selectors) {
                if (std::any_of(arm.combinators.begin(), arm.combinators.end(),
                        [](char value) { return value == '+' || value == '~'; }))
                    output.child_list_sensitive = true;
                for (size_t i = 0; i < arm.compiled_compounds.size(); ++i) {
                    css_invalidation_route nested_route;
                    if (has) {
                        // The relative selector is anchored at its first
                        // compound. Walk backwards from the changed feature.
                        for (size_t j = i; j > 0; --j)
                            nested_route.push_back(reverse(arm.combinators[j - 1U]));
                    } else {
                        // :is/:not/:where test their final compound against
                        // the outer subject. Reach it from any inner feature.
                        for (size_t j = i; j < arm.combinators.size(); ++j)
                            nested_route.push_back(forward(arm.combinators[j]));
                    }
                    nested_route.insert(nested_route.end(), route.begin(), route.end());
                    self(self, arm.compiled_compounds[i], output, nested_route);
                }
            }
        }
    };
    for (size_t i = 0; i < result.size(); ++i)
        collect(collect, selector.compiled_compounds[i], result[i], {});
    if (!result.empty() && std::any_of(selector.combinators.begin(), selector.combinators.end(),
            [](char value) { return value == '+' || value == '~'; }))
        result.front().child_list_sensitive = true;
    return result;
}
} // namespace webscene_native::css
