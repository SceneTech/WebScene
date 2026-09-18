#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace webscene_native::css {

inline std::string ascii_lower(std::string_view value)
{
    auto result = std::string(value);
    for (auto& character : result) {
        if (character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character + ('a' - 'A'));
        }
    }
    return result;
}

inline bool is_effect_property(std::string_view name) noexcept
{
    return name == "mask-image" || name == "mask-size" || name == "mask-position"
        || name == "mask-repeat" || name == "mask-composite" || name == "mask-mode"
        || name == "clip-path"
        || name == "filter" || name == "backdrop-filter";
}

inline std::string_view effect_initial_value(std::string_view name) noexcept
{
    if (name == "mask-size") return "auto";
    if (name == "mask-position") return "0% 0%";
    if (name == "mask-repeat") return "repeat";
    if (name == "mask-composite") return "add";
    if (name == "mask-mode") return "match-source";
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

struct parsed_mask_shorthand final {
    std::string image{"none"};
    std::string position{"0% 0%"};
    std::string size{"auto"};
    std::string repeat{"repeat"};
    std::string composite{"add"};
    std::string mode{"match-source"};
};

inline std::optional<parsed_mask_shorthand> parse_single_mask_shorthand(
    std::string_view authored)
{
    const auto trim = [](std::string_view value) {
        while (!value.empty()
            && std::isspace(static_cast<unsigned char>(value.front()))) {
            value.remove_prefix(1U);
        }
        while (!value.empty()
            && std::isspace(static_cast<unsigned char>(value.back()))) {
            value.remove_suffix(1U);
        }
        return value;
    };
    authored = trim(authored);
    if (authored.empty()) return std::nullopt;
    const auto lowered = ascii_lower(authored);
    if (lowered.starts_with("var(") && lowered.ends_with(')')) {
        auto depth = 0;
        for (size_t index = 0U; index < lowered.size(); ++index) {
            if (lowered[index] == '(') ++depth;
            else if (lowered[index] == ')') {
                if (depth == 0) return std::nullopt;
                --depth;
                if (depth == 0 && index != lowered.size() - 1U) {
                    depth = 1;
                    break;
                }
            }
        }
        if (depth == 0) return std::nullopt;
    }
    if (lowered == "initial" || lowered == "inherit" || lowered == "unset"
        || lowered == "revert" || lowered == "revert-layer") {
        parsed_mask_shorthand result;
        result.image = result.position = result.size = result.repeat =
            result.composite = result.mode = lowered;
        return result;
    }
    if (lowered == "none") return parsed_mask_shorthand{};

    std::vector<std::string> tokens;
    auto start = std::string_view::npos;
    auto depth = 0U;
    char quote = 0;
    for (size_t index = 0U; index <= authored.size(); ++index) {
        const auto at_end = index == authored.size();
        const auto character = at_end ? ' ' : authored[index];
        if (quote != 0) {
            if (!at_end && character == '\\' && index + 1U < authored.size()) ++index;
            else if (!at_end && character == quote) quote = 0;
        } else if (!at_end && (character == '\'' || character == '"')) {
            quote = character;
        } else if (!at_end && character == '(') {
            ++depth;
        } else if (!at_end && character == ')' && depth != 0U) {
            --depth;
        } else if (!at_end && character == ',' && depth == 0U) {
            return std::nullopt;
        }
        const auto separator = depth == 0U && quote == 0
            && (at_end || std::isspace(static_cast<unsigned char>(character))
                || character == '/');
        if (!separator && start == std::string_view::npos) start = index;
        if (separator && start != std::string_view::npos) {
            tokens.emplace_back(authored.substr(start, index - start));
            start = std::string_view::npos;
        }
        if (!at_end && depth == 0U && quote == 0 && character == '/') {
            tokens.emplace_back("/");
        }
    }
    if (depth != 0U || quote != 0 || tokens.empty()) return std::nullopt;

    parsed_mask_shorthand result;
    std::vector<std::string> position;
    std::vector<std::string> size;
    auto after_slash = false;
    auto has_image = false;
    for (const auto& token : tokens) {
        const auto lower = ascii_lower(token);
        if (token == "/") {
            if (after_slash || position.empty()) return std::nullopt;
            after_slash = true;
            continue;
        }
        if (lower.starts_with("url(")
                || lower.starts_with("linear-gradient(")
                || lower.starts_with("radial-gradient(")
                || lower.starts_with("repeating-linear-gradient(")
                || lower.starts_with("repeating-radial-gradient(")) {
            if (has_image || after_slash) return std::nullopt;
            result.image = token;
            has_image = true;
            continue;
        }
        if (lower == "none") {
            if (has_image || after_slash) return std::nullopt;
            result.image = lower;
            has_image = true;
            continue;
        }
        if (lower == "no-repeat" || lower == "repeat" || lower == "repeat-x"
            || lower == "repeat-y") {
            if (after_slash || result.repeat != "repeat") return std::nullopt;
            result.repeat = lower;
            continue;
        }
        if (lower == "add" || lower == "exclude" || lower == "intersect"
            || lower == "subtract") {
            if (after_slash || result.composite != "add") return std::nullopt;
            result.composite = lower;
            continue;
        }
        if (lower == "alpha" || lower == "luminance" || lower == "match-source") {
            if (after_slash || result.mode != "match-source") return std::nullopt;
            result.mode = lower;
            continue;
        }
        if (lower == "border-box" || lower == "padding-box"
            || lower == "content-box" || lower == "fill-box"
            || lower == "stroke-box" || lower == "view-box" || lower == "no-clip") {
            return std::nullopt;
        }
        const auto numeric = !lower.empty()
            && (std::isdigit(static_cast<unsigned char>(lower.front()))
                || lower.front() == '.' || lower.front() == '-'
                || lower.front() == '+');
        const auto functional_length = lower.starts_with("var(")
            || lower.starts_with("calc(") || lower.starts_with("min(")
            || lower.starts_with("max(") || lower.starts_with("clamp(");
        const auto position_keyword = lower == "left" || lower == "right"
            || lower == "top" || lower == "bottom" || lower == "center";
        const auto size_keyword = lower == "auto" || lower == "contain"
            || lower == "cover";
        if (after_slash
                ? !numeric && !functional_length && !size_keyword
                : !numeric && !functional_length && !position_keyword) {
            return std::nullopt;
        }
        auto& geometry = after_slash ? size : position;
        if (geometry.size() == 2U) return std::nullopt;
        geometry.push_back(token);
    }
    if (after_slash && size.empty()) return std::nullopt;
    if (!position.empty()) {
        result.position = position.front();
        if (position.size() > 1U) result.position += " " + position[1];
    }
    if (!size.empty()) {
        result.size = size.front();
        if (size.size() > 1U) result.size += " " + size[1];
    }
    return result;
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
    if (normalized == "inherit") return normalized;
    if (normalized == "none") {
        if (name == "mask-mode") return std::nullopt;
        return normalized;
    }

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
    if (name == "mask-mode") {
        return valid_effect_keyword_list(normalized, {"alpha", "luminance", "match-source"})
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
