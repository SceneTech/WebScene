#include "webscene_secure_random.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

void test_operating_system_random_source()
{
    std::array<std::uint8_t, 0> empty{};
    require(webscene_native::fill_secure_random(empty),
        "the operating-system random source rejected an empty request");

    std::vector<std::uint8_t> first(webscene_native::web_crypto_random_quota, 0x5aU);
    std::vector<std::uint8_t> second(first.size(), 0x5aU);
    require(webscene_native::fill_secure_random(first),
        "the operating-system random source rejected the Web Crypto quota");
    require(webscene_native::fill_secure_random(second),
        "the operating-system random source rejected a repeated request");
    require(std::any_of(first.begin(), first.end(), [](auto byte) { return byte != 0x5aU; }),
        "secure random output did not modify the destination");
    require(first != second, "two independent secure random results were identical");
}

void test_small_request_performance_gate()
{
    constexpr std::size_t iterations = 4'096U;
    std::array<std::uint8_t, 16> bytes{};
    const auto start = std::chrono::steady_clock::now();
    for (std::size_t index = 0; index < iterations; ++index) {
        require(webscene_native::fill_secure_random(bytes),
            "the operating-system random source failed during the performance gate");
    }
    const auto elapsed = std::chrono::steady_clock::now() - start;
    require(elapsed < std::chrono::seconds(2),
        "4,096 16-byte CSPRNG requests exceeded the two-second performance budget");
}

}

int main()
{
    try {
        test_operating_system_random_source();
        test_small_request_performance_gate();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
