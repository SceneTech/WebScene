#include "webscene_file_panel_v2.hpp"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(__APPLE__) || defined(__linux__)
#include <dirent.h>
#include <sys/resource.h>
#endif

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
std::uint64_t resident_bytes() {
#if defined(__APPLE__) || defined(__linux__)
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0) return 0;
#if defined(__APPLE__)
    return static_cast<std::uint64_t>(usage.ru_maxrss);
#else
    return static_cast<std::uint64_t>(usage.ru_maxrss) * 1024U;
#endif
#else
    return 0;
#endif
}
std::size_t descriptor_count() {
#if defined(__APPLE__) || defined(__linux__)
    auto* directory = opendir("/dev/fd");
    if (!directory) return 0;
    std::size_t result{};
    while (readdir(directory)) ++result;
    closedir(directory);
    return result;
#else
    return 0;
#endif
}
webscene_file_grant_release_request_v2 request(
    const std::vector<std::uint8_t>& token) {
    return {sizeof(webscene_file_grant_release_request_v2), 2,
        {token.data(), token.size()}, 0, 0};
}
} // namespace

int main() {
    using namespace webscene_native;
    file_grant_release_broker_v2 broker;
    const std::vector<std::uint8_t> first{1, 2, 3};
    auto valid = request(first);
    require(broker.queue(valid), "valid release rejected");
    require(!broker.queue(valid), "duplicate release admitted");
    auto lease = broker.take();
    require(lease && lease->grant_id == first
            && lease->view.grant_id.data == lease->grant_id.data(),
        "release lease did not own and rebind token");
    require(!broker.queue(valid), "delivered token released twice");

    auto malformed = valid;
    malformed.struct_size = 0;
    require(!broker.queue(malformed), "short request admitted");
    malformed = valid;
    malformed.grant_id = {};
    require(!broker.queue(malformed), "empty token admitted");
    malformed = valid;
    malformed.reserved = 1;
    require(!broker.queue(malformed), "reserved input admitted");
    std::vector<std::uint8_t> oversized(file_panel_maximum_token_bytes_v2 + 1, 7);
    malformed = request(oversized);
    require(!broker.queue(malformed), "oversized token admitted");

    const auto rss_before = resident_bytes();
    const auto fd_before = descriptor_count();
    const auto started = std::chrono::steady_clock::now();
    constexpr std::size_t cycles = 10000;
    for (std::size_t index = 0; index < cycles; ++index) {
        std::vector<std::uint8_t> token{
            static_cast<std::uint8_t>(index),
            static_cast<std::uint8_t>(index >> 8U),
            static_cast<std::uint8_t>(index >> 16U), 0xa5};
        auto value = request(token);
        require(broker.queue(value), "10k release queue rejected");
        auto copied = broker.take();
        require(copied && copied->grant_id == token, "10k release copy changed");
    }
    const auto elapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - started).count();
    const auto metrics = broker.metrics();
    require(metrics.queued_requests == 0 && metrics.retained_input_bytes == 0,
        "release broker retained queued state");
    require(metrics.delivered_requests == cycles + 1,
        "release delivery count changed");
    require(elapsed < 5.0, "10k release broker gate exceeded five seconds");
    const auto fd_after = descriptor_count();
    require(fd_before == 0 || fd_after <= fd_before + 1,
        "release broker leaked descriptors");
    const auto rss_after = resident_bytes();
    require(rss_before == 0 || rss_after <= rss_before + 32U * 1024U * 1024U,
        "release broker exceeded bounded memory gate");

    for (std::size_t index = 0; index < 100; ++index) {
        file_grant_release_broker_v2 lifecycle;
        std::vector<std::uint8_t> token{static_cast<std::uint8_t>(index), 0x5a};
        auto value = request(token);
        require(lifecycle.queue(value), "lifecycle release rejected");
        lifecycle.retire();
        const auto retired = lifecycle.metrics();
        require(retired.queued_requests == 0 && retired.retained_input_bytes == 0
                && retired.retired_requests == 1,
            "release retirement retained authority bytes");
    }

    std::cout << "file_grant_release_contract_tests: 10k=" << elapsed
              << "s rss_delta="
              << (rss_after >= rss_before ? rss_after - rss_before : 0)
              << " fd_delta=" << (fd_after >= fd_before ? fd_after - fd_before : 0)
              << " retained=" << metrics.retained_input_bytes << '\n';
    return 0;
}
