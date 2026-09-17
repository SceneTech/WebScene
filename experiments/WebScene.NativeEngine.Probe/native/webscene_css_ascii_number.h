#pragma once

#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <string_view>

namespace webscene_native::css {

struct ascii_number_prefix final {
    float value{};
    size_t consumed{};
};

// Parse the locale-independent CSS <number> prefix needed by retained paint.
// Returning the consumed prefix lets callers validate their property-specific
// unit without copying or reading past the supplied string_view.
inline std::optional<ascii_number_prefix> parse_ascii_number_prefix(
    std::string_view input) noexcept
{
    if (input.empty()) return std::nullopt;
    size_t cursor = 0U;
    auto negative = false;
    if (input[cursor] == '+' || input[cursor] == '-') {
        negative = input[cursor] == '-';
        if (++cursor == input.size()) return std::nullopt;
    }

    long double significand = 0.0L;
    auto fractional_digits = 0;
    auto digits = 0U;
    auto significand_overflow = false;
    const auto append_digit = [&](unsigned digit) {
        ++digits;
        if (significand
            > (std::numeric_limits<long double>::max() - digit) / 10.0L) {
            significand_overflow = true;
            return;
        }
        significand = significand * 10.0L + static_cast<long double>(digit);
    };

    while (cursor < input.size() && input[cursor] >= '0' && input[cursor] <= '9') {
        append_digit(static_cast<unsigned>(input[cursor] - '0'));
        ++cursor;
    }
    if (cursor < input.size() && input[cursor] == '.') {
        ++cursor;
        while (cursor < input.size() && input[cursor] >= '0' && input[cursor] <= '9') {
            append_digit(static_cast<unsigned>(input[cursor] - '0'));
            if (fractional_digits < 100000) ++fractional_digits;
            else significand_overflow = true;
            ++cursor;
        }
    }
    if (digits == 0U || significand_overflow) return std::nullopt;

    auto exponent = 0;
    auto exponent_negative = false;
    auto exponent_overflow = false;
    if (cursor < input.size() && (input[cursor] == 'e' || input[cursor] == 'E')) {
        ++cursor;
        if (cursor < input.size() && (input[cursor] == '+' || input[cursor] == '-')) {
            exponent_negative = input[cursor] == '-';
            ++cursor;
        }
        const auto exponent_start = cursor;
        while (cursor < input.size() && input[cursor] >= '0' && input[cursor] <= '9') {
            const auto digit = static_cast<int>(input[cursor] - '0');
            if (exponent > 10000) exponent_overflow = true;
            else exponent = exponent * 10 + digit;
            ++cursor;
        }
        if (cursor == exponent_start) return std::nullopt;
    }
    if (significand == 0.0L) {
        return ascii_number_prefix{negative ? -0.0F : 0.0F, cursor};
    }
    if (exponent_overflow) return std::nullopt;
    if (exponent_negative) exponent = -exponent;

    const auto decimal_exponent = exponent - fractional_digits;
    if (decimal_exponent > 10000 || decimal_exponent < -10000) return std::nullopt;
    auto scaled = significand * std::pow(10.0L, static_cast<long double>(decimal_exponent));
    if (negative) scaled = -scaled;
    if (!std::isfinite(scaled)
        || std::abs(scaled) > std::numeric_limits<float>::max()) return std::nullopt;
    const auto result = static_cast<float>(scaled);
    if (!std::isfinite(result) || (result == 0.0F && significand != 0.0L)) {
        return std::nullopt;
    }
    return ascii_number_prefix{result, cursor};
}

struct ascii_inset_length final {
    float value{};
    bool percent{};
};

inline std::optional<ascii_inset_length> parse_ascii_inset_length(
    std::string_view token) noexcept
{
    const auto parsed = parse_ascii_number_prefix(token);
    if (!parsed) return std::nullopt;
    const auto suffix = token.substr(parsed->consumed);
    if (suffix.empty()) {
        if (std::abs(parsed->value) > 0.0001F) return std::nullopt;
        return ascii_inset_length{0.0F, false};
    }
    if (suffix.size() == 2U
        && (suffix[0] == 'p' || suffix[0] == 'P')
        && (suffix[1] == 'x' || suffix[1] == 'X')) {
        return ascii_inset_length{parsed->value, false};
    }
    if (suffix == "%") return ascii_inset_length{parsed->value, true};
    return std::nullopt;
}

} // namespace webscene_native::css
