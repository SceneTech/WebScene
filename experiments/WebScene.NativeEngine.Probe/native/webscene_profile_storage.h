#pragma once

#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace webscene_native {

enum class profile_storage_status : uint8_t {
    ok,
    not_found,
    busy,
    quota_exceeded,
    unavailable,
    corrupt,
    io_error
};

enum class profile_cookie_same_site : uint8_t { lax, strict, none };

struct profile_cookie final {
    std::string name;
    std::string value;
    std::string domain;
    std::string path;
    int64_t expiry_unix_seconds{0};
    uint64_t creation_order{0};
    bool host_only{true};
    bool secure{false};
    bool http_only{false};
    profile_cookie_same_site same_site{profile_cookie_same_site::lax};
};

struct profile_local_storage final {
    std::vector<std::string> keys;
    std::unordered_map<std::string, std::string> values;
};

enum class profile_form_control_kind : uint8_t {
    value,
    checked,
    selected,
    custom
};

struct profile_form_control_state final {
    std::string identity;
    std::string value;
    std::vector<uint32_t> selected_indices;
    profile_form_control_kind kind{profile_form_control_kind::value};
    bool checked{false};
};

struct profile_form_document_state final {
    std::vector<profile_form_control_state> controls;
};

struct browser_profile_state final {
    uint64_t revision{0};
    std::vector<profile_cookie> cookies;
    std::unordered_map<std::string, profile_local_storage> local_storage;
    std::vector<std::string> form_state_order;
    std::unordered_map<std::string, profile_form_document_state> form_states;
};

struct profile_storage_result final {
    profile_storage_status status{profile_storage_status::io_error};
    browser_profile_state state;
    std::string message;
};

enum : uint32_t {
    profile_clear_cookies = 1U << 0U,
    profile_clear_local_storage = 1U << 1U,
    profile_clear_all_site_data = 1U << 2U
};

// One instance owns the non-blocking process lock for a host partition. The
// V8 thread updates the in-memory snapshot; one coalescing worker performs all
// writes. Destruction checkpoints the latest admitted snapshot before return.
class browser_profile_storage final {
public:
    browser_profile_storage(
        std::filesystem::path root,
        std::string partition,
        uint64_t quota_bytes);
    ~browser_profile_storage();

    browser_profile_storage(const browser_profile_storage&) = delete;
    browser_profile_storage& operator=(const browser_profile_storage&) = delete;

    bool available() const noexcept;
    profile_storage_status open_status() const noexcept;
    profile_storage_result load_sync();
    profile_storage_result store_sync(const browser_profile_state& state);
    profile_storage_result clear_sync(uint32_t flags);

    void replace_cookies(std::vector<profile_cookie> cookies);
    void replace_local_storage(std::string origin, profile_local_storage storage);
    void clear_local_storage_origin(const std::string& origin);
    void replace_form_state(
        std::string document,
        profile_form_document_state state);
    browser_profile_state snapshot() const;

    static profile_storage_result clear_partition_sync(
        const std::filesystem::path& root,
        const std::string& partition,
        uint64_t quota_bytes,
        uint32_t flags);

private:
    struct lock_handle;
    void schedule_write_locked();
    void run(std::stop_token token);
    std::filesystem::path partition_path() const;
    std::filesystem::path profile_path() const;

    std::filesystem::path root_;
    std::string partition_;
    uint64_t quota_bytes_{0};
    profile_storage_status open_status_{profile_storage_status::unavailable};
    std::unique_ptr<lock_handle> lock_;
    mutable std::mutex mutex_;
    std::condition_variable_any wake_;
    browser_profile_state state_;
    uint64_t admitted_generation_{0};
    uint64_t committed_generation_{0};
    bool write_pending_{false};
    std::jthread worker_;
};

} // namespace webscene_native
