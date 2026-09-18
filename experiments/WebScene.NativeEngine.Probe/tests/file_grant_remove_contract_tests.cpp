#include "webscene_file_panel_v2.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#if defined(__APPLE__) || defined(__linux__)
#include <dirent.h>
#include <sys/resource.h>
#endif

namespace {
void require(bool value, const char* message) {
    if (!value) { std::cerr << message << '\n'; std::abort(); }
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
    if (directory == nullptr) return 0;
    std::size_t result{};
    while (readdir(directory) != nullptr) ++result;
    closedir(directory);
    return result;
#else
    return 0;
#endif
}
webscene_file_grant_remove_request_v2 request(
    std::uint64_t id, const std::array<std::uint8_t, 16>& token,
    std::string_view name, bool recursive = false) {
    return {sizeof(webscene_file_grant_remove_request_v2), 2, id,
        {token.data(), token.size()}, {name.data(), name.size()},
        static_cast<std::uint8_t>(recursive), {}};
}
webscene_file_grant_remove_completion_v2 completion(
    std::uint64_t id, std::uint32_t status) {
    return {sizeof(webscene_file_grant_remove_completion_v2), 2, id,
        status, 0};
}
}

int main() {
    using webscene_native::file_grant_remove_broker_v2;
    std::array<std::uint8_t, 16> parent{1};
    file_grant_remove_broker_v2 broker;
    webscene_native::file_grant_remove_completion_data_v2 observed;
    require(broker.queue(request(1, parent, "child-🙂", true),
                [&](auto&& value) { observed = std::move(value); }),
        "valid remove request was rejected");
    auto taken = broker.take();
    require(taken && taken->view.request_id == 1
            && taken->display_name == "child-🙂"
            && taken->view.recursive == 1
            && taken->directory_grant_id.size() == parent.size(),
        "remove request was not copied exactly");
    auto success = completion(1, WEBSCENE_FILE_GRANT_REMOVE_SUCCESS_V2);
    require(broker.complete(success)
            && observed.status == WEBSCENE_FILE_GRANT_REMOVE_SUCCESS_V2
            && !broker.complete(success),
        "remove completion was not exactly once");

    for (const auto name : {std::string_view{}, std::string_view{"."},
             std::string_view{".."}, std::string_view{"a/b"},
             std::string_view{"a\\b"}})
        require(!broker.queue(request(20 + name.size(), parent, name), {}),
            "unsafe remove name was admitted");
    auto malformed = request(99, parent, "child");
    malformed.recursive = 2;
    require(!broker.queue(malformed, {}),
        "invalid recursive flag was admitted");

    constexpr std::uint64_t cycles = 10000;
    const auto descriptors_before = descriptor_count();
    const auto resident_before = resident_bytes();
    const auto started = std::chrono::steady_clock::now();
    for (std::uint64_t index = 0; index < cycles; ++index) {
        const auto id = 1000 + index;
        require(broker.queue(request(id, parent, "existing"), [](auto&&) {})
                && broker.take() != nullptr,
            "10k remove broker request failed");
        auto current = completion(id, WEBSCENE_FILE_GRANT_REMOVE_SUCCESS_V2);
        require(broker.complete(current),
            "10k remove broker completion failed");
    }
    const auto elapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - started).count();
    const auto resident_after = resident_bytes();
    const auto descriptors_after = descriptor_count();
    bool cancelled = false;
    require(broker.queue(request(50000, parent, "pending"),
                [&](auto&& value) {
                    cancelled = value.status
                        == WEBSCENE_FILE_GRANT_REMOVE_CANCELLED_V2;
                }),
        "retirement request was rejected");
    broker.retire();
    const auto metrics = broker.metrics();
    require(cancelled && metrics.queued_requests == 0
            && metrics.pending_requests == 0
            && metrics.retained_input_bytes == 0
            && metrics.completed_requests == cycles + 1
            && metrics.retired_requests == 1 && elapsed < 5.0,
        "remove broker lifecycle or performance bound failed");
    require(resident_before == 0
            || resident_after <= resident_before + 8U * 1024U * 1024U,
        "remove broker exceeded the 8 MiB RSS budget");
    require(descriptors_before == 0
            || descriptors_after <= descriptors_before + 1,
        "remove broker leaked file descriptors");
    for (std::uint64_t cycle = 0; cycle < 100; ++cycle) {
        file_grant_remove_broker_v2 lifecycle;
        bool retired = false;
        require(lifecycle.queue(request(60'000 + cycle, parent, "cycle"),
                    [&](auto&& value) {
                        retired = value.status
                            == WEBSCENE_FILE_GRANT_REMOVE_CANCELLED_V2;
                    }),
            "remove lifecycle admission failed");
        lifecycle.retire();
        const auto current = lifecycle.metrics();
        require(retired && current.queued_requests == 0
                && current.pending_requests == 0
                && current.retained_input_bytes == 0,
            "remove lifecycle retirement retained state");
    }
    std::cout << "WebScene remove broker passed; operations=" << cycles
              << " elapsedSeconds=" << elapsed
              << " rssDelta=" << static_cast<std::int64_t>(resident_after)
                    - static_cast<std::int64_t>(resident_before)
              << " fdDelta=" << static_cast<std::int64_t>(descriptors_after)
                    - static_cast<std::int64_t>(descriptors_before) << '\n';
}
