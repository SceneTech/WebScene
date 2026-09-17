#pragma once
#include "webscene_css_selectors.h"

namespace webscene_native::css {
// Negative-only filter for one immutable selector-matching pass. Nodes and
// compiled selectors must outlive the pass; discard it before DOM mutations.
// Hash collisions may retain impossible selectors, but a positive answer never
// replaces matching. Sibling and functional-pseudo features are not requirements.
struct selector_ancestor_filter final {
    using mask=css_ancestor_feature_mask;

    size_t capacity{16384U};
    std::unordered_map<const dom_node*,mask> inclusive_ancestors;

    const mask& required(const compiled_css_selector& selector,size_t component) const {
        static const mask empty;
        return component<selector.ancestor_requirements.size()
            ? selector.ancestor_requirements[component] : empty;
    }

    static mask features(const dom_node& node) {
        mask result;
        if(!node.tag.empty()) result.add('t',node.tag);
        // The matcher also represents a synthetic document element as body.
        if(node.tag=="body") result.add('t',"html");
        if(!node.id_attribute.empty()) result.add('#',node.id_attribute);
        const std::string_view classes=node.class_name;
        constexpr std::string_view whitespace=" \t\n\f\r";
        size_t offset=0;
        while((offset=classes.find_first_not_of(whitespace,offset))!=std::string_view::npos) {
            const auto end=classes.find_first_of(whitespace,offset);
            result.add('.',classes.substr(offset,end==std::string_view::npos?end:end-offset));
            if(end==std::string_view::npos) break;
            offset=end;
        }
        return result;
    }

    mask ancestors(const native_document& document,const dom_node& subject) {
        std::vector<const dom_node*> pending;
        mask result;
        for(auto* node=document.dom_parent(subject);node;node=document.dom_parent(*node)) {
            if(const auto found=inclusive_ancestors.find(node);found!=inclusive_ancestors.end()) {
                result=found->second;break;
            }
            if(inclusive_ancestors.size()+pending.size()>=capacity) return mask::saturated();
            pending.push_back(node);
        }
        for(auto node=pending.rbegin();node!=pending.rend();++node) {
            result.merge(features(**node));
            inclusive_ancestors.emplace(*node,result);
        }
        return result;
    }

    bool may_match(const native_document& document,const dom_node& node,
        const compiled_css_selector& selector,size_t component) {
        const auto& needed=required(selector,component);
        return needed.empty() || ancestors(document,node).contains(needed);
    }
};
} // namespace webscene_native::css
