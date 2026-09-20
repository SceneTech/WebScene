#pragma once
#include "webscene_css_state.h"
#include <array>
#include <span>

namespace webscene_native::css {
inline bool cascade_layer_precedes(
    const css_rule& left, const css_rule& right, bool important) noexcept
{
    const auto left_layer = left.cascade_layer_order;
    const auto right_layer = right.cascade_layer_order;
    if (left_layer == right_layer) return false;
    if (important) {
        // Important layer order is reversed, and every layered important
        // declaration outranks the unlayered important tier.
        if (left_layer == 0U) return true;
        if (right_layer == 0U) return false;
        return left_layer > right_layer;
    }
    // Ordinary unlayered declarations outrank every named/anonymous layer.
    if (left_layer == 0U) return false;
    if (right_layer == 0U) return true;
    return left_layer < right_layer;
}

inline bool cascade_rule_precedes(
    const css_rule* left, const css_rule* right, bool important) noexcept
{
    if (left->cascade_layer_order != right->cascade_layer_order) {
        return cascade_layer_precedes(*left, *right, important);
    }
    const auto left_specificity = left->specificity();
    const auto right_specificity = right->specificity();
    return left_specificity != right_specificity
        ? left_specificity < right_specificity
        : left < right;
}

inline bool cascade_keyword_is(std::string_view value, std::string_view keyword) noexcept
{
    value = trim_css_view(value);
    return value.size() == keyword.size()
        && std::equal(
            value.begin(), value.end(), keyword.begin(),
            [](unsigned char left, unsigned char right) {
                return std::tolower(left) == std::tolower(right);
            });
}

struct cascaded_rule_order final {
    std::span<const css_rule* const> normal;
    std::vector<const css_rule*> important_storage;
    bool custom_rollback{};
    bool ordinary_rollback{};

    explicit cascaded_rule_order(std::span<const css_rule* const> rules)
        : normal(rules)
    {
        bool has_layers = false;
        for (const auto* rule : rules) {
            has_layers = has_layers || rule->cascade_layer_order != 0U;
            for (const auto& declaration : rule->declarations()) {
                if (!cascade_keyword_is(declaration.value, "revert")
                    && !cascade_keyword_is(declaration.value, "revert-layer")) continue;
                if (declaration.name.starts_with("--")) custom_rollback = true;
                else ordinary_rollback = true;
            }
        }
        if (!has_layers) return;
        important_storage.assign(rules.begin(), rules.end());
        std::sort(
            important_storage.begin(), important_storage.end(),
            [](const css_rule* left, const css_rule* right) {
                return cascade_rule_precedes(left, right, true);
            });
    }

    std::span<const css_rule* const> important() const noexcept
    {
        return important_storage.empty()
            ? normal
            : std::span<const css_rule* const>(important_storage);
    }

    bool has_rollback(bool custom) const noexcept
    {
        return custom ? custom_rollback : ordinary_rollback;
    }
};

struct cascade_layer_property final {
    uint32_t layer{};
    std::string_view property;
    bool operator==(const cascade_layer_property&) const noexcept = default;
};

struct cascade_layer_property_hash final {
    size_t operator()(const cascade_layer_property& value) const noexcept
    {
        const auto name_hash = std::hash<std::string_view>{}(value.property);
        return name_hash ^ (static_cast<size_t>(value.layer) + 0x9e3779b9U
            + (name_hash << 6U) + (name_hash >> 2U));
    }
};

// Rollback is defined over effective longhands, not the authored spelling.
// Keep expansion on the rare rollback path: the ordinary cascade remains a
// zero-expansion declaration replay, while a sheet containing revert keywords
// pays the small amount of parsing needed to distinguish shorthand components.
template<typename Apply>
void for_each_effective_declaration(
    const css_declaration& declaration,
    Apply&& apply)
{
    for_each_effective_property_component(
        declaration.name,
        declaration.value,
        [&](std::string_view name, std::string_view value) {
            css_declaration effective{
                std::string(name), std::string(value), declaration.important};
            apply(effective, name);
        });
}

struct cascade_rollback_winner final {
    size_t sequence{};
    bool layer_rollback{};
    bool origin_rollback{};
};

template<typename Apply, typename PropertyKey>
void for_each_cascaded_declaration(
    const cascaded_rule_order& order,
    bool custom,
    Apply&& apply,
    PropertyKey&& property_key)
{
        const auto apply_declarations = [&](const auto rules, bool important) {
            for (const auto* rule : rules) {
                for (const auto& declaration : rule->declarations()) {
                    if (declaration.name.starts_with("--") == custom
                        && declaration.important == important) {
                        apply(declaration);
                    }
                }
            }
        };
        if (!order.has_rollback(custom)) {
            // This is the hot layered path: one rule-order vector is shared by
            // custom and ordinary replay, with no per-property maps or strings.
            apply_declarations(order.normal, false);
            apply_declarations(order.important(), true);
            return;
        }

        const auto apply_tier = [&](const auto rules, bool important) {
            using layer_map = std::unordered_map<
                cascade_layer_property,
                cascade_rollback_winner,
                cascade_layer_property_hash>;
            layer_map layer_winners;
            std::unordered_map<uint32_t, cascade_rollback_winner> layer_all_winners;
            std::unordered_map<std::string_view, cascade_rollback_winner> origin_winners;
            std::optional<cascade_rollback_winner> origin_all_winner;
            size_t sequence = 0U;
            for (const auto* rule : rules) {
                for (const auto& declaration : rule->declarations()) {
                    if (declaration.name.starts_with("--") != custom
                        || declaration.important != important) {
                        continue;
                    }
                    for_each_effective_declaration(
                        declaration,
                        [&](const css_declaration& effective,
                            std::string_view effective_name) {
                            const auto mapped_name =
                                std::string_view(property_key(effective));
                            const auto name = mapped_name == effective.name
                                ? effective_name : mapped_name;
                            const auto revert = cascade_keyword_is(
                                effective.value, "revert");
                            const auto revert_layer = cascade_keyword_is(
                                effective.value, "revert-layer");
                            const cascade_rollback_winner winner{
                                ++sequence,
                                revert || revert_layer,
                                revert || (revert_layer
                                    && rule->cascade_layer_order == 0U)};
                            if (!custom && name == "all") {
                                layer_all_winners[rule->cascade_layer_order] = winner;
                                origin_all_winner = winner;
                            } else {
                                layer_winners[{rule->cascade_layer_order, name}] = winner;
                                origin_winners[name] = winner;
                            }
                        });
                }
            }
            const auto later = [](const cascade_rollback_winner* left,
                                  const cascade_rollback_winner* right) {
                if (left == nullptr) return right;
                if (right == nullptr) return left;
                return left->sequence < right->sequence ? right : left;
            };
            const auto layer_winner = [&](uint32_t layer, std::string_view name) {
                const auto explicit_winner = layer_winners.find({layer, name});
                const auto all_winner = layer_all_winners.find(layer);
                return later(
                    explicit_winner == layer_winners.end()
                        ? nullptr : &explicit_winner->second,
                    all_winner == layer_all_winners.end()
                        ? nullptr : &all_winner->second);
            };
            const auto origin_winner = [&](std::string_view name) {
                const auto explicit_winner = origin_winners.find(name);
                return later(
                    explicit_winner == origin_winners.end()
                        ? nullptr : &explicit_winner->second,
                    origin_all_winner.has_value() ? &*origin_all_winner : nullptr);
            };
            for (const auto* rule : rules) {
                for (const auto& declaration : rule->declarations()) {
                    if (declaration.name.starts_with("--") != custom
                        || declaration.important != important) {
                        continue;
                    }
                    for_each_effective_declaration(
                        declaration,
                        [&](const css_declaration& effective,
                            std::string_view effective_name) {
                            const auto mapped_name =
                                std::string_view(property_key(effective));
                            const auto name = mapped_name == effective.name
                                ? effective_name : mapped_name;
                            if (!custom && name == "all") {
                                if (!cascade_keyword_is(effective.value, "revert")
                                    && !cascade_keyword_is(
                                        effective.value, "revert-layer")) {
                                    apply(effective);
                                }
                                return;
                            }
                            const auto* layer = layer_winner(
                                rule->cascade_layer_order, name);
                            const auto* origin = origin_winner(name);
                            if ((layer != nullptr && layer->layer_rollback)
                                || (origin != nullptr && origin->origin_rollback)) {
                                return;
                            }
                            apply(effective);
                        });
                }
            }
        };
        apply_tier(order.normal, false);
        apply_tier(order.important(), true);
}

template<typename Apply, typename PropertyKey>
void for_each_cascaded_declaration(
    std::span<const css_rule* const> matched_rules,
    bool custom,
    Apply&& apply,
    PropertyKey&& property_key)
{
        const cascaded_rule_order order(matched_rules);
        for_each_cascaded_declaration(
            order,
            custom,
            std::forward<Apply>(apply),
            std::forward<PropertyKey>(property_key));
}

template<typename Apply>
void for_each_cascaded_declaration(
    const cascaded_rule_order& order,
    bool custom,
    Apply&& apply)
{
        for_each_cascaded_declaration(
            order,
            custom,
            std::forward<Apply>(apply),
            [](const css_declaration& declaration) -> std::string_view {
                return declaration.name;
            });
}

template<typename Apply>
void for_each_cascaded_declaration(
    std::span<const css_rule* const> matched_rules,
    bool custom,
    Apply&& apply)
{
        for_each_cascaded_declaration(
            matched_rules,
            custom,
            std::forward<Apply>(apply),
            [](const css_declaration& declaration) -> std::string_view {
                return declaration.name;
            });
}

template<typename Apply>
void for_each_cascaded_pseudo_declaration(
    std::span<const std::pair<int, const css_rule*>> matched_rules,
    Apply&& apply)
{
        // Each pseudo-element establishes an independent cascade. Grouping by
        // kind prevents a high-priority ::before declaration from affecting
        // ::after while reusing the same layer/importance/rollback machinery
        // as the originating element.
        std::array<std::vector<const css_rule*>, 13> rules_by_kind;
        for (const auto& [kind, rule] : matched_rules) {
            if (kind > 0 && static_cast<size_t>(kind) < rules_by_kind.size())
                rules_by_kind[static_cast<size_t>(kind)].push_back(rule);
        }
        for (size_t kind = 1; kind < rules_by_kind.size(); ++kind) {
            const auto& rules = rules_by_kind[kind];
            if (rules.empty()) continue;
            for_each_cascaded_declaration(
                std::span<const css_rule* const>(rules),
                false,
                [&](const css_declaration& declaration) {
                    apply(static_cast<int>(kind), declaration);
                });
        }
}

// Candidate identity is enough to deduplicate index buckets. Do not chase rule
// payloads for precedence until selector/media/scope checks have rejected misses.
inline void deduplicate_candidates(std::vector<size_t>& candidates) {
    std::sort(candidates.begin(), candidates.end());
    candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
}

inline void sort_candidates(std::span<const css_rule> rules,std::vector<size_t>& candidates)
    {
        // Declarations are applied in ascending precedence; later declarations
        // therefore replace earlier ones while equal specificity retains source order.
        std::sort(
            candidates.begin(),
            candidates.end(),
            [&rules](size_t left, size_t right) {
                if (rules[left].cascade_layer_order
                    != rules[right].cascade_layer_order) {
                    return cascade_layer_precedes(
                        rules[left], rules[right], false);
                }
                const auto left_specificity = rules[left].specificity();
                const auto right_specificity = rules[right].specificity();
                return left_specificity != right_specificity
                    ? left_specificity < right_specificity
                    : left < right;
            });
    }


// Preserve the ordinary runtime's root-variable policy; per-element custom
// property inheritance and substitution are handled by cascade application.
inline void rebuild_root_variables(std::span<const css_rule> rules,
    std::unordered_map<std::string,std::string>& variables,
    std::unordered_set<std::string>& important) {
        variables.clear();
        important.clear();
        std::vector<const css_rule*> root_rules;
        for (const auto& rule : rules) {
            if (!rule.media_matches || rule.shadow_scope_root_id != 0U
                || (rule.selector() != ":root" && rule.selector() != "html")) continue;
            root_rules.push_back(&rule);
        }
        std::sort(
            root_rules.begin(), root_rules.end(),
            [](const css_rule* left, const css_rule* right) {
                return cascade_rule_precedes(left, right, false);
            });
        for_each_cascaded_declaration(
            std::span<const css_rule* const>(root_rules),
            true,
            [&](const css_declaration& declaration) {
                variables[declaration.name] = declaration.value;
                if (declaration.important) important.insert(declaration.name);
                else important.erase(declaration.name);
            });
}
} // namespace webscene_native::css
