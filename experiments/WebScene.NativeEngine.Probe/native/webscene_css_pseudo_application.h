#pragma once
#include "webscene_css_pseudo_values.h"
#include "webscene_css_text_values.h"
#include "webscene_css_transitions.h"
#include "webscene_css_variables.h"

namespace webscene_native::css {
inline property_result apply_backdrop_declaration(dom_node& node,const css_declaration& declaration,
    const std::unordered_map<std::string,std::string>& variables,bool& important) {
    property_result result;result.classification="unsupported";
    if(node.tag!="dialog" || (declaration.name!="background" && declaration.name!="background-color"))return result;
    if(important && !declaration.important){result.classification="supported";return result;}
    const auto value=resolve_value(node,declaration.value,variables);
    const auto color=native_document::parse_color(value);
    if(is_explicit_color_token(value,color)) {
        if(!node.dialog_state)node.dialog_state=std::make_unique<dom_node::dialog_data>();
        node.dialog_state->backdrop_rgba=color;important=declaration.important;
        result.classification="supported";
    }
    return result;
}
inline int split_pseudo_element_selector(const std::string& selector, std::string& origin)
    {
        const auto split_suffix = [&](std::string_view suffix, int kind) {
            if (!selector.ends_with(suffix)) return 0;
            origin = trim_value(std::string_view(selector).substr(0, selector.size() - suffix.size()));
            return kind;
        };
        if (const auto kind = split_suffix("::before", 1); kind != 0) return kind;
        if (const auto kind = split_suffix("::after", 2); kind != 0) return kind;
        if (const auto kind = split_suffix("::placeholder", 8); kind != 0) return kind;
        if (const auto kind = split_suffix("::details-content", 9); kind != 0) return kind;
        if (const auto kind = split_suffix("::backdrop", 7); kind != 0) return kind;
        if (const auto kind = split_suffix("::-webkit-scrollbar-thumb", 4); kind != 0) return kind;
        if (const auto kind = split_suffix("::-webkit-scrollbar-track", 5); kind != 0) return kind;
        if (const auto kind = split_suffix("::-webkit-scrollbar-corner", 6); kind != 0) return kind;
        if (const auto kind = split_suffix("::-webkit-scrollbar", 3); kind != 0) return kind;
        if (const auto kind = split_suffix(":before", 1); kind != 0) return kind;
        return split_suffix(":after", 2);
    }

inline bool split_custom_highlight_selector(
    std::string_view selector,
    std::string& origin,
    std::string& name)
{
    constexpr auto prefix = std::string_view{"::highlight("};
    const auto marker = selector.rfind(prefix);
    if (marker == std::string_view::npos || !selector.ends_with(')')) return false;
    const auto raw_name = trim_value(
        selector.substr(marker + prefix.size(), selector.size() - marker - prefix.size() - 1U));
    if (raw_name.empty()) return false;
    const auto valid_start = [](unsigned char value) {
        return std::isalpha(value) != 0 || value == '_' || value == '-';
    };
    const auto valid_rest = [&](unsigned char value) {
        return valid_start(value) || std::isdigit(value) != 0;
    };
    if (!valid_start(static_cast<unsigned char>(raw_name.front()))
        || !std::all_of(raw_name.begin() + 1U, raw_name.end(), [&](char value) {
            return valid_rest(static_cast<unsigned char>(value));
        })) return false;
    origin = trim_value(selector.substr(0U, marker));
    name = raw_name;
    return true;
}

inline bool split_selection_selector(
    std::string_view selector,
    std::string& origin)
{
    constexpr auto standard = std::string_view{"::selection"};
    constexpr auto moz = std::string_view{"::-moz-selection"};
    const auto suffix = selector.ends_with(standard)
        ? standard : selector.ends_with(moz) ? moz : std::string_view{};
    if (suffix.empty()) return false;
    origin = trim_value(selector.substr(0U, selector.size() - suffix.size()));
    return true;
}

template<typename Decision,typename Resolved>
void apply_details_content_declaration(
    dom_node& node,
    node_style::pseudo_element_pair::details_content_element& details_content,
    const css_declaration& declaration,
    const std::unordered_map<std::string,std::string>& variables,
    Decision& decision,
    Resolved&& on_resolved)
{
    decision.classification = "unsupported";
    const auto contains_variable = declaration.value.find("var(") != std::string::npos;
    auto resolved_value = std::string{};
    const auto& value = contains_variable
        ? (resolved_value = resolve_value(node,declaration.value,variables))
        : declaration.value;
    if (value.empty() && contains_variable) {
        decision.classification = "invalid-authoring";
        decision.semantic_slice = "unresolved custom property at computed-value time";
        return;
    }
    on_resolved(contains_variable);
    const auto lower = ascii_lower(trim_value(value));
    details_content.present = true;
    if (declaration.name == "block-size") {
        if (lower == "auto") {
            details_content.block_size_zero = false;
            decision.classification = "supported";
        } else if (lower == "0") {
            details_content.block_size_zero = true;
            decision.classification = "supported";
        } else {
            decision.classification = "unsupported";
            decision.semantic_slice = "authored zero or auto block-size";
        }
    } else if (declaration.name == "overflow") {
        if (lower == "hidden") {
            details_content.overflow_hidden = true;
            decision.classification = "supported";
        }
    } else if (declaration.name == "opacity") {
        char* end = nullptr;
        const auto parsed = std::strtof(lower.c_str(), &end);
        if (end != lower.c_str() && end == lower.c_str() + lower.size()
            && std::isfinite(parsed)) {
            details_content.opacity = std::clamp(parsed, 0.0F, 1.0F);
            decision.classification = "supported";
        } else {
            decision.classification = "invalid-authoring";
        }
    } else if (declaration.name == "margin-inline-start") {
        details_content.margin_inline_start = native_document::parse_length(value);
        decision.classification = "supported";
    } else if (declaration.name == "padding-inline-start") {
        details_content.padding_inline_start = native_document::parse_length(value);
        decision.classification = "supported";
    } else if (declaration.name == "box-sizing") {
        decision.classification = lower == "border-box" || lower == "content-box"
            ? "supported" : "invalid-authoring";
        decision.semantic_slice = "recognized compatibility value; collapsed static size is unchanged";
    } else if (declaration.name == "border-inline-start") {
        node_style border_style{};
        border_style.foreground_rgba = node.style.foreground_rgba;
        if (apply_border_declaration(border_style, "border-left", value)) {
            details_content.border_inline_start_width = border_style.border_left_width;
            details_content.border_inline_start_rgba = border_style.border_left_rgba;
            details_content.border_inline_start_current_color =
                border_style.border_left_current_color;
            decision.classification = "supported";
        } else {
            decision.classification = "invalid-authoring";
        }
    } else if (declaration.name == "transition") {
        apply_transition_shorthand(details_content.transitions, value);
        decision.classification = "supported";
        decision.semantic_slice = "retained details-content transition timeline";
    } else if (declaration.name == "content-visibility") {
        decision.classification = lower == "visible" || lower == "hidden"
            ? "supported" : "unsupported";
        decision.semantic_slice = "discrete details-content visibility";
    } else if (declaration.name == "border-image") {
        const auto prefix = std::string_view{"linear-gradient("};
        if (!lower.starts_with(prefix)) {
            decision.semantic_slice =
                "single vertical linear-gradient with unit slice";
            return;
        }
        auto depth = 0U;
        auto close = std::string::npos;
        for (size_t index = prefix.size() - 1U; index < value.size(); ++index) {
            if (value[index] == '(') ++depth;
            else if (value[index] == ')' && depth != 0U && --depth == 0U) {
                close = index;
                break;
            }
        }
        const auto suffix = close == std::string::npos
            ? std::string{} : trim_value(std::string_view{value}.substr(close + 1U));
        const auto image = close == std::string::npos
            ? std::string{} : trim_value(std::string_view{value}.substr(0U, close + 1U));
        const auto image_lower = ascii_lower(image);
        if (suffix != "1" || !image_lower.starts_with("linear-gradient(to bottom,")) {
            decision.semantic_slice =
                "single vertical linear-gradient with unit slice";
            return;
        }
        details_content.border_image_value = image;
        decision.classification = "supported";
        decision.semantic_slice =
            "retained details-content inline-start border gradient";
    }
}

template<typename Decision,typename Resolved>
void apply_placeholder_declaration(
    dom_node& node,
    node_style::pseudo_element_pair::placeholder_element& placeholder,
    const css_declaration& declaration,
    const std::unordered_map<std::string,std::string>& variables,
    Decision& decision,
    Resolved&& on_resolved)
{
    decision.classification = "unsupported";
    const auto contains_variable = declaration.value.find("var(") != std::string::npos;
    auto resolved_value = std::string{};
    const auto& value = contains_variable
        ? (resolved_value = resolve_value(node,declaration.value,variables))
        : declaration.value;
    if (value.empty() && contains_variable) {
        decision.classification = "invalid-authoring";
        decision.semantic_slice = "unresolved custom property at computed-value time";
        return;
    }
    on_resolved(contains_variable);
    const auto lower = ascii_lower(trim_value(value));
    if (declaration.name == "color") {
        if (lower == "currentcolor" || lower == "inherit" || lower == "unset") {
            placeholder.foreground_specified = false;
            placeholder.foreground_rgba = 0U;
            decision.classification = "supported";
        } else {
            const auto color = native_document::parse_color(value);
            if (is_explicit_color_token(value,color)) {
                placeholder.foreground_specified = true;
                placeholder.foreground_rgba = color;
                decision.classification = "supported";
            } else {
                decision.classification = "invalid-authoring";
            }
        }
    } else if (declaration.name == "opacity") {
        if (lower == "initial" || lower == "unset" || lower == "revert") {
            placeholder.opacity = 1;
            decision.classification = "supported";
            return;
        }
        if (lower == "inherit") {
            placeholder.opacity = std::clamp(node.style.opacity, 0.0F, 1.0F);
            decision.classification = "supported";
            return;
        }
        char* end = nullptr;
        const auto parsed = std::strtof(lower.c_str(), &end);
        if (end != lower.c_str() && end == lower.c_str() + lower.size()
            && std::isfinite(parsed)) {
            placeholder.opacity = std::clamp(parsed, 0.0F, 1.0F);
            decision.classification = "supported";
        } else {
            decision.classification = "invalid-authoring";
        }
    }
}
inline void apply_scrollbar_declaration(
        dom_node& node,
        int pseudo_kind,
        const css_declaration& declaration,
        const std::unordered_map<std::string,std::string>& variables)
    {
        auto& style = node.style;
        const auto contains_variable = declaration.value.find("var(") != std::string::npos;
        auto resolved_value = std::string{};
        const auto& value = contains_variable
            ? (resolved_value = resolve_value(node,declaration.value,variables))
            : declaration.value;
        if (value.empty() && contains_variable) return;
        const auto lower = ascii_lower(trim_value(value));
        auto& scrollbar = style.mutable_scrollbar();
        if (pseudo_kind == 3) {
            if (declaration.name == "display") {
                if (style.scrollbar_visibility_important && !declaration.important) return;
                style.scrollbar_hidden = lower == "none";
                style.scrollbar_visibility_important = declaration.important;
            } else if (declaration.name == "width") {
                const auto length = native_document::parse_length(value);
                if (length.unit == length_unit::pixels) {
                    scrollbar.width = std::max(0.0F, length.value);
                    scrollbar.overlay_inset = 0;
                }
            } else if (declaration.name == "height") {
                const auto length = native_document::parse_length(value);
                if (length.unit == length_unit::pixels) {
                    scrollbar.height = std::max(0.0F, length.value);
                    scrollbar.overlay_inset = 0;
                }
            }
            return;
        }
        if (pseudo_kind == 4) {
            if (declaration.name == "background" || declaration.name == "background-color") {
                scrollbar.thumb_rgba = lower == "initial" ? 0U
                    : native_document::parse_color(value);
            } else if (declaration.name == "border-radius") {
                scrollbar.thumb_radius = std::max(
                    0.0F, native_document::parse_length(value).value);
            } else if (declaration.name == "border" || declaration.name == "border-width") {
                std::istringstream stream(value);
                for (std::string token; stream >> token;) {
                    if (!token.empty() && (std::isdigit(static_cast<unsigned char>(token.front()))
                            || token.front() == '.')) {
                        scrollbar.thumb_border_width = std::max(
                            0.0F, native_document::parse_length(token).value);
                        break;
                    }
                }
            }
            return;
        }
        if (pseudo_kind == 5) {
            if (declaration.name == "background" || declaration.name == "background-color") {
                scrollbar.track_rgba = lower == "initial" ? 0U
                    : native_document::parse_color(value);
            } else if (declaration.name == "border-radius") {
                scrollbar.track_radius = std::max(
                    0.0F, native_document::parse_length(value).value);
            }
        }
    }

template<typename Decision,typename Resolved>
void apply_pseudo_declaration(dom_node& node,node_style::pseudo_element& pseudo,
    const css_declaration& declaration,const std::unordered_map<std::string,std::string>& variables,
    Decision& decision,Resolved&& on_resolved)
{
        const auto contains_variable = declaration.value.find("var(") != std::string::npos;
        auto resolved_value = std::string{};
        const auto& value = contains_variable
            ? (resolved_value = resolve_value(node,declaration.value,variables))
            : declaration.value;
        if (value.empty() && contains_variable) {
            decision.classification = "invalid-authoring";
            decision.semantic_slice = "unresolved custom property at computed-value time";
            return;
        }
        const auto& name = declaration.name;
        if (contains_variable
            && (name == "background" || name == "background-image"
                || name == "mask" || name == "mask-image"
                || name == "border-image")) {
            pseudo.variable_dependent_values[name] = declaration.value;
        }
        on_resolved(contains_variable);
        if (name == "font-family"
            && pseudo.font_family_important
            && !declaration.important) {
            decision.classification = "supported";
            return;
        }
        if (name.starts_with("animation")
            && apply_animation_property(pseudo.mutable_animations(), name, value)) {
            decision.classification = "partially-supported";
            decision.semantic_slice =
                "bounded generated-pseudo keyframes on the originating element";
            return;
        }
        const auto result = css::apply_pseudo_value(
            pseudo, node.style.foreground_rgba, node.style.border_box, name, value);
        if (name == "font-size" && result.classification == "supported") {
            pseudo.font_size = resolved_pseudo_font_size(node, value);
        }
        if (name == "font-family" && result.classification == "supported") {
            pseudo.font_family_important = declaration.important;
        }
        decision.classification = result.classification;
        decision.semantic_slice = result.semantic_slice;
}
} // namespace webscene_native::css
