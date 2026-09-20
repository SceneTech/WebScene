#pragma once
#include "webscene_css_property_mask.h"
#include <cctype>
#include <sstream>
#include <utility>

namespace webscene_native::css {
inline std::string_view serialize_touch_action(touch_action value) noexcept
{
    switch (value) {
    case touch_action::none: return "none";
    case touch_action::manipulation: return "manipulation";
    case touch_action::pan_x: return "pan-x";
    case touch_action::pan_y: return "pan-y";
    case touch_action::pan_x_y: return "pan-x pan-y";
    default: return "auto";
    }
}

inline uint8_t touch_action_axes(touch_action value) noexcept
{
    switch (value) {
    case touch_action::none: return 0U;
    case touch_action::pan_x: return 1U;
    case touch_action::pan_y: return 2U;
    default: return 3U;
    }
}

inline std::optional<touch_action> parse_touch_action(
    std::string_view value) noexcept
{
    if (value == "auto") return touch_action::automatic;
    if (value == "none") return touch_action::none;
    if (value == "manipulation") return touch_action::manipulation;
    if (value == "pan-x") return touch_action::pan_x;
    if (value == "pan-y") return touch_action::pan_y;
    if (value == "pan-x pan-y" || value == "pan-y pan-x")
        return touch_action::pan_x_y;
    return std::nullopt;
}

template<typename Decision>
bool apply_touch_action_value(dom_node& node,const std::string& name,
    const std::string& raw_value,Decision& decision)
{
    if (canonical_property_name(name) != "touch-action") return false;
    auto value = ascii_lower(trim_value(raw_value));
    if (value == "inherit") {
        node.style.mutable_textual().touch_action_value = node.parent == nullptr
            ? touch_action::automatic
            : node.parent->style.textual().touch_action_value;
        return true;
    }
    if (value == "initial" || value == "unset" || value == "revert"
        || value == "revert-layer") value = "auto";
    const auto parsed = parse_touch_action(value);
    if (!parsed.has_value()) {
        decision.classification = "invalid-authoring";
        return true;
    }
    if (*parsed == touch_action::automatic && !node.style.has_textual_data())
        return true;
    node.style.mutable_textual().touch_action_value = *parsed;
    return true;
}

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

inline std::optional<std::string> normalize_will_change_value(
    std::string_view raw_value)
{
    auto value = ascii_lower(trim_value(raw_value));
    if (value == "auto") return std::string{};
    if (value.empty() || value.size() > 256U) return std::nullopt;
    std::string result;
    auto start = size_t{0};
    auto count = size_t{0};
    while (start <= value.size()) {
        const auto comma = value.find(',', start);
        const auto token = trim_value(value.substr(
            start, comma == std::string::npos ? std::string::npos : comma - start));
        if (token.empty() || token.size() > 64U || ++count > 8U) return std::nullopt;
        const auto reserved = token == "auto" || token == "none"
            || token == "default" || token == "initial" || token == "inherit"
            || token == "unset" || token == "revert" || token == "revert-layer";
        const auto identifier = std::all_of(token.begin(), token.end(), [](unsigned char c) {
            return std::isalnum(c) != 0 || c == '-' || c == '_';
        }) && (std::isalpha(static_cast<unsigned char>(token.front())) != 0
            || token.front() == '-' || token.front() == '_');
        if (reserved || !identifier) return std::nullopt;
        if (!result.empty()) result += ", ";
        result += token;
        if (comma == std::string::npos) break;
        start = comma + 1U;
    }
    return result;
}

inline bool will_change_establishes_stacking_context(std::string_view value)
{
    auto start = size_t{0};
    while (start <= value.size()) {
        const auto comma = value.find(',', start);
        const auto token = trim_value(value.substr(
            start, comma == std::string::npos ? std::string::npos : comma - start));
        if (token == "transform" || token == "opacity" || token == "filter"
            || token == "perspective" || token == "clip-path"
            || token == "mask" || token == "mask-image"
            || token == "backdrop-filter") return true;
        if (comma == std::string::npos) break;
        start = comma + 1U;
    }
    return false;
}

template<typename Decision>
bool apply_will_change_value(dom_node& node,const std::string& name,
    const std::string& raw_value,Decision& decision)
{
    if (canonical_property_name(name) != "will-change") return false;
    auto value = ascii_lower(trim_value(raw_value));
    if (value == "inherit") {
        value = node.parent == nullptr
            ? std::string{} : node.parent->style.textual().will_change;
    } else if (value == "initial" || value == "unset" || value == "revert"
        || value == "revert-layer") {
        value = "auto";
    }
    const auto normalized = normalize_will_change_value(value.empty() ? "auto" : value);
    if (!normalized.has_value()) {
        decision.classification = "invalid-authoring";
        return true;
    }
    node.style.will_change_stacking_context =
        will_change_establishes_stacking_context(*normalized);
    if (normalized->empty() && !node.style.has_textual_data()) return true;
    node.style.mutable_textual().will_change = *normalized;
    return true;
}
} // namespace webscene_native::css
