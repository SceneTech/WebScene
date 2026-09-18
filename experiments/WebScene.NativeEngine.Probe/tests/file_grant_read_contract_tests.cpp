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
    if (directory == nullptr) return 0;
    std::size_t result{};
    while (readdir(directory) != nullptr) ++result;
    closedir(directory);
    return result;
#else
    return 0;
#endif
}

struct request_fixture final {
    std::vector<std::uint8_t> token{1, 0, 2, 255};
    webscene_file_grant_read_request_v2 request{
        sizeof(request), 2, 1, {token.data(), token.size()}, 0, 32, 0};
};

webscene_file_grant_read_completion_v2 success(
    std::uint64_t id, std::uint64_t offset,
    const std::vector<std::uint8_t>& bytes,
    std::uint64_t total, std::int64_t modified, bool eof) {
    return {sizeof(webscene_file_grant_read_completion_v2), 2, id,
        WEBSCENE_FILE_GRANT_READ_SUCCESS_V2, 0,
        {sizeof(webscene_file_grant_metadata_v2), 2, total, modified,
            WEBSCENE_FILE_PANEL_ENTRY_FILE_V2, 0},
        offset, bytes.empty() ? nullptr : bytes.data(), bytes.size(),
        static_cast<std::uint8_t>(eof), {}};
}

webscene_file_grant_read_completion_v2 failure(
    std::uint64_t id, std::uint64_t offset, std::uint32_t status) {
    return {sizeof(webscene_file_grant_read_completion_v2), 2, id,
        status, 0, {}, offset, nullptr, 0, 0, {}};
}

} // namespace

int main() {
    using namespace webscene_native;
    file_grant_read_broker_v2 broker;
    std::uint64_t callback_count{};
    file_grant_read_completion_data_v2 observed;
    request_fixture fixture;
    require(broker.queue(fixture.request,
        [&](file_grant_read_completion_data_v2&& value) {
            ++callback_count;
            observed = std::move(value);
        }), "valid grant read request was rejected");
    auto lease = broker.take();
    require(lease && lease->view.request_id == 1
            && lease->view.grant_id.byte_count == fixture.token.size()
            && lease->view.maximum_bytes == 32,
        "grant read lease lost request fields");
    fixture.token.assign(64, 42);
    require(lease->view.grant_id.byte_count == 4
            && lease->view.grant_id.data[0] == 1,
        "grant read lease retained producer token storage");
    lease.reset();

    std::vector<std::uint8_t> bytes{0, 255, 128, 1};
    auto invalid = success(1, 1, bytes, 5, 123, true);
    require(!broker.complete(invalid), "mismatched read offset was accepted");
    invalid = success(1, 0, bytes, 5, 123, true);
    require(!broker.complete(invalid), "premature read eof was accepted");
    invalid = success(1, 0, bytes, 4, 123, true);
    invalid.reserved_bytes[3] = 1;
    require(!broker.complete(invalid), "read reserved byte was accepted");
    const auto valid = success(1, 0, bytes, 4, 123, true);
    require(broker.complete(valid) && callback_count == 1
            && observed.bytes == bytes && observed.byte_count == 4
            && observed.modification_time_ns == 123 && observed.eof,
        "valid read completion lost metadata or bytes");
    require(!broker.complete(valid) && callback_count == 1,
        "grant read completed more than once");

    request_fixture failed;
    failed.request.request_id = 3;
    require(broker.queue(failed.request,
        [&](file_grant_read_completion_data_v2&& value) {
            require(value.status == WEBSCENE_FILE_GRANT_READ_NOT_FOUND_V2,
                "versioned empty failure metadata changed the status");
            ++callback_count;
        }), "failure request was rejected");
    auto failed_lease = broker.take();
    require(failed_lease != nullptr, "failure request lease was lost");
    auto failed_completion = failure(
        3, 0, WEBSCENE_FILE_GRANT_READ_NOT_FOUND_V2);
    failed_completion.metadata.struct_size = sizeof(failed_completion.metadata);
    failed_completion.metadata.version = 2;
    require(broker.complete(failed_completion),
        "versioned empty failure metadata was rejected");

    request_fixture malformed;
    malformed.request.request_id = 2;
    malformed.request.version = 1;
    require(!broker.queue(malformed.request, {}),
        "wrong grant read request version was accepted");
    malformed.request.version = 2;
    malformed.request.grant_id = {};
    require(!broker.queue(malformed.request, {}),
        "empty grant read token was accepted");
    malformed.request.grant_id = {malformed.token.data(), malformed.token.size()};
    malformed.request.maximum_bytes =
        WEBSCENE_FILE_GRANT_READ_MAXIMUM_BYTES_V2 + 1U;
    require(!broker.queue(malformed.request, {}),
        "oversized grant read was accepted");

    constexpr std::uint64_t cycles = 10'000;
    const auto descriptors_before = descriptor_count();
    const auto resident_before = resident_bytes();
    const auto started = std::chrono::steady_clock::now();
    for (std::uint64_t index = 0; index < cycles; ++index) {
        request_fixture current;
        current.request.request_id = 100 + index;
        require(broker.queue(current.request,
            [&](file_grant_read_completion_data_v2&& value) {
                require(value.status == WEBSCENE_FILE_GRANT_READ_SUCCESS_V2,
                    "cycle read did not succeed");
                ++callback_count;
            }), "grant read lifecycle request was rejected");
        auto current_lease = broker.take();
        require(current_lease && current_lease->view.request_id == 100 + index,
            "grant read lifecycle lease was lost");
        current_lease.reset();
        std::vector<std::uint8_t> payload(32, static_cast<std::uint8_t>(index));
        const auto result = success(100 + index, 0, payload, payload.size(),
            static_cast<std::int64_t>(index), true);
        require(broker.complete(result),
            "grant read lifecycle completion was rejected");
        if ((index + 1) % 100 == 0) {
            const auto metrics = broker.metrics();
            require(metrics.queued_requests == 0
                    && metrics.pending_requests == 0
                    && metrics.retained_token_bytes == 0,
                "grant read lifecycle retained request storage");
        }
    }
    const auto elapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - started).count();
    const auto resident_after = resident_bytes();
    const auto descriptors_after = descriptor_count();
    require(elapsed < 1.0, "10,000 grant reads exceeded one second");
    require(resident_before == 0
            || resident_after <= resident_before + 16U * 1024U * 1024U,
        "grant read broker exceeded the 16 MiB RSS budget");
    require(descriptors_before == 0
            || descriptors_after <= descriptors_before + 2,
        "grant read broker leaked file descriptors");

    bool retirement_valid = true;
    for (std::uint64_t index = 0;
         index < file_grant_read_maximum_pending_v2; ++index) {
        request_fixture pending;
        pending.request.request_id = 20'000 + index;
        pending.request.offset = index;
        require(broker.queue(pending.request,
            [&](file_grant_read_completion_data_v2&& value) {
                retirement_valid = retirement_valid
                    && value.status == WEBSCENE_FILE_GRANT_READ_CANCELLED_V2;
                ++callback_count;
            }), "bounded grant read request was rejected early");
    }
    request_fixture overflow;
    overflow.request.request_id = 30'000;
    require(!broker.queue(overflow.request, {}),
        "grant read pending capacity was unbounded");
    broker.retire();
    const auto final = broker.metrics();
    require(retirement_valid && final.queued_requests == 0
            && final.pending_requests == 0
            && final.retained_token_bytes == 0
            && final.completed_requests == cycles + 2
            && final.retired_requests == file_grant_read_maximum_pending_v2
            && final.copied_completion_bytes == cycles * 32 + bytes.size()
            && callback_count == cycles + 2 + file_grant_read_maximum_pending_v2,
        "grant read retirement or metrics changed");
    std::cout << "WebScene file grant read broker passed; cycles=" << cycles
              << " elapsedSeconds=" << elapsed
              << " rssDelta=" << static_cast<std::int64_t>(resident_after)
                    - static_cast<std::int64_t>(resident_before)
              << " fdDelta=" << static_cast<std::int64_t>(descriptors_after)
                    - static_cast<std::int64_t>(descriptors_before)
              << " retainedBytes=" << final.retained_token_bytes << '\n';
}
