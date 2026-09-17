#pragma once
#include "webscene_css_selectors.h"
#include <array>

namespace webscene_native::css {
// Negative-only filter for one immutable selector-matching pass. Nodes and
// compiled selectors must outlive the pass; discard it before DOM mutations.
// Hash collisions may retain impossible selectors, but a positive answer never
// replaces matching. Sibling and functional-pseudo features are not requirements.
struct selector_ancestor_filter final {
    struct mask final {
        std::array<uint64_t,4> words{};
        void add(char kind,std::string_view text) {
            uint64_t hash=14695981039346656037ULL;
            hash=(hash^static_cast<unsigned char>(kind))*1099511628211ULL;
            for(unsigned char c:text) {
                // Folding is conservative for XML and case-sensitive identities.
                if(c>='A' && c<='Z') c+=static_cast<unsigned char>('a'-'A');
                hash=(hash^c)*1099511628211ULL;
            }
            const auto bit=static_cast<size_t>(hash&255U);
            words[bit/64U]|=uint64_t{1}<<(bit%64U);
        }
        void merge(const mask& other) {
            for(size_t i=0;i<words.size();++i) words[i]|=other.words[i];
        }
        bool empty() const { return (words[0]|words[1]|words[2]|words[3])==0; }
        bool contains(const mask& required) const {
            for(size_t i=0;i<words.size();++i)
                if((words[i]&required.words[i])!=required.words[i]) return false;
            return true;
        }
        static mask saturated() { return {{~uint64_t{0},~uint64_t{0},~uint64_t{0},~uint64_t{0}}}; }
    };

    size_t capacity{16384U};
    size_t requirement_entries{0U};
    std::unordered_map<const dom_node*,mask> inclusive_ancestors;
    std::unordered_map<const compiled_css_selector*,std::vector<mask>> requirements;

    mask required(const compiled_css_selector& selector,size_t component) {
        if(const auto found=requirements.find(&selector);found!=requirements.end())
            return found->second[component];
        const auto count=selector.compiled_compounds.size();
        // On exhaustion, discard this optimization rather than omit features.
        if(count>capacity || requirement_entries>capacity-count) return {};
        std::vector<mask> prepared(count);
        for(size_t i=1;i<count && i<=selector.combinators.size();++i) {
            const auto relation=selector.combinators[i-1];
            // A sibling's features need not occur in the subject's ancestors.
            // Stop collecting to the left of that relation. Functional arms
            // likewise contribute no mandatory outer-compound identities here.
            if(relation!=' ' && relation!='>') continue;
            prepared[i]=prepared[i-1];
            const auto& compound=selector.compiled_compounds[i-1];
            if(!compound.valid) continue;
            if(!compound.tag.empty() && compound.tag!="*") prepared[i].add('t',compound.tag);
            for(const auto& [kind,name]:compound.identities) prepared[i].add(kind,name);
        }
        requirement_entries+=count;
        return requirements.emplace(&selector,std::move(prepared)).first->second[component];
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
        const auto needed=required(selector,component);
        return needed.empty() || ancestors(document,node).contains(needed);
    }
};
} // namespace webscene_native::css
