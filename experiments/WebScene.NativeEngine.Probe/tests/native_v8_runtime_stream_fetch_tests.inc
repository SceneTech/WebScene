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
