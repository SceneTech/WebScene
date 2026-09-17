void test_readable_stream_body_and_fetch_event_vertical()
{
    resource_server server{.content = {
        {"https://stream-fetch.test/index.html",
            "<!doctype html><title>stream and fetch event gate</title>"},
        {"https://stream-fetch.test/frame.html",
            "<!doctype html><title>stream nested realm</title>"},
        {"https://stream-fetch.test/service-worker.js", R"JS(
            let report = {complete:false};
            const check = (condition, message) => {
              if (!condition) throw new Error(message);
            };
            self.addEventListener('install', event => event.waitUntil(skipWaiting()));
            self.addEventListener('fetch', event => {
              event.waitUntil(Promise.resolve());
              event.respondWith(new Response(new ReadableStream({
                start(controller) {
                  controller.enqueue(new Uint8Array([65, 66]));
                  controller.enqueue(new Uint8Array([67]));
                  controller.close();
                }
              }), {status:206, headers:{'content-type':'text/plain'}}));
              let duplicate = '';
              try { event.respondWith(new Response('late')); }
              catch (error) { duplicate = error.name; }
              report.duplicateRespondWith = duplicate;
              event.handled.then(() => report.handled = true);
            });
            self.addEventListener('activate', event => event.waitUntil((async () => {
              check(Object.prototype.toString.call(new ReadableStream()) ===
                '[object ReadableStream]', 'ReadableStream brand');
              check(Object.prototype.toString.call(new FetchEvent('fetch', {
                request:new Request('https://stream-fetch.test/data')})) ===
                '[object FetchEvent]', 'FetchEvent brand');
              check(FetchEvent.prototype instanceof ExtendableEvent,
                'FetchEvent prototype chain');
              check(Object.getOwnPropertyDescriptor(
                FetchEvent.prototype, 'respondWith').enumerable,
                'FetchEvent WebIDL descriptor');
              check(Object.getOwnPropertyDescriptor(
                ReadableStream.prototype, 'getReader').enumerable,
                'ReadableStream WebIDL descriptor');
              const response = await __webSceneDispatchServiceWorkerFetch({
                request:new Request('https://stream-fetch.test/data'),
                clientId:'synthetic-client', resultingClientId:'', replacesClientId:''
              });
              check(response instanceof Response, 'respondWith response brand');
              check(response.status === 206, 'respondWith status');
              check(await response.text() === 'ABC', 'streamed response body');
              check(report.duplicateRespondWith === 'InvalidStateError',
                'duplicate respondWith rejection');
              await Promise.resolve();
              check(report.handled === true, 'FetchEvent handled promise');
              let late = '';
              const outside = new FetchEvent('fetch', {
                request:new Request('https://stream-fetch.test/late')});
              try { outside.respondWith(new Response('late')); }
              catch (error) { late = error.name; }
              check(late === 'InvalidStateError', 'late respondWith rejection');
              report = {complete:true, status:response.status, text:'ABC',
                duplicateRespondWith:report.duplicateRespondWith, lateRespondWith:late};
              await clients.claim();
            })()));
            self.addEventListener('message', event => {
              if (event.data === 'report') event.source.postMessage(report);
            });
        )JS"}
    }};
    webscene_engine_options options{};
    options.struct_size = sizeof(options);
    options.resource_load_callback = load_test_resource;
    options.resource_load_user_data = &server;
    auto* engine = webscene_engine_create_with_options(&options);
    require(engine != nullptr, "stream/fetch event engine creation failed");
    constexpr std::string_view address{"https://stream-fetch.test/index.html"};
    require(webscene_engine_load_url(engine, address.data(), address.size()) != 0U,
        "stream/fetch event navigation failed");
    require(
        evaluate_until_equals(engine, "location.href", "stream-fetch-navigation.js",
            R"("https://stream-fetch.test/index.html")", 2'500)
            == R"("https://stream-fetch.test/index.html")",
        "stream/fetch event document did not become ready");

    webscene_engine_memory_metrics memory_before{sizeof(webscene_engine_memory_metrics)};
    require(webscene_engine_get_memory_metrics(engine, &memory_before) != 0,
        "stream/fetch memory baseline was unavailable");
    execute(engine, R"JS(
      globalThis.__streamFetch = {complete:false, error:null, durations:[], report:null};
      (async () => {
          try {
          const closeOrder = new ReadableStream({start(controller) {
            controller.enqueue('first'); controller.close();
          }});
          const closeReader = closeOrder.getReader();
          let closedEarly = false;
          closeReader.closed.then(() => closedEarly = true);
          await Promise.resolve();
          if (closedEarly) throw new Error('reader.closed resolved before queued data drained');
          const first = await closeReader.read();
          const end = await closeReader.read();
          await closeReader.closed;
          if (first.value !== 'first' || first.done || !end.done) {
            throw new Error('close ordering changed queued data');
          }
          let wrongReceiver = '';
          try { ReadableStream.prototype.getReader.call({}); }
          catch (error) { wrongReceiver = error.name; }
          if (wrongReceiver !== 'TypeError') throw new Error('stream receiver brand check');
          let controllerConstructor = '';
          try { new ReadableStreamDefaultController(); }
          catch (error) { controllerConstructor = error.name; }
          if (controllerConstructor !== 'TypeError') {
            throw new Error('controller constructor was not illegal');
          }
          const frame = document.createElement('iframe');
          frame.src = 'frame.html';
          document.body.append(frame);
          for (let attempt = 0; attempt < 100
              && typeof frame.contentWindow.Response !== 'function'; ++attempt) {
            await new Promise(resolve => setTimeout(resolve, 1));
          }
          if (typeof frame.contentWindow.ReadableStream !== 'function'
              || !(new frame.contentWindow.Response('frame').body
                instanceof frame.contentWindow.ReadableStream)) {
            throw new Error(`nested realm did not install stream/body globals: ${
              typeof frame.contentWindow.ReadableStream}/${
              typeof frame.contentWindow.Response}`);
          }
          frame.remove();
          for (let cycle = 0; cycle < 100; ++cycle) {
            const start = performance.now();
            let cancelReason = '';
            const canceled = new ReadableStream({
              cancel(reason) { cancelReason = reason; }
            });
            const cancelReader = canceled.getReader();
            await cancelReader.cancel('retired');
            if (cancelReason !== 'retired') throw new Error('reader cancellation lost');

            const response = new Response(new ReadableStream({
              start(controller) {
                controller.enqueue(new Uint8Array([cycle & 255, 13]));
                controller.enqueue(new Uint8Array([21]));
                controller.close();
              }
            }), {headers:{'content-type':'application/octet-stream'}});
            const clone = response.clone();
            const [left, right] = await Promise.all(
              [response.arrayBuffer(), clone.arrayBuffer()]);
            if (new Uint8Array(left).join(',') !== `${cycle & 255},13,21`
                || new Uint8Array(right).join(',') !== `${cycle & 255},13,21`
                || !response.bodyUsed || !clone.bodyUsed) {
              throw new Error(`tee/body ownership failed at cycle ${cycle}`);
            }
            __streamFetch.durations.push(performance.now() - start);
          }
          const changed = new Promise(resolve => navigator.serviceWorker.addEventListener(
            'controllerchange', resolve, {once:true}));
          await navigator.serviceWorker.register('service-worker.js', {type:'module'});
          if (!navigator.serviceWorker.controller) await changed;
          __streamFetch.report = await new Promise(resolve => {
            const listener = message => {
              navigator.serviceWorker.removeEventListener('message', listener);
              resolve(message.data);
            };
            navigator.serviceWorker.addEventListener('message', listener);
            navigator.serviceWorker.controller.postMessage('report');
          });
          if (!__streamFetch.report.complete || __streamFetch.report.text !== 'ABC') {
            throw new Error(`controlled response failed: ${JSON.stringify(__streamFetch.report)}`);
          }
        } catch (error) {
          __streamFetch.error = String(error && error.stack || error);
        } finally { __streamFetch.complete = true; }
      })();
    )JS", "stream-fetch-gate.js");
    require(
        evaluate_until_equals(engine, "__streamFetch.complete",
            "stream-fetch-complete.js", "true", 10'000) == "true",
        "100 stream/body cycles and controlled response did not finish");
    require(evaluate(engine, "__streamFetch.error", "stream-fetch-error.js") == "null",
        "stream/fetch event gate failed: "
            + evaluate(engine, "__streamFetch.error", "stream-fetch-error-detail.js"));
    require(evaluate(engine, "__streamFetch.report.status", "stream-fetch-status.js")
            == "206",
        "controlled synthetic response lost its status");
    const auto p95 = std::stod(evaluate(engine, R"JS((() => {
      const values = [...__streamFetch.durations].sort((a,b) => a-b);
      return values[Math.ceil(values.length * .95) - 1];
    })())JS", "stream-fetch-p95.js"));
    require(p95 <= 50.0,
        "stream/body 100-cycle p95 exceeded 50 ms: " + std::to_string(p95));
    require(webscene_engine_request_low_memory(engine) != 0,
        "stream/fetch low-memory request was rejected");
    webscene_engine_memory_metrics memory_after{sizeof(webscene_engine_memory_metrics)};
    for (auto attempt = 0; attempt < 250; ++attempt) {
        require(webscene_engine_get_memory_metrics(engine, &memory_after) != 0,
            "stream/fetch final memory metrics were unavailable");
        if (memory_after.low_memory_notifications
            > memory_before.low_memory_notifications) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(memory_after.low_memory_notifications
            > memory_before.low_memory_notifications,
        "stream/fetch low-memory collection did not complete");
    require(memory_after.v8_used_heap_bytes
            <= memory_before.v8_used_heap_bytes + 8U * 1024U * 1024U,
        "stream/fetch cycles retained more than 8 MiB");
    std::cout << "Readable body/FetchEvent gate: cycles=100 p95=" << p95
              << "ms heapBefore=" << memory_before.v8_used_heap_bytes
              << " heapAfter=" << memory_after.v8_used_heap_bytes << '\n';
    webscene_engine_destroy(engine);
}

void test_cache_storage_and_controlled_fetch_broker()
{
    resource_server server{.content = {
        {"https://resource-plane.test/index.html",
            "<!doctype html><title>resource plane broker gate</title>"},
        {"https://resource-plane.test/host.txt", "host-fallback"},
        {"https://resource-plane.test/service-worker.js", R"JS(
            let fetches = 0;
            let misses = 0;
            let emptyMatch = false;
            self.addEventListener('install', event => event.waitUntil(skipWaiting()));
            self.addEventListener('activate', event => event.waitUntil((async () => {
              const cache = await caches.open('resource-plane-v1');
              emptyMatch = await cache.match(
                new Request('https://resource-plane.test/never')) === undefined;
              await clients.claim();
            })()));
            self.addEventListener('fetch', event => {
              const url = new URL(event.request.url);
              if (url.pathname === '/broker-too-large') {
                event.respondWith(new Response(new ReadableStream({start(controller) {
                  const chunk = new Uint8Array(1024 * 1024);
                  for (let index = 0; index < 65; ++index) controller.enqueue(chunk);
                  controller.close();
                }})));
                return;
              }
              if (url.pathname === '/broker-error') {
                event.respondWith(Promise.reject(new Error('broker failure')));
                return;
              }
              if (url.pathname !== '/broker') return;
              event.respondWith((async () => {
                ++fetches;
                const cache = await caches.open('resource-plane-v1');
                const known = await cache.match(event.request);
                if (known) return known;
                ++misses;
                const response = new Response('broker-ok', {
                  status:200,
                  headers:{'content-type':'text/plain', 'x-resource-plane':'native'}
                });
                const stored = cache.put(event.request, response.clone());
                await stored;
                return response;
              })());
            });
            self.addEventListener('message', event => {
              if (event.data !== 'report') return;
              event.source.postMessage({
                fetches, misses, emptyMatch,
                cachesTag:Object.prototype.toString.call(caches),
                cacheStorageBrand:caches instanceof CacheStorage,
                cacheMethodEnumerable:Object.getOwnPropertyDescriptor(
                  CacheStorage.prototype, 'open').enumerable
              });
            });
        )JS"}
    }};
    webscene_engine_options options{};
    options.struct_size = sizeof(options);
    options.resource_load_callback = load_test_resource;
    options.resource_load_user_data = &server;
    auto* engine = webscene_engine_create_with_options(&options);
    require(engine != nullptr, "resource-plane engine creation failed");
    constexpr std::string_view address{"https://resource-plane.test/index.html"};
    require(webscene_engine_load_url(engine, address.data(), address.size()) != 0U,
        "resource-plane navigation failed");
    require(
        evaluate_until_equals(engine, "location.href", "resource-plane-navigation.js",
            R"("https://resource-plane.test/index.html")", 2'500)
            == R"("https://resource-plane.test/index.html")",
        "resource-plane document did not become ready");

    webscene_engine_memory_metrics memory_before{sizeof(webscene_engine_memory_metrics)};
    require(webscene_engine_get_memory_metrics(engine, &memory_before) != 0,
        "resource-plane memory baseline was unavailable");
    const auto rss_before = service_worker_test_peak_rss_bytes();
    execute(engine, R"JS(
      globalThis.__resourcePlane = {
        complete:false, error:null, durations:[], report:null,
        hostFallback:null, rejected:null, oversizedRejected:null
      };
      (async () => {
        try {
          const changed = new Promise(resolve => navigator.serviceWorker.addEventListener(
            'controllerchange', resolve, {once:true}));
          const registration = await navigator.serviceWorker.register(
            'service-worker.js', {type:'module'});
          if (!navigator.serviceWorker.controller) await changed;
          for (let cycle = 0; cycle < 100; ++cycle) {
            const start = performance.now();
            const response = await fetch('/broker');
            const text = await response.text();
            if (response.status !== 200 || text !== 'broker-ok'
                || response.headers.get('x-resource-plane') !== 'native') {
              throw new Error(`controlled response mismatch at ${cycle}: ${
                response.status}/${text}`);
            }
            __resourcePlane.durations.push(performance.now() - start);
          }
          __resourcePlane.hostFallback = await (await fetch('/host.txt')).text();
          try { await fetch('/broker-error'); }
          catch (error) { __resourcePlane.rejected = String(error); }
          try { await fetch('/broker-too-large'); }
          catch (error) { __resourcePlane.oversizedRejected = String(error); }
          __resourcePlane.report = await new Promise(resolve => {
            const listener = event => {
              navigator.serviceWorker.removeEventListener('message', listener);
              resolve(event.data);
            };
            navigator.serviceWorker.addEventListener('message', listener);
            navigator.serviceWorker.controller.postMessage('report');
          });
          await registration.unregister();
        } catch (error) {
          __resourcePlane.error = String(error && error.stack || error);
        } finally { __resourcePlane.complete = true; }
      })();
    )JS", "resource-plane-gate.js");
    require(
        evaluate_until_equals(engine, "__resourcePlane.complete",
            "resource-plane-complete.js", "true", 30'000) == "true",
        "resource-plane 100-cycle gate did not finish");
    require(evaluate(engine, "__resourcePlane.error", "resource-plane-error.js") == "null",
        "resource-plane gate failed: "
            + evaluate(engine, "__resourcePlane.error", "resource-plane-error-detail.js"));
    require(evaluate(engine, "__resourcePlane.hostFallback",
            "resource-plane-fallback.js") == R"("host-fallback")",
        "unhandled controlled fetch did not fall back to the host loader");
    require(evaluate(engine, "__resourcePlane.rejected !== null",
            "resource-plane-rejection.js") == "true",
        "rejected respondWith promise did not reject the controlled fetch");
    require(evaluate(engine, R"JS(
          __resourcePlane.oversizedRejected?.includes('64 MiB') === true
        )JS", "resource-plane-oversized.js") == "true",
        "oversized service-worker response did not fail at the 64 MiB bound");
    require(evaluate(engine, R"JS((() => {
      const value = __resourcePlane.report;
      return value.fetches === 100 && value.misses === 1 && value.emptyMatch
        && value.cachesTag === '[object CacheStorage]' && value.cacheStorageBrand
        && value.cacheMethodEnumerable;
    })())JS", "resource-plane-report.js") == "true",
        "CacheStorage/controlled broker worker contract failed: "
            + evaluate(engine, "JSON.stringify(__resourcePlane.report)",
                "resource-plane-report-detail.js"));
    const auto p95 = std::stod(evaluate(engine, R"JS((() => {
      const values = [...__resourcePlane.durations].sort((a,b) => a-b);
      return values[Math.ceil(values.length * .95) - 1];
    })())JS", "resource-plane-p95.js"));
    require(p95 <= 250.0,
        "controlled resource fetch p95 exceeded 250 ms: " + std::to_string(p95));
    require(webscene_engine_request_low_memory(engine) != 0,
        "resource-plane low-memory request was rejected");
    webscene_engine_memory_metrics memory_after{sizeof(webscene_engine_memory_metrics)};
    for (auto attempt = 0; attempt < 250; ++attempt) {
        require(webscene_engine_get_memory_metrics(engine, &memory_after) != 0,
            "resource-plane final memory metrics were unavailable");
        if (memory_after.low_memory_notifications
            > memory_before.low_memory_notifications) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(memory_after.low_memory_notifications
            > memory_before.low_memory_notifications,
        "resource-plane low-memory collection did not complete");
    require(memory_after.v8_used_heap_bytes
            <= memory_before.v8_used_heap_bytes + 8U * 1024U * 1024U,
        "resource-plane cycles retained more than 8 MiB");
    const auto rss_after = service_worker_test_peak_rss_bytes();
    require(rss_after <= rss_before + 64U * 1024U * 1024U,
        "resource-plane cycles grew peak RSS by more than 64 MiB");
    std::cout << "Resource-plane gate: cycles=100 p95=" << p95
              << "ms heapBefore=" << memory_before.v8_used_heap_bytes
              << " heapAfter=" << memory_after.v8_used_heap_bytes
              << " rssBefore=" << rss_before << " rssAfter=" << rss_after << '\n';
    webscene_engine_destroy(engine);
}
