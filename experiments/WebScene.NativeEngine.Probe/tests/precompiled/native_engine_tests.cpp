// Exercises the production compiler/isolate/snapshot and the engine's normal
// script-loading path. No persistent cache directory is supplied.
#include "webscene_precompiled_javascript.h"
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {
void check(bool ok, const std::string& text) {
    if (!ok) { std::cerr << text << '\n'; std::exit(1); }
}
int install(const webscene_precompiled_javascript_v1* value, void* opaque) {
    auto descriptor = *value;
    if (opaque) descriptor.v8_version = "deliberately-incompatible-v8";
    return webscene_register_precompiled_javascript_v1(&descriptor);
}
std::string last_error(webscene_engine* engine) {
    auto size = webscene_engine_copy_last_error(engine, nullptr, 0);
    std::vector<char> text(size ? size : 1, '\0');
    webscene_engine_copy_last_error(engine, text.data(), text.size());
    return text.data();
}
}
int main(int argc, char**) {
    const bool reject = argc > 1;
    const std::string source = R"JS(
        (() => {
            const button = document.createElement('button');
            const label = document.createElement('span');
            document.body.appendChild(button);
            document.body.appendChild(label);
            let count = 40;
            button.addEventListener('click', () => { label.textContent = String(++count); });
            button.dispatchEvent(new Event('click'));
            button.dispatchEvent(new Event('click'));
            if (label.textContent !== '42') throw new Error('precompiled DOM closure failed');
            button.remove(); label.remove();
        })();
    )JS";
    char error[2048]{};
    check(webscene_precompile_javascript_v1(source.data(), source.size(), "build/counter.js", 0,
        install, reject ? &error : nullptr, error, sizeof(error)) == 0, error);
    auto* engine = webscene_engine_create(0);
    check(engine != nullptr, "Unable to create native V8 engine");
    const std::string name = "file:///relocated/application/counter.js";
    webscene_engine_metrics before{};
    webscene_engine_get_metrics(engine, &before);
    check(webscene_engine_execute_script(engine, source.data(), source.size(), name.data(), name.size()) != 0,
        "Engine rejected script request");
    bool completed = false;
    for (unsigned i = 0; i != 500; ++i) {
        webscene_engine_metrics after{};
        webscene_engine_get_metrics(engine, &after);
        if (after.script_errors > before.script_errors) {
            check(reject, last_error(engine));
            check(last_error(engine).find("Precompiled JavaScript") != std::string::npos, last_error(engine));
            completed = true; break;
        }
        if (after.executed_scripts > before.executed_scripts) {
            check(!reject, "Incompatible cache executed a source fallback");
            completed = true; break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    check(completed, "Timed out waiting for native script execution");
    webscene_precompiled_javascript_stats_v1 stats{};
    stats.struct_size = sizeof(stats); stats.version = 1;
    check(webscene_get_precompiled_javascript_stats_v1(&stats) == 0, "Stats unavailable");
    check(stats.registered_scripts == 1, "Expected exactly one application cache");
    check(reject ? stats.cache_rejections == 1 : stats.cache_hits == 1, "The normal loading path did not consume/reject the packaged cache");
    webscene_engine_destroy(engine);
    std::cout << "precompiled native DOM test passed (reject=" << reject << ")\n";
}
