#include "webscene_profile_storage.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

#if defined(_WIN32)
#include <process.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {

using namespace webscene_native;

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

class temporary_directory final {
public:
    temporary_directory()
        : path_(std::filesystem::temp_directory_path()
            / ("webscene-profile-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count())))
    {
        std::filesystem::create_directories(path_);
    }
    ~temporary_directory()
    {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }
    const std::filesystem::path& path() const noexcept { return path_; }
private:
    std::filesystem::path path_;
};

profile_cookie persistent_cookie(std::string value)
{
    profile_cookie cookie;
    cookie.name = "sid";
    cookie.value = std::move(value);
    cookie.domain = "example.test";
    cookie.path = "/";
    cookie.expiry_unix_seconds = 4102444800LL;
    cookie.creation_order = 1U;
    cookie.secure = true;
    cookie.http_only = true;
    cookie.same_site = profile_cookie_same_site::strict;
    return cookie;
}

void test_round_trip_and_partitioning()
{
    temporary_directory root;
    {
        browser_profile_storage storage(root.path(), "application/default", 4U * 1024U * 1024U);
        require(storage.available(), "profile did not open");
        storage.replace_cookies({persistent_cookie("secret")});
        profile_local_storage local;
        local.keys = {"theme", "empty"};
        local.values = {{"theme", "dark"}, {"empty", ""}};
        storage.replace_local_storage("https://example.test", std::move(local));
    }
    {
        browser_profile_storage storage(root.path(), "application/default", 4U * 1024U * 1024U);
        const auto state = storage.snapshot();
        require(state.cookies.size() == 1U
                && state.cookies.front().value == "secret"
                && state.cookies.front().domain == "example.test"
                && state.cookies.front().path == "/"
                && state.cookies.front().expiry_unix_seconds == 4102444800LL
                && state.cookies.front().creation_order == 1U
                && state.cookies.front().host_only
                && state.cookies.front().secure
                && state.cookies.front().http_only
                && state.cookies.front().same_site == profile_cookie_same_site::strict,
            "persistent cookie metadata did not survive restart");
        const auto known = state.local_storage.find("https://example.test");
        require(known != state.local_storage.end()
                && known->second.keys == std::vector<std::string>({"theme", "empty"})
                && known->second.values.at("theme") == "dark",
            "localStorage order or values did not survive restart");
    }
    browser_profile_storage isolated(root.path(), "application/other", 4U * 1024U * 1024U);
    require(isolated.snapshot().cookies.empty()
            && isolated.snapshot().local_storage.empty(),
        "another profile observed durable state");
}

void test_interrupted_temporary_write_is_ignored()
{
    temporary_directory root;
    std::filesystem::path profile_file;
    {
        browser_profile_storage storage(root.path(), "profile", 4U * 1024U * 1024U);
        storage.replace_cookies({persistent_cookie("committed")});
    }
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root.path())) {
        if (entry.path().extension() == ".wsprofile") profile_file = entry.path();
    }
    require(!profile_file.empty(), "committed profile fixture was not created");
    std::ofstream(profile_file.string() + ".interrupted.tmp", std::ios::binary)
        << "partial-next-generation";
    browser_profile_storage reopened(root.path(), "profile", 4U * 1024U * 1024U);
    require(reopened.snapshot().cookies.size() == 1U
            && reopened.snapshot().cookies.front().value == "committed",
        "an interrupted temporary write displaced the last committed profile");
}

void test_concurrent_open_is_rejected()
{
    temporary_directory root;
    browser_profile_storage first(root.path(), "profile", 4U * 1024U * 1024U);
    browser_profile_storage second(root.path(), "profile", 4U * 1024U * 1024U);
    require(first.available(), "first profile did not open");
    require(!second.available()
            && second.open_status() == profile_storage_status::busy,
        "concurrent profile open was not rejected");
}

void test_corruption_recovers_on_next_commit()
{
    temporary_directory root;
    std::filesystem::path profile_file;
    {
        browser_profile_storage storage(root.path(), "profile", 4U * 1024U * 1024U);
        storage.replace_cookies({persistent_cookie("first")});
    }
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root.path())) {
        if (entry.path().extension() == ".wsprofile") profile_file = entry.path();
    }
    require(!profile_file.empty(), "profile file was not created");
    {
        std::ofstream truncated(profile_file, std::ios::binary | std::ios::trunc);
        truncated << "broken";
    }
    {
        browser_profile_storage storage(root.path(), "profile", 4U * 1024U * 1024U);
        require(storage.available() && storage.snapshot().cookies.empty(),
            "corrupt profile did not recover to an empty in-memory state");
        storage.replace_cookies({persistent_cookie("recovered")});
    }
    browser_profile_storage reopened(root.path(), "profile", 4U * 1024U * 1024U);
    require(reopened.snapshot().cookies.size() == 1U
            && reopened.snapshot().cookies.front().value == "recovered",
        "atomic commit did not replace corrupt profile");
}

void test_scoped_clear()
{
    temporary_directory root;
    {
        browser_profile_storage storage(root.path(), "profile", 4U * 1024U * 1024U);
        storage.replace_cookies({persistent_cookie("secret")});
        profile_local_storage local;
        local.keys = {"key"};
        local.values = {{"key", "value"}};
        storage.replace_local_storage("https://example.test", std::move(local));
    }
    auto cleared = browser_profile_storage::clear_partition_sync(
        root.path(), "profile", 4U * 1024U * 1024U, profile_clear_cookies);
    require(cleared.status == profile_storage_status::ok,
        "cookie-only profile clear failed");
    {
        browser_profile_storage storage(root.path(), "profile", 4U * 1024U * 1024U);
        require(storage.snapshot().cookies.empty()
                && storage.snapshot().local_storage.size() == 1U,
            "cookie-only clear crossed the requested data boundary");
    }
    cleared = browser_profile_storage::clear_partition_sync(
        root.path(), "profile", 4U * 1024U * 1024U,
        profile_clear_local_storage);
    require(cleared.status == profile_storage_status::ok,
        "localStorage-only profile clear failed");
    {
        browser_profile_storage storage(root.path(), "profile", 4U * 1024U * 1024U);
        require(storage.snapshot().cookies.empty()
                && storage.snapshot().local_storage.empty(),
            "localStorage-only clear crossed the requested data boundary");
    }
    std::filesystem::path indexeddb_file;
    for (const auto& entry : std::filesystem::directory_iterator(root.path())) {
        if (!entry.is_directory()) continue;
        indexeddb_file = entry.path() / "origin" / "database.wsidb";
        std::filesystem::create_directories(indexeddb_file.parent_path());
        std::ofstream(indexeddb_file, std::ios::binary) << "indexeddb";
        break;
    }
    require(!indexeddb_file.empty() && std::filesystem::exists(indexeddb_file),
        "IndexedDB clear fixture was not created");
    cleared = browser_profile_storage::clear_partition_sync(
        root.path(), "profile", 4U * 1024U * 1024U,
        profile_clear_all_site_data);
    require(cleared.status == profile_storage_status::ok
            && !std::filesystem::exists(indexeddb_file),
        "all-site-data clear did not remove IndexedDB data in the partition");
}

int run_child(const std::string& executable, const char* mode, const std::filesystem::path& root)
{
    const auto root_text = root.string();
#if defined(_WIN32)
    return static_cast<int>(_spawnl(
        _P_WAIT, executable.c_str(), executable.c_str(), mode,
        root_text.c_str(), nullptr));
#else
    const auto process = ::fork();
    if (process == 0) {
        ::execl(executable.c_str(), executable.c_str(), mode,
            root_text.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }
    if (process < 0) return -1;
    int status = 0;
    return ::waitpid(process, &status, 0) == process && WIFEXITED(status)
        ? WEXITSTATUS(status) : -1;
#endif
}

void test_two_process_restart(const std::string& executable)
{
    temporary_directory root;
    require(run_child(executable, "--write-child", root.path()) == 0,
        "process A could not commit the browser profile");
    require(run_child(executable, "--read-child", root.path()) == 0,
        "process B could not read the committed browser profile");
}

size_t descriptor_count()
{
#if defined(_WIN32)
    return 0U;
#else
    std::error_code error;
    const std::filesystem::path descriptors{"/dev/fd"};
    if (!std::filesystem::is_directory(descriptors, error)) return 0U;
    return static_cast<size_t>(std::distance(
        std::filesystem::directory_iterator(descriptors, error),
        std::filesystem::directory_iterator{}));
#endif
}

void test_repeated_close_and_growth_bounds()
{
    temporary_directory root;
    const auto descriptors_before = descriptor_count();
    const auto started = std::chrono::steady_clock::now();
    for (size_t cycle = 0U; cycle < 50U; ++cycle) {
        browser_profile_storage storage(root.path(), "cycles/default", 4U * 1024U * 1024U);
        require(storage.available(), "profile cycle could not acquire its lock");
        profile_local_storage local;
        local.keys = {"cycle"};
        local.values = {{"cycle", std::to_string(cycle)}};
        storage.replace_local_storage("https://cycles.test", std::move(local));
    }
    const auto elapsed = std::chrono::steady_clock::now() - started;
    require(elapsed < std::chrono::seconds(5),
        "repeated profile checkpoints exceeded the close-latency budget");
    const auto descriptors_after = descriptor_count();
    require(descriptors_before == 0U || descriptors_after <= descriptors_before + 2U,
        "profile cycles leaked file descriptors");
    uint64_t stored_bytes = 0U;
    size_t temporary_files = 0U;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root.path())) {
        if (!entry.is_regular_file()) continue;
        stored_bytes += entry.file_size();
        if (entry.path().extension() == ".tmp") ++temporary_files;
    }
    require(stored_bytes < 4U * 1024U * 1024U && temporary_files == 0U,
        "profile cycles leaked temporary files or exceeded the on-disk bound");
}

} // namespace

int main(int argc, char** argv)
{
    try {
        if (argc == 3 && std::string_view(argv[1]) == "--write-child") {
            browser_profile_storage storage(argv[2], "two-process/default", 4U * 1024U * 1024U);
            if (!storage.available()) return 2;
            storage.replace_cookies({persistent_cookie("cross-process")});
            profile_local_storage local;
            local.keys = {"project"};
            local.values = {{"project", "restored"}};
            storage.replace_local_storage("https://process.test", std::move(local));
            return 0;
        }
        if (argc == 3 && std::string_view(argv[1]) == "--read-child") {
            browser_profile_storage storage(argv[2], "two-process/default", 4U * 1024U * 1024U);
            const auto state = storage.snapshot();
            const auto origin = state.local_storage.find("https://process.test");
            return state.cookies.size() == 1U
                    && state.cookies.front().value == "cross-process"
                    && origin != state.local_storage.end()
                    && origin->second.values.at("project") == "restored"
                ? 0 : 3;
        }
        test_round_trip_and_partitioning();
        test_concurrent_open_is_rejected();
        test_corruption_recovers_on_next_commit();
        test_interrupted_temporary_write_is_ignored();
        test_scoped_clear();
        test_two_process_restart(std::filesystem::absolute(argv[0]).string());
        test_repeated_close_and_growth_bounds();
        std::cout << "Browser profile storage contracts passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
