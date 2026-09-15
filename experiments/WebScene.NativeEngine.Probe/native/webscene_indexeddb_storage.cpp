#include "webscene_indexeddb_storage.h"

#include <atomic>
#include <array>
#include <chrono>
#include <cstring>
#include <fstream>
#include <limits>
#include <system_error>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace webscene_native {
namespace {

constexpr std::array<char, 8> storage_magic{'W', 'S', 'I', 'D', 'B', '0', '0', '1'};
constexpr uint32_t storage_schema = 1U;
constexpr size_t maximum_identity_bytes = 4096U;
constexpr uint64_t minimum_quota_bytes = 1024U * 1024U;
constexpr uint64_t maximum_payload_bytes = 512U * 1024U * 1024U;

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
bool read_scalar(std::istream& stream, Value& value)
{
    stream.read(reinterpret_cast<char*>(&value), sizeof(value));
    return static_cast<bool>(stream);
}

template<typename Value>
bool write_scalar(std::ostream& stream, const Value& value)
{
    stream.write(reinterpret_cast<const char*>(&value), sizeof(value));
    return static_cast<bool>(stream);
}

bool read_string(std::istream& stream, std::string& value)
{
    uint32_t length = 0;
    if (!read_scalar(stream, length) || length > maximum_identity_bytes) return false;
    value.resize(length);
    if (length != 0U) stream.read(value.data(), static_cast<std::streamsize>(length));
    return static_cast<bool>(stream);
}

bool write_string(std::ostream& stream, const std::string& value)
{
    if (value.size() > maximum_identity_bytes) return false;
    const auto length = static_cast<uint32_t>(value.size());
    return write_scalar(stream, length)
        && (length == 0U
            || static_cast<bool>(stream.write(
                value.data(), static_cast<std::streamsize>(length))));
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

class directory_lock final {
public:
    explicit directory_lock(std::filesystem::path path) : path_(std::move(path))
    {
        const auto deadline = std::chrono::steady_clock::now()
            + std::chrono::seconds(2);
        while (std::chrono::steady_clock::now() < deadline) {
            std::error_code error;
            if (std::filesystem::create_directory(path_, error)) {
                locked_ = true;
                return;
            }
            error.clear();
            const auto modified = std::filesystem::last_write_time(path_, error);
            if (!error
                && std::filesystem::file_time_type::clock::now() - modified
                    > std::chrono::seconds(30)) {
                std::filesystem::remove_all(path_, error);
                continue;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }

    ~directory_lock()
    {
        if (!locked_) return;
        std::error_code error;
        std::filesystem::remove(path_, error);
    }

    explicit operator bool() const noexcept { return locked_; }

private:
    std::filesystem::path path_;
    bool locked_{false};
};

uint64_t partition_usage(const std::filesystem::path& root)
{
    std::error_code error;
    if (!std::filesystem::is_directory(root, error)) return 0U;
    uint64_t total = 0U;
    std::filesystem::recursive_directory_iterator iterator(root, error);
    const std::filesystem::recursive_directory_iterator end;
    while (!error && iterator != end) {
        if (iterator->is_regular_file(error)
            && iterator->path().extension() == ".wsidb") {
            const auto size = iterator->file_size(error);
            if (!error && size <= std::numeric_limits<uint64_t>::max() - total) {
                total += size;
            }
        }
        iterator.increment(error);
    }
    return total;
}

} // namespace

indexeddb_storage::indexeddb_storage(
    std::filesystem::path root,
    std::string partition,
    uint64_t quota_bytes)
    : root_(std::move(root))
    , partition_(std::move(partition))
    , quota_bytes_(quota_bytes == 0U ? 256U * 1024U * 1024U : quota_bytes)
    , worker_([this](std::stop_token token) { run(token); })
{
}

indexeddb_storage::~indexeddb_storage()
{
    worker_.request_stop();
    wake_.notify_all();
    if (worker_.joinable()) worker_.join();
}

bool indexeddb_storage::available() const noexcept
{
    return !root_.empty() && !partition_.empty()
        && partition_.size() <= maximum_identity_bytes
        && quota_bytes_ >= minimum_quota_bytes;
}

std::filesystem::path indexeddb_storage::database_path(
    const std::string& origin,
    const std::string& database) const
{
    if (!available() || origin.empty() || origin == "null"
        || origin.size() > maximum_identity_bytes || database.empty()
        || database.size() > maximum_identity_bytes) {
        return {};
    }
    return root_ / digest_name(partition_) / digest_name(origin)
        / (digest_name(database) + ".wsidb");
}

indexeddb_storage_result indexeddb_storage::load_sync(
    const std::string& origin,
    const std::string& database) const
{
    const auto path = database_path(origin, database);
    if (path.empty()) {
        return {indexeddb_storage_status::unavailable, 0U, {},
            "Persistent storage is not configured for this origin"};
    }
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        std::error_code error;
        if (!std::filesystem::exists(path, error)) {
            return {indexeddb_storage_status::not_found, 0U, {}, {}};
        }
        return {indexeddb_storage_status::io_error, 0U, {},
            "Unable to open the IndexedDB data file"};
    }
    std::array<char, 8> magic{};
    uint32_t schema = 0;
    uint64_t revision = 0;
    std::string stored_partition, stored_origin, stored_database;
    uint64_t payload_length = 0, payload_hash = 0;
    stream.read(magic.data(), static_cast<std::streamsize>(magic.size()));
    if (!stream || magic != storage_magic || !read_scalar(stream, schema)
        || schema != storage_schema || !read_scalar(stream, revision)
        || !read_string(stream, stored_partition)
        || !read_string(stream, stored_origin)
        || !read_string(stream, stored_database)
        || !read_scalar(stream, payload_length)
        || !read_scalar(stream, payload_hash)
        || stored_partition != partition_ || stored_origin != origin
        || stored_database != database || payload_length > maximum_payload_bytes
        || payload_length > quota_bytes_) {
        return {indexeddb_storage_status::corrupt, 0U, {},
            "IndexedDB data failed schema or identity validation"};
    }
    std::vector<uint8_t> bytes(static_cast<size_t>(payload_length));
    if (payload_length != 0U) {
        stream.read(
            reinterpret_cast<char*>(bytes.data()),
            static_cast<std::streamsize>(payload_length));
    }
    if (!stream || stream.peek() != std::ifstream::traits_type::eof()
        || hash_bytes(bytes.data(), bytes.size()) != payload_hash) {
        return {indexeddb_storage_status::corrupt, 0U, {},
            "IndexedDB data failed content validation"};
    }
    return {indexeddb_storage_status::ok, revision, std::move(bytes), {}};
}

indexeddb_storage_result indexeddb_storage::store_sync(
    const std::string& origin,
    const std::string& database,
    uint64_t expected_revision,
    const std::vector<uint8_t>& bytes) const
{
    const auto path = database_path(origin, database);
    if (path.empty()) {
        return {indexeddb_storage_status::unavailable, 0U, {},
            "Persistent storage is not configured for this origin"};
    }
    if (bytes.size() > quota_bytes_ || bytes.size() > maximum_payload_bytes) {
        return {indexeddb_storage_status::quota_exceeded, 0U, {},
            "IndexedDB payload exceeds the configured quota"};
    }
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        return {indexeddb_storage_status::io_error, 0U, {},
            "Unable to create the IndexedDB profile directory"};
    }
    directory_lock lock(path.string() + ".lock");
    if (!lock) {
        return {indexeddb_storage_status::io_error, 0U, {},
            "Timed out acquiring the IndexedDB database lock"};
    }
    const auto current = load_sync(origin, database);
    const auto current_revision = current.status == indexeddb_storage_status::not_found
        ? 0U : current.revision;
    if (current.status != indexeddb_storage_status::ok
        && current.status != indexeddb_storage_status::not_found) {
        return current;
    }
    if (current_revision != expected_revision) {
        return {indexeddb_storage_status::conflict, current_revision, {},
            "IndexedDB database changed before this transaction committed"};
    }

    const auto old_size = std::filesystem::exists(path, error)
        ? std::filesystem::file_size(path, error) : 0U;
    if (error) {
        return {indexeddb_storage_status::io_error, current_revision, {},
            "Unable to inspect the IndexedDB data file"};
    }
    constexpr uint64_t fixed_header = 8U + sizeof(uint32_t)
        + sizeof(uint64_t) * 3U + sizeof(uint32_t) * 3U;
    const auto new_size = fixed_header + partition_.size() + origin.size()
        + database.size() + bytes.size();
    const auto usage = partition_usage(root_ / digest_name(partition_));
    const auto projected = usage >= old_size ? usage - old_size + new_size : new_size;
    if (projected > quota_bytes_) {
        return {indexeddb_storage_status::quota_exceeded, current_revision, {},
            "IndexedDB profile quota would be exceeded"};
    }

    static std::atomic<uint64_t> sequence{0U};
    const auto temporary = path.string() + "."
#if defined(_WIN32)
        + std::to_string(GetCurrentProcessId())
#else
        + std::to_string(static_cast<uint64_t>(::getpid()))
#endif
        + "." + std::to_string(sequence.fetch_add(1, std::memory_order_relaxed))
        + ".tmp";
    std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
    const auto revision = current_revision + 1U;
    const auto payload_length = static_cast<uint64_t>(bytes.size());
    const auto payload_hash = hash_bytes(bytes.data(), bytes.size());
    stream.write(storage_magic.data(), static_cast<std::streamsize>(storage_magic.size()));
    if (!stream || !write_scalar(stream, storage_schema)
        || !write_scalar(stream, revision)
        || !write_string(stream, partition_) || !write_string(stream, origin)
        || !write_string(stream, database)
        || !write_scalar(stream, payload_length)
        || !write_scalar(stream, payload_hash)) {
        stream.close();
        std::filesystem::remove(temporary, error);
        return {indexeddb_storage_status::io_error, current_revision, {},
            "Unable to write the IndexedDB transaction header"};
    }
    if (!bytes.empty()) {
        stream.write(
            reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
    }
    stream.close();
    if (!stream || !flush_file(temporary) || !replace_file(temporary, path)) {
        std::filesystem::remove(temporary, error);
        return {indexeddb_storage_status::io_error, current_revision, {},
            "Unable to commit the IndexedDB transaction atomically"};
    }
    return {indexeddb_storage_status::ok, revision, {}, {}};
}

indexeddb_storage_result indexeddb_storage::erase_sync(
    const std::string& origin,
    const std::string& database) const
{
    const auto path = database_path(origin, database);
    if (path.empty()) {
        return {indexeddb_storage_status::unavailable, 0U, {},
            "Persistent storage is not configured for this origin"};
    }
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) return {indexeddb_storage_status::io_error, 0U, {}, {}};
    directory_lock lock(path.string() + ".lock");
    if (!lock) return {indexeddb_storage_status::io_error, 0U, {}, {}};
    if (!std::filesystem::remove(path, error) && error) {
        return {indexeddb_storage_status::io_error, 0U, {},
            "Unable to remove the IndexedDB data file"};
    }
    return {indexeddb_storage_status::ok, 0U, {}, {}};
}

void indexeddb_storage::load(
    std::string origin,
    std::string database,
    completion callback)
{
    enqueue({operation_kind::load, std::move(origin), std::move(database),
        0U, {}, std::move(callback)});
}

void indexeddb_storage::store(
    std::string origin,
    std::string database,
    uint64_t expected_revision,
    std::vector<uint8_t> bytes,
    completion callback)
{
    enqueue({operation_kind::store, std::move(origin), std::move(database),
        expected_revision, std::move(bytes), std::move(callback)});
}

void indexeddb_storage::erase(
    std::string origin,
    std::string database,
    completion callback)
{
    enqueue({operation_kind::erase, std::move(origin), std::move(database),
        0U, {}, std::move(callback)});
}

void indexeddb_storage::enqueue(operation value)
{
    {
        std::lock_guard lock(mutex_);
        operations_.push_back(std::move(value));
    }
    wake_.notify_one();
}

void indexeddb_storage::run(std::stop_token token)
{
    while (!token.stop_requested()) {
        operation next{operation_kind::load, {}, {}, 0U, {}, {}};
        {
            std::unique_lock lock(mutex_);
            wake_.wait(lock, token, [this] { return !operations_.empty(); });
            if (token.stop_requested()) break;
            next = std::move(operations_.front());
            operations_.pop_front();
        }
        indexeddb_storage_result result;
        switch (next.kind) {
        case operation_kind::load:
            result = load_sync(next.origin, next.database);
            break;
        case operation_kind::store:
            result = store_sync(
                next.origin, next.database, next.expected_revision, next.bytes);
            break;
        case operation_kind::erase:
            result = erase_sync(next.origin, next.database);
            break;
        }
        try {
            if (next.callback) next.callback(std::move(result));
        } catch (...) {
        }
    }
    std::deque<operation> abandoned;
    {
        std::lock_guard lock(mutex_);
        abandoned.swap(operations_);
    }
    for (auto& value : abandoned) {
        try {
            if (value.callback) value.callback({
                indexeddb_storage_status::unavailable, 0U, {},
                "IndexedDB storage is shutting down"});
        } catch (...) {
        }
    }
}

} // namespace webscene_native
