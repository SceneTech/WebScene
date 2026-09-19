#pragma once
#include "webscene_css_stylesheet_owner.h"
#include "webscene_css_query.h"
#include "webscene_css_application.h"
#include "webscene_css_cascade_reset.h"
#include "webscene_css_cascade_application.h"
#include "webscene_css_cascade_finalization.h"
#include "webscene_css_rule_matching.h"
#include "webscene_css_declarations.h"

namespace webscene_native::css {
// Apply one node in parent-before-child order. The host supplies resource loading,
// diagnostics and scheduling; it must not mutate sheets from either callback.
template<class LoadSvg,class Observe>
bool apply_native_cascade(native_document& document,dom_node& node,
    const stylesheet_owner& sheets,query_host& query,
    const std::unordered_map<std::string,std::string>& variables,
    bool focused,LoadSvg&& load_svg,Observe&& observe,bool inline_attributes=false)
{
    if(node.tag=="input" || node.tag=="textarea") forms::ensure_text_value(node);
    const auto previous=node.style;
    if(node.dialog_state)node.dialog_state->backdrop_rgba=0;
    if(node.kind!=dom_node_kind::element) {
        node.style.display=node.kind==dom_node_kind::text?display_mode::inline_flow:display_mode::none;
    } else {
        std::vector<css_declaration> inline_values;
        if(inline_attributes) {
            node.style.inline_property_mask=0;
            node.clear_authored_style();
            const auto attribute=node.attributes.find("style");
            if(attribute!=node.attributes.end()) inline_values=parse_declarations(attribute->second);
            // Seed inline custom properties before matching dependent values.
            // Preserve priority when the attribute declares the same name twice.
            for(const auto& declaration:inline_values) {
                auto& authored=node.mutable_authored_style();
                if(!declaration.important && authored.important_declarations.contains(declaration.name)) continue;
                authored.declarations[declaration.name]=declaration.value;
                authored.record_declaration_order(declaration.name);
                if(declaration.important) authored.important_declarations.insert(declaration.name);
            }
        }
        reset_cascaded_style(
            node, variables, &sheets.state().registered_custom_properties);
        auto indices=sheets.candidates(node,focused);
        auto matched=match_candidates(document,node,sheets.state().rules,indices,
            [&](const auto& subject,const auto& rule,const auto&) { return query.matches_prepared(subject,rule.payload->compiled_pseudo_origin); },
            [&](const auto& subject,const auto& rule) { return query.matches_prepared(subject,rule.compiled_selector()); });
        seed_relevant_registered_custom_properties(
            node, sheets.state().registered_custom_properties,
            [&](const std::string& name, const std::string& variable_token) {
                const auto references = [&](const css_rule* rule) {
                    return std::any_of(
                        rule->declarations().begin(),
                        rule->declarations().end(),
                        [&](const css_declaration& declaration) {
                            return declaration.name == name
                                || declaration.value.find(variable_token)
                                    != std::string::npos;
                        });
                };
                return std::any_of(
                        matched.ordinary.begin(), matched.ordinary.end(), references)
                    || std::any_of(
                        matched.pseudo.begin(), matched.pseudo.end(),
                        [&](const auto& entry) { return references(entry.second); });
            });
        const cascaded_rule_order cascade_order(matched.ordinary);
        apply_matched_declarations(node,cascade_order,[&](const css_declaration& declaration,bool inline_origin) {
            property_result result;
            if (declaration.name.starts_with("--")) {
                apply_custom_property(
                    node, declaration,
                    &sheets.state().registered_custom_properties);
                result.classification = "supported";
            } else {
                apply_declaration(document,node,declaration,variables,inline_origin,result,load_svg,[](bool) {});
            }
            observe(declaration,result);
        });
        if(inline_attributes) {
            for(bool important:{false,true}) for(const auto& declaration:inline_values) {
                if(declaration.important!=important || declaration.name.starts_with("--")) continue;
                property_result result;
                apply_declaration(document,node,declaration,variables,true,result,load_svg,[](bool) {});
                observe(declaration,result);
            }
        }
        recompute_cascaded_line_height(node,cascade_order,variables);
        recompute_inline_font_relative_metrics(node);
        configure_style_transitions(node.style);
        document.update_discrete_display_transition(node,previous.display);
        std::optional<node_style> starting_style;
        const auto rendered_after_change=node.style.display!=display_mode::none;
        const auto entering_rendered_state=rendered_after_change
            && (!node.css_cascade_initialized || !node.css_was_rendered);
        if(entering_rendered_state) {
            std::vector<const css_rule*> starting_rules;
            for(const auto index:indices) {
                if(index>=sheets.state().rules.size()) continue;
                const auto& rule=sheets.state().rules[index];
                if(!sheets.starting_rule_media_matches(rule)
                    || rule.payload->pseudo_kind!=0U
                    || !query.matches_prepared(node,rule.compiled_selector())) continue;
                starting_rules.push_back(&rule);
            }
            if(!starting_rules.empty()) {
                auto after_change_style=node.style;
                reset_cascaded_style(
                    node,variables,&sheets.state().registered_custom_properties);
                auto initial_rules=matched.ordinary;
                initial_rules.insert(
                    initial_rules.end(),starting_rules.begin(),starting_rules.end());
                std::sort(
                    initial_rules.begin(),initial_rules.end(),
                    [](const css_rule* left,const css_rule* right) {
                        return cascade_rule_precedes(left,right,false);
                    });
                const cascaded_rule_order initial_order(initial_rules);
                apply_matched_declarations(
                    node,initial_order,[&](const css_declaration& declaration,bool inline_origin) {
                        property_result result;
                        if(declaration.name.starts_with("--")) {
                            apply_custom_property(
                                node,declaration,
                                &sheets.state().registered_custom_properties);
                        } else {
                            apply_declaration(
                                document,node,declaration,variables,inline_origin,
                                result,load_svg,[](bool) {},true);
                        }
                    });
                configure_style_transitions(node.style);
                recompute_cascaded_line_height(node,initial_order,variables);
                recompute_inline_font_relative_metrics(node);
                starting_style=node.style;
                node.style=std::move(after_change_style);
            }
        }
        node.css_cascade_initialized=true;
        node.css_was_rendered=rendered_after_change;
        bool backdrop_important=false;
        for_each_cascaded_pseudo_declaration(
            matched.pseudo,
            [&](int kind, const css_declaration& declaration) {
                if(kind==7) {
                    const auto result=apply_backdrop_declaration(node,declaration,variables,backdrop_important);
                    observe(declaration,result);
                }
                else if(kind==8) {
                    property_result result;
                    apply_placeholder_declaration(
                        node,node.style.mutable_placeholder_pseudo(),declaration,
                        variables,result,[](bool) {});
                    observe(declaration,result);
                }
                else if(kind==9) {
                    property_result result;
                    apply_details_content_declaration(
                        node,node.style.mutable_details_content_pseudo(),declaration,
                        variables,result,[](bool) {});
                    observe(declaration,result);
                }
                else if(kind>=3) apply_scrollbar_declaration(node,kind,declaration,variables);
                else {
                    auto& pseudo=kind==1?node.style.mutable_before_pseudo():node.style.mutable_after_pseudo();
                    property_result result;
                    apply_pseudo_declaration(node,pseudo,declaration,variables,result,[](bool) {});
                    observe(declaration,result);
                }
            });
        if(node.style.before_pseudo().generated && previous.before_pseudo().generated)
            node.style.mutable_before_pseudo().layout=previous.before_pseudo().layout;
        if(node.style.after_pseudo().generated && previous.after_pseudo().generated)
            node.style.mutable_after_pseudo().layout=previous.after_pseudo().layout;
        document.update_details_content_transition(
            node, previous.details_content_pseudo());
        configure_keyframes(
            node.style,
            sheets.state().opacity_keyframes,
            sheets.state().registered_custom_properties);
        if(node.style.has_pseudo_elements()) {
            auto& before=node.style.mutable_before_pseudo();
            auto& after=node.style.mutable_after_pseudo();
            if(before.has_animation_data())
                configure_keyframes(
                    before.mutable_animations(),
                    sheets.state().opacity_keyframes,
                    sheets.state().registered_custom_properties);
            if(after.has_animation_data())
                configure_keyframes(
                    after.mutable_animations(),
                    sheets.state().opacity_keyframes,
                    sheets.state().registered_custom_properties);
        }
        if(starting_style.has_value())
            document.prime_starting_style(node,*starting_style);
        document.update_style_animations(node);
    }
    const bool layout_changed=!computed_layout_style_equal(previous,node.style);
    if(layout_changed) document.mark_dirty();
    else document.mark_scene_changed();
    return layout_changed;
}
// Explicit full-document refresh for stylesheet, viewport and DOM changes.
// This correctness path is not a per-frame operation. Hosts may later schedule
// smaller invalidated subtrees; callbacks must not mutate the tree during traversal.
template<class LoadSvg,class Observe>
bool apply_native_document_cascade(native_document& document,
    const stylesheet_owner& sheets,query_host& query,LoadSvg&& load_svg,Observe&& observe,bool inline_attributes=false)
{
    // A retained native_style_session can outlive ID and tree mutations. Resolve
    // the one fragment target once per explicit cascade, never once per node.
    query.refresh_target();
    std::unordered_map<std::string,std::string> variables;
    std::unordered_set<std::string> important;
    rebuild_root_variables(sheets.state().rules,variables,important);
    const auto* focus=query.selector_interaction_state().focused;
    std::vector<dom_node*> pending{&document.body()};
    bool layout_changed=false;
    while(!pending.empty()) {
        auto* node=pending.back();
        pending.pop_back();
        layout_changed|=apply_native_cascade(document,*node,sheets,query,variables,
            node==focus,load_svg,observe,inline_attributes);
        for(auto child=node->children.rbegin();child!=node->children.rend();++child)
            if(*child) pending.push_back(*child);
    }
    return layout_changed;
}
} // namespace webscene_native::css
