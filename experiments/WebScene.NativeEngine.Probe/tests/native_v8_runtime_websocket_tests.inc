size_t load_websocket_cookie_document_v4(
    void*,
    uint32_t,
    const char* url,
    size_t url_length,
    const char*,
    size_t,
    int64_t,
    const webscene_resource_request_context_v4*,
    webscene_resource_response_v4* response,
    char* destination,
    size_t destination_capacity)
{
    static constexpr std::string_view document{"<!doctype html>"};
    static const webscene_resource_header_v4 headers[]{
        {sizeof(webscene_resource_header_v4), "Set-Cookie", 10U,
            "ws_auth=loopback; Path=/; HttpOnly; SameSite=Lax", 48U}};
    if (response != nullptr) {
        response->status = 200U;
        response->status_text = "OK";
        response->status_text_length = 2U;
        response->final_url = url;
        response->final_url_length = url_length;
        response->headers = headers;
        response->header_count = std::size(headers);
    }
    constexpr size_t header_size = 2U + sizeof(uint32_t) + sizeof(int64_t) * 2U;
    const auto required = header_size + document.size();
    if (destination == nullptr || destination_capacity < required) return required;
    std::memset(destination, 0, header_size);
    destination[0] = 1;
    std::copy(document.begin(), document.end(), destination + header_size);
    return required;
}

void test_native_websocket_browser_api()
{
    require(ix::initNetSystem(), "IXWebSocket network initialization failed");
    const auto port = ix::getFreePort();
    require(port > 0, "could not reserve a local WebSocket test port");

    std::mutex server_state_mutex;
    std::string observed_origin;
    std::string observed_cookie;
    ix::WebSocketServer server(port, "127.0.0.1");
    server.setOnClientMessageCallback(
        [&](std::shared_ptr<ix::ConnectionState>,
            ix::WebSocket& socket,
            const ix::WebSocketMessagePtr& message) {
            if (message->type == ix::WebSocketMessageType::Open) {
                std::lock_guard lock(server_state_mutex);
                const auto origin = message->openInfo.headers.find("Origin");
                if (origin != message->openInfo.headers.end()) {
                    observed_origin = origin->second;
                }
                const auto cookie = message->openInfo.headers.find("Cookie");
                if (cookie != message->openInfo.headers.end()) {
                    observed_cookie = cookie->second;
                }
            } else if (message->type == ix::WebSocketMessageType::Message) {
                socket.send(message->str, message->binary);
            }
        });
    const auto listening = server.listen();
    require(listening.first, "local WebSocket server could not listen");
    server.start();

    const auto document_address = std::string("http://127.0.0.1:")
        + std::to_string(port) + "/index.html";
    webscene_engine_options options{};
    options.struct_size = sizeof(options);
    options.resource_load_callback_v4 = load_websocket_cookie_document_v4;
    auto* engine = webscene_engine_create_with_options(&options);
    require(engine != nullptr, "WebSocket test engine creation failed");
    require(webscene_engine_load_url(
        engine, document_address.data(), document_address.size()) != 0U,
        "WebSocket cookie document navigation failed");
    require(webscene_engine_set_runtime_work_metrics_enabled(engine, 1U) != 0,
        "WebSocket runtime-work metrics could not be enabled");
    webscene_runtime_work_metrics work_before{
        sizeof(webscene_runtime_work_metrics)};
    webscene_engine_memory_metrics memory_before{
        sizeof(webscene_engine_memory_metrics)};
    require(webscene_engine_get_runtime_work_metrics(engine, &work_before) != 0,
        "WebSocket baseline work metrics were unavailable");
    for (auto attempt = 0; attempt < 100; ++attempt) {
        require(webscene_engine_get_memory_metrics(engine, &memory_before) != 0,
            "WebSocket baseline memory metrics were unavailable");
        if (memory_before.v8_used_heap_bytes != 0U) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(memory_before.v8_used_heap_bytes != 0U,
        "WebSocket baseline V8 heap snapshot was unavailable");
    require(
        evaluate(engine, "document.cookie", "native-websocket-cookie-visibility.js") == "\"\"",
        "HTTP-only navigation cookie leaked to document.cookie");
    const auto source = std::string(R"JS(
        globalThis.__webSceneWebSocketTest = {
          events: [], text: null, blobBinary: null, arrayBufferBinary: null,
          closed: false, closeCode: 0,
          closeReason: '', error: null, fairnessClosed: false,
          fairnessCycles: 0, fairnessLatencies: [], fairnessColdMs: null,
          fairnessP95Ms: null, maxPortMessagesBeforeEcho: 0,
          cancelCloseCount: 0, cancelMessages: 0, reconnectEcho: null
        };
        const socket = new WebSocket('ws://127.0.0.1:)JS")
        + std::to_string(port)
        + R"JS(/echo');
        socket.addEventListener('open', () => {
          __webSceneWebSocketTest.events.push('open');
          socket.send('native-text');
        });
        socket.addEventListener('message', event => {
          __webSceneWebSocketTest.events.push('message');
          if (typeof event.data === 'string') {
            __webSceneWebSocketTest.text = event.data;
            socket.send(new Uint8Array([0, 1, 254, 255]));
          } else if (event.data instanceof Blob) {
            const reader = new FileReader();
            reader.addEventListener('load', () => {
              __webSceneWebSocketTest.blobBinary =
                Array.from(new Uint8Array(reader.result));
              socket.binaryType = 'arraybuffer';
              socket.send(new Uint8Array([9, 8, 7]));
            });
            reader.addEventListener('error', () => {
              __webSceneWebSocketTest.error = reader.error?.message || 'file read error';
              socket.close(1011, 'file read failed');
            });
            reader.readAsArrayBuffer(event.data);
          } else {
            __webSceneWebSocketTest.arrayBufferBinary =
              Array.from(new Uint8Array(event.data));
            socket.close(1000, 'complete');
          }
        });
        socket.addEventListener('error', event => {
          __webSceneWebSocketTest.error = event.message || 'error';
        });
        socket.addEventListener('close', event => {
          __webSceneWebSocketTest.events.push('close');
          __webSceneWebSocketTest.closed = true;
          __webSceneWebSocketTest.closeCode = event.code;
          __webSceneWebSocketTest.closeReason = event.reason;
          const runReconnect = () => {
            const reconnect = new WebSocket('ws://127.0.0.1:)JS"
        + std::to_string(port)
        + R"JS(/echo');
            reconnect.addEventListener('open', () => reconnect.send('reconnect'));
            reconnect.addEventListener('message', reconnectEvent => {
              __webSceneWebSocketTest.reconnectEcho = reconnectEvent.data;
              reconnect.close(1000, 'reconnected');
            });
            reconnect.addEventListener('close', () => {
              __webSceneWebSocketTest.fairnessClosed = true;
            });
          };
          const runCancellation = () => {
            const cancelled = new WebSocket('ws://127.0.0.1:)JS"
        + std::to_string(port)
        + R"JS(/echo');
            cancelled.addEventListener('open', () => {
              cancelled.send('cancelled');
              cancelled.close(1000, 'cancelled');
            });
            cancelled.addEventListener('message', () => {
              ++__webSceneWebSocketTest.cancelMessages;
            });
            cancelled.addEventListener('close', () => {
              ++__webSceneWebSocketTest.cancelCloseCount;
              runReconnect();
            });
          };
          const runFairness = () => {
            const cycle = __webSceneWebSocketTest.fairnessCycles;
            const fairness = new WebSocket('ws://127.0.0.1:)JS"
        + std::to_string(port)
        + R"JS(/echo');
            let complete = false;
          fairness.addEventListener('open', () => {
            const channel = new MessageChannel();
            let messages = 0;
            const started = performance.now();
            channel.port1.addEventListener('message', () => {
              ++messages;
              if (!complete && messages < 100000) {
                channel.port2.postMessage(messages);
              }
            });
            channel.port1.start();
            channel.port2.postMessage(0);
            fairness.send('fairness-' + cycle);
            fairness.addEventListener('message', fairnessEvent => {
              if (fairnessEvent.data !== 'fairness-' + cycle) return;
              complete = true;
              const elapsed = performance.now() - started;
              __webSceneWebSocketTest.fairnessLatencies.push(elapsed);
              __webSceneWebSocketTest.maxPortMessagesBeforeEcho = Math.max(
                __webSceneWebSocketTest.maxPortMessagesBeforeEcho, messages);
              fairness.close(1000, 'fair');
            });
          });
            fairness.addEventListener('close', () => {
              ++__webSceneWebSocketTest.fairnessCycles;
              if (__webSceneWebSocketTest.fairnessCycles < 100) {
                runFairness();
                return;
              }
              const ordered = [...__webSceneWebSocketTest.fairnessLatencies]
                .sort((left, right) => left - right);
              __webSceneWebSocketTest.fairnessColdMs =
                __webSceneWebSocketTest.fairnessLatencies[0];
              __webSceneWebSocketTest.fairnessP95Ms = ordered[Math.ceil(ordered.length * .95) - 1];
              runCancellation();
            });
          };
          runFairness();
        });
    )JS";
    execute(engine, source, "native-websocket-browser-api.js");

    // Do not poll the engine while the socket lifecycle is active. Every
    // evaluation is itself worker work and would hide a missing transport wake
    // by repeatedly releasing the runtime from its idle wait.
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    const auto state = evaluate(
        engine,
        "globalThis.__webSceneWebSocketTest",
        "native-websocket-browser-api-state.js");
    require(
        state.find("\"events\":[\"open\",\"message\",\"message\",\"message\",\"close\"]")
                != std::string::npos
            && state.find("\"text\":\"native-text\"") != std::string::npos
            && state.find("\"blobBinary\":[0,1,254,255]") != std::string::npos
            && state.find("\"arrayBufferBinary\":[9,8,7]") != std::string::npos
            && state.find("\"closeCode\":1000") != std::string::npos
            && state.find("\"closeReason\":\"complete\"") != std::string::npos
            && state.find("\"fairnessClosed\":true") != std::string::npos
            && state.find("\"error\":null") != std::string::npos,
        "browser WebSocket API did not complete text/binary/close echo lifecycle");
    const auto number_field = [&](std::string_view name) {
        const auto key = std::string("\"") + std::string(name) + "\":";
        const auto position = state.find(key);
        require(position != std::string::npos,
            "WebSocket evidence field was not retained: " + std::string(name));
        return std::stod(state.substr(position + key.size()));
    };
    const auto max_messages_before_echo = number_field("maxPortMessagesBeforeEcho");
    const auto cold_ms = number_field("fairnessColdMs");
    const auto p95_ms = number_field("fairnessP95Ms");
    const auto cancellation_messages = number_field("cancelMessages");
    require(max_messages_before_echo <= 64.0,
        "MessagePort backlog starved a ready WebSocket response");
    require(cold_ms <= 500.0 && p95_ms <= 250.0,
        "WebSocket cold or warm fairness latency exceeded its bound");
    require(state.find("\"fairnessCycles\":100") != std::string::npos
            && state.find("\"cancelCloseCount\":1") != std::string::npos
            && state.find("\"reconnectEcho\":\"reconnect\"") != std::string::npos
            && cancellation_messages <= 1.0,
        "WebSocket cancellation/reconnect lifecycle did not settle exactly once");
    {
        std::lock_guard lock(server_state_mutex);
        require(
            observed_origin == "http://127.0.0.1:" + std::to_string(port),
            "browser WebSocket API did not forward the document origin");
        require(
            observed_cookie.find("ws_auth=loopback") != std::string::npos,
            "browser WebSocket API did not forward eligible cookie state");
    }

    require(webscene_engine_request_low_memory(engine) != 0,
        "WebSocket lifecycle low-memory request was rejected");
    webscene_runtime_work_metrics work_after{
        sizeof(webscene_runtime_work_metrics)};
    webscene_engine_memory_metrics memory_after{
        sizeof(webscene_engine_memory_metrics)};
    for (auto attempt = 0; attempt < 500; ++attempt) {
        require(
            webscene_engine_get_runtime_work_metrics(engine, &work_after) != 0
                && webscene_engine_get_memory_metrics(engine, &memory_after) != 0,
            "WebSocket final work or memory metrics were unavailable");
        if (memory_after.low_memory_notifications
                > memory_before.low_memory_notifications
            && work_after.worker_signalled_wakes
                > work_before.worker_signalled_wakes) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(
        memory_after.low_memory_notifications
                > memory_before.low_memory_notifications
            && memory_after.v8_used_heap_bytes
                <= memory_before.v8_used_heap_bytes + 8U * 1024U * 1024U,
        "WebSocket lifecycle retained more than 8 MiB of V8 heap");
    require(
        work_after.worker_signalled_wakes > work_before.worker_signalled_wakes,
        "WebSocket transport did not wake the idle runtime worker");
    std::cout << "WebSocket fairness gate: cycles=100 coldMs=" << cold_ms
              << " p95Ms=" << p95_ms
              << " maxPortMessagesBeforeEcho=" << max_messages_before_echo
              << " signalledWakes="
              << (work_after.worker_signalled_wakes
                  - work_before.worker_signalled_wakes)
              << " timeoutWakes="
              << (work_after.worker_timeout_wakes
                  - work_before.worker_timeout_wakes)
              << " heapBefore=" << memory_before.v8_used_heap_bytes
              << " heapAfter=" << memory_after.v8_used_heap_bytes << "\n";

    webscene_engine_destroy(engine);
    server.stop();
    require(ix::uninitNetSystem(), "IXWebSocket network cleanup failed");
}

} // namespace
