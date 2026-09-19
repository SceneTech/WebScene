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

struct constraint_validity final {
    bool value_missing{false};
    bool type_mismatch{false};
    bool pattern_mismatch{false};
    bool too_long{false};
    bool too_short{false};
    bool range_underflow{false};
    bool range_overflow{false};
    bool step_mismatch{false};
    bool bad_input{false};
    bool custom_error{false};

    bool valid() const noexcept {
        return !value_missing && !type_mismatch && !pattern_mismatch
            && !too_long && !too_short && !range_underflow
            && !range_overflow && !step_mismatch && !bad_input
            && !custom_error;
    }
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
    date,
    month,
    week,
    time,
};

struct numeric_type_parameters final {
    numeric_input_kind kind{numeric_input_kind::none};
    std::optional<double> minimum;
    std::optional<double> maximum;
    double step{1.0};
    double step_base{0.0};
    bool step_any{false};
    bool periodic{false};
};

inline bool leap_year(uint64_t year) {
    return year%4U==0U && (year%100U!=0U || year%400U==0U);
}

inline unsigned days_in_month(uint64_t year,unsigned month) {
    constexpr std::array<unsigned,12U> days{
        31U,28U,31U,30U,31U,30U,31U,31U,30U,31U,30U,31U};
    return days[month-1U]+(month==2U && leap_year(year)?1U:0U);
}

inline std::optional<uint64_t> decimal_component(std::string_view text) {
    if(text.empty()) return std::nullopt;
    uint64_t result{};
    const auto parsed=std::from_chars(text.data(),text.data()+text.size(),result);
    if(parsed.ec!=std::errc{} || parsed.ptr!=text.data()+text.size())
        return std::nullopt;
    return result;
}

inline int64_t days_from_civil(uint64_t year,unsigned month,unsigned day) {
    auto adjusted=static_cast<int64_t>(year)-(month<=2U?1:0);
    const auto era=adjusted/400;
    const auto year_of_era=static_cast<unsigned>(adjusted-era*400);
    const auto adjusted_month=static_cast<unsigned>(
        static_cast<int>(month)+(month>2U?-3:9));
    const auto day_of_year=(153U*adjusted_month+2U)/5U+day-1U;
    const auto day_of_era=year_of_era*365U+year_of_era/4U-year_of_era/100U
        +day_of_year;
    return era*146097+static_cast<int64_t>(day_of_era)-719468;
}

struct civil_date final {
    int64_t year{};
    unsigned month{};
    unsigned day{};
};

inline civil_date civil_from_days(int64_t days) {
    days+=719468;
    const auto era=(days>=0?days:days-146096)/146097;
    const auto day_of_era=static_cast<unsigned>(days-era*146097);
    const auto year_of_era=(day_of_era-day_of_era/1460U+day_of_era/36524U
        -day_of_era/146096U)/365U;
    auto year=static_cast<int64_t>(year_of_era)+era*400;
    const auto day_of_year=day_of_era-(365U*year_of_era+year_of_era/4U
        -year_of_era/100U);
    const auto month_prime=(5U*day_of_year+2U)/153U;
    const auto day=day_of_year-(153U*month_prime+2U)/5U+1U;
    const auto month=static_cast<unsigned>(
        static_cast<int>(month_prime)+(month_prime<10U?3:-9));
    year+=month<=2U;
    return {year,month,day};
}

inline unsigned iso_weekday(int64_t days) {
    auto weekday=(days+3)%7;
    if(weekday<0) weekday+=7;
    return static_cast<unsigned>(weekday)+1U;
}

inline unsigned iso_weeks_in_year(uint64_t year) {
    const auto january_first=iso_weekday(days_from_civil(year,1U,1U));
    return january_first==4U || (january_first==3U && leap_year(year))?53U:52U;
}

inline std::optional<std::pair<uint64_t,unsigned>> parse_year_suffix(
    std::string_view text,std::string_view marker) {
    const auto separator=text.find(marker);
    if(separator<4U || separator==std::string_view::npos
        || separator+marker.size()+2U!=text.size()) return std::nullopt;
    const auto year=decimal_component(text.substr(0U,separator));
    const auto suffix=decimal_component(text.substr(separator+marker.size()));
    if(!year || *year==0U || *year>std::numeric_limits<uint32_t>::max()
        || !suffix) return std::nullopt;
    return std::pair<uint64_t,unsigned>{*year,static_cast<unsigned>(*suffix)};
}

inline std::optional<double> date_number(std::string_view text) {
    const auto month_separator=text.rfind('-');
    if(month_separator==std::string_view::npos || month_separator+3U!=text.size())
        return std::nullopt;
    const auto year_month=parse_year_suffix(
        text.substr(0U,month_separator),"-");
    const auto day=decimal_component(text.substr(month_separator+1U));
    if(!year_month || !day || year_month->second<1U || year_month->second>12U
        || *day<1U || *day>days_in_month(year_month->first,year_month->second))
        return std::nullopt;
    return static_cast<double>(days_from_civil(
        year_month->first,year_month->second,static_cast<unsigned>(*day)))
        *86400000.0;
}

inline std::optional<double> month_number(std::string_view text) {
    const auto parsed=parse_year_suffix(text,"-");
    if(!parsed || parsed->second<1U || parsed->second>12U)
        return std::nullopt;
    return (static_cast<double>(parsed->first)-1970.0)*12.0
        +static_cast<double>(parsed->second)-1.0;
}

inline std::optional<double> week_number(std::string_view text) {
    const auto parsed=parse_year_suffix(text,"-W");
    if(!parsed || parsed->second<1U
        || parsed->second>iso_weeks_in_year(parsed->first)) return std::nullopt;
    const auto january_fourth=days_from_civil(parsed->first,1U,4U);
    const auto week_one_monday=january_fourth
        -static_cast<int64_t>(iso_weekday(january_fourth)-1U);
    return static_cast<double>(week_one_monday
        +static_cast<int64_t>(parsed->second-1U)*7)*86400000.0;
}

inline std::optional<double> time_number(std::string_view text) {
    if(text.size()<5U || text[2]!=':') return std::nullopt;
    const auto hour=decimal_component(text.substr(0U,2U));
    const auto minute=decimal_component(text.substr(3U,2U));
    if(!hour || !minute || *hour>23U || *minute>59U) return std::nullopt;
    double seconds=0.0;
    if(text.size()>5U) {
        if(text.size()<8U || text[5]!=':') return std::nullopt;
        const auto whole_seconds=decimal_component(text.substr(6U,2U));
        if(!whole_seconds || *whole_seconds>59U) return std::nullopt;
        seconds=static_cast<double>(*whole_seconds);
        if(text.size()>8U) {
            if(text[8]!='.' || text.size()==9U) return std::nullopt;
            auto scale=0.1;
            for(size_t index=9U;index<text.size();++index) {
                if(text[index]<'0' || text[index]>'9') return std::nullopt;
                seconds+=static_cast<double>(text[index]-'0')*scale;
                scale*=0.1;
            }
        }
    }
    return (static_cast<double>(*hour)*3600.0
        +static_cast<double>(*minute)*60.0+seconds)*1000.0;
}

inline std::optional<double> numeric_value(
    numeric_input_kind kind,std::string_view text) {
    switch(kind) {
    case numeric_input_kind::number:
    case numeric_input_kind::range: return finite_number(text);
    case numeric_input_kind::date: return date_number(text);
    case numeric_input_kind::month: return month_number(text);
    case numeric_input_kind::week: return week_number(text);
    case numeric_input_kind::time: return time_number(text);
    default: return std::nullopt;
    }
}

inline numeric_input_kind numeric_kind(const dom_node& node) {
    if(node.tag!="input") return numeric_input_kind::none;
    const auto authored=node.attributes.find("type");
    const auto type=authored==node.attributes.end()
        ? std::string_view{"text"}:std::string_view{authored->second};
    if(form_keyword_equals(type,"number")) return numeric_input_kind::number;
    if(form_keyword_equals(type,"range")) return numeric_input_kind::range;
    if(form_keyword_equals(type,"date")) return numeric_input_kind::date;
    if(form_keyword_equals(type,"month")) return numeric_input_kind::month;
    if(form_keyword_equals(type,"week")) return numeric_input_kind::week;
    if(form_keyword_equals(type,"time")) return numeric_input_kind::time;
    return numeric_input_kind::none;
}

inline std::optional<double> numeric_attribute(
    const dom_node& node,std::string_view name,numeric_input_kind kind) {
    const auto authored=node.attributes.find(std::string(name));
    return authored==node.attributes.end()
        ? std::nullopt:numeric_value(kind,authored->second);
}

inline numeric_type_parameters numeric_parameters(const dom_node& node) {
    numeric_type_parameters result;
    result.kind=numeric_kind(node);
    if(result.kind==numeric_input_kind::none) return result;
    result.minimum=numeric_attribute(node,"min",result.kind);
    result.maximum=numeric_attribute(node,"max",result.kind);
    if(result.kind==numeric_input_kind::range) {
        if(!result.minimum) result.minimum=0.0;
        if(!result.maximum) result.maximum=100.0;
        if(*result.maximum<*result.minimum) result.maximum=result.minimum;
    }
    auto step_scale=1.0;
    if(result.kind==numeric_input_kind::date) step_scale=86400000.0;
    else if(result.kind==numeric_input_kind::week) {
        step_scale=604800000.0;
        result.step_base=-259200000.0;
    } else if(result.kind==numeric_input_kind::time) {
        step_scale=1000.0;
        result.step=60.0;
        result.periodic=true;
    }
    result.step*=step_scale;
    const auto authored_step=node.attributes.find("step");
    result.step_any=authored_step!=node.attributes.end()
        && form_keyword_equals(authored_step->second,"any");
    if(!result.step_any && authored_step!=node.attributes.end()) {
        const auto parsed=finite_number(authored_step->second);
        if(parsed && *parsed>0.0) result.step=*parsed*step_scale;
    }
    if(result.minimum) result.step_base=*result.minimum;
    else if(const auto authored_value=numeric_attribute(node,"value",result.kind))
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

inline std::string padded_decimal(uint64_t value,size_t width) {
    auto result=std::to_string(value);
    if(result.size()<width) result.insert(0U,width-result.size(),'0');
    return result;
}

inline std::optional<std::string> format_date_number(double value) {
    if(!std::isfinite(value)) return std::nullopt;
    const auto day_value=std::floor(value/86400000.0);
    if(day_value<static_cast<double>(days_from_civil(1U,1U,1U))
        || day_value>static_cast<double>(days_from_civil(
            std::numeric_limits<uint32_t>::max(),12U,31U)))
        return std::nullopt;
    const auto date=civil_from_days(static_cast<int64_t>(day_value));
    if(date.year<=0 || date.year>std::numeric_limits<uint32_t>::max())
        return std::nullopt;
    return padded_decimal(static_cast<uint64_t>(date.year),4U)+"-"
        +padded_decimal(date.month,2U)+"-"+padded_decimal(date.day,2U);
}

inline std::optional<std::string> format_month_number(double value) {
    if(!std::isfinite(value) || value!=std::trunc(value)) return std::nullopt;
    const auto absolute=value+1970.0*12.0;
    if(absolute<0.0
        || absolute>static_cast<double>(std::numeric_limits<uint32_t>::max())*12.0)
        return std::nullopt;
    const auto months=static_cast<uint64_t>(absolute);
    const auto year=months/12U;
    const auto month=months%12U+1U;
    if(year==0U) return std::nullopt;
    return padded_decimal(year,4U)+"-"+padded_decimal(month,2U);
}

inline std::optional<std::string> format_week_number(double value) {
    if(!std::isfinite(value)) return std::nullopt;
    const auto day_value=std::floor(value/86400000.0);
    if(day_value<static_cast<double>(days_from_civil(1U,1U,1U))
        || day_value>static_cast<double>(days_from_civil(
            std::numeric_limits<uint32_t>::max(),12U,31U)))
        return std::nullopt;
    const auto day=static_cast<int64_t>(day_value);
    const auto thursday=day+4-static_cast<int64_t>(iso_weekday(day));
    const auto date=civil_from_days(thursday);
    if(date.year<=0 || date.year>std::numeric_limits<uint32_t>::max())
        return std::nullopt;
    const auto january_fourth=days_from_civil(
        static_cast<uint64_t>(date.year),1U,4U);
    const auto week_one_monday=january_fourth
        -static_cast<int64_t>(iso_weekday(january_fourth)-1U);
    const auto week=static_cast<uint64_t>((day-week_one_monday)/7+1);
    return padded_decimal(static_cast<uint64_t>(date.year),4U)+"-W"
        +padded_decimal(week,2U);
}

inline std::optional<std::string> format_time_number(double value) {
    if(!std::isfinite(value)) return std::nullopt;
    value=std::fmod(value,86400000.0);
    if(value<0.0) value+=86400000.0;
    const auto hour=static_cast<unsigned>(value/3600000.0);
    value-=static_cast<double>(hour)*3600000.0;
    const auto minute=static_cast<unsigned>(value/60000.0);
    value-=static_cast<double>(minute)*60000.0;
    auto result=padded_decimal(hour,2U)+":"+padded_decimal(minute,2U);
    if(value==0.0) return result;
    const auto seconds=value/1000.0;
    std::array<char,64U> buffer{};
    const auto formatted=std::to_chars(buffer.data(),buffer.data()+buffer.size(),
        seconds,std::chars_format::fixed,15);
    if(formatted.ec!=std::errc{}) return std::nullopt;
    std::string seconds_text(buffer.data(),formatted.ptr);
    while(seconds_text.ends_with('0')) seconds_text.pop_back();
    if(seconds_text.ends_with('.')) seconds_text.pop_back();
    if(seconds<10.0) seconds_text.insert(0U,1U,'0');
    return result+":"+seconds_text;
}

inline std::optional<std::string> format_numeric_value(
    numeric_input_kind kind,double value) {
    switch(kind) {
    case numeric_input_kind::number:
    case numeric_input_kind::range: return format_finite_number(value);
    case numeric_input_kind::date: return format_date_number(value);
    case numeric_input_kind::month: return format_month_number(value);
    case numeric_input_kind::week: return format_week_number(value);
    case numeric_input_kind::time: return format_time_number(value);
    default: return std::nullopt;
    }
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
    if(parameters.kind!=numeric_input_kind::range && value.empty()) return value;
    const auto parsed=numeric_value(parameters.kind,value);
    if(parameters.kind!=numeric_input_kind::range)
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
        if(!parameters.minimum && !parameters.maximum)
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
        auto number=numeric_value(parameters.kind,value);
        if(!number && parameters.kind==numeric_input_kind::range)
            number=range_default_value(parameters);
        if(!number) return numeric_range_state::not_applicable;
        const auto reversed=parameters.periodic && parameters.minimum
            && parameters.maximum && *parameters.minimum>*parameters.maximum;
        return (reversed
                ? *number>*parameters.maximum && *number<*parameters.minimum
                : (parameters.minimum && *number<*parameters.minimum)
                    || (parameters.maximum && *number>*parameters.maximum))
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
        && node.tag!="select" && node.tag!="textarea"
        && !node.form_control().form_associated_custom_element) return false;
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
    const auto form_associated_custom=
        node.form_control().form_associated_custom_element;
    if(node.tag!="button" && node.tag!="input"
        && node.tag!="select" && node.tag!="textarea"
        && !form_associated_custom) return false;
    if(is_effectively_disabled(document,node)
        || ((node.tag=="input" || node.tag=="textarea")
            && node.attributes.contains("readonly"))) return false;
    for(auto* ancestor=document.dom_parent(node);ancestor!=nullptr;
        ancestor=document.dom_parent(*ancestor))
        if(ancestor->tag=="datalist") return false;
    if(form_associated_custom) return true;
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

inline bool is_constraint_validation_control(const dom_node& node) {
    return node.tag=="button" || node.tag=="input"
        || node.tag=="select" || node.tag=="textarea"
        || node.form_control().form_associated_custom_element;
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
    auto number=numeric_value(parameters.kind,value);
    if(!number) {
        if(parameters.kind!=numeric_input_kind::range) result.bad_input=true;
        return result;
    }
    const auto reversed=parameters.periodic && parameters.minimum
        && parameters.maximum && *parameters.minimum>*parameters.maximum;
    if(reversed) {
        result.range_underflow=*number>*parameters.maximum
            && *number<*parameters.minimum;
        result.range_overflow=result.range_underflow;
    } else {
        result.range_underflow=parameters.minimum && *number<*parameters.minimum;
        result.range_overflow=parameters.maximum && *number>*parameters.maximum;
    }
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

inline constraint_validity constraint_validity_state(
    const native_document& document,const dom_node& node) {
    constraint_validity result;
    if(node.form_control().form_associated_custom_element) {
        const auto flags=node.form_control().element_internals_validity_flags;
        result.value_missing=(flags&(1U<<0U))!=0U;
        result.type_mismatch=(flags&(1U<<1U))!=0U;
        result.pattern_mismatch=(flags&(1U<<2U))!=0U;
        result.too_long=(flags&(1U<<3U))!=0U;
        result.too_short=(flags&(1U<<4U))!=0U;
        result.range_underflow=(flags&(1U<<5U))!=0U;
        result.range_overflow=(flags&(1U<<6U))!=0U;
        result.step_mismatch=(flags&(1U<<7U))!=0U;
        result.bad_input=(flags&(1U<<8U))!=0U;
        result.custom_error=(flags&(1U<<9U))!=0U;
        return result;
    }
    result.custom_error=!node.form_control().custom_validation_message.empty();
    if(!will_validate(document,node)) return result;
    const auto text_validity=text_validity_state(document,node);
    result.type_mismatch=text_validity.type_mismatch;
    result.pattern_mismatch=text_validity.pattern_mismatch;
    result.too_long=text_validity.too_long;
    result.too_short=text_validity.too_short;
    const auto numeric_validity=numeric_validity_state(document,node);
    result.bad_input=numeric_validity.bad_input;
    result.range_underflow=numeric_validity.range_underflow;
    result.range_overflow=numeric_validity.range_overflow;
    result.step_mismatch=numeric_validity.step_mismatch;
    if(input_type_is(node,"radio")) {
        result.value_missing=radio_group_value_missing(document,node);
    } else if(node.tag=="select") {
        result.value_missing=select_value_missing(node);
    } else if(node.attributes.contains("required") && required_applies(node)) {
        const auto empty = input_type_is(node,"checkbox")
            ? !input_checked(node)
            : supports_text_selection(&node)
                ? text_value_empty(node)
                : !node.attributes.contains("value")
                    || node.attributes.at("value").empty();
        result.value_missing=empty;
    }
    return result;
}

inline std::string validation_message(
    const native_document& document,const dom_node& node) {
    if(!will_validate(document,node)) return {};
    const auto state=constraint_validity_state(document,node);
    if(node.form_control().form_associated_custom_element)
        return state.valid()?std::string{}:node.form_control().custom_validation_message;
    if(state.custom_error) return node.form_control().custom_validation_message;
    if(state.value_missing) return "Please fill out this field.";
    if(state.type_mismatch) return "Please enter a valid value.";
    if(state.pattern_mismatch) return "Please match the requested format.";
    if(state.too_long) return "Please shorten this text.";
    if(state.too_short) return "Please lengthen this text.";
    if(state.range_underflow) return "The value is below the allowed minimum.";
    if(state.range_overflow) return "The value is above the allowed maximum.";
    if(state.step_mismatch) return "Please enter a valid value.";
    if(state.bad_input) return "Please enter a valid value.";
    return {};
}

inline std::vector<const dom_node*> form_validation_controls(
    const native_document& document,const dom_node& form) {
    std::vector<const dom_node*> result;
    if(form.tag!="form") return result;
    const auto* root=tree_root(document,form);
    const auto collect=[&](const auto& recurse,const dom_node& current)->void {
        if(is_constraint_validation_control(current)
            && form_owner(document,current)==&form) result.push_back(&current);
        for(const auto* child:current.children) {
            if(child==nullptr || child->tag=="iframe"
                || document.is_shadow_root(*child)) continue;
            recurse(recurse,*child);
        }
    };
    collect(collect,*root);
    return result;
}

inline bool form_has_invalid_control(
    const native_document& document,const dom_node& form) {
    const auto controls=form_validation_controls(document,form);
    return std::any_of(controls.begin(),controls.end(),[&](const auto* control) {
        return control!=nullptr && will_validate(document,*control)
            && !constraint_validity_state(document,*control).valid();
    });
}

inline bool fieldset_has_invalid_descendant(
    const native_document& document,const dom_node& fieldset) {
    if(fieldset.tag!="fieldset") return false;
    const auto find_invalid=[&](const auto& recurse,const dom_node& current)->bool {
        for(const auto* child:current.children) {
            if(child==nullptr || child->tag=="iframe"
                || document.is_shadow_root(*child)) continue;
            if(is_constraint_validation_control(*child)
                && will_validate(document,*child)
                && !constraint_validity_state(document,*child).valid()) return true;
            if(recurse(recurse,*child)) return true;
        }
        return false;
    };
    return find_invalid(find_invalid,fieldset);
}

inline simple_validity_state validity_state(
    const native_document& document,const dom_node& node) {
    if(node.tag=="form") return form_has_invalid_control(document,node)
        ? simple_validity_state::invalid:simple_validity_state::valid;
    if(node.tag=="fieldset") return fieldset_has_invalid_descendant(document,node)
        ? simple_validity_state::invalid:simple_validity_state::valid;
    const auto form_control = node.tag == "button" || node.tag == "input"
        || node.tag == "select" || node.tag == "textarea"
        || node.form_control().form_associated_custom_element;
    if(!form_control || !will_validate(document,node))
        return simple_validity_state::not_applicable;
    return constraint_validity_state(document,node).valid()
        ? simple_validity_state::valid:simple_validity_state::invalid;
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
