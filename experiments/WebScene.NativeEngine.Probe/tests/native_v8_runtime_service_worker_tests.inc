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
