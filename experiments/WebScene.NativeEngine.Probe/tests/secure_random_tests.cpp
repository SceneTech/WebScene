#include "webscene_secure_random.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
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

struct scripted_source_state final {
    std::size_t call_count{0U};
    bool fail_after_first_write{false};
};

webscene_native::secure_random_chunk_result scripted_source(
    void* context,
    std::span<std::uint8_t> output) noexcept
{
    auto& state = *static_cast<scripted_source_state*>(context);
    ++state.call_count;
    if (state.call_count == 1U) {
        return {webscene_native::secure_random_chunk_status::interrupted, 0U};
    }
    if (state.fail_after_first_write && state.call_count > 2U) {
        return {webscene_native::secure_random_chunk_status::failure, 0U};
    }
    const auto count = std::min<std::size_t>(output.size(), 3U);
    std::fill_n(output.begin(), count, static_cast<std::uint8_t>(0xa5U));
    return {webscene_native::secure_random_chunk_status::success, count};
}

void test_entropy_source_interruption_partial_reads_and_failure()
{
    std::array<std::uint8_t, 8> completed{};
    scripted_source_state completed_state{};
    require(webscene_native::fill_secure_random_from_source(
                completed, 5U, scripted_source, &completed_state),
        "interrupted and partial entropy reads did not complete");
    require(std::all_of(completed.begin(), completed.end(),
                [](auto byte) { return byte == 0xa5U; }),
        "partial entropy reads did not fill the complete destination");
    require(completed_state.call_count == 4U,
        "entropy interruption/partial-read retry count changed");

    std::array<std::uint8_t, 8> failed{};
    failed.fill(0x5aU);
    scripted_source_state failed_state{0U, true};
    require(!webscene_native::fill_secure_random_from_source(
                failed, 5U, scripted_source, &failed_state),
        "entropy source failure was accepted");
    require(std::all_of(failed.begin(), failed.begin() + 3,
                [](auto byte) { return byte == 0xa5U; })
            && std::all_of(failed.begin() + 3, failed.end(),
                [](auto byte) { return byte == 0x5aU; }),
        "entropy failure used a fallback or wrote beyond successful source bytes");
}

void test_rfc_9562_uuid_v4_vector()
{
    const std::array<std::uint8_t, 16> random_bytes{
        0x91U, 0x91U, 0x08U, 0xf7U, 0x52U, 0xd1U, 0x33U, 0x20U,
        0x5bU, 0xacU, 0xf8U, 0x47U, 0xdbU, 0x41U, 0x48U, 0xa8U};
    const auto uuid = webscene_native::format_uuid_v4(random_bytes);
    require(std::string(uuid.data(), uuid.size())
            == "919108f7-52d1-4320-9bac-f847db4148a8",
        "RFC 9562 UUIDv4 example vector changed");
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
    std::cout << "Secure random throughput gate: requests=" << iterations
              << " bytesPerRequest=" << bytes.size()
              << " elapsed="
              << std::chrono::duration<double>(elapsed).count() << "s\n";
}

}

int main()
{
    try {
        test_operating_system_random_source();
        test_entropy_source_interruption_partial_reads_and_failure();
        test_rfc_9562_uuid_v4_vector();
        test_small_request_performance_gate();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
