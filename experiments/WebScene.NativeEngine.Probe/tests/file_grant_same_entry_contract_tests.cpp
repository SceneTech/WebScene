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
    std::vector<std::uint8_t> first{1, 0, 2, 255};
    std::vector<std::uint8_t> second{8, 0, 9, 255};
    webscene_file_grant_same_entry_request_v2 request{
        sizeof(request), 2, 1,
        {first.data(), first.size()}, {second.data(), second.size()}};
};

webscene_file_grant_same_entry_completion_v2 completion(
    std::uint64_t id, bool admitted, bool same) {
    return {sizeof(webscene_file_grant_same_entry_completion_v2), 2, id,
        static_cast<std::uint8_t>(admitted), static_cast<std::uint8_t>(same), {}};
}

} // namespace

int main() {
    using namespace webscene_native;
    file_grant_same_entry_broker_v2 broker;
    std::uint64_t callback_count{};
    bool callback_result{};
    request_fixture request;
    require(broker.queue(request.request,
        [&](file_grant_same_entry_completion_data_v2&& value) {
            ++callback_count;
            callback_result = value.admitted && value.same_entry;
        }), "valid same-entry request was rejected");
    auto lease = broker.take();
    require(lease && lease->view.request_id == 1
            && lease->view.first_grant_id.byte_count == request.first.size()
            && lease->view.second_grant_id.byte_count == request.second.size(),
        "same-entry lease lost opaque tokens");
    request.first.assign(32, 42);
    require(lease->view.first_grant_id.byte_count == 4
            && lease->view.first_grant_id.data[0] == 1,
        "same-entry lease retained producer token storage");
    lease.reset();

    auto invalid_completion = completion(1, false, true);
    require(!broker.complete(invalid_completion),
        "denied completion leaked a true identity result");
    invalid_completion = completion(1, true, true);
    invalid_completion.reserved[2] = 1;
    require(!broker.complete(invalid_completion),
        "same-entry completion accepted reserved bytes");
    const auto same = completion(1, true, true);
    require(broker.complete(same) && callback_count == 1 && callback_result,
        "same-entry completion lost the admitted result");
    require(!broker.complete(same) && callback_count == 1,
        "same-entry request completed more than once");

    request_fixture malformed;
    malformed.request.request_id = 2;
    malformed.request.version = 1;
    require(!broker.queue(malformed.request, {}),
        "wrong same-entry request version was accepted");
    malformed.request.version = 2;
    malformed.request.first_grant_id = {};
    require(!broker.queue(malformed.request, {}),
        "empty same-entry grant was accepted");
    malformed.request.first_grant_id = {
        malformed.first.data(), file_panel_maximum_token_bytes_v2 + 1};
    require(!broker.queue(malformed.request, {}),
        "oversized same-entry grant was accepted");

    constexpr std::uint64_t cycles = 10'000;
    const auto descriptors_before = descriptor_count();
    const auto resident_before = resident_bytes();
    const auto started = std::chrono::steady_clock::now();
    for (std::uint64_t index = 0; index < cycles; ++index) {
        request_fixture current;
        current.request.request_id = 100 + index;
        require(broker.queue(current.request,
            [&](file_grant_same_entry_completion_data_v2&& value) {
                ++callback_count;
                callback_result = value.admitted && value.same_entry;
            }), "same-entry lifecycle request was rejected");
        auto current_lease = broker.take();
        require(current_lease && current_lease->view.request_id == 100 + index,
            "same-entry lifecycle lease was lost");
        current_lease.reset();
        const auto result = completion(100 + index, true, index % 2 == 0);
        require(broker.complete(result),
            "same-entry lifecycle completion was rejected");
        if ((index + 1) % 100 == 0) {
            const auto metrics = broker.metrics();
            require(metrics.queued_requests == 0
                    && metrics.pending_requests == 0
                    && metrics.retained_token_bytes == 0,
                "same-entry lifecycle retained opaque token bytes");
        }
    }
    const auto elapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - started).count();
    const auto resident_after = resident_bytes();
    const auto descriptors_after = descriptor_count();
    require(elapsed < 1.0, "10,000 same-entry bridge cycles exceeded one second");
    require(resident_before == 0 || resident_after <= resident_before + 16U * 1024U * 1024U,
        "same-entry bridge exceeded the 16 MiB RSS budget");
    require(descriptors_before == 0 || descriptors_after <= descriptors_before + 2,
        "same-entry bridge leaked file descriptors");

    bool retirement_valid = true;
    for (std::uint64_t index = 0;
         index < file_grant_same_entry_maximum_pending_v2; ++index) {
        request_fixture pending;
        pending.request.request_id = 20'000 + index;
        require(broker.queue(pending.request,
            [&](file_grant_same_entry_completion_data_v2&& value) {
                retirement_valid = retirement_valid && !value.admitted
                    && !value.same_entry;
                ++callback_count;
            }), "bounded same-entry request was rejected early");
    }
    request_fixture overflow;
    overflow.request.request_id = 30'000;
    require(!broker.queue(overflow.request, {}),
        "same-entry pending capacity was unbounded");
    broker.retire();
    const auto final = broker.metrics();
    require(retirement_valid && final.queued_requests == 0
            && final.pending_requests == 0
            && final.retained_token_bytes == 0
            && final.completed_requests == cycles + 1
            && final.retired_requests == file_grant_same_entry_maximum_pending_v2
            && callback_count == cycles + 1
                + file_grant_same_entry_maximum_pending_v2,
        "same-entry retirement or metrics changed");
    std::cout << "WebScene file grant same-entry bridge passed; cycles="
              << cycles << " elapsedSeconds=" << elapsed
              << " rssDelta=" << static_cast<std::int64_t>(resident_after)
                    - static_cast<std::int64_t>(resident_before)
              << " fdDelta=" << static_cast<std::int64_t>(descriptors_after)
                    - static_cast<std::int64_t>(descriptors_before)
              << " retainedBytes=" << final.retained_token_bytes << '\n';
}
