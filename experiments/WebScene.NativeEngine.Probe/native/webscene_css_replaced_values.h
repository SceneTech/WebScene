#pragma once
#include "webscene_css_property_mask.h"
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <utility>

namespace webscene_native::css {
inline bool valid_object_position_component(std::string_view token)
{
    if (token == "left" || token == "right" || token == "top"
        || token == "bottom" || token == "center") return true;
    if (token.starts_with("calc(") || token.starts_with("min(")
        || token.starts_with("max(")) return true;
    const auto value = std::string(token);
    char* end = nullptr;
    const auto number = std::strtof(value.c_str(), &end);
    if (end == value.c_str() || !std::isfinite(number)) return false;
    const std::string_view unit(end);
    return (number == 0.0F && unit.empty()) || unit == "%" || unit == "px"
        || unit == "em" || unit == "rem" || unit == "vw" || unit == "vh"
        || unit == "cqw" || unit == "cqh" || unit == "cqi" || unit == "cqb"
        || unit == "cqmin" || unit == "cqmax" || unit == "in" || unit == "cm"
        || unit == "mm" || unit == "pt" || unit == "pc" || unit == "q";
}

inline bool horizontal_object_position_keyword(std::string_view token)
{
    return token == "left" || token == "right";
}

inline bool vertical_object_position_keyword(std::string_view token)
{
    return token == "top" || token == "bottom";
}

inline std::string canonical_object_position_component(std::string value)
{
    if (value == "left" || value == "top") return "0%";
    if (value == "center") return "50%";
    if (value == "right" || value == "bottom") return "100%";
    return value;
}

inline bool normalize_object_position(std::string_view raw_value,
    std::string& normalized)
{
    std::istringstream tokens(std::string{raw_value});
    std::string first;
    std::string second;
    std::string extra;
    tokens >> first >> second >> extra;
    if (first.empty() || !extra.empty() || !valid_object_position_component(first)
        || (!second.empty() && !valid_object_position_component(second))) {
        return false;
    }
    if (second.empty()) {
        if (vertical_object_position_keyword(first)) {
            second = first;
            first = "50%";
        } else {
            second = "50%";
        }
    } else {
        if ((horizontal_object_position_keyword(first)
                && horizontal_object_position_keyword(second))
            || (vertical_object_position_keyword(first)
                && vertical_object_position_keyword(second))) {
            return false;
        }
        if (vertical_object_position_keyword(first)
            || horizontal_object_position_keyword(second)) {
            std::swap(first, second);
        }
    }
    normalized = canonical_object_position_component(std::move(first)) + " "
        + canonical_object_position_component(std::move(second));
    return true;
}

inline bool valid_object_position_value(std::string_view raw_value)
{
    const auto value = ascii_lower(trim_value(raw_value));
    if (value.find("var(") != std::string::npos) return true;
    if (value == "inherit" || value == "initial" || value == "unset"
        || value == "revert" || value == "revert-layer") return true;
    std::string normalized;
    return normalize_object_position(value, normalized);
}

template<typename Decision>
bool apply_replaced_value(dom_node& node,const std::string& name,
    const std::string& raw_value,Decision& decision)
{
    if (name != "object-fit" && name != "object-position") return false;
    auto value = ascii_lower(trim_value(raw_value));
    if (value == "inherit") {
        value = node.parent == nullptr ? std::string{}
            : name == "object-fit" ? node.parent->style.textual().object_fit
            : node.parent->style.textual().object_position;
    } else if (value == "initial" || value == "unset" || value == "revert"
        || value == "revert-layer") {
        value.clear();
    }
    if (name == "object-fit") {
        if (!value.empty() && value != "fill" && value != "contain"
            && value != "cover" && value != "none" && value != "scale-down") {
            decision.classification = "invalid-authoring";
            return true;
        }
        node.style.mutable_textual().object_fit = std::move(value);
        return true;
    }
    if (!value.empty()) {
        std::string normalized;
        if (!normalize_object_position(value, normalized)) {
            decision.classification = "invalid-authoring";
            return true;
        }
        value = std::move(normalized);
    }
    node.style.mutable_textual().object_position = std::move(value);
    return true;
}
} // namespace webscene_native::css
