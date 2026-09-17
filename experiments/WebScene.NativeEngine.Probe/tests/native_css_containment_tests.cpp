#include "webscene_native_engine.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {
[[noreturn]] void fail(std::string_view message)
{
    std::cerr << "webscene_css_containment_tests: " << message << '\n';
    std::exit(1);
}

void require(bool condition, std::string_view message)
{
    if (!condition) fail(message);
}

std::string last_error(webscene_engine* engine)
{
    const auto required = webscene_engine_copy_last_error(engine, nullptr, 0);
    std::vector<char> buffer(required > 0 ? required : 1U, '\0');
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
    require(
        webscene_engine_execute_script(
            engine, source.data(), source.size(), name.data(), name.size()) != 0,
        "script was rejected");
    for (auto attempt = 0; attempt < 500; ++attempt) {
        webscene_engine_metrics after{};
        webscene_engine_get_metrics(engine, &after);
        if (after.script_errors > before.script_errors) {
            fail("script failed: " + last_error(engine));
        }
        if (after.executed_scripts > before.executed_scripts) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    fail("script did not complete within one second");
}
} // namespace

int main()
{
    require(webscene_engine_prewarm() != 0, "V8 prewarm failed");
    auto* engine = webscene_engine_create(0);
    require(engine != nullptr, "engine creation failed");
    execute_and_wait(engine, "document.body.textContent = ''", "native-containment-baseline.js");

    webscene_engine_memory_metrics before{sizeof(webscene_engine_memory_metrics)};
    require(webscene_engine_get_memory_metrics(engine, &before) != 0,
        "baseline memory metrics were unavailable");
    webscene_engine_metrics before_dom{};
    for (auto attempt = 0; attempt < 500; ++attempt) {
        webscene_engine_get_metrics(engine, &before_dom);
        if (before_dom.dom_nodes != 0U) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(before_dom.dom_nodes != 0U, "baseline DOM metrics were unavailable");

    const auto started = std::chrono::steady_clock::now();
    execute_and_wait(engine, R"JS(
      (() => {
        const rules = document.createElement('style');
        rules.id = 'containment-gate-rules';
        rules.textContent = `
          #containment-gate { width: 320px; container: workspace / inline-size; }
          #containment-gate-target { width: 10cqi; height: 10px; padding-left: 1px; }
          @container workspace (width >= 300px) {
            #containment-gate-target { padding-left: 17px; }
          }
          #containment-gate-skipped { content-visibility: hidden; contain-intrinsic-size: 40px; }
          #containment-gate-skipped > span { display: block; height: 1px; }
        `;
        document.head.appendChild(rules);
        const host = document.createElement('div');
        host.id = 'containment-gate';
        host.innerHTML = '<div id="containment-gate-target"></div>'
          + '<div id="containment-gate-skipped"></div>';
        document.body.appendChild(host);
        const target = document.getElementById('containment-gate-target');
        const skipped = document.getElementById('containment-gate-skipped');
        for (let index = 0; index < 4096; index++) {
          skipped.appendChild(document.createElement('span'));
        }
        if (target.offsetWidth !== 49) throw new Error('initial container query failed');
        host.style.width = '240px';
        if (target.offsetWidth !== 25) throw new Error('shrinking threshold failed');
        host.style.width = '320px';
        if (target.offsetWidth !== 49) throw new Error('resize-back threshold failed');
        if (skipped.offsetHeight !== 40 || skipped.firstChild.offsetHeight !== 0) {
          throw new Error('hidden intrinsic geometry failed');
        }
        for (let cycle = 0; cycle < 100; cycle++) {
          skipped.style.setProperty('content-visibility', 'visible');
          void skipped.offsetHeight;
          skipped.style.setProperty('content-visibility', 'hidden');
          void skipped.offsetHeight;
        }
      })()
    )JS", "native-containment-lifecycle-gate.js");
    const auto elapsed = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - started).count();
    require(elapsed < 10000.0, "4096-descendant, 100-cycle gate exceeded 10 seconds");

    webscene_engine_metrics peak_dom{};
    for (auto attempt = 0; attempt < 500; ++attempt) {
        webscene_engine_get_metrics(engine, &peak_dom);
        if (peak_dom.dom_nodes >= before_dom.dom_nodes + 4098U) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(peak_dom.dom_nodes >= before_dom.dom_nodes + 4098U,
        "fixture nodes were absent from the peak worker snapshot");
    require(peak_dom.dom_nodes <= before_dom.dom_nodes + 4102U,
        "fixture exceeded its DOM node bound");

    execute_and_wait(engine, R"JS(
      document.getElementById('containment-gate').remove();
      document.getElementById('containment-gate-rules').remove();
    )JS", "native-containment-lifecycle-cleanup.js");
    webscene_engine_memory_metrics after{sizeof(webscene_engine_memory_metrics)};
    webscene_engine_metrics after_dom{};
    require(webscene_engine_request_low_memory(engine) != 0,
        "post-cleanup collection request failed");
    for (auto attempt = 0; attempt < 500; ++attempt) {
        require(webscene_engine_get_memory_metrics(engine, &after) != 0,
            "post-cleanup memory metrics were unavailable");
        webscene_engine_get_metrics(engine, &after_dom);
        if (after.low_memory_notifications > before.low_memory_notifications
            && after_dom.dom_nodes <= before_dom.dom_nodes) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(after_dom.dom_nodes <= before_dom.dom_nodes,
        "fixture nodes remained after cleanup (before="
            + std::to_string(before_dom.dom_nodes) + ", after="
            + std::to_string(after_dom.dom_nodes) + ")");
    require(after.v8_used_heap_bytes <= before.v8_used_heap_bytes + 32U * 1024U * 1024U,
        "post-cleanup V8 heap exceeded its 32 MiB bound");

    webscene_engine_destroy(engine);
    std::cout << "css-containment-lifecycle descendants=4096 cycles=100 elapsed-ms="
              << elapsed << " peak-node-delta="
              << (peak_dom.dom_nodes - before_dom.dom_nodes)
              << " retained-node-delta=0\n";
    return 0;
}
