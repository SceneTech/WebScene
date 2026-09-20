#pragma once
#include "webscene_css_property_mask.h"
#include "webscene_css_transitions.h"
#include "webscene_native_style_defaults.h"

namespace webscene_native::css {
// Existing modeled behavior for a non-important author all:unset declaration.
// Callers handle unsupported reset keywords and cascade ordering before entry.
inline void apply_all_unset(
    dom_node& node,
    bool important = false,
    bool inline_origin = false)
{
    const auto is_inline=[&](uint64_t property) {
        if (inline_origin) {
            // An inline `all` replaces earlier declarations in its own block.
            // Only a higher author-important rule survives inline-normal all.
            return !important
                && (node.style.important_property_mask & property) != 0U;
        }
        if (!important) {
            return ((node.style.inline_property_mask
                | node.style.important_property_mask) & property) != 0U;
        }
        // Author-important all still yields to inline-important declarations.
        return std::any_of(
            node.authored_style().important_declarations.begin(),
            node.authored_style().important_declarations.end(),
            [&](const std::string& name) {
                return (property_mask(name) & property) != 0U;
            });
    };
    // `all: unset` is a common component-control reset. Start from the
    // modeled initial/inherited sentinels, then restore declarations
    // from the higher-priority inline origin. Custom properties are
    // deliberately excluded from `all` by CSS Cascade and therefore
    // survive the reset. Generated pseudo-element state belongs to its
    // own selector subject and must not be cleared with the principal
    // element's declarations.
    auto previous = node.style;
    auto reset = node_style{};
    // `unset` selects the CSS initial value for non-inherited properties. The
    // element-specific block/table defaults belong to the lower UA origin and
    // must not replace author `all: unset` (whose initial display is inline).
    reset.display = display_mode::inline_flow;
    reset.inline_property_mask = inline_origin
        ? std::numeric_limits<uint64_t>::max()
        : previous.inline_property_mask;
    reset.important_property_mask = important
        ? std::numeric_limits<uint64_t>::max()
        : previous.important_property_mask;
    reset.important_margin_sides = important
        ? 15U
        : previous.important_margin_sides;
    reset.move_custom_properties_from(previous);
    reset.move_pseudo_elements_from(previous);

    const auto has_inline = [&](std::initializer_list<std::string_view> names) {
        return std::any_of(names.begin(), names.end(), [&](std::string_view candidate) {
            if (!node.authored_style().declarations.contains(
                    std::string(candidate))) {
                return false;
            }
            if (inline_origin) return false;
            return !important
                || node.authored_style().important_declarations.contains(
                    std::string(candidate));
        });
    };
    if (is_inline(inline_width)) reset.width = previous.width;
    if (is_inline(inline_height)) reset.height = previous.height;
    if (is_inline(inline_min_width)) reset.min_width = previous.min_width;
    if (is_inline(inline_min_height)) reset.min_height = previous.min_height;
    if (is_inline(inline_max_width)) reset.max_width = previous.max_width;
    if (is_inline(inline_max_height)) reset.max_height = previous.max_height;
    if (is_inline(inline_left)) reset.left = previous.left;
    if (is_inline(inline_top)) reset.top = previous.top;
    if (is_inline(inline_right)) reset.right = previous.right;
    if (is_inline(inline_bottom)) reset.bottom = previous.bottom;
    if (is_inline(inline_display)) reset.display = previous.display;
    if (is_inline(inline_position)) reset.position = previous.position;
    if (is_inline(inline_contain)) {
        reset.contain_stacking_context = previous.contain_stacking_context;
        reset.mutable_textual().contain_value =
            previous.textual().contain_value;
    }
    if (is_inline(inline_containment_features)) {
        auto& textual = reset.mutable_textual();
        textual.container_type = previous.textual().container_type;
        textual.container_name = previous.textual().container_name;
        textual.content_visibility = previous.textual().content_visibility;
        textual.contain_intrinsic_size = previous.textual().contain_intrinsic_size;
        reset.content_visibility_hidden = previous.content_visibility_hidden;
    }
    if (is_inline(inline_accessibility_colors)) {
        reset.mutable_textual().color_scheme = previous.textual().color_scheme;
        reset.mutable_textual().accent_color = previous.textual().accent_color;
    }
    if (is_inline(inline_padding)) {
        reset.padding_left = previous.padding_left;
        reset.padding_top = previous.padding_top;
        reset.padding_right = previous.padding_right;
        reset.padding_bottom = previous.padding_bottom;
    }
    if (is_inline(inline_margin)) {
        reset.margin_left = previous.margin_left;
        reset.margin_top = previous.margin_top;
        reset.margin_right = previous.margin_right;
        reset.margin_bottom = previous.margin_bottom;
        reset.margin_left_auto = previous.margin_left_auto;
        reset.margin_top_auto = previous.margin_top_auto;
        reset.margin_right_auto = previous.margin_right_auto;
        reset.margin_bottom_auto = previous.margin_bottom_auto;
    }
    if (is_inline(inline_gap)) {
        reset.row_gap = previous.row_gap;
        reset.column_gap = previous.column_gap;
    }
    if (is_inline(inline_table_border_model)) {
        reset.mutable_table() = previous.table();
    }
    if (is_inline(inline_flex_direction)) {
        reset.direction = previous.direction;
        reset.flex_reverse = previous.flex_reverse;
    }
    if (is_inline(inline_flex_grow)) reset.flex_grow = previous.flex_grow;
    if (is_inline(inline_flex_shrink)) reset.flex_shrink = previous.flex_shrink;
    if (is_inline(inline_flex_basis)) reset.flex_basis = previous.flex_basis;
    if (is_inline(inline_flex_wrap)) reset.flex_wrap = previous.flex_wrap;
    if (is_inline(inline_align_items)) reset.align_items = previous.align_items;
    if (is_inline(inline_align_self)) {
        reset.align_self = previous.align_self;
        reset.align_self_specified = previous.align_self_specified;
    }
    if (is_inline(inline_align_content)) {
        reset.align_content_stretches = previous.align_content_stretches;
    }
    if (is_inline(inline_justify_content)) {
        reset.justify_content = previous.justify_content;
    }
    if (is_inline(inline_box_sizing)) reset.border_box = previous.border_box;
    if (is_inline(inline_border_radius)) {
        reset.border_top_left_radius = previous.border_top_left_radius;
        reset.border_top_right_radius = previous.border_top_right_radius;
        reset.border_bottom_right_radius = previous.border_bottom_right_radius;
        reset.border_bottom_left_radius = previous.border_bottom_left_radius;
        reset.set_vertical_corner_radii(
            previous.border_top_left_radius_y(),
            previous.border_top_right_radius_y(),
            previous.border_bottom_right_radius_y(),
            previous.border_bottom_left_radius_y());
    }
    if (is_inline(inline_box_shadow)) {
        reset.box_shadow_offset_x = previous.box_shadow_offset_x;
        reset.box_shadow_offset_y = previous.box_shadow_offset_y;
        reset.box_shadow_blur_radius = previous.box_shadow_blur_radius;
        reset.box_shadow_spread_radius = previous.box_shadow_spread_radius;
        reset.box_shadow_rgba = previous.box_shadow_rgba;
        reset.box_shadow_present = previous.box_shadow_present;
        reset.box_shadow_inset = previous.box_shadow_inset;
        reset.box_shadow_current_color = previous.box_shadow_current_color;
    }
    if (is_inline(inline_transform)) {
        reset.transform_translate_x = previous.transform_translate_x;
        reset.transform_translate_y = previous.transform_translate_y;
        reset.transform_scale_x = previous.transform_scale_x;
        reset.transform_scale_y = previous.transform_scale_y;
        reset.transform_rotate_degrees = previous.transform_rotate_degrees;
        reset.transform_specified = previous.transform_specified;
        reset.transform_stacking_context =
            previous.transform_stacking_context;
    }
    if (is_inline(inline_transform_origin)) {
        reset.transform_origin_x = previous.transform_origin_x;
        reset.transform_origin_y = previous.transform_origin_y;
        reset.transform_origin_specified = previous.transform_origin_specified;
    }
    if (is_inline(inline_background)) {
        reset.background_rgba = previous.background_rgba;
        reset.background_current_color = previous.background_current_color;
    }
    if (is_inline(inline_overflow)) {
        reset.overflow_x = previous.overflow_x;
        reset.overflow_y = previous.overflow_y;
        reset.clip = previous.clip;
        reset.scroll_x_enabled = previous.scroll_x_enabled;
        reset.scroll_y_enabled = previous.scroll_y_enabled;
    }
    if (is_inline(inline_visibility)) {
        reset.visibility_hidden = previous.visibility_hidden;
        reset.visibility_specified = previous.visibility_specified;
    }
    if (is_inline(inline_pointer_events)) {
        reset.pointer_events_none = previous.pointer_events_none;
        reset.pointer_events_specified = previous.pointer_events_specified;
    }
    if (is_inline(inline_opacity)) reset.opacity = previous.opacity;
    if (is_inline(inline_color)) reset.foreground_rgba = previous.foreground_rgba;
    if (is_inline(inline_font_size)) reset.font_size = previous.font_size;
    if (is_inline(inline_font_family)) {
        reset.mutable_textual().font_family =
            previous.textual().font_family;
    }
    if (is_inline(inline_font_smoothing)) {
        reset.mutable_textual().font_smoothing =
            previous.textual().font_smoothing;
    }
    if (is_inline(inline_font_weight)) reset.font_weight = previous.font_weight;
    if (is_inline(inline_line_height)) reset.line_height = previous.line_height;
    if (is_inline(inline_letter_spacing)) {
        reset.letter_spacing = previous.letter_spacing;
        reset.letter_spacing_specified = previous.letter_spacing_specified;
    }
    if (is_inline(inline_word_spacing)) {
        reset.word_spacing = previous.word_spacing;
        reset.word_spacing_specified = previous.word_spacing_specified;
    }
    if (is_inline(inline_text_align)) {
        reset.mutable_textual().text_align =
            previous.textual().text_align;
    }
    if (is_inline(inline_white_space)) {
        reset.mutable_textual().white_space =
            previous.textual().white_space;
    }
    if (has_inline({"word-break"})) {
        reset.mutable_textual().word_break =
            previous.textual().word_break;
    }
    if (has_inline({"overflow-wrap", "word-wrap"})) {
        reset.mutable_textual().overflow_wrap =
            previous.textual().overflow_wrap;
    }
    if (has_inline({"object-fit"})) {
        reset.mutable_textual().object_fit =
            previous.textual().object_fit;
    }
    if (has_inline({"object-position"})) {
        reset.mutable_textual().object_position =
            previous.textual().object_position;
    }
    if (has_inline({"user-select", "-webkit-user-select", "-ms-user-select"})) {
        reset.mutable_textual().user_select = previous.textual().user_select;
    }
    if (has_inline({"overscroll-behavior", "overscroll-behavior-x"})) {
        reset.mutable_textual().overscroll_x = previous.textual().overscroll_x;
    }
    if (has_inline({"overscroll-behavior", "overscroll-behavior-y"})) {
        reset.mutable_textual().overscroll_y = previous.textual().overscroll_y;
    }
    if (has_inline({"isolation"})) {
        reset.isolation_stacking_context = previous.isolation_stacking_context;
    }
    if (has_inline({"will-change"})) {
        reset.mutable_textual().will_change = previous.textual().will_change;
        reset.will_change_stacking_context = previous.will_change_stacking_context;
    }

    // These modeled properties do not yet have dedicated inline-mask
    // bits, so preserve their applied values by authored declaration.
    if (is_inline(inline_background_image)
        || has_inline({"background-image", "background-repeat",
            "background-position", "background-size"})) {
        reset.mutable_background_image() = previous.background_image();
    }
    if (has_inline({"border", "border-top", "border-right", "border-bottom",
            "border-left", "border-width", "border-color",
            "border-top-width", "border-right-width", "border-bottom-width",
            "border-left-width", "border-top-color", "border-right-color",
            "border-bottom-color", "border-left-color"})) {
        reset.border_left_width = previous.border_left_width;
        reset.border_top_width = previous.border_top_width;
        reset.border_right_width = previous.border_right_width;
        reset.border_bottom_width = previous.border_bottom_width;
        reset.border_left_rgba = previous.border_left_rgba;
        reset.border_top_rgba = previous.border_top_rgba;
        reset.border_right_rgba = previous.border_right_rgba;
        reset.border_bottom_rgba = previous.border_bottom_rgba;
        reset.border_left_current_color = previous.border_left_current_color;
        reset.border_top_current_color = previous.border_top_current_color;
        reset.border_right_current_color = previous.border_right_current_color;
        reset.border_bottom_current_color = previous.border_bottom_current_color;
    }
    if (has_inline({"outline", "outline-width", "outline-color"})) {
        reset.outline_width = previous.outline_width;
        reset.outline_rgba = previous.outline_rgba;
    }
    if (has_inline({"transition", "transition-property", "transition-duration",
            "transition-delay", "transition-timing-function", "transition-behavior"})) {
        auto& reset_animations = reset.mutable_animations();
        reset_animations.transition_property_value = previous.animations().transition_property_value;
        reset_animations.transition_duration_value = previous.animations().transition_duration_value;
        reset_animations.transition_delay_value = previous.animations().transition_delay_value;
        reset_animations.transition_timing_function_value =
            previous.animations().transition_timing_function_value;
        reset_animations.transition_behavior_value =
            previous.animations().transition_behavior_value;
        configure_style_transitions(reset);
    }
    if (has_inline({"z-index"})) {
        reset.z_index = previous.z_index;
        reset.z_index_auto = previous.z_index_auto;
    }
    if (has_inline({"flex-basis", "flex"})) reset.flex_basis = previous.flex_basis;
    if (has_inline({"grid-template-columns", "grid-template-rows", "grid-template-areas",
            "grid-auto-columns", "grid-auto-flow",
            "grid-area", "grid-row", "grid-row-start", "grid-row-end",
            "grid-column", "grid-column-start", "grid-column-end"})) {
        reset.mutable_grid() = previous.grid();
    }
    if (has_inline({"table-layout"})) reset.table_layout_fixed = previous.table_layout_fixed;
    if (has_inline({"text-transform"})) {
        reset.mutable_textual().text_transform =
            previous.textual().text_transform;
    }
    if (has_inline({"cursor"})) {
        reset.mutable_textual().cursor =
            previous.textual().cursor;
    }
    if (has_inline({"list-style", "list-style-position", "list-style-type"})) {
        reset.mutable_textual().list_style_position =
            previous.textual().list_style_position;
        reset.mutable_textual().list_style_type =
            previous.textual().list_style_type;
    }

    node.style = std::move(reset);
}
} // namespace webscene_native::css
