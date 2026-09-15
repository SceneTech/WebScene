#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace webscene_native {

inline constexpr std::size_t web_crypto_random_quota = 65'536U;

// Fills the complete span from the operating-system CSPRNG. A failure never
// falls back to a userspace pseudo-random generator.
[[nodiscard]] bool fill_secure_random(std::span<std::uint8_t> output) noexcept;

}
