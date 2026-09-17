#pragma once

#include <cctype>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace webscene_native::css {
namespace detail {
inline bool css_hex_digit(unsigned char character)
{
    return (character >= '0' && character <= '9')
        || (character >= 'a' && character <= 'f')
        || (character >= 'A' && character <= 'F');
}

inline uint32_t css_hex_value(unsigned char character)
{
    if (character >= '0' && character <= '9') return character - '0';
    if (character >= 'a' && character <= 'f') return character - 'a' + 10U;
    return character - 'A' + 10U;
}

inline bool css_whitespace(unsigned char character)
{
    return character == ' ' || character == '\t' || character == '\n'
        || character == '\r' || character == '\f';
}

inline void append_utf8(std::string& result, uint32_t code_point)
{
    if (code_point == 0U || code_point > 0x10FFFFU
        || (code_point >= 0xD800U && code_point <= 0xDFFFU)) {
        code_point = 0xFFFDU;
    }
    if (code_point <= 0x7FU) {
        result.push_back(static_cast<char>(code_point));
    } else if (code_point <= 0x7FFU) {
        result.push_back(static_cast<char>(0xC0U | (code_point >> 6U)));
        result.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
    } else if (code_point <= 0xFFFFU) {
        result.push_back(static_cast<char>(0xE0U | (code_point >> 12U)));
        result.push_back(static_cast<char>(0x80U | ((code_point >> 6U) & 0x3FU)));
        result.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
    } else {
        result.push_back(static_cast<char>(0xF0U | (code_point >> 18U)));
        result.push_back(static_cast<char>(0x80U | ((code_point >> 12U) & 0x3FU)));
        result.push_back(static_cast<char>(0x80U | ((code_point >> 6U) & 0x3FU)));
        result.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
    }
}

inline bool consume_escape(
    std::string_view input,
    size_t& cursor,
    std::string& result)
{
    if (cursor >= input.size() || input[cursor] != '\\') return false;
    ++cursor;
    if (cursor >= input.size()) return false;
    const auto character = static_cast<unsigned char>(input[cursor]);
    if (character == '\n' || character == '\f') {
        ++cursor;
        return true;
    }
    if (character == '\r') {
        ++cursor;
        if (cursor < input.size() && input[cursor] == '\n') ++cursor;
        return true;
    }
    if (!css_hex_digit(character)) {
        result.push_back(static_cast<char>(character));
        ++cursor;
        return true;
    }

    uint32_t code_point = 0U;
    size_t digits = 0U;
    while (cursor < input.size() && digits < 6U
        && css_hex_digit(static_cast<unsigned char>(input[cursor]))) {
        code_point = code_point * 16U
            + css_hex_value(static_cast<unsigned char>(input[cursor]));
        ++cursor;
        ++digits;
    }
    if (cursor < input.size()
        && css_whitespace(static_cast<unsigned char>(input[cursor]))) {
        if (input[cursor] == '\r' && cursor + 1U < input.size()
            && input[cursor + 1U] == '\n') {
            ++cursor;
        }
        ++cursor;
    }
    append_utf8(result, code_point);
    return true;
}

inline void append_css_quoted(std::string& result, std::string_view value)
{
    result += "url(\"";
    for (const auto character : value) {
        if (character == '\\' || character == '"') result.push_back('\\');
        result.push_back(character);
    }
    result += "\")";
}
} // namespace detail

inline std::optional<std::string> decode_css_escapes(std::string_view input)
{
    std::string result;
    result.reserve(input.size());
    for (size_t cursor = 0U; cursor < input.size();) {
        if (input[cursor] != '\\') {
            result.push_back(input[cursor++]);
            continue;
        }
        if (!detail::consume_escape(input, cursor, result)) return std::nullopt;
    }
    return result;
}

inline std::string percent_encode_css_url(std::string_view input)
{
    static constexpr char hexadecimal[] = "0123456789ABCDEF";
    std::string result;
    result.reserve(input.size());
    for (const auto raw : input) {
        const auto character = static_cast<unsigned char>(raw);
        if (character > 0x20U && character < 0x7FU
            && character != '"' && character != '\\') {
            result.push_back(static_cast<char>(character));
            continue;
        }
        result.push_back('%');
        result.push_back(hexadecimal[character >> 4U]);
        result.push_back(hexadecimal[character & 0x0FU]);
    }
    return result;
}

struct css_url_token final {
    size_t begin{};
    size_t end{};
    std::string value;
};

inline std::optional<css_url_token> consume_css_url_token(
    std::string_view input,
    size_t function_begin)
{
    if (function_begin + 4U > input.size()) return std::nullopt;
    const auto matches = [&](size_t offset, char expected) {
        const auto value = static_cast<unsigned char>(input[function_begin + offset]);
        return static_cast<char>(std::tolower(value)) == expected;
    };
    if (!matches(0U, 'u') || !matches(1U, 'r') || !matches(2U, 'l')
        || input[function_begin + 3U] != '(') {
        return std::nullopt;
    }

    size_t cursor = function_begin + 4U;
    while (cursor < input.size()
        && detail::css_whitespace(static_cast<unsigned char>(input[cursor]))) {
        ++cursor;
    }
    const auto quote = cursor < input.size()
        && (input[cursor] == '\'' || input[cursor] == '"')
        ? input[cursor++] : '\0';
    std::string decoded;
    decoded.reserve(input.size() - cursor);
    auto closed_quote = quote == '\0';
    auto saw_unquoted_whitespace = false;
    for (; cursor < input.size();) {
        const auto character = input[cursor];
        if (character == '\\') {
            if (!detail::consume_escape(input, cursor, decoded)) return std::nullopt;
            continue;
        }
        if (quote != '\0') {
            if (character == quote) {
                ++cursor;
                closed_quote = true;
                while (cursor < input.size()
                    && detail::css_whitespace(static_cast<unsigned char>(input[cursor]))) {
                    ++cursor;
                }
                if (cursor < input.size() && input[cursor] == ')') {
                    return css_url_token{function_begin, cursor + 1U, std::move(decoded)};
                }
                return std::nullopt;
            }
            if (character == '\n' || character == '\r' || character == '\f') {
                return std::nullopt;
            }
            decoded.push_back(character);
            ++cursor;
            continue;
        }
        if (character == ')') {
            while (!decoded.empty()
                && detail::css_whitespace(static_cast<unsigned char>(decoded.back()))) {
                decoded.pop_back();
            }
            return css_url_token{function_begin, cursor + 1U, std::move(decoded)};
        }
        if (character == '"' || character == '\'' || character == '(') {
            return std::nullopt;
        }
        if (detail::css_whitespace(static_cast<unsigned char>(character))) {
            saw_unquoted_whitespace = true;
            decoded.push_back(character);
            ++cursor;
            continue;
        }
        if (saw_unquoted_whitespace) return std::nullopt;
        decoded.push_back(character);
        ++cursor;
    }

    // CSS recovers an unterminated quoted URL at EOF by closing the string and
    // function. Preserve a trailing ')' as string data when it occurred after
    // the opening quote, matching browser CSSOM serialization.
    if (quote != '\0' && !closed_quote) {
        return css_url_token{function_begin, input.size(), std::move(decoded)};
    }
    return std::nullopt;
}

inline std::optional<std::string> serialize_css_string_and_url_tokens(
    std::string_view input)
{
    std::string result;
    result.reserve(input.size());
    for (size_t cursor = 0U; cursor < input.size();) {
        if (cursor + 4U <= input.size()) {
            const auto token = consume_css_url_token(input, cursor);
            if (token.has_value()) {
                detail::append_css_quoted(result, token->value);
                cursor = token->end;
                continue;
            }
            const auto lower = [](char value) {
                return static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
            };
            if (lower(input[cursor]) == 'u' && lower(input[cursor + 1U]) == 'r'
                && lower(input[cursor + 2U]) == 'l' && input[cursor + 3U] == '(') {
                return std::nullopt;
            }
        }
        const auto quote = input[cursor];
        if (quote != '\'' && quote != '"') {
            result.push_back(input[cursor++]);
            continue;
        }
        result.push_back(quote);
        ++cursor;
        auto closed = false;
        while (cursor < input.size()) {
            if (input[cursor] == quote) {
                result.push_back(quote);
                ++cursor;
                closed = true;
                break;
            }
            if (input[cursor] == '\n' || input[cursor] == '\r' || input[cursor] == '\f') {
                return std::nullopt;
            }
            if (input[cursor] != '\\') {
                result.push_back(input[cursor++]);
                continue;
            }
            std::string decoded;
            if (!detail::consume_escape(input, cursor, decoded)) return std::nullopt;
            for (const auto character : decoded) {
                if (character == quote || character == '\\') result.push_back('\\');
                result.push_back(character);
            }
        }
        if (!closed) return std::nullopt;
    }
    return result;
}

inline std::optional<std::string> first_decoded_css_url(std::string_view value)
{
    for (size_t cursor = 0U; cursor + 4U <= value.size(); ++cursor) {
        if (const auto token = consume_css_url_token(value, cursor); token.has_value()) {
            if (!token->value.empty()) {
                const auto colon = token->value.find(':');
                if (colon != std::string::npos) {
                    auto scheme = token->value.substr(0U, colon);
                    for (auto& character : scheme) {
                        character = static_cast<char>(std::tolower(
                            static_cast<unsigned char>(character)));
                    }
                    if (scheme == "javascript" || scheme == "vbscript") {
                        return std::nullopt;
                    }
                }
                return token->value;
            }
            return std::nullopt;
        }
    }
    return std::nullopt;
}
} // namespace webscene_native::css
