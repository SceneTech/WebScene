void test_service_worker_cross_origin_iframe_visibility_gate()
{
    resource_server server{.content = {
        {"https://workspace.test/index.html",
            "<!doctype html><title>isolated worker owner</title><body></body>"},
        {"https://preview.test/pre/index.html", R"HTML(
            <!doctype html><body><script>
            console.log('isolated-sw-visible:' + Boolean(navigator.serviceWorker));
            if (navigator.serviceWorker) {
              navigator.serviceWorker.register('service-worker.js', {
                type: 'module', updateViaCache: 'none'
              }).then(registration => console.log('isolated-sw-registered:' +
                Boolean(registration.active))).catch(error =>
                  console.error('isolated-sw-error:' + error.message));
            }
            </script></body>)HTML"},
        {"https://preview.test/pre/service-worker.js", R"JS(
            self.addEventListener('install', event => event.waitUntil(skipWaiting()));
            self.addEventListener('activate', event => event.waitUntil(clients.claim()));
        )JS"},
        {"https://preview.test/pre/opaque.html",
            "<!doctype html><body><script>console.log('opaque-sw-visible:' +"
            " Boolean(navigator.serviceWorker));"
            "const nested=document.createElement('iframe');"
            "nested.setAttribute('sandbox','allow-same-origin allow-scripts');"
            "nested.src='https://preview.test/pre/nested.html';"
            "document.body.appendChild(nested);</script></body>"},
        {"https://preview.test/pre/nested.html",
            "<!doctype html><body><script>console.log('nested-opaque-sw-visible:' +"
            " Boolean(navigator.serviceWorker));</script></body>"}
    }};
    webscene_engine_options options{};
    options.struct_size = sizeof(options);
    options.resource_load_callback = load_test_resource;
    options.resource_load_user_data = &server;
    auto* engine = webscene_engine_create_with_options(&options);
    require(engine != nullptr, "isolated iframe worker engine creation failed");
    webscene_engine_configure_diagnostics(
        engine, WEBSCENE_DIAGNOSTIC_CONSOLE | WEBSCENE_DIAGNOSTIC_EXCEPTIONS,
        nullptr, nullptr);
    constexpr std::string_view address{"https://workspace.test/index.html"};
    require(webscene_engine_load_url(engine, address.data(), address.size()) != 0,
        "isolated iframe worker document load failed");
    require(evaluate_until_equals(engine, "location.href", "isolated-sw-parent.js",
        R"("https://workspace.test/index.html")", 2'500)
            == R"("https://workspace.test/index.html")",
        "isolated iframe worker parent did not become ready");
    const auto frame_started = std::chrono::steady_clock::now();
    execute(engine, R"JS((() => {
      const admitted = document.createElement('iframe');
      admitted.setAttribute('sandbox', 'allow-same-origin allow-scripts');
      admitted.src = 'https://preview.test/pre/index.html';
      document.body.appendChild(admitted);
      const opaque = document.createElement('iframe');
      opaque.setAttribute('sandbox', 'allow-scripts');
      opaque.src = 'https://preview.test/pre/opaque.html';
      document.body.appendChild(opaque);
      globalThis.__isolatedFrames = [admitted, opaque];
    })())JS", "isolated-sw-create.js");
    require(evaluate_until_equals(engine,
        "__isolatedFrames.every(frame => frame.contentDocument === null)",
        "isolated-sw-parent-denied.js", "true", 2'500) == "true",
        "isolated worker clients leaked cross-origin parent DOM access");

    bool visible = false, registered = false;
    bool opaque_hidden = false, nested_opaque_hidden = false;
    std::string records;
    for (int attempt = 0; attempt < 1'500
            && !(visible && registered && opaque_hidden && nested_opaque_hidden);
         ++attempt) {
        const auto length = webscene_engine_take_diagnostic(engine, nullptr, 0);
        if (length == 0U) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }
        std::vector<char> payload(length);
        require(webscene_engine_take_diagnostic(engine, payload.data(), length)
                == length, "isolated worker diagnostic changed during copy");
        const auto record = std::string(payload.data());
        records += record + '\n';
        const bool admitted_document = record.find(
            "https://preview.test/pre/index.html") != std::string::npos;
        visible |= admitted_document && record.find("isolated-sw-visible:true")
            != std::string::npos;
        registered |= admitted_document && record.find("isolated-sw-registered:true")
            != std::string::npos;
        opaque_hidden |= record.find("https://preview.test/pre/opaque.html")
            != std::string::npos
            && record.find("opaque-sw-visible:false") != std::string::npos;
        nested_opaque_hidden |= record.find("https://preview.test/pre/nested.html")
            != std::string::npos
            && record.find("nested-opaque-sw-visible:false")
                != std::string::npos;
    }
    require(visible && registered && opaque_hidden && nested_opaque_hidden,
        "ServiceWorker visibility/registration escaped iframe origin or sandbox: "
        + records);
    const auto elapsed_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - frame_started).count();
    require(elapsed_ms <= 1'000.0,
        "isolated iframe registration exceeded the 1 s focused budget");
    std::cout << "Isolated ServiceWorker iframe: registrationAndSandboxMs="
              << elapsed_ms << " registered=1 opaqueHidden=1 nestedHidden=1\n";
    webscene_engine_destroy(engine);
}

void test_service_worker_lifecycle_performance_and_teardown_gate()
{
    resource_server server{.content = {
        {"https://service-worker-cycle.test/index.html",
            "<!doctype html><title>service worker cycle gate</title>"},
        {"https://service-worker-cycle.test/service-worker.js", R"JS(
            self.addEventListener('install', event => event.waitUntil(skipWaiting()));
            self.addEventListener('activate', event => event.waitUntil(clients.claim()));
        )JS"}
    }};
    webscene_engine_options options{};
    options.struct_size = sizeof(options);
    options.resource_load_callback = load_test_resource;
    options.resource_load_user_data = &server;
    auto* engine = webscene_engine_create_with_options(&options);
    require(engine != nullptr, "service worker cycle engine creation failed");
    constexpr std::string_view address{
        "https://service-worker-cycle.test/index.html"};
    require(
        webscene_engine_load_url(engine, address.data(), address.size()) != 0U,
        "service worker cycle navigation failed");
    const auto navigation = evaluate_until_equals(
        engine,
        "location.href",
        "service-worker-cycle-navigation-ready.js",
        R"("https://service-worker-cycle.test/index.html")",
        2'500);
    require(
        navigation == R"("https://service-worker-cycle.test/index.html")",
        "service worker cycle document did not become ready: " + navigation);

    execute(engine, R"JS(
        globalThis.__serviceWorkerCycles = {
          complete: false, error: null, durations: [], maximumRegistrations: 0
        };
        (async () => {
          try {
            for (let cycle = 0; cycle < 100; ++cycle) {
              const start = performance.now();
              const registration = await navigator.serviceWorker.register(
                'service-worker.js', {type:'module', updateViaCache:'none'});
              if (navigator.serviceWorker.controller !== registration.active) {
                throw new Error(`cycle ${cycle} did not claim its client`);
              }
              if (await registration.update() !== registration) {
                throw new Error(`cycle ${cycle} replaced registration identity`);
              }
              const count = (await navigator.serviceWorker.getRegistrations()).length;
              __serviceWorkerCycles.maximumRegistrations = Math.max(
                __serviceWorkerCycles.maximumRegistrations, count);
              if (!await registration.unregister()) {
                throw new Error(`cycle ${cycle} did not unregister`);
              }
              if ((await navigator.serviceWorker.getRegistrations()).length !== 0) {
                throw new Error(`cycle ${cycle} retained registration ownership`);
              }
              __serviceWorkerCycles.durations.push(performance.now() - start);
            }
          } catch (error) {
            __serviceWorkerCycles.error = String(error && error.stack || error);
          } finally {
            __serviceWorkerCycles.complete = true;
          }
        })();
    )JS", "service-worker-cycles.js");
    require(
        evaluate_until_equals(
            engine,
            "__serviceWorkerCycles.complete",
            "service-worker-cycles-complete.js",
            "true",
            10'000)
            == "true",
        "100 service worker lifecycle cycles did not finish");
    require(
        evaluate(engine,
            "__serviceWorkerCycles.error",
            "service-worker-cycles-error.js") == "null",
        "service worker lifecycle cycle failed: "
            + evaluate(engine,
                "__serviceWorkerCycles.error",
                "service-worker-cycles-error-detail.js"));
    require(
        evaluate(engine,
            "__serviceWorkerCycles.maximumRegistrations",
            "service-worker-cycle-registration-high-water.js") == "1",
        "service worker registration high-water exceeded one");
    require(
        evaluate(engine,
            "navigator.serviceWorker.getRegistrations().then(values => "
            "globalThis.__serviceWorkerFinalRegistrationCount = values.length); true",
            "service-worker-final-registration-count-start.js") == "true",
        "service worker final registration count probe failed to start");
    require(
        evaluate_until_equals(
            engine,
            "globalThis.__serviceWorkerFinalRegistrationCount",
            "service-worker-final-registration-count.js",
            "0") == "0",
        "service worker registrations remained after 100 lifecycle cycles");
    const auto p95 = std::stod(evaluate(engine, R"JS((() => {
        const sorted = [...__serviceWorkerCycles.durations].sort((a, b) => a - b);
        return sorted[Math.ceil(sorted.length * 0.95) - 1];
      })())JS", "service-worker-cycle-p95.js"));
    require(p95 <= 1000.0,
        "service worker register/control/update/unregister p95 exceeded one second: "
            + std::to_string(p95));

    webscene_engine_memory_metrics memory_before{
        sizeof(webscene_engine_memory_metrics)};
    require(webscene_engine_get_memory_metrics(engine, &memory_before) != 0,
        "service worker cycle memory baseline was unavailable");
    require(webscene_engine_request_low_memory(engine) != 0,
        "service worker cycle low-memory request was rejected");
    webscene_engine_memory_metrics memory_after{
        sizeof(webscene_engine_memory_metrics)};
    for (auto attempt = 0; attempt < 250; ++attempt) {
        require(webscene_engine_get_memory_metrics(engine, &memory_after) != 0,
            "service worker cycle final memory metrics were unavailable");
        if (memory_after.low_memory_notifications
            > memory_before.low_memory_notifications) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(
        memory_after.low_memory_notifications
            > memory_before.low_memory_notifications,
        "service worker cycle low-memory collection did not complete");
    require(
        memory_after.v8_used_heap_bytes
            <= memory_before.v8_used_heap_bytes + 8U * 1024U * 1024U,
        "service worker lifecycle cycles retained more than 8 MiB");
    std::cout << "Service worker lifecycle gate: cycles=100 p95=" << p95
              << "ms registrationsHighWater=1 heapBefore="
              << memory_before.v8_used_heap_bytes << " heapAfter="
              << memory_after.v8_used_heap_bytes << '\n';
    webscene_engine_destroy(engine);
}

void test_service_worker_client_navigation_generation_gate()
{
    resource_server server{.content = {
        {"https://service-worker-navigation.test/first.html",
            "<!doctype html><title>first controlled generation</title>"},
        {"https://service-worker-navigation.test/second.html",
            "<!doctype html><title>replacement controlled generation</title>"},
        {"https://service-worker-navigation.test/service-worker.js", R"JS(
            let retiredSource;
            let retiredId;
            self.addEventListener('install', event => event.waitUntil(skipWaiting()));
            self.addEventListener('activate', event => event.waitUntil(clients.claim()));
            self.addEventListener('message', event => {
              if (event.data === 'arm-retired-source') {
                retiredSource = event.source;
                retiredId = event.source.id;
                event.source.postMessage({kind:'armed', id:retiredId});
                return;
              }
              if (event.data === 'flush-retired-source') {
                Promise.resolve(clients.get(retiredId)).then(retiredLookup => {
                  let staleRejected = false;
                  try {
                    retiredSource.postMessage({kind:'stale'});
                  } catch (error) {
                    staleRejected = error?.name === 'InvalidStateError';
                  }
                  event.source.postMessage({
                    kind:'fresh', id:event.source.id,
                    retiredAbsent:retiredLookup === undefined,
                    staleRejected
                  });
                });
              }
            });
        )JS"}
    }};
    webscene_engine_options options{};
    options.struct_size = sizeof(options);
    options.resource_load_callback = load_test_resource;
    options.resource_load_user_data = &server;
    auto* engine = webscene_engine_create_with_options(&options);
    require(engine != nullptr, "service worker navigation engine creation failed");

    constexpr std::string_view first_address{
        "https://service-worker-navigation.test/first.html"};
    require(
        webscene_engine_load_url(
            engine, first_address.data(), first_address.size()) != 0U,
        "first service worker client navigation failed");
    require(
        evaluate_until_equals(
            engine,
            "location.href",
            "service-worker-client-first-navigation.js",
            R"("https://service-worker-navigation.test/first.html")",
            2'500)
            == R"("https://service-worker-navigation.test/first.html")",
        "first service worker client document did not become ready");
    execute(engine, R"JS(
        globalThis.__serviceWorkerFirst = {complete:false, error:null, id:null};
        (async () => {
          try {
            const changed = new Promise(resolve => navigator.serviceWorker.addEventListener(
              'controllerchange', resolve, {once:true}));
            const registration = await navigator.serviceWorker.register(
              'service-worker.js', {type:'module'});
            if (!navigator.serviceWorker.controller) await changed;
            const reply = new Promise(resolve => navigator.serviceWorker.addEventListener(
              'message', resolve, {once:true}));
            navigator.serviceWorker.controller.postMessage('arm-retired-source');
            const event = await reply;
            if (event.data.kind !== 'armed') throw new Error('retired source was not armed');
            __serviceWorkerFirst.id = event.data.id;
          } catch (error) {
            __serviceWorkerFirst.error = String(error && error.stack || error);
          } finally {
            __serviceWorkerFirst.complete = true;
          }
        })();
    )JS", "service-worker-client-first.js");
    require(
        evaluate_until_equals(
            engine,
            "__serviceWorkerFirst.complete",
            "service-worker-client-first-complete.js",
            "true",
            2'500) == "true",
        "first service worker generation did not arm its source");
    require(
        evaluate(engine,
            "__serviceWorkerFirst.error",
            "service-worker-client-first-error.js") == "null",
        "first service worker generation failed: "
            + evaluate(engine,
                "__serviceWorkerFirst.error",
                "service-worker-client-first-error-detail.js"));
    const auto first_id = evaluate(engine,
        "__serviceWorkerFirst.id",
        "service-worker-client-first-id.js");

    constexpr std::string_view second_address{
        "https://service-worker-navigation.test/second.html"};
    require(
        webscene_engine_load_url(
            engine, second_address.data(), second_address.size()) != 0U,
        "replacement service worker client navigation failed");
    require(
        evaluate_until_equals(
            engine,
            "location.href",
            "service-worker-client-second-navigation.js",
            R"("https://service-worker-navigation.test/second.html")",
            2'500)
            == R"("https://service-worker-navigation.test/second.html")",
        "replacement service worker client document did not become ready");
    execute(engine, R"JS(
        globalThis.__serviceWorkerSecond = {
          complete:false, error:null, id:null, staleCount:0,
          retiredAbsent:false, staleRejected:false
        };
        (async () => {
          try {
            if (!navigator.serviceWorker.controller) {
              throw new Error('replacement navigation was not controlled');
            }
            const reply = new Promise(resolve => {
              navigator.serviceWorker.addEventListener('message', event => {
                if (event.data.kind === 'stale') {
                  ++__serviceWorkerSecond.staleCount;
                  return;
                }
                if (event.data.kind === 'fresh') resolve(event);
              });
            });
            navigator.serviceWorker.controller.postMessage('flush-retired-source');
            const event = await reply;
            __serviceWorkerSecond.id = event.data.id;
            __serviceWorkerSecond.retiredAbsent = event.data.retiredAbsent;
            __serviceWorkerSecond.staleRejected = event.data.staleRejected;
          } catch (error) {
            __serviceWorkerSecond.error = String(error && error.stack || error);
          } finally {
            __serviceWorkerSecond.complete = true;
          }
        })();
    )JS", "service-worker-client-second.js");
    require(
        evaluate_until_equals(
            engine,
            "__serviceWorkerSecond.complete",
            "service-worker-client-second-complete.js",
            "true",
            2'500) == "true",
        "replacement service worker generation did not finish");
    require(
        evaluate(engine,
            "__serviceWorkerSecond.error",
            "service-worker-client-second-error.js") == "null",
        "replacement service worker generation failed: "
            + evaluate(engine,
                "__serviceWorkerSecond.error",
                "service-worker-client-second-error-detail.js"));
    const auto second_id = evaluate(engine,
        "__serviceWorkerSecond.id",
        "service-worker-client-second-id.js");
    require(first_id != second_id,
        "replacement navigation reused the retired client identity");
    const auto retirement = evaluate(engine,
        "JSON.stringify({staleCount:__serviceWorkerSecond.staleCount,"
        "retiredAbsent:__serviceWorkerSecond.retiredAbsent,"
        "staleRejected:__serviceWorkerSecond.staleRejected})",
        "service-worker-client-retirement-result.js");
    require(
        evaluate(engine,
            "__serviceWorkerSecond.staleCount === 0"
            " && __serviceWorkerSecond.retiredAbsent"
            " && __serviceWorkerSecond.staleRejected",
            "service-worker-client-retirement-valid.js") == "true",
        "retired service worker client remained observable or deliverable: "
            + retirement);
    webscene_engine_destroy(engine);
}

uint64_t service_worker_test_peak_rss_bytes()
{
#if defined(__APPLE__) || defined(__linux__)
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0) return 0U;
#if defined(__APPLE__)
    return static_cast<uint64_t>(usage.ru_maxrss);
#else
    return static_cast<uint64_t>(usage.ru_maxrss) * 1024U;
#endif
#else
    return 0U;
#endif
}

uint64_t service_worker_test_current_rss_bytes()
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

void test_service_worker_client_queue_performance_and_memory_gate()
{
    resource_server server{.content = {
        {"https://service-worker-message-gate.test/index.html",
            "<!doctype html><title>service worker message gate</title>"},
        {"https://service-worker-message-gate.test/service-worker.js", R"JS(
            self.addEventListener('install', event => event.waitUntil(skipWaiting()));
            self.addEventListener('activate', event => event.waitUntil(clients.claim()));
            self.addEventListener('message', event => {
              if (event.data.kind === 'round-trip') {
                const bytes = event.data.bytes;
                event.source.postMessage({kind:'round-trip', sequence:event.data.sequence,
                  bytes}, [bytes.buffer]);
                event.ports[0].postMessage(event.data.sequence);
                event.ports[0].close();
                return;
              }
              if (event.data.kind === 'block') {
                event.source.postMessage({kind:'block-armed'});
                const until = performance.now() + 250;
                while (performance.now() < until) {}
                return;
              }
              if (event.data.kind === 'barrier') {
                event.source.postMessage({kind:'barrier'});
              }
            });
        )JS"}
    }};
    webscene_engine_options options{};
    options.struct_size = sizeof(options);
    options.resource_load_callback = load_test_resource;
    options.resource_load_user_data = &server;
    const auto rss_before = service_worker_test_peak_rss_bytes();
    auto* engine = webscene_engine_create_with_options(&options);
    require(engine != nullptr, "service worker message gate engine creation failed");
    constexpr std::string_view address{
        "https://service-worker-message-gate.test/index.html"};
    require(
        webscene_engine_load_url(engine, address.data(), address.size()) != 0U,
        "service worker message gate navigation failed");
    require(
        evaluate_until_equals(
            engine,
            "location.href",
            "service-worker-message-gate-navigation.js",
            R"("https://service-worker-message-gate.test/index.html")",
            2'500)
            == R"("https://service-worker-message-gate.test/index.html")",
        "service worker message gate document did not become ready");

    webscene_engine_memory_metrics memory_before{
        sizeof(webscene_engine_memory_metrics)};
    require(webscene_engine_get_memory_metrics(engine, &memory_before) != 0,
        "service worker message gate memory baseline was unavailable");
    execute(engine, R"JS(
        globalThis.__serviceWorkerMessages = {
          complete:false, error:null, durations:[], accepted:0, quotaRejected:0
        };
        (async () => {
          try {
            const changed = new Promise(resolve => navigator.serviceWorker.addEventListener(
              'controllerchange', resolve, {once:true}));
            await navigator.serviceWorker.register(
              'service-worker.js', {type:'module'});
            if (!navigator.serviceWorker.controller) await changed;
            const worker = navigator.serviceWorker.controller;
            for (let sequence = 0; sequence < 100; ++sequence) {
              const channel = new MessageChannel();
              const bytes = new Uint8Array([sequence & 255, 13, 21]);
              const messageReply = new Promise(resolve => {
                const listener = event => {
                  if (event.data.kind !== 'round-trip'
                    || event.data.sequence !== sequence) return;
                  navigator.serviceWorker.removeEventListener('message', listener);
                  resolve(event.data);
                };
                navigator.serviceWorker.addEventListener('message', listener);
              });
              const portReply = new Promise(resolve => {
                channel.port1.onmessage = resolve;
                channel.port1.start();
              });
              const start = performance.now();
              worker.postMessage({kind:'round-trip', sequence, bytes},
                [bytes.buffer, channel.port2]);
              if (bytes.byteLength !== 0) throw new Error('sender kept ArrayBuffer');
              const [message, portMessage] = await Promise.all(
                [messageReply, portReply]);
              channel.port1.close();
              if (message.bytes.join(',') !== `${sequence & 255},13,21`
                || portMessage.data !== sequence) {
                throw new Error(`round trip ${sequence} changed transferred data`);
              }
              __serviceWorkerMessages.durations.push(performance.now() - start);
            }

            const blockArmed = new Promise(resolve => {
              const listener = event => {
                if (event.data.kind !== 'block-armed') return;
                navigator.serviceWorker.removeEventListener('message', listener);
                resolve();
              };
              navigator.serviceWorker.addEventListener('message', listener);
            });
            worker.postMessage({kind:'block'});
            await blockArmed;
            for (let sequence = 0; sequence < 300; ++sequence) {
              try {
                worker.postMessage({kind:'load', sequence});
                ++__serviceWorkerMessages.accepted;
              } catch (error) {
                if (error?.name !== 'QuotaExceededError') throw error;
                ++__serviceWorkerMessages.quotaRejected;
              }
            }
            let barrierQueued = false;
            while (!barrierQueued) {
              try {
                worker.postMessage({kind:'barrier'});
                barrierQueued = true;
              } catch (error) {
                if (error?.name !== 'QuotaExceededError') throw error;
                await new Promise(resolve => setTimeout(resolve, 1));
              }
            }
            await new Promise(resolve => {
              const listener = event => {
                if (event.data.kind !== 'barrier') return;
                navigator.serviceWorker.removeEventListener('message', listener);
                resolve();
              };
              navigator.serviceWorker.addEventListener('message', listener);
            });
          } catch (error) {
            __serviceWorkerMessages.error = String(error && error.stack || error);
          } finally {
            __serviceWorkerMessages.complete = true;
          }
        })();
    )JS", "service-worker-message-gate.js");
    require(
        evaluate_until_equals(
            engine,
            "__serviceWorkerMessages.complete",
            "service-worker-message-gate-complete.js",
            "true",
            10'000) == "true",
        "100 service worker client message cycles did not finish");
    require(
        evaluate(engine,
            "__serviceWorkerMessages.error",
            "service-worker-message-gate-error.js") == "null",
        "service worker message gate failed: "
            + evaluate(engine,
                "__serviceWorkerMessages.error",
                "service-worker-message-gate-error-detail.js"));
    const auto p95 = std::stod(evaluate(engine, R"JS((() => {
        const sorted = [...__serviceWorkerMessages.durations].sort((a, b) => a - b);
        return sorted[Math.ceil(sorted.length * 0.95) - 1];
      })())JS", "service-worker-message-p95.js"));
    require(p95 <= 250.0,
        "service worker message round-trip p95 exceeded 250 ms: "
            + std::to_string(p95));
    const auto accepted = std::stoull(evaluate(engine,
        "__serviceWorkerMessages.accepted",
        "service-worker-message-accepted.js"));
    const auto rejected = std::stoull(evaluate(engine,
        "__serviceWorkerMessages.quotaRejected",
        "service-worker-message-rejected.js"));
    require(accepted <= 256U && rejected > 0U && accepted + rejected == 300U,
        "service worker message queue did not enforce its 256-message bound");

    require(webscene_engine_request_low_memory(engine) != 0,
        "service worker message gate low-memory request was rejected");
    webscene_engine_memory_metrics memory_after{
        sizeof(webscene_engine_memory_metrics)};
    for (auto attempt = 0; attempt < 250; ++attempt) {
        require(webscene_engine_get_memory_metrics(engine, &memory_after) != 0,
            "service worker message gate final memory metrics were unavailable");
        if (memory_after.low_memory_notifications
            > memory_before.low_memory_notifications) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(
        memory_after.low_memory_notifications
            > memory_before.low_memory_notifications,
        "service worker message gate low-memory collection did not complete");
    require(
        memory_after.v8_used_heap_bytes
            <= memory_before.v8_used_heap_bytes + 8U * 1024U * 1024U,
        "service worker client messages retained more than 8 MiB of V8 heap");
    const auto rss_after = service_worker_test_peak_rss_bytes();
    constexpr uint64_t maximum_rss_growth = 64U * 1024U * 1024U;
    require(
        rss_before == 0U || rss_after <= rss_before + maximum_rss_growth,
        "service worker client message gate grew peak RSS by more than 64 MiB");
    std::cout << "Service worker client message gate: cycles=100 p95=" << p95
              << "ms acceptedHighWater=" << accepted
              << " queueCapacity=256 heapBefore="
              << memory_before.v8_used_heap_bytes << " heapAfter="
              << memory_after.v8_used_heap_bytes << " rssBefore=" << rss_before
              << " rssAfter=" << rss_after << '\n';
    webscene_engine_destroy(engine);
}

void test_service_worker_message_port_active_listener_gc()
{
    resource_server server{.content = {
        {"https://service-worker-port-gc.test/index.html",
            "<!doctype html><title>service worker port gc</title>"},
        {"https://service-worker-port-gc.test/service-worker.js", R"JS(
            self.addEventListener('install', event => event.waitUntil(skipWaiting()));
            self.addEventListener('activate', event => event.waitUntil(clients.claim()));
            self.addEventListener('message', event => {
              const message = event.data;
              if (message.kind === 'attach') {
                const port = event.ports[0];
                const receive = received => {
                  port.postMessage({sequence:received.data});
                  if (message.mode === 0) port.onmessage = null;
                  else port.removeEventListener('message', receive);
                  port.close();
                };
                if (message.mode === 0) port.onmessage = receive;
                else {
                  port.addEventListener('message', receive);
                  port.start();
                }
                event.source.postMessage({kind:'armed', sequence:message.sequence});
                return;
              }
              if (message.kind === 'gc-barrier') {
                event.source.postMessage({kind:'gc-barrier', sequence:message.sequence});
              }
            });
        )JS"}
    }};
    webscene_engine_options options{};
    options.struct_size = sizeof(options);
    options.resource_load_callback = load_test_resource;
    options.resource_load_user_data = &server;
    auto* engine = webscene_engine_create_with_options(&options);
    require(engine != nullptr, "service worker MessagePort GC engine creation failed");
    constexpr std::string_view address{
        "https://service-worker-port-gc.test/index.html"};
    require(webscene_engine_load_url(engine, address.data(), address.size()) != 0U,
        "service worker MessagePort GC navigation failed");
    require(evaluate_until_equals(
        engine, "location.href", "service-worker-port-gc-navigation.js",
        R"("https://service-worker-port-gc.test/index.html")", 2'500)
            == R"("https://service-worker-port-gc.test/index.html")",
        "service worker MessagePort GC document did not become ready");

    execute(engine, R"JS(
        globalThis.__serviceWorkerPortGc = {
          ready:false, armed:0, barriers:0, delivered:0, error:null
        };
        (async () => {
          try {
            const changed = new Promise(resolve => navigator.serviceWorker.addEventListener(
              'controllerchange', resolve, {once:true}));
            await navigator.serviceWorker.register(
              'service-worker.js', {type:'module'});
            if (!navigator.serviceWorker.controller) await changed;
            navigator.serviceWorker.addEventListener('message', event => {
              if (event.data.kind === 'armed') {
                __serviceWorkerPortGc.armed = event.data.sequence + 1;
              } else if (event.data.kind === 'gc-barrier') {
                __serviceWorkerPortGc.barriers = event.data.sequence + 1;
              }
            });
            globalThis.__attachServiceWorkerGcPort = sequence => {
              const channel = new MessageChannel();
              globalThis.__serviceWorkerGcSender = channel.port1;
              let reply;
              if ((sequence & 1) === 0) {
                reply = new Promise(resolve => {
                  channel.port1.onmessage = resolve;
                  channel.port1.start();
                });
              } else {
                reply = new Promise(resolve => {
                  const listener = event => {
                    channel.port1.removeEventListener('message', listener);
                    resolve(event);
                  };
                  channel.port1.addEventListener('message', listener);
                  channel.port1.start();
                });
              }
              reply.then(event => {
                if (event.data.sequence !== sequence) {
                  throw new Error(`cycle ${sequence} delivered ${event.data.sequence}`);
                }
                channel.port1.onmessage = null;
                channel.port1.close();
                globalThis.__serviceWorkerGcSender = null;
                __serviceWorkerPortGc.delivered = sequence + 1;
              }).catch(error => {
                __serviceWorkerPortGc.error = String(error && error.stack || error);
              });
              navigator.serviceWorker.controller.postMessage(
                {kind:'attach', sequence, mode:sequence & 1}, [channel.port2]);
            };
            __serviceWorkerPortGc.ready = true;
          } catch (error) {
            __serviceWorkerPortGc.error = String(error && error.stack || error);
          }
        })();
    )JS", "service-worker-port-gc-bootstrap.js");
    require(evaluate_until_equals(
        engine, "__serviceWorkerPortGc.ready",
        "service-worker-port-gc-ready.js", "true", 2'500) == "true",
        "service worker MessagePort GC controller did not become ready");

    webscene_engine_memory_metrics memory_before{
        sizeof(webscene_engine_memory_metrics)};
    require(webscene_engine_get_memory_metrics(engine, &memory_before) != 0,
        "service worker MessagePort GC memory baseline was unavailable");
    const auto rss_before = service_worker_test_peak_rss_bytes();
    constexpr unsigned cycles = 100U;
    for (unsigned cycle = 0; cycle < cycles; ++cycle) {
        execute(engine,
            "__attachServiceWorkerGcPort(" + std::to_string(cycle) + ")",
            "service-worker-port-gc-attach.js");
        require(evaluate_until_equals(
            engine,
            "__serviceWorkerPortGc.armed",
            "service-worker-port-gc-armed.js",
            std::to_string(cycle + 1U),
            2'500) == std::to_string(cycle + 1U),
            "service worker did not arm its transferred MessagePort");
        require(webscene_engine_request_low_memory(engine) != 0,
            "service worker MessagePort GC request was rejected");
        execute(engine,
            "navigator.serviceWorker.controller.postMessage({kind:'gc-barrier',sequence:"
                + std::to_string(cycle) + "})",
            "service-worker-port-gc-barrier.js");
        require(evaluate_until_equals(
            engine,
            "__serviceWorkerPortGc.barriers",
            "service-worker-port-gc-barrier-ready.js",
            std::to_string(cycle + 1U),
            2'500) == std::to_string(cycle + 1U),
            "service worker did not cross the forced-GC barrier");
        execute(engine,
            "__serviceWorkerGcSender.postMessage(" + std::to_string(cycle) + ")",
            "service-worker-port-gc-send.js");
        require(evaluate_until_equals(
            engine,
            "__serviceWorkerPortGc.delivered",
            "service-worker-port-gc-delivered.js",
            std::to_string(cycle + 1U),
            2'500) == std::to_string(cycle + 1U),
            "service worker lost an active MessagePort listener after forced GC");
        require(evaluate(engine,
            "__serviceWorkerPortGc.error",
            "service-worker-port-gc-error.js") == "null",
            "service worker MessagePort GC cycle failed: " + evaluate(engine,
                "__serviceWorkerPortGc.error",
                "service-worker-port-gc-error-detail.js"));
    }
    require(webscene_engine_request_low_memory(engine) != 0,
        "service worker MessagePort final GC request was rejected");
    webscene_engine_memory_metrics memory_after{
        sizeof(webscene_engine_memory_metrics)};
    for (auto attempt = 0; attempt < 250; ++attempt) {
        require(webscene_engine_get_memory_metrics(engine, &memory_after) != 0,
            "service worker MessagePort final memory metrics were unavailable");
        if (memory_after.low_memory_notifications
            > memory_before.low_memory_notifications) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(memory_after.v8_used_heap_bytes
            <= memory_before.v8_used_heap_bytes + 8U * 1024U * 1024U,
        "service worker MessagePort cycles retained more than 8 MiB of parent heap");
    const auto rss_after = service_worker_test_peak_rss_bytes();
    require(rss_before == 0U || rss_after <= rss_before + 64U * 1024U * 1024U,
        "service worker MessagePort cycles grew peak RSS by more than 64 MiB");
    webscene_engine_destroy(engine);
}
