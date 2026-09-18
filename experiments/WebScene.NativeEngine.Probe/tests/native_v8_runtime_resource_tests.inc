struct resource_server final {
    std::unordered_map<std::string, std::string> content;
    std::atomic<int> active{0};
    std::atomic<int> peak{0};
    std::atomic<int> active_documents{0};
    std::atomic<int> peak_documents{0};
    std::atomic<int> requests{0};
    std::atomic<bool> block_named_resource{false};
    std::atomic<bool> release_blocked_resource{false};
    std::chrono::steady_clock::time_point observation_start{};
    std::atomic<int64_t> first_subresource_start_ms{
        std::numeric_limits<int64_t>::max()};
};

constexpr auto iframe_outer_script_work_ms = 300;

size_t load_test_resource(
    void* user_data,
    uint32_t kind,
    const char* url,
    size_t url_length,
    const char*,
    size_t,
    int64_t,
    char* destination,
    size_t destination_capacity)
{
    auto& server = *static_cast<resource_server*>(user_data);
    const auto address = std::string(url, url_length);
    const auto known = server.content.find(address);
    if (known == server.content.end()) return 0U;

    constexpr size_t header_size = 2U + sizeof(uint32_t) + sizeof(int64_t) * 2U;
    const auto required = header_size + known->second.size();
    // The runtime may now supply speculative capacity on the first call. An
    // exact-size destination identifies the retry after a short probe.
    const auto first_transfer = destination == nullptr
        || destination_capacity != required;
    if (first_transfer && !address.ends_with("index.html")) {
        if (kind != WEBSCENE_RESOURCE_DOCUMENT
            && server.observation_start != std::chrono::steady_clock::time_point{}) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - server.observation_start).count();
            auto earliest = server.first_subresource_start_ms.load(
                std::memory_order_relaxed);
            while (elapsed < earliest
                && !server.first_subresource_start_ms.compare_exchange_weak(
                    earliest,
                    elapsed,
                    std::memory_order_relaxed)) {
            }
        }
        server.requests.fetch_add(1, std::memory_order_relaxed);
        const auto active = server.active.fetch_add(1, std::memory_order_relaxed) + 1;
        auto peak = server.peak.load(std::memory_order_relaxed);
        while (active > peak
            && !server.peak.compare_exchange_weak(
                peak,
                active,
                std::memory_order_relaxed)) {
        }
        if (kind == WEBSCENE_RESOURCE_DOCUMENT) {
            const auto active_documents =
                server.active_documents.fetch_add(1, std::memory_order_relaxed) + 1;
            auto peak_documents = server.peak_documents.load(std::memory_order_relaxed);
            while (active_documents > peak_documents
                && !server.peak_documents.compare_exchange_weak(
                    peak_documents,
                    active_documents,
                    std::memory_order_relaxed)) {
            }
        }
        if (address.find("blocked-") != std::string::npos
            && server.block_named_resource.load(std::memory_order_acquire)) {
            while (!server.release_blocked_resource.load(std::memory_order_acquire))
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(60));
        }
        if (kind == WEBSCENE_RESOURCE_DOCUMENT) {
            server.active_documents.fetch_sub(1, std::memory_order_relaxed);
        }
        server.active.fetch_sub(1, std::memory_order_relaxed);
    }
    else if (first_transfer) {
        server.requests.fetch_add(1, std::memory_order_relaxed);
    }

    if (destination == nullptr || destination_capacity < required) return required;
    destination[0] = 1;
    destination[1] = 1;
    const uint32_t entity_tag_length = 0U;
    const int64_t last_modified = 0;
    const auto fresh_until = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count() + 3600;
    std::memcpy(destination + 2U, &entity_tag_length, sizeof(entity_tag_length));
    std::memcpy(
        destination + 2U + sizeof(entity_tag_length),
        &last_modified,
        sizeof(last_modified));
    std::memcpy(
        destination + 2U + sizeof(entity_tag_length) + sizeof(last_modified),
        &fresh_until,
        sizeof(fresh_until));
    std::copy(known->second.begin(), known->second.end(), destination + header_size);
    return required;
}

struct contextual_resource_server final {
    resource_server resources;
    std::mutex request_mutex;
    std::vector<std::string> requested_urls;
    std::string fetched_url;
    std::string origin;
    std::string referrer;
    uint32_t initiator{0U};
    uint32_t mode{0U};
    std::string method;
    std::string body;
    std::string content_type;
};

void test_same_origin_iframe_navigation_document_replacement()
{
    constexpr std::string_view address{
        "https://iframe-navigation.test/index.html"};
    resource_server server{.content = {
        {std::string{address}, "<!doctype html><title>frame navigation</title><body></body>"},
        {"https://iframe-navigation.test/fake.html?id=markdown-preview",
            "<!doctype html><title>Fake</title><body></body>"},
        {"https://cross-origin.test/fake.html",
            "<!doctype html><title>Cross origin</title><body>"
            "<script>parent.__crossOriginEscape=true;</script></body>"}
    }};
    webscene_engine_options options{};
    options.struct_size = sizeof(options);
    options.resource_load_callback = load_test_resource;
    options.resource_load_user_data = &server;
    auto* engine = webscene_engine_create_with_options(&options);
    require(engine != nullptr, "iframe navigation engine creation failed");
    require(webscene_engine_load_url(engine, address.data(), address.size()) != 0U,
        "iframe navigation outer document failed");
    require(evaluate_until_equals(engine, "location.href", "iframe-navigation-ready.js",
            R"("https://iframe-navigation.test/index.html")", 2'500)
            == R"("https://iframe-navigation.test/index.html")",
        "iframe navigation outer document did not become ready");

    execute(engine, R"JS(
      globalThis.__iframeNavigation = {
        complete:false, error:null, lifecycle:0, stable:false,
        target:false, fakePath:false, replaced:false,
        markdownAdvanced:false, replacementStable:false
      };
      (() => {
        try {
          const frame = document.createElement('iframe');
          frame.setAttribute('sandbox', 'allow-same-origin allow-scripts');
          frame.src = './fake.html?id=markdown-preview';
          document.body.appendChild(frame);
          const initialWindow = frame.contentWindow;
          let handled = false;
          initialWindow.addEventListener('DOMContentLoaded', event => {
            if (handled) return;
            handled = true;
            ++__iframeNavigation.lifecycle;
            try {
              const contentDocument = event.target;
              __iframeNavigation.stable = frame.contentWindow === initialWindow;
              __iframeNavigation.target = contentDocument === frame.contentDocument;
              __iframeNavigation.fakePath = contentDocument.location.pathname
                .endsWith('/fake.html');
              globalThis.__finishIframeMarkdown = marker => {
                __iframeNavigation.markdownAdvanced = marker?.textContent === 'Markdown';
                __iframeNavigation.replacementStable = frame.contentWindow === initialWindow;
                delete globalThis.__finishIframeMarkdown;
                frame.remove();
                __iframeNavigation.complete = true;
              };
              setTimeout(() => {
                try {
                  contentDocument.open();
                  contentDocument.write(
                    "<!doctype html><html><body><main id='markdown-body'>Markdown</main>"
                    + "<script>parent.__finishIframeMarkdown(document.querySelector('#markdown-body'));</script>"
                    + "</body></html>");
                  contentDocument.close();
                  __iframeNavigation.replaced = frame.contentDocument
                    .querySelector('#markdown-body')?.textContent === 'Markdown';
                } catch (error) {
                  __iframeNavigation.error = String(error && error.stack || error);
                  frame.remove();
                  __iframeNavigation.complete = true;
                }
              }, 0);
            } catch (error) {
              __iframeNavigation.error = String(error && error.stack || error);
              __iframeNavigation.complete = true;
            }
          });
        } catch (error) {
          __iframeNavigation.error = String(error && error.stack || error);
          __iframeNavigation.complete = true;
        }
      })();
    )JS", "iframe-navigation-markdown-sequence.js");
    require(evaluate_until_equals(engine, "__iframeNavigation.complete",
            "iframe-navigation-complete.js", "true", 3'000) == "true",
        "pre-navigation DOMContentLoaded listener did not observe the navigated frame: "
            + evaluate(engine, "JSON.stringify(__iframeNavigation)",
                "iframe-navigation-timeout-detail.js")
            + " / " + last_error(engine));
    require(evaluate(engine, R"JS((() => {
      const s=__iframeNavigation;
      return s.error===null && s.lifecycle===1 && s.stable && s.target
        && s.fakePath && s.replaced && s.markdownAdvanced && s.replacementStable;
    })())JS", "iframe-navigation-result.js") == "true",
        "same-origin iframe navigation/document replacement diverged: "
            + evaluate(engine, "JSON.stringify(__iframeNavigation)",
                "iframe-navigation-result-detail.js"));

    webscene_engine_memory_metrics memory_before{sizeof(memory_before)};
    require(webscene_engine_get_memory_metrics(engine, &memory_before) != 0,
        "iframe navigation memory baseline was unavailable");
    const auto rss_before = service_worker_test_current_rss_bytes();
    execute(engine, R"JS(
      globalThis.__iframeLifecycleGate = {
        complete:false, error:null, cycles:0, maximumLiveFrames:0,
        p95:0, opaqueDenied:false, crossOriginDenied:false
      };
      (async () => {
        try {
          const denied = document.createElement('iframe');
          denied.setAttribute('sandbox', 'allow-scripts');
          denied.src = './fake.html?id=markdown-preview';
          document.body.appendChild(denied);
          __iframeLifecycleGate.opaqueDenied =
            denied.contentWindow === null && denied.contentDocument === null;
          denied.remove();

          const crossOrigin = document.createElement('iframe');
          globalThis.__crossOriginEscape = false;
          crossOrigin.setAttribute('sandbox', 'allow-same-origin allow-scripts');
          crossOrigin.src = 'https://cross-origin.test/fake.html';
          document.body.appendChild(crossOrigin);
          await new Promise(resolve => setTimeout(resolve, 150));
          __iframeLifecycleGate.crossOriginDenied =
            crossOrigin.contentWindow === null && crossOrigin.contentDocument === null
              && globalThis.__crossOriginEscape === false;
          crossOrigin.remove();

          for (let cycle = 0; cycle < 100; ++cycle) {
            await new Promise((resolve, reject) => {
              const frame = document.createElement('iframe');
              frame.setAttribute('sandbox', 'allow-same-origin allow-scripts');
              frame.src = './fake.html?id=markdown-preview';
              document.body.appendChild(frame);
              const proxy = frame.contentWindow;
              const timeout = setTimeout(
                () => reject(new Error(`cycle ${cycle} timed out`)), 1000);
              proxy.addEventListener('DOMContentLoaded', event => {
                try {
                  const contentDocument = event.target;
                  contentDocument.open();
                  contentDocument.write(
                    `<!doctype html><body><main data-cycle="${cycle}">${cycle}</main></body>`);
                  contentDocument.close();
                  if (frame.contentWindow !== proxy
                      || frame.contentDocument !== contentDocument
                      || contentDocument.querySelector('main')?.textContent !== String(cycle)) {
                    throw new Error(`cycle ${cycle} identity/replacement mismatch`);
                  }
                  ++__iframeLifecycleGate.cycles;
                  __iframeLifecycleGate.maximumLiveFrames = Math.max(
                    __iframeLifecycleGate.maximumLiveFrames,
                    document.querySelectorAll('iframe').length);
                  clearTimeout(timeout);
                  frame.remove();
                  setTimeout(resolve, 0);
                } catch (error) { clearTimeout(timeout); reject(error); }
              });
            });
          }

          await new Promise((resolve, reject) => {
            const frame = document.createElement('iframe');
            frame.setAttribute('sandbox', 'allow-same-origin allow-scripts');
            frame.src = './fake.html?id=markdown-preview';
            document.body.appendChild(frame);
            const proxy = frame.contentWindow;
            const timeout = setTimeout(() => reject(new Error('bulk frame timed out')), 1000);
            proxy.addEventListener('DOMContentLoaded', event => {
              try {
                const contentDocument = event.target;
                const payload = 'x'.repeat(1024 * 1024);
                const durations = [];
                for (let iteration = 0; iteration < 10; ++iteration) {
                  const started = performance.now();
                  contentDocument.open();
                  contentDocument.write(
                    `<!doctype html><body><main id="bulk">${payload}</main></body>`);
                  contentDocument.close();
                  if (contentDocument.querySelector('#bulk')?.textContent.length
                      !== payload.length) {
                    throw new Error(`bulk replacement ${iteration} was truncated`);
                  }
                  durations.push(performance.now() - started);
                }
                durations.sort((a, b) => a - b);
                __iframeLifecycleGate.p95 = durations[Math.ceil(durations.length * .95) - 1];
                clearTimeout(timeout);
                frame.remove();
                setTimeout(resolve, 0);
              } catch (error) { clearTimeout(timeout); reject(error); }
            });
          });
        } catch (error) {
          __iframeLifecycleGate.error = String(error && error.stack || error);
        } finally { __iframeLifecycleGate.complete = true; }
      })();
    )JS", "iframe-navigation-lifecycle-gate.js");
    require(evaluate_until_equals(engine, "__iframeLifecycleGate.complete",
            "iframe-navigation-lifecycle-gate-complete.js", "true", 20'000) == "true",
        "iframe navigation lifecycle/performance gate timed out");
    require(evaluate(engine, R"JS((() => {
      const s=__iframeLifecycleGate;
      return s.error===null && s.cycles===100 && s.maximumLiveFrames===1
        && s.opaqueDenied && s.crossOriginDenied && s.p95 <= 250;
    })())JS", "iframe-navigation-lifecycle-gate-result.js") == "true",
        "iframe navigation security/lifecycle/performance gate diverged: "
            + evaluate(engine, "JSON.stringify(__iframeLifecycleGate)",
                "iframe-navigation-lifecycle-gate-detail.js"));
    require(event_listener_inventory(engine).find(R"("targets":[])") != std::string::npos,
        "detached iframe retained DOMContentLoaded listeners: "
            + event_listener_inventory(engine));

    require(webscene_engine_request_low_memory(engine) != 0,
        "iframe navigation low-memory request was rejected");
    webscene_engine_memory_metrics memory_after{sizeof(memory_after)};
    for (auto attempt = 0; attempt < 250; ++attempt) {
        require(webscene_engine_get_memory_metrics(engine, &memory_after) != 0,
            "iframe navigation final memory metrics were unavailable");
        if (memory_after.low_memory_notifications
            > memory_before.low_memory_notifications) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    const auto rss_after = service_worker_test_current_rss_bytes();
    require(memory_after.low_memory_notifications
            > memory_before.low_memory_notifications
            && memory_after.v8_used_heap_bytes
                <= memory_before.v8_used_heap_bytes + 8U * 1024U * 1024U
            && memory_after.native_dom_node_count
                <= memory_before.native_dom_node_count + 8U,
        "iframe navigation cycles retained heap or DOM nodes");
    require(rss_before == 0U || rss_after <= rss_before + 64U * 1024U * 1024U,
        "iframe navigation cycles retained more than 64 MiB RSS");
    std::cout << "Iframe navigation lifecycle gate: cycles=100 bulkMiB=1 p95="
              << evaluate(engine, "__iframeLifecycleGate.p95", "iframe-p95.js")
              << "ms heapBefore=" << memory_before.v8_used_heap_bytes
              << " heapAfter=" << memory_after.v8_used_heap_bytes
              << " rssBefore=" << rss_before << " rssAfter=" << rss_after << '\n';
    webscene_engine_destroy(engine);
}

void test_nested_window_find_selection() {
  constexpr std::string_view address{"https://iframe-find.test/index.html"};
  resource_server server{
      .content = {{std::string{address},
                   "<!doctype html><title>frame find</title><body></body>"},
                  {"https://iframe-find.test/fake.html?id=markdown-preview",
                   "<!doctype html><title>Fake</title><body></body>"},
                  {"https://cross-origin-find.test/fake.html",
                   "<!doctype "
                   "html><body><script>parent.__windowFindEscape=true;</"
                   "script></body>"}}};
  webscene_engine_options options{};
  options.struct_size = sizeof(options);
  options.resource_load_callback = load_test_resource;
  options.resource_load_user_data = &server;
  auto *engine = webscene_engine_create_with_options(&options);
  require(engine != nullptr, "nested Window.find engine creation failed");
  require(webscene_engine_load_url(engine, address.data(), address.size()) !=
              0U,
          "nested Window.find outer document failed");
  require(evaluate_until_equals(engine, "location.href", "window-find-ready.js",
                                R"("https://iframe-find.test/index.html")",
                                2'500) ==
              R"("https://iframe-find.test/index.html")",
          "nested Window.find outer document did not become ready");

  execute(engine, R"JS(
      globalThis.__windowFind = {complete:false,error:null};
      (() => {
        const state = __windowFind;
        try {
          const frame = document.createElement('iframe');
          frame.setAttribute('sandbox', 'allow-same-origin allow-scripts');
          frame.src = './fake.html?id=markdown-preview';
          document.body.appendChild(frame);
          const proxy = frame.contentWindow;
          proxy.addEventListener('DOMContentLoaded', event => {
            try {
              const doc = event.target;
              doc.open();
              doc.write('<!doctype html><body><p id="a">Alpha beta alpha BETA alphabet</p><p id="b">omega beta</p><p id="c">café 😀 target</p></body>');
              doc.close();
              const selection = proxy.getSelection();
              state.stable = selection === proxy.getSelection()
                && selection === doc.getSelection();
              const snap = result => ({
                result, text:selection.toString(),
                parent:selection.anchorNode?.parentElement?.id || null,
                anchor:selection.anchorOffset, focus:selection.focusOffset,
                count:selection.rangeCount, collapsed:selection.isCollapsed
              });
              state.alpha1 = snap(proxy.find('alpha', false, false, true, false, false, false));
              state.alpha2 = snap(proxy.find('alpha', false, false, true, false, false, false));
              state.alpha3 = snap(proxy.find('alpha', false, false, true, false, false, false));
              state.case1 = snap(proxy.find('BETA', true, false, true, false, false, false));
              state.case2 = snap(proxy.find('BETA', true, false, true, false, false, false));
              selection.removeAllRanges();
              state.cleared = snap(null);
              state.backward1 = snap(proxy.find('beta', false, true, true, false, false, false));
              state.backward2 = snap(proxy.find('beta', false, true, true, false, false, false));
              selection.empty();
              state.whole1 = snap(proxy.find('alpha', false, false, true, true, false, false));
              state.whole2 = snap(proxy.find('alpha', false, false, true, true, false, false));
              state.whole3 = snap(proxy.find('alpha', false, false, true, true, false, false));
              const beforeMiss = selection.toString();
              state.miss = snap(proxy.find('absent', false, false, true, false, false, false));
              state.missPreserved = selection.toString() === beforeMiss;
              const range = selection.getRangeAt(0);
              state.range = {
                startContainer:range.startContainer === selection.anchorNode,
                endContainer:range.endContainer === selection.focusNode,
                startOffset:range.startOffset,
                selectionStart:selection.anchorOffset,
                endOffset:range.endOffset,
                selectionEnd:selection.focusOffset
              };
              selection.collapse(selection.anchorNode);
              state.collapsed = snap(null);
              state.afterCollapse = snap(proxy.find('beta', false, false, true, false, false, false));
              selection.removeAllRanges();
              state.unicode = snap(proxy.find('😀', true, false, false, true, false, false));
              doc.open();
              doc.write('<!doctype html><body><p>replacement text</p></body>');
              doc.close();
              state.replacementReset = selection.rangeCount === 0
                && selection.anchorNode === null && selection.toString() === '';
              state.selectionAfterReplacementStable = selection === proxy.getSelection();
              frame.remove();
              state.detachReset = selection.rangeCount === 0
                && selection.anchorNode === null;
              const opaque = document.createElement('iframe');
              opaque.setAttribute('sandbox', 'allow-scripts');
              opaque.src = './fake.html?id=markdown-preview';
              document.body.appendChild(opaque);
              const cross = document.createElement('iframe');
              cross.setAttribute('sandbox', 'allow-same-origin allow-scripts');
              cross.src = 'https://cross-origin-find.test/fake.html';
              document.body.appendChild(cross);
              state.failClosed = opaque.contentWindow === null
                && opaque.contentDocument === null
                && cross.contentWindow === null
                && cross.contentDocument === null;
              opaque.remove(); cross.remove();
              state.complete = true;
            } catch (error) { state.error = String(error && error.stack || error); state.complete = true; }
          });
        } catch (error) { state.error = String(error && error.stack || error); state.complete = true; }
      })();
    )JS",
          "window-find-markdown-sequence.js");
  require(evaluate_until_equals(engine, "__windowFind.complete",
                                "window-find-complete.js", "true",
                                3'000) == "true",
          "nested Window.find sequence timed out: " +
              evaluate(engine, "JSON.stringify(__windowFind)",
                       "window-find-timeout.js"));
  require(evaluate(engine, R"JS((() => {
      const s=__windowFind;
      return s.error===null && s.stable
        && JSON.stringify(s.alpha1)==='{"result":true,"text":"Alpha","parent":"a","anchor":0,"focus":5,"count":1,"collapsed":false}'
        && JSON.stringify(s.alpha2)==='{"result":true,"text":"alpha","parent":"a","anchor":11,"focus":16,"count":1,"collapsed":false}'
        && JSON.stringify(s.alpha3)==='{"result":true,"text":"alpha","parent":"a","anchor":22,"focus":27,"count":1,"collapsed":false}'
        && s.case1.text==='BETA' && s.case1.anchor===17 && s.case2.text==='BETA'
        && s.cleared.count===0 && s.cleared.collapsed && s.cleared.parent===null
        && s.backward1.parent==='b' && s.backward1.anchor===6
        && s.backward2.parent==='a' && s.backward2.anchor===17
        && s.whole1.text==='Alpha' && s.whole1.anchor===0
        && s.whole2.text==='alpha' && s.whole2.anchor===11
        && s.whole3.text==='Alpha' && s.whole3.anchor===0
        && !s.miss.result && s.missPreserved
        && s.range.startContainer && s.range.endContainer
        && s.range.startOffset===s.range.selectionStart
        && s.range.endOffset===s.range.selectionEnd
        && s.collapsed.collapsed && s.collapsed.count===1
        && s.afterCollapse.text==='beta' && s.afterCollapse.anchor===6
        && s.unicode.text==='😀' && s.unicode.parent==='c'
        && s.unicode.anchor===5 && s.unicode.focus===7
        && s.replacementReset && s.selectionAfterReplacementStable
        && s.detachReset && s.failClosed;
    })())JS",
                   "window-find-result.js") == "true",
          "nested Window.find/Selection diverged from Chrome: " +
              evaluate(engine, "JSON.stringify(__windowFind)",
                       "window-find-detail.js"));

  webscene_engine_memory_metrics memory_before{sizeof(memory_before)};
  require(webscene_engine_get_memory_metrics(engine, &memory_before) != 0,
          "Window.find memory baseline was unavailable");
  const auto rss_before = service_worker_test_current_rss_bytes();
  execute(engine, R"JS(
      globalThis.__windowFindGate = {complete:false,error:null,cycles:0,p95:0};
      (async () => {
        try {
          for (let cycle=0; cycle<100; ++cycle) {
            await new Promise((resolve,reject) => {
              const frame=document.createElement('iframe');
              frame.setAttribute('sandbox','allow-same-origin allow-scripts');
              frame.src='./fake.html?id=markdown-preview';
              document.body.appendChild(frame);
              const proxy=frame.contentWindow;
              const timeout=setTimeout(()=>reject(new Error(`cycle ${cycle} timed out`)),1000);
              proxy.addEventListener('DOMContentLoaded',event=>{
                try {
                  const doc=event.target;
                  doc.open(); doc.write(`<body><p>cycle ${cycle} needle tail</p></body>`); doc.close();
                  const selection=proxy.getSelection();
                  if (!proxy.find('needle',false,false,true,false,false,false)
                      || selection.toString()!=='needle') throw new Error(`cycle ${cycle} find failed`);
                  frame.remove();
                  if (selection.rangeCount!==0 || selection.anchorNode!==null)
                    throw new Error(`cycle ${cycle} selection survived detach`);
                  ++__windowFindGate.cycles;
                  clearTimeout(timeout); setTimeout(resolve,0);
                } catch(error) { clearTimeout(timeout); reject(error); }
              });
            });
          }
          const bulk=document.createElement('div');
          bulk.id='window-find-bulk';
          bulk.innerHTML='<span>noise</span>'.repeat(100000)+'<span>target-token</span>';
          document.body.appendChild(bulk);
          if (!find('target-token',true,false,false,true,false,false))
            throw new Error('bulk warm-up find failed');
          getSelection().removeAllRanges();
          const durations=[];
          for (let iteration=0; iteration<10; ++iteration) {
            getSelection().removeAllRanges();
            const started=performance.now();
            if (!find('target-token',true,false,false,true,false,false))
              throw new Error(`bulk find ${iteration} failed`);
            durations.push(performance.now()-started);
          }
          durations.sort((a,b)=>a-b);
          __windowFindGate.p95=durations[Math.ceil(durations.length*.95)-1];
          bulk.remove();
          getSelection().removeAllRanges();
        } catch(error) { __windowFindGate.error=String(error&&error.stack||error); }
        finally { __windowFindGate.complete=true; }
      })();
    )JS",
          "window-find-lifecycle-performance.js");
  require(evaluate_until_equals(engine, "__windowFindGate.complete",
                                "window-find-gate-complete.js", "true",
                                30'000) == "true",
          "Window.find lifecycle/performance gate timed out");
  require(evaluate(engine, R"JS((() => {
      const s=__windowFindGate;
      return s.error===null && s.cycles===100 && s.p95<=50;
    })())JS",
                   "window-find-gate-result.js") == "true",
          "Window.find lifecycle/performance gate diverged: " +
              evaluate(engine, "JSON.stringify(__windowFindGate)",
                       "window-find-gate-detail.js"));
  require(event_listener_inventory(engine).find(R"("targets":[])") !=
              std::string::npos,
          "Window.find frame cycles retained listeners: " +
              event_listener_inventory(engine));
  require(webscene_engine_request_low_memory(engine) != 0,
          "Window.find low-memory request was rejected");
  webscene_engine_memory_metrics memory_after{sizeof(memory_after)};
  for (auto attempt = 0; attempt < 250; ++attempt) {
    require(webscene_engine_get_memory_metrics(engine, &memory_after) != 0,
            "Window.find final memory metrics were unavailable");
    if (memory_after.low_memory_notifications >
        memory_before.low_memory_notifications)
      break;
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  const auto rss_after = service_worker_test_current_rss_bytes();
  std::cout << "Window.find gate: textNodes=100000 cycles=100 p95="
            << evaluate(engine, "__windowFindGate.p95", "window-find-p95.js")
            << "ms heapBefore=" << memory_before.v8_used_heap_bytes
            << " heapAfter=" << memory_after.v8_used_heap_bytes
            << " nodesBefore=" << memory_before.native_dom_node_count
            << " nodesAfter=" << memory_after.native_dom_node_count
            << " rssBefore=" << rss_before << " rssAfter=" << rss_after << '\n';
  require(memory_after.low_memory_notifications >
                  memory_before.low_memory_notifications &&
              memory_after.v8_used_heap_bytes <=
                  memory_before.v8_used_heap_bytes + 4U * 1024U * 1024U &&
              memory_after.native_dom_node_count <=
                  memory_before.native_dom_node_count + 16U,
          "Window.find cycles retained V8 heap or native DOM nodes");
  require(rss_before == 0U || rss_after <= rss_before + 512U * 1024U * 1024U,
          "Window.find 100,000-text-node high-water RSS exceeded 512 MiB");
  webscene_engine_destroy(engine);
}

size_t load_contextual_test_resource(
    void* user_data,
    uint32_t kind,
    const char* url,
    size_t url_length,
    const char* entity_tag,
    size_t entity_tag_length,
    int64_t last_modified,
    const webscene_resource_request_context* context,
    char* destination,
    size_t destination_capacity)
{
    auto& server = *static_cast<contextual_resource_server*>(user_data);
    const auto address = std::string(url, url_length);
    {
        const std::scoped_lock lock(server.request_mutex);
        server.requested_urls.push_back(address);
    }
    if (address.starts_with("https://symbol-search.test/")) {
        server.fetched_url = address;
        if (context != nullptr) {
            server.origin.assign(context->origin, context->origin_length);
            server.referrer.assign(context->referrer, context->referrer_length);
            server.initiator = context->initiator;
            server.mode = context->mode;
        }
    }
    return load_test_resource(
        &server.resources,
        kind,
        url,
        url_length,
        entity_tag,
        entity_tag_length,
        last_modified,
        destination,
        destination_capacity);
}

size_t load_contextual_test_resource_v3(
    void* user_data,
    uint32_t kind,
    const char* url,
    size_t url_length,
    const char* entity_tag,
    size_t entity_tag_length,
    int64_t last_modified,
    const webscene_resource_request_context_v3* context,
    char* destination,
    size_t destination_capacity)
{
    auto& server = *static_cast<contextual_resource_server*>(user_data);
    const auto address = std::string(url, url_length);
    {
        const std::scoped_lock lock(server.request_mutex);
        server.requested_urls.push_back(address);
    }
    if (context != nullptr && context->initiator == WEBSCENE_RESOURCE_INITIATOR_FETCH) {
        server.fetched_url = address;
        server.origin.assign(context->origin, context->origin_length);
        server.referrer.assign(context->referrer, context->referrer_length);
        server.initiator = context->initiator;
        server.mode = context->mode;
        server.method.assign(context->method, context->method_length);
        server.body.assign(context->body, context->body_length);
        server.content_type.assign(context->content_type, context->content_type_length);
    }
    return load_test_resource(
        &server.resources,
        kind,
        url,
        url_length,
        entity_tag,
        entity_tag_length,
        last_modified,
        destination,
        destination_capacity);
}

void test_initial_non_javascript_script_is_inert()
{
    resource_server server{.content = {
        {"https://script-type.test/index.html", R"HTML(
            <!doctype html><body>
            <script id="data" type="application/json">globalThis.__inertExecuted = true</script>
            <script>globalThis.__executableObservedInert = globalThis.__inertExecuted === undefined</script>
            </body>
        )HTML"}
    }};
    webscene_engine_options options{};
    options.struct_size = sizeof(options);
    options.resource_load_callback = load_test_resource;
    options.resource_load_user_data = &server;
    auto* engine = webscene_engine_create_with_options(&options);
    require(engine != nullptr, "inert initial script engine creation failed");
    constexpr std::string_view address{"https://script-type.test/index.html"};
    require(
        webscene_engine_load_url(engine, address.data(), address.size()) != 0U,
        "inert initial script navigation failed");
    const auto result = evaluate_until_equals(
        engine,
        "JSON.stringify([globalThis.__executableObservedInert, document.getElementById('data')?.type, document.getElementById('data')?.textContent])",
        "initial-inert-script-result.js",
        R"("[true,\"application/json\",\"globalThis.__inertExecuted = true\"]")");
    require(
        result == R"("[true,\"application/json\",\"globalThis.__inertExecuted = true\"]")",
        "initial non-JavaScript script executed or lost its DOM data: " + result);
    webscene_engine_destroy(engine);
}

void test_initial_document_images_are_loaded()
{
    constexpr std::array<unsigned char,70> raster_png{{
        0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a,0x00,0x00,0x00,0x0d,
        0x49,0x48,0x44,0x52,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x01,
        0x08,0x06,0x00,0x00,0x00,0x1f,0x15,0xc4,0x89,0x00,0x00,0x00,
        0x0d,0x49,0x44,0x41,0x54,0x78,0xda,0x63,0x90,0xdd,0x19,0xf2,
        0x1f,0x00,0x04,0x4b,0x02,0x2a,0x81,0xeb,0x0d,0xbc,0x00,0x00,
        0x00,0x00,0x49,0x45,0x4e,0x44,0xae,0x42,0x60,0x82}};
    resource_server server{.content = {
        {"https://initial-image.test/index.html",
            R"HTML(<!doctype html><img id="cover" src="cover.png" style="width:40px">)HTML"},
        {"https://initial-image.test/cover.png", std::string(
            reinterpret_cast<const char*>(raster_png.data()), raster_png.size())}
    }};
    webscene_engine_options options{};
    options.struct_size = sizeof(options);
    options.resource_load_callback = load_test_resource;
    options.resource_load_user_data = &server;
    auto* engine = webscene_engine_create_with_options(&options);
    require(engine != nullptr, "initial image engine creation failed");
    constexpr std::string_view address{"https://initial-image.test/index.html"};
    require(
        webscene_engine_load_url(engine, address.data(), address.size()) != 0U,
        "initial image navigation failed");
    const auto state = evaluate_until_equals(
        engine,
        "JSON.stringify((() => { const image = document.getElementById('cover'); "
        "const rect = image.getBoundingClientRect(); return [image.complete, "
        "image.naturalWidth, image.naturalHeight, rect.width, rect.height]; })())",
        "initial-image-state.js",
        R"("[true,1,1,40,40]")");
    require(
        state == R"("[true,1,1,40,40]")",
        "an image authored in the navigation response was not loaded: " + state);
    webscene_engine_destroy(engine);
}

void test_fetch_carries_document_origin_to_resource_host()
{
    contextual_resource_server server{
        .resources = resource_server{.content = {
            {"https://www.tradingview-widget.test/embed/index.html", R"HTML(
                <main>symbol search fixture</main>
            )HTML"},
            {"https://symbol-search.test/symbol_search/?text=AAPL",
                R"JSON([{"symbol":"AAPL","description":"Apple Inc."}])JSON"}
        }}
    };
    webscene_engine_options options{};
    options.struct_size = sizeof(options);
    options.resource_load_callback_v2 = load_contextual_test_resource;
    options.resource_load_v2_user_data = &server;
    auto* engine = webscene_engine_create_with_options(&options);
    require(engine != nullptr, "contextual resource engine creation failed");
    constexpr std::string_view address{
        "https://www.tradingview-widget.test/embed/index.html"};
    require(
        webscene_engine_load_url(engine, address.data(), address.size()) != 0U,
        "contextual resource navigation failed");
    execute_and_wait(
        engine,
        "fetch('https://symbol-search.test/symbol_search/?text=AAPL')"
        ".then(response => response.json())"
        ".then(symbols => globalThis.__symbols = symbols)",
        "fetch-origin-fixture.js");

    // execute_and_wait observes completion of the script that schedules the
    // fetch, not settlement of the fetch promise. Wait for the fixture result
    // before inspecting metadata populated by the asynchronous callback.
    const auto result = evaluate_until_equals(
        engine,
        "globalThis.__symbols?.[0]?.symbol ?? ''",
        "iframe-fetch-origin-result.js",
        R"("AAPL")");

    require(
        server.origin == "https://www.tradingview-widget.test",
        "fetch did not carry the document origin: " + server.origin);
    require(
        server.referrer == "https://www.tradingview-widget.test/embed/index.html",
        "fetch did not carry the document referrer: " + server.referrer);
    require(
        server.initiator == WEBSCENE_RESOURCE_INITIATOR_FETCH
            && server.mode == WEBSCENE_FETCH_MODE_CORS,
        "fetch request context did not identify a CORS fetch");
    require(result == R"("AAPL")", "symbol fetch did not populate results: " + result);
    webscene_engine_destroy(engine);
}

struct slow_fetch_boundary_server final {
    resource_server resources;
    std::atomic<bool> entered{false};
    std::atomic<bool> release{false};
};

size_t load_slow_fetch_boundary_resource(
    void* user_data,
    uint32_t kind,
    const char* url,
    size_t url_length,
    const char* entity_tag,
    size_t entity_tag_length,
    int64_t last_modified,
    char* destination,
    size_t destination_capacity)
{
    auto& server = *static_cast<slow_fetch_boundary_server*>(user_data);
    const auto address = std::string_view(url, url_length);
    if (kind == WEBSCENE_RESOURCE_DATA
        && address == "https://symbol-search.test/search?q=T") {
        server.entered.store(true, std::memory_order_release);
        while (!server.release.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
    }
    return load_test_resource(
        &server.resources,
        kind,
        url,
        url_length,
        entity_tag,
        entity_tag_length,
        last_modified,
        destination,
        destination_capacity);
}

void test_slow_fetch_does_not_block_loading_state_publication()
{
    slow_fetch_boundary_server server{
        .resources = resource_server{.content = {
            {"https://symbol-search.test/index.html", "<main>ready</main>"},
            {"https://symbol-search.test/search?q=T", R"JSON([{"symbol":"TSLA"}])JSON"}
        }}};
    webscene_engine_options options{};
    options.struct_size = sizeof(options);
    options.resource_load_callback = load_slow_fetch_boundary_resource;
    options.resource_load_user_data = &server;
    auto* engine = webscene_engine_create_with_options(&options);
    require(engine != nullptr, "slow-fetch boundary engine creation failed");
    constexpr std::string_view address{"https://symbol-search.test/index.html"};
    require(
        webscene_engine_load_url(engine, address.data(), address.size()) != 0U,
        "slow-fetch boundary navigation failed");
    webscene_engine_metrics before{};
    webscene_engine_get_metrics(engine, &before);
    execute_and_wait(
        engine,
        R"JS(
          document.body.innerHTML = '<div id="loading">Loading symbols</div>';
          globalThis.__slowFetchResult = '';
          fetch('https://symbol-search.test/search?q=T')
            .then(response => response.json())
            .then(rows => { globalThis.__slowFetchResult = rows[0].symbol; });
        )JS",
        "slow-fetch-loading-state.js");

    auto entered = false;
    auto published_while_blocked = false;
    for (auto attempt = 0; attempt < 250; ++attempt) {
        entered = server.entered.load(std::memory_order_acquire);
        webscene_engine_metrics after{};
        webscene_engine_get_metrics(engine, &after);
        published_while_blocked = after.published_scenes > before.published_scenes;
        if (entered && published_while_blocked) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    const auto loading_text = evaluate(
        engine,
        "document.getElementById('loading')?.textContent ?? ''",
        "slow-fetch-loading-state-result.js");
    server.release.store(true, std::memory_order_release);

    require(entered, "slow fetch did not reach the host resource loader");
    require(
        published_while_blocked,
        "loading state was not published while the fetch remained blocked");
    require(
        loading_text == R"("Loading symbols")",
        "V8 became unresponsive while the host fetch was blocked: " + loading_text);
    require(
        evaluate_until_equals(
            engine,
            "globalThis.__slowFetchResult",
            "slow-fetch-completion-result.js",
            R"("TSLA")",
            500) == R"("TSLA")",
        "slow fetch did not settle its Promise after host completion");
    webscene_engine_destroy(engine);
}

void test_tradingview_datafeed_iframe_symbol_search_round_trip()
{
    contextual_resource_server server{
        .resources = resource_server{.content = {
            {"https://terminal.test/index.html", R"HTML(
                <html><body>
                <script>
                  globalThis.__searchResult = null;
                  globalThis.__searchError = null;
                  globalThis.__messageLog = [];
                  addEventListener('message', event => {
                    globalThis.__messageLog.push([event.origin, event.data?.uid ?? null]);
                    if (event.origin !== 'https://datafeed.test') return;
                    if (event.data?.uid === 'READY') {
                      document.querySelector('iframe').contentWindow.postMessage(
                        { name: 'searchSymbols', args: ['AAPL'], uid: 'search-1' },
                        'https://datafeed.test');
                    } else if (event.data?.uid === 'search-1') {
                      globalThis.__searchResult = event.data.data.args;
                    }
                  });
                  const frame = document.createElement('iframe');
                  frame.src = 'https://datafeed.test/embed-widget/tradingview-datafeed/';
                  document.body.appendChild(frame);
                </script>
                </body></html>
            )HTML"},
            {"https://datafeed.test/embed-widget/tradingview-datafeed/", R"HTML(
                <html><head><script defer src="/datafeed.js"></script></head><body></body></html>
            )HTML"},
            {"https://datafeed.test/datafeed.js", R"JS(
                  parent.__frameScript = 'ran';
                  addEventListener('DOMContentLoaded', () => {
                    parent.__frameDomReady = 'ran';
                    const parentOrigin = 'https://terminal.test';
                    addEventListener('message', async event => {
                      if (event.origin !== parentOrigin
                          || event.data?.name !== 'searchSymbols') return;
                      try {
                        const controller = new AbortController();
                        const query = new URLSearchParams({
                          text: event.data.args[0],
                          exchange: '',
                          type: ''
                        });
                        const response = await fetch(
                          `https://symbol-search.test/symbol_search/?${query}`,
                          { signal: controller.signal });
                        const symbols = await response.json();
                        parent.postMessage({
                          uid: event.data.uid,
                          data: { status: 'ok', args: [symbols] }
                        }, parentOrigin);
                      } catch (error) {
                        parent.postMessage({
                          uid: event.data.uid,
                          data: { status: 'error', args: [String(error)] }
                        }, parentOrigin);
                      }
                    });
                    parent.postMessage({
                      uid: 'READY',
                      data: { status: 'ok', args: [] }
                    }, parentOrigin);
                  });
            )JS"},
            {"https://symbol-search.test/symbol_search/?text=AAPL&exchange=&type=",
                R"JSON([{"symbol":"AAPL","description":"Apple Inc."}])JSON"}
        }}
    };
    webscene_engine_options options{};
    options.struct_size = sizeof(options);
    options.simulated_chart_command_count = 64U;
    options.resource_load_callback_v3 = load_contextual_test_resource_v3;
    options.resource_load_v3_user_data = &server;
    auto* engine = webscene_engine_create_with_options(&options);
    require(engine != nullptr, "datafeed symbol-search engine creation failed");
    constexpr std::string_view address{"https://terminal.test/index.html"};
    require(
        webscene_engine_load_url(engine, address.data(), address.size()) != 0U,
        "datafeed symbol-search navigation failed");

    auto result = std::string{};
    for (auto attempt = 0; attempt < 500; ++attempt) {
        result = evaluate(
            engine,
            "JSON.stringify({result:globalThis.__searchResult,error:globalThis.__searchError,messages:globalThis.__messageLog,frameScript:globalThis.__frameScript,frameDomReady:globalThis.__frameDomReady,frameCount:document.querySelectorAll('iframe').length,frameReady:document.querySelector('iframe')?.contentDocument?.readyState,frameWindowScript:document.querySelector('iframe')?.contentWindow?.__frameScript,frameWindowDomReady:document.querySelector('iframe')?.contentWindow?.__frameDomReady,scriptCount:document.querySelector('iframe')?.contentDocument?.querySelectorAll('script').length,scriptSrc:document.querySelector('iframe')?.contentDocument?.querySelector('script')?.getAttribute('src')})",
            "datafeed-symbol-search-result.js");
        if (result.find("AAPL") != std::string::npos) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    webscene_engine_metrics metrics{};
    webscene_engine_get_metrics(engine, &metrics);
    auto requested_urls = std::string{};
    {
        const std::scoped_lock lock(server.request_mutex);
        for (const auto& requested : server.requested_urls) {
            if (!requested_urls.empty()) requested_urls += ", ";
            requested_urls += requested;
        }
    }
    require(
        result.find("AAPL") != std::string::npos,
        "TradingView-shaped datafeed iframe search did not round-trip: " + result
            + "; last error: " + last_error(engine)
            + "; requests: " + std::to_string(
                server.resources.requests.load(std::memory_order_relaxed))
            + "; frame script errors: " + std::to_string(metrics.frame_script_errors)
            + "; URLs: " + requested_urls);
    require(
        server.origin == "https://datafeed.test",
        "datafeed fetch did not carry the iframe origin: " + server.origin);
    require(
        server.referrer
            == "https://datafeed.test/embed-widget/tradingview-datafeed/",
        "datafeed fetch did not carry the iframe referrer: " + server.referrer);
    webscene_engine_destroy(engine);
}

void test_tradingview_save_acknowledgement_uses_multipart_post()
{
    constexpr std::string_view page_url{"https://host.test/index.html"};
    constexpr std::string_view save_url{
        "https://saveload.test/1.1/charts?client=terminal&user=public"};
    contextual_resource_server server{
        .resources = resource_server{.content = {
            {std::string(page_url), R"HTML(
                <style>
                  .stateTitle { display: block; color: rgb(41, 98, 255); }
                  .stateTitle.hidden { height: 0; overflow: hidden; }
                </style>
                <button id="save">Default <span id="dirty" class="stateTitle">Save</span></button>
                <script>
                  let saveInProgress = false;
                  document.getElementById('save').addEventListener('click', async () => {
                    if (saveInProgress) return;
                    saveInProgress = true;
                    const payload = new FormData();
                    payload.append('name', 'Default');
                    payload.append('content', '{"layout":1}');
                    try {
                      const response = await fetch(
                        'https://saveload.test/1.1/charts?client=terminal&user=public',
                        { method: 'POST', body: payload, credentials: 'same-origin' });
                      const result = await response.json();
                      if (response.ok && result.status === 'ok') {
                        document.getElementById('dirty').classList.add('hidden');
                      }
                    } finally {
                      saveInProgress = false;
                    }
                  });
                </script>)HTML"},
            {std::string(save_url), R"JSON({"status":"ok","id":"layout-42"})JSON"}
        }}
    };
    webscene_engine_options options{};
    options.struct_size = sizeof(options);
    options.resource_load_callback_v3 = load_contextual_test_resource_v3;
    options.resource_load_v3_user_data = &server;
    auto* engine = webscene_engine_create_with_options(&options);
    require(engine != nullptr, "save acknowledgement engine creation failed");
    resize(engine, 800, 600, 1U);
    require(
        webscene_engine_load_url(engine, page_url.data(), page_url.size()) != 0U,
        "save acknowledgement fixture navigation failed");
    auto document_ready = false;
    for (auto attempt = 0; attempt < 400; ++attempt) {
        document_ready = evaluate(
            engine,
            "Boolean(document.getElementById('save'))",
            "tradingview-save-document-ready.js") == "true";
        if (document_ready) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(document_ready, "TradingView-shaped save document did not finish loading");
    execute_and_wait(
        engine,
        "document.getElementById('save').click()",
        "tradingview-save-activation.js");
    for (auto attempt = 0; attempt < 250; ++attempt) {
        if (evaluate(
                engine,
                "document.getElementById('dirty').classList.contains('hidden')",
                "tradingview-save-dirty-result.js") == "true") {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    require(server.method == "POST", "TradingView-shaped save did not reach the host as POST");
    require(
        server.content_type.starts_with("multipart/form-data; boundary="),
        "TradingView-shaped save did not carry a multipart boundary");
    require(
        server.body.find("name=\"name\"\r\n\r\nDefault") != std::string::npos
            && server.body.find("name=\"content\"\r\n\r\n{\"layout\":1}") != std::string::npos,
        "TradingView-shaped save multipart fields were not preserved");
    require(
        evaluate(
            engine,
            R"JS((() => {
              const dirty = document.getElementById('dirty');
              return [dirty.textContent, dirty.getBoundingClientRect().height,
                getComputedStyle(dirty).overflow];
            })())JS",
            "tradingview-save-final-dirty-result.js") == R"(["Save",0,"hidden"])",
        "successful asynchronous save acknowledgement did not collapse the dirty label");

    uint64_t prior_scene_revision = 0U;
    if (const auto* stale = webscene_engine_acquire_latest_scene(engine)) {
        prior_scene_revision = stale->header.revision;
        webscene_scene_acknowledge(stale);
        webscene_scene_release(stale);
    }
    webscene_engine_request_scene_checkpoint(engine);
    auto observed_retained_save_text = false;
    auto observed_zero_height_clip = false;
    for (auto attempt = 0; attempt < 100; ++attempt) {
        const auto* scene = webscene_engine_acquire_latest_scene(engine);
        if (scene != nullptr) {
            if (scene->header.revision > prior_scene_revision
                && (scene->header.flags & 1U) != 0U) {
                std::vector<bool> zero_height_clip_stack;
                for (uint32_t index = 0; index < scene->header.command_count; ++index) {
                    const auto& command = scene->commands[index];
                    if (command.kind == 12U) {
                        zero_height_clip_stack.push_back(
                            std::abs(command.height) < 0.01F);
                        continue;
                    }
                    if (command.kind == 13U) {
                        if (!zero_height_clip_stack.empty()) {
                            zero_height_clip_stack.pop_back();
                        }
                        continue;
                    }
                    if (command.kind != 3U || command.flags >= scene->string_count) {
                        continue;
                    }
                    const auto& descriptor = scene->strings[command.flags];
                    const std::string_view resource(
                        scene->string_bytes + descriptor.byte_offset,
                        descriptor.byte_length);
                    if (!resource.ends_with("\tSave")) continue;
                    observed_retained_save_text = true;
                    observed_zero_height_clip = std::find(
                        zero_height_clip_stack.begin(),
                        zero_height_clip_stack.end(),
                        true) != zero_height_clip_stack.end();
                    break;
                }
            }
            webscene_scene_acknowledge(scene);
            webscene_scene_release(scene);
        }
        if (observed_retained_save_text) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(
        observed_retained_save_text,
        "successful save fixture did not retain its collapsed status text");
    require(
        observed_zero_height_clip,
        "zero-height overflow viewport did not clip retained save status text");
    webscene_engine_destroy(engine);
}

void test_failed_tradingview_save_keeps_dirty_label()
{
    constexpr std::string_view page_url{"https://terminal.test/failing-save.html"};
    constexpr std::string_view save_url{"https://saveload.test/failing"};
    contextual_resource_server server{
        .resources = resource_server{.content = {
            {std::string(page_url), R"HTML(
                <button id="save"><span id="dirty">Save</span></button>
                <script>
                  document.getElementById('save').addEventListener('click', async () => {
                    const form = new FormData();
                    form.append('content', '{}');
                    const response = await fetch(
                      'https://saveload.test/failing',
                      { method: 'POST', body: form });
                    const result = await response.json();
                    if (response.ok && result.status === 'ok') {
                      document.getElementById('dirty').textContent = '';
                    }
                  });
                </script>)HTML"},
            {std::string(save_url), R"JSON({"status":"error","message":"rejected"})JSON"}
        }}
    };
    webscene_engine_options options{};
    options.struct_size = sizeof(options);
    options.resource_load_callback_v3 = load_contextual_test_resource_v3;
    options.resource_load_v3_user_data = &server;
    auto* engine = webscene_engine_create_with_options(&options);
    require(engine != nullptr, "failed-save engine creation failed");
    require(
        webscene_engine_load_url(engine, page_url.data(), page_url.size()) != 0U,
        "failed-save fixture navigation failed");
    execute_and_wait(
        engine,
        "document.getElementById('save').click()",
        "tradingview-failed-save-activation.js");
    require(
        evaluate(
            engine,
            "document.getElementById('dirty').textContent",
            "tradingview-failed-save-dirty-result.js") == R"("Save")",
        "failed save incorrectly cleared the dirty label");
    webscene_engine_destroy(engine);
}

void test_document_start_ordering_storage_and_fail_closed_errors()
{
    resource_server server{
        .content = {
            {"https://document-start.test/index.html", R"HTML(
                <!doctype html><html><body><script>
                  __documentStartTrace.push('authored-main');
                  Promise.resolve().then(() =>
                    __documentStartTrace.push('authored-main-microtask'));
                  setTimeout(() =>
                    __documentStartTrace.push('authored-main-timer'), 0);
                  globalThis.__earliestStorage = [
                    typeof localStorage, typeof sessionStorage,
                    localStorage.getItem('document-start')
                  ];
                  const frame = document.createElement('iframe');
                  document.body.appendChild(frame);
                  const frameDocument = frame.contentDocument;
                  frameDocument.open();
                  frameDocument.write('<html><body><script>'
                    + 'parent.__documentStartTrace.push("authored-frame");'
                    + 'Promise.resolve().then(() => '
                    + 'parent.__documentStartTrace.push("authored-frame-microtask"));'
                    + 'globalThis.__frameStorage = ['
                    + 'typeof localStorage, typeof sessionStorage,'
                    + 'localStorage.getItem("document-start")];'
                    + 'globalThis.__mainOnlyAbsent = '
                    + 'typeof __documentStartMainOnly === "undefined";'
                    + '<\/script></body></html>');
                  frameDocument.close();
                  globalThis.__documentStartFrame = frame;
                </script></body></html>)HTML"},
            {"https://document-start.test/failure.html", R"HTML(
                <html><body><script>
                  globalThis.__authoredRanAfterDocumentStartFailure = true;
                </script></body></html>)HTML"},
            {"https://document-start.test/frame-failure.html", R"HTML(
                <html><body><script>
                  var failingFrame = document.createElement('iframe');
                  document.body.appendChild(failingFrame);
                  var failingFrameDocument = failingFrame.contentDocument;
                  failingFrameDocument.open();
                  failingFrameDocument.write('<html><body><script>'
                    + 'globalThis.__frameAuthoredAfterFailure = true;'
                    + '<\/script></body></html>');
                  failingFrameDocument.close();
                  globalThis.__failingDocumentStartFrame = failingFrame;
                </script></body></html>)HTML"}
        }};
    const webscene_engine_options engine_options{
        sizeof(webscene_engine_options),
        64U,
        nullptr,
        0U,
        load_test_resource,
        &server};
    auto* engine = webscene_engine_create_with_options(&engine_options);
    require(engine != nullptr, "document-start engine creation failed");
    webscene_engine_configure_diagnostics(engine, WEBSCENE_DIAGNOSTIC_LEGACY_CONSOLE, nullptr, nullptr);

    constexpr std::string_view all_frames_source =
        "if (parent === window) {"
        " globalThis.__documentStartTrace = ['start-all-main'];"
        " globalThis.__documentStartEnvironment = ["
        "   window === globalThis, /^https:/.test(location.href),"
        "   document.readyState];"
        " localStorage.setItem('document-start', 'ready');"
        " queueMicrotask(() => __documentStartTrace.push('start-all-main-microtask'));"
        "} else {"
        " parent.__documentStartTrace.push('start-all-frame');"
        " queueMicrotask(() => parent.__documentStartTrace.push('start-all-frame-microtask'));"
        "}";
    constexpr std::string_view all_frames_name = "storage-bootstrap.js";
    constexpr std::string_view main_only_source =
        "globalThis.__documentStartMainOnly = true;"
        "__documentStartTrace.push('start-main-only');";
    constexpr std::string_view main_only_name = "main-only-bootstrap.js";
    const std::array<webscene_document_script, 2> scripts{{
        {sizeof(webscene_document_script),
            WEBSCENE_DOCUMENT_SCRIPT_ALL_FRAMES,
            all_frames_source.data(), all_frames_source.size(),
            all_frames_name.data(), all_frames_name.size()},
        {sizeof(webscene_document_script), 0U,
            main_only_source.data(), main_only_source.size(),
            main_only_name.data(), main_only_name.size()}
    }};
    const webscene_navigation_options navigation{
        sizeof(webscene_navigation_options),
        static_cast<uint32_t>(scripts.size()),
        scripts.data()};
    constexpr std::string_view url{"https://document-start.test/index.html"};
    require(
        webscene_engine_load_url_with_options(
            engine, url.data(), url.size(), &navigation) != 0U,
        "document-start navigation was rejected");
    const auto result = evaluate(engine, R"JS(({
      trace: __documentStartTrace,
      environment: __documentStartEnvironment,
      mainStorage: __earliestStorage,
      frameStorage: __documentStartFrame.contentWindow.__frameStorage,
      mainOnlyAbsent: __documentStartFrame.contentWindow.__mainOnlyAbsent
    }))JS", "document-start-order-result.js");
    require(
        result == R"({"trace":["start-all-main","start-all-main-microtask","start-main-only","authored-main","authored-main-microtask","start-all-frame","start-all-frame-microtask","authored-frame","authored-frame-microtask"],"environment":[true,true,"loading"],"mainStorage":["object","object","ready"],"frameStorage":["object","object","ready"],"mainOnlyAbsent":true})",
        "document-start ordering, AllFrames, or earliest storage visibility regressed: "
            + result);
    require(
        evaluate(
            engine,
            "__documentStartTrace.at(-1)",
            "document-start-timer-order-result.js")
            == R"("authored-main-timer")",
        "document-start startup timer did not run after initial microtasks and frame scripts");

    constexpr std::string_view failure_source =
        "throw new Error('document start exploded');";
    constexpr std::string_view failure_name = "configured-failure-name.js";
    const webscene_document_script failure_script{
        sizeof(webscene_document_script), 0U,
        failure_source.data(), failure_source.size(),
        failure_name.data(), failure_name.size()};
    const webscene_navigation_options failure_navigation{
        sizeof(webscene_navigation_options), 1U, &failure_script};
    constexpr std::string_view failure_url{
        "https://document-start.test/failure.html"};
    webscene_engine_metrics before{};
    webscene_engine_get_metrics(engine, &before);
    require(
        webscene_engine_load_url_with_options(
            engine,
            failure_url.data(),
            failure_url.size(),
            &failure_navigation) != 0U,
        "failing document-start navigation was not queued");
    std::string diagnostic;
    webscene_engine_metrics after{};
    for (auto attempt = 0; attempt < 250; ++attempt) {
        webscene_engine_get_metrics(engine, &after);
        diagnostic = last_error(engine);
        if (after.script_errors > before.script_errors) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(
        after.script_errors > before.script_errors
            && diagnostic.find(failure_name) != std::string::npos,
        "document-start failure did not retain its configured name: "
            + diagnostic);
    require(
        evaluate(
            engine,
            "typeof globalThis.__authoredRanAfterDocumentStartFailure",
            "document-start-fail-closed-result.js") == R"("undefined")",
        "authored script ran after a document-start failure");
    const auto console_required =
        webscene_engine_take_console_message(engine, nullptr, 0U);
    std::vector<char> console(console_required == 0U ? 1U : console_required, '\0');
    if (console_required != 0U) {
        webscene_engine_take_console_message(
            engine, console.data(), console.size());
    }
    require(
        std::string(console.data()).find(failure_name) != std::string::npos,
        "document-start failure was not emitted to the console with its name");

    constexpr std::string_view frame_failure_source =
        "if (parent !== window) throw new Error('frame document start exploded');";
    constexpr std::string_view frame_failure_name =
        "configured-frame-failure-name.js";
    const webscene_document_script frame_failure_script{
        sizeof(webscene_document_script),
        WEBSCENE_DOCUMENT_SCRIPT_ALL_FRAMES,
        frame_failure_source.data(), frame_failure_source.size(),
        frame_failure_name.data(), frame_failure_name.size()};
    const webscene_navigation_options frame_failure_navigation{
        sizeof(webscene_navigation_options), 1U, &frame_failure_script};
    constexpr std::string_view frame_failure_url{
        "https://document-start.test/frame-failure.html"};
    webscene_engine_get_metrics(engine, &before);
    require(
        webscene_engine_load_url_with_options(
            engine,
            frame_failure_url.data(),
            frame_failure_url.size(),
            &frame_failure_navigation) != 0U,
        "failing frame document-start navigation was not queued");
    for (auto attempt = 0; attempt < 250; ++attempt) {
        webscene_engine_get_metrics(engine, &after);
        diagnostic = last_error(engine);
        if (after.script_errors > before.script_errors) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(
        after.script_errors > before.script_errors
            && diagnostic.find(frame_failure_name) != std::string::npos,
        "frame document-start failure did not fail navigation with its name: "
            + diagnostic);
    require(
        evaluate(
            engine,
            "typeof __failingDocumentStartFrame.contentWindow."
            "__frameAuthoredAfterFailure",
            "frame-document-start-fail-closed-result.js") == R"("undefined")",
        "frame authored script ran after its document-start failure");
    webscene_engine_destroy(engine);
}

struct navigation_barrier_server final {
    resource_server resources;
    std::atomic<uint32_t> entered{0U};
    std::atomic<bool> release{false};
};

size_t load_navigation_barrier_resource(
    void* user_data,
    uint32_t kind,
    const char* url,
    size_t url_length,
    const char* entity_tag,
    size_t entity_tag_length,
    int64_t last_modified,
    char* destination,
    size_t destination_capacity)
{
    auto& server = *static_cast<navigation_barrier_server*>(user_data);
    if (kind == WEBSCENE_RESOURCE_DOCUMENT) {
        server.entered.fetch_add(1U, std::memory_order_release);
        while (!server.release.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
    }
    return load_test_resource(
        &server.resources,
        kind,
        url,
        url_length,
        entity_tag,
        entity_tag_length,
        last_modified,
        destination,
        destination_capacity);
}

void test_four_navigation_workers_enter_startup_concurrently()
{
    constexpr size_t engine_count = 4U;
    navigation_barrier_server server{
        .resources = resource_server{
            .content = {
              {"https://navigation-barrier.test/engine-0.html", R"HTML(
                <html><body><script>
                  globalThis.__authoredEngineIdentity =
                    localStorage.getItem('engine-identity');
                </script></body></html>)HTML"},
              {"https://navigation-barrier.test/engine-1.html", R"HTML(
                <html><body><script>
                  globalThis.__authoredEngineIdentity =
                    localStorage.getItem('engine-identity');
                </script></body></html>)HTML"},
              {"https://navigation-barrier.test/engine-2.html", R"HTML(
                <html><body><script>
                  globalThis.__authoredEngineIdentity =
                    localStorage.getItem('engine-identity');
                </script></body></html>)HTML"},
              {"https://navigation-barrier.test/engine-3.html", R"HTML(
                <html><body><script>
                  globalThis.__authoredEngineIdentity =
                    localStorage.getItem('engine-identity');
                </script></body></html>)HTML"}}}};
    webscene_engine_options options{};
    options.struct_size = sizeof(options);
    options.resource_load_callback = load_navigation_barrier_resource;
    options.resource_load_user_data = &server;
    std::array<webscene_engine*, engine_count> engines{};
    for (auto& engine : engines) {
        engine = webscene_engine_create_with_options(&options);
        require(engine != nullptr, "navigation barrier engine creation failed");
    }

    std::array<std::string, engine_count> sources;
    std::array<std::string, engine_count> names;
    std::array<std::string, engine_count> urls;
    std::array<webscene_document_script, engine_count> descriptors{};
    std::array<webscene_navigation_options, engine_count> navigations{};
    for (size_t index = 0U; index < engine_count; ++index) {
        sources[index] = "localStorage.setItem('engine-identity', 'engine-"
            + std::to_string(index) + "'); globalThis.__engineIdentity = 'engine-"
            + std::to_string(index) + "';";
        names[index] = "engine-" + std::to_string(index) + "-document-start.js";
        urls[index] = "https://navigation-barrier.test/engine-"
            + std::to_string(index) + ".html";
        descriptors[index] = webscene_document_script{
            sizeof(webscene_document_script), 0U,
            sources[index].data(), sources[index].size(),
            names[index].data(), names[index].size()};
        navigations[index] = webscene_navigation_options{
            sizeof(webscene_navigation_options), 1U, &descriptors[index]};
        require(
            webscene_engine_load_url_with_options(
                engines[index],
                urls[index].data(),
                urls[index].size(),
                &navigations[index]) != 0U,
            "navigation barrier load was rejected");
    }
    for (size_t index = 0U; index < engine_count; ++index) {
        sources[index].assign(sources[index].size(), 'x');
        names[index].assign(names[index].size(), 'x');
    }
    for (auto attempt = 0;
         attempt < 500
            && server.entered.load(std::memory_order_acquire) < engine_count;
         ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(
        server.entered.load(std::memory_order_acquire) == engine_count,
        "four navigation workers did not enter document startup concurrently");
    server.release.store(true, std::memory_order_release);

    for (size_t index = 0U; index < engine_count; ++index) {
        const auto identity = "engine-" + std::to_string(index);
        const auto result = evaluate(
            engines[index],
            "[globalThis.__engineIdentity, globalThis.__authoredEngineIdentity, "
            "localStorage.getItem('engine-identity')]",
            "navigation-barrier-result.js");
        require(
            result == "[\"" + identity + "\",\"" + identity
                + "\",\"" + identity + "\"]",
            "four-engine startup leaked globals or storage: " + result);
    }

    const std::array<webscene_interop_value_v3, 2> registration_values{{
        {WEBSCENE_INTEROP_VALUE_ARRAY_V3, 0U, 0U, 1U, 0U},
        {WEBSCENE_INTEROP_VALUE_NUMBER_V3, 0U, 0U, 0U,
            std::bit_cast<uint64_t>(
                static_cast<double>(WEBSCENE_INTEROP_CALLBACK_VOID_V3))}
    }};
    const std::array<webscene_interop_edge_v3, 1> registration_edges{{
        {0U, 0U, 1U, 0U}
    }};
    const std::array<webscene_interop_value_v3, 1> empty_arguments{{
        {WEBSCENE_INTEROP_VALUE_ARRAY_V3, 0U, 0U, 0U, 0U}
    }};
    for (size_t index = 0U; index < engine_count; ++index) {
        const auto target_id = static_cast<uint64_t>(100U + index);
        const webscene_interop_invoke_request_v3 registration{
            static_cast<uint32_t>(sizeof(webscene_interop_invoke_request_v3)),
            3U,
            WEBSCENE_INTEROP_CREATE_CALLBACK_FUNCTION_V3,
            0U,
            target_id,
            nullptr,
            0U,
            nullptr,
            0U,
            registration_values.data(),
            registration_values.size(),
            registration_edges.data(),
            registration_edges.size(),
            nullptr,
            0U,
            0U,
            WEBSCENE_INTEROP_RESULT_RETAINED_HANDLE_V3};
        const auto* registered = invoke_generated(engines[index], registration);
        require(
            registered->status == WEBSCENE_INTEROP_RESULT_SUCCEEDED_V3
                && registered->values[registered->root_value_index].kind
                    == WEBSCENE_INTEROP_VALUE_HANDLE_V3,
            "four-engine callback registration failed");
        const auto callback_handle =
            registered->values[registered->root_value_index].payload;
        release_interop_result(registered);

        const webscene_interop_invoke_request_v3 invoke_callback{
            static_cast<uint32_t>(sizeof(webscene_interop_invoke_request_v3)),
            3U,
            WEBSCENE_INTEROP_INVOKE_FUNCTION_V3,
            0U,
            callback_handle,
            nullptr,
            0U,
            nullptr,
            0U,
            empty_arguments.data(),
            empty_arguments.size(),
            nullptr,
            0U,
            nullptr,
            0U,
            0U,
            WEBSCENE_INTEROP_RESULT_VOID_V3};
        const auto* invoked = invoke_generated(engines[index], invoke_callback);
        require(
            invoked->status == WEBSCENE_INTEROP_RESULT_SUCCEEDED_V3,
            "four-engine callback invocation failed");
        release_interop_result(invoked);
    }
    for (size_t index = 0U; index < engine_count; ++index) {
        const auto* callback = webscene_engine_take_callback_v3(engines[index]);
        require(
            callback != nullptr
                && callback->target_id == static_cast<uint64_t>(100U + index)
                && callback->return_kind == WEBSCENE_INTEROP_CALLBACK_VOID_V3,
            "four-engine startup leaked or reordered callback queues");
        webscene_interop_callback_release_v3(callback, callback->lease_id);
        require(
            webscene_engine_take_callback_v3(engines[index]) == nullptr,
            "four-engine callback queue retained a foreign callback");
    }
    for (auto* engine : engines) webscene_engine_destroy(engine);
}

void test_parallel_resource_prefetch()
{
    resource_server server{
        .content = {
            {"https://prefetch.test/index.html", R"(
                <html><body>
                  <script src="one.js"></script>
                  <script src="two.js"></script>
                  <script src="three.js"></script>
                </body></html>)"},
            {"https://prefetch.test/one.js", "globalThis.__prefetchValues = [1];"},
            {"https://prefetch.test/two.js", "globalThis.__prefetchValues.push(2);"},
            {"https://prefetch.test/three.js", "globalThis.__prefetchValues.push(3);"},
            {"https://prefetch.test/data.json", R"({"answer":42})"}
        }};
    const webscene_engine_options options{
        sizeof(webscene_engine_options),
        64U,
        nullptr,
        0U,
        load_test_resource,
        &server};
    auto* engine = webscene_engine_create_with_options(&options);
    require(engine != nullptr, "prefetch engine creation failed");
    const std::string url = "https://prefetch.test/index.html";
    require(
        webscene_engine_load_url(engine, url.data(), url.size()) != 0,
        "prefetch document load was rejected");
    require(
        evaluate(
            engine,
            "[document.URL, document.documentURI, document.baseURI]",
            "native-document-address-result.js")
            == R"(["https://prefetch.test/index.html","https://prefetch.test/index.html","https://prefetch.test/index.html"])",
        "the loaded document did not expose its fetched address through the Document API");
    const auto result = evaluate(
        engine,
        R"JS((() => {
          const scripts = Array.from(document.getElementsByTagName('script'));
          return {
            values: globalThis.__prefetchValues,
            sources: scripts.map(script => script.getAttribute('src')),
            parented: scripts.every(script => script.parentNode === document.body)
          };
        })())JS",
        "native-parallel-prefetch-result.js");
    require(
        result == R"({"values":[1,2,3],"sources":["one.js","two.js","three.js"],"parented":true})",
        "prefetched scripts or their DOM nodes did not preserve document order");
    execute(
        engine,
        R"JS(
          globalThis.__fetchResult = null;
          fetch("data.json")
            .then(response => response.json().then(value => ({
              ok: response.ok,
              status: response.status,
              answer: value.answer
            })))
            .then(value => { globalThis.__fetchResult = value; });
        )JS",
        "native-fetch-resource.js");
    require(
        evaluate_until_equals(
            engine,
            "globalThis.__fetchResult",
            "native-fetch-resource-result.js",
            R"({"ok":true,"status":200,"answer":42})")
            == R"({"ok":true,"status":200,"answer":42})",
        "fetch did not resolve a relative URL through the host resource loader");
    require(
        server.peak.load(std::memory_order_relaxed) >= 2,
        "external document resources were not fetched concurrently");
    webscene_engine_destroy(engine);
}

#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8_INSPECTOR)
void test_inspector_navigation_resets_context_group()
{
    resource_server server{
        .content = {
            {"https://inspector-navigation.test/first.html", R"HTML(
                <html><body><script>
                  globalThis.__inspectorNavigationObject = { value: 41 };
                </script></body></html>)HTML"},
            {"https://inspector-navigation.test/second.html", R"HTML(
                <html><body><script>
                  globalThis.__inspectorNavigationResult = 42;
                </script></body></html>)HTML"}
        }};
    const webscene_engine_options options{
        sizeof(webscene_engine_options),
        64U,
        nullptr,
        0U,
        load_test_resource,
        &server};
    auto* engine = webscene_engine_create_with_options(&options);
    require(engine != nullptr, "Inspector navigation engine creation failed");
    for (auto attempt = 0;
         attempt < 500 && webscene_engine_inspector_is_available(engine) == 0;
         ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(
        webscene_engine_inspector_is_available(engine) != 0,
        "Inspector navigation engine did not publish availability");

    inspector_test_messages messages;
    const auto session_id = webscene_engine_inspector_connect(
        engine,
        receive_inspector_test_message,
        &messages,
        0U);
    require(session_id != 0U, "Inspector navigation session creation failed");
    dispatch_inspector_test_message(
        engine,
        session_id,
        R"({"id":1,"method":"Runtime.enable"})");
    dispatch_inspector_test_message(
        engine,
        session_id,
        R"({"id":2,"method":"Debugger.enable"})");
    require(
        wait_for_inspector_message(messages, R"("id":1)"),
        "Inspector navigation Runtime.enable did not respond");
    require(
        wait_for_inspector_message(messages, R"("id":2)"),
        "Inspector navigation Debugger.enable did not respond");

    const std::string first_url = "https://inspector-navigation.test/first.html";
    require(
        webscene_engine_load_url(engine, first_url.data(), first_url.size()) != 0,
        "First Inspector navigation document load was rejected");
    require(
        wait_for_inspector_message(
            messages,
            R"("method":"Debugger.scriptParsed")",
            first_url),
        "First navigation script was not reported to Inspector");
    dispatch_inspector_test_message(
        engine,
        session_id,
        R"({"id":3,"method":"Runtime.evaluate","params":{"expression":"globalThis.__inspectorNavigationObject","returnByValue":false,"objectGroup":"navigation-test"}})");
    require(
        wait_for_inspector_message(messages, R"("id":3)", R"("objectId")"),
        "Inspector navigation fixture did not return a remote object");
    const auto object_response = find_inspector_message(
        messages,
        R"("id":3)",
        R"("objectId")");
    const auto stale_object_id = inspector_json_string(object_response, "objectId");
    require(!stale_object_id.empty(), "Inspector navigation object id was empty");

    const auto contexts_cleared_before = count_inspector_messages(
        messages,
        R"("method":"Runtime.executionContextsCleared")");
    const std::string second_url = "https://inspector-navigation.test/second.html";
    require(
        webscene_engine_load_url(engine, second_url.data(), second_url.size()) != 0,
        "Second Inspector navigation document load was rejected");
    require(
        wait_for_inspector_message_count(
            messages,
            R"("method":"Runtime.executionContextsCleared")",
            contexts_cleared_before + 1U),
        "Navigation did not clear the Inspector execution-context group");
    require(
        wait_for_inspector_message(
            messages,
            R"("method":"Runtime.executionContextCreated")",
            second_url),
        "Navigation did not publish the replacement context origin");
    require(
        wait_for_inspector_message(
            messages,
            R"("method":"Debugger.scriptParsed")",
            second_url),
        "Second navigation script was not reported to Inspector");
    dispatch_inspector_test_message(
        engine,
        session_id,
        std::string{"{\"id\":4,\"method\":\"Runtime.getProperties\",\"params\":{\"objectId\":\""}
            + stale_object_id + "\",\"ownProperties\":true}}" );
    require(
        wait_for_inspector_message(messages, R"("id":4)", R"("error")"),
        "A pre-navigation V8 remote object remained usable after context reset");
    require(
        evaluate(
            engine,
            "globalThis.__inspectorNavigationResult",
            "inspector-navigation-result.js") == "42",
        "Replacement document did not finish after Inspector context reset");
    require(
        webscene_engine_inspector_disconnect(engine, session_id) != 0,
        "Inspector navigation session disconnect was rejected");
    webscene_engine_destroy(engine);
}
#endif

void test_navigation_replaces_top_level_realm()
{
    constexpr std::string_view page = R"HTML(<!doctype html><html><body><script>
      const baseUrl = location.href;
      const loadNumber = Number(sessionStorage.getItem('reload-count') || 0) + 1;
      const staleGlobal = '__retiredRealmMarker' in globalThis;
      if (loadNumber === 1) {
        globalThis.__retiredRealmMarker = true;
        localStorage.setItem('reload-local', 'preserved');
        document.cookie = 'reload-cookie=preserved; Path=/';
        addEventListener('retired-realm-probe', () =>
          localStorage.setItem('retired-listener-fired', 'yes'));
        const retiredControl = document.createElement('input');
        retiredControl.id = 'retired-control';
        document.body.appendChild(retiredControl);
        retiredControl.focus();
        retiredControl.setPointerCapture(1);
        document.body.className = 'retired-style-root';
        const retiredStyle = document.createElement('style');
        retiredStyle.textContent =
          '.retired-style-root { color: rgb(1, 2, 3); }';
        document.head.appendChild(retiredStyle);
        customElements.define('retirement-probe', class extends HTMLElement {
          disconnectedCallback() {
            setTimeout(() =>
              localStorage.setItem('retired-disconnect-timer-fired', 'yes'), 60000);
            fetch('blocked-fetch.json').then(() =>
              localStorage.setItem('retired-disconnect-fetch-fired', 'yes'));
            __webSceneIndexedDBStorage(
              'store', 'retired-disconnect-navigation', 1n,
              new Uint8Array(2 * 1024 * 1024))
              .then(() => localStorage.setItem(
                'retired-disconnect-indexeddb-fired', 'yes'))
              .catch(() => {});
            const channel = new MessageChannel();
            channel.port1.onmessage = () =>
              localStorage.setItem('retired-disconnect-port-fired', 'yes');
            channel.port2.postMessage('retired');
            const worker = new Worker('retired-worker.js');
            worker.onmessage = () =>
              localStorage.setItem('retired-disconnect-worker-fired', 'yes');
            const socket = new WebSocket('ws://127.0.0.1:1/retired');
            socket.onopen = () =>
              localStorage.setItem('retired-disconnect-websocket-fired', 'yes');
          }
        });
        document.body.appendChild(document.createElement('retirement-probe'));
        const frame = document.createElement('iframe');
        frame.src = 'frame-seed.html';
        document.body.appendChild(frame);
        const image = document.createElement('img');
        image.src = 'slow-data.json?image';
        document.body.appendChild(image);
        setTimeout(() =>
          localStorage.setItem('retired-timer-fired', 'yes'), 60000);
      } else {
        dispatchEvent(new Event('retired-realm-probe'));
        globalThis.__reloadRealmResult = JSON.stringify({
          baseUrl,
          loadNumber,
          staleGlobal,
          staleListener: localStorage.getItem('retired-listener-fired'),
          local: localStorage.getItem('reload-local'),
          cookie: document.cookie.includes('reload-cookie=preserved')
        });
      }
      sessionStorage.setItem('reload-count', String(loadNumber));
    </script></body></html>)HTML";
    resource_server server{
        .content = {
            {"https://reload-realm.test/index.html", std::string(page)},
            {"https://reload-realm.test/slow-data.json", R"JSON({"ok":true})JSON"},
            {"https://reload-realm.test/blocked-frame.html",
                "<!doctype html><html><body>blocked frame</body></html>"},
            {"https://reload-realm.test/blocked-fetch.json", R"JSON({"ok":true})JSON"},
            {"https://reload-realm.test/retired-worker.js",
                "postMessage('retired-worker-message');"},
            {"https://reload-realm.test/frame-seed.html", R"HTML(<!doctype html><html><body><script>
              localStorage.setItem('frame-seeded', 'yes');
              globalThis.seedRetiredFrameTasks = () => {
                setTimeout(() =>
                  localStorage.setItem('retired-frame-timer-fired', 'yes'), 60000);
                fetch('blocked-fetch.json').then(() =>
                  localStorage.setItem('retired-frame-fetch-fired', 'yes'));
                __webSceneIndexedDBStorage(
                  'store', 'retired-frame-navigation', 1n,
                  new Uint8Array(2 * 1024 * 1024))
                  .then(() =>
                    localStorage.setItem('retired-frame-indexeddb-fired', 'yes'))
                  .catch(() => {});
              };
            </script></body></html>)HTML"},
            {"https://other-realm.test/index.html", R"HTML(<!doctype html><html><body><script>
              globalThis.__crossOriginStorageResult = JSON.stringify({
                session: sessionStorage.getItem('reload-count'),
                local: localStorage.getItem('reload-local'),
                cookie: document.cookie.includes('reload-cookie=preserved')
              });
              sessionStorage.setItem('other-session', 'isolated');
              localStorage.setItem('other-local', 'isolated');
            </script></body></html>)HTML"}}};
    const auto storage_root = std::filesystem::temp_directory_path()
        / ("webscene-navigation-realm-"
            + std::to_string(std::chrono::steady_clock::now()
                .time_since_epoch().count()));
    std::filesystem::create_directories(storage_root);
    const auto storage_root_text = storage_root.string();
    constexpr std::string_view storage_partition{"navigation-realm"};
    webscene_engine_options options{};
    options.struct_size = sizeof(options);
    options.resource_load_callback = load_test_resource;
    options.resource_load_user_data = &server;
    options.storage_directory = storage_root_text.data();
    options.storage_directory_length = storage_root_text.size();
    options.storage_partition_key = storage_partition.data();
    options.storage_partition_key_length = storage_partition.size();
    options.storage_quota_bytes = 8U * 1024U * 1024U;
    auto* engine = webscene_engine_create_with_options(&options);
    require(engine != nullptr, "Reload realm engine creation failed");
    const std::string url = "https://reload-realm.test/index.html";
    require(
        webscene_engine_load_url(engine, url.data(), url.size()) != 0,
        "Initial reload realm document load was rejected");
    require(evaluate(engine, "true", "initial-reload-realm-barrier.js") == "true",
        "Initial reload realm document did not finish");
    require(
        evaluate_until_equals(engine,
            "localStorage.getItem('frame-seeded')",
            "frame-seed-barrier.js", R"("yes")", 2500) == R"("yes")",
        "Same-origin frame did not seed its retirement callbacks");
    server.block_named_resource.store(true, std::memory_order_release);
    execute_and_wait(engine, R"JS(
      document.querySelector('iframe').contentWindow.seedRetiredFrameTasks();
      fetch('blocked-fetch.json').then(() =>
        localStorage.setItem('retired-fetch-fired', 'yes'));
      __webSceneIndexedDBStorage(
        'store', 'retired-navigation', 1n, new Uint8Array(2 * 1024 * 1024))
        .then(() => localStorage.setItem('retired-indexeddb-fired', 'yes'))
        .catch(() => {});
    )JS", "seed-retired-navigation-async-state.js");
    const auto blocked_wait_started = std::chrono::steady_clock::now();
    while (server.active.load(std::memory_order_acquire) == 0
        && std::chrono::steady_clock::now() - blocked_wait_started
            < std::chrono::seconds(2)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(server.active.load(std::memory_order_acquire) > 0,
        "Blocked retirement resource did not enter the host loader");
    webscene_engine_memory_metrics initial_memory{
        sizeof(webscene_engine_memory_metrics)};
    require(webscene_engine_get_memory_metrics(engine, &initial_memory) != 0,
        "Initial reload realm memory metrics were unavailable");
    constexpr unsigned load_count = 32U;
    const auto started = std::chrono::steady_clock::now();
    for (unsigned load = 1U; load < load_count; ++load) {
        const auto replacement_started = std::chrono::steady_clock::now();
        require(
            webscene_engine_load_url(engine, url.data(), url.size()) != 0,
            "Replacement reload realm document load #"
                + std::to_string(load + 1U) + " was rejected: "
                + last_error(engine));
        require(evaluate(engine, "true", "reload-realm-barrier.js") == "true",
            "Replacement reload realm document did not finish");
        if (load == 1U) {
            const auto replacement_elapsed = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - replacement_started).count();
            server.release_blocked_resource.store(true, std::memory_order_release);
            require(replacement_elapsed < 0.25,
                "Navigation blocked on a retired iframe preparation");
        }
    }
    const auto elapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - started).count();
    require(elapsed < 10.0,
        "31 replacement realms exceeded the ten-second lifecycle budget");
    const auto result = evaluate(engine, "globalThis.__reloadRealmResult",
        "reload-realm-result.js");
    require(
        result
            == R"JSON("{\"baseUrl\":\"https://reload-realm.test/index.html\",\"loadNumber\":32,\"staleGlobal\":false,\"staleListener\":null,\"local\":\"preserved\",\"cookie\":true}")JSON",
        "Navigation reused top-level lexical/global/listener state or lost browsing-session state: "
            + result);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    const auto retired_state = evaluate(engine, R"JS(JSON.stringify({
          timer: localStorage.getItem('retired-timer-fired'),
          fetch: localStorage.getItem('retired-fetch-fired'),
          indexeddb: localStorage.getItem('retired-indexeddb-fired'),
          frameTimer: localStorage.getItem('retired-frame-timer-fired'),
          frameFetch: localStorage.getItem('retired-frame-fetch-fired'),
          frameIndexeddb: localStorage.getItem('retired-frame-indexeddb-fired'),
          disconnectTimer: localStorage.getItem('retired-disconnect-timer-fired'),
          disconnectFetch: localStorage.getItem('retired-disconnect-fetch-fired'),
          disconnectIndexeddb: localStorage.getItem('retired-disconnect-indexeddb-fired'),
          disconnectPort: localStorage.getItem('retired-disconnect-port-fired'),
          disconnectWorker: localStorage.getItem('retired-disconnect-worker-fired'),
          disconnectWebSocket: localStorage.getItem('retired-disconnect-websocket-fired'),
          active: document.activeElement?.id || '',
          color: getComputedStyle(document.body).color
        }))JS", "retired-navigation-state-result.js");
    require(
        retired_state
            == R"JSON("{\"timer\":null,\"fetch\":null,\"indexeddb\":null,\"frameTimer\":null,\"frameFetch\":null,\"frameIndexeddb\":null,\"disconnectTimer\":null,\"disconnectFetch\":null,\"disconnectIndexeddb\":null,\"disconnectPort\":null,\"disconnectWorker\":null,\"disconnectWebSocket\":null,\"active\":\"\",\"color\":\"rgb(0, 0, 0)\"}")JSON",
        "A retired async callback, input target, or style state survived navigation: "
            + retired_state);

    const std::string other_url = "https://other-realm.test/index.html";
    require(
        webscene_engine_load_url(engine, other_url.data(), other_url.size()) != 0,
        "Cross-origin replacement document load was rejected");
    require(
        evaluate(engine, "globalThis.__crossOriginStorageResult",
            "cross-origin-storage-result.js")
            == R"JSON("{\"session\":null,\"local\":null,\"cookie\":false}")JSON",
        "Top-level Storage or cookies leaked across origins");
    require(
        webscene_engine_load_url(engine, url.data(), url.size()) != 0,
        "Original-origin restoration document load was rejected");
    require(
        evaluate(engine, R"JS(JSON.stringify({
          reload: sessionStorage.getItem('reload-count'),
          local: localStorage.getItem('reload-local'),
          otherSession: sessionStorage.getItem('other-session'),
          otherLocal: localStorage.getItem('other-local')
        }))JS", "restored-origin-storage-result.js")
            == R"JSON("{\"reload\":\"33\",\"local\":\"preserved\",\"otherSession\":null,\"otherLocal\":null}")JSON",
        "Returning to an origin did not restore its isolated Storage state");
    require(webscene_engine_request_low_memory(engine) != 0,
        "Reload realm low-memory request was rejected");
    webscene_engine_memory_metrics final_memory{
        sizeof(webscene_engine_memory_metrics)};
    for (auto attempt = 0; attempt < 500; ++attempt) {
        require(webscene_engine_get_memory_metrics(engine, &final_memory) != 0,
            "Reload realm final memory metrics were unavailable");
        if (final_memory.low_memory_notifications
            > initial_memory.low_memory_notifications) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(
        final_memory.low_memory_notifications
            > initial_memory.low_memory_notifications,
        "Reload realm low-memory collection did not complete");
    require(
        final_memory.v8_used_heap_bytes
            <= initial_memory.v8_used_heap_bytes + 32U * 1024U * 1024U,
        "Repeated realm replacement retained more than 32 MiB of V8 heap");
    std::cout << "Navigation realm gate: reloads=" << load_count - 1U
              << " elapsed=" << elapsed << "s heapBefore="
              << initial_memory.v8_used_heap_bytes << " heapAfter="
              << final_memory.v8_used_heap_bytes << " listenersBefore="
              << initial_memory.native_event_listener_count
              << " listenersAfter=" << final_memory.native_event_listener_count
              << "\n";
    webscene_engine_destroy(engine);
    std::error_code cleanup_error;
    std::filesystem::remove_all(storage_root, cleanup_error);
}

void test_document_script_failure_remains_diagnostic()
{
    resource_server server{
        .content = {
            {"https://script-error.test/index.html", R"(
                <html><body><script>throw new Error('release page failure');</script></body></html>)"}
        }};
    const webscene_engine_options options{
        sizeof(webscene_engine_options),
        64U,
        nullptr,
        0U,
        load_test_resource,
        &server};
    auto* engine = webscene_engine_create_with_options(&options);
    require(engine != nullptr, "script-error engine creation failed");
    const std::string url = "https://script-error.test/index.html";
    require(
        webscene_engine_load_url(engine, url.data(), url.size()) != 0,
        "script-error document load was rejected");

    webscene_engine_metrics metrics{};
    auto diagnostic = std::string{};
    for (auto attempt = 0; attempt < 200; ++attempt) {
        webscene_engine_get_metrics(engine, &metrics);
        diagnostic = last_error(engine);
        if (metrics.frame_script_errors != 0
            && diagnostic.find("release page failure") != std::string::npos) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(metrics.frame_script_errors == 1,
        "document script failure was not counted");
    require(diagnostic.find("release page failure") != std::string::npos,
        "document script failure was cleared before the host could diagnose it: "
            + diagnostic);
    webscene_engine_destroy(engine);
}

void test_outer_document_lifecycle_for_editor_bootstrap()
{
    resource_server server{
        .content = {
            {"https://editor-lifecycle.test/index.html", R"HTML(
                <!doctype html>
                <html><head>
                  <script>
                    globalThis.__editorLifecycle = [
                      ['script', document.readyState]
                    ];
                    document.addEventListener('DOMContentLoaded', () => {
                      __editorLifecycle.push(['domcontentloaded', document.readyState]);
                    });
                    window.addEventListener('DOMContentLoaded', () => {
                      __editorLifecycle.push(['window-domcontentloaded', document.readyState]);
                    });
                    window.addEventListener('load', () => {
                      __editorLifecycle.push(['load', document.readyState]);
                    });
                  </script>
                  <script defer>
                    __editorLifecycle.push(['defer', document.readyState]);
                  </script>
                </head><body><div id="editor"></div></body></html>
            )HTML"}
        }};
    const webscene_engine_options options{
        sizeof(webscene_engine_options),
        64U,
        nullptr,
        0U,
        load_test_resource,
        &server};
    auto* engine = webscene_engine_create_with_options(&options);
    require(engine != nullptr, "editor lifecycle engine creation failed");
    const std::string url = "https://editor-lifecycle.test/index.html";
    require(
        webscene_engine_load_url(engine, url.data(), url.size()) != 0,
        "editor lifecycle document load was rejected");
    const auto result = evaluate(
        engine,
        "({ state: document.readyState, events: __editorLifecycle })",
        "native-editor-lifecycle-result.js");
    require(
        result == R"({"state":"complete","events":[["script","loading"],["defer","interactive"],["domcontentloaded","interactive"],["window-domcontentloaded","interactive"],["load","complete"]]})",
        "outer document lifecycle did not follow browser ordering: " + result);
    webscene_engine_destroy(engine);
}

void test_event_listener_exceptions_do_not_abort_document_load()
{
    resource_server server{
        .content = {
            {"https://listener-error.test/index.html", R"HTML(
                <!doctype html>
                <html><head>
                  <script>
                    globalThis.__listenerEvents = [];
                    document.addEventListener('DOMContentLoaded', () => {
                      __listenerEvents.push('before-error');
                      throw new Error('expected listener failure');
                    });
                    document.addEventListener('DOMContentLoaded', () => {
                      __listenerEvents.push('after-error');
                    });
                    window.addEventListener('load', () => {
                      __listenerEvents.push('load');
                    });
                  </script>
                </head><body><div id="content">loaded</div></body></html>
            )HTML"}
        }};
    const webscene_engine_options options{
        sizeof(webscene_engine_options),
        64U,
        nullptr,
        0U,
        load_test_resource,
        &server};
    auto* engine = webscene_engine_create_with_options(&options);
    require(engine != nullptr, "listener-error engine creation failed");
    const std::string url = "https://listener-error.test/index.html";
    require(
        webscene_engine_load_url(engine, url.data(), url.size()) != 0,
        "event listener exception incorrectly rejected the document load");
    const auto result = evaluate(
        engine,
        "({ state: document.readyState, events: __listenerEvents })",
        "native-listener-error-result.js");
    require(
        result == R"({"state":"complete","events":["before-error","after-error","load"]})",
        "event listener exception stopped lifecycle dispatch: " + result);

    webscene_engine_metrics metrics{};
    webscene_engine_get_metrics(engine, &metrics);
    require(metrics.frame_script_errors == 1,
        "event listener exception was not retained as a diagnostic");
    webscene_engine_destroy(engine);
}

void test_timer_error_handler_preserves_later_tasks()
{
    auto* engine = webscene_engine_create(64);
    require(engine != nullptr, "timer-error engine creation failed");
    const auto setup = evaluate(engine, R"JS(
        (() => {
          globalThis.__timerErrorLifecycle = [];
          window.onerror = (message, source, line, column, error) => {
            __timerErrorLifecycle.push([
              'error',
              String(message).includes('expected timer failure'),
              error && error.message,
              typeof source === 'string',
              line > 0,
              column > 0
            ]);
            window.onerror = null;
            return true;
          };
          setTimeout(() => { throw new Error('expected timer failure'); }, 0);
          setTimeout(() => { __timerErrorLifecycle.push('after'); }, 0);
          return true;
        })()
    )JS", "native-timer-error-handler-setup.js");
    require(setup == "true", "handled timer error rejected task drain: " + setup);
    const auto result = evaluate(
        engine,
        "globalThis.__timerErrorLifecycle",
        "native-timer-error-handler-result.js");
    require(
        result == R"([["error",true,"expected timer failure",true,true,true],"after"])",
        "handled timer error stopped later task execution: " + result);
    webscene_engine_destroy(engine);

    auto* unhandled_engine = webscene_engine_create(64);
    require(unhandled_engine != nullptr, "unhandled timer-error engine creation failed");
    webscene_engine_metrics before{};
    webscene_engine_get_metrics(unhandled_engine, &before);
    constexpr std::string_view unhandled_source =
        "setTimeout(() => { throw new Error('unhandled timer failure'); }, 0); true";
    require(
        webscene_engine_execute_script(
            unhandled_engine,
            unhandled_source.data(),
            unhandled_source.size(),
            "native-unhandled-timer-error.js",
            sizeof("native-unhandled-timer-error.js") - 1U) != 0,
        "unhandled timer-error script was rejected before execution");
    auto error = std::string{};
    for (auto attempt = 0; attempt < 250; ++attempt) {
        webscene_engine_metrics after{};
        webscene_engine_get_metrics(unhandled_engine, &after);
        if (after.script_errors > before.script_errors) {
            error = last_error(unhandled_engine);
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(
        error.find("timer task failed") != std::string::npos
            && error.find("unhandled timer failure") != std::string::npos,
        "timer exception without Window.onerror no longer failed closed: " + error);
    webscene_engine_destroy(unhandled_engine);
}

void test_dom_implementation_create_html_document()
{
    resource_server server{
        .content = {
            {"https://detached-document.test/index.html", R"(
                <html data-shell="live"><head></head><body>
                  <main id="live-root"></main><a href="/release">Release</a>
                </body></html>)"}
        }};
    const webscene_engine_options options{
        sizeof(webscene_engine_options),
        64U,
        nullptr,
        0U,
        load_test_resource,
        &server};
    auto* engine = webscene_engine_create_with_options(&options);
    require(engine != nullptr, "detached-document engine creation failed");
    const std::string url = "https://detached-document.test/index.html";
    require(
        webscene_engine_load_url(engine, url.data(), url.size()) != 0,
        "detached-document load was rejected");

    const auto result = evaluate(
        engine,
        R"JS((() => {
          const implementation = document.implementation;
          const detached = implementation.createHTMLDocument('Release notes');
          detached.body.innerHTML = '<form class="entry"></form><form class="entry"></form>';
          const detachedNode = detached.createElement('aside');
          detachedNode.className = 'future-release';
          detached.body.appendChild(detachedNode);
          const style = detached.createElement('a').style;
          const cssomAliasPresent = 'transition-timing-function' in style;
          style['transition-timing-function'] = 'ease-in-out';
          const cookieDescriptor = Object.getOwnPropertyDescriptor(Document.prototype, 'cookie');
          let cookieCache = document.cookie;
          Object.defineProperty(document, 'cookie', {
            get() { return cookieCache; },
            set(value) {
              cookieDescriptor.set.call(document, value);
              cookieCache = cookieDescriptor.get.call(document);
            },
            configurable: true
          });
          document.cookie = 'release_theme=dark; Path=/; SameSite=Lax';
          document.cookie = 'release_seen=1; Path=/';
          document.cookie = 'release_theme=light; Path=/';
          const wrappedCookieAccessors = typeof cookieDescriptor.get === 'function'
            && typeof cookieDescriptor.set === 'function'
            && document.cookie === 'release_theme=light; release_seen=1';
          delete document.cookie;
          return {
            stableImplementation: implementation === document.implementation,
            distinct: detached !== document,
            nodeType: detached.nodeType,
            defaultView: detached.defaultView,
            location: detached.location,
            documentElement: detached.documentElement.tagName,
            head: detached.head.tagName,
            body: detached.body.tagName,
            title: detached.head.querySelector('title').textContent,
            forms: detached.getElementsByTagName('form').length,
            entries: detached.getElementsByClassName('entry').length,
            futureRelease: detached.querySelector('.future-release') === detachedNode,
            cssomAliasPresent,
            cssomAliasValue: style.transitionTimingFunction,
            liveDocumentBindings: document.documentElement.getAttribute('data-shell') === 'live'
              && document.body.tagName === 'BODY',
            liveLocation: document.location === window.location
              && document.location.search === ''
              && document.location.pathname === '/index.html',
            liveLinks: document.links.length === 1
              && document.links.item(0).textContent === 'Release',
            liveCookies: document.cookie === 'release_theme=light; release_seen=1',
            wrappedCookieAccessors,
            detachedCookies: detached.cookie,
            liveRootUnaffected: document.getElementById('live-root') !== null
              && document.querySelector('.future-release') === null,
            address: [detached.URL, detached.documentURI, detached.baseURI]
          };
        })())JS",
        "native-create-html-document-result.js");
    require(
        result == R"({"stableImplementation":true,"distinct":true,"nodeType":9,"defaultView":null,"location":null,"documentElement":"HTML","head":"HEAD","body":"BODY","title":"Release notes","forms":2,"entries":2,"futureRelease":true,"cssomAliasPresent":true,"cssomAliasValue":"ease-in-out","liveDocumentBindings":true,"liveLocation":true,"liveLinks":true,"liveCookies":true,"wrappedCookieAccessors":true,"detachedCookies":"","liveRootUnaffected":true,"address":["about:blank","about:blank","about:blank"]})",
        "DOMImplementation.createHTMLDocument did not create an isolated usable document: " + result);
    webscene_engine_destroy(engine);
}

void test_loaded_document_keeps_html_and_body_cascade_distinct()
{
    resource_server server{
        .content = {
            {"https://root-cascade.test/index.html", R"(
                <!doctype html>
                <html data-root="html"><head></head><body data-root="body">
                  <main id="content" style="height: 1200px"></main>
                  <style>
                    html {
                      z-index: inherit;
                      position: inherit;
                      overflow: inherit;
                      background-color: inherit;
                    }
                    body { overflow: scroll; background-color: #ffc0cb; }
                  </style>
                </body></html>)"}
        }};
    const webscene_engine_options options{
        sizeof(webscene_engine_options),
        64U,
        nullptr,
        0U,
        load_test_resource,
        &server};
    auto* engine = webscene_engine_create_with_options(&options);
    require(engine != nullptr, "root-cascade engine creation failed");
    const std::string url = "https://root-cascade.test/index.html";
    require(
        webscene_engine_load_url(engine, url.data(), url.size()) != 0,
        "root-cascade document load was rejected");

    const auto result = evaluate(
        engine,
        R"JS((() => {
          const root = document.documentElement;
          const body = document.body;
          const rootStyle = getComputedStyle(root);
          const bodyStyle = getComputedStyle(body);
          const rootHasScrollRange = root.clientHeight > 0
            && root.scrollHeight > root.clientHeight;
          root.scrollTop = 23;
          const rootScrolls = root.scrollTop === 23;
          root.scrollTop = 0;
          root.style.zIndex = '7';
          const authoredZ = getComputedStyle(root).zIndex;
          root.style.removeProperty('z-index');
          return {
            tags: [root.tagName, document.head.tagName, body.tagName],
            parented: body.parentNode === root && document.head.parentNode === root,
            distinct: root !== body,
            scrollingElement: document.scrollingElement === root,
            rootHasScrollRange,
            rootScrolls,
            roots: document.querySelectorAll('html').length,
            bodies: document.querySelectorAll('body').length,
            attributes: [root.getAttribute('data-root'), body.getAttribute('data-root')],
            rootStyle: [rootStyle.zIndex, rootStyle.position, rootStyle.overflow,
              rootStyle.backgroundColor],
            bodyStyle: [bodyStyle.overflow, bodyStyle.backgroundColor],
            authoredZ,
            restoredZ: getComputedStyle(root).zIndex
          };
        })())JS",
        "native-root-cascade-result.js");
    require(
        result == R"JSON({"tags":["HTML","HEAD","BODY"],"parented":true,"distinct":true,"scrollingElement":true,"rootHasScrollRange":true,"rootScrolls":true,"roots":1,"bodies":1,"attributes":["html","body"],"rootStyle":["auto","static","visible","rgba(0, 0, 0, 0)"],"bodyStyle":["scroll","rgb(255, 192, 203)"],"authoredZ":"7","restoredZ":"auto"})JSON",
        "loaded HTML/BODY cascade or computed root initials were collapsed: " + result);
    const auto consumed_before_wheel = consumed_input_count(engine);
    wheel_input(engine, 10, 10, 111, 34U);
    wait_for_consumed_inputs(
        engine,
        consumed_before_wheel + 1,
        "loaded-document root wheel input was not consumed");
    const auto wheel_result = evaluate(
        engine,
        R"JS(({
          scrollTop: document.scrollingElement.scrollTop,
          contentTop: document.getElementById('content').getBoundingClientRect().top
        }))JS",
        "native-root-cascade-wheel-result.js");
    require(
        wheel_result == R"({"scrollTop":111,"contentTop":-111})",
        "loaded-document wheel input did not use the scrollingElement viewport owner: "
            + wheel_result);
    webscene_engine_destroy(engine);
}

void test_relative_stylesheet_background_uses_stylesheet_address()
{
    resource_server server{
        .content = {
            {"https://relative-style.test/app/index.html", R"(
                <html><head>
                  <link rel="stylesheet" href="assets/theme.css">
                </head><body><div class="badge"></div></body></html>)"},
            {"https://relative-style.test/app/assets/theme.css", R"(
                .badge {
                  width: 100px;
                  height: 60px;
                  background-image: url("../../images/badge.svg");
                  background-repeat: no-repeat;
                  background-position: 0 2px;
                  background-size: 48px auto;
                })"},
            {"https://relative-style.test/images/badge.svg",
                R"(<svg xmlns="http://www.w3.org/2000/svg" width="8" height="4"><rect width="8" height="4" fill="#00ff00"/></svg>)"}
        }};
    const webscene_engine_options options{
        sizeof(webscene_engine_options),
        64U,
        nullptr,
        0U,
        load_test_resource,
        &server};
    auto* engine = webscene_engine_create_with_options(&options);
    require(engine != nullptr, "relative stylesheet resource engine creation failed");
    const std::string url = "https://relative-style.test/app/index.html";
    require(
        webscene_engine_load_url(engine, url.data(), url.size()) != 0,
        "relative stylesheet resource document load failed");
    const auto computed = evaluate(engine, R"JS((() => {
      const style = getComputedStyle(document.querySelector('.badge'));
      return {
        image: style.backgroundImage,
        repeat: style.backgroundRepeat,
        position: style.backgroundPosition,
        size: style.backgroundSize
      };
    })())JS", "relative-stylesheet-background-cssom.js");
    require(
        computed.find("https://relative-style.test/images/badge.svg") != std::string::npos
            && computed.find(R"("repeat":"no-repeat")") != std::string::npos,
        "relative CSS url() was not resolved against the stylesheet address: " + computed);

    webscene_engine_request_scene_checkpoint(engine);
    auto found = false;
    for (auto attempt = 0; attempt < 100; ++attempt) {
        const auto* scene = webscene_engine_acquire_latest_scene(engine);
        if (scene != nullptr) {
            for (uint32_t index = 0; index < scene->header.command_count; ++index) {
                const auto& command = scene->commands[index];
                if (command.kind == 6U
                    && std::abs(command.y) < 0.01F
                    && std::abs(command.width - 100.0F) < 0.01F
                    && std::abs(command.height - 60.0F) < 0.01F
                    && command.flags < scene->string_count) {
                    const auto& descriptor = scene->strings[command.flags];
                    const std::string_view resource(
                        scene->string_bytes + descriptor.byte_offset,
                        descriptor.byte_length);
                    found = resource.starts_with("webscene-bg-svg-v1\t0 0 8 4\t")
                        && resource.find("\tno-repeat\t0 2px\t48px auto\t")
                            != std::string_view::npos;
                    if (found) break;
                }
            }
            webscene_scene_acknowledge(scene);
            webscene_scene_release(scene);
        }
        if (found) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(found, "stylesheet-relative SVG background did not reach the native scene");
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
    const auto report = feature_use(engine);
    require(
        report.find(R"("feature":"load:failed")") == std::string::npos,
        "stylesheet-relative background resource was reported as a failed load");
    require(
        report.find(R"("feature":"function:url","classification":"partially-supported","count":1,"semanticSlice":"first URL-backed SVG CSS background layer")")
            != std::string::npos,
        "CSS url() was not reported as the bounded implemented background slice");
#endif
    webscene_engine_destroy(engine);
}

void test_resource_cache_reuse_across_engine_generations()
{
    const auto cache_directory = std::filesystem::temp_directory_path()
        / ("webscene-native-resource-memory-cache-test-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(cache_directory);
    const auto cache_path = cache_directory.string();
    resource_server server{
        .content = {
            {"https://resource-cache.test/index.html", R"(
                <html><body><script src="value.js"></script></body></html>)"},
            {"https://resource-cache.test/value.js",
                "globalThis.__cachedResourceValue = 42; globalThis.__webSceneComponentReady = true;"}
        }};
    const webscene_engine_options options{
        sizeof(webscene_engine_options),
        64U,
        cache_path.data(),
        cache_path.size(),
        load_test_resource,
        &server};
    const std::string url = "https://resource-cache.test/index.html";

    auto* first = webscene_engine_create_with_options(&options);
    require(first != nullptr, "first resource-cache engine creation failed");
    require(
        webscene_engine_load_url(first, url.data(), url.size()) != 0,
        "first resource-cache document load failed");
    require(
        evaluate(first, "globalThis.__cachedResourceValue", "resource-cache-first.js") == "42",
        "first resource-cache script did not execute");
    const auto requests_after_first =
        server.requests.load(std::memory_order_relaxed);
    webscene_engine_memory_metrics warm_memory{
        sizeof(webscene_engine_memory_metrics)};
    require(webscene_engine_get_memory_metrics(first, &warm_memory) != 0,
        "warm resource-cache memory metrics were unavailable");
    webscene_engine_destroy(first);
    const auto rss_after_warmup = service_worker_test_current_rss_bytes();
    std::vector<double> durations;
    durations.reserve(100U);
    uint64_t maximum_v8_heap = warm_memory.v8_used_heap_bytes;
    for (size_t cycle = 0U; cycle < 100U; ++cycle) {
        auto* engine = webscene_engine_create_with_options(&options);
        require(engine != nullptr, "resource-cache cycle engine creation failed");
        const auto started = std::chrono::steady_clock::now();
        require(
            webscene_engine_load_url(engine, url.data(), url.size()) != 0,
            "resource-cache cycle document load failed");
        require(
            evaluate(engine, "globalThis.__cachedResourceValue",
                "resource-cache-cycle.js") == "42",
            "process-cached resource did not execute in a replacement engine");
        durations.push_back(std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started).count());
        webscene_engine_memory_metrics memory{
            sizeof(webscene_engine_memory_metrics)};
        require(webscene_engine_get_memory_metrics(engine, &memory) != 0,
            "resource-cache cycle memory metrics were unavailable");
        maximum_v8_heap = std::max(maximum_v8_heap, memory.v8_used_heap_bytes);
        webscene_engine_destroy(engine);
    }
    std::sort(durations.begin(), durations.end());
    const auto p95 = durations[94U];
    require(
        requests_after_first == 2
            && server.requests.load(std::memory_order_relaxed)
                == requests_after_first,
        "document and script bodies were not reused from the process resource cache");
    require(p95 <= 100.0,
        "warm parser resource reuse exceeded 100ms p95: " + std::to_string(p95));
    require(maximum_v8_heap <= warm_memory.v8_used_heap_bytes + 8U * 1024U * 1024U,
        "resource-cache cycles retained more than 8 MiB V8 heap");
    const auto rss_after_cycles = service_worker_test_current_rss_bytes();
    require(rss_after_cycles <= rss_after_warmup + 64U * 1024U * 1024U,
        "resource-cache cycles retained more than 64 MiB RSS after teardown");
    std::cout << "Parser resource cache gate: cycles=100 p95=" << p95
              << "ms requests=" << requests_after_first
              << " warmHeap=" << warm_memory.v8_used_heap_bytes
              << " maxHeap=" << maximum_v8_heap
              << " rssBefore=" << rss_after_warmup
              << " rssAfter=" << rss_after_cycles << '\n';

    std::error_code cleanup_error;
    std::filesystem::remove_all(cache_directory, cleanup_error);
}

void test_parser_resource_cache_partitioning()
{
    const auto cache_directory = std::filesystem::temp_directory_path()
        / ("webscene-native-parser-cache-partition-test-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(cache_directory);
    const auto cache_path = cache_directory.string();
    resource_server server{.content = {
        {"https://partition-a.test/seed.html", R"HTML(
            <!doctype html><script>
              document.cookie='parser-partition=a; Path=/; Secure';
              globalThis.__webSceneComponentReady=true;
            </script>)HTML"},
        {"https://partition-a.test/index.html", R"HTML(
            <!doctype html><script src="/value.js"></script>)HTML"},
        {"https://partition-b.test/index.html", R"HTML(
            <!doctype html><script src="https://partition-a.test/value.js"></script>)HTML"},
        {"https://partition-a.test/value.js",
            "globalThis.__partitionValue=42;globalThis.__webSceneComponentReady=true;"}
    }};
    const webscene_engine_options options{
        sizeof(webscene_engine_options),
        64U,
        cache_path.data(),
        cache_path.size(),
        load_test_resource,
        &server};
    auto* engine = webscene_engine_create_with_options(&options);
    require(engine != nullptr, "parser cache partition engine creation failed");
    const auto load_and_require_script = [&](std::string_view address) {
        require(webscene_engine_load_url(
                engine, address.data(), address.size()) != 0,
            "parser cache partition navigation failed");
        require(evaluate(engine, "globalThis.__partitionValue",
                "parser-cache-partition-value.js") == "42",
            "partitioned parser resource did not execute");
    };
    constexpr std::string_view seed{"https://partition-a.test/seed.html"};
    require(webscene_engine_load_url(engine, seed.data(), seed.size()) != 0,
        "parser cache cookie seed navigation failed");
    constexpr std::string_view first{"https://partition-a.test/index.html"};
    load_and_require_script(first);
    require(server.requests.load(std::memory_order_relaxed) == 3,
        "initial cookie-partitioned parser resources had unexpected host reads");
    execute(engine,
        "document.cookie='parser-partition=b; Path=/; Secure'",
        "parser-cache-cookie-replacement.js");
    load_and_require_script(first);
    require(server.requests.load(std::memory_order_relaxed) == 5,
        "a different cookie representation consumed fresh parser cache entries");
    load_and_require_script(first);
    require(server.requests.load(std::memory_order_relaxed) == 5,
        "the same cookie representation did not reuse fresh parser cache entries");
    constexpr std::string_view second{"https://partition-b.test/index.html"};
    load_and_require_script(second);
    require(server.requests.load(std::memory_order_relaxed) == 7,
        "a different initiator origin consumed fresh parser cache entries");
    load_and_require_script(second);
    require(server.requests.load(std::memory_order_relaxed) == 7,
        "the same initiator origin did not reuse fresh parser cache entries");
    webscene_engine_destroy(engine);

    std::error_code cleanup_error;
    std::filesystem::remove_all(cache_directory, cleanup_error);
}

void test_parsed_css_rule_payloads_are_shared_across_live_engines()
{
    resource_server server{
        .content = {
            {"https://shared-css.test/index.html", R"(
                <html><head><link rel="stylesheet" href="shared.css"></head>
                <body><div class="shared-alpha">shared</div></body></html>)"},
            {"https://shared-css.test/shared.css", R"(
                .shared-alpha { color: rgb(1, 2, 3); padding: 4px; }
                .shared-beta:hover { opacity: 0.75; }
                @media (min-width: 1px) {
                    .shared-gamma { display: block; }
                })"}
        }};
    const webscene_engine_options options{
        sizeof(webscene_engine_options),
        64U,
        nullptr,
        0U,
        load_test_resource,
        &server};
    constexpr std::string_view url = "https://shared-css.test/index.html";
    auto* first = webscene_engine_create_with_options(&options);
    auto* second = webscene_engine_create_with_options(&options);
    require(first != nullptr && second != nullptr,
        "shared-CSS engine creation failed");
    require(
        webscene_engine_load_url(first, url.data(), url.size()) != 0
            && webscene_engine_load_url(second, url.data(), url.size()) != 0,
        "shared-CSS documents did not load");

    webscene_engine_memory_metrics first_memory{
        sizeof(webscene_engine_memory_metrics)};
    webscene_engine_memory_metrics second_memory{
        sizeof(webscene_engine_memory_metrics)};
    for (auto attempt = 0; attempt < 250; ++attempt) {
        webscene_engine_get_memory_metrics(first, &first_memory);
        webscene_engine_get_memory_metrics(second, &second_memory);
        if (first_memory.native_css_rule_count >= 3U
            && second_memory.native_css_rule_count >= 3U
            && second_memory.process_shared_css_rule_count >= 3U) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(
        first_memory.native_css_rule_count >= 3U
            && second_memory.native_css_rule_count
                == first_memory.native_css_rule_count,
        "equivalent live engines did not retain the same logical CSS rules");
    require(
        second_memory.process_shared_css_rule_count
            < first_memory.native_css_rule_count
                + second_memory.native_css_rule_count,
        "equivalent CSS rule payloads were duplicated across live engines");
    require(
        second_memory.process_shared_css_rule_storage_bytes > 0U,
        "shared CSS payload allocation attribution was absent");
    webscene_engine_destroy(second);
    webscene_engine_destroy(first);
}

void test_process_wide_resource_load_single_flight()
{
    constexpr size_t engine_count = 4U;
    const auto shared_script =
        std::string{
            "globalThis.__sharedResourceUnicode = \"🙂\";\n"
            "globalThis.__sharedResourceExecutions = "
            "(globalThis.__sharedResourceExecutions || 0) + 1;\n/*"}
        + std::string(128U * 1024U, 'x')
        + "*/";
    const auto shared_ascii_script =
        std::string{"globalThis.__sharedAsciiResource = true;\n/*"}
        + std::string(8U * 1024U, 'a')
        + "*/";
    const auto unique_suffix = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const auto origin = "https://resource-single-flight-" + unique_suffix + ".test/";
    const auto document_url = origin + "index.html";
    const auto script_url = origin + "shared.js";
    const auto warmup_url = origin + "warmup.html";
    resource_server server{
        .content = {
            {warmup_url, "<html><body>Warmup</body></html>"},
            {document_url,
                "<html><body><script src=\"shared.js\"></script>"
                "<script src=\"shared-ascii.js\"></script></body></html>"},
            {script_url, shared_script},
            {origin + "shared-ascii.js", shared_ascii_script}
        }};
    const auto cache_directory = std::filesystem::temp_directory_path()
        / ("webscene-native-resource-single-flight-test-" + unique_suffix);
    std::filesystem::create_directories(cache_directory);
    const auto cache_path = cache_directory.string();
    const webscene_engine_options options{
        sizeof(webscene_engine_options),
        64U,
        cache_path.data(),
        cache_path.size(),
        load_test_resource,
        &server};

    std::array<webscene_engine*, engine_count> engines{};
    for (auto& engine : engines) {
        engine = webscene_engine_create_with_options(&options);
        require(engine != nullptr, "resource single-flight engine creation failed");
    }

    std::atomic<size_t> ready{0U};
    std::atomic<bool> start{false};
    std::array<std::thread, engine_count> workers;
    std::array<std::string, engine_count> results;
    for (size_t index = 0U; index < engine_count; ++index) {
        workers[index] = std::thread([&, index] {
            // Engine creation is lazy: initialize each V8 realm on its owning
            // thread before the release barrier. Otherwise a slow cold isolate
            // can miss the entire resource flight (observed on Windows CI).
            // Keep the waiter assertion below: this test must exercise overlap,
            // not merely sequential cache hits.
            require(webscene_engine_load_url(engines[index], warmup_url.data(), warmup_url.size()) != 0,
                "resource single-flight warmup navigation was rejected");
            require(evaluate(engines[index], "1", "single-flight-warmup.js") == "1",
                "resource single-flight warmup was rejected");
            ready.fetch_add(1U, std::memory_order_release);
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            require(
                webscene_engine_load_url(
                    engines[index],
                    document_url.data(),
                    document_url.size()) != 0,
                "resource single-flight document load was rejected");
            results[index] = evaluate(
                engines[index],
                "globalThis.__sharedResourceUnicode === \"🙂\" "
                "&& globalThis.__sharedAsciiResource "
                "? globalThis.__sharedResourceExecutions : -1",
                "resource-single-flight-result-" + unique_suffix + ".js");
        });
    }
    while (ready.load(std::memory_order_acquire) != engine_count) {
        std::this_thread::yield();
    }
    start.store(true, std::memory_order_release);
    for (auto& worker : workers) worker.join();

    uint64_t leaders = 0U;
    uint64_t waiters = 0U;
    uint64_t memory_hits = 0U;
    uint64_t shared_bytes = 0U;
    uint64_t script_source_hits = 0U;
    uint64_t script_source_shared_bytes = 0U;
    uint64_t mapped_resource_bytes = 0U;
    for (size_t index = 0U; index < engine_count; ++index) {
        require(results[index] == "1", "resource sharing leaked mutable script state");
        webscene_process_cache_metrics metrics{
            sizeof(webscene_process_cache_metrics)};
        require(
            webscene_engine_get_process_cache_metrics(engines[index], &metrics) != 0,
            "resource single-flight metrics were unavailable");
        leaders += metrics.resource_load_leaders;
        waiters += metrics.resource_load_waiters;
        memory_hits += metrics.resource_memory_hits;
        shared_bytes += metrics.resource_shared_bytes;
        script_source_hits += metrics.script_source_memory_hits;
        script_source_shared_bytes += metrics.script_source_shared_bytes;

        webscene_engine_memory_metrics memory{
            sizeof(webscene_engine_memory_metrics)};
        require(
            webscene_engine_get_memory_metrics(engines[index], &memory) != 0,
            "external script-source memory metrics were unavailable");
        require(
            memory.v8_external_script_source_bytes
                >= shared_script.size() + shared_ascii_script.size(),
            "large UTF-8 script source was copied into the V8 heap");
        // Memory telemetry is sampled, not refreshed synchronously by its getter.
        // A warmed realm can finish navigation within the five-second sample window.
        for (int attempt = 0; memory.process_resource_mapped_cache_bytes < shared_script.size()
                && attempt < 600; ++attempt) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            evaluate(engines[index], "1", "single-flight-memory-sample.js");
            webscene_engine_get_memory_metrics(engines[index], &memory);
        }
        mapped_resource_bytes = std::max(
            mapped_resource_bytes,
            memory.process_resource_mapped_cache_bytes);
    }
    require(
        mapped_resource_bytes >= shared_script.size(),
        "large persisted script resource was not retained as file-backed memory");
    require(
        server.requests.load(std::memory_order_relaxed) == 4,
        "identical resources (including warmup) were loaded more than once across four engines");
    require(leaders == 4U, "resource loads did not elect one producer per URL");
    require(waiters > 0U, "concurrent resource loads did not record waiters");
    require(memory_hits > 0U, "resource waiters did not consume shared results");
    require(shared_bytes > 0U, "resource waiters did not share immutable response bytes");
    if (std::getenv("WEBSCENE_V8_SHARED_ISOLATE") == nullptr) {
        require(
            script_source_hits >= 2U * (engine_count - 1U),
            "identical live engines did not reuse the external script-source backing");
        require(
            script_source_shared_bytes
                >= (engine_count - 1U)
                    * (shared_script.size() + shared_ascii_script.size()),
            "shared external script-source bytes were not attributed");
    }

    for (auto* engine : engines) webscene_engine_destroy(engine);
    std::error_code cleanup_error;
    std::filesystem::remove_all(cache_directory, cleanup_error);
}

enum class cache_policy_mode {
    fresh,
    stale_validator,
    no_store
};

struct cache_policy_server final {
    std::string url;
    cache_policy_mode mode{cache_policy_mode::fresh};
    std::string entity_tag{"\"v1\""};
    int value{1};
    int requests{0};
    int conditional_requests{0};
    bool online{true};
};

size_t load_cache_policy_resource(
    void* user_data,
    uint32_t,
    const char* url,
    size_t url_length,
    const char* entity_tag,
    size_t entity_tag_length,
    int64_t,
    char* destination,
    size_t destination_capacity)
{
    auto& server = *static_cast<cache_policy_server*>(user_data);
    if (std::string_view(url, url_length) != server.url || !server.online) return 0U;
    const auto request_tag = std::string_view(entity_tag, entity_tag_length);
    const auto not_modified = server.mode == cache_policy_mode::stale_validator
        && request_tag == server.entity_tag;
    const auto cacheable = server.mode != cache_policy_mode::no_store;
    const auto body = not_modified
        ? std::string{}
        : "<html><body><script>globalThis.__policyValue="
            + std::to_string(server.value)
            + ";globalThis.__webSceneComponentReady=true;</script></body></html>";
    constexpr size_t header_size = 2U + sizeof(uint32_t) + sizeof(int64_t) * 2U;
    const auto required = header_size + server.entity_tag.size() + body.size();
    if (destination == nullptr || destination_capacity < required) {
        if (destination == nullptr || destination_capacity != required) {
            ++server.requests;
            if (!request_tag.empty()) ++server.conditional_requests;
        }
        return required;
    }
    if (destination_capacity != required) {
        ++server.requests;
        if (!request_tag.empty()) ++server.conditional_requests;
    }
    destination[0] = not_modified ? 2 : 1;
    destination[1] = cacheable ? 1 : 0;
    const auto tag_length = static_cast<uint32_t>(server.entity_tag.size());
    const int64_t last_modified = 1'700'000'000;
    const auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    const int64_t fresh_until = server.mode == cache_policy_mode::fresh ? now + 3600 : 0;
    std::memcpy(destination + 2U, &tag_length, sizeof(tag_length));
    std::memcpy(destination + 2U + sizeof(tag_length), &last_modified, sizeof(last_modified));
    std::memcpy(
        destination + 2U + sizeof(tag_length) + sizeof(last_modified),
        &fresh_until,
        sizeof(fresh_until));
    std::copy(
        server.entity_tag.begin(),
        server.entity_tag.end(),
        destination + header_size);
    std::copy(
        body.begin(),
        body.end(),
        destination + header_size + server.entity_tag.size());
    return required;
}

int load_policy_value(cache_policy_server& server)
{
    const auto cache_path = (std::filesystem::current_path()
        / "webscene-native-cache-policy-matrix").string();
    std::filesystem::create_directories(cache_path);
    const webscene_engine_options options{
        sizeof(webscene_engine_options),
        64U,
        cache_path.data(),
        cache_path.size(),
        load_cache_policy_resource,
        &server};
    auto* engine = webscene_engine_create_with_options(&options);
    require(engine != nullptr, "cache-policy engine creation failed");
    require(
        webscene_engine_load_url(engine, server.url.data(), server.url.size()) != 0,
        "cache-policy document load was rejected");
    const auto value = evaluate(engine, "globalThis.__policyValue", "cache-policy-value.js");
    webscene_engine_destroy(engine);
    return std::stoi(value);
}

void test_resource_cache_policy_matrix()
{
    const auto cache_directory = std::filesystem::current_path()
        / "webscene-native-cache-policy-matrix";
    std::error_code cleanup_error;
    std::filesystem::remove_all(cache_directory, cleanup_error);

    cache_policy_server fresh{"https://cache-policy.test/fresh.html"};
    require(load_policy_value(fresh) == 1, "fresh resource initial load failed");
    fresh.value = 2;
    require(load_policy_value(fresh) == 1, "fresh resource was needlessly revalidated");
    require(fresh.requests == 1, "fresh resource reached the loader more than once");

    cache_policy_server revalidated{
        "https://cache-policy.test/revalidated.html",
        cache_policy_mode::stale_validator};
    require(load_policy_value(revalidated) == 1, "stale resource initial load failed");
    require(load_policy_value(revalidated) == 1, "304 did not reuse the cached body");
    if (revalidated.requests != 2 || revalidated.conditional_requests != 1) {
        fail("stale resource did not revalidate with its validator: requests="
            + std::to_string(revalidated.requests)
            + " conditional=" + std::to_string(revalidated.conditional_requests));
    }

    cache_policy_server changed{
        "https://cache-policy.test/changed.html",
        cache_policy_mode::stale_validator};
    require(load_policy_value(changed) == 1, "changed resource initial load failed");
    changed.entity_tag = "\"v2\"";
    changed.value = 2;
    require(load_policy_value(changed) == 2, "changed 200 response did not replace the body");
    require(changed.conditional_requests == 1, "changed resource omitted its prior validator");

    cache_policy_server no_cache{
        "https://cache-policy.test/no-cache.html",
        cache_policy_mode::stale_validator};
    require(load_policy_value(no_cache) == 1, "no-cache initial load failed");
    require(load_policy_value(no_cache) == 1, "no-cache 304 did not reuse the body");
    require(no_cache.requests == 2, "no-cache resource was not revalidated");

    cache_policy_server no_store{
        "https://cache-policy.test/no-store.html",
        cache_policy_mode::no_store};
    require(load_policy_value(no_store) == 1, "no-store initial load failed");
    no_store.value = 2;
    no_store.entity_tag = "\"v2\"";
    require(load_policy_value(no_store) == 2, "no-store response was incorrectly retained");
    require(no_store.requests == 2, "no-store resource bypassed the loader");

    cache_policy_server offline{
        "https://cache-policy.test/offline.html",
        cache_policy_mode::stale_validator};
    require(load_policy_value(offline) == 1, "offline resource initial load failed");
    offline.online = false;
    require(load_policy_value(offline) == 1, "permitted offline warm cache did not serve its body");
    offline.online = true;
    offline.value = 2;
    offline.entity_tag = "\"v2\"";
    require(
        load_policy_value(offline) == 1,
        "failed revalidation did not receive a short stale-cache backoff");
    require(
        offline.requests == 1,
        "stale-cache backoff retried the recovered origin immediately");

    std::filesystem::remove_all(cache_directory, cleanup_error);
}

void test_due_timer_precedes_dynamic_resource_wave()
{
    resource_server server{
        .content = {
            {"https://task-fairness.test/index.html", R"(
                <html><body><script>
                  globalThis.__taskOrder = [];
                  setTimeout(() => globalThis.__taskOrder.push('timer'), 0);
                  const script = document.createElement('script');
                  script.src = 'dynamic.js';
                  script.onload = () => globalThis.__taskOrder.push('load');
                  document.body.appendChild(script);
                </script></body></html>)"},
            {"https://task-fairness.test/dynamic.js",
                "globalThis.__taskOrder.push('script');"}
        }};
    const webscene_engine_options options{
        sizeof(webscene_engine_options),
        64U,
        nullptr,
        0U,
        load_test_resource,
        &server};
    auto* engine = webscene_engine_create_with_options(&options);
    require(engine != nullptr, "task-fairness engine creation failed");
    const std::string url = "https://task-fairness.test/index.html";
    require(
        webscene_engine_load_url(engine, url.data(), url.size()) != 0,
        "task-fairness document load failed");

    auto order = std::string{};
    for (auto attempt = 0; attempt < 200; ++attempt) {
        order = evaluate(engine, "globalThis.__taskOrder", "task-fairness-result.js");
        if (order == R"(["timer","script","load"])") break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(
        order == R"(["timer","script","load"])",
        "a due timer was starved behind the dynamic resource task source");
    webscene_engine_destroy(engine);
}

void test_dynamic_frame_resources_use_each_document_base_url()
{
    resource_server server{
        .content = {
            {"https://parent.test/index.html", R"HTML(
                <html><body><script>
                  globalThis.__frameResourceOrigins = [];
                  globalThis.__frameMessageOrigins = [];
                  for (const source of [
                    'https://first-frame.test/dir/frame.html',
                    'https://second-frame.test/frame.html'
                  ]) {
                    const frame = document.createElement('iframe');
                    frame.src = source;
                    document.body.appendChild(frame);
                  }
                </script></body></html>)HTML"},
            {"https://first-frame.test/dir/frame.html", R"HTML(
                <html><head><base href="../assets/"><style>
                  body { background-image: url("chart.svg"); }
                </style></head><body><script>
                  addEventListener('message', event =>
                    parent.__frameMessageOrigins.push([
                      'first', event.origin, event.source === parent
                    ]));
                  const resource = document.createElement('script');
                  resource.src = '/assets/dynamic.js';
                  resource.onload = () => parent.__frameResourceOrigins.push('first-load');
                  document.head.appendChild(resource);
                </script></body></html>)HTML"},
            {"https://second-frame.test/frame.html", R"HTML(
                <html><body><script>
                  addEventListener('message', event =>
                    parent.__frameMessageOrigins.push([
                      'second', event.origin, event.source === parent
                    ]));
                  const resource = document.createElement('script');
                  resource.src = '/assets/dynamic.js';
                  resource.onload = () => parent.__frameResourceOrigins.push('second-load');
                  document.head.appendChild(resource);
                </script></body></html>)HTML"},
            {"https://first-frame.test/assets/dynamic.js",
                "parent.__frameResourceOrigins.push('first-script');"},
            {"https://first-frame.test/assets/chart.svg",
                "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 1 1\"/>"},
            {"https://second-frame.test/assets/dynamic.js",
                "parent.__frameResourceOrigins.push('second-script');"}
        }};
    const webscene_engine_options options{
        sizeof(webscene_engine_options),
        64U,
        nullptr,
        0U,
        load_test_resource,
        &server};
    auto* engine = webscene_engine_create_with_options(&options);
    require(engine != nullptr, "frame-resource-base engine creation failed");
    const std::string url = "https://parent.test/index.html";
    require(
        webscene_engine_load_url(engine, url.data(), url.size()) != 0,
        "frame-resource-base document load failed");

    auto origins = std::string{};
    for (auto attempt = 0; attempt < 400; ++attempt) {
        origins = evaluate(
            engine,
            "[...globalThis.__frameResourceOrigins].sort()",
            "frame-resource-base-result.js");
        if (origins
            == R"(["first-load","first-script","second-load","second-script"])") {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(
        origins == R"(["first-load","first-script","second-load","second-script"])",
        "dynamic frame resources did not resolve against their owning documents: "
            + origins);
    const auto frame_background = evaluate(
        engine,
        "getComputedStyle(document.querySelector('iframe').contentDocument.body).backgroundImage",
        "frame-css-base-result.js");
    require(
        frame_background
            == "\"url(\\\"https://first-frame.test/assets/chart.svg\\\")\"",
        "frame CSS URL did not resolve against its relative base element: "
            + frame_background);
    const auto live_base = evaluate(engine, R"JS((() => {
      const exercise = (document, navigation) => {
        const host = document.createElement('div');
        host.className = 'live-base-target';
        document.body.appendChild(host);
        const first = document.createElement('base');
        first.setAttribute('href', '/alpha/');
        document.head.appendChild(first);
        const alpha = [document.URL, document.baseURI,
          new URL('next.js', document.baseURI).href];
        const alphaStyle = document.createElement('style');
        alphaStyle.textContent = '.live-base-target { background-image: url(retained.svg); }';
        document.head.appendChild(alphaStyle);
        const retained = document.defaultView.getComputedStyle(host).backgroundImage;
        first.setAttribute('href', '/beta/');
        const beta = [document.URL, document.baseURI,
          new URL('next.js', document.baseURI).href, retained,
          document.defaultView.getComputedStyle(host).backgroundImage];
        const second = document.createElement('base');
        second.setAttribute('href', '/first/');
        document.head.insertBefore(second, first);
        const reordered = document.baseURI;
        second.remove();
        const restored = document.baseURI;
        first.setAttribute('href', '/attr/');
        const attr = document.baseURI;
        first.removeAttribute('href');
        const removedHref = document.baseURI;
        first.remove();
        alphaStyle.remove();
        host.remove();
        return { navigation, alpha, beta, reordered, restored, attr, removedHref,
          removed: document.baseURI };
      };
      const frames = document.querySelectorAll('iframe');
      return {
        top: exercise(document, 'https://parent.test/index.html'),
        frame: exercise(frames[1].contentDocument,
          'https://second-frame.test/frame.html')
      };
    })())JS", "live-document-base-result.js");
    require(
        live_base
            == R"JSON({"top":{"navigation":"https://parent.test/index.html","alpha":["https://parent.test/index.html","https://parent.test/alpha/","https://parent.test/alpha/next.js"],"beta":["https://parent.test/index.html","https://parent.test/beta/","https://parent.test/beta/next.js","url(\"https://parent.test/alpha/retained.svg\")","url(\"https://parent.test/alpha/retained.svg\")"],"reordered":"https://parent.test/first/","restored":"https://parent.test/beta/","attr":"https://parent.test/attr/","removedHref":"https://parent.test/index.html","removed":"https://parent.test/index.html"},"frame":{"navigation":"https://second-frame.test/frame.html","alpha":["https://second-frame.test/frame.html","https://second-frame.test/alpha/","https://second-frame.test/alpha/next.js"],"beta":["https://second-frame.test/frame.html","https://second-frame.test/beta/","https://second-frame.test/beta/next.js","none","none"],"reordered":"https://second-frame.test/first/","restored":"https://second-frame.test/beta/","attr":"https://second-frame.test/attr/","removedHref":"https://second-frame.test/frame.html","removed":"https://second-frame.test/frame.html"}})JSON",
        "live effective base did not preserve navigation, ordering, or resolved CSS: "
            + live_base);
    webscene_engine_memory_metrics base_memory_before{
        sizeof(webscene_engine_memory_metrics)};
    require(webscene_engine_get_memory_metrics(engine, &base_memory_before) != 0,
        "live effective base baseline memory metrics unavailable");
    const auto base_rss_before = file_system_access_test_peak_rss_bytes();
    const auto descriptor_count = [] {
        size_t count = 0U;
        std::error_code error;
        for (std::filesystem::directory_iterator iterator("/dev/fd", error), end;
             !error && iterator != end; iterator.increment(error)) ++count;
        return error ? size_t{0U} : count;
    };
    const auto descriptors_before = descriptor_count();
    const auto base_profile = evaluate(engine, R"JS((() => {
      const doc = document.querySelectorAll('iframe')[1].contentDocument;
      const root = doc.createElement('div');
      const fragment = doc.createDocumentFragment();
      for (let index = 0; index < 4096; index++) {
        const node = doc.createElement('span');
        node.dataset.index = String(index);
        fragment.appendChild(node);
      }
      root.appendChild(fragment);
      doc.body.appendChild(root);
      const base = doc.createElement('base');
      doc.head.appendChild(base);
      const updates = [], lookups = [];
      let checksum = 0;
      for (let cycle = 0; cycle < 20; cycle++) {
        let started = performance.now();
        base.setAttribute('href', `/cycle-${cycle}/`);
        if (!doc.baseURI.endsWith(`/cycle-${cycle}/`)) throw new Error('stale base');
        updates.push(performance.now() - started);
        started = performance.now();
        for (let index = 0; index < 4096; index++) {
          checksum += new URL(`asset-${index}.js`, doc.baseURI).href.length;
        }
        lookups.push(performance.now() - started);
      }
      for (let cycle = 0; cycle < 100; cycle++) {
        const transient = doc.createElement('base');
        transient.setAttribute('href', `/transient-${cycle}/`);
        doc.head.insertBefore(transient, base);
        if (!doc.baseURI.endsWith(`/transient-${cycle}/`)) throw new Error('reorder');
        transient.remove();
        if (!doc.baseURI.endsWith('/cycle-19/')) throw new Error('restore');
      }
      const summary = values => {
        const ordered = values.slice().sort((a, b) => a - b);
        return { p95: ordered[18], max: ordered[19] };
      };
      const update = summary(updates), lookup = summary(lookups);
      if (update.p95 > 16 || update.max > 250
          || lookup.p95 > 250 || lookup.max > 1000) {
        throw new Error(`base latency exceeded ${JSON.stringify({ update, lookup })}`);
      }
      base.remove();
      root.remove();
      return { nodes: 4096, urls: 4096 * 20, cycles: 20,
        lifecycleCycles: 100, checksum,
        update, lookup,
        restored: doc.baseURI };
    })())JS", "live-document-base-profile.js");
    require(
        base_profile.find("\"nodes\":4096") != std::string::npos
            && base_profile.find("\"urls\":81920") != std::string::npos
            && base_profile.find("\"lifecycleCycles\":100") != std::string::npos
            && base_profile.find(
                "\"restored\":\"https://second-frame.test/frame.html\"")
                != std::string::npos,
        "live effective base profile failed: " + base_profile);
    require(webscene_engine_request_low_memory(engine) != 0,
        "live effective base low-memory request failed");
    webscene_engine_memory_metrics base_memory_after{
        sizeof(webscene_engine_memory_metrics)};
    for (auto attempt = 0; attempt < 500; ++attempt) {
        require(webscene_engine_get_memory_metrics(engine, &base_memory_after) != 0,
            "live effective base final memory metrics unavailable");
        if (base_memory_after.low_memory_notifications
                > base_memory_before.low_memory_notifications
            && base_memory_after.native_dom_node_count
                <= base_memory_before.native_dom_node_count + 16U) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    const auto base_rss_after = file_system_access_test_peak_rss_bytes();
    const auto descriptors_after = descriptor_count();
    require(
        base_memory_after.v8_used_heap_bytes
                <= base_memory_before.v8_used_heap_bytes + 16U * 1024U * 1024U
            && base_memory_after.native_dom_node_count
                <= base_memory_before.native_dom_node_count + 16U
            && base_memory_after.native_wrapper_handle_count
                <= base_memory_before.native_wrapper_handle_count + 16U,
        "live effective base retained heap, DOM nodes, or wrappers");
    require(base_rss_before == 0U
            || base_rss_after <= base_rss_before + 64U * 1024U * 1024U,
        "live effective base exceeded the 64 MiB RSS budget");
    require(descriptors_before == 0U || descriptors_after <= descriptors_before + 2U,
        "live effective base retained file descriptors");
    std::cout << "document-base-profile=" << base_profile
              << " heapBefore=" << base_memory_before.v8_used_heap_bytes
              << " heapAfter=" << base_memory_after.v8_used_heap_bytes
              << " nodesBefore=" << base_memory_before.native_dom_node_count
              << " nodesAfter=" << base_memory_after.native_dom_node_count
              << " wrappersBefore=" << base_memory_before.native_wrapper_handle_count
              << " wrappersAfter=" << base_memory_after.native_wrapper_handle_count
              << " rssBefore=" << base_rss_before
              << " rssAfter=" << base_rss_after
              << " fdBefore=" << descriptors_before
              << " fdAfter=" << descriptors_after << '\n';
    require(
        server.peak_documents.load(std::memory_order_relaxed) >= 2,
        "dynamic iframe documents were not prefetched concurrently");
    execute(engine, R"JS(
        (() => {
          const children = Array.from(document.querySelectorAll('iframe'));
          children[0].contentWindow.postMessage(
            'first-probe', 'https://first-frame.test');
          children[1].contentWindow.postMessage(
            'second-probe', 'https://second-frame.test');
        })()
    )JS", "frame-message-origin-send.js");
    auto message_origins = std::string{};
    for (auto attempt = 0; attempt < 200; ++attempt) {
        message_origins = evaluate(
            engine,
            "[...globalThis.__frameMessageOrigins].sort()",
            "frame-message-origin-result.js");
        if (message_origins
            == R"([["first","https://parent.test",true],["second","https://parent.test",true]])") {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(
        message_origins
            == R"([["first","https://parent.test",true],["second","https://parent.test",true]])",
        "cross-origin frame messages did not expose the sender origin: "
            + message_origins);
    webscene_engine_destroy(engine);
}

int64_t measure_iframe_subresource_discovery(bool disable_preparation)
{
#if defined(_WIN32)
    _putenv_s(
        "WEBSCENE_PROBE_DISABLE_IFRAME_PREPARATION",
        disable_preparation ? "1" : "");
#else
    if (disable_preparation) {
        setenv("WEBSCENE_PROBE_DISABLE_IFRAME_PREPARATION", "1", 1);
    } else {
        unsetenv("WEBSCENE_PROBE_DISABLE_IFRAME_PREPARATION");
    }
#endif
    const auto host = disable_preparation
        ? std::string("https://frame-preparation-control.test")
        : std::string("https://frame-preparation-enabled.test");
    resource_server server{
        .content = {
            {host + "/index.html", R"HTML(
                <html><body>
                  <script>
                    const frame = document.createElement('iframe');
                    frame.src = 'frame.html';
                    document.body.appendChild(frame);
                    const stop = performance.now() + 300;
                    while (performance.now() < stop) {}
                  </script>
                </body></html>)HTML"},
            {host + "/frame.html", R"HTML(
                <html><head><link rel="stylesheet" href="frame.css"></head>
                <body><script src="frame.js"></script></body></html>)HTML"},
            {host + "/frame.css", "body { color: rgb(1, 2, 3); }"},
            {host + "/frame.js", "parent.__framePreparationLoaded = true;"}
        }};
    const webscene_engine_options options{
        sizeof(webscene_engine_options),
        64U,
        nullptr,
        0U,
        load_test_resource,
        &server};
    auto* engine = webscene_engine_create_with_options(&options);
    require(engine != nullptr, "iframe-preparation engine creation failed");
    constexpr std::string_view start_source{"globalThis.__documentStartRan = true;"};
    constexpr std::string_view start_name{"iframe-preparation-start.js"};
    const webscene_document_script script{
        sizeof(webscene_document_script),
        WEBSCENE_DOCUMENT_SCRIPT_ALL_FRAMES,
        start_source.data(), start_source.size(),
        start_name.data(), start_name.size()};
    const webscene_navigation_options navigation{
        sizeof(webscene_navigation_options),
        1U,
        &script};
    const auto url = host + "/index.html";
    server.observation_start = std::chrono::steady_clock::now();
    require(
        webscene_engine_load_url_with_options(
            engine,
            url.data(),
            url.size(),
            &navigation) != 0U,
        "iframe-preparation navigation failed");
    const auto frame_result = evaluate(
        engine,
        "(() => { const frame = document.querySelector('iframe'); return {"
        "count: document.querySelectorAll('iframe').length,"
        "window: frame ? typeof frame.contentWindow : 'missing',"
        "loaded: globalThis.__framePreparationLoaded,"
        "error: frame && frame.getAttribute('data-webscene-frame-error')"
        "}; })()",
        "iframe-preparation-result.js");
    require(
        frame_result == R"({"count":1,"window":"object","loaded":true,"error":null})",
        "prepared iframe resources did not preserve script execution: "
            + frame_result + " diagnostics=" + diagnostics(engine));
    const auto result = server.first_subresource_start_ms.load(
        std::memory_order_relaxed);
    webscene_engine_destroy(engine);
    return result;
}

void test_iframe_preparation_discovers_subresources_during_outer_script()
{
    const auto prepared_start = measure_iframe_subresource_discovery(false);
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
    const auto control_start = measure_iframe_subresource_discovery(true);
#if defined(_WIN32)
    _putenv_s("WEBSCENE_PROBE_DISABLE_IFRAME_PREPARATION", "");
#else
    unsetenv("WEBSCENE_PROBE_DISABLE_IFRAME_PREPARATION");
#endif
    require(
        prepared_start != std::numeric_limits<int64_t>::max()
            && control_start != std::numeric_limits<int64_t>::max(),
        "iframe subresource discovery was not observed");
    require(
        prepared_start + 70 < control_start,
        "iframe preparation did not overlap discovery with outer script work");
#else
    require(
        prepared_start != std::numeric_limits<int64_t>::max()
            // Keep enough margin for a loaded runner while still requiring
            // preparation to begin before the outer script yields.
            && prepared_start < iframe_outer_script_work_ms - 40,
        "iframe preparation did not discover subresources during outer script work");
#endif
}

void test_deferred_frame_script_observes_window_dom_content_loaded()
{
    resource_server server{
        .content = {
            {"https://frame-lifecycle.test/index.html", R"HTML(
                <html><body><script>
                  globalThis.__frameLifecycle = [];
                  const frame = document.createElement('iframe');
                  frame.src = 'https://child-lifecycle.test/frame.html';
                  document.body.appendChild(frame);
                </script></body></html>)HTML"},
            {"https://child-lifecycle.test/frame.html", R"HTML(
                <html><head>
                  <script defer src="/deferred.js"></script>
                </head><body></body></html>)HTML"},
            {"https://child-lifecycle.test/deferred.js", R"JS(
                parent.__frameLifecycle.push(['defer', document.readyState]);
                document.addEventListener('DOMContentLoaded', event => {
                  parent.__frameLifecycle.push([
                    'document-domcontentloaded', document.readyState,
                    event.target === document, event.currentTarget === document
                  ]);
                });
                window.addEventListener('DOMContentLoaded', event => {
                  parent.__frameLifecycle.push([
                    'window-domcontentloaded', document.readyState,
                    event.target === document, event.currentTarget === window
                  ]);
                });
                window.addEventListener('load', () => {
                  parent.__frameLifecycle.push(['load', document.readyState]);
                });
            )JS"}
        }};
    const webscene_engine_options options{
        sizeof(webscene_engine_options),
        64U,
        nullptr,
        0U,
        load_test_resource,
        &server};
    auto* engine = webscene_engine_create_with_options(&options);
    require(engine != nullptr, "frame-lifecycle engine creation failed");
    const std::string url = "https://frame-lifecycle.test/index.html";
    require(
        webscene_engine_load_url(engine, url.data(), url.size()) != 0,
        "frame-lifecycle document load failed");

    auto lifecycle = std::string{};
    for (auto attempt = 0; attempt < 400; ++attempt) {
        lifecycle = evaluate(
            engine,
            "globalThis.__frameLifecycle",
            "frame-lifecycle-result.js");
        if (lifecycle.ends_with(R"(["load","complete"]])")) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(
        lifecycle == R"JSON([["defer","interactive"],["document-domcontentloaded","interactive",true,true],["window-domcontentloaded","interactive",true,true],["load","complete"]])JSON",
        "deferred iframe lifecycle did not propagate DOMContentLoaded to Window: "
            + lifecycle);
    webscene_engine_destroy(engine);
}

void test_dynamic_stylesheet_custom_properties_preserve_cascade_order()
{
    resource_server server{
        .content = {
            {"https://dynamic-cascade.test/index.html", R"(
                <html><body><script>
                  const symbol = document.createElement('div');
                  symbol.className = 'symbol';
                  const volume = document.createElement('span');
                  volume.className = 'cell volume hidden';
                  symbol.appendChild(volume);
                  document.body.appendChild(symbol);
                  const direct = document.createElement('div');
                  direct.className = 'direct-variable';
                  document.body.appendChild(direct);
                  const indirect = document.createElement('div');
                  indirect.className = 'indirect-variable';
                  document.body.appendChild(indirect);
                  const inline = document.createElement('div');
                  inline.style.display = 'var(--watchlist-volume-display)';
                  document.body.appendChild(inline);
                  for (let index = 0; index < 256; ++index) {
                    const irrelevant = document.createElement('div');
                    irrelevant.className = 'irrelevant-' + index;
                    document.body.appendChild(irrelevant);
                  }
                  globalThis.__dynamicCascadeDisplay = 'loading';
                  const link = document.createElement('link');
                  link.rel = 'stylesheet';
                  link.href = 'watchlist.css';
                  link.onload = () => {
                    globalThis.__dynamicCascadeDisplay = [
                      getComputedStyle(volume).display,
                      getComputedStyle(direct).display,
                      getComputedStyle(indirect).display,
                      getComputedStyle(inline).display
                    ];
                    globalThis.__webSceneComponentReady = true;
                  };
                  document.head.appendChild(link);
                </script></body></html>)"},
            {"https://dynamic-cascade.test/watchlist.css", R"(
                :root { --watchlist-volume-display: flex; }
                :root {
                  --watchlist-indirect-display:
                    var(--watchlist-volume-display);
                }
                .symbol .cell.volume {
                  display: var(--watchlist-volume-display, flex);
                }
                .symbol .cell.hidden { display: none; }
                .direct-variable {
                  display: var(--watchlist-volume-display);
                }
                .indirect-variable {
                  display: var(--watchlist-indirect-display);
                }
            )"}
        }};
    const webscene_engine_options options{
        sizeof(webscene_engine_options),
        64U,
        nullptr,
        0U,
        load_test_resource,
        &server};
    auto* engine = webscene_engine_create_with_options(&options);
    require(engine != nullptr, "dynamic-cascade engine creation failed");
    const std::string url = "https://dynamic-cascade.test/index.html";
    require(
        webscene_engine_load_url(engine, url.data(), url.size()) != 0,
        "dynamic-cascade document load failed");

    auto display = std::string{};
    for (auto attempt = 0; attempt < 200; ++attempt) {
        display = evaluate(
            engine,
            "globalThis.__dynamicCascadeDisplay",
            "dynamic-cascade-result.js");
        if (display == R"(["none","flex","flex","flex"])") break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(
        display == R"(["none","flex","flex","flex"])",
        "custom-property recascade overrode a later display declaration: "
            + display);
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
    auto profile = std::string{};
    for (auto attempt = 0; attempt < 100; ++attempt) {
        profile = diagnostics(engine);
        if (profile.find("stylesheet-variable-nodes=") != std::string::npos) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    const auto read_metric = [&](std::string_view name) {
        const auto start = profile.find(name);
        require(start != std::string::npos, "missing startup profile metric: "
            + std::string(name) + " in " + profile);
        const auto value_start = start + name.size();
        return std::stoull(profile.substr(value_start));
    };
    const auto visited_nodes = read_metric("stylesheet-nodes=");
    const auto candidate_nodes = read_metric("stylesheet-candidate-nodes=");
    const auto variable_nodes = read_metric("stylesheet-variable-nodes=");
    require(
        visited_nodes >= 256U,
        "dynamic stylesheet benchmark did not visit its irrelevant-node corpus");
    require(
        candidate_nodes <= 5U,
        "dynamic stylesheet candidate filtering finalized irrelevant nodes: "
            + std::to_string(candidate_nodes));
    require(
        variable_nodes <= 4U,
        "custom-property dependency filtering revisited irrelevant nodes: "
            + std::to_string(variable_nodes));
#endif
    webscene_engine_destroy(engine);
}

void test_persistent_compilation_cache_reuse()
{
    const auto cache_directory = std::filesystem::temp_directory_path()
        / ("webscene-native-v8-cache-test-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(cache_directory);
    const auto cache_path = cache_directory.string();
    const webscene_engine_options options{
        sizeof(webscene_engine_options),
        64U,
        cache_path.data(),
        cache_path.size(),
        nullptr,
        nullptr};
    constexpr std::string_view source =
        "globalThis.__persistentCacheValue = "
        "(globalThis.__persistentCacheValue || 0) + 1;";
    constexpr std::string_view document_name = "native-persistent-cache-script.js";
    constexpr std::string_view result_name = "native-persistent-cache-result.js";

    auto* first = webscene_engine_create_with_options(&options);
    require(first != nullptr, "first persistent-cache engine creation failed");
    execute(first, source, document_name);
    require(
        evaluate(first, "globalThis.__persistentCacheValue", result_name) == "1",
        "freshly compiled script produced an unexpected result");
    webscene_engine_metrics first_metrics{};
    webscene_engine_get_metrics(first, &first_metrics);
    require(
        first_metrics.compilation_cache_bytes_written > 0U,
        "first engine did not persist V8 compilation data");
    webscene_engine_destroy(first);

    auto* second = webscene_engine_create_with_options(&options);
    require(second != nullptr, "second persistent-cache engine creation failed");
    execute(second, source, document_name);
    require(
        evaluate(second, "globalThis.__persistentCacheValue", result_name) == "1",
        "persisted compilation data changed script semantics in a fresh isolate");
    webscene_engine_metrics second_metrics{};
    webscene_engine_get_metrics(second, &second_metrics);
    webscene_process_cache_metrics second_process_metrics{
        sizeof(webscene_process_cache_metrics)};
    require(
        webscene_engine_get_process_cache_metrics(second, &second_process_metrics) != 0,
        "process compilation-cache metrics were unavailable");
    require(
        second_metrics.compilation_persistent_hits >= 2U
            || second_process_metrics.compilation_memory_hits >= 2U
            || (std::getenv("WEBSCENE_V8_SHARED_ISOLATE") != nullptr
                && second_metrics.compilation_memory_hits >= 2U),
        "second engine did not reuse process or persisted V8 compilation data");
    require(
        second_metrics.compilation_cache_bytes_read > 0U
            || second_process_metrics.compilation_shared_bytes > 0U
            || (std::getenv("WEBSCENE_V8_SHARED_ISOLATE") != nullptr
                && second_metrics.compilation_memory_hits >= 2U),
        "second engine did not consume reusable V8 compilation data");
    webscene_engine_destroy(second);

    std::error_code cleanup_error;
    std::filesystem::remove_all(cache_directory, cleanup_error);
}

void test_executed_compilation_units_enrich_persistent_cache()
{
    const auto cache_directory = std::filesystem::temp_directory_path()
        / ("webscene-native-hot-v8-cache-test-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(cache_directory);
    const auto cache_path = cache_directory.string();
    const webscene_engine_options options{
        sizeof(webscene_engine_options),
        64U,
        cache_path.data(),
        cache_path.size(),
        nullptr,
        nullptr};

    std::string source = "globalThis.__hotCacheValue = 0;\n";
    for (auto function = 0; function < 128; ++function) {
        source += "function hot" + std::to_string(function)
            + "(value) { let total = value; for (let i = 0; i < 16; i++) "
              "total = ((total * 33) ^ i) | 0; return total; }\n"
              "globalThis.__hotCacheValue ^= hot"
            + std::to_string(function) + "(" + std::to_string(function) + ");\n";
    }
    constexpr std::string_view document_name = "native-hot-persistent-cache-script.js";

    auto* first = webscene_engine_create_with_options(&options);
    require(first != nullptr, "hot-cache engine creation failed");
    execute(first, source, document_name);
    require(
        evaluate(first, "typeof globalThis.__hotCacheValue", "hot-cache-barrier.js")
            == R"("number")",
        "hot compilation unit did not execute");
    auto cache_file = std::filesystem::path{};
    uintmax_t initial_size = 0U;
    for (const auto& entry : std::filesystem::directory_iterator(cache_directory)) {
        if (entry.path().extension() == ".v8cache"
            && entry.file_size() > initial_size) {
            cache_file = entry.path();
            initial_size = entry.file_size();
        }
    }
    require(!cache_file.empty(), "initial hot compilation cache was not written");
    webscene_engine_destroy(first);
    const auto enriched_size = std::filesystem::file_size(cache_file);
    require(
        enriched_size > initial_size,
        "executed lazy functions did not enrich the persisted compilation unit");

    auto* second = webscene_engine_create_with_options(&options);
    require(second != nullptr, "second hot-cache engine creation failed");
    execute(second, source, document_name);
    require(
        evaluate(second, "typeof globalThis.__hotCacheValue", "hot-cache-second-barrier.js")
            == R"("number")",
        "enriched compilation unit did not execute in the second engine");
    webscene_engine_metrics metrics{};
    webscene_engine_get_metrics(second, &metrics);
    webscene_process_cache_metrics process_metrics{
        sizeof(webscene_process_cache_metrics)};
    require(
        webscene_engine_get_process_cache_metrics(second, &process_metrics) != 0,
        "hot-cache process metrics were unavailable");
    require(
        metrics.compilation_persistent_hits > 0U
            || metrics.compilation_memory_hits > 0U
            || process_metrics.compilation_memory_hits > 0U,
        "hot compilation unit was not reused by the second engine");
    webscene_engine_destroy(second);

    std::error_code cleanup_error;
    std::filesystem::remove_all(cache_directory, cleanup_error);
}

void test_process_wide_compilation_single_flight()
{
    constexpr size_t engine_count = 4U;
    const auto cache_directory = std::filesystem::temp_directory_path()
        / ("webscene-native-v8-single-flight-test-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(cache_directory);
    const auto cache_path = cache_directory.string();
    const webscene_engine_options options{
        sizeof(webscene_engine_options),
        64U,
        cache_path.data(),
        cache_path.size(),
        nullptr,
        nullptr};

    std::string source =
        "globalThis.__singleFlightExecutions = "
        "(globalThis.__singleFlightExecutions || 0) + 1;\n";
    for (auto function = 0; function < 12'000; ++function) {
        source += "function sharedCold" + std::to_string(function)
            + "(value){return value+" + std::to_string(function) + ";}\n";
    }
    const auto unique_suffix = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const auto document_name = "native-process-single-flight-" + unique_suffix + ".js";
    const auto barrier_name = "native-process-single-flight-barrier-" + unique_suffix + ".js";

    std::array<webscene_engine*, engine_count> engines{};
    for (auto& engine : engines) {
        engine = webscene_engine_create_with_options(&options);
        require(engine != nullptr, "single-flight engine creation failed");
    }

    std::atomic<size_t> ready{0U};
    std::atomic<bool> start{false};
    std::array<std::thread, engine_count> workers;
    std::array<std::string, engine_count> results;
    for (size_t index = 0U; index < engine_count; ++index) {
        workers[index] = std::thread([&, index] {
            ready.fetch_add(1U, std::memory_order_release);
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            execute(engines[index], source, document_name);
            results[index] = evaluate(
                engines[index],
                "globalThis.__singleFlightExecutions",
                barrier_name);
        });
    }
    while (ready.load(std::memory_order_acquire) != engine_count) {
        std::this_thread::yield();
    }
    start.store(true, std::memory_order_release);
    for (auto& worker : workers) worker.join();

    uint64_t leaders = 0U;
    uint64_t waiters = 0U;
    uint64_t memory_hits = 0U;
    uint64_t shared_bytes = 0U;
    uint64_t persistent_misses = 0U;
    for (size_t index = 0U; index < engine_count; ++index) {
        require(results[index] == "1", "single-flight sharing leaked mutable globals");
        webscene_process_cache_metrics process_metrics{
            sizeof(webscene_process_cache_metrics)};
        require(
            webscene_engine_get_process_cache_metrics(
                engines[index],
                &process_metrics) != 0,
            "single-flight process metrics were unavailable");
        webscene_engine_metrics metrics{};
        webscene_engine_get_metrics(engines[index], &metrics);
        leaders += process_metrics.compilation_leaders;
        waiters += process_metrics.compilation_waiters;
        memory_hits += process_metrics.compilation_memory_hits;
        shared_bytes += process_metrics.compilation_shared_bytes;
        persistent_misses += metrics.compilation_persistent_misses;
    }

    require(
        leaders == 2U,
        "four engines did not elect exactly one producer for each cold compilation unit");
    if (std::getenv("WEBSCENE_V8_SHARED_ISOLATE") == nullptr) {
        require(
            memory_hits == (engine_count - 1U) * 2U,
            "cold compilation results were not consumed by every non-producing isolate");
    }
    // A shared isolate intentionally serializes V8 compilation entry. The
    // followers still reuse the producer's immutable cached data, but they can
    // arrive after production completes rather than blocking as waiters.
    if (std::getenv("WEBSCENE_V8_SHARED_ISOLATE") == nullptr) {
        require(
            waiters > 0U,
            "the concurrent cold-start test did not observe compilation waiters");
    }
    if (std::getenv("WEBSCENE_V8_SHARED_ISOLATE") == nullptr) {
        require(
            shared_bytes > 0U,
            "non-producing isolates did not consume shared immutable cache bytes");
    }
    require(
        persistent_misses == 2U,
        "more than one engine read the cold persistent cache for a compilation unit");
    webscene_engine_memory_metrics memory{
        sizeof(webscene_engine_memory_metrics)};
    require(
        webscene_engine_get_memory_metrics(engines.front(), &memory) != 0,
        "single-flight memory metrics were unavailable");
    require(
        memory.process_compilation_mapped_cache_bytes > 0U,
        "large persisted V8 compilation data was not retained as file-backed memory");

    for (auto* engine : engines) webscene_engine_destroy(engine);
    std::error_code cleanup_error;
    std::filesystem::remove_all(cache_directory, cleanup_error);
}
