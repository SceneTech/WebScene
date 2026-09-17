// These tests measure invalidation work, not wall-clock limits. Unrelated DOM
// and stylesheet growth must not amplify a fixed-size component mutation.
void test_compiled_css_invalidation_scaling()
{
    std::vector<std::array<uint64_t, 7>> observations;
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
        const std::array<std::string_view, 7> names{
            "selector-invalidation-plan-lookups",
            "selector-invalidation-candidate-visits",
            "selector-invalidation-fallback-visits",
            "css-compound-match-checks", "css-rule-match-checks",
            "css-cascade-applications", "css-cascade-candidate-checks"};
        std::array<uint64_t, 7> before{};
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
        std::array<uint64_t, 7> delta{};
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

void test_compiled_css_route_scaling(bool structural_only = false)
{
    // Two independent axes: grow the affected component and grow unrelated
    // DOM/rules. Actual matching/cascade work must follow only the first axis.
    for (const auto* shape : {"descendants", "siblings", "disabled", "variables", "relational", "identity", "dataset",
            "tree-wide-position", "tree-position", "tree-siblings", "tree-relational", "tree-empty"}) {
        if (structural_only && !std::string_view(shape).starts_with("tree-")) continue;
        for (const auto affected_count : {8, 128}) {
            std::vector<std::array<uint64_t, 8>> observations;
            for (const auto unrelated_count : {32, 1024}) {
                auto* engine = webscene_engine_create(0);
                require(engine != nullptr, "CSS route scaling engine creation failed");
                resize(engine, 800, 600, 1);
                wait_for_consumed_inputs(engine, 1, "CSS route scaling viewport not applied");
                auto source = std::string{R"JS(((shape, count, unrelated) => {
                  const style = document.createElement('style');
                  const target = '<div class="matrix-target"></div>';
                  let markup = '';
                  let rule = '';
                  if (shape === 'descendants') {
                    markup = '<div id="trigger">' + ('<div>' + target + '</div>').repeat(count) + '</div>';
                    rule = '.matrix-target:is(.active .matrix-target) {width:41px}';
                  } else if (shape === 'siblings') {
                    markup = '<div><i id="trigger"></i>' + target.repeat(count) + '</div>';
                    rule = '.matrix-target:where(.active ~ .matrix-target) {width:41px}';
                  } else if (shape === 'disabled') {
                    markup = '<fieldset id="trigger">' + '<input class="matrix-target">'.repeat(count) + '</fieldset>';
                    rule = '.matrix-target:disabled {width:41px}';
                  } else if (shape === 'identity' || shape === 'dataset') {
                    markup = '<div><i id="trigger"></i>' + target.repeat(count) + '</div>';
                    rule = shape === 'identity' ? '#on ~ .matrix-target {width:41px}'
                      : '[data-mode="on"] ~ .matrix-target {width:41px}';
                  } else if (shape === 'variables') {
                    markup = '<div><i id="trigger"></i><div class="provider">' +
                      ('<div class="alias">' + target + '</div>').repeat(count) + '</div></div>';
                    rule = '.active + .provider {--base:41px} .alias {--alias:var(--base, 11px)} .matrix-target {width:var(--alias)}';
                  } else if (shape === 'tree-position') {
                    markup = ('<div class="tree-list">' + target + '</div>').repeat(count);
                    rule = '.matrix-target:nth-child(2) {width:41px}';
                  } else if (shape === 'tree-siblings' || shape === 'tree-wide-position') {
                    markup = '<div class="tree-list">' + target.repeat(count) + '</div>';
                    rule = shape === 'tree-wide-position'
                      ? '.marker ~ .matrix-target:nth-child(n+2) {width:41px}'
                      : '.marker ~ .matrix-target {width:41px}';
                  } else if (shape === 'tree-relational') {
                    markup = '<div class="tree-list"></div>' + target.repeat(count);
                    rule = '.tree-list:has(.marker) ~ .matrix-target {width:41px}';
                  } else if (shape === 'tree-empty') {
                    markup = ('<div class="tree-list"><i class="marker"></i></div>' + target).repeat(count);
                    rule = '.tree-list:empty + .matrix-target {width:41px}';
                  } else {
                    markup = ('<div class="matrix-target"><div class="branch"><i class="ready"></i></div></div>').repeat(count);
                    rule = '.matrix-target:has(.branch > [data-ready]) {width:41px}';
                  }
                  document.body.innerHTML = '<main>' + markup + '</main><aside>' +
                    Array.from({length:unrelated}, (_, i) => '<div class="noise-' + i + '"></div>').join('') + '</aside>';
                  style.textContent = '.matrix-target {display:block;width:11px}' + rule +
                    Array.from({length:unrelated}, (_, i) => '.noise-' + i + ' {height:1px}').join('');
                  document.head.appendChild(style);
                  const targets = Array.from(document.querySelectorAll('.matrix-target'));
                  const ready = Array.from(document.querySelectorAll('.ready'));
                  const trigger = document.getElementById('trigger');
                  const lists = Array.from(document.querySelectorAll('.tree-list'));
                  globalThis.matrixMutation = enabled => {
                    if (shape.startsWith('tree-')) lists.forEach(list => {
                      if (enabled !== (shape === 'tree-empty')) {
                        const marker = document.createElement('i'); marker.className = 'marker';
                        list.insertBefore(marker, list.firstChild);
                      } else list.removeChild(list.firstChild);
                    });
                    else if (shape === 'disabled') trigger.toggleAttribute('disabled', enabled);
                    else if (shape === 'identity') trigger.id = enabled ? 'on' : 'off';
                    else if (shape === 'dataset') {
                      if (enabled) trigger.dataset.mode = 'on';
                      else delete trigger.dataset.mode;
                    }
                    else if (shape === 'relational') ready.forEach(node => node.toggleAttribute('data-ready', enabled));
                    else trigger.className = enabled ? 'active' : '';
                    return targets.map(node => getComputedStyle(node).width).join(',');
                  };
                  return targets.map(node => getComputedStyle(node).width).join(',');
                })(SHAPE,COUNT,UNRELATED))JS"};
                source.replace(source.find("SHAPE"), 5, "'" + std::string(shape) + "'");
                source.replace(source.find("COUNT"), 5, std::to_string(affected_count));
                source.replace(source.find("UNRELATED"), 9, std::to_string(unrelated_count));
                const auto expected = [&](const char* value) {
                    std::string result = "\"";
                    for (int i = 0; i < affected_count; ++i) {
                        if (i) result += ',';
                        result += value;
                    }
                    return result + '"';
                };
                const auto setup = evaluate(engine, source, "css-route-setup.js");
                require(setup == expected("11px"),
                    std::string("CSS route initial style mismatch: ") + shape + ": " + setup);
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
                const std::array<std::string_view, 8> names{
                    "selector-invalidation-plan-lookups", "selector-invalidation-candidate-visits",
                    "selector-invalidation-fallback-visits", "css-compound-match-checks",
                    "css-rule-match-checks", "css-cascade-applications", "css-cascade-candidate-checks",
                    "css-positional-sibling-visits"};
                std::this_thread::sleep_for(std::chrono::milliseconds(260));
                execute_and_wait_for_wrapper_retention_scene(engine,
                    "document.body.style.backgroundColor = 'rgb(1, 2, 3)'", "css-route-baseline.js");
                std::array<uint64_t, 8> before{};
                for (size_t i = 0; i < names.size(); ++i)
                    before[i] = runtime_counter_diagnostic(engine, names[i]);
#endif
                const auto started = std::chrono::steady_clock::now();
                require(evaluate(engine, "matrixMutation(true)", "css-route-add.js") == expected("41px"),
                    std::string("CSS route addition failed: ") + shape);
                require(evaluate(engine, "matrixMutation(false)", "css-route-remove.js") == expected("11px"),
                    std::string("CSS route removal failed: ") + shape);
                std::cout << "css-route-scaling shape=" << shape << " affected=" << affected_count
                    << " unrelated=" << unrelated_count << " mutation-and-check-ms="
                    << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
                std::this_thread::sleep_for(std::chrono::milliseconds(260));
                execute_and_wait_for_wrapper_retention_scene(engine,
                    "document.body.style.backgroundColor = 'rgb(3, 2, 1)'", "css-route-result.js");
                std::array<uint64_t, 8> delta{};
                for (size_t i = 0; i < names.size(); ++i) {
                    delta[i] = runtime_counter_diagnostic(engine, names[i]) - before[i];
                    std::cout << ' ' << names[i] << '=' << delta[i];
                }
                require(delta[2] == 0, "compiled routes unexpectedly used document fallback");
                require(delta[5] >= static_cast<uint64_t>(2 * affected_count),
                    "route scaling failed to measure the affected cascades");
                // Generous linear ceilings catch accidental quadratic cascades
                // without binding the test to an incidental exact count.
                for (const auto value : delta)
                    require(value <= static_cast<uint64_t>(200 * affected_count + 256),
                        "compiled route work exceeded its linear component-size budget");
                if (std::string_view(shape) == "tree-wide-position")
                    require(delta[7] > 0 && delta[7] <= static_cast<uint64_t>(4 * (affected_count + 1)),
                        "positional selectors repeatedly scanned the same sibling list");
                observations.push_back(delta);
#endif
                std::cout << '\n';
                webscene_engine_destroy(engine);
            }
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
            require(observations[0] == observations[1],
                std::string("unrelated DOM/rules amplified compiled route work: ") + shape);
#endif
        }
    }
}
