#pragma once
#include "webscene_css_selectors.h"
#include "webscene_css_declarations.h"
#include <algorithm>

namespace webscene_native::css {

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
            if (!attribute.local_name.empty())
                add(output.attributes[attribute.local_name], route);
        }
        for (const auto& pseudo : compound.pseudos) {
            if (pseudo.name == "has" || pseudo.name == "empty"
                || pseudo.name == "first-child" || pseudo.name == "last-child"
                || pseudo.name == "only-child" || pseudo.name == "first-of-type"
                || pseudo.name == "last-of-type" || pseudo.name == "only-of-type"
                || pseudo.name.starts_with("nth-")) {
                output.child_list_sensitive = true;
                auto structural_route = route;
                if (pseudo.name == "has") {
                    // Removing a descendant can change both the parent anchor
                    // and any ancestor anchor, even when no current match remains.
                    add(output.child_list, route);
                    structural_route.insert(structural_route.begin(), css_invalidation_step::ancestors);
                } else if (pseudo.name != "empty") {
                    structural_route.insert(structural_route.begin(), css_invalidation_step::children);
                }
                add(output.child_list, structural_route);
            }
            if (pseudo.name == "disabled" || pseudo.name == "enabled") {
                auto inherited_route = css_invalidation_route{
                    css_invalidation_step::inclusive_descendants};
                inherited_route.insert(inherited_route.end(), route.begin(), route.end());
                add(output.attributes["disabled"], inherited_route);
                // Moving the first legend changes inherited disabled state.
                output.child_list_sensitive = true;
                add(output.child_list, inherited_route);
            } else if (pseudo.name == "checked") {
                for (const auto* name : {
                        "checked", "selected", "type", "$live-form-checkedness"})
                    add(output.attributes[name], route);
            } else if (pseudo.name == "default") {
                // Authored defaults update the subject directly. Submit-button
                // ownership/order changes can select a different control, so
                // those rare mutations conservatively recascade the document.
                add(output.attributes["checked"], route);
                add(output.attributes["selected"], route);
                for (const auto* name : {"type", "form", "id"})
                    output.attributes[name].scope |= invalidation_fallback;
                output.child_list_sensitive = true;
                output.child_list.scope |= invalidation_fallback;
            } else if (pseudo.name == "indeterminate") {
                for (const auto* name : {"$live-form-indeterminate", "checked", "type", "name"})
                    add(output.attributes[name], route);
                output.child_list_sensitive = true;
                add(output.child_list, route);
            } else if (pseudo.name == "in-range" || pseudo.name == "out-of-range") {
                for (const auto* name : {"$live-form-range", "type", "min", "max"})
                    add(output.attributes[name], route);
            } else if (pseudo.name == "read-only" || pseudo.name == "read-write") {
                add(output.attributes["readonly"], route);
                add(output.attributes["type"], route);
                auto inherited_route = css_invalidation_route{
                    css_invalidation_step::inclusive_descendants};
                inherited_route.insert(inherited_route.end(), route.begin(), route.end());
                add(output.attributes["disabled"], inherited_route);
                add(output.attributes["contenteditable"], inherited_route);
                // Moving the first legend changes inherited disabled state.
                output.child_list_sensitive = true;
                add(output.child_list, inherited_route);
            } else if (pseudo.name == "required" || pseudo.name == "optional"
                || pseudo.name == "valid" || pseudo.name == "invalid"
                || pseudo.name == "user-valid" || pseudo.name == "user-invalid") {
                add(output.attributes["required"], route);
                if (pseudo.name == "required" || pseudo.name == "optional") {
                    add(output.attributes["type"], route);
                } else {
                    // Text controls observe live value. Required checkboxes
                    // additionally consume their live checkedness below; the
                    // remaining non-text controls retain the authored-value
                    // dependency until their constraint slices land.
                    add(output.attributes["value"], route);
                    add(output.attributes["$live-form-value"], route);
                    add(output.attributes["type"], route);
                    add(output.attributes["checked"], route);
                    add(output.attributes["$live-form-checkedness"], route);
                    // A non-dirty textarea derives its live value from its
                    // text children. Reuse the structural route machinery.
                    output.child_list_sensitive = true;
                    add(output.child_list, route);
                    if (pseudo.name == "user-valid" || pseudo.name == "user-invalid")
                        add(output.attributes["$live-form-user-validity"], route);
                }
            } else if (pseudo.name == "placeholder-shown") {
                add(output.attributes["placeholder"], route);
                add(output.attributes["$live-form-value"], route);
                output.child_list_sensitive = true;
                add(output.child_list, route);
            }
            const bool has = pseudo.name == "has";
            const bool selector_list = has || pseudo.name == "is"
                || pseudo.name == "where" || pseudo.name == "not";
            if (!selector_list || !pseudo.compiled_argument_valid
                || pseudo.compiled_argument == nullptr) continue;
            for (const auto& arm : pseudo.compiled_argument->selectors) {
                if (!compiled_selector_is_valid(arm)) continue;
                if (std::any_of(arm.combinators.begin(), arm.combinators.end(),
                        [](char value) { return value == '+' || value == '~'; }))
                    output.child_list_sensitive = true;
                if (has && !arm.combinators.empty()
                    && (arm.combinators.front() == '+'
                        || arm.combinators.front() == '~')) {
                    // On removal, the changed final sibling no longer exists to
                    // seed a reverse route. Visit the surviving sibling subjects;
                    // stable outer-compound keys filter the bounded child list.
                    auto surviving_subjects = css_invalidation_route{
                        css_invalidation_step::children};
                    surviving_subjects.insert(
                        surviving_subjects.end(), route.begin(), route.end());
                    add(output.child_list, surviving_subjects);
                }
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
                    if (i > 0 && (arm.combinators[i - 1U] == '+'
                        || arm.combinators[i - 1U] == '~')) {
                        auto structural_route = nested_route;
                        structural_route.insert(structural_route.begin(), css_invalidation_step::children);
                        add(output.child_list, structural_route);
                    }
                    self(self, arm.compiled_compounds[i], output, nested_route);
                }
            }
        }
    };
    for (size_t i = 0; i < result.size(); ++i)
        collect(collect, selector.compiled_compounds[i], result[i], {});
    for (size_t i = 0; i < selector.combinators.size(); ++i) {
        if (selector.combinators[i] != '+' && selector.combinators[i] != '~') continue;
        result[i + 1U].child_list_sensitive = true;
        add(result[i + 1U].child_list, {css_invalidation_step::children});
    }
    return result;
}
// Index the stable outer compound, not a feature inside a functional pseudo.
// For example :not(.x) does not require .x, and :is(.a,.b) requires neither arm.
// Missing mandatory keys deliberately use the universal bucket.
inline void index_child_list_rule(size_t rule_index,
    const compiled_css_selector& selector,
    const std::vector<css_compound_dependencies>& dependencies,
    std::vector<css_child_list_bucket>& buckets)
{
    const auto bucket_for = [&](const css_invalidation_route& route) -> css_child_list_bucket& {
        const auto found = std::find_if(buckets.begin(), buckets.end(),
            [&](const auto& bucket) { return bucket.route == route; });
        if (found != buckets.end()) return *found;
        buckets.emplace_back();
        buckets.back().route = route;
        return buckets.back();
    };
    const auto append = [&](std::vector<size_t>& rules) {
        if (rules.empty() || rules.back() != rule_index) rules.push_back(rule_index);
    };
    if (dependencies.size() != selector.compiled_compounds.size()) {
        append(bucket_for({}).universal);
        return;
    }
    for (size_t i = 0; i < dependencies.size(); ++i) {
        const auto& dependency = dependencies[i].child_list;
        if (dependency.scope == 0U) continue;
        const auto& compound = selector.compiled_compounds[i];
        const auto index_route = [&](const css_invalidation_route& route) {
            auto& bucket = bucket_for(route);
            for (const auto& [marker, value] : compound.identities) {
                if (marker == '#') { append(bucket.by_id[value]); return; }
            }
            for (const auto& [marker, value] : compound.identities) {
                if (marker == '.') { append(bucket.by_class[value]); return; }
            }
            if (!compound.tag.empty() && compound.tag != "*") {
                append(bucket.by_tag[ascii_lower(compound.tag)]);
                return;
            }
            for (const auto& attribute : compound.attributes) {
                if (!attribute.local_name.empty()) {
                    append(bucket.by_attribute[ascii_lower(attribute.local_name)]);
                    return;
                }
            }
            append(bucket.universal);
        };
        if ((dependency.scope & invalidation_fallback) != 0U)
            append(bucket_for({}).universal);
        if ((dependency.scope & invalidation_subject) != 0U) index_route({});
        if ((dependency.scope & invalidation_ancestors) != 0U)
            index_route({css_invalidation_step::ancestors});
        for (const auto& route : dependency.routes) index_route(route);
    }
}
} // namespace webscene_native::css
