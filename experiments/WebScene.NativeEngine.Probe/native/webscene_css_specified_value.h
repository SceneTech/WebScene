#pragma once

#include "webscene_native_dom.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace webscene_native::css {

// Stable property identity shared by runtime parsing, persistent CSS cache and
// build-time generated stylesheets. Aliases resolve to their semantic property;
// custom properties deliberately remain token values because CSS variables are
// substituted at computed-value time by the CSS specification.
enum class css_property_id : uint16_t {
#include "generated/webscene_css_property_ids.inc"
};

#include "generated/webscene_css_property_identity.inc"

enum class css_wide_keyword : uint8_t {
    none,
    inherit,
    initial,
    unset,
    revert,
};

enum class specified_css_kind : uint8_t {
    invalid,
    wide_keyword,
    keyword,
    number,
    integer,
    length,
    length_list,
    color,
    color_list,
    border,
    transform,
    transform_origin,
    track_list,
    grid_placement,
    flex_flow,
    flex,
    overflow,
    background_image,
    component_list,
    shadow,
    font,
    list_style,
    content,
    deferred,
    token_string,
};

struct css_keyword_value final { std::string value; };
struct css_number_value final { float value{}; bool percentage{}; };
struct css_integer_value final { int32_t value{}; bool automatic{}; };
struct css_length_value final { css_length value{}; std::string keyword; };
struct css_length_list_value final {
    std::array<css_length, 4> values{};
    std::array<std::string, 4> keywords{};
    uint8_t count{};
};
struct css_color_value final {
    uint32_t rgba{};
    bool current_color{};
    bool valid{};
};
struct css_color_list_value final {
    std::array<css_color_value, 4> values{};
    uint8_t count{};
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
struct css_transform_origin_value final {
    css_length x{};
    css_length y{};
};
struct css_track_list_value final {
    std::vector<node_style::grid_data::track> tracks;
    bool subgrid{};
    bool multiple{};
    std::string serialization;
};
struct css_grid_placement_value final {
    std::vector<std::string> components;
    std::string serialization;
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
    bool basis_specified{};
    bool none{};
};
struct css_overflow_value final {
    overflow_mode x{overflow_mode::visible};
    overflow_mode y{overflow_mode::visible};
};
enum class css_background_image_kind : uint8_t { none, gradient, url, other };
struct css_background_image_value final {
    css_background_image_kind kind{css_background_image_kind::none};
    std::string serialization;
    std::string url;
};
struct css_component_list_value final {
    std::vector<std::string> components;
    std::string serialization;
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
    std::string size_token;
    std::string line_height_token{"normal"};
    int32_t weight{400};
    std::string family;
    bool complete{true};
};
struct css_list_style_value final {
    std::string position;
    std::string type;
};
struct css_content_value final {
    std::string decoded;
    bool generated{};
};
struct css_deferred_segment final {
    enum class kind : uint8_t { literal, variable } type{kind::literal};
    std::string text;
    std::string fallback;
};
struct css_deferred_value final {
    std::vector<css_deferred_segment> segments;
    std::string serialization;
};
struct css_token_string_value final { std::string value; };

using specified_css_payload = std::variant<
    std::monostate,
    css_keyword_value,
    css_number_value,
    css_integer_value,
    css_length_value,
    css_length_list_value,
    css_color_value,
    css_color_list_value,
    css_border_value,
    css_transform_value,
    css_transform_origin_value,
    css_track_list_value,
    css_grid_placement_value,
    css_flex_flow_value,
    css_flex_value,
    css_overflow_value,
    css_background_image_value,
    css_component_list_value,
    css_shadow_value,
    css_font_value,
    css_list_style_value,
    css_content_value,
    css_deferred_value,
    css_token_string_value>;

struct specified_css_value final {
    specified_css_kind kind{specified_css_kind::invalid};
    css_wide_keyword wide{css_wide_keyword::none};
    specified_css_payload payload{};
    bool valid{};

    bool fully_typed() const noexcept
    {
        return valid && kind != specified_css_kind::invalid;
    }
};

inline std::string specified_ascii_lower(std::string_view value)
{
    std::string result(value);
    for (auto& character : result) {
        if (character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character + ('a' - 'A'));
        }
    }
    return result;
}

inline std::string specified_trim(std::string_view input)
{
    const auto first = input.find_first_not_of(" \t\r\n\f");
    if (first == std::string_view::npos) return {};
    const auto last = input.find_last_not_of(" \t\r\n\f");
    return std::string(input.substr(first, last - first + 1U));
}

inline std::vector<std::string> specified_components(
    std::string_view input, char delimiter = 0)
{
    std::vector<std::string> result;
    size_t start = std::string_view::npos;
    int depth = 0;
    char quote = 0;
    auto finish = [&](size_t end) {
        if (start == std::string_view::npos) return;
        auto value = specified_trim(input.substr(start, end - start));
        if (!value.empty() || delimiter != 0) result.push_back(std::move(value));
        start = std::string_view::npos;
    };
    for (size_t index = 0; index <= input.size(); ++index) {
        const auto at_end = index == input.size();
        const auto character = at_end ? ' ' : input[index];
        if (!at_end && quote != 0) {
            if (character == '\\' && index + 1U < input.size()) ++index;
            else if (character == quote) quote = 0;
            if (start == std::string_view::npos) start = index;
            continue;
        }
        if (!at_end && (character == '\'' || character == '"')) {
            quote = character;
            if (start == std::string_view::npos) start = index;
            continue;
        }
        if (!at_end && character == '(') ++depth;
        else if (!at_end && character == ')' && depth > 0) --depth;
        const auto separator = at_end || (depth == 0 &&
            (delimiter != 0 ? character == delimiter
                : std::isspace(static_cast<unsigned char>(character)) != 0));
        if (!separator && start == std::string_view::npos) start = index;
        if (separator) finish(index);
    }
    return result;
}

inline css_wide_keyword parse_css_wide_keyword(std::string_view value)
{
    const auto lower = specified_ascii_lower(specified_trim(value));
    if (lower == "inherit") return css_wide_keyword::inherit;
    if (lower == "initial") return css_wide_keyword::initial;
    if (lower == "unset") return css_wide_keyword::unset;
    if (lower == "revert") return css_wide_keyword::revert;
    return css_wide_keyword::none;
}

inline css_property_id property_id(std::string_view raw_name)
{
    if (raw_name.starts_with("--")) return css_property_id::custom;
    for (const auto character : raw_name) {
        if (character >= 'A' && character <= 'Z') {
            return generated_property_id_lowercase(specified_ascii_lower(raw_name));
        }
    }
    return generated_property_id_lowercase(raw_name);
}

inline overflow_mode specified_overflow_mode(std::string_view value)
{
    const auto lower = specified_ascii_lower(value);
    if (lower == "hidden") return overflow_mode::hidden;
    if (lower == "clip") return overflow_mode::clip;
    if (lower == "auto") return overflow_mode::automatic;
    if (lower == "scroll") return overflow_mode::scroll;
    return overflow_mode::visible;
}

inline std::optional<float> specified_number(std::string_view input)
{
    const auto text = specified_trim(input);
    if (text.empty()) return std::nullopt;
    char* end = nullptr;
    const auto value = std::strtof(text.c_str(), &end);
    if (end == text.c_str() || *end != '\0' || !std::isfinite(value)) return std::nullopt;
    return value;
}

inline css_color_value specified_color(std::string_view input)
{
    const auto value = specified_trim(input);
    const auto lower = specified_ascii_lower(value);
    css_color_value result;
    result.current_color = lower == "currentcolor";
    result.rgba = result.current_color ? 0U : native_document::parse_color(value);
    result.valid = result.current_color || lower == "transparent" || result.rgba != 0U
        || lower == "black" || lower == "#000" || lower == "#000000";
    return result;
}

inline css_length_value specified_length(std::string_view input)
{
    css_length_value result;
    const auto value = specified_trim(input);
    const auto lower = specified_ascii_lower(value);
    if (lower == "auto" || lower == "none" || lower == "normal"
        || lower == "fit-content" || lower == "max-content" || lower == "min-content") {
        result.keyword = lower;
    } else {
        result.value = native_document::parse_length(value);
    }
    return result;
}

inline css_length_list_value specified_length_list(std::string_view input, size_t maximum = 4U)
{
    css_length_list_value result;
    const auto values = specified_components(input);
    if (values.empty() || values.size() > maximum) return result;
    result.count = static_cast<uint8_t>(values.size());
    for (size_t index = 0; index < values.size(); ++index) {
        const auto parsed = specified_length(values[index]);
        result.values[index] = parsed.value;
        result.keywords[index] = parsed.keyword;
    }
    return result;
}

inline css_border_value specified_border(std::string_view input)
{
    css_border_value result;
    result.color.current_color = true;
    result.color.valid = true;
    for (const auto& token : specified_components(input)) {
        const auto lower = specified_ascii_lower(token);
        if (lower == "none") {
            result.none = true;
            result.width = {};
            result.width_specified = true;
            result.style = "none";
            result.style_specified = true;
            continue;
        }
        if (lower == "thin" || lower == "medium" || lower == "thick") {
            result.width = {lower == "thin" ? 1.0F : lower == "medium" ? 3.0F : 5.0F,
                length_unit::pixels};
            result.width_specified = true;
            continue;
        }
        if (lower == "solid" || lower == "dashed" || lower == "dotted"
            || lower == "double" || lower == "hidden") {
            result.style = lower;
            result.style_specified = true;
            continue;
        }
        if (!token.empty() && (std::isdigit(static_cast<unsigned char>(token.front()))
            || token.front() == '.' || token.front() == '-' || token.front() == '+')) {
            result.width = native_document::parse_length(token);
            result.width_specified = true;
            continue;
        }
        const auto color = specified_color(token);
        if (color.valid) {
            result.color = color;
            result.color_specified = true;
        }
    }
    return result;
}

inline css_track_list_value specified_track_list(std::string_view input)
{
    css_track_list_value result;
    result.serialization = specified_trim(input);
    const auto trimmed = specified_ascii_lower(result.serialization);
    result.subgrid = trimmed == "subgrid" || trimmed.starts_with("subgrid ");
    if (result.subgrid) { result.multiple = true; return result; }
    const auto values = specified_components(input);
    for (const auto& token : values) {
        if (token.empty() || token.front() == '[') continue;
        if (token == "none" || token.starts_with("repeat(")) {
            if (token.starts_with("repeat(")) result.multiple = true;
            result.tracks.clear();
            return result;
        }
        node_style::grid_data::track track;
        const auto lower = specified_ascii_lower(token);
        if (lower == "auto" || lower == "max-content") {
            track.kind = node_style::grid_data::track::sizing::automatic;
        } else if (lower == "min-content") {
            track.kind = node_style::grid_data::track::sizing::min_content;
        } else if (lower.ends_with("fr")) {
            auto numeric = lower.substr(0, lower.size() - 2U);
            const auto fraction = numeric.empty() ? std::optional<float>{1.0F} : specified_number(numeric);
            if (fraction && *fraction > 0) {
                track.kind = node_style::grid_data::track::sizing::fractional;
                track.fraction = *fraction;
            }
        } else if (lower.starts_with("minmax(") && lower.ends_with(')')) {
            const auto body = std::string_view(token).substr(7U, token.size() - 8U);
            const auto arguments = specified_components(body, ',');
            if (arguments.size() == 2U) {
                track.kind = node_style::grid_data::track::sizing::minmax;
                const auto minimum = specified_ascii_lower(arguments[0]);
                const auto maximum = specified_ascii_lower(arguments[1]);
                if (minimum != "auto" && minimum != "min-content" && minimum != "max-content") {
                    track.minimum = native_document::parse_length(arguments[0]);
                }
                if (maximum.ends_with("fr")) {
                    auto numeric = maximum.substr(0, maximum.size() - 2U);
                    const auto fraction = numeric.empty() ? std::optional<float>{1.0F} : specified_number(numeric);
                    if (fraction) track.fraction = *fraction;
                } else if (maximum != "auto" && maximum != "min-content" && maximum != "max-content") {
                    track.maximum = native_document::parse_length(arguments[1]);
                }
            }
        } else {
            track.kind = node_style::grid_data::track::sizing::fixed;
            track.minimum = native_document::parse_length(token);
            track.maximum = track.minimum;
        }
        result.tracks.push_back(track);
    }
    result.multiple = result.tracks.size() > 1U;
    return result;
}

inline css_flex_flow_value specified_flex_flow(std::string_view input)
{
    css_flex_flow_value result;
    for (auto token : specified_components(input)) {
        token = specified_ascii_lower(token);
        if (token == "row" || token == "row-reverse" || token == "column" || token == "column-reverse") {
            result.direction = token == "row" || token == "row-reverse"
                ? flex_direction::row : flex_direction::column;
            result.reverse = token == "row-reverse" || token == "column-reverse";
        } else if (token == "wrap" || token == "wrap-reverse") result.wrap = true;
        else if (token == "nowrap") result.wrap = false;
    }
    return result;
}

inline css_flex_value specified_flex(std::string_view input)
{
    css_flex_value result;
    const auto lower = specified_ascii_lower(specified_trim(input));
    if (lower == "none") { result.none = true; result.shrink = 0.0F; return result; }
    const auto values = specified_components(input);
    if (!values.empty()) {
        if (specified_ascii_lower(values[0]) == "auto") result.grow = 1.0F;
        else if (const auto value = specified_number(values[0])) result.grow = std::max(0.0F, *value);
    }
    if (values.size() > 1U) {
        if (const auto value = specified_number(values[1])) result.shrink = std::max(0.0F, *value);
    }
    if (values.size() > 2U) {
        result.basis = native_document::parse_length(values[2]);
        result.basis_specified = true;
    }
    return result;
}

inline css_background_image_value specified_background_image(std::string_view input)
{
    css_background_image_value result;
    result.serialization = specified_trim(input);
    const auto lower = specified_ascii_lower(result.serialization);
    if (lower.empty() || lower == "none") return result;
    if (lower.starts_with("linear-gradient(") || lower.starts_with("radial-gradient(")) {
        result.kind = css_background_image_kind::gradient;
        return result;
    }
    const auto start = lower.find("url(");
    if (start != std::string::npos) {
        const auto end = result.serialization.find(')', start + 4U);
        if (end != std::string::npos) {
            result.kind = css_background_image_kind::url;
            result.url = specified_trim(std::string_view(result.serialization).substr(start + 4U, end - start - 4U));
            if (result.url.size() >= 2U && ((result.url.front() == '"' && result.url.back() == '"')
                || (result.url.front() == '\'' && result.url.back() == '\''))) {
                result.url = result.url.substr(1U, result.url.size() - 2U);
            }
            return result;
        }
    }
    result.kind = css_background_image_kind::other;
    return result;
}

inline css_shadow_value specified_shadow(std::string_view input)
{
    css_shadow_value result;
    const auto value = specified_trim(input);
    if (value.empty() || specified_ascii_lower(value) == "none") {
        result.valid = true;
        return result;
    }
    auto first_shadow = value;
    const auto comma = specified_components(value, ',');
    if (!comma.empty()) first_shadow = comma.front();
    result.multiple = comma.size() > 1U;
    for (const auto& token : specified_components(first_shadow)) {
        const auto lower = specified_ascii_lower(token);
        if (lower == "inset") { result.inset = true; continue; }
        if (lower == "currentcolor") {
            result.color = specified_color(token);
            result.color_specified = true;
            continue;
        }
        if (!token.empty() && (std::isdigit(static_cast<unsigned char>(token.front()))
            || token.front() == '.' || token.front() == '-' || token.front() == '+')) {
            if (result.length_count < result.lengths.size()) {
                result.lengths[result.length_count++] = native_document::parse_length(token);
            }
            continue;
        }
        const auto color = specified_color(token);
        if (color.valid) { result.color = color; result.color_specified = true; }
    }
    result.valid = result.length_count >= 2U;
    return result;
}

inline css_font_value specified_font(std::string_view input)
{
    css_font_value result;
    const auto tokens = specified_components(input);
    if (tokens.empty()) { result.complete = false; return result; }
    size_t size_index = tokens.size();
    const auto size_keyword = [](std::string_view token) {
        const auto lower = specified_ascii_lower(token);
        return lower == "xx-small" || lower == "x-small" || lower == "small"
            || lower == "medium" || lower == "large" || lower == "x-large"
            || lower == "xx-large" || lower == "smaller" || lower == "larger";
    };
    for (size_t index = 0; index < tokens.size(); ++index) {
        const auto token = specified_ascii_lower(tokens[index]);
        const auto numeric_weight = token.size() == 3U && token.front() >= '1' && token.front() <= '9'
            && token[1] == '0' && token[2] == '0';
        if (numeric_weight) continue;
        auto before_slash = token;
        if (const auto slash = before_slash.find('/'); slash != std::string::npos) before_slash.resize(slash);
        if (size_keyword(before_slash) || (!before_slash.empty()
            && (std::isdigit(static_cast<unsigned char>(before_slash.front())) || before_slash.front() == '.'))) {
            size_index = index;
            break;
        }
    }
    if (size_index == tokens.size()) { result.complete = false; return result; }
    for (size_t index = 0; index < size_index; ++index) {
        const auto token = specified_ascii_lower(tokens[index]);
        if (token == "bold" || token == "bolder") result.weight = 700;
        else if (token == "normal") {}
        else if (token.size() == 3U && token.front() >= '1' && token.front() <= '9'
            && token[1] == '0' && token[2] == '0') result.weight = std::atoi(token.c_str());
        else result.complete = false;
    }
    result.size_token = tokens[size_index];
    if (const auto slash = result.size_token.find('/'); slash != std::string::npos) {
        result.line_height_token = result.size_token.substr(slash + 1U);
        result.size_token.resize(slash);
    }
    auto family_index = size_index + 1U;
    if (result.line_height_token == "normal" && family_index < tokens.size() && tokens[family_index] == "/") {
        ++family_index;
        if (family_index < tokens.size()) result.line_height_token = tokens[family_index++];
    }
    for (size_t index = family_index; index < tokens.size(); ++index) {
        if (!result.family.empty()) result.family.push_back(' ');
        result.family += tokens[index];
    }
    if (result.family.empty()) result.complete = false;
    return result;
}

inline css_list_style_value specified_list_style(std::string_view input)
{
    css_list_style_value result;
    for (auto token : specified_components(input)) {
        token = specified_ascii_lower(token);
        if (token == "inside" || token == "outside") result.position = token;
        else if (token == "none" || token == "decimal" || token == "decimal-leading-zero"
            || token == "disc" || token == "circle" || token == "square") result.type = token;
    }
    return result;
}

inline css_content_value specified_content(std::string_view input)
{
    css_content_value result;
    auto value = specified_trim(input);
    const auto lower = specified_ascii_lower(value);
    if (lower == "none" || lower == "normal") return result;
    result.generated = true;
    if (value.size() >= 2U && (value.front() == '\'' || value.front() == '"') && value.back() == value.front()) {
        value = value.substr(1U, value.size() - 2U);
    }
    for (size_t index = 0; index < value.size(); ++index) {
        if (value[index] != '\\' || index + 1U >= value.size()) {
            result.decoded.push_back(value[index]);
            continue;
        }
        size_t end = index + 1U;
        while (end < value.size() && end - index <= 6U
            && std::isxdigit(static_cast<unsigned char>(value[end]))) ++end;
        if (end > index + 1U) {
            const auto codepoint = static_cast<uint32_t>(std::strtoul(
                value.substr(index + 1U, end - index - 1U).c_str(), nullptr, 16));
            if (codepoint <= 0x7FU) result.decoded.push_back(static_cast<char>(codepoint));
            else if (codepoint <= 0x7FFU) {
                result.decoded.push_back(static_cast<char>(0xC0U | (codepoint >> 6U)));
                result.decoded.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
            } else if (codepoint <= 0xFFFFU) {
                result.decoded.push_back(static_cast<char>(0xE0U | (codepoint >> 12U)));
                result.decoded.push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3FU)));
                result.decoded.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
            } else {
                result.decoded.push_back(static_cast<char>(0xF0U | (codepoint >> 18U)));
                result.decoded.push_back(static_cast<char>(0x80U | ((codepoint >> 12U) & 0x3FU)));
                result.decoded.push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3FU)));
                result.decoded.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
            }
            if (end < value.size() && std::isspace(static_cast<unsigned char>(value[end]))) ++end;
            index = end - 1U;
        } else {
            result.decoded.push_back(value[++index]);
        }
    }
    return result;
}

inline css_deferred_value specified_deferred(std::string_view input)
{
    css_deferred_value result;
    result.serialization = std::string(input);
    size_t cursor = 0U;
    while (cursor < input.size()) {
        const auto start = input.find("var(", cursor);
        if (start == std::string_view::npos) {
            if (cursor < input.size()) result.segments.push_back({css_deferred_segment::kind::literal,
                std::string(input.substr(cursor)), {}});
            break;
        }
        if (start > cursor) result.segments.push_back({css_deferred_segment::kind::literal,
            std::string(input.substr(cursor, start - cursor)), {}});
        auto depth = 1;
        size_t close = std::string_view::npos;
        size_t comma = std::string_view::npos;
        for (size_t index = start + 4U; index < input.size(); ++index) {
            if (input[index] == '(') ++depth;
            else if (input[index] == ')' && --depth == 0) { close = index; break; }
            else if (input[index] == ',' && depth == 1 && comma == std::string_view::npos) comma = index;
        }
        if (close == std::string_view::npos) {
            result.segments.push_back({css_deferred_segment::kind::literal,
                std::string(input.substr(start)), {}});
            break;
        }
        const auto name_end = comma == std::string_view::npos ? close : comma;
        css_deferred_segment segment;
        segment.type = css_deferred_segment::kind::variable;
        segment.text = specified_trim(input.substr(start + 4U, name_end - start - 4U));
        if (comma != std::string_view::npos) segment.fallback = specified_trim(input.substr(comma + 1U, close - comma - 1U));
        result.segments.push_back(std::move(segment));
        cursor = close + 1U;
    }
    return result;
}

inline specified_css_value compile_specified_value(css_property_id property, std::string_view input)
{
    specified_css_value result;
    const auto value = specified_trim(input);
    if (property == css_property_id::unknown) return result;
    if (property == css_property_id::custom) {
        result.kind = specified_css_kind::token_string;
        result.payload = css_token_string_value{std::string(input)};
        result.valid = true;
        return result;
    }
    if (value.find("var(") != std::string::npos) {
        result.kind = specified_css_kind::deferred;
        result.payload = specified_deferred(value);
        result.valid = true;
        return result;
    }
    result.wide = parse_css_wide_keyword(value);
    if (result.wide != css_wide_keyword::none) {
        result.kind = specified_css_kind::wide_keyword;
        result.valid = true;
        return result;
    }

    const auto set_keyword = [&] {
        result.kind = specified_css_kind::keyword;
        result.payload = css_keyword_value{specified_ascii_lower(value)};
        result.valid = true;
    };
    const auto set_token_string = [&] {
        result.kind = specified_css_kind::token_string;
        result.payload = css_token_string_value{value};
        result.valid = true;
    };
    const auto set_component_list = [&] {
        result.kind = specified_css_kind::component_list;
        result.payload = css_component_list_value{specified_components(value), value};
        result.valid = true;
    };
    const auto set_length = [&] {
        result.kind = specified_css_kind::length;
        result.payload = specified_length(value);
        result.valid = true;
    };
    const auto set_length_list = [&](size_t maximum) {
        result.kind = specified_css_kind::length_list;
        result.payload = specified_length_list(value, maximum);
        result.valid = std::get<css_length_list_value>(result.payload).count != 0U;
    };
    const auto set_color = [&] {
        result.kind = specified_css_kind::color;
        result.payload = specified_color(value);
        result.valid = std::get<css_color_value>(result.payload).valid;
    };

    switch (property) {
    case css_property_id::all:
    case css_property_id::display:
    case css_property_id::position:
    case css_property_id::contain:
    case css_property_id::container:
    case css_property_id::container_type:
    case css_property_id::content_visibility:
    case css_property_id::floating:
    case css_property_id::flex_direction:
    case css_property_id::flex_wrap:
    case css_property_id::align_items:
    case css_property_id::align_self:
    case css_property_id::justify_content:
    case css_property_id::box_sizing:
    case css_property_id::vertical_align:
    case css_property_id::grid_auto_flow:
    case css_property_id::border_collapse:
    case css_property_id::table_layout:
    case css_property_id::border_style:
    case css_property_id::background_repeat:
    case css_property_id::visibility:
    case css_property_id::pointer_events:
    case css_property_id::text_anchor:
    case css_property_id::cursor:
    case css_property_id::font_family:
    case css_property_id::font_smoothing:
    case css_property_id::text_align:
    case css_property_id::text_transform:
    case css_property_id::white_space:
    case css_property_id::list_style_position:
    case css_property_id::list_style_type:
    case css_property_id::scrollbar_width:
        set_keyword(); break;
    case css_property_id::content:
        result.kind = specified_css_kind::content; result.payload = specified_content(value); result.valid = true; break;
    case css_property_id::width:
    case css_property_id::height:
    case css_property_id::min_width:
    case css_property_id::min_height:
    case css_property_id::max_width:
    case css_property_id::max_height:
    case css_property_id::left:
    case css_property_id::top:
    case css_property_id::right:
    case css_property_id::bottom:
    case css_property_id::padding_left:
    case css_property_id::padding_right:
    case css_property_id::padding_top:
    case css_property_id::padding_bottom:
    case css_property_id::margin_left:
    case css_property_id::margin_right:
    case css_property_id::margin_top:
    case css_property_id::margin_bottom:
    case css_property_id::row_gap:
    case css_property_id::column_gap:
    case css_property_id::flex_basis:
    case css_property_id::border_top_width:
    case css_property_id::border_right_width:
    case css_property_id::border_bottom_width:
    case css_property_id::border_left_width:
    case css_property_id::border_inline_width:
    case css_property_id::border_block_width:
    case css_property_id::outline_width:
    case css_property_id::font_size:
    case css_property_id::letter_spacing:
    case css_property_id::word_spacing:
    case css_property_id::line_height:
    case css_property_id::stroke_width:
        set_length(); break;
    case css_property_id::inset:
    case css_property_id::padding:
    case css_property_id::margin:
    case css_property_id::border_radius:
        set_length_list(4U); break;
    case css_property_id::padding_inline:
    case css_property_id::padding_block:
    case css_property_id::margin_inline:
    case css_property_id::margin_block:
    case css_property_id::gap:
    case css_property_id::border_spacing:
    case css_property_id::border_top_left_radius:
    case css_property_id::border_top_right_radius:
    case css_property_id::border_bottom_right_radius:
    case css_property_id::border_bottom_left_radius:
        set_length_list(2U); break;
    case css_property_id::color:
    case css_property_id::background_color:
    case css_property_id::outline_color:
    case css_property_id::border_top_color:
    case css_property_id::border_right_color:
    case css_property_id::border_bottom_color:
    case css_property_id::border_left_color:
    case css_property_id::border_inline_color:
    case css_property_id::border_block_color:
        set_color(); break;
    case css_property_id::border_color: {
        result.kind = specified_css_kind::color_list;
        css_color_list_value colors;
        const auto parts = specified_components(value);
        colors.count = static_cast<uint8_t>(std::min<size_t>(parts.size(), 4U));
        result.valid = colors.count != 0U;
        for (size_t index = 0; index < colors.count; ++index) {
            colors.values[index] = specified_color(parts[index]);
            result.valid = result.valid && colors.values[index].valid;
        }
        result.payload = std::move(colors);
        break;
    }
    case css_property_id::border_width:
        set_length_list(4U); break;
    case css_property_id::border:
    case css_property_id::border_top:
    case css_property_id::border_right:
    case css_property_id::border_bottom:
    case css_property_id::border_left:
    case css_property_id::border_inline:
    case css_property_id::border_block:
    case css_property_id::outline:
        result.kind = specified_css_kind::border; result.payload = specified_border(value); result.valid = true; break;
    case css_property_id::transform: {
        css_transform_value transform;
        transform.none = specified_ascii_lower(value) == "none";
        native_document::parse_transform_translate(value, transform.translate_x, transform.translate_y,
            transform.scale_x, transform.scale_y, transform.rotate_degrees);
        result.kind = specified_css_kind::transform; result.payload = transform; result.valid = true; break;
    }
    case css_property_id::transform_origin: {
        css_transform_origin_value origin;
        native_document::parse_transform_origin(value, origin.x, origin.y);
        result.kind = specified_css_kind::transform_origin; result.payload = origin; result.valid = true; break;
    }
    case css_property_id::grid_template_columns:
    case css_property_id::grid_template_rows:
    case css_property_id::grid_auto_columns:
        result.kind = specified_css_kind::track_list; result.payload = specified_track_list(value); result.valid = true; break;
    case css_property_id::grid_area:
    case css_property_id::grid_row:
    case css_property_id::grid_row_start:
    case css_property_id::grid_row_end:
    case css_property_id::grid_column:
    case css_property_id::grid_column_start:
    case css_property_id::grid_column_end:
        result.kind = specified_css_kind::grid_placement;
        result.payload = css_grid_placement_value{specified_components(value, '/'), value};
        result.valid = true; break;
    case css_property_id::flex_flow:
        result.kind = specified_css_kind::flex_flow; result.payload = specified_flex_flow(value); result.valid = true; break;
    case css_property_id::flex_grow:
    case css_property_id::flex_shrink:
    case css_property_id::opacity: {
        auto number = specified_number(value);
        if (!number) break;
        result.kind = specified_css_kind::number; result.payload = css_number_value{*number, false}; result.valid = true; break;
    }
    case css_property_id::z_index: {
        if (specified_ascii_lower(value) == "auto") {
            result.kind = specified_css_kind::integer; result.payload = css_integer_value{0, true}; result.valid = true;
        } else {
            int32_t integer{};
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), integer);
            if (parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size()) {
                result.kind = specified_css_kind::integer; result.payload = css_integer_value{integer, false}; result.valid = true;
            }
        }
        break;
    }
    case css_property_id::flex:
        result.kind = specified_css_kind::flex; result.payload = specified_flex(value); result.valid = true; break;
    case css_property_id::overflow:
    case css_property_id::overflow_x:
    case css_property_id::overflow_y: {
        css_overflow_value overflow;
        const auto parts = specified_components(value);
        if (parts.empty()) break;
        overflow.x = specified_overflow_mode(parts[0]);
        overflow.y = specified_overflow_mode(parts.size() > 1U ? parts[1] : parts[0]);
        if (property == css_property_id::overflow_y) overflow.x = overflow.y;
        result.kind = specified_css_kind::overflow; result.payload = overflow; result.valid = true; break;
    }
    case css_property_id::background_image:
        result.kind = specified_css_kind::background_image; result.payload = specified_background_image(value); result.valid = true; break;
    case css_property_id::background: {
        const auto image = specified_background_image(value);
        if (image.kind != css_background_image_kind::none || specified_ascii_lower(value).find("none") != std::string::npos) {
            result.kind = specified_css_kind::background_image; result.payload = image; result.valid = true;
        } else {
            const auto color = specified_color(value);
            if (color.valid) { result.kind = specified_css_kind::color; result.payload = color; result.valid = true; }
            else set_component_list();
        }
        break;
    }
    case css_property_id::background_position:
    case css_property_id::background_size:
    case css_property_id::transition:
    case css_property_id::transition_property:
    case css_property_id::transition_duration:
    case css_property_id::transition_delay:
    case css_property_id::transition_timing_function:
    case css_property_id::animation:
    case css_property_id::animation_name:
    case css_property_id::animation_duration:
    case css_property_id::animation_delay:
    case css_property_id::animation_timing_function:
    case css_property_id::animation_iteration_count:
    case css_property_id::scrollbar_color:
    case css_property_id::container_name:
        set_component_list(); break;
    case css_property_id::contain_intrinsic_size:
        set_length(); break;
    case css_property_id::box_shadow:
        result.kind = specified_css_kind::shadow; result.payload = specified_shadow(value); result.valid = true; break;
    case css_property_id::font:
        result.kind = specified_css_kind::font; result.payload = specified_font(value); result.valid = true; break;
    case css_property_id::font_weight: {
        const auto lower = specified_ascii_lower(value);
        int32_t weight = lower == "bold" ? 700 : lower == "normal" ? 400 : 0;
        if (weight == 0) {
            int32_t integer{};
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), integer);
            if (parsed.ec != std::errc{}) break;
            weight = integer;
        }
        result.kind = specified_css_kind::integer; result.payload = css_integer_value{weight, false}; result.valid = true; break;
    }
    case css_property_id::fill:
    case css_property_id::stroke: {
        const auto color = specified_color(value);
        if (color.valid || specified_ascii_lower(value) == "none") {
            result.kind = specified_css_kind::color; result.payload = color; result.valid = true;
        } else set_token_string();
        break;
    }
    case css_property_id::list_style:
        result.kind = specified_css_kind::list_style; result.payload = specified_list_style(value); result.valid = true; break;
    case css_property_id::unknown:
    case css_property_id::custom:
        break;
    }
    return result;
}

inline specified_css_value compile_specified_value(std::string_view property_name, std::string_view input)
{
    return compile_specified_value(property_id(property_name), input);
}

} // namespace webscene_native::css
