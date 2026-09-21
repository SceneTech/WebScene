#pragma once

#include "webscene_native_dom.h"

namespace webscene_native::css {

// Anchor tokens are cold: only positioned elements that author anchor()
// functions retain a value, while ordinary node_style records stay compact.
inline void retain_anchor_function(node_style& style,
    const std::string& property, const std::string& value)
{
    if (value.starts_with("anchor(") || value.starts_with("anchor-size(")) {
        style.mutable_textual().effect_values[property] = value;
    } else if (auto* textual = style.mutable_textual_if_present(); textual != nullptr) {
        textual->effect_values.erase(property);
    }
}

inline void retain_anchor_identifier(node_style& style,
    const std::string& property, const std::string& value)
{
    const auto valid = value.starts_with("--")
        && value.find_first_of(" \t\r\n,()") == std::string::npos;
    if (valid) {
        style.mutable_textual().effect_values[property] = value;
    } else if (auto* textual = style.mutable_textual_if_present(); textual != nullptr) {
        textual->effect_values.erase(property);
    }
    if (property == "anchor-name") style.anchor_name_present = valid;
    if (property == "position-anchor") style.position_anchor_present = valid;
}

inline std::string_view retained_anchor_value(const node_style& style,
    std::string_view property)
{
    const auto* textual = style.textual_data_identity();
    if (textual == nullptr) return {};
    const auto known = textual->effect_values.find(std::string(property));
    return known == textual->effect_values.end()
        ? std::string_view{} : std::string_view{known->second};
}

} // namespace webscene_native::css
