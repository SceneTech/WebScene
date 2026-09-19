#pragma once
#include "webscene_native_dom.h"
#include <charconv>
#include <cmath>

namespace webscene_native::forms {
enum class numeric_range_state : uint8_t {
    not_applicable,
    in_range,
    out_of_range,
};

enum class simple_validity_state : uint8_t {
    not_applicable,
    valid,
    invalid,
};

inline bool form_keyword_equals(std::string_view value,std::string_view expected)
    {
        if(value.size()!=expected.size()) return false;
        for(size_t index=0;index<expected.size();++index) {
            auto character=value[index];
            if(character>='A' && character<='Z') character+='a'-'A';
            if(character!=expected[index]) return false;
        }
        return true;
    }

inline std::optional<double> finite_number(std::string_view text)
    {
        if(text.empty()) return std::nullopt;
        double result{};
        const auto parsed=std::from_chars(text.data(),text.data()+text.size(),result);
        if(parsed.ec!=std::errc{} || parsed.ptr!=text.data()+text.size()
            || !std::isfinite(result)) return std::nullopt;
        return result;
    }

inline numeric_range_state range_state(
    const dom_node& node,std::optional<std::string_view> live_value=std::nullopt)
    {
        if(node.tag!="input") return numeric_range_state::not_applicable;
        const auto authored_type=node.attributes.find("type");
        const auto type=authored_type==node.attributes.end()
            ? std::string_view{"text"}:std::string_view{authored_type->second};
        const auto range=form_keyword_equals(type,"range");
        if(!range && !form_keyword_equals(type,"number"))
            return numeric_range_state::not_applicable;
        const auto bound=[&](std::string_view name)->std::optional<double> {
            const auto authored=node.attributes.find(std::string(name));
            return authored==node.attributes.end()
                ? std::nullopt:finite_number(authored->second);
        };
        auto minimum=bound("min");
        auto maximum=bound("max");
        if(range) {
            if(!minimum) minimum=0.0;
            if(!maximum) maximum=100.0;
            if(*maximum<*minimum) maximum=*minimum;
        }
        std::string_view value;
        if(live_value) value=*live_value;
        else if(node.form_control().value_initialized) value=node.form_control().value;
        else if(const auto authored=node.attributes.find("value");authored!=node.attributes.end())
            value=authored->second;
        auto number=finite_number(value);
        if(!number && range) number=*minimum+(*maximum-*minimum)*0.5;
        if(!number) return numeric_range_state::not_applicable;
        return (minimum && *number<*minimum) || (maximum && *number>*maximum)
            ? numeric_range_state::out_of_range:numeric_range_state::in_range;
    }

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

inline bool required_applies(const dom_node& node)
    {
        if (node.tag == "select" || node.tag == "textarea") return true;
        if (node.tag != "input") return false;
        const auto authored = node.attributes.find("type");
        if (authored == node.attributes.end()) return true;
        const auto equals = [&](std::string_view expected) {
            if (authored->second.size() != expected.size()) return false;
            for (size_t index = 0; index < expected.size(); ++index) {
                auto character = authored->second[index];
                if (character >= 'A' && character <= 'Z') character += 'a' - 'A';
                if (character != expected[index]) return false;
            }
            return true;
        };
        return !equals("hidden") && !equals("range") && !equals("color")
            && !equals("button") && !equals("submit") && !equals("reset")
            && !equals("image");
    }

inline bool input_type_is(const dom_node& node,std::string_view expected)
    {
        if(node.tag!="input") return false;
        const auto authored=node.attributes.find("type");
        const auto type=authored==node.attributes.end()
            ? std::string_view{"text"}:std::string_view{authored->second};
        return form_keyword_equals(type,expected);
    }

inline bool input_checked(const dom_node& node)
    {
        return node.form_control().checkedness_initialized
            ? node.form_control().checkedness
            : node.attributes.contains("checked");
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

// Selector matching is const and may run before the first cascade initializes
// a control. Read the same live/default source without mutating the node.
inline bool text_value_empty(const dom_node& node) {
    if(node.form_control().value_initialized) return node.form_control().value.empty();
    if(node.tag=="textarea") {
        const auto has_text=[](const auto& self,const dom_node& current)->bool {
            if(current.kind==dom_node_kind::text && !current.text_content.empty()) return true;
            for(const auto* child:current.children)
                if(child && self(self,*child)) return true;
            return false;
        };
        return !has_text(has_text,node);
    }
    const auto attribute=node.attributes.find("value");
    return attribute==node.attributes.end() || attribute->second.empty();
}

inline simple_validity_state validity_state(const dom_node& node) {
    const auto form_control = node.tag == "button" || node.tag == "input"
        || node.tag == "select" || node.tag == "textarea"
        || node.tag == "option" || node.tag == "optgroup" || node.tag == "fieldset";
    if (!form_control || node.tag == "fieldset" || node.tag == "optgroup"
        || node.tag == "option") return simple_validity_state::not_applicable;
    if (node.attributes.contains("required")) {
        const auto empty = input_type_is(node,"checkbox")
            ? !input_checked(node)
            : supports_text_selection(&node)
                ? text_value_empty(node)
                : !node.attributes.contains("value")
                    || node.attributes.at("value").empty();
        if (empty) return simple_validity_state::invalid;
    }
    return simple_validity_state::valid;
}

inline bool supports_placeholder_selector(const dom_node& node) {
    return (node.tag=="input" || node.tag=="textarea")
        && supports_text_selection(&node);
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
