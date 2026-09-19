#pragma once
#include "webscene_css_matching.h"

namespace webscene_native::css {
template<typename Variables>
inline bool variable_collection_contains(
    const Variables& variables,
    const std::string& variable)
{
    return std::find(variables.begin(), variables.end(), variable)
        != variables.end();
}

inline bool variable_collection_contains(
    const std::unordered_set<std::string>& variables,
    const std::string& variable)
{
    return variables.find(variable) != variables.end();
}

// Existing parsed-CSS substitution semantics, shared with the runtime adapter.
// Compiled-only style expressions continue to use their typed evaluator.
inline std::string resolve_value(const dom_node& node,std::string value,
    const std::unordered_map<std::string,std::string>& root_variables)
    {
        struct resolution final {
            std::string value;
            bool valid{true};
            bool cycle{false};
        };
        std::unordered_set<std::string> resolving;
        const auto known_custom_value = [&](const std::string& name) {
            const std::string* known_value = nullptr;
            for (auto* current = &node; current != nullptr; current = current->parent) {
                const auto& custom = current->style.custom_properties().values;
                const auto known = custom.find(name);
                if (known != custom.end()) {
                    known_value = &known->second;
                    break;
                }
            }
            if (known_value == nullptr) {
                const auto known = root_variables.find(name);
                if (known != root_variables.end()) known_value = &known->second;
            }
            return known_value;
        };

        // Resolve custom values recursively so a dependency cycle is invalid,
        // rather than repeatedly substituting until a numeric parser happens to
        // see a remaining var() token. A cycle poisons every custom property on
        // that dependency path; the fallback belongs to the consuming var().
        std::function<resolution(std::string, bool, size_t)> resolve =
            [&](std::string current_value, bool custom_value, size_t depth) -> resolution {
                if (depth >= 128U) return {{}, false, false};
                for (;;) {
                    const auto start = current_value.find("var(");
                    if (start == std::string::npos) {
                        return {trim_value(current_value), true, false};
                    }
                    size_t close = std::string::npos;
                    int parenthesis_depth = 1;
                    for (size_t index = start + 4U; index < current_value.size(); ++index) {
                        if (current_value[index] == '(') ++parenthesis_depth;
                        else if (current_value[index] == ')'
                            && --parenthesis_depth == 0) {
                            close = index;
                            break;
                        }
                    }
                    if (close == std::string::npos) return {{}, false, false};
                    const auto content = current_value.substr(
                        start + 4U, close - start - 4U);
                    size_t comma = std::string::npos;
                    parenthesis_depth = 0;
                    for (size_t index = 0; index < content.size(); ++index) {
                        if (content[index] == '(') ++parenthesis_depth;
                        else if (content[index] == ')') --parenthesis_depth;
                        else if (content[index] == ',' && parenthesis_depth == 0) {
                            comma = index;
                            break;
                        }
                    }
                    const auto name = trim_value(
                        std::string_view(content).substr(0, comma));
                    resolution replacement{{}, false, false};
                    if (resolving.contains(name)) {
                        replacement.cycle = true;
                    } else if (const auto* known_value = known_custom_value(name);
                               known_value != nullptr) {
                        resolving.insert(name);
                        replacement = resolve(*known_value, true, depth + 1U);
                        resolving.erase(name);
                    }
                    if (!replacement.valid) {
                        // A custom property's dependency graph remains cyclic
                        // even when one edge has a fallback. The declaration
                        // consuming that invalid property may use its own fallback.
                        if (replacement.cycle && custom_value) return replacement;
                        if (comma == std::string::npos) return replacement;
                        replacement = resolve(
                            trim_value(std::string_view(content).substr(comma + 1U)),
                            custom_value,
                            depth + 1U);
                        if (!replacement.valid) return replacement;
                    }
                    current_value.replace(
                        start, close - start + 1U, replacement.value);
                }
            };
        const auto resolved = resolve(std::move(value), false, 0U);
        return resolved.valid ? resolved.value : std::string{};
    }

inline bool registered_custom_value_is_valid(
    std::string_view value,
    registered_property_syntax syntax);

inline void seed_inline_custom_properties(
    dom_node& node,
    const std::unordered_map<std::string, registered_custom_property>* registrations = nullptr) {
    node.style.clear_custom_properties();
    node.authored_style().for_each_declaration(
        [&](const std::string& name, const std::string& value) {
            if (!name.starts_with("--")) return;
            auto& custom = node.style.mutable_custom_properties();
            auto computed_value = value;
            if (registrations != nullptr) {
                const auto registration = registrations->find(name);
                if (registration != registrations->end()
                    && !registered_custom_value_is_valid(
                        value, registration->second.syntax)) {
                    computed_value = registration->second.initial_value;
                }
            }
            custom.values[name] = std::move(computed_value);
            if (node.authored_style().important_declarations.contains(name)) {
                custom.important.insert(name);
            }
        });
}
inline bool registered_custom_value_is_valid(
    std::string_view value,
    registered_property_syntax syntax)
{
    const auto source = ascii_lower(trim_value(value));
    const auto unit = syntax == registered_property_syntax::angle
        ? std::string_view{"deg"} : std::string_view{"%"};
    if (!source.ends_with(unit)) return false;
    const auto number = source.substr(0U, source.size() - unit.size());
    if (number.empty()) return false;
    char* end = nullptr;
    const auto parsed = std::strtof(number.c_str(), &end);
    return end == number.c_str() + number.size() && std::isfinite(parsed);
}

template<typename RuleReferences>
inline void seed_relevant_registered_custom_properties(
    dom_node& node,
    const std::unordered_map<std::string, registered_custom_property>& registrations,
    RuleReferences&& rule_references)
{
    for (const auto& [name, registration] : registrations) {
        if (registration.inherits
            || node.style.custom_properties().values.contains(name)) continue;
        const auto variable_token = "var(" + name;
        auto relevant = node.authored_style().declarations.contains(name);
        if (!relevant) {
            for (const auto& [property, value]
                 : node.authored_style().declarations) {
                static_cast<void>(property);
                if (value.find(variable_token) != std::string::npos) {
                    relevant = true;
                    break;
                }
            }
        }
        relevant = relevant || rule_references(name, variable_token);
        if (!relevant) continue;
        node.style.mutable_custom_properties().values.emplace(
            name, registration.initial_value);
    }
}

inline bool apply_custom_property(
    dom_node& node,
    const css_declaration& declaration,
    const std::unordered_map<std::string, registered_custom_property>* registrations = nullptr) {
    const auto& name=declaration.name;
    if(!name.starts_with("--")) return false;
    const auto& custom=node.style.custom_properties();
    const bool existing_important=custom.important.contains(name);
    const bool existing_inline=node.authored_style().declarations.contains(name) && custom.values.contains(name);
    const bool inline_important=existing_inline && node.authored_style().important_declarations.contains(name);
    if(inline_important || (!declaration.important && (existing_important || existing_inline))) return false;
    auto& values=node.style.mutable_custom_properties();
    auto value=declaration.value;
    if (registrations != nullptr) {
        const auto registration=registrations->find(name);
        if (registration != registrations->end()
            && !registered_custom_value_is_valid(
                value, registration->second.syntax)) {
            value=registration->second.initial_value;
        }
    }
    values.values[name]=std::move(value);
    if(declaration.important) values.important.insert(name);
    else values.important.erase(name);
    return true;
}
} // namespace webscene_native::css
