#pragma once
#include "webscene_native_dom.h"

namespace webscene_native::forms {
inline void collect_descendants_by_tag(
        dom_node& root,
        std::string_view tag,
        std::vector<dom_node*>& result)
    {
        for (auto* child : root.children) {
            if (child == nullptr) continue;
            if (child->tag == tag) result.push_back(child);
            collect_descendants_by_tag(*child, tag, result);
        }
    }

inline dom_node* containing_select(dom_node& option)
    {
        auto* select = option.parent;
        while (select != nullptr && select->tag != "select") select = select->parent;
        return select;
    }

inline bool option_is_disabled(const dom_node& option)
    {
        if (option.attributes.contains("disabled")) return true;
        for (auto* ancestor = option.parent;
             ancestor != nullptr && ancestor->tag != "select";
             ancestor = ancestor->parent) {
            if (ancestor->tag == "optgroup"
                && ancestor->attributes.contains("disabled")) return true;
        }
        return false;
    }

inline bool option_is_selected(dom_node& option)
    {
        if (option.form_control().selectedness_initialized) {
            return option.form_control().selectedness;
        }
        auto* select = containing_select(option);
        if (select == nullptr) return option.attributes.contains("selected");
        if (select->form_control().selection_explicitly_empty) return false;
        std::vector<dom_node*> options;
        collect_descendants_by_tag(*select, "option", options);
        const auto has_live_selection = std::any_of(
            options.begin(), options.end(), [](const auto* candidate) {
                return candidate != nullptr
                    && candidate->form_control().selectedness_initialized
                    && candidate->form_control().selectedness;
            });
        if (has_live_selection) return false;
        const auto authored = std::find_if(
            options.begin(),
            options.end(),
            [](const auto* candidate) {
                return candidate != nullptr && candidate->attributes.contains("selected");
            });
        if (authored != options.end()) {
            return select->attributes.contains("multiple")
                ? option.attributes.contains("selected")
                : *authored == &option;
        }
        if (select->attributes.contains("multiple")) return false;
        return !options.empty() && options.front() == &option;
    }

inline std::string option_text(const dom_node& option)
    {
        std::string text;
        const auto append=[&](auto&& self,const dom_node& node)->void {
            if(node.kind==dom_node_kind::text) text+=node.text_content;
            for(auto* child:node.children) if(child) self(self,*child);
        };
        append(append,option);
        std::string result;bool space=false;
        for(char c:text) {
            if(c==' ' || c=='\t' || c=='\n' || c=='\r' || c=='\f') {space=!result.empty();continue;}
            if(space) result+=' ';
            result+=c;space=false;
        }
        return result;
    }

inline std::string option_label(const dom_node& option)
    {
        if (auto authored = option.attributes.find("label");
            authored != option.attributes.end() && !authored->second.empty()) {
            return authored->second;
        }
        return option_text(option);
    }

inline std::string option_value(const dom_node& option) {
    if(auto authored=option.attributes.find("value");authored!=option.attributes.end()) return authored->second;
    return option_text(option);
}

inline std::vector<dom_node*> select_options(dom_node& select)
    {
        std::vector<dom_node*> options;
        collect_descendants_by_tag(select,"option",options);
        return options;
    }

inline dom_node* selected_option(dom_node& select)
    {
        auto options=select_options(select);
        const auto selected=std::find_if(options.begin(),options.end(),[](auto* option){
            return option!=nullptr && option_is_selected(*option);
        });
        return selected==options.end()?nullptr:*selected;
    }

inline bool select_option(dom_node& select,dom_node& option)
    {
        if(containing_select(option)!=&select || option_is_disabled(option)) return false;
        auto options=select_options(select);
        if(std::find(options.begin(),options.end(),&option)==options.end()) return false;
        const auto* previous=selected_option(select);
        select.mutable_form_control().selection_explicitly_empty=false;
        for(auto* candidate:options) {
            auto& state=candidate->mutable_form_control();
            state.selectedness_initialized=true;
            state.selectedness=candidate==&option;
        }
        return previous!=&option;
    }

inline void set_select_value(dom_node& select,std::string_view value) {
    std::vector<dom_node*> options;collect_descendants_by_tag(select,"option",options);
    bool matched=false;
    for(auto* option:options) {
        auto& state=option->mutable_form_control();
        state.selectedness_initialized=true;
        state.selectedness=!matched && option_value(*option)==value;
        matched=matched || state.selectedness;
    }
    select.mutable_form_control().selection_explicitly_empty=!matched;
}

inline bool supports_text_selection(const dom_node* node)
    {
        if (node == nullptr || (node->tag != "input" && node->tag != "textarea")) {
            return false;
        }
        if (node->tag == "textarea") return true;
        const auto type = node->attributes.find("type");
        if (type == node->attributes.end()) return true;
        return type->second != "hidden" && type->second != "checkbox" && type->second != "radio"
            && type->second != "button" && type->second != "submit"
            && type->second != "reset" && type->second != "file"
            && type->second != "image" && type->second != "range"
            && type->second != "color";
    }

inline bool is_text_control(const dom_node* node)
    {
        return supports_text_selection(node)
            && !node->attributes.contains("disabled");
    }

// Initialize the live value once from authored markup. No HTML parser is needed.
inline void ensure_text_value(dom_node& node) {
    auto& control=node.mutable_form_control();
    if(control.value_initialized) return;
    if(node.tag=="textarea") {
        control.value.clear();
        std::string text;
        const auto collect=[&](auto&& self,const dom_node& current)->void {
            if(current.kind==dom_node_kind::text) text+=current.text_content;
            for(auto* child:current.children) if(child) self(self,*child);
        };
        collect(collect,node);
        for(size_t i=0;i<text.size();++i) {
            if(text[i]=='\r') {control.value+='\n';if(i+1<text.size() && text[i+1]=='\n') ++i;}
            else control.value+=text[i];
        }
        control.dirty_value=false;
    } else {
        auto attribute=node.attributes.find("value");
        control.value=attribute==node.attributes.end()?std::string{}:attribute->second;
    }
    control.value_initialized=true;
    control.selection_start=control.selection_end=control.value.size();
    control.selection_start_utf16_suboffset=0U;
    control.selection_end_utf16_suboffset=0U;
    control.selection_direction=text_selection_direction::none;
}

inline size_t previous_utf8_boundary(const std::string& value,size_t index) {
    index=std::min(index,value.size());
    if(!index) return 0;
    --index;
    while(index && (static_cast<unsigned char>(value[index])&0xc0U)==0x80U) --index;
    return index;
}
inline size_t next_utf8_boundary(const std::string& value,size_t index) {
    if(index>=value.size()) return value.size();
    ++index;
    while(index<value.size() && (static_cast<unsigned char>(value[index])&0xc0U)==0x80U) ++index;
    return index;
}

inline std::pair<size_t,size_t> utf8_scalar_extent(
    std::string_view value,size_t index) {
    if(index>=value.size()) return {0U,0U};
    const auto first=static_cast<unsigned char>(value[index]);
    size_t bytes=1U;
    size_t utf16_units=1U;
    if((first&0xe0U)==0xc0U) bytes=2U;
    else if((first&0xf0U)==0xe0U) bytes=3U;
    else if((first&0xf8U)==0xf0U) {bytes=4U;utf16_units=2U;}
    if(index+bytes>value.size()) return {1U,1U};
    for(size_t offset=1U;offset<bytes;++offset)
        if((static_cast<unsigned char>(value[index+offset])&0xc0U)!=0x80U)
            return {1U,1U};
    return {bytes,utf16_units};
}

// Form-control storage remains byte-oriented so edits never split UTF-8.
// Selection APIs expose browser UTF-16 code-unit offsets at the JS boundary.
struct utf8_selection_offset {
    size_t byte_offset{};
    uint8_t utf16_suboffset{};
};

inline size_t utf16_offset_from_utf8_selection(
    std::string_view value,utf8_selection_offset selection) {
    const auto byte_offset=selection.byte_offset;
    const auto limit=std::min(byte_offset,value.size());
    size_t bytes=0U,units=0U;
    while(bytes<limit) {
        const auto [length,utf16_units]=utf8_scalar_extent(value,bytes);
        if(length==0U || bytes+length>limit) break;
        bytes+=length;
        units+=utf16_units;
    }
    if(bytes<value.size()) {
        const auto [length,utf16_units]=utf8_scalar_extent(value,bytes);
        if(length!=0U)
            units+=std::min<size_t>(selection.utf16_suboffset,utf16_units-1U);
    }
    return units;
}

inline utf8_selection_offset utf8_selection_from_utf16_offset(
    std::string_view value,size_t utf16_offset) {
    size_t bytes=0U,units=0U;
    while(bytes<value.size() && units<utf16_offset) {
        const auto [length,utf16_units]=utf8_scalar_extent(value,bytes);
        if(length==0U) break;
        if(units+utf16_units>utf16_offset)
            return {bytes,static_cast<uint8_t>(utf16_offset-units)};
        bytes+=length;
        units+=utf16_units;
    }
    return {bytes,0U};
}

inline bool selection_offset_less(
    utf8_selection_offset left,utf8_selection_offset right) {
    return left.byte_offset<right.byte_offset
        || (left.byte_offset==right.byte_offset
            && left.utf16_suboffset<right.utf16_suboffset);
}

inline bool selection_offset_equal(
    utf8_selection_offset left,utf8_selection_offset right) {
    return left.byte_offset==right.byte_offset
        && left.utf16_suboffset==right.utf16_suboffset;
}

inline std::vector<uint16_t> utf16_units_from_utf8(std::string_view value) {
    std::vector<uint16_t> result;
    result.reserve(value.size());
    for(size_t index=0;index<value.size();) {
        const auto [length,utf16_length]=utf8_scalar_extent(value,index);
        if(length==0U) break;
        uint32_t scalar=static_cast<unsigned char>(value[index]);
        if(length>1U) {
            scalar&=length==2U?0x1fU:length==3U?0x0fU:0x07U;
            for(size_t offset=1U;offset<length;++offset)
                scalar=(scalar<<6U)|(static_cast<unsigned char>(value[index+offset])&0x3fU);
        }
        if(utf16_length==2U) {
            scalar-=0x10000U;
            result.push_back(static_cast<uint16_t>(0xd800U+(scalar>>10U)));
            result.push_back(static_cast<uint16_t>(0xdc00U+(scalar&0x3ffU)));
        } else result.push_back(static_cast<uint16_t>(scalar));
        index+=length;
    }
    return result;
}

inline void append_wtf8_scalar(std::string& result,uint32_t scalar) {
    if(scalar<0x80U) result.push_back(static_cast<char>(scalar));
    else if(scalar<0x800U) {
        result.push_back(static_cast<char>(0xc0U|(scalar>>6U)));
        result.push_back(static_cast<char>(0x80U|(scalar&0x3fU)));
    } else if(scalar<0x10000U) {
        result.push_back(static_cast<char>(0xe0U|(scalar>>12U)));
        result.push_back(static_cast<char>(0x80U|((scalar>>6U)&0x3fU)));
        result.push_back(static_cast<char>(0x80U|(scalar&0x3fU)));
    } else {
        result.push_back(static_cast<char>(0xf0U|(scalar>>18U)));
        result.push_back(static_cast<char>(0x80U|((scalar>>12U)&0x3fU)));
        result.push_back(static_cast<char>(0x80U|((scalar>>6U)&0x3fU)));
        result.push_back(static_cast<char>(0x80U|(scalar&0x3fU)));
    }
}

inline std::string wtf8_from_utf16_units(const std::vector<uint16_t>& units) {
    std::string result;
    result.reserve(units.size()*3U);
    for(size_t index=0;index<units.size();++index) {
        uint32_t scalar=units[index];
        if(scalar>=0xd800U && scalar<=0xdbffU && index+1U<units.size()
            && units[index+1U]>=0xdc00U && units[index+1U]<=0xdfffU) {
            scalar=0x10000U+((scalar-0xd800U)<<10U)+(units[++index]-0xdc00U);
        }
        append_wtf8_scalar(result,scalar);
    }
    return result;
}

}
