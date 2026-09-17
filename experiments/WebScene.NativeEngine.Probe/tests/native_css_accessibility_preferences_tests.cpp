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
    std::cerr << "webscene_css_accessibility_preferences_tests: " << message << '\n';
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
    execute_and_wait(engine, "document.body.textContent = ''", "native-accessibility-empty.js");
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
        document.body.textContent = '';
        const style = document.createElement('style');
        style.id = 'accessibility-rules';
        style.textContent = `
          button { color: CanvasText; background-color: Canvas; width: 20px; height: 8px; box-sizing: border-box;
            color-scheme: light dark; accent-color: AccentColor; transition: opacity 2s linear; }
          @media (forced-colors: active) { button { color: HighlightText; background-color: Highlight; width: 21px; } }
          @media (prefers-contrast: more) { button { height: 9px; } }
          @media (prefers-reduced-motion: reduce) { button { padding-left: 2px; transition-duration: 0s; } }
        `;
        document.head.appendChild(style);
        const host = document.createElement('main');
        host.id = 'accessibility-controls';
        const fragment = document.createDocumentFragment();
        for (let index = 0; index < 4096; index++) {
          const button = document.createElement('button');
          button.textContent = String(index);
          fragment.appendChild(button);
        }
        host.appendChild(fragment);
        document.body.appendChild(host);
        const first = host.firstElementChild;
        const computed = getComputedStyle(first);
        if (!matchMedia('(forced-colors: none)').matches
            || !matchMedia('(prefers-contrast: no-preference)').matches
            || !matchMedia('(prefers-reduced-motion: no-preference)').matches
            || first.offsetWidth !== 20 || first.offsetHeight !== 8
            || computed.color === computed.backgroundColor
            || computed.getPropertyValue('color-scheme') !== 'light dark'
            || !/^(auto|rgb)/.test(computed.getPropertyValue('accent-color'))
            || computed.transitionDuration !== '2s') {
          throw new Error('initial accessibility preference state failed');
        }
      })()
    )JS", "native-accessibility-baseline.js");

    require(webscene_engine_set_accessibility_preferences_v1(engine, 1U << 8U) == 0,
        "unknown preference flags were accepted");

    const auto all_preferences =
        WEBSCENE_ACCESSIBILITY_PREFERENCE_FORCED_COLORS_V1
        | WEBSCENE_ACCESSIBILITY_PREFERENCE_REDUCED_MOTION_V1
        | WEBSCENE_ACCESSIBILITY_PREFERENCE_MORE_CONTRAST_V1;
    const auto started = std::chrono::steady_clock::now();
    for (auto cycle = 0; cycle < 100; ++cycle) {
        require(webscene_engine_set_accessibility_preferences_v1(engine, all_preferences) != 0,
            "enabling accessibility preferences failed");
        execute_and_wait(engine, R"JS(
          (() => {
            const first = document.querySelector('#accessibility-controls button');
            if (!matchMedia('(forced-colors: active)').matches
                || !matchMedia('(prefers-contrast: more)').matches
                || !matchMedia('(prefers-reduced-motion: reduce)').matches
                || first.offsetWidth !== 21 || first.offsetHeight !== 9
                || parseFloat(getComputedStyle(first).paddingLeft) !== 2
                || getComputedStyle(first).transitionDuration !== '0s') {
              throw new Error('enabled accessibility preference state failed: ' + JSON.stringify({
                forced: matchMedia('(forced-colors: active)').matches,
                contrast: matchMedia('(prefers-contrast: more)').matches,
                motion: matchMedia('(prefers-reduced-motion: reduce)').matches,
                width: first.offsetWidth,
                height: first.offsetHeight,
                padding: getComputedStyle(first).paddingLeft
              }));
            }
          })()
        )JS", "native-accessibility-enabled.js");
        require(webscene_engine_set_accessibility_preferences_v1(
            engine, WEBSCENE_ACCESSIBILITY_PREFERENCE_NONE_V1) != 0,
            "disabling accessibility preferences failed");
        execute_and_wait(engine, R"JS(
          (() => {
            const first = document.querySelector('#accessibility-controls button');
            if (!matchMedia('(forced-colors: none)').matches
                || !matchMedia('(prefers-contrast: no-preference)').matches
                || !matchMedia('(prefers-reduced-motion: no-preference)').matches
                || first.offsetWidth !== 20 || first.offsetHeight !== 8
                || parseFloat(getComputedStyle(first).paddingLeft) !== 0
                || getComputedStyle(first).transitionDuration !== '2s') {
              throw new Error('restored accessibility preference state failed: ' + JSON.stringify({
                forced: matchMedia('(forced-colors: none)').matches,
                contrast: matchMedia('(prefers-contrast: no-preference)').matches,
                motion: matchMedia('(prefers-reduced-motion: no-preference)').matches,
                width: first.offsetWidth,
                height: first.offsetHeight,
                padding: getComputedStyle(first).paddingLeft
              }));
            }
          })()
        )JS", "native-accessibility-restored.js");
    }
    const auto elapsed = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - started).count();
    require(elapsed < 15000.0, "4096-control, 100-cycle gate exceeded 15 seconds");
    require(webscene_engine_requires_animation_frame(engine) == 0U,
        "settled accessibility preferences retained host frame demand");

    webscene_engine_metrics peak{};
    webscene_engine_get_metrics(engine, &peak);
    require(peak.dom_nodes >= before.dom_nodes + 8194U
        && peak.dom_nodes <= before.dom_nodes + 8198U,
        "fixture DOM node delta was outside its 8194..8198 bound");

    execute_and_wait(engine, R"JS(
      document.getElementById('accessibility-controls').remove();
      document.getElementById('accessibility-rules').remove();
    )JS", "native-accessibility-cleanup.js");
    require(webscene_engine_request_low_memory(engine) != 0, "cleanup request failed");
    webscene_engine_metrics after{};
    webscene_engine_memory_metrics after_memory{sizeof(webscene_engine_memory_metrics)};
    for (auto attempt = 0; attempt < 1000; ++attempt) {
        webscene_engine_get_metrics(engine, &after);
        require(webscene_engine_get_memory_metrics(engine, &after_memory) != 0,
            "post-cleanup memory metrics were unavailable");
        if (after_memory.low_memory_notifications
                > before_memory.low_memory_notifications
            && after.dom_nodes <= before.dom_nodes + 1U) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(after.dom_nodes <= before.dom_nodes + 1U,
        "control nodes remained after lifecycle cleanup (before="
            + std::to_string(before.dom_nodes) + ", after="
            + std::to_string(after.dom_nodes) + ")");
    require(after_memory.v8_used_heap_bytes
            <= before_memory.v8_used_heap_bytes + 32U * 1024U * 1024U,
        "post-cleanup V8 heap exceeded its 32 MiB bound");

    webscene_engine_destroy(engine);
    std::cout << "css-accessibility-preferences controls=4096 cycles=100 states=200 elapsed-ms="
              << elapsed << " retained-node-delta<="
              << (after.dom_nodes - before.dom_nodes)
              << " post-cleanup-heap-growth<=" << 32U * 1024U * 1024U << "\n";
    return 0;
}
