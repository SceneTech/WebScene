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
struct request_fixture final {
    std::vector<std::uint8_t> grant{1, 2, 3};
    std::vector<std::uint8_t> cursor;
    webscene_file_grant_directory_request_v2 request{
        sizeof(request), 2, 1,
        WEBSCENE_FILE_GRANT_DIRECTORY_ENUMERATE_V2, 0,
        {grant.data(), grant.size()}, {}, 32, 0};
    void bind() {
        request.directory_grant_id = {grant.empty() ? nullptr : grant.data(),
            grant.size()};
        request.cursor = {cursor.empty() ? nullptr : cursor.data(), cursor.size()};
    }
};
struct entry_fixture final {
    std::string name;
    std::vector<std::uint8_t> grant;
    webscene_file_grant_directory_entry_v2 entry{};
    entry_fixture(std::string value, std::vector<std::uint8_t> token,
                  std::uint32_t kind) : name(std::move(value)), grant(std::move(token)) {
        entry = {sizeof(entry), 2,
            {sizeof(webscene_file_grant_metadata_v2), 2,
                kind == WEBSCENE_FILE_PANEL_ENTRY_FILE_V2 ? 7U : 0U,
                123, kind, 0},
            kind == WEBSCENE_FILE_PANEL_ENTRY_FILE_V2
                ? WEBSCENE_FILE_PANEL_GRANT_READ_V2
                : WEBSCENE_FILE_PANEL_GRANT_ENUMERATE_V2,
            0, {name.data(), name.size()}, {grant.data(), grant.size()}};
    }
};
webscene_file_grant_directory_completion_v2 completion(
    std::uint64_t id, std::uint32_t action, std::uint32_t status,
    const webscene_file_grant_directory_entry_v2* entries = nullptr,
    std::size_t count = 0, webscene_file_panel_token_v2 cursor = {},
    std::uint64_t skipped = 0) {
    return {sizeof(webscene_file_grant_directory_completion_v2), 2, id,
        status, action, entries, count, cursor, skipped};
}
} // namespace

int main() {
    using namespace webscene_native;
    file_grant_directory_broker_v2 broker;
    request_fixture fixture;
    fixture.bind();
    file_grant_directory_completion_data_v2 observed;
    std::uint64_t callbacks{};
    require(broker.queue(fixture.request,
        [&](auto&& value) { observed = std::move(value); ++callbacks; }),
        "valid directory request rejected");
    auto lease = broker.take();
    require(lease && lease->view.maximum_entries == 32
            && lease->directory_grant_id == fixture.grant,
        "directory request lease lost fields");
    fixture.grant.assign(64, 9);
    require(lease->directory_grant_id.size() == 3,
        "directory request retained caller storage");
    lease.reset();

    entry_fixture file{"line\nname.txt", {4, 5},
        WEBSCENE_FILE_PANEL_ENTRY_FILE_V2};
    entry_fixture directory{"資料", {6, 7},
        WEBSCENE_FILE_PANEL_ENTRY_DIRECTORY_V2};
    std::vector<webscene_file_grant_directory_entry_v2> entries{
        file.entry, directory.entry};
    const std::vector<std::uint8_t> cursor{8, 9};
    auto page = completion(1, WEBSCENE_FILE_GRANT_DIRECTORY_ENUMERATE_V2,
        WEBSCENE_FILE_GRANT_DIRECTORY_SUCCESS_V2, entries.data(), entries.size(),
        {cursor.data(), cursor.size()}, 2);
    require(broker.complete(page) && callbacks == 1
            && observed.entries.size() == 2
            && observed.entries[0].display_name == "line\nname.txt"
            && observed.entries[1].kind == WEBSCENE_FILE_PANEL_ENTRY_DIRECTORY_V2
            && observed.next_cursor == cursor && observed.skipped_symlinks == 2,
        "directory completion lost bounded page data");
    require(!broker.complete(page) && callbacks == 1,
        "directory request completed twice");

    request_fixture malformed;
    malformed.request.request_id = 2;
    malformed.request.maximum_entries = 33;
    malformed.bind();
    require(!broker.queue(malformed.request, {}),
        "oversized directory page request accepted");
    malformed.request.maximum_entries = 32;
    malformed.request.directory_grant_id = {};
    require(!broker.queue(malformed.request, {}),
        "empty directory grant accepted");

    request_fixture duplicate;
    duplicate.request.request_id = 3;
    duplicate.bind();
    require(broker.queue(duplicate.request, {}), "duplicate-name fixture rejected");
    auto duplicate_lease = broker.take();
    entries[1] = file.entry;
    auto duplicate_page = completion(3,
        WEBSCENE_FILE_GRANT_DIRECTORY_ENUMERATE_V2,
        WEBSCENE_FILE_GRANT_DIRECTORY_SUCCESS_V2,
        entries.data(), entries.size());
    require(!broker.complete(duplicate_page), "duplicate page entry accepted");
    broker.retire();

    request_fixture release;
    release.request.request_id = 4;
    release.request.action = WEBSCENE_FILE_GRANT_DIRECTORY_RELEASE_CURSOR_V2;
    release.grant.clear();
    release.cursor = {10, 11};
    release.request.maximum_entries = 0;
    release.bind();
    require(broker.queue(release.request,
        [&](auto&& value) {
            require(value.action
                    == WEBSCENE_FILE_GRANT_DIRECTORY_RELEASE_CURSOR_V2,
                "cursor release action changed");
            ++callbacks;
        }), "cursor release request rejected");
    auto release_lease = broker.take();
    require(release_lease && release_lease->cursor == release.cursor,
        "cursor release lease lost token");
    require(broker.complete(completion(4,
        WEBSCENE_FILE_GRANT_DIRECTORY_RELEASE_CURSOR_V2,
        WEBSCENE_FILE_GRANT_DIRECTORY_SUCCESS_V2)),
        "cursor release completion rejected");

    const auto fd_before = descriptor_count();
    const auto rss_before = resident_bytes();
    const auto started = std::chrono::steady_clock::now();
    constexpr std::uint64_t cycles = 10000;
    for (std::uint64_t index = 0; index < cycles; ++index) {
        request_fixture current;
        current.request.request_id = 100 + index;
        current.request.maximum_entries = 1;
        current.bind();
        require(broker.queue(current.request, {}), "performance request rejected");
        require(broker.take() != nullptr, "performance request missing");
        require(broker.complete(completion(100 + index,
            WEBSCENE_FILE_GRANT_DIRECTORY_ENUMERATE_V2,
            WEBSCENE_FILE_GRANT_DIRECTORY_SUCCESS_V2)),
            "performance completion rejected");
    }
    for (std::uint64_t index = 0; index < 100; ++index) {
        request_fixture pending;
        pending.request.request_id = 20000 + index;
        pending.bind();
        require(broker.queue(pending.request, {}), "lifecycle request rejected");
        broker.retire();
    }
    const auto elapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - started).count();
    const auto metrics = broker.metrics();
    const auto fd_after = descriptor_count();
    const auto rss_after = resident_bytes();
    require(metrics.queued_requests == 0 && metrics.pending_requests == 0
            && metrics.retained_input_bytes == 0,
        "directory broker retained state");
    require(fd_before == 0 || fd_after <= fd_before + 1,
        "directory broker leaked descriptors");
    require(rss_before == 0 || rss_after <= rss_before + 8U * 1024U * 1024U,
        "directory broker exceeded RSS budget");
    require(elapsed < 5.0, "directory broker latency exceeded budget");
    std::cout << "WebScene directory broker passed; cycles=" << cycles
              << " elapsed_seconds=" << elapsed
              << " rss_delta=" << (rss_after >= rss_before ? rss_after-rss_before : 0)
              << " fd_delta=" << (fd_after >= fd_before ? fd_after-fd_before : 0)
              << " retained=" << metrics.retained_input_bytes << '\n';
}
