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

bool fill_secure_random(std::span<std::uint8_t> output) noexcept
{
#if defined(_WIN32)
    while (!output.empty()) {
        const auto count = static_cast<ULONG>(std::min<std::size_t>(
            output.size(), std::numeric_limits<ULONG>::max()));
        if (!BCRYPT_SUCCESS(BCryptGenRandom(
                nullptr,
                reinterpret_cast<PUCHAR>(output.data()),
                count,
                BCRYPT_USE_SYSTEM_PREFERRED_RNG))) {
            return false;
        }
        output = output.subspan(count);
    }
    return true;
#elif defined(WEBSCENE_USE_GETRANDOM)
    while (!output.empty()) {
        const auto received = getrandom(output.data(), output.size(), 0);
        if (received < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (received == 0) return false;
        output = output.subspan(static_cast<std::size_t>(received));
    }
    return true;
#else
    // getentropy() rejects requests larger than 256 bytes on these platforms.
    while (!output.empty()) {
        const auto count = std::min<std::size_t>(output.size(), 256U);
        if (getentropy(output.data(), count) != 0) return false;
        output = output.subspan(count);
    }
    return true;
#endif
}

}
