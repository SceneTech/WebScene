#include "webscene_file_panel_v2.hpp"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

webscene_file_panel_string_v2 text(std::string_view value) {
    return {value.data(), value.size()};
}

struct request_fixture final {
    std::string title{"Open drawing"};
    std::string prompt{"Choose"};
    std::string suggested;
    std::vector<std::uint8_t> initial{0, 1, 2, 255};
    std::string description{"Drawings"};
    std::string mime{"application/json"};
    std::string extension{".json"};
    webscene_file_panel_string_v2 mime_view{text(mime)};
    webscene_file_panel_string_v2 extension_view{text(extension)};
    webscene_file_panel_filter_v2 filter{
        sizeof(filter), 2, text(description), &mime_view, 1,
        &extension_view, 1};
    webscene_file_panel_request_v2 request{
        sizeof(request), 2, 1, WEBSCENE_FILE_PANEL_OPEN_FILE_V2,
        WEBSCENE_FILE_PANEL_ALLOW_MULTIPLE_V2, 2, 0,
        text(title), text(prompt), text(suggested),
        {initial.data(), initial.size()}, &filter, 1};
};

struct completion_fixture final {
    std::string name{"café.json"};
    std::vector<std::uint8_t> grant{9, 0, 8, 255};
    webscene_file_panel_entry_v2 entry{
        sizeof(entry), 2, WEBSCENE_FILE_PANEL_ENTRY_FILE_V2,
        WEBSCENE_FILE_PANEL_GRANT_READ_V2
            | WEBSCENE_FILE_PANEL_GRANT_WRITE_V2,
        text(name), {grant.data(), grant.size()}};
    webscene_file_panel_completion_v2 completion{
        sizeof(completion), 2, 1, WEBSCENE_FILE_PANEL_SUCCESS_V2, 0,
        &entry, 1, {}, {}};
};

} // namespace

int main() {
    using namespace webscene_native;
    file_panel_broker_v2 broker;
    request_fixture request;
    completion_fixture result;
    std::uint64_t callback_count = 0;
    std::uint64_t callback_checksum = 0;
    bool callbacks_valid = true;
    bool retirement_callbacks_valid = true;

    require(broker.queue(request.request, [&](file_panel_completion_data_v2&& value) {
        ++callback_count;
        callback_checksum ^= value.request_id;
        callbacks_valid = callbacks_valid
            && value.status == WEBSCENE_FILE_PANEL_SUCCESS_V2
            && value.entries.size() == 1
            && value.entries.front().display_name == "café.json"
            && value.entries.front().grant_id == result.grant;
        request_fixture reentered;
        reentered.request.request_id = 2;
        reentered.request.flags = 0;
        reentered.request.maximum_selection_count = 1;
        callbacks_valid = callbacks_valid && broker.queue(reentered.request,
            [&](file_panel_completion_data_v2&& value) {
                callbacks_valid = callbacks_valid
                    && value.status == WEBSCENE_FILE_PANEL_CANCELLED_V2;
                ++callback_count;
            });
    }), "valid open-file request was rejected");
    auto lease = broker.take();
    require(lease && lease->view.version == 2 && lease->view.request_id == 1
                && lease->view.kind == WEBSCENE_FILE_PANEL_OPEN_FILE_V2
                && lease->view.flags == WEBSCENE_FILE_PANEL_ALLOW_MULTIPLE_V2
                && lease->view.maximum_selection_count == 2
                && lease->view.filter_count == 1
                && lease->view.filters[0].extension_count == 1
                && std::string_view(lease->view.prompt.data,
                       lease->view.prompt.byte_count) == "Choose"
                && lease->view.initial_location_token.byte_count == 4,
            "leased request lost typed options");
    request.title.assign(5000, 'x');
    require(std::string_view(lease->view.title.data, lease->view.title.byte_count)
                == "Open drawing",
            "leased request retained producer string storage");
    lease.reset();

    auto malformed = result.completion;
    malformed.request_id = 99;
    require(!broker.complete(malformed), "unknown completion ID was accepted");
    malformed = result.completion;
    malformed.entry_count = 0;
    require(!broker.complete(malformed), "empty successful result was accepted");
    malformed = result.completion;
    malformed.status = WEBSCENE_FILE_PANEL_CANCELLED_V2;
    require(!broker.complete(malformed), "cancelled result carried entries");
    malformed = result.completion;
    malformed.version = 1;
    require(!broker.complete(malformed), "wrong completion version was accepted");
    malformed = result.completion;
    malformed.struct_size = sizeof(malformed) - 1;
    require(!broker.complete(malformed), "short completion structure was accepted");
    result.entry.grant_id.byte_count = file_panel_maximum_token_bytes_v2 + 1;
    require(!broker.complete(result.completion),
            "oversized opaque grant identifier was accepted");
    result.entry.grant_id = {result.grant.data(), result.grant.size()};
    result.entry.capabilities = WEBSCENE_FILE_PANEL_GRANT_WRITE_V2;
    require(!broker.complete(result.completion),
            "open-file result omitted read authority");
    result.entry.capabilities = WEBSCENE_FILE_PANEL_GRANT_READ_V2
        | WEBSCENE_FILE_PANEL_GRANT_WRITE_V2;
    auto duplicate = result.entry;
    webscene_file_panel_entry_v2 duplicate_entries[]{result.entry, duplicate};
    malformed = result.completion;
    malformed.entries = duplicate_entries;
    malformed.entry_count = 2;
    require(!broker.complete(malformed), "duplicate grant identifiers were accepted");
    require(broker.complete(result.completion), "valid completion was rejected");
    require(callback_count == 1 && !broker.complete(result.completion),
            "request completed more than once");

    auto reentrant = broker.take();
    require(reentrant && reentrant->view.request_id == 2,
            "reentrant request was not queued");
    reentrant.reset();
    webscene_file_panel_completion_v2 cancelled{
        sizeof(cancelled), 2, 2, WEBSCENE_FILE_PANEL_CANCELLED_V2, 0,
        nullptr, 0, {}, {}};
    require(broker.complete(cancelled), "explicit cancellation was rejected");

    request_fixture directory;
    directory.request.request_id = 10;
    directory.request.kind = WEBSCENE_FILE_PANEL_OPEN_DIRECTORY_V2;
    directory.request.filters = nullptr;
    directory.request.filter_count = 0;
    require(broker.queue(directory.request,
        [&](file_panel_completion_data_v2&& value) {
            callbacks_valid = callbacks_valid
                && value.status == WEBSCENE_FILE_PANEL_SUCCESS_V2
                && value.entries.front().kind
                    == WEBSCENE_FILE_PANEL_ENTRY_DIRECTORY_V2;
            ++callback_count;
        }), "valid open-directory request was rejected");
    require(static_cast<bool>(broker.take()), "directory request was not leased");
    completion_fixture directory_result;
    directory_result.completion.request_id = 10;
    directory_result.entry.kind = WEBSCENE_FILE_PANEL_ENTRY_DIRECTORY_V2;
    directory_result.entry.capabilities =
        WEBSCENE_FILE_PANEL_GRANT_READ_V2
        | WEBSCENE_FILE_PANEL_GRANT_ENUMERATE_V2;
    require(broker.complete(directory_result.completion),
            "valid directory result was rejected");

    request_fixture save;
    save.request.request_id = 11;
    save.request.kind = WEBSCENE_FILE_PANEL_SAVE_FILE_V2;
    save.request.flags = WEBSCENE_FILE_PANEL_CAN_CREATE_DIRECTORIES_V2
        | WEBSCENE_FILE_PANEL_CONFIRM_OVERWRITE_V2;
    save.request.maximum_selection_count = 1;
    save.suggested = "drawing.json";
    save.request.suggested_name = text(save.suggested);
    require(broker.queue(save.request,
        [&](file_panel_completion_data_v2&& value) {
            callbacks_valid = callbacks_valid
                && value.status == WEBSCENE_FILE_PANEL_SUCCESS_V2
                && (value.entries.front().capabilities
                    & WEBSCENE_FILE_PANEL_GRANT_WRITE_V2) != 0;
            ++callback_count;
        }), "valid save-file request was rejected");
    require(static_cast<bool>(broker.take()), "save request was not leased");
    completion_fixture save_result;
    save_result.completion.request_id = 11;
    require(broker.complete(save_result.completion),
            "valid save result was rejected");

    for (const auto [id, status] : {
             std::pair<std::uint64_t, std::uint32_t>{12, WEBSCENE_FILE_PANEL_DENIED_V2},
             {13, WEBSCENE_FILE_PANEL_ERROR_V2}}) {
        request_fixture failed;
        failed.request.request_id = id;
        failed.request.flags = 0;
        failed.request.maximum_selection_count = 1;
        require(broker.queue(failed.request,
            [&, status](file_panel_completion_data_v2&& value) {
                callbacks_valid = callbacks_valid
                    && value.status == status
                    && value.error_code == "permission-denied";
                ++callback_count;
            }), "denial/error request was rejected");
        require(static_cast<bool>(broker.take()), "denial/error request was not leased");
        std::string code{"permission-denied"};
        std::string message{"Access unavailable"};
        webscene_file_panel_completion_v2 failed_result{
            sizeof(failed_result), 2, id, status, 0, nullptr, 0,
            text(code), text(message)};
        require(broker.complete(failed_result),
                "denial/error completion was rejected");
    }

    request_fixture invalid;
    invalid.request.request_id = 3;
    invalid.request.struct_size = sizeof(invalid.request) - 1;
    require(!broker.queue(invalid.request, {}), "short request structure was accepted");
    invalid.request.struct_size = sizeof(invalid.request);
    invalid.request.version = 1;
    require(!broker.queue(invalid.request, {}), "wrong request version was accepted");
    invalid.request.version = 2;
    invalid.request.title = {"\xc0\xaf", 2};
    require(!broker.queue(invalid.request, {}), "malformed UTF-8 was accepted");
    invalid.request.title = text(invalid.title);
    invalid.request.title.byte_count = file_panel_maximum_text_bytes_v2 + 1;
    require(!broker.queue(invalid.request, {}), "oversized title was accepted");
    invalid.request.title = text(invalid.title);
    invalid.request.initial_location_token.byte_count =
        file_panel_maximum_token_bytes_v2 + 1;
    require(!broker.queue(invalid.request, {}),
            "oversized initial-location token was accepted");
    invalid.request.initial_location_token = {
        invalid.initial.data(), invalid.initial.size()};
    invalid.request.flags = 1U << 31U;
    require(!broker.queue(invalid.request, {}), "unknown option flag was accepted");
    invalid.request.flags = WEBSCENE_FILE_PANEL_ALLOW_MULTIPLE_V2;
    invalid.request.kind = WEBSCENE_FILE_PANEL_SAVE_FILE_V2;
    require(!broker.queue(invalid.request, {}), "multi-select save was accepted");
    invalid.request.kind = WEBSCENE_FILE_PANEL_OPEN_DIRECTORY_V2;
    require(!broker.queue(invalid.request, {}), "directory filters were accepted");

    const auto started = std::chrono::steady_clock::now();
    constexpr std::uint64_t cycles = 10000;
    for (std::uint64_t index = 0; index < cycles; ++index) {
        request_fixture cycle;
        cycle.request.request_id = 100 + index;
        cycle.request.flags = 0;
        cycle.request.maximum_selection_count = 1;
        require(broker.queue(cycle.request,
            [&](file_panel_completion_data_v2&& value) {
                ++callback_count;
                callback_checksum ^= value.request_id;
            }), "bounded lifecycle request was rejected");
        auto current = broker.take();
        require(current && current->view.request_id == 100 + index,
                "bounded lifecycle request lease was lost");
        current.reset();
        completion_fixture completed;
        completed.completion.request_id = 100 + index;
        require(broker.complete(completed.completion),
                "bounded lifecycle completion was rejected");
        if ((index + 1) % 100 == 0) {
            const auto metrics = broker.metrics();
            require(metrics.queued_requests == 0
                        && metrics.pending_requests == 0
                        && metrics.retained_metadata_bytes == 0,
                    "100-cycle boundary retained request metadata");
        }
    }
    const auto elapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - started).count();
    require(elapsed < 1.0, "10,000 panel ABI cycles exceeded one second");

    for (std::uint64_t index = 0; index < file_panel_maximum_pending_v2; ++index) {
        request_fixture pending;
        pending.request.request_id = 20000 + index;
        pending.request.flags = 0;
        pending.request.maximum_selection_count = 1;
        require(broker.queue(pending.request,
            [&](file_panel_completion_data_v2&& value) {
                retirement_callbacks_valid = retirement_callbacks_valid
                    && value.status == WEBSCENE_FILE_PANEL_CANCELLED_V2
                    && value.entries.empty()
                    && value.error_code.empty()
                    && value.error_message.empty();
                request_fixture stale_reentry;
                stale_reentry.request.request_id = value.request_id + 50000;
                stale_reentry.request.flags = 0;
                stale_reentry.request.maximum_selection_count = 1;
                retirement_callbacks_valid = retirement_callbacks_valid
                    && !broker.queue(stale_reentry.request, {});
                ++callback_count;
                if (value.request_id == 20000)
                    throw std::runtime_error("simulated retiring producer");
            }), "bounded pending request was rejected early");
    }
    request_fixture overflow;
    overflow.request.request_id = 30000;
    overflow.request.flags = 0;
    overflow.request.maximum_selection_count = 1;
    require(!broker.queue(overflow.request, {}),
            "pending request capacity was unbounded");
    broker.retire();
    const auto final = broker.metrics();
    require(final.queued_requests == 0 && final.pending_requests == 0
                && final.retained_metadata_bytes == 0
                && final.retired_requests == file_panel_maximum_pending_v2
                && callbacks_valid
                && retirement_callbacks_valid
                && callback_count == cycles + 6 + file_panel_maximum_pending_v2
                && callback_checksum != 0,
            "retirement or completion metrics changed");
    std::cout << "WebScene file panel v2 contracts passed; cycles=" << cycles
              << " elapsedSeconds=" << elapsed
              << " completed=" << final.completed_requests
              << " retired=" << final.retired_requests
              << " retainedBytes=" << final.retained_metadata_bytes << '\n';
}
