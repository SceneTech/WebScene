void test_nested_drag_drop_routing_and_retirement()
{
    constexpr std::string_view address{"https://iframe-drag.test/index.html"};
    resource_server server{
        .content = {{std::string{address},
                     "<!doctype html><title>drag outer</title><body></body>"},
                    {"https://iframe-drag.test/frame.html", R"HTML(
<!doctype html><style>
html,body{margin:0} .target{position:absolute;top:8px;width:40px;height:30px}
#left{left:10px} #right{left:60px}
</style><body><div id="left" class="target"></div><div id="right" class="target"></div>
<script>
  const state = parent.__nestedDrag;
  let firstTransfer = null;
  for (const target of document.querySelectorAll('.target')) {
    for (const type of ['dragenter','dragover','dragleave','drop']) {
      target.addEventListener(type, event => {
        state.events.push([type,target.id,event.relatedTarget?.id || null,
          event.clientX,event.clientY,event.isTrusted,event instanceof DragEvent]);
        state.sameTransfer &&= !firstTransfer || firstTransfer === event.dataTransfer;
        firstTransfer ||= event.dataTransfer;
        if (type === 'dragover' && target.id === 'left') event.preventDefault();
        if (type !== 'drop') return;
        ++state.drops;
        const transfer = event.dataTransfer;
        state.payload = {
          uri: transfer.getData('text/uri-list'),
          types: Array.from(transfer.types),
          items: transfer.items.length,
          files: transfer.files.length,
          name: transfer.files.item(0)?.name,
          path: transfer.files.item(0)?.webkitRelativePath,
          size: transfer.files.item(0)?.size,
          dropEffect: (transfer.dropEffect='copy',transfer.dropEffect)
        };
        const root = Array.from(transfer.items)
          .map(item => item.webkitGetAsEntry?.()).find(entry => entry?.isDirectory);
        root?.createReader().readEntries(entries => {
          state.children = entries.map(entry => [entry.name,entry.isFile,entry.fullPath]);
        });
      });
    }
  }
  state.ready = true;
</script>)HTML"}};
    webscene_engine_options options{};
    options.struct_size = sizeof(options);
    options.resource_load_callback = load_test_resource;
    options.resource_load_user_data = &server;
    auto* engine = webscene_engine_create_with_options(&options);
    require(engine != nullptr, "nested drag engine creation failed");
    require(webscene_engine_load_url(engine, address.data(), address.size()) != 0U,
        "nested drag outer navigation failed");
    require(evaluate_until_equals(engine, "location.href", "nested-drag-ready.js",
                R"("https://iframe-drag.test/index.html")", 2'500)
            == R"("https://iframe-drag.test/index.html")",
        "nested drag outer document did not become ready");
    execute(engine, R"JS(
      globalThis.__nestedDrag = {
        ready:false, events:[], drops:0, sameTransfer:true,
        payload:null, children:null
      };
      document.body.style.margin='0';
      const frame=document.createElement('iframe');
      frame.id='webview';
      frame.setAttribute('sandbox','allow-same-origin allow-scripts');
      frame.style.cssText='position:absolute;left:80px;top:60px;width:200px;height:120px;border:4px solid black';
      frame.src='./frame.html';
      document.body.appendChild(frame);
    )JS", "nested-drag-setup.js");
    require(evaluate_until_equals(engine, "__nestedDrag.ready",
                "nested-drag-frame-ready.js", "true", 3'000) == "true",
        "nested drag child document did not become ready");

    const std::string uri{"https://example.test/dragged\r\n"};
    const std::array<uint8_t, 3> file_bytes{'A','B','C'};
    std::array<webscene_drag_item_v1, 3> items{};
    const auto set_item = [](webscene_drag_item_v1& item, uint32_t kind,
                              const char* type, const char* name,
                              const char* path, const uint8_t* bytes,
                              size_t byte_count) {
        item.struct_size = sizeof(item);
        item.version = 1U;
        item.kind = kind;
        item.mime_type = type;
        item.mime_type_length = type == nullptr ? 0U : std::strlen(type);
        item.name = name;
        item.name_length = name == nullptr ? 0U : std::strlen(name);
        item.relative_path = path;
        item.relative_path_length = path == nullptr ? 0U : std::strlen(path);
        item.bytes = bytes;
        item.byte_count = byte_count;
    };
    set_item(items[0], WEBSCENE_DRAG_ITEM_STRING_V1, "text/uri-list",
        nullptr, nullptr, reinterpret_cast<const uint8_t*>(uri.data()), uri.size());
    set_item(items[1], WEBSCENE_DRAG_ITEM_DIRECTORY_V1, "", "root", "root",
        nullptr, 0U);
    set_item(items[2], WEBSCENE_DRAG_ITEM_FILE_V1, "text/plain", "note.txt",
        "root/note.txt", file_bytes.data(), file_bytes.size());

    uint64_t sequence = 12'000U;
    const auto dispatch = [&](uint64_t session, uint32_t action,
                              double x, double y, bool include_items = false) {
        webscene_drag_event_v1 event{};
        event.struct_size = sizeof(event);
        event.version = 1U;
        event.session_id = session;
        event.sequence = sequence++;
        event.action = action;
        event.x = x;
        event.y = y;
        event.items = include_items ? items.data() : nullptr;
        event.item_count = include_items ? items.size() : 0U;
        return webscene_engine_dispatch_drag_v1(engine, &event) != 0U;
    };
    auto invalid = items[2];
    invalid.relative_path = "../escape.txt";
    invalid.relative_path_length = std::strlen(invalid.relative_path);
    webscene_drag_event_v1 invalid_event{
        sizeof(webscene_drag_event_v1), 1U, 7U, sequence++,
        WEBSCENE_DRAG_ENTER_V1, 0U, 99, 77, &invalid, 1U};
    require(webscene_engine_dispatch_drag_v1(engine, &invalid_event) == 0U,
        "drag ABI accepted a parent-traversing relative path");

    require(dispatch(1U, WEBSCENE_DRAG_ENTER_V1, 99, 77, true)
            && dispatch(1U, WEBSCENE_DRAG_OVER_V1, 99, 77)
            && dispatch(1U, WEBSCENE_DRAG_DROP_V1, 99, 77),
        "nested admitted drag sequence was rejected");
    require(evaluate_until_equals(engine, "__nestedDrag.children !== null",
                "nested-drag-drop-ready.js", "true", 2'500) == "true",
        "nested drop callback did not drain");
    const auto admitted = evaluate(engine, R"JS(JSON.stringify({
      drops:__nestedDrag.drops,
      sameTransfer:__nestedDrag.sameTransfer,
      payload:__nestedDrag.payload,
      children:__nestedDrag.children,
      eventShape:__nestedDrag.events.slice(0,3)
    }))JS", "nested-drag-admitted-result.js");
    require(admitted == R"JSON({"drops":1,"sameTransfer":true,"payload":{"uri":"https://example.test/dragged\r\n","types":["text/uri-list","Files"],"items":2,"files":1,"name":"note.txt","path":"root/note.txt","size":3,"dropEffect":"copy"},"children":[["note.txt",true,"/root/note.txt"]],"eventShape":[["dragenter","left",null,15,13,true,true],["dragover","left",null,15,13,true,true],["drop","left",null,15,13,true,true]]})JSON",
        "nested admitted drag or DataTransfer shape diverged: " + admitted);

    execute_and_wait(engine,
        "__nestedDrag.events=[];__nestedDrag.drops=0;__nestedDrag.children=null",
        "nested-drag-route-reset.js");
    require(dispatch(2U, WEBSCENE_DRAG_ENTER_V1, 99, 77, true)
            && dispatch(2U, WEBSCENE_DRAG_OVER_V1, 99, 77)
            && dispatch(2U, WEBSCENE_DRAG_OVER_V1, 149, 77)
            && dispatch(2U, WEBSCENE_DRAG_DROP_V1, 149, 77),
        "nested retargeted drag sequence was rejected");
    const auto retargeted = evaluate(engine, R"JS(JSON.stringify({
      drops:__nestedDrag.drops,
      route:__nestedDrag.events.map(event=>event.slice(0,3))
    }))JS", "nested-drag-retarget-result.js");
    require(retargeted == R"JSON({"drops":0,"route":[["dragenter","left",null],["dragover","left",null],["dragleave","left","right"],["dragenter","right","left"],["dragover","right",null]]})JSON",
        "nested drag target transition or drop admission diverged: " + retargeted);

    require(dispatch(3U, WEBSCENE_DRAG_ENTER_V1, 99, 77, true)
            && dispatch(3U, WEBSCENE_DRAG_OVER_V1, 99, 77),
        "pre-navigation drag was rejected");
    execute_and_wait(engine,
        "document.querySelector('#webview').src='./frame.html?generation=2'",
        "nested-drag-frame-navigation.js");
    require(dispatch(3U, WEBSCENE_DRAG_DROP_V1, 99, 77),
        "retired generation drag completion was rejected");
    require(evaluate(engine, "__nestedDrag.drops", "nested-drag-retired-result.js")
            == "0",
        "a drag session survived child-document navigation");

    require(dispatch(4U, WEBSCENE_DRAG_ENTER_V1, 99, 77, true),
        "dragover coalescing session was rejected");
    const auto coalescing_started = std::chrono::steady_clock::now();
    for (uint32_t index = 0U; index < 10'000U; ++index) {
        require(dispatch(4U, WEBSCENE_DRAG_OVER_V1, 99, 77),
            "identical dragover queue coalescing was rejected");
    }
    require(dispatch(4U, WEBSCENE_DRAG_CANCEL_V1, 99, 77),
        "coalesced drag session cancellation was rejected");
    const auto coalescing_elapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - coalescing_started).count();
    require(coalescing_elapsed < 5.0,
        "10,000 identical dragover submissions exceeded five seconds");
    static_cast<void>(evaluate(engine, "true", "nested-drag-coalescing-barrier.js"));

    webscene_engine_memory_metrics memory_before{sizeof(memory_before)};
    require(webscene_engine_get_memory_metrics(engine, &memory_before) != 0,
        "drag lifecycle memory baseline was unavailable");
    const auto started = std::chrono::steady_clock::now();
    for (uint64_t session = 100U; session < 200U; ++session) {
        require(dispatch(session, WEBSCENE_DRAG_ENTER_V1, 99, 77, true)
                && dispatch(session, WEBSCENE_DRAG_OVER_V1, 99, 77)
                && dispatch(session, WEBSCENE_DRAG_CANCEL_V1, 99, 77),
            "bounded drag lifecycle cycle was rejected");
    }
    const auto elapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - started).count();
    require(elapsed < 5.0,
        "100 copied drag ownership/retirement cycles exceeded five seconds");
    static_cast<void>(evaluate(engine, "true", "nested-drag-cycle-barrier.js"));
    require(webscene_engine_request_low_memory(engine) != 0,
        "drag lifecycle low-memory request was rejected");
    webscene_engine_memory_metrics memory_after{sizeof(memory_after)};
    for (auto attempt = 0; attempt < 250; ++attempt) {
        require(webscene_engine_get_memory_metrics(engine, &memory_after) != 0,
            "drag lifecycle final memory metrics were unavailable");
        if (memory_after.low_memory_notifications
            > memory_before.low_memory_notifications) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(memory_after.low_memory_notifications
                > memory_before.low_memory_notifications
            && memory_after.v8_used_heap_bytes
                <= memory_before.v8_used_heap_bytes + 4U * 1024U * 1024U
            && memory_after.native_dom_node_count
                <= memory_before.native_dom_node_count + 16U,
        "drag ownership/retirement cycles retained V8 heap or native DOM nodes");
    std::cout << "Nested drag gate: dragovers=10000 coalescing="
              << coalescing_elapsed << "s cycles=100 elapsed=" << elapsed
              << "s heapBefore=" << memory_before.v8_used_heap_bytes
              << " heapAfter=" << memory_after.v8_used_heap_bytes << '\n';
    webscene_engine_destroy(engine);
}
