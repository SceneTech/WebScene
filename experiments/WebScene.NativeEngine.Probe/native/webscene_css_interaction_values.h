#pragma once
#include "webscene_css_property_mask.h"
#include <sstream>
#include <utility>

namespace webscene_native::css {
template<typename Decision>
bool apply_user_select_value(dom_node& node,const std::string& name,
    const std::string& raw_value,Decision& decision)
{
    if (canonical_property_name(name) != "user-select") return false;
    auto value = ascii_lower(trim_value(raw_value));
    if (value == "inherit") {
        value = node.parent == nullptr ? std::string{}
            : node.parent->style.textual().user_select;
    } else if (value == "initial" || value == "unset" || value == "revert"
        || value == "revert-layer") {
        value.clear();
    }
    if (!value.empty() && value != "auto" && value != "text"
        && value != "none" && value != "all") {
        decision.classification = "invalid-authoring";
        return true;
    }
    node.style.mutable_textual().user_select = value == "auto"
        ? std::string{} : std::move(value);
    return true;
}

inline std::string_view serialize_overscroll_behavior(
    overscroll_behavior value) noexcept
{
    switch (value) {
    case overscroll_behavior::contain: return "contain";
    case overscroll_behavior::none: return "none";
    default: return "auto";
    }
}

inline std::optional<overscroll_behavior> parse_overscroll_behavior(
    std::string_view value) noexcept
{
    if (value == "auto") return overscroll_behavior::automatic;
    if (value == "contain") return overscroll_behavior::contain;
    if (value == "none") return overscroll_behavior::none;
    return std::nullopt;
}

inline std::string serialize_overscroll_shorthand(const node_style& style)
{
    const auto x = serialize_overscroll_behavior(style.textual().overscroll_x);
    const auto y = serialize_overscroll_behavior(style.textual().overscroll_y);
    return x == y ? std::string(x) : std::string(x) + " " + std::string(y);
}

template<typename Decision>
bool apply_overscroll_behavior_value(dom_node& node,const std::string& name,
    const std::string& raw_value,Decision& decision)
{
    const auto property = canonical_property_name(name);
    if (property != "overscroll-behavior"
        && property != "overscroll-behavior-x"
        && property != "overscroll-behavior-y") return false;
    auto value = ascii_lower(trim_value(raw_value));
    const auto inherited = value == "inherit";
    const auto initial = value == "initial" || value == "unset"
        || value == "revert" || value == "revert-layer";
    const auto parent_x = node.parent == nullptr
        ? overscroll_behavior::automatic
        : node.parent->style.textual().overscroll_x;
    const auto parent_y = node.parent == nullptr
        ? overscroll_behavior::automatic
        : node.parent->style.textual().overscroll_y;
    auto x = node.style.textual().overscroll_x;
    auto y = node.style.textual().overscroll_y;
    if (inherited || initial) {
        const auto reset_x = inherited ? parent_x : overscroll_behavior::automatic;
        const auto reset_y = inherited ? parent_y : overscroll_behavior::automatic;
        if (property != "overscroll-behavior-y") x = reset_x;
        if (property != "overscroll-behavior-x") y = reset_y;
    } else if (property == "overscroll-behavior") {
        std::istringstream stream(value);
        std::string first;
        std::string second;
        std::string extra;
        stream >> first >> second >> extra;
        const auto parsed_x = parse_overscroll_behavior(first);
        const auto parsed_y = parse_overscroll_behavior(
            second.empty() ? first : second);
        if (!parsed_x.has_value() || !parsed_y.has_value() || !extra.empty()) {
            decision.classification = "invalid-authoring";
            return true;
        }
        x = *parsed_x;
        y = *parsed_y;
    } else {
        const auto parsed = parse_overscroll_behavior(value);
        if (!parsed.has_value()) {
            decision.classification = "invalid-authoring";
            return true;
        }
        if (property == "overscroll-behavior-x") x = *parsed;
        else y = *parsed;
    }
    if (x == overscroll_behavior::automatic
        && y == overscroll_behavior::automatic
        && !node.style.has_textual_data()) return true;
    auto& textual = node.style.mutable_textual();
    textual.overscroll_x = x;
    textual.overscroll_y = y;
    return true;
}

template<typename Decision>
bool apply_isolation_value(dom_node& node,const std::string& name,
    const std::string& raw_value,Decision& decision)
{
    if (canonical_property_name(name) != "isolation") return false;
    auto value = ascii_lower(trim_value(raw_value));
    if (value == "inherit") {
        node.style.isolation_stacking_context = node.parent != nullptr
            && node.parent->style.isolation_stacking_context;
        return true;
    }
    if (value == "initial" || value == "unset" || value == "revert"
        || value == "revert-layer") value = "auto";
    if (value != "auto" && value != "isolate") {
        decision.classification = "invalid-authoring";
        return true;
    }
    node.style.isolation_stacking_context = value == "isolate";
    return true;
}
} // namespace webscene_native::css
