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
    std::vector<std::uint8_t> base{1, 0, 2, 255};
    std::vector<std::uint8_t> descendant{8, 0, 9, 255};
    webscene_file_grant_ancestry_request_v2 request{
        sizeof(request), 2, 1,
        {base.data(), base.size()}, {descendant.data(), descendant.size()}, 0};
};

struct completion_fixture final {
    std::vector<std::string> names;
    std::vector<webscene_file_grant_ancestry_component_v2> components;
    std::string error;
    webscene_file_grant_ancestry_completion_v2 completion{};

    completion_fixture(std::uint64_t id, std::uint32_t status,
                       std::vector<std::string> values = {},
                       std::string error_code = {})
        : names(std::move(values)), error(std::move(error_code)) {
        components.reserve(names.size());
        for (const auto& name : names) components.push_back({
            sizeof(webscene_file_grant_ancestry_component_v2), 2,
            {name.data(), name.size()}});
        completion = {sizeof(completion), 2, id, status, 0,
            components.empty() ? nullptr : components.data(), components.size(),
            {error.empty() ? nullptr : error.data(), error.size()}};
    }
};

} // namespace

int main() {
    using namespace webscene_native;
    file_grant_ancestry_broker_v2 broker;
    std::uint64_t callbacks{};
    file_grant_ancestry_completion_data_v2 observed;
    request_fixture request;
    require(broker.queue(request.request,
        [&](file_grant_ancestry_completion_data_v2&& value) {
            ++callbacks;
            observed = std::move(value);
        }), "valid ancestry request was rejected");
    auto lease = broker.take();
    require(lease && lease->view.request_id == 1
            && lease->view.base_directory_grant_id.byte_count == 4
            && lease->view.possible_descendant_grant_id.byte_count == 4,
        "ancestry lease lost opaque tokens");
    request.base.assign(32, 42);
    require(lease->view.base_directory_grant_id.byte_count == 4
            && lease->view.base_directory_grant_id.data[0] == 1,
        "ancestry lease retained producer token storage");
    lease.reset();
    completion_fixture success{1, WEBSCENE_FILE_GRANT_ANCESTRY_SUCCESS_V2,
        {"nested\xF0\x9F\x98\x8A", "file.txt"}};
    require(broker.complete(success.completion) && callbacks == 1
            && observed.components == success.names,
        "ancestry completion lost ordered Unicode components");
    require(!broker.complete(success.completion),
        "ancestry request completed more than once");

    request_fixture malformed;
    malformed.request.request_id = 2;
    malformed.request.reserved = 1;
    require(!broker.queue(malformed.request, {}),
        "reserved ancestry request data was accepted");
    malformed.request.reserved = 0;
    malformed.request.base_directory_grant_id = {};
    require(!broker.queue(malformed.request, {}),
        "empty ancestry base grant was accepted");

    request_fixture unsafe_request;
    unsafe_request.request.request_id = 3;
    require(broker.queue(unsafe_request.request, {}),
        "unsafe-name ancestry fixture was rejected");
    broker.take();
    completion_fixture unsafe{3, WEBSCENE_FILE_GRANT_ANCESTRY_SUCCESS_V2,
        {"../escape"}};
    require(!broker.complete(unsafe.completion),
        "unsafe ancestry component crossed the ABI");
    completion_fixture safe_after_reject{3,
        WEBSCENE_FILE_GRANT_ANCESTRY_NOT_DESCENDANT_V2};
    require(broker.complete(safe_after_reject.completion),
        "valid completion after malformed data was lost");

    request_fixture maximum_request;
    maximum_request.request.request_id = 4;
    require(broker.queue(maximum_request.request, {}),
        "maximum-component ancestry request was rejected");
    broker.take();
    completion_fixture maximum{4, WEBSCENE_FILE_GRANT_ANCESTRY_SUCCESS_V2,
        std::vector<std::string>(
            WEBSCENE_FILE_GRANT_ANCESTRY_MAXIMUM_COMPONENTS_V2, "x")};
    require(broker.complete(maximum.completion),
        "maximum ancestry component count was rejected");
    request_fixture over_request;
    over_request.request.request_id = 5;
    require(broker.queue(over_request.request, {}),
        "over-limit ancestry fixture was rejected at admission");
    broker.take();
    completion_fixture over{5, WEBSCENE_FILE_GRANT_ANCESTRY_SUCCESS_V2,
        std::vector<std::string>(
            WEBSCENE_FILE_GRANT_ANCESTRY_MAXIMUM_COMPONENTS_V2 + 1, "x")};
    require(!broker.complete(over.completion),
        "over-limit ancestry components crossed the ABI");
    completion_fixture bounded_after_reject{5,
        WEBSCENE_FILE_GRANT_ANCESTRY_NOT_DESCENDANT_V2};
    require(broker.complete(bounded_after_reject.completion),
        "bounded completion after over-limit data was lost");

    constexpr std::uint64_t cycles = 10'000;
    const auto descriptors_before = descriptor_count();
    const auto resident_before = resident_bytes();
    const auto started = std::chrono::steady_clock::now();
    for (std::uint64_t index = 0; index < cycles; ++index) {
        request_fixture current;
        current.request.request_id = 100 + index;
        require(broker.queue(current.request,
            [&](file_grant_ancestry_completion_data_v2&& value) {
                ++callbacks;
                observed = std::move(value);
            }), "ancestry lifecycle request was rejected");
        auto current_lease = broker.take();
        require(current_lease
                && current_lease->view.request_id == 100 + index,
            "ancestry lifecycle lease was lost");
        completion_fixture result{100 + index,
            static_cast<std::uint32_t>(
                index % 2 == 0 ? WEBSCENE_FILE_GRANT_ANCESTRY_SUCCESS_V2
                               : WEBSCENE_FILE_GRANT_ANCESTRY_NOT_DESCENDANT_V2),
            index % 2 == 0 ? std::vector<std::string>{"child"}
                           : std::vector<std::string>{}};
        require(broker.complete(result.completion),
            "ancestry lifecycle completion was rejected");
        if ((index + 1) % 100 == 0) {
            const auto metrics = broker.metrics();
            require(metrics.queued_requests == 0
                    && metrics.pending_requests == 0
                    && metrics.retained_input_bytes == 0,
                "ancestry lifecycle retained opaque input");
        }
    }
    const auto elapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - started).count();
    const auto resident_after = resident_bytes();
    const auto descriptors_after = descriptor_count();
    require(elapsed < 1.0, "10,000 ancestry bridge cycles exceeded one second");
    require(resident_before == 0
            || resident_after <= resident_before + 16U * 1024U * 1024U,
        "ancestry bridge exceeded the 16 MiB RSS budget");
    require(descriptors_before == 0 || descriptors_after <= descriptors_before + 2,
        "ancestry bridge leaked file descriptors");

    bool retirement_valid = true;
    for (std::uint64_t index = 0;
         index < WEBSCENE_FILE_GRANT_ANCESTRY_MAXIMUM_PENDING_OPERATIONS_V2;
         ++index) {
        request_fixture pending;
        pending.request.request_id = 20'000 + index;
        require(broker.queue(pending.request,
            [&](file_grant_ancestry_completion_data_v2&& value) {
                retirement_valid = retirement_valid
                    && value.status == WEBSCENE_FILE_GRANT_ANCESTRY_CANCELLED_V2
                    && value.components.empty();
                ++callbacks;
            }), "bounded ancestry request was rejected early");
    }
    request_fixture overflow;
    overflow.request.request_id = 30'000;
    require(!broker.queue(overflow.request, {}),
        "ancestry pending capacity was unbounded");
    broker.retire();
    const auto final = broker.metrics();
    require(retirement_valid && final.queued_requests == 0
            && final.pending_requests == 0
            && final.retained_input_bytes == 0
            && final.completed_requests == cycles + 4
            && final.retired_requests
                == WEBSCENE_FILE_GRANT_ANCESTRY_MAXIMUM_PENDING_OPERATIONS_V2,
        "ancestry retirement or metrics changed");

    for (std::uint64_t cycle = 0; cycle < 100; ++cycle) {
        file_grant_ancestry_broker_v2 lifecycle;
        request_fixture pending;
        pending.request.request_id = 40'000 + cycle;
        bool cancelled = false;
        require(lifecycle.queue(pending.request,
            [&](file_grant_ancestry_completion_data_v2&& value) {
                cancelled = value.status
                    == WEBSCENE_FILE_GRANT_ANCESTRY_CANCELLED_V2;
            }), "ancestry lifecycle admission failed");
        lifecycle.retire();
        const auto metrics = lifecycle.metrics();
        require(cancelled && metrics.queued_requests == 0
                && metrics.pending_requests == 0
                && metrics.retained_input_bytes == 0,
            "ancestry lifecycle retirement retained state");
    }

    std::cout << "WebScene file grant ancestry bridge passed; cycles="
              << cycles << " elapsedSeconds=" << elapsed
              << " rssDelta=" << static_cast<std::int64_t>(resident_after)
                    - static_cast<std::int64_t>(resident_before)
              << " fdDelta=" << static_cast<std::int64_t>(descriptors_after)
                    - static_cast<std::int64_t>(descriptors_before)
              << " retainedBytes=" << final.retained_input_bytes << '\n';
}
