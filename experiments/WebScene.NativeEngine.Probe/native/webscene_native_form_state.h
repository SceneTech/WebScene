#pragma once
#include "webscene_native_dom.h"
#include <array>
#include <charconv>
#include <cmath>
#include <regex>

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

struct text_constraint_validity final {
    bool applicable{false};
    bool type_mismatch{false};
    bool pattern_mismatch{false};
    bool too_long{false};
    bool too_short{false};

    bool valid() const noexcept {
        return applicable && !type_mismatch && !pattern_mismatch
            && !too_long && !too_short;
    }

    bool operator==(const text_constraint_validity&) const = default;
};

struct numeric_constraint_validity final {
    bool applicable{false};
    bool bad_input{false};
    bool range_underflow{false};
    bool range_overflow{false};
    bool step_mismatch{false};

    bool valid() const noexcept {
        return applicable && !bad_input && !range_underflow
            && !range_overflow && !step_mismatch;
    }

    bool operator==(const numeric_constraint_validity&) const = default;
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
        size_t cursor=0U;
        if(text[cursor]=='-') {
            if(++cursor==text.size()) return std::nullopt;
        } else if(text[cursor]=='+') {
            return std::nullopt;
        }
        auto integer_digits=false;
        while(cursor<text.size() && text[cursor]>='0' && text[cursor]<='9') {
            integer_digits=true;
            ++cursor;
        }
        auto fractional_digits=false;
        if(cursor<text.size() && text[cursor]=='.') {
            ++cursor;
            while(cursor<text.size() && text[cursor]>='0' && text[cursor]<='9') {
                fractional_digits=true;
                ++cursor;
            }
            if(!fractional_digits) return std::nullopt;
        }
        if(!integer_digits && !fractional_digits) return std::nullopt;
        if(cursor<text.size() && (text[cursor]=='e' || text[cursor]=='E')) {
            ++cursor;
            if(cursor<text.size() && (text[cursor]=='+' || text[cursor]=='-'))
                ++cursor;
            const auto exponent_begin=cursor;
            while(cursor<text.size() && text[cursor]>='0' && text[cursor]<='9')
                ++cursor;
            if(cursor==exponent_begin) return std::nullopt;
        }
        if(cursor!=text.size()) return std::nullopt;
        double result{};
        const auto parsed=std::from_chars(text.data(),text.data()+text.size(),result);
        if(parsed.ec!=std::errc{} || parsed.ptr!=text.data()+text.size()
            || !std::isfinite(result)) return std::nullopt;
        return result;
    }

enum class numeric_input_kind : uint8_t {
    none,
    number,
    range,
};

struct numeric_type_parameters final {
    numeric_input_kind kind{numeric_input_kind::none};
    std::optional<double> minimum;
    std::optional<double> maximum;
    double step{1.0};
    double step_base{0.0};
    bool step_any{false};
};

inline numeric_input_kind numeric_kind(const dom_node& node) {
    if(node.tag!="input") return numeric_input_kind::none;
    const auto authored=node.attributes.find("type");
    const auto type=authored==node.attributes.end()
        ? std::string_view{"text"}:std::string_view{authored->second};
    if(form_keyword_equals(type,"number")) return numeric_input_kind::number;
    if(form_keyword_equals(type,"range")) return numeric_input_kind::range;
    return numeric_input_kind::none;
}

inline std::optional<double> numeric_attribute(
    const dom_node& node,std::string_view name) {
    const auto authored=node.attributes.find(std::string(name));
    return authored==node.attributes.end()
        ? std::nullopt:finite_number(authored->second);
}

inline numeric_type_parameters numeric_parameters(const dom_node& node) {
    numeric_type_parameters result;
    result.kind=numeric_kind(node);
    if(result.kind==numeric_input_kind::none) return result;
    result.minimum=numeric_attribute(node,"min");
    result.maximum=numeric_attribute(node,"max");
    if(result.kind==numeric_input_kind::range) {
        if(!result.minimum) result.minimum=0.0;
        if(!result.maximum) result.maximum=100.0;
        if(*result.maximum<*result.minimum) result.maximum=result.minimum;
    }
    const auto authored_step=node.attributes.find("step");
    result.step_any=authored_step!=node.attributes.end()
        && form_keyword_equals(authored_step->second,"any");
    if(!result.step_any && authored_step!=node.attributes.end()) {
        const auto parsed=finite_number(authored_step->second);
        if(parsed && *parsed>0.0) result.step=*parsed;
    }
    if(result.minimum) result.step_base=*result.minimum;
    else if(const auto authored_value=numeric_attribute(node,"value"))
        result.step_base=*authored_value;
    return result;
}

inline bool numeric_step_mismatch(
    double value,const numeric_type_parameters& parameters) {
    if(parameters.step_any) return false;
    const auto steps=(value-parameters.step_base)/parameters.step;
    return std::abs(steps-std::round(steps))>1e-7;
}

inline std::optional<std::string> format_finite_number(double value) {
    if(!std::isfinite(value)) return std::nullopt;
    std::array<char,64U> buffer{};
    const auto formatted=std::to_chars(
        buffer.data(),buffer.data()+buffer.size(),value,std::chars_format::general);
    if(formatted.ec!=std::errc{}) return std::nullopt;
    return std::string(buffer.data(),formatted.ptr);
}

inline double range_default_value(const numeric_type_parameters& parameters) {
    return *parameters.minimum+(*parameters.maximum-*parameters.minimum)*0.5;
}

inline double sanitize_range_number(
    double value,const numeric_type_parameters& parameters) {
    value=std::clamp(value,*parameters.minimum,*parameters.maximum);
    if(!numeric_step_mismatch(value,parameters)) return value;
    const auto steps=(value-parameters.step_base)/parameters.step;
    value=parameters.step_base+std::floor(steps+0.5)*parameters.step;
    if(value<*parameters.minimum)
        value=parameters.step_base+std::ceil(
            (*parameters.minimum-parameters.step_base)/parameters.step)
            *parameters.step;
    if(value>*parameters.maximum)
        value=parameters.step_base+std::floor(
            (*parameters.maximum-parameters.step_base)/parameters.step)
            *parameters.step;
    return std::clamp(value,*parameters.minimum,*parameters.maximum);
}

inline std::string sanitize_programmatic_numeric_value(
    const dom_node& node,std::string value) {
    const auto parameters=numeric_parameters(node);
    if(parameters.kind==numeric_input_kind::none) return value;
    if(parameters.kind==numeric_input_kind::number && value.empty()) return value;
    const auto parsed=finite_number(value);
    if(parameters.kind==numeric_input_kind::number)
        return parsed?std::move(value):std::string{};
    const auto number=sanitize_range_number(
        parsed.value_or(range_default_value(parameters)),parameters);
    return format_finite_number(number).value_or(std::string{});
}

inline numeric_range_state range_state(
    const dom_node& node,std::optional<std::string_view> live_value=std::nullopt)
    {
        if(node.tag!="input") return numeric_range_state::not_applicable;
        const auto parameters=numeric_parameters(node);
        if(parameters.kind==numeric_input_kind::none)
            return numeric_range_state::not_applicable;
        std::string owned_value;
        std::string_view value;
        if(live_value) value=*live_value;
        else if(node.form_control().value_initialized) value=node.form_control().value;
        else {
            const auto authored=node.attributes.find("value");
            owned_value=sanitize_programmatic_numeric_value(
                node,authored==node.attributes.end()?std::string{}:authored->second);
            value=owned_value;
        }
        auto number=finite_number(value);
        if(!number && parameters.kind==numeric_input_kind::range)
            number=range_default_value(parameters);
        if(!number) return numeric_range_state::not_applicable;
        return (parameters.minimum && *number<*parameters.minimum)
                || (parameters.maximum && *number>*parameters.maximum)
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

inline void collect_descendants_by_tag(
        const dom_node& root,
        std::string_view tag,
        std::vector<const dom_node*>& result)
    {
        for (const auto* child : root.children) {
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

inline const dom_node* containing_select(const dom_node& option)
    {
        const auto* select = option.parent;
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

inline bool option_is_selected(const dom_node& option)
    {
        if (option.form_control().selectedness_initialized) {
            return option.form_control().selectedness;
        }
        auto* select = containing_select(option);
        if (select == nullptr) return option.attributes.contains("selected");
        if (select->form_control().selection_explicitly_empty) return false;
        std::vector<const dom_node*> options;
        collect_descendants_by_tag(*select, "option", options);
        const auto has_live_selection = std::any_of(
            options.begin(), options.end(), [](const auto* candidate) {
                return candidate != nullptr
                    && candidate->form_control().selectedness_initialized
                    && candidate->form_control().selectedness;
            });
        if (select->attributes.contains("multiple"))
            return option.attributes.contains("selected");
        if (has_live_selection) return false;
        const auto authored = std::find_if(
            options.begin(),
            options.end(),
            [](const auto* candidate) {
                return candidate != nullptr && candidate->attributes.contains("selected");
            });
        if (authored != options.end()) {
            return *authored == &option;
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

inline size_t select_display_size(const dom_node& select)
    {
        const auto fallback=select.attributes.contains("multiple")?4U:1U;
        const auto authored=select.attributes.find("size");
        if(authored==select.attributes.end() || authored->second.empty())
            return fallback;
        size_t parsed{};
        const auto converted=std::from_chars(
            authored->second.data(),authored->second.data()+authored->second.size(),parsed);
        return converted.ec==std::errc{}
                && converted.ptr==authored->second.data()+authored->second.size()
                && parsed>0U
            ? parsed:fallback;
    }

inline bool select_value_missing(const dom_node& select)
    {
        if(select.tag!="select" || !select.attributes.contains("required"))
            return false;
        std::vector<const dom_node*> options;
        collect_descendants_by_tag(select,"option",options);
        std::vector<const dom_node*> selected;
        for(auto* option:options)
            if(option!=nullptr && option_is_selected(*option))
                selected.push_back(option);
        if(selected.empty()) return true;
        if(select.attributes.contains("multiple") || select_display_size(select)!=1U)
            return false;
        // The empty first option is a placeholder label only when it is a
        // direct child. An empty option inside OPTGROUP remains a real value.
        const auto* placeholder=options.empty()?nullptr:options.front();
        return selected.size()==1U && selected.front()==placeholder
            && placeholder->parent==&select && option_value(*placeholder).empty();
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

inline const dom_node* tree_root(
    const native_document& document,const dom_node& node)
    {
        const dom_node* root=&node;
        while(const auto* parent=document.dom_parent(*root)) {
            // A frame document is represented below its owning iframe in the
            // native tree, but it remains a distinct DOM tree.
            if(parent->tag=="iframe") break;
            root=parent;
        }
        return root;
    }

inline const dom_node* form_owner(
    const native_document& document,const dom_node& control)
    {
        const auto explicit_owner=control.attributes.find("form");
        if(explicit_owner!=control.attributes.end()) {
            if(explicit_owner->second.empty()) return nullptr;
            const auto* root=tree_root(document,control);
            const auto find=[&](const auto& recurse,const dom_node& current)
                ->const dom_node* {
                if(current.tag=="form"
                    && current.id_attribute==explicit_owner->second) return &current;
                for(const auto* child:current.children) {
                    if(child==nullptr || child->tag=="iframe"
                        || document.is_shadow_root(*child)) continue;
                    if(const auto* matched=recurse(recurse,*child)) return matched;
                }
                return nullptr;
            };
            return find(find,*root);
        }
        for(auto* ancestor=document.dom_parent(control);ancestor!=nullptr;
            ancestor=document.dom_parent(*ancestor)) {
            if(ancestor->tag=="iframe") break;
            if(ancestor->tag=="form") return ancestor;
        }
        return nullptr;
    }

inline bool same_radio_group(
    const native_document& document,
    const dom_node& reference,
    const dom_node& candidate)
    {
        if(!input_type_is(reference,"radio") || !input_type_is(candidate,"radio"))
            return false;
        const auto reference_name=reference.attributes.find("name");
        const auto candidate_name=candidate.attributes.find("name");
        return reference_name!=reference.attributes.end()
            && !reference_name->second.empty()
            && candidate_name!=candidate.attributes.end()
            && candidate_name->second==reference_name->second
            && tree_root(document,reference)==tree_root(document,candidate)
            && form_owner(document,reference)==form_owner(document,candidate);
    }

inline std::vector<const dom_node*> radio_group_members(
    const native_document& document,const dom_node& radio)
    {
        if(!input_type_is(radio,"radio")) return {};
        const auto name=radio.attributes.find("name");
        if(name==radio.attributes.end() || name->second.empty()) return {&radio};
        std::vector<const dom_node*> result;
        const auto* root=tree_root(document,radio);
        const auto collect=[&](const auto& recurse,const dom_node& current)->void {
            if(same_radio_group(document,radio,current)) result.push_back(&current);
            for(const auto* child:current.children) {
                if(child==nullptr || child->tag=="iframe"
                    || document.is_shadow_root(*child)) continue;
                recurse(recurse,*child);
            }
        };
        collect(collect,*root);
        return result;
    }

inline bool radio_group_value_missing(
    const native_document& document,const dom_node& radio)
    {
        const auto members=radio_group_members(document,radio);
        const auto required=std::any_of(
            members.begin(),members.end(),[](const auto* member) {
                return member!=nullptr && member->attributes.contains("required");
            });
        const auto checked=std::any_of(
            members.begin(),members.end(),[](const auto* member) {
                return member!=nullptr && input_checked(*member);
            });
        return required && !checked;
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
        control.value=sanitize_programmatic_numeric_value(
            node,std::move(control.value));
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
    if(numeric_kind(node)!=numeric_input_kind::none)
        return sanitize_programmatic_numeric_value(
            node,attribute==node.attributes.end()?std::string{}:attribute->second).empty();
    return attribute==node.attributes.end() || attribute->second.empty();
}

inline bool is_effectively_disabled(
    const native_document& document,const dom_node& node) {
    if(node.attributes.contains("disabled")) return true;
    if(node.tag!="button" && node.tag!="input"
        && node.tag!="select" && node.tag!="textarea") return false;
    for(auto* fieldset=document.dom_parent(node);fieldset!=nullptr;
        fieldset=document.dom_parent(*fieldset)) {
        if(fieldset->tag!="fieldset"
            || !fieldset->attributes.contains("disabled")) continue;
        const dom_node* first_legend=nullptr;
        for(const auto* child:fieldset->children) {
            if(child!=nullptr && child->tag=="legend") {
                first_legend=child;
                break;
            }
        }
        auto inside_first_legend=false;
        for(auto* ancestor=&node;ancestor!=fieldset;
            ancestor=document.dom_parent(*ancestor)) {
            if(ancestor==first_legend) {
                inside_first_legend=true;
                break;
            }
        }
        if(!inside_first_legend) return true;
    }
    return false;
}

inline bool will_validate(
    const native_document& document,const dom_node& node) {
    if(node.tag!="button" && node.tag!="input"
        && node.tag!="select" && node.tag!="textarea") return false;
    if(is_effectively_disabled(document,node)
        || ((node.tag=="input" || node.tag=="textarea")
            && node.attributes.contains("readonly"))) return false;
    for(auto* ancestor=document.dom_parent(node);ancestor!=nullptr;
        ancestor=document.dom_parent(*ancestor))
        if(ancestor->tag=="datalist") return false;
    if(node.tag=="input") {
        return !input_type_is(node,"hidden")
            && !input_type_is(node,"button")
            && !input_type_is(node,"reset");
    }
    if(node.tag=="button") {
        const auto authored=node.attributes.find("type");
        const auto type=authored==node.attributes.end()
            ? std::string_view{"submit"}:std::string_view{authored->second};
        return !form_keyword_equals(type,"button")
            && !form_keyword_equals(type,"reset");
    }
    return true;
}

inline numeric_constraint_validity numeric_validity_state(
    const native_document& document,
    const dom_node& node,
    std::optional<std::string_view> live_value=std::nullopt) {
    numeric_constraint_validity result;
    const auto parameters=numeric_parameters(node);
    result.applicable=will_validate(document,node)
        && parameters.kind!=numeric_input_kind::none;
    if(!result.applicable) return result;
    std::string owned_value;
    if(!live_value) {
        if(node.form_control().value_initialized) owned_value=node.form_control().value;
        else if(const auto authored=node.attributes.find("value");
                authored!=node.attributes.end())
            owned_value=sanitize_programmatic_numeric_value(node,authored->second);
        else owned_value=sanitize_programmatic_numeric_value(node,std::string{});
    }
    const auto value=live_value.value_or(owned_value);
    if(value.empty()) return result;
    auto number=finite_number(value);
    if(!number) {
        if(parameters.kind==numeric_input_kind::number) result.bad_input=true;
        return result;
    }
    result.range_underflow=parameters.minimum && *number<*parameters.minimum;
    result.range_overflow=parameters.maximum && *number>*parameters.maximum;
    result.step_mismatch=numeric_step_mismatch(*number,parameters);
    return result;
}

inline bool text_constraint_applies(const dom_node& node) {
    if(node.tag=="textarea") return true;
    if(node.tag!="input") return false;
    const auto authored=node.attributes.find("type");
    const auto type=authored==node.attributes.end()
        ? std::string_view{"text"}:std::string_view{authored->second};
    return form_keyword_equals(type,"text") || form_keyword_equals(type,"search")
        || form_keyword_equals(type,"tel") || form_keyword_equals(type,"url")
        || form_keyword_equals(type,"email") || form_keyword_equals(type,"password")
        || (!form_keyword_equals(type,"hidden") && !form_keyword_equals(type,"number")
            && !form_keyword_equals(type,"range") && !form_keyword_equals(type,"color")
            && !form_keyword_equals(type,"checkbox") && !form_keyword_equals(type,"radio")
            && !form_keyword_equals(type,"button") && !form_keyword_equals(type,"submit")
            && !form_keyword_equals(type,"reset") && !form_keyword_equals(type,"file")
            && !form_keyword_equals(type,"image") && !form_keyword_equals(type,"date")
            && !form_keyword_equals(type,"month") && !form_keyword_equals(type,"week")
            && !form_keyword_equals(type,"time") && !form_keyword_equals(type,"datetime-local"));
}

inline std::string text_control_value(const dom_node& node) {
    if(node.form_control().value_initialized) return node.form_control().value;
    if(node.tag!="textarea") {
        const auto authored=node.attributes.find("value");
        return authored==node.attributes.end()?std::string{}:authored->second;
    }
    std::string result;
    const auto append=[&](const auto& recurse,const dom_node& current)->void {
        if(current.kind==dom_node_kind::text) result+=current.text_content;
        for(const auto* child:current.children) if(child) recurse(recurse,*child);
    };
    append(append,node);
    return result;
}

inline std::string_view trim_ascii_whitespace(std::string_view value) {
    const auto whitespace=[](char character) {
        return character==' ' || character=='\t' || character=='\n'
            || character=='\r' || character=='\f';
    };
    while(!value.empty() && whitespace(value.front())) value.remove_prefix(1U);
    while(!value.empty() && whitespace(value.back())) value.remove_suffix(1U);
    return value;
}

inline bool valid_email_address(std::string_view value) {
    static const std::regex address(
        R"(^[A-Za-z0-9.!#$%&'*+/=?^_`{|}~-]+@[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?(?:\.[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?)*$)",
        std::regex::ECMAScript);
    return std::regex_match(value.begin(),value.end(),address);
}

inline bool email_type_mismatch(const dom_node& node,std::string_view value) {
    value=trim_ascii_whitespace(value);
    if(value.empty()) return false;
    if(!node.attributes.contains("multiple")) return !valid_email_address(value);
    while(true) {
        const auto comma=value.find(',');
        const auto item=trim_ascii_whitespace(value.substr(0,comma));
        if(item.empty() || !valid_email_address(item)) return true;
        if(comma==std::string_view::npos) return false;
        value.remove_prefix(comma+1U);
    }
}

inline bool url_type_mismatch(std::string_view value) {
    value=trim_ascii_whitespace(value);
    if(value.empty()) return false;
    if(!((value.front()>='A' && value.front()<='Z')
        || (value.front()>='a' && value.front()<='z'))) return true;
    size_t colon=1U;
    while(colon<value.size() && value[colon]!=':') {
        const auto character=value[colon];
        if(!((character>='A' && character<='Z')
            || (character>='a' && character<='z')
            || (character>='0' && character<='9')
            || character=='+' || character=='-' || character=='.')) return true;
        ++colon;
    }
    if(colon==value.size()) return true;
    for(const auto character:value)
        if(static_cast<unsigned char>(character)<=0x20U || character==0x7f) return true;
    const auto scheme=value.substr(0,colon);
    if(form_keyword_equals(scheme,"http") || form_keyword_equals(scheme,"https")
        || form_keyword_equals(scheme,"ftp") || form_keyword_equals(scheme,"ws")
        || form_keyword_equals(scheme,"wss")) {
        if(value.substr(colon+1U,2U)!="//") return true;
        const auto host=value.substr(colon+3U);
        return host.empty() || host.front()=='/' || host.front()=='?' || host.front()=='#';
    }
    return false;
}

inline std::optional<size_t> nonnegative_integer_attribute(
    const dom_node& node,std::string_view name) {
    const auto authored=node.attributes.find(std::string(name));
    if(authored==node.attributes.end() || authored->second.empty()) return std::nullopt;
    size_t result{};
    const auto parsed=std::from_chars(authored->second.data(),
        authored->second.data()+authored->second.size(),result);
    return parsed.ec==std::errc{}
            && parsed.ptr==authored->second.data()+authored->second.size()
        ? std::optional<size_t>{result}:std::nullopt;
}

inline size_t utf16_length(std::string_view value) {
    size_t result=0U;
    for(size_t index=0U;index<value.size();) {
        const auto first=static_cast<unsigned char>(value[index]);
        size_t bytes=1U;
        if((first&0xe0U)==0xc0U) bytes=2U;
        else if((first&0xf0U)==0xe0U) bytes=3U;
        else if((first&0xf8U)==0xf0U) bytes=4U;
        result+=bytes==4U?2U:1U;
        index+=std::min(bytes,value.size()-index);
    }
    return result;
}

inline bool pattern_mismatch(const dom_node& node,std::string_view value) {
    const auto authored=node.attributes.find("pattern");
    if(authored==node.attributes.end() || value.empty()) return false;
    try {
        const std::regex pattern(
            "^(?:"+authored->second+")$",std::regex::ECMAScript);
        if(input_type_is(node,"email") && node.attributes.contains("multiple")) {
            while(true) {
                const auto comma=value.find(',');
                if(!std::regex_match(value.begin(),value.begin()
                        +static_cast<std::ptrdiff_t>(comma==std::string_view::npos
                            ? value.size():comma),pattern)) return true;
                if(comma==std::string_view::npos) return false;
                value.remove_prefix(comma+1U);
            }
        }
        return !std::regex_match(value.begin(),value.end(),pattern);
    } catch(const std::regex_error&) {
        return false;
    }
}

inline text_constraint_validity text_validity_state(
    const native_document& document,
    const dom_node& node,
    std::optional<std::string_view> live_value=std::nullopt,
    std::optional<bool> user_edited=std::nullopt) {
    text_constraint_validity result;
    result.applicable=will_validate(document,node) && text_constraint_applies(node);
    if(!result.applicable) return result;
    const auto owned_value=live_value?std::string{}:text_control_value(node);
    const auto value=live_value.value_or(owned_value);
    result.type_mismatch=input_type_is(node,"email")
        ? email_type_mismatch(node,value)
        : input_type_is(node,"url") && url_type_mismatch(value);
    result.pattern_mismatch=pattern_mismatch(node,value);
    const auto edited=user_edited.value_or(node.form_control().value_changed_by_user);
    // User-edited provenance implies the dirty-value state. Keeping the bit
    // distinct lets reset compare the old user value after it has already
    // restored the dirty flag to false.
    if(edited && !value.empty()) {
        const auto length=utf16_length(value);
        if(const auto maximum=nonnegative_integer_attribute(node,"maxlength"))
            result.too_long=length>*maximum;
        if(const auto minimum=nonnegative_integer_attribute(node,"minlength"))
            result.too_short=length<*minimum;
    }
    return result;
}

inline simple_validity_state validity_state(
    const native_document& document,const dom_node& node) {
    const auto form_control = node.tag == "button" || node.tag == "input"
        || node.tag == "select" || node.tag == "textarea"
        || node.tag == "option" || node.tag == "optgroup" || node.tag == "fieldset";
    if (!form_control || node.tag == "fieldset" || node.tag == "optgroup"
        || node.tag == "option" || !will_validate(document,node))
        return simple_validity_state::not_applicable;
    const auto text_validity=text_validity_state(document,node);
    if(text_validity.applicable && !text_validity.valid())
        return simple_validity_state::invalid;
    const auto numeric_validity=numeric_validity_state(document,node);
    if(numeric_validity.applicable && !numeric_validity.valid())
        return simple_validity_state::invalid;
    if(input_type_is(node,"radio")) {
        return radio_group_value_missing(document,node)
            ? simple_validity_state::invalid:simple_validity_state::valid;
    }
    if(node.tag=="select") {
        return select_value_missing(node)
            ? simple_validity_state::invalid:simple_validity_state::valid;
    }
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
