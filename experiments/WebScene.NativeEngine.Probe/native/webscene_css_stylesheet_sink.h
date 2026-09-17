#pragma once
#include "webscene_css_declarations.h"
#include "webscene_css_matching.h"
#include "webscene_css_transitions.h"
#include "webscene_css_container_queries.h"

#include <iterator>

namespace webscene_native::css {
// Host supplies rule/keyframe storage, media capability inventory and diagnostics.
// This adapter preserves the existing runtime at-rule policy. CSS nesting is
// expanded before selector compilation; supports, layers, and containers remain
// the bounded implementations documented below.
template<typename Host>
class stylesheet_sink final : public css_syntax_sink {
public:
    stylesheet_sink(
        Host& owner,
        const std::string& stylesheet_address,
        size_t input_size)
        : owner_(owner), stylesheet_address_(stylesheet_address)
    {
        stack_.reserve(8U);
        completed_keyframes_.reserve(input_size / 4096U);
    }

    bool begin_rule(
        uint32_t kind,
        bool has_block,
        size_t parent_index,
        std::string_view raw_name,
        std::string_view prelude,
        size_t& rule_index) override
    {
        const auto expected_parent = stack_.empty()
            ? css_syntax_no_parent
            : stack_.back().rule_index;
        if (parent_index != expected_parent) return false;

        rule_index = next_rule_index_++;
        frame current;
        current.rule_index = rule_index;
        current.kind = kind;
        current.active = stack_.empty() || stack_.back().children_active;
        current.children_active = current.active;
        if (!stack_.empty()) {
            current.cascade_layer = stack_.back().cascade_layer;
            current.layer_path = stack_.back().layer_path;
        }

        if (!stack_.empty() && stack_.back().keyframes
            && kind != css_syntax_style_rule) {
            current.active = false;
            current.children_active = false;
        }

        if (kind == css_syntax_style_rule) {
            current.prelude = std::string(prelude);
            if (const auto* parent = nearest_style_frame(); parent != nullptr
                && !parent->keyframes) {
                current.prelude = combine_nested_selectors(
                    parent->prelude, current.prelude);
                owner_.record_feature(
                    "css", "css-nesting", "supported",
                    "qualified nested rules with implicit descendant, combinator, and ampersand expansion",
                    "stylesheet-parser");
            }
            current.children_active = current.active;
            // Two declarations is the common generated-rule shape and
            // avoids the former IR's exact-size allocation without
            // over-reserving every small rule.
            current.declarations.reserve(2U);
            stack_.push_back(std::move(current));
            return true;
        }

        if (!current.active) {
            current.children_active = false;
            stack_.push_back(std::move(current));
            return true;
        }

        const auto name = ascii_lower(raw_name);
        if (name == "keyframes" || name == "-webkit-keyframes") {
            current.keyframes = true;
            current.prelude = std::string(prelude);
        } else if (name == "font-face") {
            current.children_active = false;
            owner_.record_feature(
                "css", "at-rule:@font-face", "supported",
                "font-family and first src url are registered by the native host",
                "stylesheet-parser");
        } else if (name == "media") {
            current.media_query = trim_value(prelude);
            const auto supported = owner_.inventory_media_query(current.media_query);
            owner_.record_feature(
                "css", "at-rule:@media",
                supported ? "supported" : "unsupported",
                supported ? std::string{} : "unsupported media type or condition",
                "stylesheet-parser");
        } else if (name == "supports") {
            auto condition = ascii_lower(trim_value(prelude));
            auto negated = false;
            if (condition.starts_with("not ")) {
                negated = true;
                condition = trim_value(std::string_view(condition).substr(4U));
            }
            const auto supported = condition == "selector(:focus-visible)";
            owner_.record_feature(
                "css", "at-rule:@supports",
                supported ? "supported" : "unsupported",
                supported ? "selector(:focus-visible)" : "condition is not evaluated",
                "stylesheet-parser");
            current.children_active = supported != negated;
        } else if (name == "layer") {
            const auto qualify = [&](std::string_view local_name) {
                const auto local = trim_value(local_name);
                if (local.empty() || current.layer_path.empty()) return local;
                return current.layer_path + "." + local;
            };
            if (has_block) {
                current.layer_path = qualify(prelude);
                current.cascade_layer = owner_.register_cascade_layer(
                    current.layer_path);
            } else {
                size_t start = 0U;
                while (start <= prelude.size()) {
                    auto comma = prelude.find(',', start);
                    if (comma == std::string_view::npos) comma = prelude.size();
                    const auto layer_name = qualify(
                        prelude.substr(start, comma - start));
                    if (!layer_name.empty()) {
                        owner_.register_cascade_layer(layer_name);
                    }
                    if (comma == prelude.size()) break;
                    start = comma + 1U;
                }
                current.children_active = false;
            }
            owner_.record_feature(
                "css", "at-rule:@layer", "supported",
                "named, anonymous, statement, nested, normal and important author layers",
                "stylesheet-parser");
        } else if (name == "container") {
            current.media_query = encode_container_query(trim_value(prelude));
            owner_.record_feature(
                "css", "at-rule:@container", "supported",
                "named and unnamed size conditions are evaluated against the nearest eligible ancestor",
                "stylesheet-parser");
        } else {
            owner_.record_feature(
                "css", "at-rule:@" + name, "unsupported", {},
                "stylesheet-parser");
            current.children_active = false;
        }
        stack_.push_back(std::move(current));
        return true;
    }

    bool declaration(
        std::string_view name,
        std::string_view value,
        bool important) override
    {
        if (stack_.empty()) return false;
        auto& current = stack_.back();
        ++current.observed_declarations;
        if (current.active && current.kind == css_syntax_style_rule) {
            append_declaration(
                current.declarations, name, value, important);
        }
        return true;
    }

    bool end_rule(size_t rule_index, size_t declaration_count) override
    {
        if (stack_.empty() || stack_.back().rule_index != rule_index
            || stack_.back().observed_declarations != declaration_count) {
            return false;
        }
        auto current = std::move(stack_.back());
        stack_.pop_back();
        if (!current.active) return true;

        if (current.kind == css_syntax_style_rule) {
            if (!stack_.empty() && stack_.back().keyframes) {
                append_keyframe(
                    stack_.back().keyframe_definition,
                    std::move(current.prelude),
                    current.declarations);
                return true;
            }
            std::vector<std::string> inherited_media;
            inherited_media.reserve(stack_.size());
            for (const auto& ancestor : stack_) {
                if (!ancestor.media_query.empty()) {
                    inherited_media.push_back(ancestor.media_query);
                }
            }
            std::vector<completed_style_rule> completed;
            completed.reserve(1U + current.nested_rules.size());
            if (!current.declarations.empty()) {
                completed.push_back({
                    std::move(current.prelude),
                    std::move(current.declarations),
                    std::move(inherited_media),
                    current.cascade_layer});
            }
            completed.insert(
                completed.end(),
                std::make_move_iterator(current.nested_rules.begin()),
                std::make_move_iterator(current.nested_rules.end()));
            if (auto* parent = nearest_style_frame(); parent != nullptr
                && !parent->keyframes) {
                parent->nested_rules.insert(
                    parent->nested_rules.end(),
                    std::make_move_iterator(completed.begin()),
                    std::make_move_iterator(completed.end()));
                return true;
            }
            for (auto& rule : completed) {
                owner_.append_parsed_css_style_rule(
                    std::move(rule.selector),
                    std::move(rule.declarations),
                    rule.media_queries,
                    stylesheet_address_,
                    rule.cascade_layer);
            }
        } else if (current.keyframes) {
            completed_keyframes_.emplace_back(
                std::move(current.prelude),
                std::move(current.keyframe_definition));
        }
        return true;
    }

    bool complete() const noexcept { return stack_.empty(); }

    std::vector<std::pair<std::string, css_opacity_keyframes>>& keyframes()
    {
        return completed_keyframes_;
    }

private:
    struct completed_style_rule final {
        std::string selector;
        std::vector<css_declaration> declarations;
        std::vector<std::string> media_queries;
        uint32_t cascade_layer{0U};
    };

    struct frame final {
        size_t rule_index{css_syntax_no_parent};
        uint32_t kind{css_syntax_style_rule};
        std::string prelude;
        std::string media_query;
        std::string layer_path;
        std::vector<css_declaration> declarations;
        std::vector<completed_style_rule> nested_rules;
        css_opacity_keyframes keyframe_definition;
        size_t observed_declarations{0U};
        uint32_t cascade_layer{0U};
        bool active{false};
        bool children_active{false};
        bool keyframes{false};
    };

    frame* nearest_style_frame() noexcept
    {
        for (auto iterator = stack_.rbegin(); iterator != stack_.rend(); ++iterator) {
            if (iterator->keyframes) return nullptr;
            if (iterator->kind == css_syntax_style_rule) return &*iterator;
        }
        return nullptr;
    }

    const frame* nearest_style_frame() const noexcept
    {
        for (auto iterator = stack_.rbegin(); iterator != stack_.rend(); ++iterator) {
            if (iterator->keyframes) return nullptr;
            if (iterator->kind == css_syntax_style_rule) return &*iterator;
        }
        return nullptr;
    }

    static std::vector<std::string> split_selector_list(std::string_view value)
    {
        std::vector<std::string> result;
        size_t start = 0U;
        size_t parentheses = 0U;
        size_t brackets = 0U;
        char quote = '\0';
        auto escaped = false;
        for (size_t index = 0U; index <= value.size(); ++index) {
            const auto character = index < value.size() ? value[index] : ',';
            if (escaped) {
                escaped = false;
                continue;
            }
            if (character == '\\') {
                escaped = true;
                continue;
            }
            if (quote != '\0') {
                if (character == quote) quote = '\0';
                continue;
            }
            if (character == '\'' || character == '"') {
                quote = character;
                continue;
            }
            if (character == '(') ++parentheses;
            else if (character == ')' && parentheses > 0U) --parentheses;
            else if (character == '[') ++brackets;
            else if (character == ']' && brackets > 0U) --brackets;
            else if (character == ',' && parentheses == 0U && brackets == 0U) {
                auto selector = trim_value(value.substr(start, index - start));
                if (!selector.empty()) result.push_back(std::move(selector));
                start = index + 1U;
            }
        }
        return result;
    }

    static std::string replace_nesting_selector(
        std::string child,
        const std::string& parent)
    {
        size_t offset = 0U;
        while ((offset = child.find('&', offset)) != std::string::npos) {
            child.replace(offset, 1U, parent);
            offset += parent.size();
        }
        return child;
    }

    static std::string combine_nested_selectors(
        std::string_view parent,
        std::string_view child)
    {
        const auto parents = split_selector_list(parent);
        const auto children = split_selector_list(child);
        if (parents.empty() || children.empty()) return {};
        std::string parent_reference;
        if (parents.size() == 1U) {
            parent_reference = parents.front();
        } else {
            parent_reference = ":is(";
            for (const auto& selector : parents) {
                if (parent_reference.size() > 4U) parent_reference += ", ";
                parent_reference += selector;
            }
            parent_reference += ')';
        }
        std::string result;
        for (const auto& child_selector : children) {
            if (!result.empty()) result += ", ";
            if (child_selector.find('&') != std::string::npos) {
                result += replace_nesting_selector(
                    child_selector, parent_reference);
            } else {
                result += parent_reference;
                result += ' ';
                result += child_selector;
            }
        }
        return result;
    }

    Host& owner_;
    const std::string& stylesheet_address_;
    std::vector<frame> stack_;
    std::vector<std::pair<std::string, css_opacity_keyframes>> completed_keyframes_;
    size_t next_rule_index_{0U};
};
} // namespace webscene_native::css
