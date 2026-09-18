#include "webscene_file_panel_v2.hpp"

#include <array>
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
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
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
    std::vector<std::uint8_t> grant{1, 0, 2, 255};
    std::array<std::uint8_t, WEBSCENE_FILE_GRANT_DURABLE_LOCATOR_BYTES_V2>
        locator{};
    std::string partition{"profile-A"};
    std::string origin{"https://example.test"};
    webscene_file_grant_durable_request_v2 request{};

    request_fixture(std::uint64_t id, std::uint32_t action) {
        for (std::size_t index = 0; index < locator.size(); ++index)
            locator[index] = static_cast<std::uint8_t>(index + 1U);
        request = {sizeof(request), 2U, id, action, 0U,
            action == WEBSCENE_FILE_GRANT_DURABLE_EXPORT_V2
                ? webscene_file_panel_token_v2{grant.data(), grant.size()}
                : webscene_file_panel_token_v2{},
            action == WEBSCENE_FILE_GRANT_DURABLE_EXPORT_V2
                ? webscene_file_panel_token_v2{}
                : webscene_file_panel_token_v2{locator.data(), locator.size()},
            {partition.data(), partition.size()}, {origin.data(), origin.size()}};
    }
};

webscene_file_grant_durable_completion_v2 completion(
    std::uint64_t id, std::uint32_t action,
    const std::array<std::uint8_t,
        WEBSCENE_FILE_GRANT_DURABLE_LOCATOR_BYTES_V2>& locator,
    const std::vector<std::uint8_t>& grant,
    std::string_view name)
{
    return {sizeof(webscene_file_grant_durable_completion_v2), 2U, id,
        WEBSCENE_FILE_GRANT_DURABLE_SUCCESS_V2, action,
        {locator.data(), locator.size()},
        {grant.empty() ? nullptr : grant.data(), grant.size()},
        action == WEBSCENE_FILE_GRANT_DURABLE_REVOKE_V2
            ? 0U : WEBSCENE_FILE_PANEL_ENTRY_FILE_V2,
        action == WEBSCENE_FILE_GRANT_DURABLE_REVOKE_V2
            ? 0U : WEBSCENE_FILE_PANEL_GRANT_READ_V2,
        {name.empty() ? nullptr : name.data(), name.size()}, {}};
}
} // namespace

int main() {
    using namespace webscene_native;
    file_grant_durable_broker_v2 broker;
    std::uint64_t callbacks{};
    file_grant_durable_completion_data_v2 observed;
    request_fixture exported{1, WEBSCENE_FILE_GRANT_DURABLE_EXPORT_V2};
    require(broker.queue(exported.request,
        [&](file_grant_durable_completion_data_v2&& value) {
            ++callbacks;
            observed = std::move(value);
        }), "valid durable export was rejected");
    auto lease = broker.take();
    require(lease && lease->view.grant_id.byte_count == exported.grant.size()
            && lease->view.storage_partition.byte_count
                == exported.partition.size(),
        "durable lease lost copied binding data");
    exported.grant.assign(16, 42);
    exported.partition.assign(16, 'x');
    require(lease->view.grant_id.byte_count == 4
            && lease->view.grant_id.data[0] == 1
            && std::string_view(lease->view.storage_partition.data,
                lease->view.storage_partition.byte_count) == "profile-A",
        "durable lease retained producer storage");
    lease.reset();
    const std::vector<std::uint8_t> empty_grant;
    auto exported_completion = completion(1,
        WEBSCENE_FILE_GRANT_DURABLE_EXPORT_V2, exported.locator,
        empty_grant, "file🙂.txt");
    require(broker.complete(exported_completion) && callbacks == 1
            && observed.locator.size()
                == WEBSCENE_FILE_GRANT_DURABLE_LOCATOR_BYTES_V2,
        "durable export completion was lost");
    require(!broker.complete(exported_completion),
        "durable request completed more than once");

    request_fixture malformed{2, WEBSCENE_FILE_GRANT_DURABLE_RESTORE_V2};
    malformed.request.locator.byte_count--;
    require(!broker.queue(malformed.request, {}),
        "short durable locator crossed the ABI");
    malformed.request.locator.byte_count++;
    malformed.request.storage_partition = {};
    require(!broker.queue(malformed.request, {}),
        "empty durable partition crossed the ABI");

    constexpr std::uint64_t cycles = 10'000;
    const auto descriptors_before = descriptor_count();
    const auto resident_before = resident_bytes();
    const auto started = std::chrono::steady_clock::now();
    for (std::uint64_t index = 0; index < cycles; ++index) {
        const auto action = index % 3 == 0
            ? WEBSCENE_FILE_GRANT_DURABLE_EXPORT_V2
            : index % 3 == 1 ? WEBSCENE_FILE_GRANT_DURABLE_RESTORE_V2
                             : WEBSCENE_FILE_GRANT_DURABLE_REVOKE_V2;
        request_fixture current{100 + index, action};
        require(broker.queue(current.request,
            [&](file_grant_durable_completion_data_v2&&) { ++callbacks; }),
            "durable lifecycle admission failed");
        auto current_lease = broker.take();
        require(current_lease && current_lease->view.request_id == 100 + index,
            "durable lifecycle lease was lost");
        std::vector<std::uint8_t> restored_grant = action
                == WEBSCENE_FILE_GRANT_DURABLE_RESTORE_V2
            ? std::vector<std::uint8_t>{9, 8, 7} : std::vector<std::uint8_t>{};
        const auto result = completion(100 + index, action, current.locator,
            restored_grant,
            action == WEBSCENE_FILE_GRANT_DURABLE_REVOKE_V2
                ? std::string_view{} : std::string_view{"file.txt"});
        require(broker.complete(result), "durable lifecycle completion failed");
    }
    const auto elapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - started).count();
    const auto resident_after = resident_bytes();
    const auto descriptors_after = descriptor_count();
    require(elapsed < 1.0, "10,000 durable broker cycles exceeded one second");
    require(resident_before == 0
            || resident_after <= resident_before + 16U * 1024U * 1024U,
        "durable broker exceeded the 16 MiB RSS budget");
    require(descriptors_before == 0 || descriptors_after <= descriptors_before + 2,
        "durable broker leaked file descriptors");

    bool retired = true;
    for (std::uint64_t index = 0;
         index < WEBSCENE_FILE_GRANT_DURABLE_MAXIMUM_PENDING_OPERATIONS_V2;
         ++index) {
        request_fixture pending{20'000 + index,
            WEBSCENE_FILE_GRANT_DURABLE_RESTORE_V2};
        require(broker.queue(pending.request,
            [&](file_grant_durable_completion_data_v2&& value) {
                retired = retired
                    && value.status == WEBSCENE_FILE_GRANT_DURABLE_CANCELLED_V2;
            }), "durable pending bound rejected early");
    }
    request_fixture overflow{30'000, WEBSCENE_FILE_GRANT_DURABLE_RESTORE_V2};
    require(!broker.queue(overflow.request, {}),
        "durable pending bound was not enforced");
    broker.retire();
    const auto metrics = broker.metrics();
    require(retired && metrics.queued_requests == 0
            && metrics.pending_requests == 0
            && metrics.retained_input_bytes == 0,
        "durable retirement retained authority data");

    std::cout << "WebScene durable file grant bridge passed; cycles=" << cycles
              << " elapsedSeconds=" << elapsed
              << " rssDelta=" << static_cast<std::int64_t>(resident_after)
                    - static_cast<std::int64_t>(resident_before)
              << " fdDelta=" << static_cast<std::int64_t>(descriptors_after)
                    - static_cast<std::int64_t>(descriptors_before)
              << " retainedBytes=" << metrics.retained_input_bytes << '\n';
}
