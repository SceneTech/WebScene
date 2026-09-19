#include "webscene_profile_storage.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <fstream>
#include <limits>
#include <system_error>
#include <unordered_set>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace webscene_native {
namespace {

constexpr std::array<char, 8> profile_magic{'W', 'S', 'P', 'R', 'O', 'F', '0', '1'};
constexpr uint32_t profile_schema = 2U;
constexpr size_t maximum_identity_bytes = 4096U;
constexpr size_t maximum_cookie_count = 256U;
constexpr size_t maximum_origin_count = 4096U;
constexpr size_t maximum_entry_count = 65536U;
constexpr size_t maximum_cookie_bytes = 4096U;
constexpr size_t maximum_storage_string_bytes = 16U * 1024U * 1024U;
constexpr size_t maximum_form_document_count = 32U;
constexpr size_t maximum_form_control_count = 512U;
constexpr size_t maximum_form_identity_bytes = 128U;
constexpr size_t maximum_form_value_bytes = 64U * 1024U;
constexpr size_t maximum_form_selected_count = 256U;
constexpr size_t maximum_form_state_bytes = 512U * 1024U;
constexpr uint64_t minimum_quota_bytes = 1024U * 1024U;
constexpr uint64_t default_quota_bytes = 256U * 1024U * 1024U;

uint64_t hash_bytes(const void* data, size_t length) noexcept
{
    auto value = UINT64_C(14695981039346656037);
    const auto* bytes = static_cast<const uint8_t*>(data);
    for (size_t index = 0; index < length; ++index) {
        value ^= bytes[index];
        value *= UINT64_C(1099511628211);
    }
    return value;
}

std::string digest_name(const std::string& value)
{
    constexpr char hex[] = "0123456789abcdef";
    const auto first = hash_bytes(value.data(), value.size());
    auto second = hash_bytes(&first, sizeof(first));
    second ^= static_cast<uint64_t>(value.size()) * UINT64_C(0x9e3779b97f4a7c15);
    std::string result(32U, '0');
    for (size_t index = 0; index < 16U; ++index) {
        const auto shift = static_cast<unsigned>((15U - index) * 4U);
        const auto source = index < 8U ? first : second;
        result[index] = hex[(source >> shift) & 0xfU];
        result[index + 16U] = hex[(source >> ((index * 4U) & 63U)) & 0xfU];
    }
    return result;
}

template<typename Value>
bool append_scalar(std::vector<uint8_t>& output, const Value& value)
{
    if (output.size() > std::numeric_limits<size_t>::max() - sizeof(value)) return false;
    const auto offset = output.size();
    output.resize(offset + sizeof(value));
    std::memcpy(output.data() + offset, &value, sizeof(value));
    return true;
}

bool append_string(
    std::vector<uint8_t>& output,
    const std::string& value,
    size_t maximum = maximum_storage_string_bytes)
{
    if (value.size() > maximum || value.size() > UINT32_MAX) return false;
    const auto length = static_cast<uint32_t>(value.size());
    if (!append_scalar(output, length)
        || output.size() > std::numeric_limits<size_t>::max() - value.size()) return false;
    output.insert(output.end(), value.begin(), value.end());
    return true;
}

template<typename Value>
bool read_scalar(const std::vector<uint8_t>& input, size_t& cursor, Value& value)
{
    if (cursor > input.size() || input.size() - cursor < sizeof(value)) return false;
    std::memcpy(&value, input.data() + cursor, sizeof(value));
    cursor += sizeof(value);
    return true;
}

bool read_string(
    const std::vector<uint8_t>& input,
    size_t& cursor,
    std::string& value,
    size_t maximum = maximum_storage_string_bytes)
{
    uint32_t length = 0U;
    if (!read_scalar(input, cursor, length) || length > maximum
        || cursor > input.size() || input.size() - cursor < length) return false;
    value.assign(reinterpret_cast<const char*>(input.data() + cursor), length);
    cursor += length;
    return true;
}

bool flush_file(const std::filesystem::path& path)
{
#if defined(_WIN32)
    const auto handle = CreateFileW(
        path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;
    const auto flushed = FlushFileBuffers(handle) != 0;
    CloseHandle(handle);
    return flushed;
#else
    const auto descriptor = ::open(path.c_str(), O_RDONLY);
    if (descriptor < 0) return false;
    const auto flushed = ::fsync(descriptor) == 0;
    ::close(descriptor);
    return flushed;
#endif
}

bool replace_file(
    const std::filesystem::path& temporary,
    const std::filesystem::path& destination)
{
#if defined(_WIN32)
    return MoveFileExW(
        temporary.c_str(), destination.c_str(),
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    std::error_code error;
    std::filesystem::rename(temporary, destination, error);
    if (error) return false;
    const auto descriptor = ::open(destination.parent_path().c_str(), O_RDONLY);
    if (descriptor >= 0) {
        static_cast<void>(::fsync(descriptor));
        ::close(descriptor);
    }
    return true;
#endif
}

bool write_profile_payload(
    const browser_profile_state& state,
    std::vector<uint8_t>& payload)
{
    if (state.cookies.size() > maximum_cookie_count
        || state.local_storage.size() > maximum_origin_count) return false;
    if (!append_scalar(payload, static_cast<uint32_t>(state.cookies.size()))) return false;
    for (const auto& cookie : state.cookies) {
        const auto flags = static_cast<uint8_t>(
            (cookie.host_only ? 1U : 0U)
            | (cookie.secure ? 2U : 0U)
            | (cookie.http_only ? 4U : 0U));
        const auto same_site = static_cast<uint8_t>(cookie.same_site);
        if (!append_string(payload, cookie.name, maximum_cookie_bytes)
            || !append_string(payload, cookie.value, maximum_cookie_bytes)
            || !append_string(payload, cookie.domain, maximum_cookie_bytes)
            || !append_string(payload, cookie.path, maximum_cookie_bytes)
            || !append_scalar(payload, cookie.expiry_unix_seconds)
            || !append_scalar(payload, cookie.creation_order)
            || !append_scalar(payload, flags)
            || !append_scalar(payload, same_site)) return false;
    }
    std::vector<std::string> origins;
    origins.reserve(state.local_storage.size());
    for (const auto& [origin, storage] : state.local_storage) {
        static_cast<void>(storage);
        origins.push_back(origin);
    }
    std::sort(origins.begin(), origins.end());
    if (!append_scalar(payload, static_cast<uint32_t>(origins.size()))) return false;
    for (const auto& origin : origins) {
        const auto& storage = state.local_storage.at(origin);
        if (storage.keys.size() > maximum_entry_count
            || !append_string(payload, origin, maximum_identity_bytes)
            || !append_scalar(payload, static_cast<uint32_t>(storage.keys.size()))) return false;
        for (const auto& key : storage.keys) {
            const auto value = storage.values.find(key);
            if (value == storage.values.end()
                || !append_string(payload, key)
                || !append_string(payload, value->second)) return false;
        }
    }
    if (state.form_state_order.size() > maximum_form_document_count
        || state.form_states.size() != state.form_state_order.size()
        || !append_scalar(payload,
            static_cast<uint32_t>(state.form_state_order.size()))) return false;
    size_t form_bytes = 0U;
    std::unordered_set<std::string> seen_documents;
    for (const auto& document : state.form_state_order) {
        const auto known = state.form_states.find(document);
        if (document.empty() || document.size() > maximum_identity_bytes
            || known == state.form_states.end()
            || !seen_documents.insert(document).second
            || known->second.controls.size() > maximum_form_control_count
            || !append_string(payload, document, maximum_identity_bytes)
            || !append_scalar(payload,
                static_cast<uint32_t>(known->second.controls.size()))) return false;
        form_bytes += document.size();
        std::unordered_set<std::string> seen_controls;
        for (const auto& control : known->second.controls) {
            const auto kind = static_cast<uint8_t>(control.kind);
            const auto checked = static_cast<uint8_t>(control.checked ? 1U : 0U);
            if (control.identity.empty()
                || control.identity.size() > maximum_form_identity_bytes
                || !seen_controls.insert(control.identity).second
                || control.value.size() > maximum_form_value_bytes
                || control.selected_indices.size() > maximum_form_selected_count
                || kind > static_cast<uint8_t>(profile_form_control_kind::custom)
                || !append_string(payload, control.identity,
                    maximum_form_identity_bytes)
                || !append_scalar(payload, kind)
                || !append_scalar(payload, checked)
                || !append_string(payload, control.value,
                    maximum_form_value_bytes)
                || !append_scalar(payload,
                    static_cast<uint32_t>(control.selected_indices.size()))) return false;
            form_bytes += control.identity.size() + control.value.size()
                + control.selected_indices.size() * sizeof(uint32_t) + 2U;
            if (form_bytes > maximum_form_state_bytes) return false;
            for (const auto selected : control.selected_indices)
                if (!append_scalar(payload, selected)) return false;
        }
    }
    return true;
}

bool read_profile_payload(
    const std::vector<uint8_t>& payload,
    browser_profile_state& state,
    uint32_t schema)
{
    size_t cursor = 0U;
    uint32_t cookie_count = 0U;
    if (!read_scalar(payload, cursor, cookie_count)
        || cookie_count > maximum_cookie_count) return false;
    state.cookies.reserve(cookie_count);
    for (uint32_t index = 0U; index < cookie_count; ++index) {
        profile_cookie cookie;
        uint8_t flags = 0U, same_site = 0U;
        if (!read_string(payload, cursor, cookie.name, maximum_cookie_bytes)
            || !read_string(payload, cursor, cookie.value, maximum_cookie_bytes)
            || !read_string(payload, cursor, cookie.domain, maximum_cookie_bytes)
            || !read_string(payload, cursor, cookie.path, maximum_cookie_bytes)
            || !read_scalar(payload, cursor, cookie.expiry_unix_seconds)
            || !read_scalar(payload, cursor, cookie.creation_order)
            || !read_scalar(payload, cursor, flags)
            || !read_scalar(payload, cursor, same_site)
            || same_site > static_cast<uint8_t>(profile_cookie_same_site::none)) return false;
        cookie.host_only = (flags & 1U) != 0U;
        cookie.secure = (flags & 2U) != 0U;
        cookie.http_only = (flags & 4U) != 0U;
        cookie.same_site = static_cast<profile_cookie_same_site>(same_site);
        state.cookies.push_back(std::move(cookie));
    }
    uint32_t origin_count = 0U;
    if (!read_scalar(payload, cursor, origin_count)
        || origin_count > maximum_origin_count) return false;
    for (uint32_t origin_index = 0U; origin_index < origin_count; ++origin_index) {
        std::string origin;
        uint32_t entry_count = 0U;
        if (!read_string(payload, cursor, origin, maximum_identity_bytes)
            || origin.empty() || origin == "null"
            || !read_scalar(payload, cursor, entry_count)
            || entry_count > maximum_entry_count) return false;
        profile_local_storage storage;
        storage.keys.reserve(entry_count);
        for (uint32_t entry_index = 0U; entry_index < entry_count; ++entry_index) {
            std::string key, value;
            if (!read_string(payload, cursor, key)
                || !read_string(payload, cursor, value)
                || storage.values.contains(key)) return false;
            storage.keys.push_back(key);
            storage.values.emplace(std::move(key), std::move(value));
        }
        if (!state.local_storage.emplace(std::move(origin), std::move(storage)).second) {
            return false;
        }
    }
    if (schema == 1U) return cursor == payload.size();
    uint32_t document_count = 0U;
    if (!read_scalar(payload, cursor, document_count)
        || document_count > maximum_form_document_count) return false;
    size_t form_bytes = 0U;
    state.form_state_order.reserve(document_count);
    for (uint32_t document_index = 0U;
        document_index < document_count; ++document_index) {
        std::string document;
        uint32_t control_count = 0U;
        if (!read_string(payload, cursor, document, maximum_identity_bytes)
            || document.empty()
            || !read_scalar(payload, cursor, control_count)
            || control_count > maximum_form_control_count
            || state.form_states.contains(document)) return false;
        profile_form_document_state form;
        form.controls.reserve(control_count);
        std::unordered_set<std::string> seen_controls;
        form_bytes += document.size();
        for (uint32_t control_index = 0U;
            control_index < control_count; ++control_index) {
            profile_form_control_state control;
            uint8_t kind = 0U, checked = 0U;
            uint32_t selected_count = 0U;
            if (!read_string(payload, cursor, control.identity,
                    maximum_form_identity_bytes)
                || control.identity.empty()
                || !seen_controls.insert(control.identity).second
                || !read_scalar(payload, cursor, kind)
                || kind > static_cast<uint8_t>(profile_form_control_kind::custom)
                || !read_scalar(payload, cursor, checked)
                || checked > 1U
                || !read_string(payload, cursor, control.value,
                    maximum_form_value_bytes)
                || !read_scalar(payload, cursor, selected_count)
                || selected_count > maximum_form_selected_count) return false;
            control.kind = static_cast<profile_form_control_kind>(kind);
            control.checked = checked != 0U;
            control.selected_indices.resize(selected_count);
            for (auto& selected : control.selected_indices)
                if (!read_scalar(payload, cursor, selected)) return false;
            form_bytes += control.identity.size() + control.value.size()
                + control.selected_indices.size() * sizeof(uint32_t) + 2U;
            if (form_bytes > maximum_form_state_bytes) return false;
            form.controls.push_back(std::move(control));
        }
        state.form_state_order.push_back(document);
        state.form_states.emplace(std::move(document), std::move(form));
    }
    return cursor == payload.size();
}

} // namespace

struct browser_profile_storage::lock_handle final {
#if defined(_WIN32)
    HANDLE value{INVALID_HANDLE_VALUE};
    ~lock_handle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
#else
    int value{-1};
    ~lock_handle() {
        if (value < 0) return;
        static_cast<void>(::flock(value, LOCK_UN));
        ::close(value);
    }
#endif
};

browser_profile_storage::browser_profile_storage(
    std::filesystem::path root,
    std::string partition,
    uint64_t quota_bytes)
    : root_(std::move(root))
    , partition_(std::move(partition))
    , quota_bytes_(quota_bytes == 0U ? default_quota_bytes : quota_bytes)
{
    if (root_.empty() || partition_.empty()
        || partition_.size() > maximum_identity_bytes
        || quota_bytes_ < minimum_quota_bytes) return;
    std::error_code error;
    std::filesystem::create_directories(partition_path(), error);
    if (error) {
        open_status_ = profile_storage_status::io_error;
        return;
    }
#if !defined(_WIN32)
    static_cast<void>(::chmod(partition_path().c_str(), S_IRWXU));
#endif
    lock_ = std::make_unique<lock_handle>();
    const auto lock_path = partition_path() / "browser-profile.lock";
#if defined(_WIN32)
    lock_->value = CreateFileW(
        lock_path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
        OPEN_ALWAYS, FILE_ATTRIBUTE_HIDDEN, nullptr);
    if (lock_->value == INVALID_HANDLE_VALUE) {
        open_status_ = GetLastError() == ERROR_SHARING_VIOLATION
            ? profile_storage_status::busy : profile_storage_status::io_error;
        lock_.reset();
        return;
    }
#else
    lock_->value = ::open(lock_path.c_str(), O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
    if (lock_->value < 0 || ::flock(lock_->value, LOCK_EX | LOCK_NB) != 0) {
        open_status_ = profile_storage_status::busy;
        lock_.reset();
        return;
    }
    static_cast<void>(::fchmod(lock_->value, S_IRUSR | S_IWUSR));
#endif
    open_status_ = profile_storage_status::ok;
    const auto loaded = load_sync();
    if (loaded.status == profile_storage_status::ok) state_ = loaded.state;
    else if (loaded.status != profile_storage_status::not_found
        && loaded.status != profile_storage_status::corrupt) {
        open_status_ = loaded.status;
        lock_.reset();
        return;
    }
    worker_ = std::jthread([this](std::stop_token token) { run(token); });
}

browser_profile_storage::~browser_profile_storage()
{
    if (!worker_.joinable()) return;
    {
        std::lock_guard guard(mutex_);
        if (admitted_generation_ != committed_generation_) write_pending_ = true;
    }
    worker_.request_stop();
    wake_.notify_all();
    worker_.join();
}

bool browser_profile_storage::available() const noexcept
{
    return open_status_ == profile_storage_status::ok && lock_ != nullptr;
}

profile_storage_status browser_profile_storage::open_status() const noexcept
{
    return open_status_;
}

std::filesystem::path browser_profile_storage::partition_path() const
{
    return root_ / digest_name(partition_);
}

std::filesystem::path browser_profile_storage::profile_path() const
{
    return partition_path() / "browser-profile.wsprofile";
}

profile_storage_result browser_profile_storage::load_sync()
{
    if (!available()) return {open_status_, {}, "Browser profile is unavailable"};
    std::ifstream stream(profile_path(), std::ios::binary);
    if (!stream) {
        std::error_code error;
        return std::filesystem::exists(profile_path(), error)
            ? profile_storage_result{profile_storage_status::io_error, {},
                "Unable to open the browser profile"}
            : profile_storage_result{profile_storage_status::not_found, {}, {}};
    }
    std::array<char, 8> magic{};
    uint32_t schema = 0U;
    uint64_t revision = 0U, payload_length = 0U, payload_hash = 0U;
    uint32_t partition_length = 0U;
    stream.read(magic.data(), static_cast<std::streamsize>(magic.size()));
    if (!stream || magic != profile_magic
        || !stream.read(reinterpret_cast<char*>(&schema), sizeof(schema))
        || (schema != 1U && schema != profile_schema)
        || !stream.read(reinterpret_cast<char*>(&revision), sizeof(revision))
        || !stream.read(reinterpret_cast<char*>(&partition_length), sizeof(partition_length))
        || partition_length > maximum_identity_bytes) {
        return {profile_storage_status::corrupt, {},
            "Browser profile failed schema validation"};
    }
    std::string stored_partition(partition_length, '\0');
    if (partition_length != 0U) stream.read(stored_partition.data(), partition_length);
    if (!stream || stored_partition != partition_
        || !stream.read(reinterpret_cast<char*>(&payload_length), sizeof(payload_length))
        || !stream.read(reinterpret_cast<char*>(&payload_hash), sizeof(payload_hash))
        || payload_length > quota_bytes_ || payload_length > SIZE_MAX) {
        return {profile_storage_status::corrupt, {},
            "Browser profile failed identity or size validation"};
    }
    std::vector<uint8_t> payload(static_cast<size_t>(payload_length));
    if (!payload.empty()) stream.read(
        reinterpret_cast<char*>(payload.data()),
        static_cast<std::streamsize>(payload.size()));
    browser_profile_state state;
    state.revision = revision;
    if (!stream || stream.peek() != std::ifstream::traits_type::eof()
        || hash_bytes(payload.data(), payload.size()) != payload_hash
        || !read_profile_payload(payload, state, schema)) {
        return {profile_storage_status::corrupt, {},
            "Browser profile failed content validation"};
    }
    return {profile_storage_status::ok, std::move(state), {}};
}

profile_storage_result browser_profile_storage::store_sync(
    const browser_profile_state& state)
{
    if (!available()) return {open_status_, {}, "Browser profile is unavailable"};
    std::vector<uint8_t> payload;
    if (!write_profile_payload(state, payload)) {
        return {profile_storage_status::quota_exceeded, {},
            "Browser profile contains an oversized record"};
    }
    const auto header_size = profile_magic.size() + sizeof(uint32_t)
        + sizeof(uint64_t) * 3U + sizeof(uint32_t) + partition_.size();
    if (payload.size() > quota_bytes_ || header_size > quota_bytes_ - payload.size()) {
        return {profile_storage_status::quota_exceeded, {},
            "Browser profile exceeds its configured quota"};
    }
    std::error_code error;
    std::filesystem::create_directories(partition_path(), error);
    if (error) return {profile_storage_status::io_error, {},
        "Unable to create the browser profile directory"};
    static std::atomic<uint64_t> sequence{0U};
    auto temporary = profile_path();
    temporary += "."
#if defined(_WIN32)
        + std::to_string(GetCurrentProcessId())
#else
        + std::to_string(static_cast<uint64_t>(::getpid()))
#endif
        + "." + std::to_string(sequence.fetch_add(1U, std::memory_order_relaxed))
        + ".tmp";
    std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
    const auto revision = state.revision + 1U;
    const auto partition_length = static_cast<uint32_t>(partition_.size());
    const auto payload_length = static_cast<uint64_t>(payload.size());
    const auto payload_hash = hash_bytes(payload.data(), payload.size());
    stream.write(profile_magic.data(), profile_magic.size());
    stream.write(reinterpret_cast<const char*>(&profile_schema), sizeof(profile_schema));
    stream.write(reinterpret_cast<const char*>(&revision), sizeof(revision));
    stream.write(reinterpret_cast<const char*>(&partition_length), sizeof(partition_length));
    stream.write(partition_.data(), static_cast<std::streamsize>(partition_.size()));
    stream.write(reinterpret_cast<const char*>(&payload_length), sizeof(payload_length));
    stream.write(reinterpret_cast<const char*>(&payload_hash), sizeof(payload_hash));
    if (!payload.empty()) stream.write(
        reinterpret_cast<const char*>(payload.data()),
        static_cast<std::streamsize>(payload.size()));
    stream.close();
#if !defined(_WIN32)
    static_cast<void>(::chmod(temporary.c_str(), S_IRUSR | S_IWUSR));
#endif
    if (!stream || !flush_file(temporary) || !replace_file(temporary, profile_path())) {
        std::filesystem::remove(temporary, error);
        return {profile_storage_status::io_error, {},
            "Unable to commit the browser profile atomically"};
    }
    auto committed = state;
    committed.revision = revision;
    return {profile_storage_status::ok, std::move(committed), {}};
}

profile_storage_result browser_profile_storage::clear_sync(uint32_t flags)
{
    if (!available()) return {open_status_, {}, "Browser profile is unavailable"};
    browser_profile_state next;
    {
        std::lock_guard guard(mutex_);
        next = state_;
    }
    if ((flags & profile_clear_all_site_data) != 0U) {
        next.cookies.clear();
        next.local_storage.clear();
        next.form_state_order.clear();
        next.form_states.clear();
        std::error_code error;
        std::vector<std::filesystem::path> empty_directory_candidates;
        size_t visited = 0U;
        std::filesystem::recursive_directory_iterator iterator(partition_path(), error);
        const std::filesystem::recursive_directory_iterator end;
        while (!error && iterator != end && visited < maximum_entry_count) {
            ++visited;
            const auto path = iterator->path();
            if (iterator->is_directory(error)) {
                empty_directory_candidates.push_back(path);
            } else if (iterator->is_regular_file(error)
                && (path.extension() == ".wsidb"
                    || (path.extension() == ".tmp"
                        && path.filename() != "browser-profile.lock"))) {
                std::filesystem::remove(path, error);
            }
            if (!error) iterator.increment(error);
        }
        if (error || iterator != end) {
            return {profile_storage_status::io_error, {},
                "Unable to clear the bounded browser profile"};
        }
        std::sort(
            empty_directory_candidates.begin(),
            empty_directory_candidates.end(),
            [](const auto& left, const auto& right) {
                return left.native().size() > right.native().size();
            });
        for (const auto& path : empty_directory_candidates) {
            std::filesystem::remove(path, error);
            if (error) {
                error.clear();
                if (!std::filesystem::is_directory(path, error)) {
                    return {profile_storage_status::io_error, {},
                        "Unable to clear the browser profile directory"};
                }
                error.clear();
            }
        }
    } else {
        if ((flags & profile_clear_cookies) != 0U) next.cookies.clear();
        if ((flags & profile_clear_local_storage) != 0U) next.local_storage.clear();
    }
    auto result = store_sync(next);
    if (result.status == profile_storage_status::ok) {
        std::lock_guard guard(mutex_);
        state_ = result.state;
        committed_generation_ = admitted_generation_;
        write_pending_ = false;
    }
    return result;
}

void browser_profile_storage::schedule_write_locked()
{
    ++admitted_generation_;
    write_pending_ = true;
    wake_.notify_one();
}

void browser_profile_storage::replace_cookies(std::vector<profile_cookie> cookies)
{
    if (!available()) return;
    std::lock_guard guard(mutex_);
    state_.cookies = std::move(cookies);
    schedule_write_locked();
}

void browser_profile_storage::replace_local_storage(
    std::string origin,
    profile_local_storage storage)
{
    if (!available() || origin.empty() || origin == "null") return;
    std::lock_guard guard(mutex_);
    state_.local_storage.insert_or_assign(std::move(origin), std::move(storage));
    schedule_write_locked();
}

void browser_profile_storage::clear_local_storage_origin(const std::string& origin)
{
    if (!available() || origin.empty() || origin == "null") return;
    std::lock_guard guard(mutex_);
    state_.local_storage.erase(origin);
    schedule_write_locked();
}

void browser_profile_storage::replace_form_state(
    std::string document,
    profile_form_document_state state)
{
    if (!available() || document.empty()
        || document.size() > maximum_identity_bytes
        || state.controls.size() > maximum_form_control_count) return;
    size_t bytes = document.size();
    std::unordered_set<std::string> identities;
    for (const auto& control : state.controls) {
        bytes += control.identity.size() + control.value.size()
            + control.selected_indices.size() * sizeof(uint32_t) + 2U;
        if (control.identity.empty()
            || control.identity.size() > maximum_form_identity_bytes
            || !identities.insert(control.identity).second
            || control.value.size() > maximum_form_value_bytes
            || control.selected_indices.size() > maximum_form_selected_count
            || bytes > maximum_form_state_bytes) return;
    }
    std::lock_guard guard(mutex_);
    std::erase(state_.form_state_order, document);
    state_.form_states.erase(document);
    if (!state.controls.empty()) {
        auto retained_bytes = bytes;
        for (const auto& key : state_.form_state_order) {
            const auto known = state_.form_states.find(key);
            if (known == state_.form_states.end()) continue;
            retained_bytes += key.size();
            for (const auto& control : known->second.controls)
                retained_bytes += control.identity.size() + control.value.size()
                    + control.selected_indices.size() * sizeof(uint32_t) + 2U;
        }
        while (!state_.form_state_order.empty()
            && retained_bytes > maximum_form_state_bytes) {
            const auto oldest = state_.form_states.find(
                state_.form_state_order.front());
            if (oldest != state_.form_states.end()) {
                retained_bytes -= state_.form_state_order.front().size();
                for (const auto& control : oldest->second.controls)
                    retained_bytes -= control.identity.size() + control.value.size()
                        + control.selected_indices.size() * sizeof(uint32_t) + 2U;
                state_.form_states.erase(oldest);
            }
            state_.form_state_order.erase(state_.form_state_order.begin());
        }
        while (state_.form_state_order.size() >= maximum_form_document_count) {
            state_.form_states.erase(state_.form_state_order.front());
            state_.form_state_order.erase(state_.form_state_order.begin());
        }
        state_.form_state_order.push_back(document);
        state_.form_states.emplace(std::move(document), std::move(state));
    }
    schedule_write_locked();
}

browser_profile_state browser_profile_storage::snapshot() const
{
    std::lock_guard guard(mutex_);
    return state_;
}

profile_storage_result browser_profile_storage::clear_partition_sync(
    const std::filesystem::path& root,
    const std::string& partition,
    uint64_t quota_bytes,
    uint32_t flags)
{
    browser_profile_storage storage(root, partition, quota_bytes);
    return storage.clear_sync(flags);
}

void browser_profile_storage::run(std::stop_token token)
{
    for (;;) {
        browser_profile_state pending;
        uint64_t generation = 0U;
        {
            std::unique_lock lock(mutex_);
            wake_.wait(lock, token, [this] { return write_pending_; });
            if (!write_pending_ && token.stop_requested()) break;
            pending = state_;
            generation = admitted_generation_;
            write_pending_ = false;
        }
        const auto result = store_sync(pending);
        {
            std::lock_guard guard(mutex_);
            if (result.status == profile_storage_status::ok) {
                state_.revision = std::max(state_.revision, result.state.revision);
                committed_generation_ = std::max(committed_generation_, generation);
            }
            if (admitted_generation_ != generation) write_pending_ = true;
        }
        if (token.stop_requested()) {
            std::lock_guard guard(mutex_);
            if (!write_pending_) break;
        }
    }
}

} // namespace webscene_native
