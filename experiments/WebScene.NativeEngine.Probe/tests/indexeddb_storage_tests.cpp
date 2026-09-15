#include "webscene_indexeddb_storage.h"

#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <process.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {

using webscene_native::indexeddb_storage;
using webscene_native::indexeddb_storage_result;
using webscene_native::indexeddb_storage_status;

#if defined(_WIN32)
using child_process = intptr_t;
#else
using child_process = pid_t;
#endif

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

class temporary_directory final {
public:
    temporary_directory()
        : path_(std::filesystem::temp_directory_path()
            / ("webscene-indexeddb-"
                + std::to_string(std::chrono::steady_clock::now()
                    .time_since_epoch().count())))
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

void test_round_trip_revision_and_partitioning()
{
    temporary_directory root;
    indexeddb_storage storage(root.path(), "app-profile", 4U * 1024U * 1024U);
    const std::vector<uint8_t> value{0U, 1U, 2U, 0xffU};
    const auto stored = storage.store_sync("https://example.test", "state", 0U, value);
    require(stored.status == indexeddb_storage_status::ok && stored.revision == 1U,
        "first commit did not create revision one");
    const auto loaded = storage.load_sync("https://example.test", "state");
    require(loaded.status == indexeddb_storage_status::ok
            && loaded.revision == 1U && loaded.bytes == value,
        "committed bytes did not round trip");
    require(storage.load_sync("https://other.test", "state").status
            == indexeddb_storage_status::not_found,
        "a distinct origin observed another origin's database");
    indexeddb_storage other_profile(root.path(), "other-profile", 4U * 1024U * 1024U);
    require(other_profile.load_sync("https://example.test", "state").status
            == indexeddb_storage_status::not_found,
        "a distinct profile observed another profile's database");
}

void test_conflict_preserves_prior_commit()
{
    temporary_directory root;
    indexeddb_storage storage(root.path(), "profile", 4U * 1024U * 1024U);
    const std::vector<uint8_t> first{1U, 2U, 3U};
    const std::vector<uint8_t> second{9U, 8U, 7U};
    require(storage.store_sync("http://loopback", "state", 0U, first).status
            == indexeddb_storage_status::ok,
        "initial commit failed");
    const auto conflict = storage.store_sync("http://loopback", "state", 0U, second);
    require(conflict.status == indexeddb_storage_status::conflict,
        "stale transaction was not rejected");
    require(storage.load_sync("http://loopback", "state").bytes == first,
        "failed transaction changed the prior commit");
}

child_process spawn_commit_child(
    const std::string& executable,
    const std::filesystem::path& root,
    const char* value)
{
    const auto root_text = root.string();
#if defined(_WIN32)
    return _spawnl(_P_NOWAIT, executable.c_str(), executable.c_str(),
        "--commit-child", root_text.c_str(), value, nullptr);
#else
    const auto process = ::fork();
    if (process == 0) {
        ::execl(executable.c_str(), executable.c_str(), "--commit-child",
            root_text.c_str(), value, static_cast<char*>(nullptr));
        _exit(127);
    }
    return process;
#endif
}

int wait_for_child(child_process process)
{
#if defined(_WIN32)
    int status = 0;
    return _cwait(&status, process, 0) < 0 ? -1 : status;
#else
    int status = 0;
    if (::waitpid(process, &status, 0) < 0 || !WIFEXITED(status)) return -1;
    return WEXITSTATUS(status);
#endif
}

void test_cross_process_conflict(const std::string& executable)
{
    temporary_directory root;
    indexeddb_storage storage(root.path(), "profile", 4U * 1024U * 1024U);
    require(storage.store_sync("http://loopback", "cross-process", 0U, {1U}).status
            == indexeddb_storage_status::ok,
        "cross-process fixture commit failed");
    const auto first = spawn_commit_child(executable, root.path(), "7");
    const auto second = spawn_commit_child(executable, root.path(), "9");
    require(first > 0 && second > 0, "cross-process writers did not start");
    const auto first_status = wait_for_child(first);
    const auto second_status = wait_for_child(second);
    require((first_status == 0 && second_status == 10)
            || (first_status == 10 && second_status == 0),
        "cross-process stale writer was not rejected exactly once");
    const auto committed = storage.load_sync("http://loopback", "cross-process");
    require(committed.status == indexeddb_storage_status::ok
            && committed.revision == 2U && committed.bytes.size() == 1U
            && (committed.bytes[0] == 7U || committed.bytes[0] == 9U),
        "cross-process conflict changed the durable winner");
}

void test_corruption_and_quota_are_honest()
{
    temporary_directory root;
    indexeddb_storage storage(root.path(), "profile", 1024U * 1024U);
    std::vector<uint8_t> large(700U * 1024U, 0x5aU);
    require(storage.store_sync("https://quota.test", "first", 0U, large).status
            == indexeddb_storage_status::ok,
        "in-quota payload was rejected");
    require(storage.store_sync("https://quota.test", "second", 0U, large).status
            == indexeddb_storage_status::quota_exceeded,
        "profile quota was not enforced");

    std::filesystem::path committed;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root.path())) {
        if (entry.path().extension() == ".wsidb") {
            committed = entry.path();
            break;
        }
    }
    require(!committed.empty(), "committed database file was not found");
    std::fstream file(committed, std::ios::binary | std::ios::in | std::ios::out);
    file.seekg(-1, std::ios::end);
    const auto prior = file.get();
    require(prior != std::char_traits<char>::eof(), "committed database file was empty");
    file.seekp(-1, std::ios::end);
    file.put(static_cast<char>(prior ^ 0xff));
    file.close();
    require(storage.load_sync("https://quota.test", "first").status
            == indexeddb_storage_status::corrupt,
        "corrupt content was accepted");
    require(storage.erase_sync("https://quota.test", "first").status
            == indexeddb_storage_status::ok
            && storage.load_sync("https://quota.test", "first").status
                == indexeddb_storage_status::not_found,
        "explicit deletion did not recover a corrupt database");
}

void test_interrupted_write_and_stale_lock_recovery()
{
    temporary_directory root;
    indexeddb_storage storage(root.path(), "profile", 4U * 1024U * 1024U);
    const std::vector<uint8_t> first{1U, 3U, 5U, 7U};
    const std::vector<uint8_t> second{2U, 4U, 6U, 8U};
    require(storage.store_sync("https://recovery.test", "state", 0U, first).status
            == indexeddb_storage_status::ok,
        "interruption fixture commit failed");

    std::filesystem::path committed;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root.path())) {
        if (entry.path().extension() == ".wsidb") {
            committed = entry.path();
            break;
        }
    }
    require(!committed.empty(), "interruption fixture file was not found");
    const auto abandoned = std::filesystem::path(committed.string() + ".999.1.tmp");
    std::ofstream(abandoned, std::ios::binary | std::ios::trunc).write("WSID", 4);
    const auto stale_lock = std::filesystem::path(committed.string() + ".lock");
    std::filesystem::create_directory(stale_lock);
    std::filesystem::last_write_time(
        stale_lock,
        std::filesystem::file_time_type::clock::now() - std::chrono::minutes(1));

    indexeddb_storage restarted(root.path(), "profile", 4U * 1024U * 1024U);
    const auto recovered = restarted.load_sync("https://recovery.test", "state");
    require(recovered.status == indexeddb_storage_status::ok
            && recovered.revision == 1U && recovered.bytes == first,
        "an abandoned temporary write hid the prior durable revision");
    const auto committed_after_restart = restarted.store_sync(
        "https://recovery.test", "state", 1U, second);
    require(committed_after_restart.status == indexeddb_storage_status::ok
            && committed_after_restart.revision == 2U,
        "a stale crash lock prevented the next atomic commit");
    require(restarted.load_sync("https://recovery.test", "state").bytes == second,
        "post-interruption commit did not replace the prior revision");
}

void test_async_io_and_commit_throughput()
{
    temporary_directory root;
    indexeddb_storage storage(root.path(), "profile", 8U * 1024U * 1024U);
    std::mutex mutex;
    std::condition_variable ready;
    bool completed = false;
    std::thread::id callback_thread;
    const auto caller_thread = std::this_thread::get_id();
    storage.store("https://async.test", "state", 0U, {1U, 2U, 3U},
        [&](indexeddb_storage_result result) {
            require(result.status == indexeddb_storage_status::ok,
                "asynchronous store failed");
            {
                std::lock_guard lock(mutex);
                callback_thread = std::this_thread::get_id();
                completed = true;
            }
            ready.notify_one();
        });
    {
        std::unique_lock lock(mutex);
        require(ready.wait_for(lock, std::chrono::seconds(5), [&] { return completed; }),
            "asynchronous store did not complete");
    }
    require(callback_thread != caller_thread, "disk callback ran on the caller thread");

    std::vector<uint8_t> payload(4096U, 0x42U);
    auto revision = uint64_t{1U};
    constexpr uint64_t iterations = 100U;
    const auto started = std::chrono::steady_clock::now();
    for (uint64_t index = 0; index < iterations; ++index) {
        payload[0] = static_cast<uint8_t>(index);
        const auto result = storage.store_sync(
            "https://async.test", "state", revision, payload);
        require(result.status == indexeddb_storage_status::ok,
            "throughput commit failed");
        revision = result.revision;
    }
    const auto elapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - started).count();
    require(elapsed < 10.0, "100 small durable commits exceeded ten seconds");
    std::cout << "indexeddb durable commits/s="
        << static_cast<double>(iterations) / elapsed << '\n';
}

} // namespace

int main(int argc, char** argv)
{
    try {
        if (argc == 4 && std::string_view(argv[1]) == "--commit-child") {
            indexeddb_storage storage(argv[2], "profile", 4U * 1024U * 1024U);
            const auto value = static_cast<uint8_t>(std::stoi(argv[3]));
            const auto result = storage.store_sync(
                "http://loopback", "cross-process", 1U, {value});
            if (result.status == indexeddb_storage_status::ok) return 0;
            if (result.status == indexeddb_storage_status::conflict) return 10;
            return 20;
        }
        test_round_trip_revision_and_partitioning();
        test_conflict_preserves_prior_commit();
        test_cross_process_conflict(std::filesystem::absolute(argv[0]).string());
        test_corruption_and_quota_are_honest();
        test_interrupted_write_and_stale_lock_recovery();
        test_async_io_and_commit_throughput();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
