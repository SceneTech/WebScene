#pragma once

#include "webscene_native_dom.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace webscene_native::css {

enum class css_property_id : uint16_t {
#include "generated/webscene_css_property_ids.inc"
};

#include "generated/webscene_css_property_identity.inc"

enum class css_wide_keyword : uint8_t { none, inherit, initial, unset, revert };

enum class specified_css_kind : uint8_t {
    invalid, wide_keyword, keyword, number, integer, length, length_list,
    color, color_list, border, transform, transform_origin, track_list,
    grid_placement, flex_flow, flex, overflow, background, shadow, font,
    list_style, content, component_list, deferred, custom_tokens
};

enum class css_typed_component_kind : uint8_t {
    identifier, keyword, number, percentage, length, time, angle, string, url, function
};

struct css_typed_component final {
    css_typed_component_kind kind{css_typed_component_kind::identifier};
    std::string name;
    float number{};
    css_length length{};
    std::vector<css_typed_component> arguments;
};

struct css_color_value final {
    uint32_t rgba{};
    bool current_color{};
    bool none{};
    bool valid{};
};

struct css_border_value final {
    css_length width{};
    css_color_value color{};
    std::string style;
    bool width_specified{};
    bool color_specified{};
    bool style_specified{};
    bool none{};
};

struct css_transform_value final {
    css_length translate_x{};
    css_length translate_y{};
    float scale_x{1.0F};
    float scale_y{1.0F};
    float rotate_degrees{};
    bool none{};
};

struct css_transform_origin_value final { css_length x{}, y{}; };

struct css_track_list_value final {
    std::vector<node_style::grid_data::track> tracks;
    bool subgrid{};
    bool multiple{};
};

struct css_flex_flow_value final {
    flex_direction direction{flex_direction::row};
    bool reverse{};
    bool wrap{};
};

struct css_flex_value final {
    float grow{};
    float shrink{1.0F};
    css_length basis{};
    bool basis_auto{};
    bool none{};
};

struct css_overflow_value final {
    overflow_mode x{overflow_mode::visible};
    overflow_mode y{overflow_mode::visible};
};

enum class css_background_image_kind : uint8_t { none, linear_gradient, radial_gradient, url, other };
struct css_background_value final {
    css_color_value color{};
    bool has_color{};
    css_background_image_kind image_kind{css_background_image_kind::none};
    std::string url;
    std::vector<css_typed_component> image_components;
};

struct css_shadow_value final {
    std::array<css_length, 4> lengths{};
    uint8_t length_count{};
    css_color_value color{};
    bool color_specified{};
    bool inset{};
    bool multiple{};
    bool valid{};
};

struct css_font_value final {
    css_typed_component size;
    css_typed_component line_height;
    int32_t weight{400};
    std::vector<css_typed_component> family;
    bool complete{true};
};

struct css_list_style_value final { std::string position, type; };
struct css_content_value final { std::string decoded; bool generated{}; };

struct css_deferred_segment final {
    enum class kind : uint8_t { literal, variable } type{kind::literal};
    std::string text;
    std::string fallback;
};
struct css_deferred_value final { std::vector<css_deferred_segment> segments; };

struct specified_css_value final {
    specified_css_kind kind{specified_css_kind::invalid};
    css_wide_keyword wide{css_wide_keyword::none};
    bool valid{};
    std::string keyword;
    float number{};
    int32_t integer{};
    bool automatic{};
    css_length length{};
    std::array<css_length, 4> lengths{};
    uint8_t length_count{};
    std::array<std::string, 4> length_keywords{};
    css_color_value color{};
    std::array<css_color_value, 4> colors{};
    uint8_t color_count{};
    css_border_value border{};
    css_transform_value transform{};
    css_transform_origin_value transform_origin{};
    css_track_list_value tracks{};
    std::vector<std::string> grid_components;
    css_flex_flow_value flex_flow{};
    css_flex_value flex{};
    css_overflow_value overflow{};
    css_background_value background{};
    css_shadow_value shadow{};
    css_font_value font{};
    css_list_style_value list_style{};
    css_content_value content{};
    std::vector<css_typed_component> components;
    css_deferred_value deferred{};
    std::string token_data;

    bool fully_typed() const noexcept { return valid && kind != specified_css_kind::invalid; }
};

inline std::string css_ir_lower(std::string_view value) {
    std::string result(value);
    for (auto& c : result) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
    return result;
}
inline std::string css_ir_trim(std::string_view value) {
    const auto first=value.find_first_not_of(" \t\r\n\f");
    if(first==std::string_view::npos)return{};
    const auto last=value.find_last_not_of(" \t\r\n\f");
    return std::string(value.substr(first,last-first+1U));
}
inline std::vector<std::string> css_ir_split(std::string_view value,char delimiter=0) {
    std::vector<std::string> result; size_t start=std::string_view::npos; int depth=0; char quote=0;
    auto finish=[&](size_t end){if(start==std::string_view::npos)return;auto item=css_ir_trim(value.substr(start,end-start));if(!item.empty()||delimiter)result.push_back(std::move(item));start=std::string_view::npos;};
    for(size_t i=0;i<=value.size();++i){const bool end=i==value.size();const char c=end?' ':value[i];
        if(!end&&quote){if(c=='\\'&&i+1U<value.size())++i;else if(c==quote)quote=0;if(start==std::string_view::npos)start=i;continue;}
        if(!end&&(c=='\''||c=='"')){quote=c;if(start==std::string_view::npos)start=i;continue;}
        if(!end&&c=='(')++depth;else if(!end&&c==')'&&depth>0)--depth;
        const bool separator=end||(depth==0&&(delimiter?c==delimiter:std::isspace(static_cast<unsigned char>(c))));
        if(!separator&&start==std::string_view::npos)start=i;if(separator)finish(i);
    }return result;
}
inline css_wide_keyword css_ir_wide(std::string_view value){const auto v=css_ir_lower(css_ir_trim(value));if(v=="inherit")return css_wide_keyword::inherit;if(v=="initial")return css_wide_keyword::initial;if(v=="unset")return css_wide_keyword::unset;if(v=="revert")return css_wide_keyword::revert;return css_wide_keyword::none;}

inline css_property_id property_id(std::string_view raw) {
    if(raw.starts_with("--"))return css_property_id::custom;
    for(const auto character:raw)if(character>='A'&&character<='Z')
        return generated_property_id_lowercase(css_ir_lower(raw));
    return generated_property_id_lowercase(raw);
}

inline std::optional<float> css_ir_number(std::string_view value){const auto t=css_ir_trim(value);if(t.empty())return{};char* end=nullptr;const auto v=std::strtof(t.c_str(),&end);if(end==t.c_str()||*end!='\0'||!std::isfinite(v))return{};return v;}
inline css_color_value css_ir_color(std::string_view value){const auto t=css_ir_trim(value),l=css_ir_lower(t);css_color_value c;c.none=l=="none";c.current_color=l=="currentcolor";c.rgba=c.current_color||c.none?0U:native_document::parse_color(t);c.valid=c.none||c.current_color||l=="transparent"||c.rgba!=0U||l=="black"||l=="#000"||l=="#000000";return c;}
inline overflow_mode css_ir_overflow(std::string_view v){const auto l=css_ir_lower(v);if(l=="hidden")return overflow_mode::hidden;if(l=="clip")return overflow_mode::clip;if(l=="auto")return overflow_mode::automatic;if(l=="scroll")return overflow_mode::scroll;return overflow_mode::visible;}

inline css_typed_component css_ir_component(std::string token){
    css_typed_component c;token=css_ir_trim(token);const auto lower=css_ir_lower(token);c.name=token;
    if(token.size()>=2U&&((token.front()=='"'&&token.back()=='"')||(token.front()=='\''&&token.back()=='\''))){c.kind=css_typed_component_kind::string;c.name=token.substr(1U,token.size()-2U);return c;}
    const auto open=token.find('(');if(open!=std::string::npos&&token.ends_with(')')){const auto fn=css_ir_lower(token.substr(0,open));c.kind=fn=="url"?css_typed_component_kind::url:css_typed_component_kind::function;c.name=fn;const auto body=std::string_view(token).substr(open+1U,token.size()-open-2U);for(auto& part:css_ir_split(body,','))c.arguments.push_back(css_ir_component(part));return c;}
    auto numeric=token;css_typed_component_kind numeric_kind=css_typed_component_kind::number;
    if(lower.ends_with("ms")){numeric.resize(numeric.size()-2U);numeric_kind=css_typed_component_kind::time;}
    else if(lower.ends_with('s')&&!lower.ends_with("px")){numeric.resize(numeric.size()-1U);numeric_kind=css_typed_component_kind::time;}
    else if(lower.ends_with("deg")){numeric.resize(numeric.size()-3U);numeric_kind=css_typed_component_kind::angle;}
    else if(lower.ends_with("turn")){numeric.resize(numeric.size()-4U);numeric_kind=css_typed_component_kind::angle;}
    else if(lower.ends_with('%')){numeric.resize(numeric.size()-1U);numeric_kind=css_typed_component_kind::percentage;}
    if(const auto n=css_ir_number(numeric)){c.kind=numeric_kind;c.number=*n;if(numeric_kind==css_typed_component_kind::time&&lower.ends_with('s')&&!lower.ends_with("ms"))c.number*=1000.0F;if(numeric_kind==css_typed_component_kind::angle&&lower.ends_with("turn"))c.number*=360.0F;return c;}
    if(!token.empty()&&(std::isdigit(static_cast<unsigned char>(token.front()))||token.front()=='.'||token.front()=='-'||token.front()=='+')){c.kind=css_typed_component_kind::length;c.length=native_document::parse_length(token);return c;}
    static const std::array<std::string_view,33> keywords={"auto","none","normal","block","inline","flex","grid","absolute","relative","fixed","sticky","hidden","visible","scroll","clip","center","start","end","stretch","baseline","wrap","nowrap","solid","dashed","dotted","currentcolor","transparent","inherit","initial","unset","revert","cover","contain"};
    c.kind=std::find(keywords.begin(),keywords.end(),lower)!=keywords.end()?css_typed_component_kind::keyword:css_typed_component_kind::identifier;c.name=lower;return c;
}
inline std::vector<css_typed_component> css_ir_components(std::string_view value){std::vector<css_typed_component> out;for(auto& t:css_ir_split(value))out.push_back(css_ir_component(t));return out;}
inline css_deferred_value css_ir_deferred(std::string_view input){css_deferred_value out;size_t cursor=0;while(cursor<input.size()){const auto start=input.find("var(",cursor);if(start==std::string_view::npos){if(cursor<input.size())out.segments.push_back({css_deferred_segment::kind::literal,std::string(input.substr(cursor)),{}});break;}if(start>cursor)out.segments.push_back({css_deferred_segment::kind::literal,std::string(input.substr(cursor,start-cursor)),{}});int depth=1;size_t close=std::string_view::npos,comma=std::string_view::npos;for(size_t i=start+4U;i<input.size();++i){if(input[i]=='(')++depth;else if(input[i]==')'&&--depth==0){close=i;break;}else if(input[i]==','&&depth==1&&comma==std::string_view::npos)comma=i;}if(close==std::string_view::npos){out.segments.push_back({css_deferred_segment::kind::literal,std::string(input.substr(start)),{}});break;}const auto end=comma==std::string_view::npos?close:comma;css_deferred_segment s;s.type=css_deferred_segment::kind::variable;s.text=css_ir_trim(input.substr(start+4U,end-start-4U));if(comma!=std::string_view::npos)s.fallback=css_ir_trim(input.substr(comma+1U,close-comma-1U));out.segments.push_back(std::move(s));cursor=close+1U;}return out;}

inline specified_css_value compile_specified_value(css_property_id property,std::string_view source){
    specified_css_value out;const auto value=css_ir_trim(source);if(property==css_property_id::unknown)return out;if(property==css_property_id::custom){out.kind=specified_css_kind::custom_tokens;out.token_data=std::string(source);out.valid=true;return out;}if(value.find("var(")!=std::string::npos){out.kind=specified_css_kind::deferred;out.deferred=css_ir_deferred(value);out.valid=true;return out;}out.wide=css_ir_wide(value);if(out.wide!=css_wide_keyword::none){out.kind=specified_css_kind::wide_keyword;out.valid=true;return out;}
    const auto keyword=[&]{out.kind=specified_css_kind::keyword;out.keyword=css_ir_lower(value);out.valid=true;};
    const auto components=[&]{out.kind=specified_css_kind::component_list;out.components=css_ir_components(value);out.valid=true;};
    const auto length=[&]{out.kind=specified_css_kind::length;const auto l=css_ir_lower(value);if(l=="auto"||l=="none"||l=="normal"||l=="fit-content"||l=="max-content"||l=="min-content")out.keyword=l;else out.length=native_document::parse_length(value);out.valid=true;};
    const auto length_list=[&](size_t max){const auto parts=css_ir_split(value);if(parts.empty()||parts.size()>max)return;out.kind=specified_css_kind::length_list;out.length_count=static_cast<uint8_t>(parts.size());for(size_t i=0;i<parts.size();++i){const auto l=css_ir_lower(parts[i]);if(l=="auto"||l=="normal"||l=="none")out.length_keywords[i]=l;else out.lengths[i]=native_document::parse_length(parts[i]);}out.valid=true;};
    const auto color=[&]{out.kind=specified_css_kind::color;out.color=css_ir_color(value);out.valid=out.color.valid;};
    switch(generated_property_grammar(property)){
    case native_property_grammar::keyword:keyword();return out;
    case native_property_grammar::component_list:components();return out;
    case native_property_grammar::length:length();return out;
    case native_property_grammar::length_list_4:length_list(4);return out;
    case native_property_grammar::length_list_2:length_list(2);return out;
    case native_property_grammar::color:color();return out;
    case native_property_grammar::complex:break;
    case native_property_grammar::special:return out;
    }
    switch(property){
    case css_property_id::contain:case css_property_id::cursor:case css_property_id::font_family:components();break;
    case css_property_id::fill:case css_property_id::stroke:color();break;
    case css_property_id::border_color:{const auto p=css_ir_split(value);if(p.empty()||p.size()>4)break;out.kind=specified_css_kind::color_list;out.color_count=static_cast<uint8_t>(p.size());out.valid=true;for(size_t i=0;i<p.size();++i){out.colors[i]=css_ir_color(p[i]);out.valid&=out.colors[i].valid;}break;}
    case css_property_id::border:case css_property_id::border_top:case css_property_id::border_right:case css_property_id::border_bottom:case css_property_id::border_left:case css_property_id::border_inline:case css_property_id::border_block:case css_property_id::outline:{out.kind=specified_css_kind::border;out.border.color.current_color=true;out.border.color.valid=true;for(auto& t:css_ir_split(value)){const auto l=css_ir_lower(t);if(l=="none"){out.border.none=true;out.border.style="none";out.border.style_specified=true;out.border.width={};out.border.width_specified=true;}else if(l=="thin"||l=="medium"||l=="thick"){out.border.width={l=="thin"?1.0F:l=="medium"?3.0F:5.0F,length_unit::pixels};out.border.width_specified=true;}else if(l=="solid"||l=="dashed"||l=="dotted"||l=="double"||l=="hidden"){out.border.style=l;out.border.style_specified=true;}else if(!t.empty()&&(std::isdigit(static_cast<unsigned char>(t.front()))||t.front()=='.'||t.front()=='-'||t.front()=='+')){out.border.width=native_document::parse_length(t);out.border.width_specified=true;}else{auto c=css_ir_color(t);if(c.valid){out.border.color=c;out.border.color_specified=true;}}}out.valid=true;break;}
    case css_property_id::transform:{out.kind=specified_css_kind::transform;out.transform.none=css_ir_lower(value)=="none";native_document::parse_transform_translate(value,out.transform.translate_x,out.transform.translate_y,out.transform.scale_x,out.transform.scale_y,out.transform.rotate_degrees);out.valid=true;break;}
    case css_property_id::transform_origin:{out.kind=specified_css_kind::transform_origin;native_document::parse_transform_origin(value,out.transform_origin.x,out.transform_origin.y);out.valid=true;break;}
    case css_property_id::grid_template_columns:case css_property_id::grid_template_rows:case css_property_id::grid_auto_columns:{out.kind=specified_css_kind::track_list;const auto l=css_ir_lower(value);out.tracks.subgrid=l=="subgrid"||l.starts_with("subgrid ");if(!out.tracks.subgrid){for(auto& t:css_ir_split(value)){if(t.empty()||t.front()=='[')continue;const auto tl=css_ir_lower(t);if(tl=="none"||tl.starts_with("repeat(")){out.tracks.multiple|=tl.starts_with("repeat(");out.tracks.tracks.clear();break;}node_style::grid_data::track tr;if(tl=="auto"||tl=="max-content")tr.kind=node_style::grid_data::track::sizing::automatic;else if(tl=="min-content")tr.kind=node_style::grid_data::track::sizing::min_content;else if(tl.ends_with("fr")){tr.kind=node_style::grid_data::track::sizing::fractional;auto n=tl.substr(0,tl.size()-2U);tr.fraction=n.empty()?1.0F:css_ir_number(n).value_or(0);}else{tr.kind=node_style::grid_data::track::sizing::fixed;tr.minimum=native_document::parse_length(t);tr.maximum=tr.minimum;}out.tracks.tracks.push_back(tr);}out.tracks.multiple|=out.tracks.tracks.size()>1U;}else out.tracks.multiple=true;out.valid=true;break;}
    case css_property_id::grid_area:case css_property_id::grid_row:case css_property_id::grid_row_start:case css_property_id::grid_row_end:case css_property_id::grid_column:case css_property_id::grid_column_start:case css_property_id::grid_column_end:out.kind=specified_css_kind::grid_placement;out.grid_components=css_ir_split(value,'/');out.valid=true;break;
    case css_property_id::flex_flow:{out.kind=specified_css_kind::flex_flow;for(auto t:css_ir_split(value)){t=css_ir_lower(t);if(t=="row"||t=="row-reverse"||t=="column"||t=="column-reverse"){out.flex_flow.direction=t=="row"||t=="row-reverse"?flex_direction::row:flex_direction::column;out.flex_flow.reverse=t.ends_with("reverse");}else if(t=="wrap"||t=="wrap-reverse")out.flex_flow.wrap=true;}out.valid=true;break;}
    case css_property_id::flex_grow:case css_property_id::flex_shrink:case css_property_id::opacity:{if(auto n=css_ir_number(value)){out.kind=specified_css_kind::number;out.number=*n;out.valid=true;}break;}
    case css_property_id::z_index:{out.kind=specified_css_kind::integer;if(css_ir_lower(value)=="auto"){out.automatic=true;out.valid=true;}else{const auto r=std::from_chars(value.data(),value.data()+value.size(),out.integer);out.valid=r.ec==std::errc{}&&r.ptr==value.data()+value.size();}break;}
    case css_property_id::flex:{out.kind=specified_css_kind::flex;const auto l=css_ir_lower(value);if(l=="none"){out.flex.none=true;out.flex.shrink=0;out.valid=true;break;}const auto p=css_ir_split(value);if(!p.empty())out.flex.grow=css_ir_lower(p[0])=="auto"?1.0F:std::max(0.0F,css_ir_number(p[0]).value_or(0));if(p.size()>1)out.flex.shrink=std::max(0.0F,css_ir_number(p[1]).value_or(1));if(p.size()>2){out.flex.basis=native_document::parse_length(p[2]);out.flex.basis_auto=css_ir_lower(p[2])=="auto";}out.valid=true;break;}
    case css_property_id::overflow:case css_property_id::overflow_x:case css_property_id::overflow_y:{const auto p=css_ir_split(value);if(p.empty())break;out.kind=specified_css_kind::overflow;out.overflow.x=css_ir_overflow(p[0]);out.overflow.y=css_ir_overflow(p.size()>1?p[1]:p[0]);if(property==css_property_id::overflow_x)out.overflow.y=out.overflow.x;if(property==css_property_id::overflow_y)out.overflow.x=out.overflow.y;out.valid=true;break;}
    case css_property_id::background:case css_property_id::background_image:{out.kind=specified_css_kind::background;const auto lower=css_ir_lower(value);const auto gradient=lower.find("linear-gradient(");const auto radial=lower.find("radial-gradient(");const auto url=lower.find("url(");if(property==css_property_id::background){auto c=css_ir_color(value);if(c.valid){out.background.color=c;out.background.has_color=true;}}if(gradient!=std::string::npos){out.background.image_kind=css_background_image_kind::linear_gradient;out.background.image_components=css_ir_components(value.substr(gradient));}else if(radial!=std::string::npos){out.background.image_kind=css_background_image_kind::radial_gradient;out.background.image_components=css_ir_components(value.substr(radial));}else if(url!=std::string::npos){out.background.image_kind=css_background_image_kind::url;const auto close=value.find(')',url+4U);if(close!=std::string::npos){out.background.url=css_ir_trim(std::string_view(value).substr(url+4U,close-url-4U));if(out.background.url.size()>1U&&((out.background.url.front()=='"'&&out.background.url.back()=='"')||(out.background.url.front()=='\''&&out.background.url.back()=='\'')))out.background.url=out.background.url.substr(1U,out.background.url.size()-2U);}}else if(lower!="none"&&!out.background.has_color)out.background.image_kind=css_background_image_kind::other;out.valid=true;break;}
    case css_property_id::box_shadow:{out.kind=specified_css_kind::shadow;const auto shadows=css_ir_split(value,',');const auto first=shadows.empty()?value:shadows.front();out.shadow.multiple=shadows.size()>1U;for(auto& t:css_ir_split(first)){const auto l=css_ir_lower(t);if(l=="inset"){out.shadow.inset=true;continue;}auto c=css_ir_color(t);if(c.valid){out.shadow.color=c;out.shadow.color_specified=true;continue;}if(!t.empty()&&(std::isdigit(static_cast<unsigned char>(t.front()))||t.front()=='.'||t.front()=='-'||t.front()=='+')&&out.shadow.length_count<4)out.shadow.lengths[out.shadow.length_count++]=native_document::parse_length(t);}out.shadow.valid=css_ir_lower(value)=="none"||out.shadow.length_count>=2;out.valid=true;break;}
    case css_property_id::font:{out.kind=specified_css_kind::font;out.components=css_ir_components(value);out.valid=true;break;}
    case css_property_id::font_weight:{const auto l=css_ir_lower(value);out.kind=specified_css_kind::integer;if(l=="bold"||l=="bolder"){out.integer=700;out.valid=true;}else if(l=="normal"){out.integer=400;out.valid=true;}else{const auto r=std::from_chars(value.data(),value.data()+value.size(),out.integer);out.valid=r.ec==std::errc{};}break;}
    case css_property_id::list_style:{out.kind=specified_css_kind::list_style;for(auto t:css_ir_split(value)){t=css_ir_lower(t);if(t=="inside"||t=="outside")out.list_style.position=t;else if(t=="none"||t=="decimal"||t=="decimal-leading-zero"||t=="disc"||t=="circle"||t=="square")out.list_style.type=t;}out.valid=true;break;}
    case css_property_id::content:{out.kind=specified_css_kind::content;auto v=value;const auto l=css_ir_lower(v);out.content.generated=l!="none"&&l!="normal";if(out.content.generated){if(v.size()>1U&&((v.front()=='"'&&v.back()=='"')||(v.front()=='\''&&v.back()=='\'')))v=v.substr(1U,v.size()-2U);out.content.decoded=v;}out.valid=true;break;}
    case css_property_id::unknown:case css_property_id::custom:break;
    default:break; // Simple families returned through generated metadata above.
    }
    return out;
}
inline specified_css_value compile_specified_value(std::string_view property,std::string_view value){return compile_specified_value(property_id(property),value);}

} // namespace webscene_native::css
