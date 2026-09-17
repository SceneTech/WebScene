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
    std::cerr << "webscene_css_effect_values_tests: " << message << '\n';
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

void execute_and_wait(webscene_engine* engine, std::string_view source, std::string_view name)
{
    webscene_engine_metrics before{};
    webscene_engine_get_metrics(engine, &before);
    require(webscene_engine_execute_script(
        engine, source.data(), source.size(), name.data(), name.size()) != 0,
        "script was rejected");
    for (auto attempt = 0; attempt < 1000; ++attempt) {
        webscene_engine_metrics after{};
        webscene_engine_get_metrics(engine, &after);
        if (after.script_errors > before.script_errors) {
            fail("script failed: " + last_error(engine));
        }
        if (after.executed_scripts > before.executed_scripts) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    fail("script did not complete within two seconds");
}
} // namespace

int main()
{
    require(webscene_engine_prewarm() != 0, "V8 prewarm failed");
    auto* engine = webscene_engine_create(0);
    require(engine != nullptr, "engine creation failed");
    execute_and_wait(engine, "document.body.textContent = ''", "native-effects-empty.js");

    webscene_engine_metrics before{};
    for (auto attempt = 0; attempt < 1000; ++attempt) {
        webscene_engine_get_metrics(engine, &before);
        if (before.dom_nodes != 0U) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(before.dom_nodes != 0U, "baseline DOM metrics were unavailable");
    webscene_engine_memory_metrics before_memory{sizeof(webscene_engine_memory_metrics)};
    require(webscene_engine_get_memory_metrics(engine, &before_memory) != 0,
        "baseline memory metrics were unavailable");

    execute_and_wait(engine, R"JS(
      (() => {
        const rules = document.createElement('style');
        rules.id = 'effect-rules';
        rules.textContent = `
          #effects > span { display: block; width: 8px; height: 2px;
            mask-image: linear-gradient(black, transparent); mask-size: 8px 2px;
            mask-position: 0px 0px; mask-repeat: no-repeat; mask-composite: add;
            clip-path: inset(1px); filter: brightness(0.5); backdrop-filter: blur(1px); }
          #effects.alternate > span { clip-path: circle(25%); filter: contrast(2); }
        `;
        document.head.appendChild(rules);
        const host = document.createElement('main');
        host.id = 'effects';
        const fragment = document.createDocumentFragment();
        for (let index = 0; index < 4096; index++) fragment.appendChild(document.createElement('span'));
        host.appendChild(fragment);
        document.body.appendChild(host);
        const first = host.firstElementChild;
        const style = getComputedStyle(first);
        if (style.getPropertyValue('filter') !== 'brightness(0.5)'
            || style.getPropertyValue('clip-path') !== 'inset(1px)'
            || style.getPropertyValue('mask-repeat') !== 'no-repeat'
            || first.offsetWidth !== 8 || first.offsetHeight !== 2) {
          throw new Error('initial effect values failed');
        }
      })()
    )JS", "native-effects-fixture.js");

    const auto started = std::chrono::steady_clock::now();
    for (auto cycle = 0; cycle < 100; ++cycle) {
        execute_and_wait(engine, R"JS(
          (() => {
            const host = document.getElementById('effects');
            host.classList.add('alternate');
            const first = host.firstElementChild;
            if (getComputedStyle(first).getPropertyValue('filter') !== 'contrast(2)'
                || getComputedStyle(first).getPropertyValue('clip-path') !== 'circle(25%)'
                || first.offsetWidth !== 8 || first.offsetHeight !== 2) {
              throw new Error('alternate effect values failed');
            }
          })()
        )JS", "native-effects-alternate.js");
        execute_and_wait(engine, R"JS(
          (() => {
            const host = document.getElementById('effects');
            host.classList.remove('alternate');
            const first = host.firstElementChild;
            if (getComputedStyle(first).getPropertyValue('filter') !== 'brightness(0.5)'
                || getComputedStyle(first).getPropertyValue('clip-path') !== 'inset(1px)') {
              throw new Error('restored effect values failed');
            }
          })()
        )JS", "native-effects-restored.js");
    }
    const auto elapsed = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - started).count();
    require(elapsed < 15000.0, "4096-effect, 100-cycle gate exceeded 15 seconds");
    require(webscene_engine_requires_animation_frame(engine) == 0U,
        "settled effect values retained host frame demand");

    webscene_engine_metrics peak{};
    webscene_engine_get_metrics(engine, &peak);
    webscene_engine_memory_metrics peak_memory{sizeof(webscene_engine_memory_metrics)};
    require(webscene_engine_get_memory_metrics(engine, &peak_memory) != 0,
        "peak memory metrics were unavailable");
    require(peak.dom_nodes >= before.dom_nodes + 4098U
        && peak.dom_nodes <= before.dom_nodes + 4102U,
        "fixture DOM node delta was outside its 4098..4102 bound");
    require(peak_memory.native_dom_textual_style_count
            > before_memory.native_dom_textual_style_count,
        "effect values were absent from native textual-style accounting");
    require(peak_memory.native_dom_textual_style_storage_bytes
            <= before_memory.native_dom_textual_style_storage_bytes
                + 32U * 1024U * 1024U,
        "effect textual-style storage exceeded its 32 MiB bound");

    execute_and_wait(engine, R"JS(
      document.getElementById('effects').remove();
      document.getElementById('effect-rules').remove();
    )JS", "native-effects-cleanup.js");
    require(webscene_engine_request_low_memory(engine) != 0, "cleanup request failed");
    webscene_engine_metrics after{};
    webscene_engine_memory_metrics after_memory{sizeof(webscene_engine_memory_metrics)};
    for (auto attempt = 0; attempt < 1000; ++attempt) {
        webscene_engine_get_metrics(engine, &after);
        require(webscene_engine_get_memory_metrics(engine, &after_memory) != 0,
            "post-cleanup memory metrics were unavailable");
        if (after_memory.low_memory_notifications > before_memory.low_memory_notifications
            && after.dom_nodes <= before.dom_nodes + 1U) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(after.dom_nodes <= before.dom_nodes + 1U,
        "effect nodes remained after lifecycle cleanup");
    require(after_memory.v8_used_heap_bytes
            <= before_memory.v8_used_heap_bytes + 32U * 1024U * 1024U,
        "post-cleanup V8 heap exceeded its 32 MiB bound");

    webscene_engine_destroy(engine);
    std::cout << "css-effect-values effects=4096 cycles=100 states=200 elapsed-ms="
              << elapsed << " retained-node-delta<="
              << (after.dom_nodes - before.dom_nodes)
              << " peak-textual-style-count-delta="
              << (peak_memory.native_dom_textual_style_count
                  - before_memory.native_dom_textual_style_count)
              << " peak-textual-style-bytes-delta="
              << (peak_memory.native_dom_textual_style_storage_bytes
                  - before_memory.native_dom_textual_style_storage_bytes)
              << '\n';
    return 0;
}
