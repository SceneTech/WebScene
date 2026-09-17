#pragma once

#include "webscene_native_dom.h"

namespace webscene_native::css {
inline constexpr std::string_view container_query_prefix =
    "--webscene-container-query:";

inline bool is_container_query(std::string_view query) noexcept
{
    return query.starts_with(container_query_prefix);
}

inline std::string encode_container_query(std::string_view query)
{
    return std::string(container_query_prefix) + std::string(query);
}

inline std::string_view container_query_text(std::string_view query) noexcept
{
    return is_container_query(query)
        ? query.substr(container_query_prefix.size())
        : std::string_view{};
}

inline std::string trim_container_token(std::string_view value)
{
    const auto first = value.find_first_not_of(" \t\r\n\f");
    if (first == std::string_view::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n\f");
    auto result = std::string(value.substr(first, last - first + 1U));
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return result;
}

inline bool container_has_name(const dom_node& container, std::string_view name)
{
    if (name.empty()) return true;
    std::istringstream names(container.style.textual().container_name);
    for (std::string candidate; names >> candidate;) {
        if (candidate == name) return true;
    }
    return false;
}

inline bool is_query_container(const dom_node& node, bool block_axis = false)
{
    const auto& type = node.style.textual().container_type;
    return type == "size" || (!block_axis && type == "inline-size");
}

inline const dom_node* nearest_query_container(
    const native_document& document,
    const dom_node& subject,
    std::string_view name = {},
    bool block_axis = false)
{
    for (auto* ancestor = document.composed_parent(subject); ancestor != nullptr;
         ancestor = document.composed_parent(*ancestor)) {
        if (is_query_container(*ancestor, block_axis)
            && container_has_name(*ancestor, name)) {
            return ancestor;
        }
    }
    return nullptr;
}

inline float query_container_size(const dom_node& container, bool block_axis)
{
    const auto authored = block_axis ? container.style.height : container.style.width;
    if (authored.unit == length_unit::pixels) return std::max(0.0F, authored.value);
    return std::max(0.0F, block_axis ? container.layout.height : container.layout.width);
}

inline std::optional<float> parse_container_query_length(std::string_view value)
{
    auto token = trim_container_token(value);
    if (token.empty()) return std::nullopt;
    auto multiplier = 1.0F;
    if (token.ends_with("px")) token.resize(token.size() - 2U);
    else if (token.ends_with("rem")) {
        token.resize(token.size() - 3U);
        multiplier = 14.0F;
    } else if (token.ends_with("em")) {
        token.resize(token.size() - 2U);
        multiplier = 14.0F;
    } else if (token != "0") return std::nullopt;
    char* end = nullptr;
    const auto parsed = std::strtof(token.c_str(), &end);
    if (end == token.c_str() || *end != '\0' || !std::isfinite(parsed)) return std::nullopt;
    return parsed * multiplier;
}

inline bool compare_container_size(float actual, std::string_view op, float expected)
{
    if (op == ">") return actual > expected;
    if (op == ">=") return actual >= expected;
    if (op == "<") return actual < expected;
    if (op == "<=") return actual <= expected;
    return std::abs(actual - expected) < 0.01F;
}

inline bool evaluate_container_feature(
    const dom_node& container,
    std::string_view raw_condition)
{
    auto condition = trim_container_token(raw_condition);
    while (condition.size() >= 2U && condition.front() == '(' && condition.back() == ')') {
        condition = trim_container_token(
            std::string_view(condition).substr(1U, condition.size() - 2U));
    }
    auto block_axis = false;
    auto feature = std::string_view{"width"};
    if (condition.starts_with("min-inline-size") || condition.starts_with("max-inline-size")) {
        feature = "inline-size";
    } else if (condition.starts_with("min-block-size") || condition.starts_with("max-block-size")
        || condition.starts_with("block-size") || condition.starts_with("height")) {
        feature = "block-size";
        block_axis = true;
    }
    const auto actual = query_container_size(container, block_axis);
    if (const auto colon = condition.find(':'); colon != std::string::npos) {
        auto name = trim_container_token(std::string_view(condition).substr(0U, colon));
        const auto expected = parse_container_query_length(
            std::string_view(condition).substr(colon + 1U));
        if (!expected.has_value()) return false;
        if (name.starts_with("min-")) return actual >= *expected;
        if (name.starts_with("max-")) return actual <= *expected;
        return std::abs(actual - *expected) < 0.01F;
    }
    for (const auto op : {std::string_view{">="}, std::string_view{"<="},
                          std::string_view{">"}, std::string_view{"<"},
                          std::string_view{"="}}) {
        const auto position = condition.find(op);
        if (position == std::string::npos) continue;
        const auto left = trim_container_token(
            std::string_view(condition).substr(0U, position));
        const auto right = trim_container_token(
            std::string_view(condition).substr(position + op.size()));
        const auto left_is_feature = left == feature || left == "width"
            || left == "inline-size" || left == "height" || left == "block-size";
        const auto expected = parse_container_query_length(left_is_feature ? right : left);
        if (!expected.has_value()) return false;
        if (left_is_feature) return compare_container_size(actual, op, *expected);
        const auto reverse = op == ">" ? "<" : op == ">=" ? "<="
            : op == "<" ? ">" : op == "<=" ? ">=" : "=";
        return compare_container_size(actual, reverse, *expected);
    }
    return false;
}

inline bool container_query_matches(
    const native_document& document,
    const dom_node& subject,
    std::string_view encoded)
{
    auto query = trim_container_token(container_query_text(encoded));
    const auto open = query.find('(');
    if (open == std::string::npos) return false;
    const auto name = trim_container_token(std::string_view(query).substr(0U, open));
    const auto block_axis = query.find("height", open) != std::string::npos
        || query.find("block-size", open) != std::string::npos;
    const auto* container = nearest_query_container(document, subject, name, block_axis);
    if (container == nullptr) return false;
    auto conditions = std::string_view(query).substr(open);
    size_t start = 0U;
    while (start < conditions.size()) {
        auto end = conditions.find(" and ", start);
        if (end == std::string_view::npos) end = conditions.size();
        if (!evaluate_container_feature(*container, conditions.substr(start, end - start))) {
            return false;
        }
        start = end + 5U;
    }
    return true;
}
} // namespace webscene_native::css
