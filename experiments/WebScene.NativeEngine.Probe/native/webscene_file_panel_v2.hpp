#pragma once

#include "webscene_native_engine.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace webscene_native {

inline constexpr std::size_t file_panel_maximum_pending_v2 = 16;
inline constexpr std::size_t file_panel_maximum_entries_v2 = 64;
inline constexpr std::size_t file_panel_maximum_filters_v2 = 32;
inline constexpr std::size_t file_panel_maximum_filter_values_v2 = 64;
inline constexpr std::size_t file_panel_maximum_metadata_bytes_v2 = 64 * 1024;
inline constexpr std::size_t file_panel_maximum_text_bytes_v2 = 4096;
inline constexpr std::size_t file_panel_maximum_token_bytes_v2 = 1024;
inline constexpr std::size_t file_panel_maximum_filter_text_bytes_v2 = 256;
inline constexpr std::size_t file_panel_maximum_extension_bytes_v2 = 64;
inline constexpr std::size_t file_panel_maximum_error_code_bytes_v2 = 128;
inline constexpr std::size_t file_grant_same_entry_maximum_pending_v2 = 64;

inline bool file_panel_valid_utf8_v2(std::string_view value) noexcept {
    for (std::size_t offset = 0; offset < value.size();) {
        const auto first = static_cast<unsigned char>(value[offset]);
        if (first == 0) return false;
        std::size_t length = 0;
        std::uint32_t scalar = 0;
        if (first <= 0x7f) { length = 1; scalar = first; }
        else if (first >= 0xc2 && first <= 0xdf) { length = 2; scalar = first & 0x1f; }
        else if (first >= 0xe0 && first <= 0xef) { length = 3; scalar = first & 0x0f; }
        else if (first >= 0xf0 && first <= 0xf4) { length = 4; scalar = first & 0x07; }
        else return false;
        if (offset + length > value.size()) return false;
        for (std::size_t index = 1; index < length; ++index) {
            const auto continuation = static_cast<unsigned char>(value[offset + index]);
            if ((continuation & 0xc0) != 0x80) return false;
            scalar = (scalar << 6) | (continuation & 0x3f);
        }
        if ((length == 3 && scalar < 0x800)
            || (length == 4 && scalar < 0x10000)
            || scalar > 0x10ffff
            || (scalar >= 0xd800 && scalar <= 0xdfff)) return false;
        offset += length;
    }
    return true;
}

inline std::optional<std::string_view> file_panel_string_view_v2(
    webscene_file_panel_string_v2 value, std::size_t maximum) noexcept {
    if (value.byte_count > maximum || (value.byte_count != 0 && value.data == nullptr))
        return std::nullopt;
    const std::string_view result{
        value.data == nullptr ? "" : value.data, value.byte_count};
    if (!file_panel_valid_utf8_v2(result)) return std::nullopt;
    return result;
}

inline bool file_panel_safe_display_name_v2(std::string_view value) noexcept {
    if (value.empty() || value == "." || value == "..") return false;
    return std::none_of(value.begin(), value.end(), [](unsigned char character) {
        return character < 0x20 || character == 0x7f
            || character == '/' || character == '\\';
    });
}

struct file_panel_filter_data_v2 final {
    webscene_file_panel_filter_v2 view{};
    std::string description;
    std::vector<std::string> mime_types;
    std::vector<std::string> extensions;
    std::vector<webscene_file_panel_string_v2> mime_views;
    std::vector<webscene_file_panel_string_v2> extension_views;

    void bind() {
        view.struct_size = sizeof(view);
        view.version = 2;
        view.description = {description.data(), description.size()};
        mime_views.clear();
        for (const auto& value : mime_types)
            mime_views.push_back({value.data(), value.size()});
        extension_views.clear();
        for (const auto& value : extensions)
            extension_views.push_back({value.data(), value.size()});
        view.mime_types = mime_views.empty() ? nullptr : mime_views.data();
        view.mime_type_count = mime_views.size();
        view.extensions = extension_views.empty() ? nullptr : extension_views.data();
        view.extension_count = extension_views.size();
    }
};

struct file_panel_request_lease_v2 final {
    webscene_file_panel_request_v2 view{};
    std::string title;
    std::string prompt;
    std::string suggested_name;
    std::vector<std::uint8_t> initial_location_token;
    std::vector<file_panel_filter_data_v2> filters;
    std::vector<webscene_file_panel_filter_v2> filter_views;
    std::size_t metadata_bytes{};

    void bind() {
        view.struct_size = sizeof(view);
        view.version = 2;
        view.title = {title.data(), title.size()};
        view.prompt = {prompt.data(), prompt.size()};
        view.suggested_name = {suggested_name.data(), suggested_name.size()};
        view.initial_location_token = {
            initial_location_token.empty() ? nullptr : initial_location_token.data(),
            initial_location_token.size()};
        filter_views.clear();
        for (auto& filter : filters) {
            filter.bind();
            filter_views.push_back(filter.view);
        }
        view.filters = filter_views.empty() ? nullptr : filter_views.data();
        view.filter_count = filter_views.size();
    }
};
static_assert(std::is_standard_layout_v<file_panel_request_lease_v2>);
static_assert(offsetof(file_panel_request_lease_v2, view) == 0);

struct file_panel_entry_data_v2 final {
    std::uint32_t kind{};
    std::uint32_t capabilities{};
    std::string display_name;
    std::vector<std::uint8_t> grant_id;
};

struct file_panel_completion_data_v2 final {
    std::uint64_t request_id{};
    std::uint32_t status{WEBSCENE_FILE_PANEL_ERROR_V2};
    std::vector<file_panel_entry_data_v2> entries;
    std::string error_code;
    std::string error_message;
};

using file_panel_completion_callback_v2 =
    std::function<void(file_panel_completion_data_v2&&)>;

struct file_panel_metrics_v2 final {
    std::size_t queued_requests{};
    std::size_t pending_requests{};
    std::size_t retained_metadata_bytes{};
    std::uint64_t completed_requests{};
    std::uint64_t retired_requests{};
    std::uint64_t rejected_operations{};
};

class file_panel_broker_v2 final {
public:
    bool queue(const webscene_file_panel_request_v2& source,
               file_panel_completion_callback_v2 callback) {
        auto lease = copy_request(source);
        if (!lease) {
            reject();
            return false;
        }
        const auto id = lease->view.request_id;
        std::lock_guard lock(mutex_);
        if (retiring_
            || pending_.size() >= file_panel_maximum_pending_v2
            || pending_.contains(id)) {
            ++rejected_operations_;
            return false;
        }
        pending_.emplace(id, pending_request{
            lease->view.kind, lease->view.flags,
            lease->view.maximum_selection_count, lease->metadata_bytes,
            std::move(callback)});
        retained_metadata_bytes_ += lease->metadata_bytes;
        queued_.push_back(std::move(lease));
        return true;
    }

    std::unique_ptr<file_panel_request_lease_v2> take() {
        std::lock_guard lock(mutex_);
        if (queued_.empty()) return {};
        auto result = std::move(queued_.front());
        queued_.pop_front();
        result->bind();
        return result;
    }

    bool complete(const webscene_file_panel_completion_v2& source) {
        file_panel_completion_callback_v2 callback;
        std::optional<file_panel_completion_data_v2> completion;
        {
            std::lock_guard lock(mutex_);
            const auto found = pending_.find(source.request_id);
            if (found == pending_.end()) {
                ++rejected_operations_;
                return false;
            }
            completion = copy_completion(source, found->second);
            if (!completion) {
                ++rejected_operations_;
                return false;
            }
            callback = std::move(found->second.callback);
            retained_metadata_bytes_ -= found->second.metadata_bytes;
            pending_.erase(found);
            ++completed_requests_;
        }
        if (callback) {
            try {
                callback(std::move(*completion));
            } catch (...) {
                // The completion has been consumed exactly once. Producer
                // failures cannot cross the C ABI or revive its authority.
            }
        }
        return true;
    }

    void retire() {
        std::vector<std::pair<std::uint64_t, file_panel_completion_callback_v2>> callbacks;
        {
            std::lock_guard lock(mutex_);
            if (retiring_) return;
            callbacks.reserve(pending_.size());
            retiring_ = true;
            for (auto& [id, request] : pending_)
                callbacks.emplace_back(id, std::move(request.callback));
            retired_requests_ += pending_.size();
            pending_.clear();
            queued_.clear();
            retained_metadata_bytes_ = 0;
        }
        for (auto& [id, callback] : callbacks) {
            if (!callback) continue;
            try {
                callback(file_panel_completion_data_v2{
                    id, WEBSCENE_FILE_PANEL_CANCELLED_V2, {}, {}, {}});
            } catch (...) {
                // One producer callback must not prevent remaining requests
                // from observing document retirement.
            }
        }
        std::lock_guard lock(mutex_);
        retiring_ = false;
    }

    file_panel_metrics_v2 metrics() const {
        std::lock_guard lock(mutex_);
        return {queued_.size(), pending_.size(), retained_metadata_bytes_,
                completed_requests_, retired_requests_, rejected_operations_};
    }

private:
    struct pending_request final {
        std::uint32_t kind{};
        std::uint32_t flags{};
        std::uint32_t maximum_selection_count{};
        std::size_t metadata_bytes{};
        file_panel_completion_callback_v2 callback;
    };

    void reject() {
        std::lock_guard lock(mutex_);
        ++rejected_operations_;
    }

    static std::unique_ptr<file_panel_request_lease_v2> copy_request(
        const webscene_file_panel_request_v2& source) {
        constexpr std::uint32_t supported_flags =
            WEBSCENE_FILE_PANEL_ALLOW_MULTIPLE_V2
            | WEBSCENE_FILE_PANEL_SHOW_HIDDEN_V2
            | WEBSCENE_FILE_PANEL_CAN_CREATE_DIRECTORIES_V2
            | WEBSCENE_FILE_PANEL_CONFIRM_OVERWRITE_V2
            | WEBSCENE_FILE_PANEL_EXCLUDE_ACCEPT_ALL_V2
            | WEBSCENE_FILE_PANEL_REQUEST_WRITE_V2;
        if (source.struct_size < sizeof(source) || source.version != 2
            || source.request_id == 0
            || source.reserved != 0
            || source.kind < WEBSCENE_FILE_PANEL_OPEN_FILE_V2
            || source.kind > WEBSCENE_FILE_PANEL_SAVE_FILE_V2
            || (source.flags & ~supported_flags) != 0
            || source.maximum_selection_count == 0
            || source.maximum_selection_count > file_panel_maximum_entries_v2
            || source.filter_count > file_panel_maximum_filters_v2
            || (source.filter_count != 0 && source.filters == nullptr)) return {};
        const bool multiple =
            (source.flags & WEBSCENE_FILE_PANEL_ALLOW_MULTIPLE_V2) != 0;
        if ((!multiple && source.maximum_selection_count != 1)
            || (source.kind != WEBSCENE_FILE_PANEL_OPEN_FILE_V2 && multiple)
            || (source.kind == WEBSCENE_FILE_PANEL_OPEN_DIRECTORY_V2
                && source.filter_count != 0)
            || (source.kind == WEBSCENE_FILE_PANEL_OPEN_DIRECTORY_V2
                && (source.flags & WEBSCENE_FILE_PANEL_EXCLUDE_ACCEPT_ALL_V2) != 0)
            || (source.kind != WEBSCENE_FILE_PANEL_OPEN_DIRECTORY_V2
                && (source.flags & WEBSCENE_FILE_PANEL_REQUEST_WRITE_V2) != 0)
            || (source.kind != WEBSCENE_FILE_PANEL_SAVE_FILE_V2
                && (source.flags & WEBSCENE_FILE_PANEL_CONFIRM_OVERWRITE_V2) != 0)
            || (source.kind == WEBSCENE_FILE_PANEL_OPEN_FILE_V2
                && (source.flags & WEBSCENE_FILE_PANEL_CAN_CREATE_DIRECTORIES_V2) != 0))
            return {};
        const auto title = file_panel_string_view_v2(
            source.title, file_panel_maximum_text_bytes_v2);
        const auto prompt = file_panel_string_view_v2(
            source.prompt, file_panel_maximum_text_bytes_v2);
        const auto suggested = file_panel_string_view_v2(
            source.suggested_name, file_panel_maximum_text_bytes_v2);
        if (!title || !prompt || !suggested
            || source.initial_location_token.byte_count
                > file_panel_maximum_token_bytes_v2
            || (source.initial_location_token.byte_count != 0
                && source.initial_location_token.data == nullptr)
            || (!suggested->empty() && !file_panel_safe_display_name_v2(*suggested))
            || (source.kind != WEBSCENE_FILE_PANEL_SAVE_FILE_V2
                && !suggested->empty())) return {};

        auto result = std::make_unique<file_panel_request_lease_v2>();
        result->view = source;
        result->title.assign(*title);
        result->prompt.assign(*prompt);
        result->suggested_name.assign(*suggested);
        if (source.initial_location_token.byte_count != 0)
            result->initial_location_token.assign(
                source.initial_location_token.data,
                source.initial_location_token.data
                    + source.initial_location_token.byte_count);
        std::size_t total_filter_values = 0;
        std::size_t bytes = result->title.size() + result->prompt.size()
            + result->suggested_name.size()
            + result->initial_location_token.size();
        for (std::size_t index = 0; index < source.filter_count; ++index) {
            const auto& filter = source.filters[index];
            if (filter.struct_size < sizeof(filter) || filter.version != 2
                || filter.mime_type_count > file_panel_maximum_filter_values_v2
                || filter.extension_count > file_panel_maximum_filter_values_v2
                || (filter.mime_type_count != 0 && filter.mime_types == nullptr)
                || (filter.extension_count != 0 && filter.extensions == nullptr)
                || filter.mime_type_count + filter.extension_count == 0)
                return {};
            total_filter_values += filter.mime_type_count + filter.extension_count;
            if (total_filter_values > file_panel_maximum_filter_values_v2) return {};
            const auto description = file_panel_string_view_v2(
                filter.description, file_panel_maximum_filter_text_bytes_v2);
            if (!description) return {};
            file_panel_filter_data_v2 copied;
            copied.description.assign(*description);
            bytes += copied.description.size();
            for (std::size_t item = 0; item < filter.mime_type_count; ++item) {
                const auto value = file_panel_string_view_v2(
                    filter.mime_types[item], file_panel_maximum_filter_text_bytes_v2);
                if (!value || value->empty() || value->find('/') == std::string_view::npos)
                    return {};
                copied.mime_types.emplace_back(*value);
                bytes += value->size();
            }
            for (std::size_t item = 0; item < filter.extension_count; ++item) {
                const auto value = file_panel_string_view_v2(
                    filter.extensions[item], file_panel_maximum_extension_bytes_v2);
                if (!value || value->size() < 2 || value->front() != '.'
                    || !file_panel_safe_display_name_v2(value->substr(1))) return {};
                copied.extensions.emplace_back(*value);
                bytes += value->size();
            }
            result->filters.push_back(std::move(copied));
        }
        if (bytes > file_panel_maximum_metadata_bytes_v2) return {};
        result->metadata_bytes = bytes;
        result->bind();
        return result;
    }

    static std::optional<file_panel_completion_data_v2> copy_completion(
        const webscene_file_panel_completion_v2& source,
        const pending_request& request) {
        if (source.struct_size < sizeof(source) || source.version != 2
            || source.request_id == 0 || source.reserved != 0
            || source.status > WEBSCENE_FILE_PANEL_ERROR_V2
            || source.entry_count > file_panel_maximum_entries_v2
            || (source.entry_count != 0 && source.entries == nullptr)) return std::nullopt;
        const auto code = file_panel_string_view_v2(
            source.error_code, file_panel_maximum_error_code_bytes_v2);
        const auto message = file_panel_string_view_v2(
            source.error_message, file_panel_maximum_text_bytes_v2);
        if (!code || !message) return std::nullopt;
        const bool success = source.status == WEBSCENE_FILE_PANEL_SUCCESS_V2;
        if ((success && (source.entry_count == 0 || !code->empty() || !message->empty()))
            || (!success && source.entry_count != 0)
            || (source.status == WEBSCENE_FILE_PANEL_CANCELLED_V2
                && (!code->empty() || !message->empty()))
            || ((source.status == WEBSCENE_FILE_PANEL_DENIED_V2
                 || source.status == WEBSCENE_FILE_PANEL_ERROR_V2)
                && code->empty())) return std::nullopt;
        if (source.entry_count > request.maximum_selection_count
            || ((request.flags & WEBSCENE_FILE_PANEL_ALLOW_MULTIPLE_V2) == 0
                && source.entry_count > 1)) return std::nullopt;

        file_panel_completion_data_v2 result;
        result.request_id = source.request_id;
        result.status = source.status;
        result.error_code.assign(*code);
        result.error_message.assign(*message);
        std::size_t bytes = result.error_code.size() + result.error_message.size();
        for (std::size_t index = 0; index < source.entry_count; ++index) {
            const auto& entry = source.entries[index];
            constexpr std::uint32_t supported_capabilities =
                WEBSCENE_FILE_PANEL_GRANT_READ_V2
                | WEBSCENE_FILE_PANEL_GRANT_WRITE_V2
                | WEBSCENE_FILE_PANEL_GRANT_ENUMERATE_V2
                | WEBSCENE_FILE_PANEL_GRANT_CREATE_V2
                | WEBSCENE_FILE_PANEL_GRANT_DELETE_V2;
            if (entry.struct_size < sizeof(entry) || entry.version != 2
                || entry.kind < WEBSCENE_FILE_PANEL_ENTRY_FILE_V2
                || entry.kind > WEBSCENE_FILE_PANEL_ENTRY_DIRECTORY_V2
                || (entry.capabilities & ~supported_capabilities) != 0
                || entry.grant_id.byte_count == 0
                || entry.grant_id.byte_count > file_panel_maximum_token_bytes_v2
                || entry.grant_id.data == nullptr) return std::nullopt;
            if ((request.kind == WEBSCENE_FILE_PANEL_OPEN_DIRECTORY_V2
                    && entry.kind != WEBSCENE_FILE_PANEL_ENTRY_DIRECTORY_V2)
                || (request.kind != WEBSCENE_FILE_PANEL_OPEN_DIRECTORY_V2
                    && entry.kind != WEBSCENE_FILE_PANEL_ENTRY_FILE_V2))
                return std::nullopt;
            if ((request.kind == WEBSCENE_FILE_PANEL_OPEN_FILE_V2
                    && (entry.capabilities & WEBSCENE_FILE_PANEL_GRANT_READ_V2) == 0)
                || (request.kind == WEBSCENE_FILE_PANEL_OPEN_DIRECTORY_V2
                    && (entry.capabilities
                        & WEBSCENE_FILE_PANEL_GRANT_ENUMERATE_V2) == 0)
                || (request.kind == WEBSCENE_FILE_PANEL_SAVE_FILE_V2
                    && (entry.capabilities & WEBSCENE_FILE_PANEL_GRANT_WRITE_V2) == 0))
                return std::nullopt;
            const auto name = file_panel_string_view_v2(
                entry.display_name, file_panel_maximum_text_bytes_v2);
            if (!name || !file_panel_safe_display_name_v2(*name)) return std::nullopt;
            bytes += name->size() + entry.grant_id.byte_count;
            if (bytes > file_panel_maximum_metadata_bytes_v2) return std::nullopt;
            file_panel_entry_data_v2 copied;
            copied.kind = entry.kind;
            copied.capabilities = entry.capabilities;
            copied.display_name.assign(*name);
            copied.grant_id.assign(
                entry.grant_id.data,
                entry.grant_id.data + entry.grant_id.byte_count);
            if (std::any_of(result.entries.begin(), result.entries.end(),
                    [&](const auto& existing) {
                        return existing.grant_id == copied.grant_id;
                    })) return std::nullopt;
            result.entries.push_back(std::move(copied));
        }
        return result;
    }

    mutable std::mutex mutex_;
    std::deque<std::unique_ptr<file_panel_request_lease_v2>> queued_;
    std::unordered_map<std::uint64_t, pending_request> pending_;
    std::size_t retained_metadata_bytes_{};
    std::uint64_t completed_requests_{};
    std::uint64_t retired_requests_{};
    std::uint64_t rejected_operations_{};
    bool retiring_{};
};

struct file_grant_same_entry_request_lease_v2 final {
    webscene_file_grant_same_entry_request_v2 view{};
    std::vector<std::uint8_t> first_grant_id;
    std::vector<std::uint8_t> second_grant_id;

    void bind() {
        view.struct_size = sizeof(view);
        view.version = 2;
        view.first_grant_id = {
            first_grant_id.empty() ? nullptr : first_grant_id.data(),
            first_grant_id.size()};
        view.second_grant_id = {
            second_grant_id.empty() ? nullptr : second_grant_id.data(),
            second_grant_id.size()};
    }
};
static_assert(std::is_standard_layout_v<file_grant_same_entry_request_lease_v2>);
static_assert(offsetof(file_grant_same_entry_request_lease_v2, view) == 0);

struct file_grant_same_entry_completion_data_v2 final {
    std::uint64_t request_id{};
    bool admitted{};
    bool same_entry{};
};

using file_grant_same_entry_completion_callback_v2 =
    std::function<void(file_grant_same_entry_completion_data_v2&&)>;

struct file_grant_same_entry_metrics_v2 final {
    std::size_t queued_requests{};
    std::size_t pending_requests{};
    std::size_t retained_token_bytes{};
    std::uint64_t completed_requests{};
    std::uint64_t retired_requests{};
    std::uint64_t rejected_operations{};
};

class file_grant_same_entry_broker_v2 final {
public:
    bool queue(const webscene_file_grant_same_entry_request_v2& source,
               file_grant_same_entry_completion_callback_v2 callback) {
        auto lease = copy_request(source);
        if (!lease) {
            reject();
            return false;
        }
        const auto id = lease->view.request_id;
        const auto bytes = lease->first_grant_id.size()
            + lease->second_grant_id.size();
        std::lock_guard lock(mutex_);
        if (retiring_
            || pending_.size() >= file_grant_same_entry_maximum_pending_v2
            || pending_.contains(id)) {
            ++rejected_operations_;
            return false;
        }
        pending_.emplace(id, pending_request{bytes, std::move(callback)});
        retained_token_bytes_ += bytes;
        queued_.push_back(std::move(lease));
        return true;
    }

    std::unique_ptr<file_grant_same_entry_request_lease_v2> take() {
        std::lock_guard lock(mutex_);
        if (queued_.empty()) return {};
        auto result = std::move(queued_.front());
        queued_.pop_front();
        result->bind();
        return result;
    }

    bool complete(const webscene_file_grant_same_entry_completion_v2& source) {
        file_grant_same_entry_completion_callback_v2 callback;
        file_grant_same_entry_completion_data_v2 completion;
        {
            std::lock_guard lock(mutex_);
            const auto found = pending_.find(source.request_id);
            if (found == pending_.end() || !valid_completion(source)) {
                ++rejected_operations_;
                return false;
            }
            callback = std::move(found->second.callback);
            retained_token_bytes_ -= found->second.token_bytes;
            pending_.erase(found);
            ++completed_requests_;
            completion = {source.request_id, source.admitted != 0,
                source.admitted != 0 && source.same_entry != 0};
        }
        if (callback) {
            try {
                callback(std::move(completion));
            } catch (...) {
                // The completion has been consumed exactly once.
            }
        }
        return true;
    }

    void retire() {
        std::vector<std::pair<std::uint64_t,
            file_grant_same_entry_completion_callback_v2>> callbacks;
        {
            std::lock_guard lock(mutex_);
            if (retiring_) return;
            retiring_ = true;
            callbacks.reserve(pending_.size());
            for (auto& [id, request] : pending_)
                callbacks.emplace_back(id, std::move(request.callback));
            retired_requests_ += pending_.size();
            pending_.clear();
            queued_.clear();
            retained_token_bytes_ = 0;
        }
        for (auto& [id, callback] : callbacks) {
            if (!callback) continue;
            try {
                callback(file_grant_same_entry_completion_data_v2{
                    id, false, false});
            } catch (...) {
                // One callback cannot prevent remaining requests from retiring.
            }
        }
        std::lock_guard lock(mutex_);
        retiring_ = false;
    }

    file_grant_same_entry_metrics_v2 metrics() const {
        std::lock_guard lock(mutex_);
        return {queued_.size(), pending_.size(), retained_token_bytes_,
            completed_requests_, retired_requests_, rejected_operations_};
    }

private:
    struct pending_request final {
        std::size_t token_bytes{};
        file_grant_same_entry_completion_callback_v2 callback;
    };

    void reject() {
        std::lock_guard lock(mutex_);
        ++rejected_operations_;
    }

    static std::unique_ptr<file_grant_same_entry_request_lease_v2> copy_request(
        const webscene_file_grant_same_entry_request_v2& source) {
        const auto valid_token = [](webscene_file_panel_token_v2 token) {
            return token.data != nullptr && token.byte_count != 0
                && token.byte_count <= file_panel_maximum_token_bytes_v2;
        };
        if (source.struct_size < sizeof(source) || source.version != 2
            || source.request_id == 0 || !valid_token(source.first_grant_id)
            || !valid_token(source.second_grant_id)) return {};
        auto result = std::make_unique<file_grant_same_entry_request_lease_v2>();
        result->view = source;
        result->first_grant_id.assign(source.first_grant_id.data,
            source.first_grant_id.data + source.first_grant_id.byte_count);
        result->second_grant_id.assign(source.second_grant_id.data,
            source.second_grant_id.data + source.second_grant_id.byte_count);
        result->bind();
        return result;
    }

    static bool valid_completion(
        const webscene_file_grant_same_entry_completion_v2& source) {
        return source.struct_size >= sizeof(source) && source.version == 2
            && source.request_id != 0 && source.admitted <= 1
            && source.same_entry <= 1
            && (source.admitted != 0 || source.same_entry == 0)
            && std::all_of(std::begin(source.reserved), std::end(source.reserved),
                [](std::uint8_t value) { return value == 0; });
    }

    mutable std::mutex mutex_;
    std::deque<std::unique_ptr<file_grant_same_entry_request_lease_v2>> queued_;
    std::unordered_map<std::uint64_t, pending_request> pending_;
    std::size_t retained_token_bytes_{};
    std::uint64_t completed_requests_{};
    std::uint64_t retired_requests_{};
    std::uint64_t rejected_operations_{};
    bool retiring_{};
};

inline constexpr std::size_t file_grant_read_maximum_pending_v2 = 64;

struct file_grant_read_request_lease_v2 final {
    webscene_file_grant_read_request_v2 view{};
    std::vector<std::uint8_t> grant_id;

    void bind() {
        view.struct_size = sizeof(view);
        view.version = 2;
        view.grant_id = {
            grant_id.empty() ? nullptr : grant_id.data(), grant_id.size()};
    }
};
static_assert(std::is_standard_layout_v<file_grant_read_request_lease_v2>);
static_assert(offsetof(file_grant_read_request_lease_v2, view) == 0);

struct file_grant_read_completion_data_v2 final {
    std::uint64_t request_id{};
    std::uint32_t status{WEBSCENE_FILE_GRANT_READ_IO_ERROR_V2};
    std::uint64_t byte_count{};
    std::int64_t modification_time_ns{};
    std::uint64_t offset{};
    std::vector<std::uint8_t> bytes;
    bool eof{};
};

using file_grant_read_completion_callback_v2 =
    std::function<void(file_grant_read_completion_data_v2&&)>;

struct file_grant_read_metrics_v2 final {
    std::size_t queued_requests{};
    std::size_t pending_requests{};
    std::size_t retained_token_bytes{};
    std::uint64_t completed_requests{};
    std::uint64_t retired_requests{};
    std::uint64_t copied_completion_bytes{};
    std::uint64_t rejected_operations{};
};

class file_grant_read_broker_v2 final {
public:
    bool queue(const webscene_file_grant_read_request_v2& source,
               file_grant_read_completion_callback_v2 callback) {
        auto lease = copy_request(source);
        if (!lease) {
            reject();
            return false;
        }
        const auto id = lease->view.request_id;
        const auto token_bytes = lease->grant_id.size();
        std::lock_guard lock(mutex_);
        if (retiring_
            || pending_.size() >= file_grant_read_maximum_pending_v2
            || pending_.contains(id)) {
            ++rejected_operations_;
            return false;
        }
        pending_.emplace(id, pending_request{lease->view.offset,
            lease->view.maximum_bytes, token_bytes, std::move(callback)});
        retained_token_bytes_ += token_bytes;
        queued_.push_back(std::move(lease));
        return true;
    }

    std::unique_ptr<file_grant_read_request_lease_v2> take() {
        std::lock_guard lock(mutex_);
        if (queued_.empty()) return {};
        auto result = std::move(queued_.front());
        queued_.pop_front();
        result->bind();
        return result;
    }

    bool complete(const webscene_file_grant_read_completion_v2& source) {
        file_grant_read_completion_callback_v2 callback;
        std::optional<file_grant_read_completion_data_v2> completion;
        {
            std::lock_guard lock(mutex_);
            const auto found = pending_.find(source.request_id);
            if (found == pending_.end()) {
                ++rejected_operations_;
                return false;
            }
            completion = copy_completion(source, found->second);
            if (!completion) {
                ++rejected_operations_;
                return false;
            }
            callback = std::move(found->second.callback);
            retained_token_bytes_ -= found->second.token_bytes;
            pending_.erase(found);
            copied_completion_bytes_ += completion->bytes.size();
            ++completed_requests_;
        }
        if (callback) {
            try {
                callback(std::move(*completion));
            } catch (...) {
                // The completion has been consumed exactly once.
            }
        }
        return true;
    }

    void retire() {
        std::vector<std::pair<std::uint64_t, pending_request>> pending;
        {
            std::lock_guard lock(mutex_);
            if (retiring_) return;
            retiring_ = true;
            pending.reserve(pending_.size());
            for (auto& [id, request] : pending_)
                pending.emplace_back(id, std::move(request));
            retired_requests_ += pending_.size();
            pending_.clear();
            queued_.clear();
            retained_token_bytes_ = 0;
        }
        for (auto& [id, request] : pending) {
            if (!request.callback) continue;
            try {
                request.callback(file_grant_read_completion_data_v2{
                    id, WEBSCENE_FILE_GRANT_READ_CANCELLED_V2,
                    0, 0, request.offset, {}, false});
            } catch (...) {
                // One callback cannot prevent remaining requests from retiring.
            }
        }
        std::lock_guard lock(mutex_);
        retiring_ = false;
    }

    file_grant_read_metrics_v2 metrics() const {
        std::lock_guard lock(mutex_);
        return {queued_.size(), pending_.size(), retained_token_bytes_,
            completed_requests_, retired_requests_, copied_completion_bytes_,
            rejected_operations_};
    }

private:
    struct pending_request final {
        std::uint64_t offset{};
        std::uint32_t maximum_bytes{};
        std::size_t token_bytes{};
        file_grant_read_completion_callback_v2 callback;
    };

    void reject() {
        std::lock_guard lock(mutex_);
        ++rejected_operations_;
    }

    static std::unique_ptr<file_grant_read_request_lease_v2> copy_request(
        const webscene_file_grant_read_request_v2& source) {
        if (source.struct_size < sizeof(source) || source.version != 2
            || source.request_id == 0 || source.reserved != 0
            || source.grant_id.data == nullptr
            || source.grant_id.byte_count == 0
            || source.grant_id.byte_count > file_panel_maximum_token_bytes_v2
            || source.maximum_bytes == 0
            || source.maximum_bytes > WEBSCENE_FILE_GRANT_READ_MAXIMUM_BYTES_V2)
            return {};
        auto result = std::make_unique<file_grant_read_request_lease_v2>();
        result->view = source;
        result->grant_id.assign(source.grant_id.data,
            source.grant_id.data + source.grant_id.byte_count);
        result->bind();
        return result;
    }

    static bool reserved_bytes_clear(
        const webscene_file_grant_read_completion_v2& source) {
        return std::all_of(std::begin(source.reserved_bytes),
            std::end(source.reserved_bytes),
            [](std::uint8_t value) { return value == 0; });
    }

    static std::optional<file_grant_read_completion_data_v2> copy_completion(
        const webscene_file_grant_read_completion_v2& source,
        const pending_request& pending) {
        if (source.struct_size < sizeof(source) || source.version != 2
            || source.request_id == 0 || source.reserved != 0
            || source.status > WEBSCENE_FILE_GRANT_READ_IO_ERROR_V2
            || source.offset != pending.offset || source.eof > 1
            || !reserved_bytes_clear(source)) return std::nullopt;
        file_grant_read_completion_data_v2 result;
        result.request_id = source.request_id;
        result.status = source.status;
        result.offset = source.offset;
        if (source.status != WEBSCENE_FILE_GRANT_READ_SUCCESS_V2) {
            const auto empty_metadata_header =
                (source.metadata.struct_size == 0 && source.metadata.version == 0)
                || (source.metadata.struct_size >= sizeof(source.metadata)
                    && source.metadata.version == 2);
            if (!empty_metadata_header || source.metadata.byte_count != 0
                || source.metadata.modification_time_ns != 0
                || source.metadata.kind != 0 || source.metadata.reserved != 0
                || source.data != nullptr || source.byte_count != 0
                || source.eof != 0) return std::nullopt;
            return result;
        }
        if (source.metadata.struct_size < sizeof(source.metadata)
            || source.metadata.version != 2
            || source.metadata.kind != WEBSCENE_FILE_PANEL_ENTRY_FILE_V2
            || source.metadata.reserved != 0
            || source.byte_count > pending.maximum_bytes
            || (source.byte_count == 0) != (source.data == nullptr))
            return std::nullopt;
        if (source.offset > source.metadata.byte_count) {
            if (source.byte_count != 0 || source.eof == 0) return std::nullopt;
        } else {
            const auto remaining = source.metadata.byte_count - source.offset;
            if (source.byte_count > remaining) return std::nullopt;
            const auto reached_end = source.byte_count == remaining;
            if ((source.eof != 0) != reached_end
                || (source.eof == 0 && source.byte_count == 0))
                return std::nullopt;
        }
        result.byte_count = source.metadata.byte_count;
        result.modification_time_ns = source.metadata.modification_time_ns;
        result.eof = source.eof != 0;
        if (source.byte_count != 0)
            result.bytes.assign(source.data, source.data + source.byte_count);
        return result;
    }

    mutable std::mutex mutex_;
    std::deque<std::unique_ptr<file_grant_read_request_lease_v2>> queued_;
    std::unordered_map<std::uint64_t, pending_request> pending_;
    std::size_t retained_token_bytes_{};
    std::uint64_t completed_requests_{};
    std::uint64_t retired_requests_{};
    std::uint64_t copied_completion_bytes_{};
    std::uint64_t rejected_operations_{};
    bool retiring_{};
};

} // namespace webscene_native
