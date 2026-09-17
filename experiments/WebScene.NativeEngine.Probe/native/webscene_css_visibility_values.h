#pragma once
#include "webscene_css_property_mask.h"

namespace webscene_native::css {
inline overflow_mode parse_overflow_mode(std::string_view value)
{
    if (value == "hidden") return overflow_mode::hidden;
    if (value == "clip") return overflow_mode::clip;
    if (value == "auto") return overflow_mode::automatic;
    if (value == "scroll") return overflow_mode::scroll;
    return overflow_mode::visible;
}

inline std::pair<overflow_mode, overflow_mode> computed_overflow(const node_style& style)
{
    auto x = style.overflow_x;
    auto y = style.overflow_y;
    const auto neither_visible_nor_clip = [](overflow_mode value) {
        return value != overflow_mode::visible && value != overflow_mode::clip;
    };
    if (x == overflow_mode::visible && neither_visible_nor_clip(y)) x = overflow_mode::automatic;
    else if (x == overflow_mode::clip && neither_visible_nor_clip(y)) x = overflow_mode::hidden;
    if (y == overflow_mode::visible && neither_visible_nor_clip(x)) y = overflow_mode::automatic;
    else if (y == overflow_mode::clip && neither_visible_nor_clip(x)) y = overflow_mode::hidden;
    return {x, y};
}

inline void refresh_overflow_state(node_style& style)
{
    const auto [x, y] = computed_overflow(style);
    style.clip = x != overflow_mode::visible || y != overflow_mode::visible;
    style.scroll_x_enabled = x == overflow_mode::automatic || x == overflow_mode::scroll;
    style.scroll_y_enabled = y == overflow_mode::automatic || y == overflow_mode::scroll;
}

template<typename Protected>
bool apply_visibility_value(native_document& document,dom_node& node,
    const std::string& name,const std::string& value,Protected&& is_inline)
{
    if ((name == "overflow" || name == "overflow-x" || name == "overflow-y")
            && !is_inline(inline_overflow)) {
            const auto explicit_document_element = node.tag == "html"
                && node.parent == &document.body();
            if (value == "inherit" && node.parent != nullptr
                && !explicit_document_element) {
                if (name == "overflow") {
                    node.style.overflow_x = node.parent->style.overflow_x;
                    node.style.overflow_y = node.parent->style.overflow_y;
                } else if (name == "overflow-x") {
                    node.style.overflow_x = node.parent->style.overflow_x;
                } else {
                    node.style.overflow_y = node.parent->style.overflow_y;
                }
            } else if (name == "overflow") {
                std::istringstream stream(value);
                std::string x;
                std::string y;
                stream >> x >> y;
                node.style.overflow_x = parse_overflow_mode(x);
                node.style.overflow_y = parse_overflow_mode(y.empty() ? x : y);
            } else if (name == "overflow-x") {
                node.style.overflow_x = parse_overflow_mode(value);
            } else {
                node.style.overflow_y = parse_overflow_mode(value);
            }
            refresh_overflow_state(node.style);
        } else if (name == "contain" && !is_inline(inline_contain)) {
            auto contain = ascii_lower(value);
            if (contain == "inherit" && node.parent != nullptr) {
                contain = node.parent->style.textual().contain_value;
            }
            if (contain == "initial" || contain == "unset" || contain == "revert"
                || contain.empty()) {
                contain = "none";
            }
            node.style.mutable_textual().contain_value = contain;
            node.style.contain_stacking_context = contain == "content"
                || contain == "strict"
                || (" " + contain + " ").find(" layout ") != std::string::npos
                || (" " + contain + " ").find(" paint ") != std::string::npos;
        } else if (name == "container" && !is_inline(inline_containment_features)) {
            const auto slash = value.find('/');
            auto names = ascii_lower(value.substr(0, slash));
            names.erase(0, names.find_first_not_of(" \t\r\n"));
            if (const auto last = names.find_last_not_of(" \t\r\n"); last != std::string::npos) {
                names.erase(last + 1U);
            }
            auto type = slash == std::string::npos ? std::string{"normal"}
                : ascii_lower(value.substr(slash + 1U));
            type.erase(0, type.find_first_not_of(" \t\r\n"));
            if (const auto last = type.find_last_not_of(" \t\r\n"); last != std::string::npos) {
                type.erase(last + 1U);
            }
            if (type != "inline-size" && type != "size") type = "normal";
            node.style.mutable_textual().container_name = names.empty() ? "none" : std::move(names);
            node.style.mutable_textual().container_type = std::move(type);
        } else if (name == "container-type" && !is_inline(inline_containment_features)) {
            auto type = ascii_lower(value);
            if (type != "inline-size" && type != "size") type = "normal";
            node.style.mutable_textual().container_type = std::move(type);
        } else if (name == "container-name" && !is_inline(inline_containment_features)) {
            auto names = ascii_lower(value);
            if (names.empty() || names == "initial" || names == "unset"
                || names == "revert") names = "none";
            node.style.mutable_textual().container_name = std::move(names);
        } else if (name == "content-visibility" && !is_inline(inline_containment_features)) {
            auto visibility = ascii_lower(value);
            if (visibility != "hidden" && visibility != "auto") visibility = "visible";
            node.style.mutable_textual().content_visibility = visibility;
            node.style.content_visibility_hidden = visibility == "hidden";
        } else if (name == "contain-intrinsic-size" && !is_inline(inline_containment_features)) {
            node.style.mutable_textual().contain_intrinsic_size =
                native_document::parse_length(value);
        } else if (name == "visibility" && !is_inline(inline_visibility)) {
            node.style.visibility_specified = value != "inherit" && value != "unset";
            node.style.visibility_hidden = value == "hidden" || value == "collapse";
        } else if (name == "pointer-events" && !is_inline(inline_pointer_events)) {
            node.style.pointer_events_specified = value != "inherit" && value != "unset";
            node.style.pointer_events_none = value == "none";
        } else if (name == "opacity" && !is_inline(inline_opacity)) {
            if (value == "inherit" && node.parent != nullptr) {
                node.style.opacity = node.parent->style.opacity;
            } else if (value == "initial" || value == "unset"
                || value == "revert" || value == "revert-layer") {
                node.style.opacity = 1.0F;
            } else {
                node.style.opacity = std::clamp(
                    std::strtof(value.c_str(), nullptr), 0.0F, 1.0F);
            }
        } else { return false; }
        return true;
}
} // namespace webscene_native::css
