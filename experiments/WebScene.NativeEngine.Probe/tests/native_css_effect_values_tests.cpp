#include "webscene_native_engine.h"

#include <chrono>
#include <cmath>
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

void execute_and_wait(webscene_engine* engine, std::string_view source, std::string_view name,
    int maximum_attempts = 1000)
{
    webscene_engine_metrics before{};
    webscene_engine_get_metrics(engine, &before);
    require(webscene_engine_execute_script(
        engine, source.data(), source.size(), name.data(), name.size()) != 0,
        "script was rejected");
    for (auto attempt = 0; attempt < maximum_attempts; ++attempt) {
        webscene_engine_metrics after{};
        webscene_engine_get_metrics(engine, &after);
        if (after.script_errors > before.script_errors) {
            fail("script failed: " + last_error(engine));
        }
        if (after.executed_scripts > before.executed_scripts) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    fail("script did not complete within its bounded wait");
}

struct clip_scene_counts final {
    uint64_t revision{};
    uint32_t inset_clip_begins{};
    uint32_t inset_clip_ends{};
    uint32_t clipped_fills{};
    uint32_t blur_filter_begins{};
    uint32_t command_count{};
    bool transform_clip_nested{};
};

clip_scene_counts wait_for_inset_clip_scene(webscene_engine* engine, uint32_t expected_count)
{
    const webscene_scene_acquire_options_v3 options{
        sizeof(webscene_scene_acquire_options_v3),
        WEBSCENE_SCENE_VIEW_VERSION_3,
        0U};
    clip_scene_counts latest{};
    for (auto attempt = 0; attempt < 1000; ++attempt) {
        const webscene_scene_view_v3* lease = nullptr;
        const auto status = webscene_engine_acquire_latest_scene_v3(
            engine, &options, &lease);
        if (status == WEBSCENE_SCENE_ACQUIRE_SUCCESS && lease != nullptr) {
            const auto* scene = lease->cpu_view;
            latest = {};
            if (scene != nullptr) {
                latest.revision = scene->header.revision;
                latest.command_count = scene->header.command_count;
                uint32_t transform_clip_node = 0U;
                auto transform_clip_stage = 0U;
                for (uint32_t index = 0; index < scene->header.command_count; ++index) {
                    const auto& command = scene->commands[index];
                    if (transform_clip_stage == 0U && command.kind == 15U) {
                        transform_clip_node = command.node_id;
                        transform_clip_stage = 1U;
                    } else if (command.node_id == transform_clip_node) {
                        if (transform_clip_stage == 1U && command.kind == 19U) {
                            transform_clip_stage = 2U;
                        } else if (transform_clip_stage == 2U && command.kind == 12U) {
                            transform_clip_stage = 3U;
                        } else if (transform_clip_stage == 3U && command.kind == 13U) {
                            transform_clip_stage = 4U;
                        } else if (transform_clip_stage == 4U && command.kind == 20U) {
                            transform_clip_stage = 5U;
                        } else if (transform_clip_stage == 5U && command.kind == 16U) {
                            latest.transform_clip_nested = true;
                            transform_clip_stage = 6U;
                        }
                    }
                    if (command.kind == 12U
                        && std::abs(command.width - 6.0F) < 0.01F
                        && std::abs(command.height - 2.0F) < 0.01F) {
                        ++latest.inset_clip_begins;
                    } else if (command.kind == 13U) {
                        ++latest.inset_clip_ends;
                    } else if ((command.kind == 1U || command.kind == 9U)
                        && command.rgba == 0x285078FFU) {
                        ++latest.clipped_fills;
                    } else if (command.kind == 30U
                        && (command.flags & (1U << 28U)) != 0U
                        && std::abs(command.stroke_width - 2.0F) < 0.01F) {
                        ++latest.blur_filter_begins;
                    }
                }
                webscene_scene_acknowledge_v3(lease);
            }
            webscene_scene_release_v3(lease);
            if (latest.inset_clip_begins == expected_count
                && latest.inset_clip_ends == expected_count
                && latest.clipped_fills == expected_count
                && (expected_count != 4096U || latest.transform_clip_nested)) return latest;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return latest;
}

uint64_t acknowledge_scene_after(webscene_engine* engine, uint64_t minimum_revision)
{
    const webscene_scene_acquire_options_v3 options{
        sizeof(webscene_scene_acquire_options_v3),
        WEBSCENE_SCENE_VIEW_VERSION_3,
        0U};
    for (auto attempt = 0; attempt < 2500; ++attempt) {
        const webscene_scene_view_v3* lease = nullptr;
        const auto status = webscene_engine_acquire_latest_scene_v3(
            engine, &options, &lease);
        if (status == WEBSCENE_SCENE_ACQUIRE_SUCCESS && lease != nullptr) {
            const auto revision = lease->cpu_view == nullptr
                ? 0U : lease->cpu_view->header.revision;
            if (revision > minimum_revision) {
                require(webscene_scene_acknowledge_v3(lease) != 0U,
                    "scene acknowledgement failed");
                webscene_scene_release_v3(lease);
                return revision;
            }
            webscene_scene_release_v3(lease);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    fail("effect mutation did not publish a newer scene within five seconds");
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
            background: rgb(40, 80, 120);
            mask-image: linear-gradient(black, transparent); mask-size: 8px 2px;
            mask-position: 0px 0px; mask-repeat: no-repeat; mask-composite: add;
            clip-path: inset(0px 1px); filter: brightness(0.5); backdrop-filter: blur(1px); }
          #effects.alternate > span { clip-path: circle(25%); filter: contrast(2); }
          #effects > span:first-child { transform: scale(1.25) rotate(3deg); }
          #effects > span:last-child { filter: blur(2px); }
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
            || style.getPropertyValue('clip-path') !== 'inset(0px 1px)'
            || style.getPropertyValue('mask-repeat') !== 'no-repeat'
            || first.offsetWidth !== 8 || first.offsetHeight !== 2) {
          throw new Error('initial effect values failed');
        }
        if (getComputedStyle(host.lastElementChild).getPropertyValue('filter') !== 'blur(2px)') {
          throw new Error('initial blur filter value failed');
        }
      })()
    )JS", "native-effects-fixture.js");

    const auto scale_started = std::chrono::steady_clock::now();
    // The explicit 25-second performance assertion below owns this workload's
    // budget. The dispatch wait includes one second of scheduling margin so a
    // slow run reports its measured gate instead of a generic timeout.
    execute_and_wait(engine, R"JS(
      (() => {
        const host = document.getElementById('effects');
        const first = host.firstElementChild;
        for (let cycle = 0; cycle < 100; ++cycle) {
          host.classList.add('alternate');
          if (getComputedStyle(first).getPropertyValue('filter') !== 'contrast(2)'
              || getComputedStyle(first).getPropertyValue('clip-path') !== 'circle(25%)'
              || first.offsetWidth !== 8 || first.offsetHeight !== 2) {
            throw new Error('scaled alternate effect values failed');
          }
          host.classList.remove('alternate');
          if (getComputedStyle(first).getPropertyValue('filter') !== 'brightness(0.5)'
              || getComputedStyle(first).getPropertyValue('clip-path') !== 'inset(0px 1px)') {
            throw new Error('scaled restored effect values failed');
          }
        }
      })()
    )JS", "native-effects-scaled-cycles.js", 13000);
    const auto scale_elapsed = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - scale_started).count();
    require(scale_elapsed < 25000.0, "4096-effect, 100-cycle gate exceeded 25 seconds");

    webscene_engine_metrics peak{};
    webscene_engine_get_metrics(engine, &peak);
    webscene_engine_memory_metrics peak_memory{sizeof(webscene_engine_memory_metrics)};
    for (auto attempt = 0; attempt < 500; ++attempt) {
        require(webscene_engine_get_memory_metrics(engine, &peak_memory) != 0,
            "peak memory metrics were unavailable");
        if (peak_memory.native_dom_textual_style_count
            > before_memory.native_dom_textual_style_count) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
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
    const auto peak_textual_style_count_delta =
        peak_memory.native_dom_textual_style_count
            - before_memory.native_dom_textual_style_count;
    const auto peak_textual_style_bytes_delta =
        peak_memory.native_dom_textual_style_storage_bytes
            - before_memory.native_dom_textual_style_storage_bytes;

    const auto initial_clip_scene = wait_for_inset_clip_scene(engine, 4096U);
    require(initial_clip_scene.inset_clip_begins == 4096U,
        "retained scene did not emit 4096 inset clip begin commands");
    require(initial_clip_scene.inset_clip_ends == 4096U,
        "retained scene did not emit 4096 balanced inset clip end commands");
    require(initial_clip_scene.clipped_fills == 4096U,
        "retained scene did not preserve all 4096 clipped fills");
    require(initial_clip_scene.transform_clip_nested,
        "transform commands did not wrap the inset clip scope");
    require(initial_clip_scene.blur_filter_begins == 1U,
        "retained scene did not emit the bounded foreground blur group");
    const auto initial_scene_command_bytes =
        static_cast<uint64_t>(initial_clip_scene.command_count)
            * sizeof(webscene_scene_command);
    require(initial_scene_command_bytes <= 4U * 1024U * 1024U,
        "4096 inset clips exceeded the 4 MiB retained-scene bound");

    webscene_engine_destroy(engine);
    engine = webscene_engine_create(0);
    require(engine != nullptr, "lifecycle engine creation failed");
    execute_and_wait(engine, "document.body.textContent = ''", "native-effects-lifecycle-empty.js");
    for (auto attempt = 0; attempt < 1000; ++attempt) {
        webscene_engine_get_metrics(engine, &before);
        if (before.dom_nodes != 0U) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(before.dom_nodes != 0U, "lifecycle baseline DOM metrics were unavailable");
    before_memory = {sizeof(webscene_engine_memory_metrics)};
    require(webscene_engine_get_memory_metrics(engine, &before_memory) != 0,
        "lifecycle baseline memory metrics were unavailable");

    execute_and_wait(engine, R"JS(
      (() => {
        const rules = document.createElement('style');
        rules.id = 'effect-rules';
        rules.textContent = `
          #effects > span { display: block; width: 8px; height: 2px;
            background: rgb(40, 80, 120);
            mask-image: linear-gradient(black, transparent); mask-size: 8px 2px;
            mask-position: 0px 0px; mask-repeat: no-repeat; mask-composite: add;
            clip-path: inset(0px 1px); filter: brightness(0.5); backdrop-filter: blur(1px); }
          #effects.alternate > span { clip-path: circle(25%); filter: contrast(2); }
        `;
        document.head.appendChild(rules);
        const host = document.createElement('main');
        host.id = 'effects';
        host.appendChild(document.createElement('span'));
        document.body.appendChild(host);
      })()
    )JS", "native-effects-lifecycle-fixture.js");

    auto scene_revision = wait_for_inset_clip_scene(engine, 1U).revision;
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
        scene_revision = acknowledge_scene_after(engine, scene_revision);
        execute_and_wait(engine, R"JS(
          (() => {
            const host = document.getElementById('effects');
            host.classList.remove('alternate');
            const first = host.firstElementChild;
            if (getComputedStyle(first).getPropertyValue('filter') !== 'brightness(0.5)'
                || getComputedStyle(first).getPropertyValue('clip-path') !== 'inset(0px 1px)') {
              throw new Error('restored effect values failed');
            }
          })()
        )JS", "native-effects-restored.js");
        scene_revision = acknowledge_scene_after(engine, scene_revision);
    }
    const auto elapsed = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - started).count();
    require(elapsed < 15000.0, "100-cycle retained-effect gate exceeded 15 seconds");
    require(webscene_engine_requires_animation_frame(engine) == 0U,
        "settled effect values retained host frame demand");

    execute_and_wait(engine, R"JS(
      document.getElementById('effects').remove();
      document.getElementById('effect-rules').remove();
    )JS", "native-effects-cleanup.js");
    scene_revision = acknowledge_scene_after(engine, scene_revision);
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
    const auto retained_heap_growth =
        after_memory.v8_used_heap_bytes > before_memory.v8_used_heap_bytes
        ? after_memory.v8_used_heap_bytes - before_memory.v8_used_heap_bytes
        : 0U;

    webscene_engine_destroy(engine);
    std::cout << "css-effect-values initial-effects=4096 scale-cycles=100 scale-elapsed-ms="
              << scale_elapsed
              << " publication-mutation-nodes=1 publication-cycles=100 publication-states=200"
              << " publication-elapsed-ms=" << elapsed << " retained-node-delta<="
              << (after.dom_nodes - before.dom_nodes)
              << " scene-commands=" << initial_clip_scene.command_count
              << " clip-begins=" << initial_clip_scene.inset_clip_begins
              << " clip-ends=" << initial_clip_scene.inset_clip_ends
              << " clipped-fills=" << initial_clip_scene.clipped_fills
              << " blur-filter-begins=" << initial_clip_scene.blur_filter_begins
              << " transform-clip-nested=" << initial_clip_scene.transform_clip_nested
              << " initial-scene-command-bytes=" << initial_scene_command_bytes
              << " peak-textual-style-count-delta=" << peak_textual_style_count_delta
              << " peak-textual-style-bytes-delta=" << peak_textual_style_bytes_delta
              << " retained-heap-growth=" << retained_heap_growth
              << '\n';
    return 0;
}
