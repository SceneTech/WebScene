#include "webscene_file_panel_v2.hpp"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

#if defined(__APPLE__) || defined(__linux__)
#include <dirent.h>
#include <sys/resource.h>
#endif

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
std::uint64_t resident_bytes() {
#if defined(__APPLE__) || defined(__linux__)
    rusage value{}; if (getrusage(RUSAGE_SELF, &value) != 0) return 0;
#if defined(__APPLE__)
    return static_cast<std::uint64_t>(value.ru_maxrss);
#else
    return static_cast<std::uint64_t>(value.ru_maxrss) * 1024U;
#endif
#else
    return 0;
#endif
}
std::size_t descriptor_count() {
#if defined(__APPLE__) || defined(__linux__)
    auto* directory = opendir("/dev/fd"); if (!directory) return 0;
    std::size_t count{}; while (readdir(directory)) ++count;
    closedir(directory); return count;
#else
    return 0;
#endif
}
webscene_file_grant_write_completion_v2 completion(
    std::uint64_t id, std::uint32_t action, std::uint32_t status,
    const std::vector<std::uint8_t>& transaction = {},
    std::uint64_t offset = 0, std::size_t bytes = 0,
    std::uint64_t size = 0) {
    webscene_file_grant_metadata_v2 metadata{};
    if (status == WEBSCENE_FILE_GRANT_WRITE_SUCCESS_V2
        && action == WEBSCENE_FILE_GRANT_WRITE_COMMIT_V2)
        metadata = {sizeof(metadata), 2, size, 123,
            WEBSCENE_FILE_PANEL_ENTRY_FILE_V2, 0};
    return {sizeof(webscene_file_grant_write_completion_v2), 2, id,
        status, action,
        {transaction.empty() ? nullptr : transaction.data(), transaction.size()},
        metadata, offset, bytes};
}
}

int main() {
    using namespace webscene_native;
    file_grant_write_broker_v2 broker;
    std::vector<std::uint8_t> grant{1, 0, 2, 255};
    std::vector<std::uint8_t> transaction(16, 7);
    std::uint64_t callbacks{};
    file_grant_write_completion_data_v2 observed;

    webscene_file_grant_write_request_v2 begin{sizeof(begin), 2, 1,
        WEBSCENE_FILE_GRANT_WRITE_BEGIN_V2, 0,
        {grant.data(), grant.size()}, {}, 0, nullptr, 0, 0};
    require(broker.queue(begin, [&](auto&& value) {
        ++callbacks; observed = std::move(value);
    }), "valid begin was rejected");
    auto lease = broker.take();
    require(lease && lease->view.grant_id.byte_count == 4,
        "begin lease lost its opaque grant");
    grant.assign(32, 42);
    require(lease->view.grant_id.data[0] == 1,
        "begin retained producer token storage");
    lease.reset();
    auto invalid = completion(1, WEBSCENE_FILE_GRANT_WRITE_BEGIN_V2,
        WEBSCENE_FILE_GRANT_WRITE_SUCCESS_V2, {});
    require(!broker.complete(invalid), "begin without transaction was accepted");
    const auto began = completion(1, WEBSCENE_FILE_GRANT_WRITE_BEGIN_V2,
        WEBSCENE_FILE_GRANT_WRITE_SUCCESS_V2, transaction);
    require(broker.complete(began) && callbacks == 1
            && observed.transaction_id == transaction,
        "begin completion lost transaction authority");
    require(!broker.complete(began), "begin completed twice");

    std::vector<std::uint8_t> payload{0, 255, 1, 2};
    webscene_file_grant_write_request_v2 chunk{sizeof(chunk), 2, 2,
        WEBSCENE_FILE_GRANT_WRITE_CHUNK_V2, 0, {},
        {transaction.data(), transaction.size()}, 9,
        payload.data(), payload.size(), 0};
    require(broker.queue(chunk, [&](auto&& value) {
        ++callbacks; observed = std::move(value);
    }), "valid chunk was rejected");
    lease = broker.take();
    payload.assign(8, 9);
    require(lease && lease->bytes == std::vector<std::uint8_t>({0,255,1,2}),
        "chunk retained producer byte storage");
    lease.reset();
    invalid = completion(2, WEBSCENE_FILE_GRANT_WRITE_CHUNK_V2,
        WEBSCENE_FILE_GRANT_WRITE_SUCCESS_V2, {}, 10, 4);
    require(!broker.complete(invalid), "mismatched chunk offset was accepted");
    const auto wrote = completion(2, WEBSCENE_FILE_GRANT_WRITE_CHUNK_V2,
        WEBSCENE_FILE_GRANT_WRITE_SUCCESS_V2, {}, 9, 4);
    require(broker.complete(wrote) && callbacks == 2
            && observed.written_byte_count == 4,
        "chunk completion lost exact range");
    chunk.data = payload.data();
    chunk.byte_count = 1U;

    webscene_file_grant_write_request_v2 commit{sizeof(commit), 2, 3,
        WEBSCENE_FILE_GRANT_WRITE_COMMIT_V2, 0, {},
        {transaction.data(), transaction.size()}, 0, nullptr, 0, 13};
    require(broker.queue(commit, [&](auto&& value) {
        ++callbacks; observed = std::move(value);
    }), "valid commit was rejected");
    lease = broker.take(); lease.reset();
    invalid = completion(3, WEBSCENE_FILE_GRANT_WRITE_COMMIT_V2,
        WEBSCENE_FILE_GRANT_WRITE_SUCCESS_V2, {}, 0, 0, 12);
    require(!broker.complete(invalid), "wrong committed length was accepted");
    const auto committed = completion(3, WEBSCENE_FILE_GRANT_WRITE_COMMIT_V2,
        WEBSCENE_FILE_GRANT_WRITE_SUCCESS_V2, {}, 0, 0, 13);
    require(broker.complete(committed) && callbacks == 3
            && observed.byte_count == 13,
        "commit completion lost metadata");

    webscene_file_grant_write_request_v2 malformed = begin;
    malformed.request_id = 4; malformed.version = 1;
    require(!broker.queue(malformed, {}), "wrong version was accepted");
    malformed.version = 2; malformed.grant_id = {};
    require(!broker.queue(malformed, {}), "empty begin grant was accepted");
    auto oversized = chunk; oversized.request_id = 5;
    oversized.byte_count = WEBSCENE_FILE_GRANT_WRITE_MAXIMUM_CHUNK_BYTES_V2 + 1U;
    require(!broker.queue(oversized, {}), "oversized chunk was accepted");

    constexpr std::uint64_t cycles = 10'000;
    std::vector<std::uint8_t> byte{42};
    const auto rss_before = resident_bytes();
    const auto fd_before = descriptor_count();
    const auto started = std::chrono::steady_clock::now();
    for (std::uint64_t index = 0; index < cycles; ++index) {
        webscene_file_grant_write_request_v2 request{sizeof(request), 2,
            100 + index, WEBSCENE_FILE_GRANT_WRITE_CHUNK_V2, 0, {},
            {transaction.data(), transaction.size()}, index,
            byte.data(), byte.size(), 0};
        require(broker.queue(request, [&](auto&& value) {
            require(value.status == WEBSCENE_FILE_GRANT_WRITE_SUCCESS_V2,
                "cycle write failed"); ++callbacks;
        }), "cycle write was rejected");
        lease = broker.take(); require(lease != nullptr, "cycle lease was lost");
        lease.reset();
        const auto result = completion(100 + index,
            WEBSCENE_FILE_GRANT_WRITE_CHUNK_V2,
            WEBSCENE_FILE_GRANT_WRITE_SUCCESS_V2, {}, index, 1);
        require(broker.complete(result), "cycle completion was rejected");
        if ((index + 1) % 100 == 0) {
            const auto metrics = broker.metrics();
            require(metrics.queued_requests == 0 && metrics.pending_requests == 0
                    && metrics.retained_input_bytes == 0,
                "cycle retained write storage");
        }
    }
    const auto elapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - started).count();
    const auto rss_after = resident_bytes();
    const auto fd_after = descriptor_count();
    require(elapsed < 1.0, "10,000 write operations exceeded one second");
    require(rss_before == 0 || rss_after <= rss_before + 16U * 1024U * 1024U,
        "write broker exceeded RSS budget");
    require(fd_before == 0 || fd_after <= fd_before + 2,
        "write broker leaked descriptors");

    bool retired = true;
    for (std::uint64_t index = 0; index < file_grant_write_maximum_pending_v2;
         ++index) {
        auto request = chunk; request.request_id = 20'000 + index;
        request.offset = index;
        require(broker.queue(request, [&](auto&& value) {
            retired = retired
                && value.status == WEBSCENE_FILE_GRANT_WRITE_CANCELLED_V2;
            ++callbacks;
        }), "pending write rejected early");
    }
    auto overflow = chunk; overflow.request_id = 30'000;
    require(!broker.queue(overflow, {}), "pending write capacity was unbounded");
    broker.retire();
    const auto final = broker.metrics();
    require(retired && final.queued_requests == 0
            && final.pending_requests == 0
            && final.retained_input_bytes == 0
            && final.retired_requests == file_grant_write_maximum_pending_v2,
        "write retirement retained state");
    std::cout << "WebScene file grant write broker passed; cycles=" << cycles
              << " elapsedSeconds=" << elapsed
              << " rssDelta=" << static_cast<std::int64_t>(rss_after)
                    - static_cast<std::int64_t>(rss_before)
              << " fdDelta=" << static_cast<std::int64_t>(fd_after)
                    - static_cast<std::int64_t>(fd_before)
              << " retainedBytes=" << final.retained_input_bytes << '\n';
}
