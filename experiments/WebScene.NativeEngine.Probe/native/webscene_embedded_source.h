#pragma once

#include <array>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace webscene_native {

// MSVC rejects a single string literal above 16,380 bytes. Keep enough
// headroom for source encoding while allowing larger programs to be assembled
// once, in declaration order, without separators.
inline constexpr std::size_t maximum_portable_embedded_source_literal_bytes =
    15'000U;

template<std::size_t Count>
constexpr bool embedded_source_parts_are_portable(
    const std::array<std::string_view, Count>& parts) noexcept
{
    for (const auto part : parts) {
        if (part.size() > maximum_portable_embedded_source_literal_bytes) {
            return false;
        }
    }
    return true;
}

template<std::size_t Count>
std::string join_embedded_source_parts(
    const std::array<std::string_view, Count>& parts)
{
    std::size_t source_size = 0U;
    for (const auto part : parts) {
        if (part.size() > std::numeric_limits<std::size_t>::max() - source_size) {
            throw std::length_error("embedded source size overflow");
        }
        source_size += part.size();
    }

    std::string source;
    source.reserve(source_size);
    for (const auto part : parts) source.append(part);
    return source;
}

} // namespace webscene_native
