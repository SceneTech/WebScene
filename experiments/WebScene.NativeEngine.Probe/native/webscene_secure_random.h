#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace webscene_native {

inline constexpr std::size_t web_crypto_random_quota = 65'536U;

enum class secure_random_chunk_status : std::uint8_t {
    success,
    interrupted,
    failure,
};

struct secure_random_chunk_result final {
    secure_random_chunk_status status{secure_random_chunk_status::failure};
    std::size_t size{0U};
};

using secure_random_chunk_source = secure_random_chunk_result (*)(
    void* context,
    std::span<std::uint8_t> output) noexcept;

// Drives a bounded chunk source until the complete span is filled. This is
// exposed inside the native component so failure, interruption, and partial
// reads can be tested without weakening the production operating-system path.
[[nodiscard]] bool fill_secure_random_from_source(
    std::span<std::uint8_t> output,
    std::size_t maximum_chunk_size,
    secure_random_chunk_source source,
    void* context) noexcept;

// Fills the complete span from the operating-system CSPRNG. A failure never
// falls back to a userspace pseudo-random generator.
[[nodiscard]] bool fill_secure_random(std::span<std::uint8_t> output) noexcept;

// Applies the UUIDv4 version and RFC variant bits and emits the canonical
// lower-case representation.
[[nodiscard]] std::array<char, 36> format_uuid_v4(
    std::array<std::uint8_t, 16> bytes) noexcept;

}
