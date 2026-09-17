// These tests measure invalidation work, not wall-clock limits. Unrelated DOM
// and stylesheet growth must not amplify a fixed-size component mutation.
void test_compiled_css_invalidation_scaling()
{
    std::vector<std::array<uint64_t, 3>> observations;
    for (const auto unrelated_count : {32, 1024}) {
        auto* engine = webscene_engine_create(0);
        require(engine != nullptr, "compiled invalidation engine creation failed");
        resize(engine, 800, 600, 1);
        wait_for_consumed_inputs(engine, 1, "compiled invalidation viewport not applied");
        auto source = std::string{R"JS((count => {
          const style = document.createElement('style');
          style.textContent = `
            .local > .target { width:11px; height:13px; }
            .local[data-a][data-b] > .target { width:41px; }
            .local.active[data-c] > .target { height:43px; }
            .card { width:17px; }
            .card:not(:has([data-ready])) { width:47px; }
            .local.active + .sibling { width:53px; }
            .sibling { width:19px; }
            [class~="raw"] > .target { height:59px; }
          ` + Array.from({length: count}, (_, i) =>
            '.unrelated-' + i + '[data-unrelated-' + i + '] { width:7px; }').join('');
          document.body.innerHTML = '<div id="local" class="local active" data-a data-b data-c><div class="target"></div></div><div class="sibling"></div><div class="card"><span id="ready"></span></div>'
            + Array.from({length: count}, (_, i) => '<div class="unrelated-' + i + '"></div>').join('');
          document.head.appendChild(style);
          globalThis.local = document.getElementById('local');
          globalThis.target = local.firstElementChild;
          globalThis.ready = document.getElementById('ready');
          globalThis.card = ready.parentElement;
          globalThis.sibling = document.querySelector('.sibling');
          return [getComputedStyle(target).width, getComputedStyle(target).height,
            getComputedStyle(card).width, getComputedStyle(sibling).width];
        })(NUMBER))JS"};
        source.replace(source.find("NUMBER"), 6, std::to_string(unrelated_count));
        const auto setup = evaluate(engine, source, "compiled-invalidation-setup.js");
        require(setup == R"(["41px","43px","47px","53px"])",
            "compiled invalidation initial styles differ: " + setup);
        const auto listener_ready = evaluate(engine, R"JS((() => {
          addEventListener('resize', () => {
            local.removeAttribute('data-a');
            local.removeAttribute('data-b');
            local.classList.remove('active');
            local.removeAttribute('data-c');
            const checkpoint = [getComputedStyle(target).width,
              getComputedStyle(target).height, getComputedStyle(sibling).width];
            for (let i = 0; i < 100; ++i) {
              local.className = i % 2 ? 'local raw' : 'local';
              ready.setAttribute('data-ready', String(i));
            }
            globalThis.checkpoint = checkpoint;
          }, {once:true});
          return true;
        })())JS", "compiled-invalidation-listener.js");
        require(listener_ready == "true", "compiled invalidation listener not installed");
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        // Scene diagnostics are sampled at most once per 250 ms. Publish a
        // fresh snapshot explicitly; input consumption is not that barrier.
        std::this_thread::sleep_for(std::chrono::milliseconds(260));
        execute_and_wait_for_wrapper_retention_scene(engine,
            "document.body.style.backgroundColor = 'rgb(1, 2, 3)'",
            "compiled-invalidation-baseline-publication.js");
        const std::array<std::string_view, 3> names{
            "selector-invalidation-plan-lookups",
            "selector-invalidation-candidate-visits",
            "selector-invalidation-fallback-visits"};
        std::array<uint64_t, 3> before{};
        for (size_t i = 0; i < names.size(); ++i)
            before[i] = runtime_counter_diagnostic(engine, names[i]);
#endif
        const auto started = std::chrono::steady_clock::now();
        resize(engine, 960, 600, 2);
        wait_for_consumed_inputs(engine, 2, "compiled invalidation resize not consumed");
        std::string result;
        for (size_t attempt = 0; attempt < 250; ++attempt) {
            result = evaluate(engine, R"JS(
              typeof checkpoint === 'undefined' ? null :
                [checkpoint, getComputedStyle(target).height, getComputedStyle(card).width]
            )JS", "compiled-invalidation-result.js");
            if (result != "null") break;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        require(result == R"([["11px","13px","19px"],"59px","17px"])",
            "compiled invalidation lost batched transitions: " + result);
        const auto elapsed = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started).count();
        std::cout << "css-invalidation-scaling unrelated=" << unrelated_count
            << " resize-and-check-ms=" << elapsed;
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        std::this_thread::sleep_for(std::chrono::milliseconds(260));
        execute_and_wait_for_wrapper_retention_scene(engine,
            "document.body.style.backgroundColor = 'rgb(3, 2, 1)'",
            "compiled-invalidation-result-publication.js");
        std::array<uint64_t, 3> delta{};
        for (size_t i = 0; i < names.size(); ++i) {
            delta[i] = runtime_counter_diagnostic(engine, names[i]) - before[i];
            std::cout << ' ' << names[i] << '=' << delta[i];
        }
        require(delta[0] > 0 && delta[0] <= 40 && delta[1] <= 40 && delta[2] == 0,
            "local transitions performed excessive work or used document fallback");
        observations.push_back(delta);
#endif
        std::cout << '\n';
        webscene_engine_destroy(engine);
    }
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
    require(observations[0] == observations[1],
        "unrelated DOM/rule growth amplified local selector invalidation work");
#endif
}
