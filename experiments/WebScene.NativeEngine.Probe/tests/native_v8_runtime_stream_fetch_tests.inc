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
                const transform = new TransformStream();
                const writer = transform.writable.getWriter();
                const response = new Response(transform.readable, {
                  status:200,
                  headers:{'content-type':'text/plain', 'x-resource-plane':'native'}
                });
                const stored = cache.put(event.request, response.clone());
                await writer.write(new Uint8Array([98, 114, 111, 107, 101, 114, 45]));
                await writer.write(new Uint8Array([111, 107]));
                await writer.close();
                await writer.closed;
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
                  CacheStorage.prototype, 'open').enumerable,
                transformTag:Object.prototype.toString.call(new TransformStream()),
                transformReadable:Object.getOwnPropertyDescriptor(
                  TransformStream.prototype, 'readable').enumerable,
                writableTag:Object.prototype.toString.call(new WritableStream()),
                writerMethodEnumerable:Object.getOwnPropertyDescriptor(
                  WritableStreamDefaultWriter.prototype, 'write').enumerable
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
          if (Object.prototype.toString.call(new TransformStream()) !==
              '[object TransformStream]') {
            throw new Error('top-level TransformStream brand');
          }
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
        && value.cacheMethodEnumerable
        && value.transformTag === '[object TransformStream]'
        && value.transformReadable
        && value.writableTag === '[object WritableStream]'
        && value.writerMethodEnumerable;
    })())JS", "resource-plane-report.js") == "true",
        "CacheStorage/TransformStream worker contract failed: "
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

void test_service_worker_host_message_streaming()
{
    resource_server server{.content = {
        {"https://host-stream.test/index.html",
            "<!doctype html><title>host message stream gate</title>"},
        {"https://host-stream.test/service-worker.js", R"JS(
            const pending = new Map();
            const writers = new Map();
            let nextId = 0;
            const observations = {
              requested:0, completed:0, aborted:0, timedOut:0, ignored:0
            };
            self.addEventListener('install', event => event.waitUntil(skipWaiting()));
            self.addEventListener('activate', event => event.waitUntil(clients.claim()));
            self.addEventListener('fetch', event => {
              const path = new URL(event.request.url).pathname;
              if (!path.startsWith('/host-')) return;
              event.respondWith((async () => {
                const client = await clients.get(event.clientId);
                if (!client) return new Response('missing client', {status:404});
                const id = ++nextId;
                const result = new Promise((resolve, reject) => {
                  const timeout = setTimeout(() => {
                    if (!pending.delete(id)) return;
                    ++observations.timedOut;
                    reject(new DOMException('host response timed out', 'TimeoutError'));
                  }, 200);
                  pending.set(id, {
                    resolve(value) { clearTimeout(timeout); pending.delete(id); resolve(value); },
                    reject(error) { clearTimeout(timeout); pending.delete(id); reject(error); }
                  });
                });
                ++observations.requested;
                client.postMessage({channel:'load-resource', id, path});
                try { return await result; }
                catch (error) {
                  return new Response(String(error?.name || error), {status:408});
                }
              })());
            });
            self.addEventListener('message', event => {
              const message = event.data || {};
              const data = message.data || {};
              if (message.channel === 'did-load-resource') {
                const request = pending.get(data.id);
                if (!request || writers.has(data.id)) { ++observations.ignored; return; }
                const transform = new TransformStream();
                const writer = transform.writable.getWriter();
                writers.set(data.id, writer);
                writer.closed.then(
                  () => writers.delete(data.id), () => writers.delete(data.id));
                request.resolve(new Response(transform.readable, {
                  status:data.status, headers:{'content-type':data.mime}
                }));
                return;
              }
              if (message.channel === 'did-load-resource-chunk') {
                const writer = writers.get(data.id);
                if (!writer) { ++observations.ignored; return; }
                writer.write(data.data).catch(() => writers.delete(data.id));
                return;
              }
              if (message.channel === 'did-load-resource-end') {
                const writer = writers.get(data.id);
                if (!writer) { ++observations.ignored; return; }
                writers.delete(data.id);
                if (data.error) {
                  ++observations.aborted;
                  writer.abort(new Error('host stream aborted')).catch(() => {});
                } else {
                  ++observations.completed;
                  writer.close().catch(() => {});
                }
                return;
              }
              if (message.channel === 'report') {
                event.source.postMessage({channel:'host-stream-report',
                  observations:{...observations}, pending:pending.size,
                  writers:writers.size});
              }
            });
        )JS"}
    }};
    webscene_engine_options options{};
    options.struct_size = sizeof(options);
    options.resource_load_callback = load_test_resource;
    options.resource_load_user_data = &server;
    auto* engine = webscene_engine_create_with_options(&options);
    require(engine != nullptr, "host message stream engine creation failed");
    constexpr std::string_view address{"https://host-stream.test/index.html"};
    require(webscene_engine_load_url(engine, address.data(), address.size()) != 0U,
        "host message stream navigation failed");
    require(evaluate_until_equals(engine, "location.href",
            "host-stream-navigation.js",
            R"("https://host-stream.test/index.html")", 2'500)
            == R"("https://host-stream.test/index.html")",
        "host message stream document did not become ready");

    webscene_engine_memory_metrics memory_before{sizeof(webscene_engine_memory_metrics)};
    require(webscene_engine_get_memory_metrics(engine, &memory_before) != 0,
        "host message stream memory baseline unavailable");
    const auto rss_before = service_worker_test_peak_rss_bytes();
    const auto current_rss_before = service_worker_test_current_rss_bytes();
    execute(engine, R"JS(
      globalThis.__hostStream = {
        complete:false, error:null, providerError:null, durations:[], report:null,
        transferred:true, abortRejected:false, malformedRejected:false,
        timeoutStatus:0, backpressured:false, canceled:false,
        quotaError:null, bulkDuration:0, bulkCount:0, retired:false,
        retireDuration:0, bulkComplete:false, continueAfterBulk:false
      };
      (async () => {
        const wait = duration => new Promise(resolve => setTimeout(resolve, duration));
        const changed = new Promise(resolve => navigator.serviceWorker.addEventListener(
          'controllerchange', resolve, {once:true}));
        const registration = await navigator.serviceWorker.register(
          'service-worker.js', {type:'module'});
        if (!navigator.serviceWorker.controller) await changed;
        const controller = navigator.serviceWorker.controller;
        const provide = async ({id, path}) => {
          try {
            if (path === '/host-timeout' || path === '/host-retire') return;
            controller.postMessage({channel:'did-load-resource',
              data:{id, status:200, mime:'application/octet-stream'}});
            if (path === '/host-malformed') {
              controller.postMessage({channel:'did-load-resource-chunk',
                data:{id, data:'not bytes'}});
            } else if (path === '/host-error') {
              const bytes = new TextEncoder().encode('partial');
              controller.postMessage({channel:'did-load-resource-chunk',
                data:{id, data:bytes}}, [bytes.buffer]);
              __hostStream.transferred &&= bytes.byteLength === 0;
            } else if (path === '/host-bulk') {
              for (let chunk = 0; chunk < 4; ++chunk) {
                const bytes = new Uint8Array(256 * 1024);
                bytes[0] = 0x48;
                bytes[bytes.length - 1] = 0x53;
                controller.postMessage({channel:'did-load-resource-chunk',
                  data:{id, data:bytes}}, [bytes.buffer]);
                __hostStream.transferred &&= bytes.byteLength === 0;
                await wait(2);
              }
            } else {
              const bytes = new Uint8Array(64 * 1024);
              bytes[0] = 0x48;
              bytes[bytes.length - 1] = 0x53;
              controller.postMessage({channel:'did-load-resource-chunk',
                data:{id, data:bytes}}, [bytes.buffer]);
              __hostStream.transferred &&= bytes.byteLength === 0;
            }
            controller.postMessage({channel:'did-load-resource-end',
              data:{id, error:path === '/host-error'}});
            if (path === '/host-malformed' || path === '/host-error') {
              controller.postMessage({channel:'did-load-resource-end', data:{id}});
              controller.postMessage({channel:'did-load-resource-chunk',
                data:{id, data:new Uint8Array([1])}});
            }
          } catch (error) {
            __hostStream.providerError = String(error && error.stack || error);
          }
        };
        navigator.serviceWorker.addEventListener('message', event => {
          if (event.data?.channel === 'load-resource') void provide(event.data);
        });
        try {
          const pressure = new TransformStream();
          const pressureWriter = pressure.writable.getWriter();
          const pressureReader = pressure.readable.getReader();
          let writeSettled = false;
          let readySettled = false;
          const pressureWrite = pressureWriter.write(new Uint8Array([1])).then(
            () => { writeSettled = true; });
          pressureWriter.ready.then(() => { readySettled = true; });
          await wait(0);
          __hostStream.backpressured = !writeSettled && !readySettled
            && pressureWriter.desiredSize <= 0;
          const pressureChunk = await pressureReader.read();
          await pressureWrite;
          await pressureWriter.ready;
          await pressureWriter.close();
          const pressureEnd = await pressureReader.read();
          __hostStream.backpressured &&= pressureChunk.value[0] === 1
            && !pressureChunk.done && pressureEnd.done
            && pressureWriter.desiredSize === 0;

          const canceled = new TransformStream();
          const cancelWriter = canceled.writable.getWriter();
          const cancelReader = canceled.readable.getReader();
          const cancelWrite = cancelWriter.write(new Uint8Array([2])).then(
            () => false, () => true);
          const cancelClosed = cancelWriter.closed.then(() => false, () => true);
          await cancelReader.cancel(new DOMException('consumer canceled', 'AbortError'));
          __hostStream.canceled = await cancelWrite && await cancelClosed
            && cancelWriter.desiredSize === null;

          const bounded = new TransformStream();
          const boundedWriter = bounded.writable.getWriter();
          const queued = [];
          for (let index = 0; index < 17; ++index) {
            queued.push(boundedWriter.write(new Uint8Array(1024 * 1024)).then(
              () => null, error => error?.name || String(error)));
          }
          __hostStream.quotaError = await queued[16];
          await Promise.all(queued);

          const bulkStarted = performance.now();
          const bulk = await Promise.all(Array.from({length:32}, (_, index) =>
            fetch(`/host-bulk?${index}`).then(response => response.arrayBuffer())));
          __hostStream.bulkDuration = performance.now() - bulkStarted;
          __hostStream.bulkCount = bulk.filter(value => {
            const bytes = new Uint8Array(value);
            return bytes.byteLength === 1024 * 1024 && bytes[0] === 0x48
              && bytes[bytes.length - 1] === 0x53;
          }).length;
          __hostStream.bulkComplete = true;
          while (!__hostStream.continueAfterBulk) await wait(1);

          for (let cycle = 0; cycle < 100; ++cycle) {
            const started = performance.now();
            const body = new Uint8Array(await (await fetch(`/host-warm?${cycle}`)).arrayBuffer());
            if (body.byteLength !== 64 * 1024 || body[0] !== 0x48
                || body[body.length - 1] !== 0x53) {
              throw new Error(`warm response ${cycle} changed`);
            }
            __hostStream.durations.push(performance.now() - started);
          }

          try { await (await fetch('/host-error')).arrayBuffer(); }
          catch (error) { __hostStream.abortRejected = true; }
          try { await (await fetch('/host-malformed')).arrayBuffer(); }
          catch (error) {
            __hostStream.malformedRejected = String(error).includes('Uint8Array');
          }
          __hostStream.timeoutStatus = (await fetch('/host-timeout')).status;

          for (let cycle = 0; cycle < 100; ++cycle) {
            const stream = new TransformStream();
            const writer = stream.writable.getWriter();
            const reader = stream.readable.getReader();
            const write = writer.write(new Uint8Array([cycle & 255])).catch(() => {});
            const closed = writer.closed.catch(() => {});
            await reader.cancel(new DOMException('cycle retired', 'AbortError'));
            await write;
            await closed;
          }

          controller.postMessage({channel:'did-load-resource-chunk',
            data:{id:999999, data:new Uint8Array([1])}});
          controller.postMessage({channel:'did-load-resource-end',
            data:{id:999999}});
          __hostStream.report = await new Promise(resolve => {
            const listener = event => {
              if (event.data?.channel !== 'host-stream-report') return;
              navigator.serviceWorker.removeEventListener('message', listener);
              resolve(event.data);
            };
            navigator.serviceWorker.addEventListener('message', listener);
            controller.postMessage({channel:'report'});
          });

          const retireFetch = fetch('/host-retire').then(
            () => false, () => true);
          await wait(5);
          const retireStarted = performance.now();
          await registration.unregister();
          __hostStream.retired = await retireFetch;
          __hostStream.retireDuration = performance.now() - retireStarted;
        } catch (error) {
          __hostStream.error = String(error && error.stack || error);
        } finally { __hostStream.complete = true; }
      })();
    )JS", "host-stream-gate.js");
    require(evaluate_until_equals(engine, "__hostStream.bulkComplete",
            "host-stream-bulk-complete.js", "true", 10'000) == "true",
        "host message stream bulk gate did not finish: " + last_error(engine));
    const auto current_rss_after_bulk = service_worker_test_current_rss_bytes();
    execute(engine, "__hostStream.continueAfterBulk = true;",
        "host-stream-continue.js");
    require(evaluate_until_equals(engine, "__hostStream.complete",
            "host-stream-complete.js", "true", 60'000) == "true",
        "host message stream gate did not finish: " + last_error(engine));
    require(evaluate(engine, "__hostStream.error", "host-stream-error.js") == "null",
        "host message stream gate failed: "
            + evaluate(engine, "__hostStream.error", "host-stream-error-detail.js"));
    require(evaluate(engine, "__hostStream.providerError",
            "host-stream-provider-error.js") == "null",
        "host message provider failed: "
            + evaluate(engine, "__hostStream.providerError",
                "host-stream-provider-error-detail.js"));
    require(evaluate(engine, R"JS((() => {
      const value = __hostStream;
      const report = value.report;
      return value.transferred && value.abortRejected && value.malformedRejected
        && value.timeoutStatus === 408 && value.backpressured && value.canceled
        && value.quotaError === 'QuotaExceededError'
        && value.bulkCount === 32 && value.bulkDuration <= 5000
        && value.retired && value.retireDuration <= 500
        && report.pending === 0 && report.writers === 0
        && report.observations.aborted === 1
        && report.observations.timedOut === 1
        && report.observations.ignored >= 4;
    })())JS", "host-stream-semantics.js") == "true",
        "host message stream semantics diverged: "
            + evaluate(engine, "JSON.stringify(__hostStream)",
                "host-stream-semantics-detail.js"));
    const auto p95 = std::stod(evaluate(engine, R"JS((() => {
      const values = [...__hostStream.durations].sort((a,b) => a-b);
      return values[Math.ceil(values.length * .95) - 1];
    })())JS", "host-stream-p95.js"));
    require(p95 <= 100.0,
        "64 KiB host message stream p95 exceeded 100 ms: "
            + std::to_string(p95));
    require(webscene_engine_request_low_memory(engine) != 0,
        "host message stream low-memory request rejected");
    webscene_engine_memory_metrics memory_after{sizeof(webscene_engine_memory_metrics)};
    for (auto attempt = 0; attempt < 250; ++attempt) {
        require(webscene_engine_get_memory_metrics(engine, &memory_after) != 0,
            "host message stream final memory metrics unavailable");
        if (memory_after.low_memory_notifications
            > memory_before.low_memory_notifications) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(memory_after.low_memory_notifications
            > memory_before.low_memory_notifications,
        "host message stream low-memory collection did not complete");
    require(memory_after.v8_used_heap_bytes
            <= memory_before.v8_used_heap_bytes + 8U * 1024U * 1024U,
        "host message stream cycles retained more than 8 MiB V8 heap");
    const auto bulk_ms = std::stod(evaluate(engine, "__hostStream.bulkDuration",
        "host-stream-bulk-duration.js"));
    const auto rss_after = service_worker_test_peak_rss_bytes();
    webscene_engine_destroy(engine);
    const auto current_rss_after = service_worker_test_current_rss_bytes();
    require(rss_before == 0U || rss_after <= rss_before + 256U * 1024U * 1024U,
        "host message stream stress exceeded 256 MiB peak RSS growth");
    require(current_rss_after_bulk == 0U || current_rss_after == 0U
            || current_rss_after <= current_rss_after_bulk + 64U * 1024U * 1024U,
        "host message stream cycles retained more than 64 MiB RSS after teardown: before="
            + std::to_string(current_rss_after_bulk) + " after="
            + std::to_string(current_rss_after));
    std::cout << "Host message stream gate: warmCycles=100 p95=" << p95
              << "ms bulk32MiB=" << bulk_ms
              << "ms heapBefore=" << memory_before.v8_used_heap_bytes
              << " heapAfter=" << memory_after.v8_used_heap_bytes
              << " peakRssBefore=" << rss_before << " peakRssAfter=" << rss_after
              << " currentRssBefore=" << current_rss_before
              << " currentRssAfterBulk=" << current_rss_after_bulk
              << " currentRssAfter=" << current_rss_after << '\n';
}

void test_controlled_fetch_abort_and_retirement()
{
    resource_server server{.content = {
        {"https://fetch-abort.test/index.html",
            "<!doctype html><title>controlled fetch abort gate</title>"},
        {"https://fetch-abort.test/service-worker.js", R"JS(
            self.addEventListener('install', event => event.waitUntil(skipWaiting()));
            self.addEventListener('activate', event => event.waitUntil(clients.claim()));
            self.addEventListener('fetch', event => {
              const path = new URL(event.request.url).pathname;
              if (path === '/controlled-pending') {
                event.respondWith(new Promise(() => {}));
              } else if (path === '/controlled-success') {
                event.respondWith(new Response('released', {
                  status:200, headers:{'content-type':'text/plain'}
                }));
              }
            });
        )JS"}
    }};
    webscene_engine_options options{};
    options.struct_size = sizeof(options);
    options.resource_load_callback = load_test_resource;
    options.resource_load_user_data = &server;
    auto* engine = webscene_engine_create_with_options(&options);
    require(engine != nullptr, "controlled fetch abort engine creation failed");
    constexpr std::string_view address{"https://fetch-abort.test/index.html"};
    require(webscene_engine_load_url(engine, address.data(), address.size()) != 0U,
        "controlled fetch abort navigation failed");
    require(evaluate_until_equals(engine, "location.href", "fetch-abort-navigation.js",
            R"("https://fetch-abort.test/index.html")", 2'500)
            == R"("https://fetch-abort.test/index.html")",
        "controlled fetch abort document did not become ready");

    webscene_engine_memory_metrics memory_before{sizeof(webscene_engine_memory_metrics)};
    require(webscene_engine_get_memory_metrics(engine, &memory_before) != 0,
        "controlled fetch abort memory baseline unavailable");
    const auto rss_before = service_worker_test_current_rss_bytes();
    execute(engine, R"JS(
      globalThis.__fetchAbort = {
        complete:false, error:null, preAborted:false, customReason:false,
        canceled:0, cycles:0, durations:[], releaseLatency:0,
        retireRejected:false, retireLatency:0
      };
      (async () => {
        const wait = duration => new Promise(resolve => setTimeout(resolve, duration));
        try {
          const changed = new Promise(resolve => navigator.serviceWorker.addEventListener(
            'controllerchange', resolve, {once:true}));
          const registration = await navigator.serviceWorker.register('service-worker.js');
          if (!navigator.serviceWorker.controller) await changed;

          const pre = new AbortController();
          pre.abort();
          try { await fetch('/controlled-pending', {signal:pre.signal}); }
          catch (error) { __fetchAbort.preAborted = error?.name === 'AbortError'; }

          const custom = new AbortController();
          const customReason = new Error('custom abort reason');
          const customFetch = fetch('/controlled-pending', {signal:custom.signal});
          await wait(0);
          custom.abort(customReason);
          try { await customFetch; }
          catch (error) { __fetchAbort.customReason = error === customReason; }
          for (;;) {
            try {
              if (await (await fetch('/controlled-success')).text() === 'released') break;
            } catch {}
            await wait(1);
          }

          const controllers = Array.from({length:32}, () => new AbortController());
          const blocked = controllers.map((controller, index) =>
            fetch(`/controlled-pending?bulk=${index}`, {signal:controller.signal})
              .then(() => false, error => error?.name === 'AbortError'));
          await wait(2);
          const releasedAt = performance.now();
          for (const controller of controllers) controller.abort();
          __fetchAbort.canceled = (await Promise.all(blocked)).filter(Boolean).length;
          for (;;) {
            try {
              const response = await fetch('/controlled-success');
              if (await response.text() === 'released') break;
            } catch {}
            if (performance.now() - releasedAt > 500) {
              throw new Error('controlled fetch capacity was not released');
            }
            await wait(1);
          }
          __fetchAbort.releaseLatency = performance.now() - releasedAt;

          for (let cycle = 0; cycle < 100; ++cycle) {
            const controller = new AbortController();
            const pending = fetch(`/controlled-pending?cycle=${cycle}`, {
              signal:controller.signal
            });
            await wait(0);
            const started = performance.now();
            controller.abort();
            const aborted = await pending.then(
              () => false, error => error?.name === 'AbortError');
            if (!aborted) throw new Error(`cycle ${cycle} did not abort`);
            __fetchAbort.durations.push(performance.now() - started);
            ++__fetchAbort.cycles;
          }

          const retiring = fetch('/controlled-pending?retire=1');
          await wait(2);
          const retireStarted = performance.now();
          await registration.unregister();
          __fetchAbort.retireRejected = await retiring.then(() => false, () => true);
          __fetchAbort.retireLatency = performance.now() - retireStarted;
        } catch (error) {
          __fetchAbort.error = String(error && error.stack || error);
        } finally { __fetchAbort.complete = true; }
      })();
    )JS", "controlled-fetch-abort-gate.js");
    require(evaluate_until_equals(engine, "__fetchAbort.complete",
            "fetch-abort-complete.js", "true", 15'000) == "true",
        "controlled fetch abort gate did not finish: " + last_error(engine));
    require(evaluate(engine, "__fetchAbort.error", "fetch-abort-error.js") == "null",
        "controlled fetch abort gate failed: "
            + evaluate(engine, "__fetchAbort.error", "fetch-abort-error-detail.js"));
    require(evaluate(engine, R"JS((() => {
      const value = __fetchAbort;
      return value.preAborted && value.customReason && value.canceled === 32
        && value.cycles === 100 && value.releaseLatency <= 100
        && value.retireRejected && value.retireLatency <= 500;
    })())JS", "fetch-abort-semantics.js") == "true",
        "controlled fetch abort semantics diverged: "
            + evaluate(engine, "JSON.stringify(__fetchAbort)",
                "fetch-abort-semantics-detail.js"));
    const auto p95 = std::stod(evaluate(engine, R"JS((() => {
      const values = [...__fetchAbort.durations].sort((a,b) => a-b);
      return values[Math.ceil(values.length * .95) - 1];
    })())JS", "fetch-abort-p95.js"));
    require(p95 <= 100.0,
        "controlled fetch abort p95 exceeded 100 ms: " + std::to_string(p95));
    require(webscene_engine_request_low_memory(engine) != 0,
        "controlled fetch abort low-memory request was rejected");
    webscene_engine_memory_metrics memory_after{sizeof(webscene_engine_memory_metrics)};
    for (auto attempt = 0; attempt < 250; ++attempt) {
        require(webscene_engine_get_memory_metrics(engine, &memory_after) != 0,
            "controlled fetch abort final memory metrics unavailable");
        if (memory_after.low_memory_notifications
            > memory_before.low_memory_notifications) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(memory_after.v8_used_heap_bytes
            <= memory_before.v8_used_heap_bytes + 8U * 1024U * 1024U,
        "controlled fetch abort cycles retained more than 8 MiB V8 heap");
    const auto rss_after = service_worker_test_current_rss_bytes();
    require(rss_after <= rss_before + 64U * 1024U * 1024U,
        "controlled fetch abort cycles retained more than 64 MiB RSS");
    std::cout << "Controlled fetch abort gate: cycles=100 p95=" << p95
              << "ms heapBefore=" << memory_before.v8_used_heap_bytes
              << " heapAfter=" << memory_after.v8_used_heap_bytes
              << " rssBefore=" << rss_before << " rssAfter=" << rss_after << '\n';
    webscene_engine_destroy(engine);
}

void test_controlled_range_cache_headers_and_body_budget()
{
    resource_server server{.content = {
        {"https://range-cache.test/index.html",
            "<!doctype html><title>range cache broker gate</title>"},
        {"https://range-cache.test/service-worker.js", R"JS(
            const observations = [];
            const bulkBody = new Uint8Array(1024 * 1024);
            bulkBody[0] = 0x57;
            bulkBody[bulkBody.length - 1] = 0x53;
            const warmBody = new Uint8Array(64 * 1024);
            warmBody[0] = 0x57;
            self.addEventListener('install', event => event.waitUntil(skipWaiting()));
            self.addEventListener('activate', event => event.waitUntil(clients.claim()));
            self.addEventListener('fetch', event => {
              const url = new URL(event.request.url);
              observations.push({path:url.pathname, method:event.request.method,
                range:event.request.headers.get('range'),
                validator:event.request.headers.get('if-none-match'),
                token:event.request.headers.get('x-broker-token')});
              if (url.pathname === '/range') {
                event.respondWith(new Response('2345', {status:206, headers:{
                  'content-type':'application/octet-stream',
                  'content-range':'bytes 2-5/10', 'etag':'"range-v1"',
                  'last-modified':'Wed, 17 Sep 2025 12:00:00 GMT'
                }}));
              } else if (url.pathname === '/conditional') {
                event.respondWith(new Response(null,
                  {status:304, headers:{etag:'"cache-v1"'}}));
              } else if (url.pathname === '/unauthorized') {
                event.respondWith(new Response(null, {status:401}));
              } else if (url.pathname === '/missing') {
                event.respondWith(new Response(null, {status:404}));
              } else if (url.pathname === '/head') {
                event.respondWith(new Response(null, {status:200, headers:{
                  'content-length':'9', 'content-type':'text/plain'
                }}));
              } else if (url.pathname === '/method') {
                event.respondWith(new Response(null,
                  {status:405, headers:{allow:'GET, HEAD'}}));
              } else if (url.pathname === '/bulk') {
                event.respondWith(new Response(bulkBody));
              } else if (url.pathname === '/warm') {
                event.respondWith(new Response(warmBody));
              }
            });
            self.addEventListener('message', event => {
              if (event.data === 'report') event.source.postMessage({observations});
            });
        )JS"}
    }};
    webscene_engine_options options{};
    options.struct_size = sizeof(options);
    options.resource_load_callback = load_test_resource;
    options.resource_load_user_data = &server;
    auto* engine = webscene_engine_create_with_options(&options);
    require(engine != nullptr, "range/cache broker engine creation failed");
    constexpr std::string_view address{"https://range-cache.test/index.html"};
    require(webscene_engine_load_url(engine, address.data(), address.size()) != 0U,
        "range/cache broker navigation failed");
    require(evaluate_until_equals(engine, "location.href", "range-cache-navigation.js",
            R"("https://range-cache.test/index.html")", 2'500)
            == R"("https://range-cache.test/index.html")",
        "range/cache broker document did not become ready");

    webscene_engine_memory_metrics memory_before{sizeof(webscene_engine_memory_metrics)};
    require(webscene_engine_get_memory_metrics(engine, &memory_before) != 0,
        "range/cache broker memory baseline unavailable");
    const auto rss_before = service_worker_test_peak_rss_bytes();
    execute(engine, R"JS(
      globalThis.__rangeCache = {complete:false,error:null,warm:[],bulkMs:0,report:null};
      (async () => {
        try {
          const changed = new Promise(resolve => navigator.serviceWorker.addEventListener(
            'controllerchange', resolve, {once:true}));
          const registration = await navigator.serviceWorker.register('service-worker.js');
          if (!navigator.serviceWorker.controller) await changed;
          const range = await fetch('/range', {headers:{Range:'bytes=2-5'}});
          if (range.status !== 206
              || range.headers.get('content-range') !== 'bytes 2-5/10'
              || range.headers.get('etag') !== '"range-v1"'
              || range.headers.get('last-modified')
                  !== 'Wed, 17 Sep 2025 12:00:00 GMT'
              || await range.text() !== '2345') throw new Error('range response mismatch');
          const conditional = await fetch('/conditional', {
            headers:{'If-None-Match':'"cache-v1"'}});
          const unauthorized = await fetch('/unauthorized');
          const missing = await fetch('/missing');
          if (conditional.status !== 304 || unauthorized.status !== 401
              || missing.status !== 404) throw new Error('status response mismatch');
          const head = await fetch('/head', {method:'HEAD'});
          const method = await fetch('/method', {method:'POST',
            headers:{'X-Broker-Token':'method-token'}, body:'payload'});
          if (head.status !== 200 || await head.text() !== ''
              || head.headers.get('content-length') !== '9'
              || method.status !== 405 || method.headers.get('allow') !== 'GET, HEAD') {
            throw new Error('method response mismatch');
          }
          let invalidHeaderRejected = false;
          try { await fetch('/method', {headers:{'X-Bad':'line\r\nbreak'}}); }
          catch (error) { invalidHeaderRejected = error instanceof TypeError; }
          if (!invalidHeaderRejected) throw new Error('malformed header was accepted');
          for (let cycle = 0; cycle < 100; ++cycle) {
            const started = performance.now();
            const response = await fetch(`/warm?cycle=${cycle}`);
            const body = await response.arrayBuffer();
            if (body.byteLength !== 64 * 1024) throw new Error('warm body size');
            __rangeCache.warm.push(performance.now() - started);
          }
          const bulkStarted = performance.now();
          const responses = await Promise.all(Array.from({length:32}, (_, index) =>
            fetch(`/bulk?index=${index}`)));
          const bodies = await Promise.all(responses.map(response => response.arrayBuffer()));
          __rangeCache.bulkMs = performance.now() - bulkStarted;
          if (!bodies.every(body => body.byteLength === 1024 * 1024
              && new Uint8Array(body)[0] === 0x57
              && new Uint8Array(body)[body.byteLength - 1] === 0x53)) {
            throw new Error('bulk body mismatch');
          }
          __rangeCache.report = await new Promise(resolve => {
            const listener = event => {
              navigator.serviceWorker.removeEventListener('message', listener);
              resolve(event.data);
            };
            navigator.serviceWorker.addEventListener('message', listener);
            navigator.serviceWorker.controller.postMessage('report');
          });
          const observed = suffix => __rangeCache.report.observations.find(
            value => value.path === `/${suffix}`);
          if (observed('range')?.range !== 'bytes=2-5'
              || observed('conditional')?.validator !== '"cache-v1"'
              || observed('head')?.method !== 'HEAD'
              || observed('method')?.method !== 'POST'
              || observed('method')?.token !== 'method-token') {
            throw new Error(`request metadata mismatch: ${JSON.stringify(__rangeCache.report)}`);
          }
          await registration.unregister();
        } catch (error) {
          __rangeCache.error = String(error && error.stack || error);
        } finally { __rangeCache.complete = true; }
      })();
    )JS", "range-cache-gate.js");
    require(evaluate_until_equals(engine, "__rangeCache.complete",
            "range-cache-complete.js", "true", 30'000) == "true",
        "range/cache broker gate did not finish");
    require(evaluate(engine, "__rangeCache.error", "range-cache-error.js") == "null",
        "range/cache broker gate failed: "
            + evaluate(engine, "__rangeCache.error", "range-cache-error-detail.js"));
    const auto p95 = std::stod(evaluate(engine, R"JS((() => {
      const values = [...__rangeCache.warm].sort((a,b) => a-b);
      return values[Math.ceil(values.length * .95) - 1];
    })())JS", "range-cache-p95.js"));
    const auto bulk_ms = std::stod(evaluate(
        engine, "__rangeCache.bulkMs", "range-cache-bulk-ms.js"));
    require(p95 <= 100.0,
        "warm 64 KiB controlled fetch p95 exceeded 100 ms: "
            + std::to_string(p95));
    require(bulk_ms <= 5'000.0,
        "32 concurrent one-MiB responses exceeded 5 seconds: "
            + std::to_string(bulk_ms));
    require(webscene_engine_request_low_memory(engine) != 0,
        "range/cache broker low-memory request rejected");
    webscene_engine_memory_metrics memory_after{sizeof(webscene_engine_memory_metrics)};
    for (auto attempt = 0; attempt < 250; ++attempt) {
        require(webscene_engine_get_memory_metrics(engine, &memory_after) != 0,
            "range/cache broker final memory metrics unavailable");
        if (memory_after.low_memory_notifications
            > memory_before.low_memory_notifications) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(memory_after.low_memory_notifications
            > memory_before.low_memory_notifications,
        "range/cache broker low-memory collection did not complete");
    require(memory_after.v8_used_heap_bytes
            <= memory_before.v8_used_heap_bytes + 8U * 1024U * 1024U,
        "range/cache broker retained more than 8 MiB V8 heap");
    const auto rss_after = service_worker_test_peak_rss_bytes();
    std::cout << "Range/cache broker gate: warmCycles=100 p95=" << p95
              << "ms bulk32MiB=" << bulk_ms
              << "ms heapBefore=" << memory_before.v8_used_heap_bytes
              << " heapAfter=" << memory_after.v8_used_heap_bytes
              << " rssBefore=" << rss_before << " rssAfter=" << rss_after << '\n';
    require(rss_after <= rss_before + 256U * 1024U * 1024U,
        "range/cache broker exceeded 256 MiB peak RSS growth");
    webscene_engine_destroy(engine);
}

void test_controlled_connected_resource_interception()
{
    resource_server server{.content = {
        {"https://connected-resource.test/index.html",
            "<!doctype html><title>connected resource interception</title>"
            "<div id='styled'>controlled</div>"},
        {"https://connected-resource.test/next.html",
            "<!doctype html><title>retired connected resource</title>"},
        {"https://connected-resource.test/service-worker.js", R"JS(
            const destinations = {};
            self.addEventListener('install', event => event.waitUntil(skipWaiting()));
            self.addEventListener('activate', event => event.waitUntil(clients.claim()));
            self.addEventListener('fetch', event => {
              const url = new URL(event.request.url);
              destinations[url.pathname] = event.request.destination;
              if (url.pathname === '/fallback.js') return;
              if (url.pathname === '/denied.js') {
                event.respondWith(Promise.reject(new Error('denied by controller')));
                return;
              }
              if (url.pathname === '/pending.svg') {
                event.respondWith(new Promise(() => {}));
                return;
              }
              if (url.pathname === '/controlled.js' || url.pathname === '/cycle.js') {
                event.respondWith(new Response(
                  'globalThis.__controlledScript=(globalThis.__controlledScript||0)+1;',
                  {headers:{'content-type':'text/javascript'}}));
                return;
              }
              if (url.pathname === '/controlled.css' || url.pathname === '/cycle.css') {
                event.respondWith(new Response('#styled{color:rgb(12, 34, 56)}',
                  {headers:{'content-type':'text/css'}}));
                return;
              }
              if (url.pathname === '/controlled.svg' || url.pathname === '/cycle.svg') {
                event.respondWith(new Response(
                  '<svg xmlns="http://www.w3.org/2000/svg" width="4" height="3" viewBox="0 0 4 3"><rect width="4" height="3"/></svg>',
                  {headers:{'content-type':'image/svg+xml'}}));
              }
            });
            self.addEventListener('message', event => {
              if (event.data === 'destinations') event.source.postMessage(destinations);
            });
        )JS"},
        {"https://connected-resource.test/fallback.js",
            "globalThis.__hostFallback=(globalThis.__hostFallback||0)+1;"},
        {"https://connected-resource.test/denied.js",
            "globalThis.__deniedBypass=true;"}
    }};
    webscene_engine_options options{};
    options.struct_size = sizeof(options);
    options.resource_load_callback = load_test_resource;
    options.resource_load_user_data = &server;
    auto* engine = webscene_engine_create_with_options(&options);
    require(engine != nullptr, "connected resource engine creation failed");
    constexpr std::string_view address{"https://connected-resource.test/index.html"};
    require(webscene_engine_load_url(engine, address.data(), address.size()) != 0U,
        "connected resource navigation failed");
    require(evaluate_until_equals(engine, "location.href", "connected-nav.js",
            R"("https://connected-resource.test/index.html")", 2'500)
            == R"("https://connected-resource.test/index.html")",
        "connected resource document did not become ready");

    webscene_engine_memory_metrics memory_before{sizeof(webscene_engine_memory_metrics)};
    require(webscene_engine_get_memory_metrics(engine, &memory_before) != 0,
        "connected resource memory baseline unavailable");
    const auto rss_before = service_worker_test_peak_rss_bytes();
    execute(engine, R"JS(
      globalThis.__connectedGate = {complete:false,error:null,phase:'register',durations:[],destinations:null};
      (async () => {
        try {
          const changed = new Promise(resolve => navigator.serviceWorker.addEventListener(
            'controllerchange', resolve, {once:true}));
          await navigator.serviceWorker.register('service-worker.js', {type:'module'});
          if (!navigator.serviceWorker.controller) await changed;
          __connectedGate.phase = 'controlled-script';
          const load = (element, expectError = false) => new Promise((resolve, reject) => {
            element.onload = () => expectError
              ? reject(new Error('resource unexpectedly loaded')) : resolve('load');
            element.onerror = () => expectError
              ? resolve('error') : reject(new Error('resource failed'));
            document.body.append(element);
          });
          const script = document.createElement('script');
          script.src = 'controlled.js';
          await load(script); script.remove();
          if (__controlledScript !== 1) throw new Error('controlled script body not executed');

          __connectedGate.phase = 'controlled-style';
          const link = document.createElement('link');
          link.rel = 'stylesheet'; link.href = 'controlled.css';
          await load(link);
          if (getComputedStyle(document.getElementById('styled')).color !== 'rgb(12, 34, 56)') {
            throw new Error('controlled stylesheet body not applied');
          }
          link.remove();

          __connectedGate.phase = 'controlled-image';
          const image = document.createElement('img');
          image.src = 'controlled.svg';
          await load(image);
          if (image.naturalWidth !== 4 || image.naturalHeight !== 3) {
            throw new Error(`controlled image dimensions ${image.naturalWidth}x${image.naturalHeight}`);
          }
          image.remove();

          __connectedGate.phase = 'fallback';
          const fallback = document.createElement('script');
          fallback.src = 'fallback.js';
          await load(fallback); fallback.remove();
          if (__hostFallback !== 1) throw new Error('unhandled request did not fall through');

          __connectedGate.phase = 'denied';
          const denied = document.createElement('script');
          denied.src = 'denied.js';
          await load(denied, true); denied.remove();
          if (globalThis.__deniedBypass === true) {
            throw new Error('handled rejection bypassed controller admission');
          }

          __connectedGate.phase = 'cycles';
          for (let cycle = 0; cycle < 100; ++cycle) {
            const started = performance.now();
            const kind = cycle % 3;
            const element = document.createElement(kind === 0 ? 'script' : kind === 1 ? 'link' : 'img');
            if (kind === 0) element.src = `cycle.js?cycle=${cycle}`;
            else if (kind === 1) {
              element.rel = 'stylesheet'; element.href = `cycle.css?cycle=${cycle}`;
            } else element.src = `cycle.svg?cycle=${cycle}`;
            await load(element);
            element.remove();
            __connectedGate.durations.push(performance.now() - started);
          }
          __connectedGate.destinations = await new Promise(resolve => {
            const listener = event => {
              navigator.serviceWorker.removeEventListener('message', listener);
              resolve(event.data);
            };
            navigator.serviceWorker.addEventListener('message', listener);
            navigator.serviceWorker.controller.postMessage('destinations');
          });
          if (__connectedGate.destinations['/controlled.js'] !== 'script'
              || __connectedGate.destinations['/controlled.css'] !== 'style'
              || __connectedGate.destinations['/controlled.svg'] !== 'image') {
            throw new Error(`resource destinations ${JSON.stringify(__connectedGate.destinations)}`);
          }
          __connectedGate.phase = 'done';
        } catch (error) {
          __connectedGate.error = String(error && error.stack || error);
        } finally { __connectedGate.complete = true; }
      })();
    )JS", "connected-resource-gate.js");
    const auto completed = evaluate_until_equals(engine, "__connectedGate.complete",
        "connected-complete.js", "true", 5'000);
    require(completed == "true",
        "controlled connected resources did not complete at phase "
            + evaluate(engine, "__connectedGate.phase", "connected-phase.js"));
    require(evaluate(engine, "__connectedGate.error", "connected-error.js") == "null",
        "connected resource gate failed: "
            + evaluate(engine, "__connectedGate.error", "connected-error-detail.js"));
    const auto p95 = std::stod(evaluate(engine, R"JS((() => {
      const values = [...__connectedGate.durations].sort((a,b) => a-b);
      return values[Math.ceil(values.length * .95) - 1];
    })())JS", "connected-p95.js"));
    require(p95 <= 100.0,
        "connected resource 100-cycle p95 exceeded 100 ms: " + std::to_string(p95));
    require(webscene_engine_request_low_memory(engine) != 0,
        "connected resource low-memory request rejected");
    webscene_engine_memory_metrics memory_after{sizeof(webscene_engine_memory_metrics)};
    for (auto attempt = 0; attempt < 250; ++attempt) {
        require(webscene_engine_get_memory_metrics(engine, &memory_after) != 0,
            "connected resource final memory metrics unavailable");
        if (memory_after.low_memory_notifications > memory_before.low_memory_notifications) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(memory_after.low_memory_notifications > memory_before.low_memory_notifications,
        "connected resource low-memory collection did not complete");
    require(memory_after.v8_used_heap_bytes
            <= memory_before.v8_used_heap_bytes + 8U * 1024U * 1024U,
        "connected resource cycles retained more than 8 MiB V8 heap");
    const auto rss_after = service_worker_test_peak_rss_bytes();
    require(rss_after <= rss_before + 64U * 1024U * 1024U,
        "connected resource cycles exceeded 64 MiB RSS growth");
    execute(engine, R"JS((() => {
      const image = document.createElement('img');
      image.src = 'pending.svg';
      image.onload = () => { globalThis.__retiredResourceLoaded = true; };
      document.body.append(image);
    })())JS", "connected-pending-navigation.js");
    const auto navigation_started = std::chrono::steady_clock::now();
    constexpr std::string_view next_address{"https://connected-resource.test/next.html"};
    require(webscene_engine_load_url(
            engine, next_address.data(), next_address.size()) != 0U,
        "navigation did not retire the pending connected resource");
    require(evaluate_until_equals(engine, "location.href", "connected-retired-nav.js",
            R"("https://connected-resource.test/next.html")", 2'500)
            == R"("https://connected-resource.test/next.html")",
        "navigation after pending connected resource did not complete");
    const auto navigation_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - navigation_started).count();
    require(navigation_ms <= 500.0,
        "pending connected resource retirement exceeded 500 ms");
    std::cout << "Connected resource gate: cycles=100 p95=" << p95
              << "ms heapBefore=" << memory_before.v8_used_heap_bytes
              << " heapAfter=" << memory_after.v8_used_heap_bytes
              << " rssBefore=" << rss_before
              << " rssAfter=" << rss_after
              << " retireNavigation=" << navigation_ms << "ms\n";
    webscene_engine_destroy(engine);
}

void test_vscode_markdown_css_image_resource_broker()
{
    const auto repository_root = std::filesystem::path(__FILE__)
        .parent_path().parent_path().parent_path().parent_path();
    const auto fixture_root = repository_root
        / "tests/WebPlatformSubset/contracts/fixtures/vscode-markdown-preview-645f29c";
    const auto read_fixture = [&](std::string_view name) {
        std::ifstream stream(fixture_root / name, std::ios::binary);
        require(stream.good(), "VS Code Markdown fixture file was not found");
        return std::string{
            std::istreambuf_iterator<char>(stream),
            std::istreambuf_iterator<char>()};
    };
    const auto exact_worker = read_fixture("service-worker.js");
    const auto readme = read_fixture("README.md");
    require(exact_worker.find("const VERSION = 6;") != std::string::npos
            && exact_worker.find("maxActiveHostResourceResponseBodies = 32")
                != std::string::npos,
        "VS Code Markdown fixture did not contain the pinned v6 worker bounds");
    require(readme.find("# Code OSS on AppScene") != std::string::npos,
        "VS Code Markdown fixture did not contain the current README");

    constexpr std::string_view address{"https://css-resource.test/index.html"};
    const auto worker = R"JS(
      const counts = {};
      const destinations = {};
      const svg = viewBox => `<svg xmlns="http://www.w3.org/2000/svg" viewBox="${viewBox}"><rect width="3" height="2"/></svg>`;
      self.addEventListener('install', event => event.waitUntil(skipWaiting()));
      self.addEventListener('activate', event => event.waitUntil(clients.claim()));
      self.addEventListener('fetch', event => {
        const url = new URL(event.request.url);
        counts[url.pathname] = (counts[url.pathname] || 0) + 1;
        destinations[url.pathname] = event.request.destination;
        if (url.pathname === '/fallback.svg') return;
        if (url.pathname === '/denied.svg') {
          event.respondWith(Promise.reject(new Error('denied CSS image')));
          return;
        }
        if (url.pathname === '/stale.svg') {
          event.respondWith(new Promise(resolve => setTimeout(() => resolve(
            new Response(svg('0 0 13 11'), {headers:{'content-type':'image/svg+xml'}})), 25)));
          return;
        }
        if (url.pathname === '/controlled.svg' || url.pathname === '/cycle.svg') {
          event.respondWith(new Response(svg('0 0 3 2'),
            {headers:{'content-type':'image/svg+xml'}}));
        }
      });
      self.addEventListener('message', event => {
        if (event.data === 'report') event.source.postMessage({counts, destinations});
      });
    )JS";
    const auto bypass_svg = std::string{
        "<svg xmlns='http://www.w3.org/2000/svg' width='99' height='97' "
        "viewBox='0 0 99 97'><rect width='99' height='97'/></svg>"};
    struct css_resource_server final {
        resource_server resources;
        std::atomic<int> controlled_host_reads{0};
        std::atomic<int> fallback_host_reads{0};
    } server{.resources = {.content = {
        {std::string{address},
            "<!doctype html><title>controlled CSS image</title>"
            "<div id='preview' style='width:32px;height:24px'></div>"},
        {"https://css-resource.test/service-worker.js", worker},
        {"https://css-resource.test/controlled.svg", bypass_svg},
        {"https://css-resource.test/cycle.svg", bypass_svg},
        {"https://css-resource.test/denied.svg", bypass_svg},
        {"https://css-resource.test/stale.svg", bypass_svg},
        {"https://css-resource.test/fallback.svg",
            "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 5 4'/>"}
    }}};
    webscene_engine_options options{};
    options.struct_size = sizeof(options);
    options.resource_load_callback = +[](
        void* user_data, uint32_t kind, const char* url, size_t url_length,
        const char* entity_tag, size_t entity_tag_length, int64_t last_modified,
        char* destination, size_t destination_capacity) {
        auto& fixture = *static_cast<css_resource_server*>(user_data);
        const std::string_view resource(url, url_length);
        const auto known = fixture.resources.content.find(std::string(resource));
        constexpr size_t header_size = 2U + sizeof(uint32_t) + sizeof(int64_t) * 2U;
        const auto first_transfer = known != fixture.resources.content.end()
            && (destination == nullptr
                || destination_capacity != header_size + known->second.size());
        if (first_transfer) {
            if (resource.ends_with("/fallback.svg")) {
                fixture.fallback_host_reads.fetch_add(1, std::memory_order_relaxed);
            } else if (resource.find("/controlled.svg") != std::string_view::npos
                || resource.find("/cycle.svg") != std::string_view::npos
                || resource.find("/denied.svg") != std::string_view::npos
                || resource.find("/stale.svg") != std::string_view::npos) {
                fixture.controlled_host_reads.fetch_add(1, std::memory_order_relaxed);
            }
        }
        return load_test_resource(
            &fixture.resources, kind, url, url_length, entity_tag,
            entity_tag_length, last_modified, destination, destination_capacity);
    };
    options.resource_load_user_data = &server;
    auto* engine = webscene_engine_create_with_options(&options);
    require(engine != nullptr, "VS Code Markdown CSS image engine creation failed");
    require(webscene_engine_load_url(engine, address.data(), address.size()) != 0U,
        "VS Code Markdown CSS image navigation failed");
    require(evaluate_until_equals(engine, "location.href", "css-image-navigation.js",
            R"("https://css-resource.test/index.html")", 2'500)
            == R"("https://css-resource.test/index.html")",
        "VS Code Markdown CSS image document did not become ready");

    webscene_engine_memory_metrics memory_before{sizeof(webscene_engine_memory_metrics)};
    require(webscene_engine_get_memory_metrics(engine, &memory_before) != 0,
        "VS Code Markdown CSS image memory baseline unavailable");
    const auto peak_rss_before = service_worker_test_peak_rss_bytes();
    const auto current_rss_before = service_worker_test_current_rss_bytes();
    execute(engine, R"JS(
      globalThis.__webSceneWptState = {
        complete:false, harness:null, results:[], errors:[], diagnostics:[], durations:[], phase:'register'
      };
      (async () => {
        const state = __webSceneWptState;
        try {
          const changed = new Promise(resolve => navigator.serviceWorker.addEventListener(
            'controllerchange', resolve, {once:true}));
          const registration = await navigator.serviceWorker.register(
            'service-worker.js', {type:'module'});
          if (!navigator.serviceWorker.controller) await changed;
          state.phase = 'controlled';
          const target = document.getElementById('preview');
          const report = () => new Promise(resolve => {
          const listener = event => {
            navigator.serviceWorker.removeEventListener('message', listener);
            resolve(event.data);
          };
          navigator.serviceWorker.addEventListener('message', listener);
          navigator.serviceWorker.controller.postMessage('report');
          });
          const waitFor = async (path, prior) => {
          for (let attempt = 0; attempt < 100; ++attempt) {
            const value = await report();
            if ((value.counts[path] || 0) > prior) return value;
            await new Promise(resolve => setTimeout(resolve, 1));
          }
          throw new Error(`CSS image did not reach controller: ${path}`);
          };
          target.style.backgroundImage = 'url("controlled.svg")';
          let value = await waitFor('/controlled.svg', 0);
          if (value.destinations['/controlled.svg'] !== 'image') {
            throw new Error(`CSS image destination ${value.destinations['/controlled.svg']}`);
          }
          for (let cycle = 0; cycle < 100; ++cycle) {
            state.phase = `cycle-${cycle}`;
            const prior = value.counts['/cycle.svg'] || 0;
            const started = performance.now();
            target.style.backgroundImage = `url("cycle.svg?cycle=${cycle}")`;
            value = await waitFor('/cycle.svg', prior);
            state.durations.push(performance.now() - started);
          }
          target.style.backgroundImage = 'url("denied.svg")';
          state.phase = 'denied';
          value = await waitFor('/denied.svg', 0);
          target.style.backgroundImage = 'url("fallback.svg")';
          state.phase = 'fallback';
          await new Promise(resolve => setTimeout(resolve, 20));
          target.style.backgroundImage = 'url("stale.svg")';
          state.phase = 'stale';
          value = await waitFor('/stale.svg', 0);
          const finalPrior = value.counts['/controlled.svg'] || 0;
          target.style.backgroundImage = 'url("controlled.svg?final=1")';
          await waitFor('/controlled.svg', finalPrior);
          await new Promise(resolve => setTimeout(resolve, 40));
          const ordered = [...state.durations].sort((a,b) => a-b);
          const p95 = ordered[Math.ceil(ordered.length * .95) - 1];
          state.diagnostics.push({name:'css-image-cycles', cycles:100, p95});
          if (p95 > 100) throw new Error(`CSS image p95 ${p95}`);
          await registration.unregister();
          state.phase = 'done';
          state.harness = {status:0, message:null, stack:null};
        } catch (error) {
          state.errors.push(String(error && error.stack || error));
          state.harness = {status:1, message:state.errors[0], stack:null};
        } finally { state.complete = true; }
      })();
    )JS", "controlled-css-image-gate.js");
    const auto fixture_completed = evaluate_until_equals(
        engine, "__webSceneWptState.complete", "css-image-complete.js", "true", 15'000);
    require(fixture_completed == "true",
        "VS Code Markdown CSS image fixture did not complete: "
            + evaluate(engine, "JSON.stringify(__webSceneWptState)",
                "css-image-timeout-detail.js"));
    require(evaluate(engine, "__webSceneWptState.harness.status",
            "css-image-status.js") == "0",
        "VS Code Markdown CSS image fixture failed: "
            + evaluate(engine, "JSON.stringify(__webSceneWptState)", "css-image-detail.js"));
    const auto p95 = std::stod(evaluate(engine,
        "__webSceneWptState.diagnostics[0].p95", "css-image-p95.js"));

    webscene_engine_request_scene_checkpoint(engine);
    auto found_current_css_image = false;
    auto found_stale_css_image = false;
    for (auto attempt = 0; attempt < 250; ++attempt) {
        const auto* scene = webscene_engine_acquire_latest_scene(engine);
        if (scene != nullptr) {
            for (uint32_t index = 0; index < scene->header.command_count; ++index) {
                const auto& command = scene->commands[index];
                if (command.kind != 6U || command.flags >= scene->string_count) continue;
                const auto resource = scene->strings[command.flags];
                const std::string_view bytes(
                    scene->string_bytes + resource.byte_offset, resource.byte_length);
                found_current_css_image |= bytes.find("0 0 3 2") != std::string_view::npos;
                found_stale_css_image |= bytes.find("0 0 13 11") != std::string_view::npos;
            }
            webscene_scene_acknowledge(scene);
            webscene_scene_release(scene);
        }
        if (found_current_css_image) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(found_current_css_image,
        "controlled CSS url() SVG did not reach the native scene");
    require(!found_stale_css_image,
        "late controlled CSS url() response replaced the current style");
    require(server.controlled_host_reads.load(std::memory_order_relaxed) == 0,
        "a handled or failed controlled CSS image reached the host loader");
    require(server.fallback_host_reads.load(std::memory_order_relaxed) > 0,
        "an unhandled CSS image did not fall through to the host loader");

    require(webscene_engine_request_low_memory(engine) != 0,
        "VS Code Markdown CSS image low-memory request rejected");
    webscene_engine_memory_metrics memory_after{sizeof(webscene_engine_memory_metrics)};
    for (auto attempt = 0; attempt < 250; ++attempt) {
        require(webscene_engine_get_memory_metrics(engine, &memory_after) != 0,
            "VS Code Markdown CSS image final memory metrics unavailable");
        if (memory_after.low_memory_notifications
            > memory_before.low_memory_notifications) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(memory_after.low_memory_notifications
            > memory_before.low_memory_notifications,
        "VS Code Markdown CSS image low-memory collection did not complete");
    require(memory_after.v8_used_heap_bytes
            <= memory_before.v8_used_heap_bytes + 8U * 1024U * 1024U,
        "VS Code Markdown CSS image cycles retained more than 8 MiB V8 heap");
    const auto peak_rss_after = service_worker_test_peak_rss_bytes();
    require(peak_rss_after <= peak_rss_before + 256U * 1024U * 1024U,
        "VS Code Markdown CSS image exceeded 256 MiB absolute RSS high-water growth");
    webscene_engine_destroy(engine);
    const auto current_rss_after = service_worker_test_current_rss_bytes();
    require(current_rss_after <= current_rss_before + 64U * 1024U * 1024U,
        "VS Code Markdown CSS image retained more than 64 MiB RSS after teardown");
    std::cout << "VS Code Markdown CSS image gate: cycles=100 p95=" << p95
              << "ms heapBefore=" << memory_before.v8_used_heap_bytes
              << " heapAfter=" << memory_after.v8_used_heap_bytes
              << " peakRssBefore=" << peak_rss_before
              << " peakRssAfter=" << peak_rss_after
              << " currentRssBefore=" << current_rss_before
              << " currentRssAfter=" << current_rss_after << '\n';
}

void test_controlled_parser_and_nested_frame_resources()
{
    resource_server server{.content = {
        {"https://parser-resource.test/bootstrap.html",
            "<!doctype html><title>parser controller bootstrap</title>"},
        {"https://parser-resource.test/controlled.html", R"HTML(
            <!doctype html>
            <html><head>
              <link rel="stylesheet" href="/parser.css">
              <link rel="stylesheet" href="/denied.css">
            </head><body id="styled">
              <script src="/parser-message.js"></script>
              <script src="/fallback.js"></script>
              <script src="/denied.js"></script>
              <script src="/malformed.js"></script>
              <script>
                __parserTrace.push('inline');
                globalThis.__parserSemantic = {
                  trace:[...__parserTrace],
                  color:getComputedStyle(document.body).color,
                  deniedStyleBypass:getComputedStyle(document.body).backgroundColor
                    === 'rgb(222, 1, 2)',
                  deniedBypass:globalThis.__deniedParserBypass === true,
                  malformedBypass:globalThis.__malformedParserBypass === true
                };
              </script>
            </body></html>)HTML"},
        {"https://parser-resource.test/frame.html", R"HTML(
            <!doctype html><html><head>
              <link rel="stylesheet" href="/frame.css">
            </head><body>
              <div id="frame-styled">nested</div>
              <script src="/frame-message.js"></script>
              <img id="frame-image" src="/frame.svg">
              <script>
                const image = document.querySelector('img');
                image.addEventListener('load', () => {
                  parent.__nestedParser.imageLoads++;
                  parent.__nestedParser.result = {
                    script:globalThis.__frameParserBody === true,
                    color:getComputedStyle(document.getElementById('frame-styled')).color,
                    width:image.naturalWidth,height:image.naturalHeight
                  };
                  parent.__nestedParser.complete = true;
                });
                image.addEventListener('error', () => {
                  parent.__nestedParser.imageErrors++;
                  parent.__nestedParser.complete = true;
                });
              </script>
            </body></html>)HTML"},
        {"https://parser-resource.test/cycle-frame.html", R"HTML(
            <!doctype html><html><head>
              <link rel="stylesheet" href="/cycle.css">
            </head><body>
              <div id="frame-styled">cycle</div>
              <script src="/cycle.js"></script>
              <img id="cycle-image" src="/cycle.svg">
              <script>
                const image = document.querySelector('img');
                image.addEventListener('load', () => parent.__finishParserCycle({
                  script:globalThis.__cycleParserBody === true,
                  color:getComputedStyle(document.getElementById('frame-styled')).color,
                  width:image.naturalWidth,height:image.naturalHeight
                }));
                image.addEventListener('error', () =>
                  parent.__finishParserCycle({error:'image'}));
              </script>
            </body></html>)HTML"},
        {"https://parser-resource.test/service-worker.js", R"JS(
            const requests = [];
            const pending = new Map();
            let token = 0;
            self.addEventListener('install', event => event.waitUntil(skipWaiting()));
            self.addEventListener('activate', event => event.waitUntil(clients.claim()));
            self.addEventListener('fetch', event => {
              const url = new URL(event.request.url);
              const record = {
                path:url.pathname,
                destination:event.request.destination,
                clientId:event.clientId,
                frameType:null
              };
              requests.push(record);
              if (url.pathname === '/fallback.js') return;
              if (url.pathname === '/denied.js' || url.pathname === '/denied.css') {
                event.respondWith(Promise.reject(new Error('parser resource denied')));
                return;
              }
              if (url.pathname === '/malformed.js') {
                event.respondWith(Promise.resolve({status:200}));
                return;
              }
              if (url.pathname === '/pending-frame.js') {
                event.respondWith((async () => {
                  const client = await clients.get(event.clientId);
                  if (client) client.postMessage({kind:'remove-pending-frame'});
                  return new Promise(() => {});
                })());
                return;
              }
              if (url.pathname === '/frame.svg' || url.pathname === '/cycle.svg') {
                event.respondWith(new Response(
                  '<svg xmlns="http://www.w3.org/2000/svg" width="7" height="5" viewBox="0 0 7 5"><rect width="7" height="5"/></svg>',
                  {headers:{'content-type':'image/svg+xml'}}));
                return;
              }
              if (url.pathname === '/parser.css'
                  || url.pathname === '/frame.css'
                  || url.pathname === '/cycle.css'
                  || url.pathname === '/parser-message.js'
                  || url.pathname === '/frame-message.js'
                  || url.pathname === '/cycle.js') {
                const currentToken = `${url.pathname}:${++token}`;
                event.respondWith((async () => {
                  const client = await clients.get(event.clientId);
                  if (!client) throw new Error('parser client retired before byte request');
                  record.frameType = client.frameType;
                  const response = new Promise((resolve, reject) => pending.set(
                    currentToken, {resolve, reject, clientId:event.clientId}));
                  client.postMessage({kind:'parser-byte-request',
                    token:currentToken, path:url.pathname});
                  return response;
                })());
              }
            });
            self.addEventListener('message', event => {
              const data = event.data || {};
              if (data.kind === 'parser-byte-response') {
                const entry = pending.get(data.token);
                if (!entry || entry.clientId !== event.source.id) return;
                pending.delete(data.token);
                entry.resolve(new Response(data.body,
                  {headers:{'content-type':data.contentType}}));
                return;
              }
              if (data === 'report') event.waitUntil((async () => {
                const activeClients = await clients.matchAll();
                event.source.postMessage({requests, pending:pending.size,
                  clients:activeClients.map(client => ({id:client.id,
                    frameType:client.frameType}))});
              })());
            });
        )JS"},
        {"https://parser-resource.test/parser.css",
            "#styled{color:rgb(255, 0, 0)}"},
        {"https://parser-resource.test/denied.css",
            "#styled{background-color:rgb(222, 1, 2)}"},
        {"https://parser-resource.test/parser-message.js",
            "globalThis.__parserHostBypass=true;"},
        {"https://parser-resource.test/fallback.js",
            "globalThis.__parserTrace=(globalThis.__parserTrace||[]);"
            "__parserTrace.push('fallback');"},
        {"https://parser-resource.test/denied.js",
            "globalThis.__deniedParserBypass=true;"},
        {"https://parser-resource.test/malformed.js",
            "globalThis.__malformedParserBypass=true;"},
        {"https://parser-resource.test/pending-frame.js",
            "parent.__pendingParserHostBypass=true;"}
    }};
    webscene_engine_options options{};
    options.struct_size = sizeof(options);
    options.resource_load_callback = load_test_resource;
    options.resource_load_user_data = &server;
    auto* engine = webscene_engine_create_with_options(&options);
    require(engine != nullptr, "parser resource engine creation failed");

    constexpr std::string_view bootstrap{
        "https://parser-resource.test/bootstrap.html"};
    require(webscene_engine_load_url(
            engine, bootstrap.data(), bootstrap.size()) != 0U,
        "parser controller bootstrap navigation failed");
    execute(engine, R"JS(
      globalThis.__parserRegistration = {complete:false,error:null};
      (async () => {
        try {
          const changed = new Promise(resolve => navigator.serviceWorker.addEventListener(
            'controllerchange', resolve, {once:true}));
          await navigator.serviceWorker.register('/service-worker.js', {type:'module'});
          if (!navigator.serviceWorker.controller) await changed;
        } catch (error) {
          __parserRegistration.error = String(error && error.stack || error);
        } finally { __parserRegistration.complete = true; }
      })();
    )JS", "parser-register.js");
    require(evaluate_until_equals(engine, "__parserRegistration.complete",
            "parser-register-complete.js", "true", 2'500) == "true",
        "parser controller registration did not complete");
    require(evaluate(engine, "__parserRegistration.error",
            "parser-register-error.js") == "null",
        "parser controller registration failed");

    constexpr std::string_view document_start = R"JS(
      globalThis.__parserTrace = [];
      navigator.serviceWorker.addEventListener('message', event => {
        const data = event.data || {};
        if (data.kind === 'remove-pending-frame') {
          if (parent !== window && typeof parent.__removePendingParserFrame === 'function') {
            parent.__removePendingParserFrame();
          }
          return;
        }
        if (data.kind !== 'parser-byte-request') {
          globalThis.__parserWorkerReport = data;
          return;
        }
        let source;
        let contentType = 'text/javascript';
        if (data.path === '/parser.css') {
          source = '#styled{color:rgb(12, 34, 56)}';
          contentType = 'text/css';
        } else if (data.path === '/frame.css' || data.path === '/cycle.css') {
          source = '#frame-styled{color:rgb(44, 55, 66)}';
          contentType = 'text/css';
        } else if (data.path === '/parser-message.js') {
          source = "__parserTrace.push('controlled');globalThis.__parserBody=true;";
        } else if (data.path === '/frame-message.js') {
          source = "globalThis.__frameParserBody=true;";
        } else {
          source = "globalThis.__cycleParserBody=true;";
        }
        if (source.length < 65536) source += ' '.repeat(65536 - source.length);
        navigator.serviceWorker.controller.postMessage({
          kind:'parser-byte-response', token:data.token, body:source, contentType
        });
      });
    )JS";
    constexpr std::string_view document_start_name =
        "parser-client-byte-provider.js";
    const webscene_document_script script{
        sizeof(webscene_document_script),
        WEBSCENE_DOCUMENT_SCRIPT_ALL_FRAMES,
        document_start.data(), document_start.size(),
        document_start_name.data(), document_start_name.size()};
    const webscene_navigation_options navigation{
        sizeof(webscene_navigation_options), 1U, &script};
    constexpr std::string_view controlled{
        "https://parser-resource.test/controlled.html"};
    const auto parser_started = std::chrono::steady_clock::now();
    require(webscene_engine_load_url_with_options(
            engine, controlled.data(), controlled.size(), &navigation) != 0U,
        "controlled parser navigation failed: " + last_error(engine));
    const auto parser_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - parser_started).count();
    require(parser_ms <= 500.0,
        "controlled parser navigation exceeded 500 ms");
    require(evaluate(engine, R"JS((() => {
      const value = __parserSemantic;
      return value.trace.join(',') === 'controlled,fallback,inline'
        && value.color === 'rgb(12, 34, 56)'
        && value.deniedStyleBypass === false
        && value.deniedBypass === false
        && value.malformedBypass === false
        && __parserBody === true
        && globalThis.__parserHostBypass !== true;
    })())JS", "parser-semantic.js") == "true",
        "parser script/style ordering or fail-closed behavior diverged: "
            + evaluate(engine, "JSON.stringify(__parserSemantic)",
                "parser-semantic-detail.js"));
    execute(engine, R"JS(
      globalThis.__nestedParser = {complete:false,error:null,imageLoads:0,imageErrors:0};
      (() => {
        try {
          const frame = document.createElement('iframe');
          frame.src = '/frame.html';
          document.body.append(frame);
          __nestedParser.frame = frame;
        } catch (error) {
          __nestedParser.error = String(error && error.stack || error);
          __nestedParser.complete = true;
        }
      })();
    )JS", "nested-parser-setup.js");
    require(evaluate_until_equals(engine, "__nestedParser.complete",
            "nested-parser-complete.js", "true", 2'500) == "true",
        "nested parser resources did not complete: "
            + evaluate(engine, "JSON.stringify(__nestedParser)",
                "nested-parser-timeout-detail.js")
            + " / tags=" + evaluate(engine, R"JS(
                [...__nestedParser.frame.contentDocument.querySelectorAll('*')]
                  .map(node => `${node.tagName}:${node.id}`).join(',')
              )JS", "nested-parser-timeout-tags.js")
            + " / " + last_error(engine));
    require(evaluate(engine, R"JS((() => {
      const value = __nestedParser;
      return value.error === null && value.imageLoads === 1 && value.imageErrors === 0
        && value.result.script && value.result.color === 'rgb(44, 55, 66)'
        && value.result.width === 7 && value.result.height === 5;
    })())JS", "nested-parser-result.js") == "true",
        "nested parser script/style/image result diverged: "
            + evaluate(engine, "JSON.stringify(__nestedParser)",
                "nested-parser-detail.js"));
    execute(engine, "__nestedParser.frame.remove();",
        "nested-parser-remove.js");

    execute(engine, R"JS(
      globalThis.__pendingParserRetirement = {complete:false,duration:0};
      (() => {
        const frame = document.createElement('iframe');
        document.body.append(frame);
        globalThis.__removePendingParserFrame = () => frame.remove();
        const started = performance.now();
        const frameDocument = frame.contentDocument;
        frameDocument.open();
        frameDocument.write('<!doctype html><script src="/pending-frame.js"><\/script>');
        frameDocument.close();
        __pendingParserRetirement.duration = performance.now() - started;
        __pendingParserRetirement.complete = true;
      })();
    )JS", "pending-parser-retirement.js");
    require(evaluate(engine, "__pendingParserRetirement.complete",
            "pending-parser-retirement-complete.js") == "true",
        "retired pending parser request did not return");
    require(std::stod(evaluate(engine, "__pendingParserRetirement.duration",
            "pending-parser-retirement-duration.js")) <= 500.0,
        "retiring a pending parser request exceeded 500 ms");
    require(evaluate(engine, "globalThis.__pendingParserHostBypass === true",
            "pending-parser-host-bypass.js") == "false",
        "retired handled parser request reached host fallback");
    webscene_engine_memory_metrics memory_before{
        sizeof(webscene_engine_memory_metrics)};
    require(webscene_engine_get_memory_metrics(engine, &memory_before) != 0,
        "parser resource memory baseline unavailable");
    const auto rss_before = service_worker_test_peak_rss_bytes();
    execute(engine, R"JS(
      globalThis.__parserCycles = {complete:false,error:null,durations:[],count:0};
      (async () => {
        try {
          for (let cycle = 0; cycle < 100; ++cycle) {
            const started = performance.now();
            await new Promise((resolve, reject) => {
              const frame = document.createElement('iframe');
              const timeout = setTimeout(() => reject(new Error(`cycle ${cycle} timed out`)), 1000);
              globalThis.__finishParserCycle = value => {
                clearTimeout(timeout);
                frame.remove();
                if (!value.script || value.color !== 'rgb(44, 55, 66)'
                    || value.width !== 7 || value.height !== 5) {
                  reject(new Error(`cycle ${cycle} mismatch ${JSON.stringify(value)}`));
                } else resolve();
              };
              frame.src = '/cycle-frame.html';
              document.body.append(frame);
            });
            __parserCycles.durations.push(performance.now() - started);
            ++__parserCycles.count;
            // Let the retired frame and its controlled fetches reach the next
            // task boundary before installing another browsing context. A
            // single uninterrupted microtask chain can consume the host's
            // bounded evaluation callback window on slower platforms.
            await new Promise(resolve => setTimeout(resolve, 0));
          }
        } catch (error) {
          __parserCycles.error = String(error && error.stack || error);
        } finally { __parserCycles.complete = true; }
      })();
    )JS", "parser-cycle-gate.js");
    require(evaluate_until_equals(engine, "__parserCycles.complete",
            "parser-cycle-complete.js", "true", 30'000) == "true",
        "100 parser/frame hydrate-remove cycles did not complete");
    require(evaluate(engine, "__parserCycles.error",
            "parser-cycle-error.js") == "null",
        "parser/frame cycle failed: "
            + evaluate(engine, "__parserCycles.error", "parser-cycle-error-detail.js"));
    require(evaluate(engine, "__parserCycles.count",
            "parser-cycle-count.js") == "100",
        "parser/frame cycle count diverged");
    const auto p95 = std::stod(evaluate(engine, R"JS((() => {
      const values = [...__parserCycles.durations].sort((a,b) => a-b);
      return values[Math.ceil(values.length * .95) - 1];
    })())JS", "parser-cycle-p95.js"));
    require(p95 <= 100.0,
        "64 KiB intercepted parser resource p95 exceeded 100 ms: "
            + std::to_string(p95));

    execute(engine, R"JS(
      navigator.serviceWorker.controller.postMessage('report');
    )JS", "parser-worker-report-request.js");
    require(evaluate_until_equals(engine,
            "globalThis.__parserWorkerReport !== undefined",
            "parser-worker-report-ready.js", "true", 2'500) == "true",
        "parser worker report did not arrive");
    require(evaluate(engine, R"JS((() => {
      const report = __parserWorkerReport;
      const top = report.requests.find(value => value.path === '/parser-message.js');
      const nested = report.requests.find(value => value.path === '/frame-message.js');
      const cycleIds = new Set(report.requests
        .filter(value => value.path === '/cycle.js').map(value => value.clientId));
      const destinations = report.requests.every(value => {
        if (value.path.endsWith('.js')) return value.destination === 'script';
        if (value.path.endsWith('.css')) return value.destination === 'style';
        if (value.path.endsWith('.svg')) return value.destination === 'image';
        return true;
      });
      return report.pending === 0 && report.clients.length === 1
        && top.frameType === 'top-level' && nested.frameType === 'nested'
        && top.clientId !== nested.clientId
        && cycleIds.size === 100 && destinations;
    })())JS", "parser-worker-report.js") == "true",
        "parser request destination/client/lifecycle report diverged: "
            + evaluate(engine, "JSON.stringify(__parserWorkerReport)",
                "parser-worker-report-detail.js"));

    require(webscene_engine_request_low_memory(engine) != 0,
        "parser resource low-memory request rejected");
    webscene_engine_memory_metrics memory_after{
        sizeof(webscene_engine_memory_metrics)};
    for (auto attempt = 0; attempt < 250; ++attempt) {
        require(webscene_engine_get_memory_metrics(engine, &memory_after) != 0,
            "parser resource final memory metrics unavailable");
        if (memory_after.low_memory_notifications
            > memory_before.low_memory_notifications) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(memory_after.low_memory_notifications
            > memory_before.low_memory_notifications,
        "parser resource low-memory collection did not complete");
    require(memory_after.v8_used_heap_bytes
            <= memory_before.v8_used_heap_bytes + 8U * 1024U * 1024U,
        "parser/frame cycles retained more than 8 MiB V8 heap");
    const auto rss_after = service_worker_test_peak_rss_bytes();
    require(rss_after <= rss_before + 64U * 1024U * 1024U,
        "parser/frame cycles exceeded 64 MiB RSS growth");
    std::cout << "Parser/frame resource gate: cycles=100 p95=" << p95
              << "ms initial=" << parser_ms
              << "ms heapBefore=" << memory_before.v8_used_heap_bytes
              << " heapAfter=" << memory_after.v8_used_heap_bytes
              << " rssBefore=" << rss_before << " rssAfter=" << rss_after << '\n';
    webscene_engine_destroy(engine);
}
