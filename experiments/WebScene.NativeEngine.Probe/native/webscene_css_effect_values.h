#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>

namespace webscene_native::css {

inline bool is_effect_property(std::string_view name) noexcept
{
    return name == "mask-image" || name == "mask-size" || name == "mask-position"
        || name == "mask-repeat" || name == "mask-composite" || name == "clip-path"
        || name == "filter" || name == "backdrop-filter";
}

inline std::string_view effect_initial_value(std::string_view name) noexcept
{
    if (name == "mask-size") return "auto";
    if (name == "mask-position") return "0% 0%";
    if (name == "mask-repeat") return "repeat";
    if (name == "mask-composite") return "add";
    return "none";
}

inline std::string trim_effect_value(std::string_view value)
{
    const auto first = value.find_first_not_of(" \t\r\n\f\v");
    if (first == std::string_view::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n\f\v");
    return std::string(value.substr(first, last - first + 1U));
}

inline bool balanced_effect_functions(std::string_view value) noexcept
{
    auto depth = 0;
    char quote = 0;
    for (size_t index = 0; index < value.size(); ++index) {
        const auto character = value[index];
        if (quote != 0) {
            if (character == '\\') ++index;
            else if (character == quote) quote = 0;
            continue;
        }
        if (character == '\'' || character == '"') quote = character;
        else if (character == '(') ++depth;
        else if (character == ')' && --depth < 0) return false;
    }
    return depth == 0 && quote == 0;
}

inline bool contains_only_named_functions(
    std::string_view value,
    std::initializer_list<std::string_view> allowed)
{
    auto cursor = size_t{0};
    auto found = false;
    while (cursor < value.size()) {
        while (cursor < value.size()
            && (std::isspace(static_cast<unsigned char>(value[cursor]))
                || value[cursor] == ',')) ++cursor;
        if (cursor == value.size()) break;
        const auto name_start = cursor;
        while (cursor < value.size()
            && (std::isalnum(static_cast<unsigned char>(value[cursor]))
                || value[cursor] == '-')) ++cursor;
        const auto name = value.substr(name_start, cursor - name_start);
        if (name.empty()
            || std::find(allowed.begin(), allowed.end(), name) == allowed.end()) return false;
        while (cursor < value.size()
            && std::isspace(static_cast<unsigned char>(value[cursor]))) ++cursor;
        if (cursor == value.size() || value[cursor] != '(') return false;
        auto depth = 1;
        char quote = 0;
        for (++cursor; cursor < value.size() && depth > 0; ++cursor) {
            const auto character = value[cursor];
            if (quote != 0) {
                if (character == '\\') ++cursor;
                else if (character == quote) quote = 0;
            } else if (character == '\'' || character == '"') quote = character;
            else if (character == '(') ++depth;
            else if (character == ')') --depth;
        }
        if (depth != 0 || quote != 0) return false;
        found = true;
    }
    return found;
}

inline bool valid_effect_keyword_list(
    std::string_view value,
    std::initializer_list<std::string_view> allowed)
{
    auto cursor = size_t{0};
    auto found = false;
    while (cursor < value.size()) {
        while (cursor < value.size()
            && (std::isspace(static_cast<unsigned char>(value[cursor]))
                || value[cursor] == ',')) ++cursor;
        if (cursor == value.size()) break;
        const auto start = cursor;
        while (cursor < value.size()
            && !std::isspace(static_cast<unsigned char>(value[cursor]))
            && value[cursor] != ',') ++cursor;
        const auto token = value.substr(start, cursor - start);
        if (std::find(allowed.begin(), allowed.end(), token) == allowed.end()) return false;
        found = true;
    }
    return found;
}

inline std::optional<std::string> normalize_effect_value(
    std::string_view name,
    std::string_view input)
{
    if (!is_effect_property(name)) return std::nullopt;
    const auto value = trim_effect_value(input);
    if (value.empty() || !balanced_effect_functions(value)) return std::nullopt;
    auto normalized = value;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    if (normalized == "initial" || normalized == "unset" || normalized == "revert"
        || normalized == "revert-layer") return std::string(effect_initial_value(name));
    if (normalized == "inherit" || normalized == "none") return normalized;

    if (name == "filter" || name == "backdrop-filter") {
        return contains_only_named_functions(normalized, {
            "blur", "brightness", "contrast", "drop-shadow", "grayscale",
            "hue-rotate", "invert", "opacity", "saturate", "sepia", "url"})
            ? std::optional<std::string>(value) : std::nullopt;
    }
    if (name == "clip-path") {
        return contains_only_named_functions(normalized, {
            "circle", "ellipse", "inset", "path", "polygon", "rect", "url", "xywh"})
            ? std::optional<std::string>(value) : std::nullopt;
    }
    if (name == "mask-image") {
        return contains_only_named_functions(normalized, {
            "image", "image-set", "linear-gradient", "radial-gradient",
            "repeating-linear-gradient", "repeating-radial-gradient", "url"})
            ? std::optional<std::string>(value) : std::nullopt;
    }
    if (name == "mask-repeat") {
        return valid_effect_keyword_list(normalized, {
            "no-repeat", "repeat", "repeat-x", "repeat-y", "round", "space"})
            ? std::optional<std::string>(value) : std::nullopt;
    }
    if (name == "mask-composite") {
        return valid_effect_keyword_list(normalized, {"add", "exclude", "intersect", "subtract"})
            ? std::optional<std::string>(normalized) : std::nullopt;
    }
    if (name == "mask-size") {
        if (normalized.find(';') != std::string::npos) return std::nullopt;
        return normalized.find_first_of("0123456789") != std::string::npos
            || valid_effect_keyword_list(normalized, {"auto", "contain", "cover"})
            ? std::optional<std::string>(value) : std::nullopt;
    }
    if (name == "mask-position") {
        if (normalized.find(';') != std::string::npos) return std::nullopt;
        return normalized.find_first_of("0123456789") != std::string::npos
            || valid_effect_keyword_list(normalized, {"bottom", "center", "left", "right", "top"})
            ? std::optional<std::string>(value) : std::nullopt;
    }
    return std::nullopt;
}

} // namespace webscene_native::css
