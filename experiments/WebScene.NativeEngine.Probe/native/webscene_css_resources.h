#pragma once
#include "webscene_css_escapes.h"
#include "webscene_css_matching.h"
#include "webscene_native_resource_url.h"

namespace webscene_native::css {
inline std::string resolve_resource_urls(
        std::string value,
        const std::string& stylesheet_address)
    {
        if (value.empty() || stylesheet_address.empty()) return value;

        const auto url_at = [&](size_t offset) {
            if (offset + 4U > value.size() || value[offset + 3U] != '(') return false;
            const auto lower = [](char character) {
                return static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
            };
            return lower(value[offset]) == 'u'
                && lower(value[offset + 1U]) == 'r'
                && lower(value[offset + 2U]) == 'l';
        };
        std::string result;
        result.reserve(value.size());
        size_t copied = 0U;
        for (size_t search = 0U; search + 4U <= value.size();) {
            if (!url_at(search)) {
                ++search;
                continue;
            }
            const auto token = consume_css_url_token(value, search);
            if (!token.has_value()) {
                search += 4U;
                continue;
            }
            result.append(value, copied, search - copied);
            if (token->value.empty()) {
                result.append(value, search, token->end - search);
            } else {
                const auto resolved = percent_encode_css_url(
                    token->value.starts_with('#')
                        ? token->value
                        : resources::resolve_url(token->value, stylesheet_address));
                detail::append_css_quoted(result, resolved);
            }
            copied = token->end;
            search = token->end;
        }
        if (copied == 0U) return value;
        result.append(value, copied, value.size() - copied);
        return result;
    }

} // namespace webscene_native::css
