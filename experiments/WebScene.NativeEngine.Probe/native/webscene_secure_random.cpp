#include "webscene_secure_random.h"

#include <algorithm>
#include <cerrno>
#include <limits>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#elif defined(__linux__) || defined(__APPLE__) || defined(__FreeBSD__)
#include <sys/random.h>
#if defined(__linux__)
#define WEBSCENE_USE_GETRANDOM 1
#endif
#elif defined(__OpenBSD__)
#include <unistd.h>
#else
#error "WebScene secure random requires an audited operating-system CSPRNG binding"
#endif

namespace webscene_native {

namespace {

secure_random_chunk_result operating_system_chunk(
    void*,
    std::span<std::uint8_t> output) noexcept
{
#if defined(_WIN32)
    const auto count = static_cast<ULONG>(output.size());
    if (!BCRYPT_SUCCESS(BCryptGenRandom(
            nullptr,
            reinterpret_cast<PUCHAR>(output.data()),
            count,
            BCRYPT_USE_SYSTEM_PREFERRED_RNG))) {
        return {secure_random_chunk_status::failure, 0U};
    }
    return {secure_random_chunk_status::success, output.size()};
#elif defined(WEBSCENE_USE_GETRANDOM)
    const auto received = getrandom(output.data(), output.size(), 0);
    if (received < 0) {
        return {
            errno == EINTR
                ? secure_random_chunk_status::interrupted
                : secure_random_chunk_status::failure,
            0U};
    }
    return {
        secure_random_chunk_status::success,
        static_cast<std::size_t>(received)};
#else
    if (getentropy(output.data(), output.size()) != 0) {
        return {secure_random_chunk_status::failure, 0U};
    }
    return {secure_random_chunk_status::success, output.size()};
#endif
}

}

bool fill_secure_random_from_source(
    std::span<std::uint8_t> output,
    std::size_t maximum_chunk_size,
    secure_random_chunk_source source,
    void* context) noexcept
{
    if (output.empty()) return true;
    if (maximum_chunk_size == 0U || source == nullptr) return false;
    while (!output.empty()) {
        const auto requested = output.first(
            std::min(output.size(), maximum_chunk_size));
        const auto result = source(context, requested);
        if (result.status == secure_random_chunk_status::interrupted) continue;
        if (result.status != secure_random_chunk_status::success
            || result.size == 0U
            || result.size > requested.size()) {
            return false;
        }
        output = output.subspan(result.size);
    }
    return true;
}

bool fill_secure_random(std::span<std::uint8_t> output) noexcept
{
#if defined(_WIN32)
    constexpr auto maximum_chunk = static_cast<std::size_t>(
        std::numeric_limits<ULONG>::max());
#elif defined(WEBSCENE_USE_GETRANDOM)
    constexpr auto maximum_chunk = std::numeric_limits<std::size_t>::max();
#else
    // getentropy() rejects requests larger than 256 bytes.
    constexpr std::size_t maximum_chunk = 256U;
#endif
    return fill_secure_random_from_source(
        output, maximum_chunk, operating_system_chunk, nullptr);
}

std::array<char, 36> format_uuid_v4(
    std::array<std::uint8_t, 16> bytes) noexcept
{
    bytes[6] = static_cast<std::uint8_t>((bytes[6] & 0x0fU) | 0x40U);
    bytes[8] = static_cast<std::uint8_t>((bytes[8] & 0x3fU) | 0x80U);
    constexpr char digits[] = "0123456789abcdef";
    std::array<char, 36> uuid{};
    std::size_t output = 0U;
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        if (index == 4U || index == 6U || index == 8U || index == 10U) {
            uuid[output++] = '-';
        }
        uuid[output++] = digits[bytes[index] >> 4U];
        uuid[output++] = digits[bytes[index] & 0x0fU];
    }
    return uuid;
}

}
