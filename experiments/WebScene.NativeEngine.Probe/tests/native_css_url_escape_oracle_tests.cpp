#include "webscene_css_parser.h"
#include "webscene_native_engine.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <mutex>
#include <numeric>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
#include <sys/resource.h>

#if defined(__APPLE__)
#include <mach/mach.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

using namespace webscene_native;

namespace {
[[noreturn]] void fail(std::string_view message)
{
    std::cerr << "native_css_url_escape_oracle_tests: " << message << '\n';
    std::exit(1);
}

void require(bool condition, std::string_view message)
{
    if (!condition) fail(message);
}

uint64_t current_rss_bytes()
{
#if defined(__APPLE__)
    mach_task_basic_info_data_t info{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
            reinterpret_cast<task_info_t>(&info), &count) != KERN_SUCCESS) return 0U;
    return static_cast<uint64_t>(info.resident_size);
#elif defined(__linux__)
    std::ifstream statm("/proc/self/statm");
    uint64_t total_pages = 0U;
    uint64_t resident_pages = 0U;
    statm >> total_pages >> resident_pages;
    const auto page_size = sysconf(_SC_PAGESIZE);
    return page_size > 0
        ? resident_pages * static_cast<uint64_t>(page_size) : 0U;
#else
    return 0U;
#endif
}

uint64_t peak_rss_bytes()
{
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0) return 0U;
#if defined(__APPLE__)
    return static_cast<uint64_t>(usage.ru_maxrss);
#else
    return static_cast<uint64_t>(usage.ru_maxrss) * 1024U;
#endif
}

std::string last_error(webscene_engine* engine)
{
    const auto required = webscene_engine_copy_last_error(engine, nullptr, 0U);
    std::vector<char> buffer(required > 0U ? required : 1U, '\0');
    webscene_engine_copy_last_error(engine, buffer.data(), buffer.size());
    return buffer.data();
}

void execute_and_wait(
    webscene_engine* engine,
    std::string_view source,
    std::string_view name)
{
    webscene_engine_metrics before{};
    webscene_engine_get_metrics(engine, &before);
    require(webscene_engine_execute_script(
        engine, source.data(), source.size(), name.data(), name.size()) != 0U,
        "script was rejected");
    for (auto attempt = 0; attempt < 2500; ++attempt) {
        webscene_engine_metrics after{};
        webscene_engine_get_metrics(engine, &after);
        if (after.script_errors > before.script_errors) {
            fail("script failed: " + last_error(engine));
        }
        if (after.executed_scripts > before.executed_scripts) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    fail("script did not complete within five seconds");
}

struct resource_request final {
    uint32_t kind{};
    std::string address;
};

struct resource_probe final {
    std::mutex mutex;
    std::vector<resource_request> requests;
    std::atomic<uint32_t> active{0U};
    std::atomic<uint32_t> peak{0U};
    std::string document_address;
    std::string stylesheet_address;
    std::string document_content;
};

constexpr std::string_view decoded_loopback =
    "http://127.0.0.1:58410/oss-dev/vscode-remote-resource";

constexpr std::string_view stylesheet_content = R"CSS(
div { width: 8px; height: 8px }
.ordinary { background-image: url("https://assets.test/ordinary.svg") }
.escaped { background-image: url("http\:\/\/127\.0\.0\.1\:58410\/oss-dev\/vscode-remote-resource") }
.forbidden { background-image: url("j\61 vascript:alert(1)") }
.traversal { background-image: url("https\3a //assets.test/safe/..\2f secret.svg") }
)CSS";

constexpr std::string_view image_content =
    R"SVG(<svg xmlns="http://www.w3.org/2000/svg" width="1" height="1"><rect width="1" height="1"/></svg>)SVG";

size_t load_resource(
    void* user_data,
    uint32_t kind,
    const char* url,
    size_t url_length,
    const char*,
    size_t,
    int64_t,
    char* destination,
    size_t destination_capacity)
{
    auto& probe = *static_cast<resource_probe*>(user_data);
    const std::string address(url, url_length);
    {
        std::lock_guard lock(probe.mutex);
        if (std::none_of(probe.requests.begin(), probe.requests.end(),
                [&](const auto& request) {
                    return request.kind == kind && request.address == address;
                })) {
            probe.requests.push_back({kind, address});
        }
    }
    if (destination == nullptr) {
        const auto active = probe.active.fetch_add(1U, std::memory_order_relaxed) + 1U;
        auto peak = probe.peak.load(std::memory_order_relaxed);
        while (active > peak && !probe.peak.compare_exchange_weak(
            peak, active, std::memory_order_relaxed)) {
        }
        probe.active.fetch_sub(1U, std::memory_order_relaxed);
    }

    const auto content = address == probe.document_address
        ? std::string_view(probe.document_content)
        : address == probe.stylesheet_address ? stylesheet_content : image_content;
    constexpr size_t header_size = 2U + sizeof(uint32_t) + sizeof(int64_t) * 2U;
    const auto required = header_size + content.size();
    if (destination == nullptr || destination_capacity < required) return required;
    destination[0] = 1U;
    destination[1] = 1U;
    const uint32_t entity_tag_length = 0U;
    const int64_t timestamp = 0;
    std::memcpy(destination + 2U, &entity_tag_length, sizeof(entity_tag_length));
    std::memcpy(destination + 2U + sizeof(entity_tag_length),
        &timestamp, sizeof(timestamp));
    std::memcpy(destination + 2U + sizeof(entity_tag_length) + sizeof(timestamp),
        &timestamp, sizeof(timestamp));
    std::memcpy(destination + header_size, content.data(), content.size());
    return required;
}

class counting_sink final : public css_syntax_sink {
public:
    bool begin_rule(
        uint32_t,
        bool,
        size_t,
        std::string_view,
        std::string_view,
        size_t& rule_index) override
    {
        rule_index = rule_count++;
        return true;
    }

    bool declaration(std::string_view, std::string_view value, bool) override
    {
        ++declaration_count;
        url_byte_count += value.size();
        return true;
    }

    bool end_rule(size_t, size_t) override { return true; }

    size_t rule_count{0U};
    size_t declaration_count{0U};
    size_t url_byte_count{0U};
};

std::string token_fixture(size_t token_count)
{
    std::string css;
    css.reserve(token_count * 54U);
    css += ".bulk{";
    for (size_t index = 0; index < token_count; ++index) {
        css += "--u";
        css += std::to_string(index);
        css += ":url(\"https\\3a //assets.test/a\\2e svg#";
        css += std::to_string(index);
        css += "\");";
    }
    css += '}';
    return css;
}

struct parse_sample final {
    double milliseconds{};
    css_syntax_metrics metrics{};
    size_t input_bytes{};
};

parse_sample parse_tokens(size_t token_count)
{
    clear_css_syntax_process_cache();
    const auto fixture = token_fixture(token_count);
    counting_sink sink;
    const auto started = std::chrono::steady_clock::now();
    const auto result = stream_css_syntax_stylesheet(fixture, sink);
    const auto elapsed = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - started).count();
    require(static_cast<bool>(result), "bounded token fixture did not parse");
    require(result.metrics.parse_error_count == 0U,
        "bounded token fixture reported syntax errors");
    require(sink.rule_count == 1U, "bounded token fixture did not emit one rule");
    require(sink.declaration_count == token_count,
        "bounded token fixture declaration denominator changed");
    require(sink.url_byte_count >= token_count * 30U,
        "bounded token fixture did not expose every URL value");
    clear_css_syntax_process_cache();
    return {elapsed, result.metrics, fixture.size()};
}

bool contains_address(
    const std::vector<resource_request>& requests,
    std::string_view address)
{
    return std::any_of(requests.begin(), requests.end(), [&](const auto& request) {
        return request.address == address;
    });
}
} // namespace

int main()
{
    const auto fifty_thousand = parse_tokens(50'000U);
    const auto one_hundred_thousand = parse_tokens(100'000U);
    require(one_hundred_thousand.milliseconds < 30'000.0,
        "100,000-token parse exceeded 30 seconds");
    require(one_hundred_thousand.milliseconds
            <= std::max(100.0, fifty_thousand.milliseconds * 3.5),
        "100,000-token parse exceeded the bounded linear-work ratio");
    require(one_hundred_thousand.metrics.parser_retained_bytes == 0U,
        "streaming parser retained its Rust output");

    require(webscene_engine_prewarm() != 0U, "V8 prewarm failed");
    resource_probe probe;
    const auto run_id = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    probe.document_address =
        "https://css-escape.test/app/index.html?run=" + run_id;
    probe.stylesheet_address =
        "https://css-escape.test/app/theme.css?run=" + run_id;
    probe.document_content =
        "<!doctype html><html><head><link rel=\"stylesheet\" href=\"theme.css?run="
        + run_id + "\"></head><body>"
        "<div class=\"ordinary\"></div><div class=\"escaped\"></div>"
        "<div class=\"forbidden\"></div><div class=\"traversal\"></div>"
        "</body></html>";
    const webscene_engine_options options{
        sizeof(webscene_engine_options),
        64U,
        nullptr,
        0U,
        load_resource,
        &probe};
    auto* engine = webscene_engine_create_with_options(&options);
    require(engine != nullptr, "resource oracle engine creation failed");
    const auto& document_url = probe.document_address;
    require(webscene_engine_load_url(
        engine, document_url.data(), document_url.size()) != 0U,
        "resource oracle document load failed");

    execute_and_wait(engine, R"JS((() => {
      const values = Array.from(document.querySelectorAll('div'), element =>
        getComputedStyle(element).backgroundImage);
      globalThis.__cssUrlEscapeComputedValues = values;
      if (values.length !== 4 || !values[0].startsWith('url(')) {
        throw new Error('ordinary CSS URL control was not exposed through computed style');
      }
    })())JS", "css-url-escape-computed.js");

    for (auto attempt = 0; attempt < 500 && probe.active.load() != 0U; ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    std::vector<resource_request> requests;
    {
        std::lock_guard lock(probe.mutex);
        requests = probe.requests;
    }
    require(contains_address(requests, probe.document_address),
        "document fixture did not reach the resource callback");
    require(contains_address(requests, probe.stylesheet_address),
        "stylesheet fixture did not reach the resource callback");
    require(contains_address(requests, "https://assets.test/ordinary.svg"),
        "ordinary CSS URL control did not reach the resource callback");

    const auto decoded_requests = static_cast<size_t>(std::count_if(
        requests.begin(), requests.end(), [](const auto& request) {
            return request.address == decoded_loopback;
        }));
    const auto escaped_requests = static_cast<size_t>(std::count_if(
        requests.begin(), requests.end(), [](const auto& request) {
            return request.address.find('\\') != std::string::npos;
        }));
    const auto forbidden_requests = static_cast<size_t>(std::count_if(
        requests.begin(), requests.end(), [](const auto& request) {
            return request.address.starts_with("javascript:")
                || request.address.find("vascript") != std::string::npos;
        }));
    require(decoded_requests + escaped_requests > 0U,
        "escaped URL fixture produced neither a decoded nor escaped host request");

    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    webscene_engine_metrics lifecycle_before{};
    webscene_engine_get_metrics(engine, &lifecycle_before);
    const auto rss_before = current_rss_bytes();
    const auto lifecycle_started = std::chrono::steady_clock::now();
    execute_and_wait(engine, R"JS((() => {
      for (let index = 0; index < 100; index++) {
        const style = document.createElement('style');
        style.textContent = '.cycle{background-image:url("data\\3a image/svg+xml,%3Csvg/%3E")}';
        document.head.appendChild(style);
        document.body.className = 'cycle';
        getComputedStyle(document.body).backgroundImage;
        document.body.className = '';
        style.remove();
      }
    })())JS", "css-url-escape-lifecycle.js");
    const auto lifecycle_milliseconds = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - lifecycle_started).count();
    require(lifecycle_milliseconds < 10'000.0,
        "100 stylesheet attach/detach cycles exceeded 10 seconds");
    require(webscene_engine_request_low_memory(engine) != 0U,
        "lifecycle low-memory request failed");

    webscene_engine_metrics lifecycle_after{};
    webscene_engine_memory_metrics memory_after{
        sizeof(webscene_engine_memory_metrics)};
    for (auto attempt = 0; attempt < 2500; ++attempt) {
        webscene_engine_get_metrics(engine, &lifecycle_after);
        require(webscene_engine_get_memory_metrics(engine, &memory_after) != 0U,
            "lifecycle memory metrics were unavailable");
        if (memory_after.low_memory_notifications > 0U
            && lifecycle_after.dom_nodes <= lifecycle_before.dom_nodes + 1U) break;
        if (attempt == 1000) {
            require(webscene_engine_request_low_memory(engine) != 0U,
                "second lifecycle low-memory request failed");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    const auto rss_after = current_rss_bytes();
    const auto retained_node_delta = lifecycle_after.dom_nodes > lifecycle_before.dom_nodes
        ? lifecycle_after.dom_nodes - lifecycle_before.dom_nodes : 0U;
    const auto rss_growth = rss_after > rss_before ? rss_after - rss_before : 0U;

    std::cout << "css-url-escape tokens=100000 input-bytes="
              << one_hundred_thousand.input_bytes
              << " parse-50k-ms=" << fifty_thousand.milliseconds
              << " parse-100k-ms=" << one_hundred_thousand.milliseconds
              << " parser-allocations="
              << one_hundred_thousand.metrics.parser_allocation_count
              << " parser-peak-bytes="
              << one_hundred_thousand.metrics.parser_peak_bytes
              << " parser-retained-bytes="
              << one_hundred_thousand.metrics.parser_retained_bytes
              << " peak-rss-bytes=" << peak_rss_bytes()
              << " resource-requests=" << requests.size()
              << " resource-active-high-water=" << probe.peak.load()
              << " decoded-loopback-requests=" << decoded_requests
              << " escaped-host-requests=" << escaped_requests
              << " forbidden-host-requests=" << forbidden_requests
              << " native-decode-pass=" << (decoded_requests > 0U ? 1 : 0)
              << " security-admission-pass=" << (forbidden_requests == 0U ? 1 : 0)
              << " lifecycle-cycles=100 lifecycle-ms=" << lifecycle_milliseconds
              << " retained-node-delta=" << retained_node_delta
              << " rss-growth-bytes=" << rss_growth << '\n';

    require(probe.active.load() == 0U,
        "resource operations remained active after fixture settlement");
    require(retained_node_delta <= 1U,
        "stylesheet attach/detach cycles retained DOM nodes");
    require(rss_growth <= 32U * 1024U * 1024U,
        "stylesheet attach/detach cycles retained more than 32 MiB RSS");

    webscene_engine_destroy(engine);
    return 0;
}
