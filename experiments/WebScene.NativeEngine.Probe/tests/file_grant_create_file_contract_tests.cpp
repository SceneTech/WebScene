#include "webscene_file_panel_v2.hpp"

#include <array>
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <string>

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
webscene_file_grant_create_file_request_v2 request(
    std::uint64_t id, const std::array<std::uint8_t, 16>& token,
    std::string_view name) {
    return {sizeof(webscene_file_grant_create_file_request_v2), 2, id,
        {token.data(), token.size()}, {name.data(), name.size()}, 0};
}
webscene_file_grant_create_file_completion_v2 completion(
    std::uint64_t id, std::uint32_t status, std::string_view name = {},
    const std::array<std::uint8_t, 16>* token = nullptr) {
    webscene_file_grant_create_file_completion_v2 result{};
    result.struct_size = sizeof(result);
    result.version = 2;
    result.request_id = id;
    result.status = status;
    if (status == WEBSCENE_FILE_GRANT_CREATE_FILE_SUCCESS_V2) {
        result.capabilities = WEBSCENE_FILE_PANEL_GRANT_READ_V2
            | WEBSCENE_FILE_PANEL_GRANT_WRITE_V2;
        result.metadata = {sizeof(result.metadata), 2, 0, 7,
            WEBSCENE_FILE_PANEL_ENTRY_FILE_V2, 0};
        result.display_name = {name.data(), name.size()};
        result.grant_id = {token->data(), token->size()};
    }
    return result;
}
}

int main() {
    using webscene_native::file_grant_create_file_broker_v2;
    std::array<std::uint8_t, 16> parent{1};
    std::array<std::uint8_t, 16> child{2};
    file_grant_create_file_broker_v2 broker;
    webscene_native::file_grant_create_file_completion_data_v2 observed;
    require(broker.queue(request(1, parent, "child.txt"),
                [&](auto&& value) { observed = std::move(value); }),
        "valid create-file request was rejected");
    auto taken = broker.take();
    require(taken && taken->view.request_id == 1
            && taken->display_name == "child.txt"
            && taken->directory_grant_id.size() == parent.size(),
        "create-file request was not copied");
    auto success = completion(1,
        WEBSCENE_FILE_GRANT_CREATE_FILE_SUCCESS_V2, "child.txt", &child);
    require(broker.complete(success)
            && observed.status == WEBSCENE_FILE_GRANT_CREATE_FILE_SUCCESS_V2
            && observed.display_name == "child.txt"
            && observed.grant_id.size() == child.size(),
        "valid create-file completion was not copied");

    for (const auto name : {std::string_view{}, std::string_view{"."},
             std::string_view{".."}, std::string_view{"a/b"},
             std::string_view{"a\\b"}})
        require(!broker.queue(request(20 + name.size(), parent, name), {}),
            "unsafe create-file name was admitted");
    auto malformed = success;
    malformed.request_id = 99;
    malformed.metadata.kind = WEBSCENE_FILE_PANEL_ENTRY_DIRECTORY_V2;
    require(!broker.complete(malformed),
        "directory completion crossed the file-child ABI");

    constexpr std::uint64_t cycles = 10000;
    const auto descriptors_before = descriptor_count();
    const auto resident_before = resident_bytes();
    const auto started = std::chrono::steady_clock::now();
    for (std::uint64_t index = 0; index < cycles; ++index) {
        const auto id = 1000 + index;
        require(broker.queue(request(id, parent, "existing.txt"), [](auto&&) {})
                && broker.take() != nullptr,
            "10k create-file broker request failed");
        auto current = completion(id,
            WEBSCENE_FILE_GRANT_CREATE_FILE_SUCCESS_V2, "existing.txt", &child);
        require(broker.complete(current),
            "10k create-file broker completion failed");
    }
    const auto elapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - started).count();
    const auto resident_after = resident_bytes();
    const auto descriptors_after = descriptor_count();
    auto pending_request = request(50000, parent, "pending.txt");
    std::uint32_t retired_status = 99;
    require(broker.queue(pending_request,
                [&](auto&& value) { retired_status = value.status; }),
        "retirement request was rejected");
    broker.retire();
    const auto metrics = broker.metrics();
    require(retired_status == WEBSCENE_FILE_GRANT_CREATE_FILE_CANCELLED_V2
            && metrics.queued_requests == 0 && metrics.pending_requests == 0
            && metrics.retained_input_bytes == 0
            && metrics.completed_requests == cycles + 1
            && metrics.retired_requests == 1 && elapsed < 5.0,
        "create-file broker lifecycle or performance bound failed");
    require(resident_before == 0
            || resident_after <= resident_before + 8U * 1024U * 1024U,
        "create-file broker exceeded the 8 MiB RSS budget");
    require(descriptors_before == 0
            || descriptors_after <= descriptors_before + 1,
        "create-file broker leaked file descriptors");
    for (std::uint64_t cycle = 0; cycle < 100; ++cycle) {
        file_grant_create_file_broker_v2 lifecycle;
        auto pending = request(60'000 + cycle, parent, "cycle.txt");
        bool cancelled = false;
        require(lifecycle.queue(pending,
                    [&](auto&& value) {
                        cancelled = value.status
                            == WEBSCENE_FILE_GRANT_CREATE_FILE_CANCELLED_V2;
                    }),
            "create-file lifecycle admission failed");
        lifecycle.retire();
        const auto lifecycle_metrics = lifecycle.metrics();
        require(cancelled && lifecycle_metrics.queued_requests == 0
                && lifecycle_metrics.pending_requests == 0
                && lifecycle_metrics.retained_input_bytes == 0,
            "create-file lifecycle retirement retained state");
    }
    std::cout << "WebScene file-child broker passed; operations=" << cycles
              << " elapsedSeconds=" << elapsed
              << " rssDelta=" << static_cast<std::int64_t>(resident_after)
                    - static_cast<std::int64_t>(resident_before)
              << " fdDelta=" << static_cast<std::int64_t>(descriptors_after)
                    - static_cast<std::int64_t>(descriptors_before) << '\n';
}
