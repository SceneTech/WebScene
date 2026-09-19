#pragma once
#include "webscene_css_box_application.h"
#include "webscene_css_layout_values.h"
#include "webscene_css_decoration_values.h"
#include "webscene_css_paint_values.h"
#include "webscene_css_visibility_values.h"
#include "webscene_css_text_values.h"
#include "webscene_css_reset.h"
#include "webscene_css_variables.h"
#include "webscene_css_scrollbar_values.h"

namespace webscene_native::css {
// Apply an already-resolved declaration. The caller owns variable resolution,
// selector/cascade ordering, resource loading, and document invalidation.
template<typename Decision,typename LoadSvg>
void apply_resolved_declaration(native_document& document,dom_node& node,
    const css_declaration& declaration,const std::string& value,
    bool inline_origin,Decision& decision,LoadSvg&& load_svg,
    bool defer_transition_configuration = false)
{
    const auto& name=declaration.name;
    if (name == "mask") {
        const auto parsed = parse_mask_shorthand(value);
        if (!parsed.has_value()) {
            decision.classification = "unsupported";
            decision.semantic_slice = "single mask layer without geometry-box syntax";
            return;
        }
        const std::array<std::pair<std::string_view, const std::string*>, 6> components{{
            {"mask-image", &parsed->image},
            {"mask-position", &parsed->position},
            {"mask-size", &parsed->size},
            {"mask-repeat", &parsed->repeat},
            {"mask-composite", &parsed->composite},
            {"mask-mode", &parsed->mode},
        }};
        for (const auto& [component_name, component_value] : components) {
            css_declaration component{
                std::string(component_name), *component_value, declaration.important};
            component.property = property_id(component.name);
            component.specified = compile_specified_value(
                component.property, component.value);
            apply_resolved_declaration(
                document,
                node,
                component,
                component.value,
                inline_origin,
                decision,
                load_svg,
                defer_transition_configuration);
        }
        return;
    }
    const auto may_require_application_expansion =
        name.starts_with("border-") || name.starts_with("inset-");
    const auto* effective_metadata = may_require_application_expansion
        ? find_effective_property_metadata(name) : nullptr;
    if (effective_metadata != nullptr && effective_metadata->apply_expansion) {
        std::array<std::pair<std::string_view, std::string_view>, 4> components{};
        size_t component_count = 0U;
        const auto expanded = for_each_effective_property_component(
            name,
            value,
            [&](std::string_view component_name, std::string_view component_value) {
                components[component_count++] = {component_name, component_value};
            });
        if (expanded) {
            for (size_t index = 0; index < component_count; ++index) {
                const css_declaration component{
                    std::string(components[index].first),
                    std::string(components[index].second),
                    declaration.important};
                apply_resolved_declaration(
                    document,
                    node,
                    component,
                    component.value,
                    inline_origin,
                    decision,
                    load_svg,
                    defer_transition_configuration);
            }
            return;
        }
    }
    if(name=="stroke-width") {
        apply_text_value(node,name,value,decision,[&](uint64_t mask) {
            return !declaration.important && ((node.style.inline_property_mask|node.style.important_property_mask)&mask)!=0;
        });
        if(declaration.important && decision.classification=="supported")
            node.style.important_property_mask|=inline_svg_stroke_width;
        return;
    }
    if(name=="scrollbar-width" || name=="scrollbar-color") {
        apply_scrollbar_value(node,declaration,value,decision);return;
    }
        if (margin_sides(name)!=0U) {
            css::apply_margin(node,declaration,value,inline_origin);
            return;
        }
        const auto property_mask = css::property_mask(name);
        if (declaration.important && property_mask != 0U) {
            node.style.important_property_mask |= property_mask;
        }
        const auto is_inline = [&](uint64_t property) {
            return !declaration.important
                && ((node.style.inline_property_mask | node.style.important_property_mask)
                    & property) != 0U;
        };
        if (name == "aspect-ratio") {
            const auto inline_wins = !inline_origin && node.style.aspect_ratio_inline
                && (!declaration.important || node.style.aspect_ratio_inline_important);
            if (inline_wins
                || (!inline_origin && !declaration.important
                    && node.style.aspect_ratio_important)) return;
            auto ratio_width = 0.0F;
            auto ratio_height = 0.0F;
            if (value == "inherit" && node.parent != nullptr) {
                ratio_width = node.parent->style.aspect_ratio_width;
                ratio_height = node.parent->style.aspect_ratio_height;
            } else if (!parse_preferred_aspect_ratio(
                    value,
                    ratio_width,
                    ratio_height)) {
                decision.classification = "invalid-authoring";
                return;
            }
            node.style.aspect_ratio_width = ratio_width;
            node.style.aspect_ratio_height = ratio_height;
            node.style.aspect_ratio_important = declaration.important;
            if (inline_origin) {
                node.style.aspect_ratio_inline = true;
                node.style.aspect_ratio_inline_important = declaration.important;
            }
            return;
        }
        // Stylesheet preparation already parses fixed lengths into immutable
        // specified-value IR. Reuse those values during large recascades
        // instead of parsing the same authored token for every matched node.
        if (declaration.specified.kind == specified_css_kind::length
            && declaration.specified.keyword.empty()) {
            switch (declaration.property) {
            case css_property_id::width:
                if (!is_inline(inline_width)) node.style.width = declaration.specified.length;
                return;
            case css_property_id::height:
                if (!is_inline(inline_height)) node.style.height = declaration.specified.length;
                return;
            case css_property_id::padding_left:
                if (!is_inline(inline_padding)) {
                    node.style.padding_left = declaration.specified.length;
                }
                return;
            case css_property_id::padding_right:
                if (!is_inline(inline_padding)) {
                    node.style.padding_right = declaration.specified.length;
                }
                return;
            case css_property_id::padding_top:
                if (!is_inline(inline_padding)) {
                    node.style.padding_top = declaration.specified.length;
                }
                return;
            case css_property_id::padding_bottom:
                if (!is_inline(inline_padding)) {
                    node.style.padding_bottom = declaration.specified.length;
                }
                return;
            default:
                break;
            }
        }
        if (declaration.specified.kind == specified_css_kind::color) {
            if (declaration.property == css_property_id::color) {
                if (!is_inline(inline_color)) {
                    node.style.foreground_rgba = declaration.specified.color.rgba;
                }
                return;
            }
            if (declaration.property == css_property_id::background_color) {
                if (!is_inline(inline_background)) {
                    node.style.background_rgba = declaration.specified.color.rgba;
                    node.style.background_current_color =
                        declaration.specified.color.current_color;
                }
                return;
            }
        }
        if (declaration.property == css_property_id::box_sizing
            && declaration.specified.kind == specified_css_kind::keyword) {
            if (!is_inline(inline_box_sizing)) {
                node.style.border_box = declaration.specified.keyword == "border-box";
            }
            return;
        }
        if (declaration.property == css_property_id::transition
            && value.find(',') == std::string::npos
            && apply_compiled_single_transition_shorthand(
                node.style,
                declaration.specified,
                !defer_transition_configuration)) return;
        if (name == "all") {
            if (!cascade_keyword_is(value, "unset")) {
                decision.classification = "unsupported";
                decision.semantic_slice =
                    "unset across modeled properties, excluding custom properties";
                return;
            }

            css::apply_all_unset(node, declaration.important, inline_origin);
            decision.classification = "partially-supported";
            decision.semantic_slice =
                "unset across modeled properties, excluding custom properties";
            return;
        }
        if (name == "color" && !is_inline(inline_color)
            && (value == "inherit" || value == "unset" || value == "initial")) {
            node.style.foreground_rgba = value == "initial" ? 0x000000FFU : 0U;
            return;
        }
        if ((name == "background" || name == "background-color")
            && !is_inline(inline_background)
            && (value == "unset" || value == "initial" || value == "inherit")) {
            const auto explicit_document_element = node.tag == "html"
                && node.parent == &document.body();
            node.style.background_rgba = value == "inherit" && node.parent != nullptr
                && !explicit_document_element
                ? node.parent->style.background_rgba
                : 0U;
            node.style.background_current_color = false;
            return;
        }
        if(css::apply_decoration_value(
            node,
            name,
            value,
            decision,
            is_inline,
            defer_transition_configuration)) return;
        if (css::apply_box_metrics(node,name,value,is_inline)) {
        } else if (css::apply_structure_value(document,node,name,value,decision,is_inline)) {
        } else if (css::apply_grid_value(node,name,value,decision,is_inline)) {
        } else if (css::apply_flex_value(node,name,value,is_inline)) {
        } else if (css::apply_paint_value(node,name,value,decision,is_inline,
            load_svg)) {
        } else if (css::apply_visibility_value(document,node,name,value,is_inline)) {
        } else if (css::apply_text_value(node,name,value,decision,is_inline)) {
        } else if (name == "border-style") {
            decision.classification = "partially-supported";
            decision.semantic_slice = "none";
        } else if (name == "vertical-align") {
            node.style.mutable_textual().vertical_align = value;
            decision.classification = "partially-supported";
            decision.semantic_slice = "middle on table rows and inline line boxes";
        } else if (property_mask == 0U) {
            decision.classification = "unsupported";
        }
}
// Complete property application entry point; rule ordering and invalidation
// remain with the document cascade owner. Custom values stay live on the node.
template<typename Decision,typename LoadSvg,typename Resolved>
void apply_declaration(native_document& document,dom_node& node,
    const css_declaration& authored,const std::unordered_map<std::string,std::string>& variables,
    bool inline_origin,Decision& decision,LoadSvg&& load_svg,Resolved&& on_resolved,
    bool defer_transition_configuration = false)
{
    std::optional<css_declaration> normalized;
    if(authored.name=="-moz-transform" || authored.name=="-webkit-transform") {
        normalized=authored;normalized->name="transform";
    } else if(authored.name=="grid-gap" || authored.name=="grid-row-gap" || authored.name=="grid-column-gap") {
        normalized=authored;normalized->name=canonical_property_name(authored.name);
    }
    const auto& declaration=normalized?*normalized:authored;
    if(declaration.name.starts_with("--")) {
        apply_custom_property(node,declaration);
        return;
    }
    const auto contains_variable=declaration.value.find("var(")!=std::string::npos;
    auto resolved=contains_variable?resolve_value(node,declaration.value,variables):std::string{};
    const auto& value=contains_variable?resolved:declaration.value;
    if(contains_variable && value.empty()) {
        decision.classification="invalid-authoring";
        decision.semantic_slice="unresolved custom property at computed-value time";
        return;
    }
    on_resolved(contains_variable);
    apply_resolved_declaration(
        document,
        node,
        declaration,
        value,
        inline_origin,
        decision,
        load_svg,
        defer_transition_configuration);
}
} // namespace webscene_native::css
