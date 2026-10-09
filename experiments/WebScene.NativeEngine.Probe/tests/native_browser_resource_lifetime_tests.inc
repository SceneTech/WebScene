void collect_browser_resource_payloads(webscene_engine* engine)
{
    webscene_engine_memory_metrics before{};
    before.struct_size = sizeof(before);
    require(webscene_engine_get_memory_metrics(engine, &before) != 0, "memory census unavailable");
    require(webscene_engine_request_low_memory(engine) != 0, "collection request rejected");
    for (int attempt = 0; attempt < 1000; ++attempt) {
        webscene_engine_memory_metrics after{};
        after.struct_size = sizeof(after);
        webscene_engine_get_memory_metrics(engine, &after);
        if (after.low_memory_notifications > before.low_memory_notifications) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    fail("browser resource collection did not finish");
}

void test_browser_resource_lifetime()
{
    auto* engine = webscene_engine_create(0);
    require(engine != nullptr, "browser resource engine creation failed");
    resize(engine, 800, 600, 1);
    wait_for_consumed_inputs(engine, 1, "browser resource viewport not applied");
    for (int round = 0; round < 3; ++round) {
        evaluate(engine, R"JS(
          globalThis.resourceRefs = [];
          for (const kind of ['disconnect', 'unobserve', 'media', 'media-remove', 'onchange', 'listener']) {
            for (let i = 0; i < 128; i++) {
              const payload = { data: new Array(4096).fill(i) };
              resourceRefs.push(new WeakRef(payload));
              const callback = () => payload.data.length;
              if (kind === 'listener') {
                const target = document.createElement('div');
                payload.target = target;
                document.body.appendChild(target);
                target.addEventListener('click', callback);
                target.remove();
              } else if (kind.startsWith('media') || kind === 'onchange') {
                const query = matchMedia('(min-width: 1px)');
                query.payload = payload;
                if (kind === 'media-remove') { query.addListener(callback); query.removeListener(callback); }
                if (kind === 'onchange') { query.onchange = callback; query.onchange = null; }
              } else {
                const observer = new ResizeObserver(callback);
                observer.observe(document.body);
                if (kind === 'disconnect') observer.disconnect();
                else observer.unobserve(document.body);
              }
            }
          }
          true
        )JS", "resource-payload-allocation.js");
        collect_browser_resource_payloads(engine);
        collect_browser_resource_payloads(engine);
        require(evaluate(engine, "resourceRefs.filter(ref => ref.deref()).length", "resource-payload-count.js") == "0",
            "dropped browser resources retained callback payloads");
    }
    // A reachable detached target must preserve its callbacks through GC and
    // reattachment. Removal/once/abort must then release callbacks on live targets.
    for (const auto route : {"remove", "once", "abort"}) {
        evaluate(engine, std::string("globalThis.retirementRoute = '") + route + "'", "listener-retirement-route.js");
        evaluate(engine, R"JS(
          globalThis.listenerTarget = document.createElement('div');
          document.body.appendChild(listenerTarget);
          globalThis.listenerCalls = 0;
          globalThis.listenerRefs = [];
          globalThis.listenerAbort = new AbortController();
          for (let i = 0; i < 128; i++) {
            const payload = new Array(4096).fill(i);
            const callback = () => { listenerCalls++; return payload.length; };
            listenerRefs.push(new WeakRef(callback));
            listenerTarget.addEventListener('probe', callback, {
              once: retirementRoute === 'once',
              signal: retirementRoute === 'abort' ? listenerAbort.signal : undefined
            });
          }
          listenerTarget.remove(); true
        )JS", "reachable-detached-listeners.js");
        collect_browser_resource_payloads(engine);
        require(evaluate(engine, "listenerRefs.filter(ref => ref.deref()).length", "reachable-detached-count.js") == "128",
            "reachable detached target lost its registered callbacks");
        evaluate(engine, R"JS(
          document.body.appendChild(listenerTarget);
          listenerTarget.dispatchEvent(new Event('probe'));
          if (retirementRoute === 'remove') for (const ref of listenerRefs) listenerTarget.removeEventListener('probe', ref.deref());
          if (retirementRoute === 'abort') listenerAbort.abort();
          true
        )JS", "retire-connected-listeners.js");
        require(evaluate(engine, "listenerCalls", "retired-listener-delivery.js") == "128",
            "reattached listeners failed to deliver");
        collect_browser_resource_payloads(engine);
        require(evaluate(engine, "listenerRefs.filter(ref => ref.deref()).length", "retired-listener-count.js") == "0",
            "removed/once/aborted listeners retained callbacks on a live target");
    }
    // Active observations/listeners must survive GC without a user reference.
    evaluate(engine, R"JS(
      globalThis.deliveries = 0;
      globalThis.activeRefs = [];
      (() => {
        const observer = new ResizeObserver((entries, owner) => {
          if (entries[0].target !== document.body || owner !== observer) throw Error('observer identity');
          deliveries++;
        });
        observer.observe(document.body);
        activeRefs.push(new WeakRef(observer));
        const query = matchMedia('(max-width: 500px)');
        query.addEventListener('change', () => deliveries++);
        activeRefs.push(new WeakRef(query));
        const onchange = matchMedia('(max-width: 500px)');
        onchange.onchange = () => deliveries++;
        activeRefs.push(new WeakRef(onchange));
      })(); true
    )JS", "active-resource-ownership.js");
    collect_browser_resource_payloads(engine);
    require(evaluate(engine, "activeRefs.filter(ref => ref.deref()).length", "active-resource-count.js") == "3",
        "active resources collected before unregistration");
    evaluate(engine, "deliveries = 0", "clear-initial-delivery.js");
    resize(engine, 400, 600, 2);
    wait_for_consumed_inputs(engine, 2, "resource viewport change not applied");
    require(evaluate(engine, "deliveries >= 3", "active-resource-delivery.js") == "true",
        "active media query listeners did not survive GC");
    // Keep an extracted method across GC, then disconnect and reuse its owner.
    evaluate(engine, R"JS(
      globalThis.reusable = new ResizeObserver(() => {});
      reusable.observe(document.body); reusable.disconnect();
      globalThis.extractedObserve = reusable.observe;
      globalThis.reusableRef = new WeakRef(reusable);
      reusable = null; true
    )JS", "resource-extracted-method.js");
    collect_browser_resource_payloads(engine);
    require(evaluate(engine, "!!reusableRef.deref()", "extracted-owner-count.js") == "true",
        "extracted method lost its native owner");
    evaluate(engine, "extractedObserve(document.body); reusableRef.deref().disconnect(); extractedObserve = null; true",
        "resource-reobserve.js");
    collect_browser_resource_payloads(engine);
    require(evaluate(engine, "!!reusableRef.deref()", "released-owner-count.js") == "false",
        "disconnected observer remained rooted after its method was released");
    // Removing a later listener in a callback must not invalidate dispatch indexes.
    evaluate(engine, R"JS(
      globalThis.reentrant = [];
      globalThis.query = matchMedia('(min-width: 500px)');
      const second = () => reentrant.push('removed');
      query.addEventListener('change', () => { reentrant.push('first'); query.removeListener(second); }, {once:true});
      query.addListener(second);
      query.addListener(() => reentrant.push('last'));
      true
    )JS", "media-removal-during-dispatch.js");
    resize(engine, 800, 600, 3);
    wait_for_consumed_inputs(engine, 3, "reentrant viewport not applied");
    require(evaluate(engine, "reentrant", "media-removal-result.js") == R"(["first","last"])",
        "media listener removal invalidated delivery order");
    // Active registrations in a discarded iframe must not retain its realm.
    evaluate(engine, R"JS(
      globalThis.framePayloadRefs = [];
      globalThis.resourceFrame = document.createElement('iframe');
      document.body.appendChild(resourceFrame);
      resourceFrame.contentDocument.open();
      resourceFrame.contentDocument.write('<html><body><script>globalThis.resourceReady=true;<\/script></body></html>');
      resourceFrame.contentDocument.close();
      true
    )JS", "resource-frame-hydration.js");
    auto frame_ready = false;
    for (int attempt = 0; attempt < 500; ++attempt) {
        if (evaluate(engine, "resourceFrame.contentWindow.resourceReady === true", "resource-frame-ready.js") == "true") {
            frame_ready = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(frame_ready, "resource frame did not hydrate");
    evaluate(engine, R"JS(
      (() => {
        const scope = resourceFrame.contentWindow;
        for (let i = 0; i < 32; i++) {
          const payload = {values: new Array(4096).fill(i)};
          framePayloadRefs.push(new WeakRef(payload));
          const observer = new scope.ResizeObserver(() => payload.values.length);
          observer.observe(scope.document.body);
          const query = scope.matchMedia('(max-width:500px)');
          query.addListener(() => {
            if (i === 0) { resourceFrame.remove(); resourceFrame = null; }
            return payload.values.length;
          });
        }
      })(); true
    )JS", "discarded-frame-resource-ownership.js");
    resize(engine, 400, 600, 4);
    wait_for_consumed_inputs(engine, 4, "discarded frame callback not delivered");
    require(evaluate(engine, "resourceFrame === null", "discarded-frame-removed.js") == "true",
        "frame was not removed by its media callback");
    collect_browser_resource_payloads(engine);
    collect_browser_resource_payloads(engine);
    require(evaluate(engine, "framePayloadRefs.filter(ref => ref.deref()).length", "discarded-frame-resource-count.js") == "0",
        "discarded iframe retained active browser resource payloads");
    webscene_engine_destroy(engine);
}
