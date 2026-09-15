#pragma once

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <mutex>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

namespace webscene_native {

enum class indexeddb_storage_status : uint8_t {
    ok,
    not_found,
    conflict,
    quota_exceeded,
    unavailable,
    corrupt,
    io_error
};

struct indexeddb_storage_result final {
    indexeddb_storage_status status{indexeddb_storage_status::io_error};
    uint64_t revision{0};
    std::vector<uint8_t> bytes;
    std::string message;
};

class indexeddb_storage final {
public:
    using completion = std::function<void(indexeddb_storage_result)>;

    indexeddb_storage(
        std::filesystem::path root,
        std::string partition,
        uint64_t quota_bytes);
    ~indexeddb_storage();

    indexeddb_storage(const indexeddb_storage&) = delete;
    indexeddb_storage& operator=(const indexeddb_storage&) = delete;

    bool available() const noexcept;
    void load(std::string origin, std::string database, completion callback);
    void store(
        std::string origin,
        std::string database,
        uint64_t expected_revision,
        std::vector<uint8_t> bytes,
        completion callback);
    void erase(std::string origin, std::string database, completion callback);

    // Synchronous entry points are intentionally public for the V8-free
    // durability contract. Production JavaScript uses only the worker-backed
    // methods above.
    indexeddb_storage_result load_sync(
        const std::string& origin,
        const std::string& database) const;
    indexeddb_storage_result store_sync(
        const std::string& origin,
        const std::string& database,
        uint64_t expected_revision,
        const std::vector<uint8_t>& bytes) const;
    indexeddb_storage_result erase_sync(
        const std::string& origin,
        const std::string& database) const;

private:
    enum class operation_kind : uint8_t { load, store, erase };
    struct operation final {
        operation_kind kind;
        std::string origin;
        std::string database;
        uint64_t expected_revision{0};
        std::vector<uint8_t> bytes;
        completion callback;
    };

    std::filesystem::path database_path(
        const std::string& origin,
        const std::string& database) const;
    void enqueue(operation value);
    void run(std::stop_token token);

    std::filesystem::path root_;
    std::string partition_;
    uint64_t quota_bytes_{0};
    mutable std::mutex mutex_;
    std::condition_variable_any wake_;
    std::deque<operation> operations_;
    std::jthread worker_;
};

} // namespace webscene_native
