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

void test_native_websocket_protocol_handshake_timing()
{
    require(ix::initNetSystem(), "protocol timing network initialization failed");
    const auto port = ix::getFreePort();
    require(port > 0, "protocol timing server could not reserve a port");

    std::mutex completion_mutex;
    std::condition_variable completion_changed;
    bool completed = false;
    std::mutex sender_mutex;
    std::vector<std::thread> senders;
    std::shared_ptr<std::atomic<bool>> active_background;
    constexpr auto background_frames_before_ready = 1U;
    constexpr auto background_frames_after_ready = 63U;
    constexpr auto background_frames_after_init = 4U;
    constexpr auto background_bytes = 192U * 1024U;
    const auto packet = [](char kind, size_t size = 32U) {
        const auto epoch = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        auto value = std::string(1U, kind) + ":" + std::to_string(epoch) + ":";
        value.resize(std::max(value.size(), size), 'x');
        return value;
    };
    ix::WebSocketServer server(port, "127.0.0.1");
    server.setOnClientMessageCallback(
        [&](std::shared_ptr<ix::ConnectionState>,
            ix::WebSocket& socket,
            const ix::WebSocketMessagePtr& message) {
            if (message->type != ix::WebSocketMessageType::Message) return;
            if (message->str.starts_with("handshake-start:")) {
                for (auto index = 0U; index < background_frames_before_ready; ++index) {
                    socket.sendBinary(packet('B', background_bytes));
                }
                socket.sendBinary(packet('R'));
                auto stop = std::make_shared<std::atomic<bool>>(false);
                {
                    std::lock_guard lock(sender_mutex);
                    active_background = stop;
                    senders.emplace_back([&socket, stop, packet, background_bytes] {
                        for (auto index = 0U;
                             index < background_frames_after_ready && !stop->load();
                             ++index) {
                            socket.sendBinary(packet('B', background_bytes));
                            std::this_thread::sleep_for(std::chrono::milliseconds(10));
                        }
                    });
                }
            } else if (const auto marker = message->str.find("handshake-init:");
                       marker != std::string::npos) {
                {
                    std::lock_guard lock(sender_mutex);
                    if (active_background) active_background->store(true);
                }
                const auto timestamp_start = marker + std::string_view("handshake-init:").size();
                const auto timestamp_end = message->str.find(':', timestamp_start);
                const auto client_sent_at = timestamp_end == std::string::npos
                    ? 0LL
                    : std::stoll(message->str.substr(
                          timestamp_start,
                          timestamp_end - timestamp_start));
                const auto received_at = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                for (auto index = 0U; index < background_frames_after_init; ++index) {
                    socket.sendBinary(packet('B', background_bytes));
                }
                socket.sendBinary(
                    std::string("I:") + std::to_string(received_at) + ":"
                    + std::to_string(received_at - client_sent_at) + ":"
                    + std::to_string(message->wireSize));
            } else if (message->str == "handshake-complete") {
                {
                    std::lock_guard lock(completion_mutex);
                    completed = true;
                }
                completion_changed.notify_one();
            }
        });
    const auto listening = server.listen();
    require(listening.first, "protocol timing server could not listen");
    server.start();

    webscene_engine_options options{};
    options.struct_size = sizeof(options);
    auto* engine = webscene_engine_create_with_options(&options);
    require(engine != nullptr, "protocol timing engine creation failed");
    evaluate(engine, "void 0", "native-websocket-protocol-timing-warmup.js");
    require(webscene_engine_set_runtime_work_metrics_enabled(engine, 1U) != 0,
        "protocol timing work metrics could not be enabled");
    webscene_runtime_work_metrics work_before{sizeof(webscene_runtime_work_metrics)};
    webscene_engine_memory_metrics memory_before{sizeof(webscene_engine_memory_metrics)};
    require(webscene_engine_get_runtime_work_metrics(engine, &work_before) != 0,
        "protocol timing baseline work metrics were unavailable");
    for (auto attempt = 0; attempt < 100; ++attempt) {
        require(webscene_engine_get_memory_metrics(engine, &memory_before) != 0,
            "protocol timing baseline memory metrics were unavailable");
        if (memory_before.v8_used_heap_bytes != 0U) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(memory_before.v8_used_heap_bytes != 0U,
        "protocol timing baseline V8 heap snapshot was unavailable");

    const auto source = std::string(R"JS(
        globalThis.__webSceneProtocolTiming = {
          complete:false, error:null, cycles:0, backgroundFrames:0,
          readyMs:[], initializedMs:[], outboundDeliveryMs:[], socketDispatchMs:[], fileReaderMs:[],
          microtaskMs:[], serializationMs:[], protocolFlushDelayMs:[], sendReturnMs:[], drainMs:[], closeCount:0,
          extensions:null, receiveQueueHighWater:0, bufferedAmountHighWater:0, initWireBytes:[],
          workerTasksBeforeFlush:[], portTasksBeforeFlush:[], windowTasksBeforeFlush:[], socketTasksBeforeFlush:[],
          fileReaderTasksBeforeFlush:[], sourceOldestAgeMs:[], workerQueueHighWater:0,
          portQueueHighWater:0, socketQueueHighWater:0, fileReaderQueueHighWater:0
        };
        (() => {
          const state = __webSceneProtocolTiming;
          const socket = new WebSocket('ws://127.0.0.1:)JS")
        + std::to_string(port)
        + R"JS(/protocol');
          const decoder = new TextDecoder();
          const initBodyBytes = 1071425;
          const protocolHeaderBytes = 13;
          const wireBytes = protocolHeaderBytes + initBodyBytes;
          state.initApplicationBytes = wireBytes;
          const initPayload = 'x'.repeat(initBodyBytes - 64);
          const encoder = new TextEncoder();
          let cycleStarted = 0;
          let readyAt = 0;
          let flushPending = false;
          let sourceStarted = 0;
          let workerTasks = 0;
          let portTasks = 0;
          let windowTasks = 0;
          let socketTasks = 0;
          let fileReaderTasks = 0;
          let workerOutstanding = 0;
          let portOutstanding = 0;
          // The first cycle retains the product's multi-second starvation
          // shape on an unfair scheduler; warm cycles keep the p95 workload
          // bounded while exercising the same continuously ready sources.
          const sourceTaskLimit = () => state.cycles === 0 ? 700000 : 20000;
          const workerUrl = URL.createObjectURL(new Blob([
            `onmessage = event => postMessage(event.data + 1); postMessage('ready');`
          ], { type:'text/javascript' }));
          const worker = new Worker(workerUrl);
          const channel = new MessageChannel();
          let workerReady = false;
          let socketReady = false;
          const maybeBegin = () => {
            if (workerReady && socketReady && state.cycles === 0 && cycleStarted === 0) begin();
          };
          worker.onmessage = event => {
            if (event.data === 'ready') {
              workerReady = true;
              maybeBegin();
              return;
            }
            --workerOutstanding;
            ++workerTasks;
            if (flushPending && workerTasks < sourceTaskLimit()) {
              ++workerOutstanding;
              state.workerQueueHighWater = Math.max(state.workerQueueHighWater, workerOutstanding);
              worker.postMessage(workerTasks);
            }
          };
          channel.port1.onmessage = () => {
            --portOutstanding;
            ++portTasks;
            if (flushPending && portTasks < sourceTaskLimit()) {
              ++portOutstanding;
              state.portQueueHighWater = Math.max(state.portQueueHighWater, portOutstanding);
              channel.port2.postMessage(portTasks);
            }
          };
          channel.port1.start();
          addEventListener('message', event => {
            if (!event.data || event.data.__protocolWindowFairness !== true) return;
            ++windowTasks;
            if (flushPending && windowTasks < sourceTaskLimit()) {
              postMessage({__protocolWindowFairness:true}, '*');
            }
          });
          const begin = () => {
            cycleStarted = performance.now();
            socket.send('handshake-start:' + state.cycles);
          };
          const percentile = values => {
            const ordered = [...values].sort((a, b) => a - b);
            return ordered[Math.ceil(ordered.length * .95) - 1];
          };
          const receiveQueue = [];
          const reader = new FileReader();
          let currentBlob = null;
          const readNext = () => {
            if (currentBlob !== null || receiveQueue.length === 0) return;
            currentBlob = receiveQueue.shift();
            const readStarted = performance.now();
            reader.onload = () => {
              if (flushPending) ++fileReaderTasks;
              const loadedAt = performance.now();
              state.fileReaderMs.push(loadedAt - readStarted);
              const text = decoder.decode(new Uint8Array(reader.result));
              currentBlob = null;
              const first = text.indexOf(':');
              const second = text.indexOf(':', first + 1);
              const kind = text.slice(0, first);
              const sentAt = Number(text.slice(first + 1, second));
              if (kind === 'B') { ++state.backgroundFrames; readNext(); return; }
              state.socketDispatchMs.push(Date.now() - sentAt);
              if (kind === 'R') {
                readyAt = loadedAt;
                state.readyMs.push(readyAt - cycleStarted);
                const microtaskQueued = performance.now();
                Promise.resolve().then(() => {
                  state.microtaskMs.push(performance.now() - microtaskQueued);
                  const serializationStarted = performance.now();
                  JSON.stringify({ cycle:state.cycles, payload:initPayload });
                  state.serializationMs.push(performance.now() - serializationStarted);
                  const protocolFlushQueued = performance.now();
                  flushPending = true;
                  sourceStarted = protocolFlushQueued;
                  workerTasks = portTasks = windowTasks = socketTasks = fileReaderTasks = 0;
                  setTimeout(() => {
                    state.protocolFlushDelayMs.push(performance.now() - protocolFlushQueued);
                    state.workerTasksBeforeFlush.push(workerTasks);
                    state.portTasksBeforeFlush.push(portTasks);
                    state.windowTasksBeforeFlush.push(windowTasks);
                    state.socketTasksBeforeFlush.push(socketTasks);
                    state.fileReaderTasksBeforeFlush.push(fileReaderTasks);
                    state.sourceOldestAgeMs.push(performance.now() - sourceStarted);
                    flushPending = false;
                    const marker = encoder.encode('handshake-init:' + Date.now() + ':');
                    const init = new Uint8Array(wireBytes);
                    init.fill(120);
                    init[0] = 1;
                    new DataView(init.buffer).setUint32(9, initBodyBytes);
                    init.set(marker, protocolHeaderBytes);
                    const sendStarted = performance.now();
                    socket.send(init);
                    state.sendReturnMs.push(performance.now() - sendStarted);
                    state.bufferedAmountHighWater = Math.max(state.bufferedAmountHighWater, socket.bufferedAmount);
                    const drainStarted = performance.now();
                    const observeDrain = () => {
                      state.bufferedAmountHighWater = Math.max(state.bufferedAmountHighWater, socket.bufferedAmount);
                      if (socket.bufferedAmount === 0) state.drainMs.push(performance.now() - drainStarted);
                      else setTimeout(observeDrain, 0);
                    };
                    observeDrain();
                  }, 0);
                  ++workerOutstanding;
                  ++portOutstanding;
                  state.workerQueueHighWater = Math.max(state.workerQueueHighWater, workerOutstanding);
                  state.portQueueHighWater = Math.max(state.portQueueHighWater, portOutstanding);
                  worker.postMessage(0);
                  channel.port2.postMessage(0);
                  postMessage({__protocolWindowFairness:true}, '*');
                });
                readNext(); return;
              }
              if (kind !== 'I') { readNext(); return; }
              const third = text.indexOf(':', second + 1);
              const fourth = text.indexOf(':', third + 1);
              state.outboundDeliveryMs.push(Number(text.slice(second + 1, third)));
              state.initWireBytes.push(Number(text.slice(third + 1, fourth < 0 ? undefined : fourth)));
              state.initializedMs.push(loadedAt - readyAt);
              ++state.cycles;
              if (state.cycles < 20) { begin(); readNext(); return; }
              state.readyP95Ms = percentile(state.readyMs);
              state.initializedP95Ms = percentile(state.initializedMs);
              state.outboundDeliveryP95Ms = percentile(state.outboundDeliveryMs);
              state.socketDispatchP95Ms = percentile(state.socketDispatchMs);
              state.fileReaderP95Ms = percentile(state.fileReaderMs);
              state.microtaskP95Ms = percentile(state.microtaskMs);
              state.serializationP95Ms = percentile(state.serializationMs);
              state.protocolFlushDelayP95Ms = percentile(state.protocolFlushDelayMs);
              state.protocolFlushDelayMaxMs = Math.max(...state.protocolFlushDelayMs);
              state.workerTasksBeforeFlushMax = Math.max(...state.workerTasksBeforeFlush);
              state.portTasksBeforeFlushMax = Math.max(...state.portTasksBeforeFlush);
              state.windowTasksBeforeFlushMax = Math.max(...state.windowTasksBeforeFlush);
              state.socketTasksBeforeFlushMax = Math.max(...state.socketTasksBeforeFlush);
              state.fileReaderTasksBeforeFlushMax = Math.max(...state.fileReaderTasksBeforeFlush);
              state.sourceOldestAgeMaxMs = Math.max(...state.sourceOldestAgeMs);
              state.sendReturnP95Ms = percentile(state.sendReturnMs);
              state.drainP95Ms = percentile(state.drainMs);
              state.complete = true;
              worker.terminate();
              URL.revokeObjectURL(workerUrl);
              channel.port1.close();
              channel.port2.close();
              socket.send('handshake-complete');
              socket.close(1000, 'complete');
            };
            reader.onerror = () => {
              state.error = reader.error?.message || 'FileReader failed';
              socket.close(1011, 'read failed');
            };
            reader.readAsArrayBuffer(currentBlob);
          };
          socket.onopen = () => {
            state.extensions = socket.extensions;
            socketReady = true;
            maybeBegin();
          };
          socket.onmessage = event => {
            if (!(event.data instanceof Blob)) {
              state.error = 'binary protocol frame was not exposed as Blob';
              socket.close(1011, 'invalid frame');
              return;
            }
            receiveQueue.push(event.data);
            state.receiveQueueHighWater = Math.max(state.receiveQueueHighWater, receiveQueue.length);
            state.socketQueueHighWater = Math.max(state.socketQueueHighWater, receiveQueue.length);
            state.fileReaderQueueHighWater = Math.max(
              state.fileReaderQueueHighWater, receiveQueue.length + (currentBlob === null ? 0 : 1));
            if (flushPending) ++socketTasks;
            readNext();
          };
          socket.onerror = event => { state.error = event.message || 'socket error'; };
          socket.onclose = () => { ++state.closeCount; };
        })();
    )JS";
    execute(engine, source, "native-websocket-protocol-timing.js");

    {
        std::unique_lock lock(completion_mutex);
        completion_changed.wait_for(lock, std::chrono::seconds(40), [&] {
            return completed;
        });
    }
    auto state = std::string{};
    for (auto attempt = 0; attempt < 100; ++attempt) {
        state = evaluate(
            engine,
            "globalThis.__webSceneProtocolTiming",
            "native-websocket-protocol-timing-state.js");
        if (state.find("\"closeCount\":1") != std::string::npos) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    const auto number_field = [&](std::string_view name) {
        const auto key = std::string("\"") + std::string(name) + "\":";
        const auto position = state.find(key);
        require(position != std::string::npos,
            "protocol timing evidence field was not retained: " + std::string(name));
        return std::stod(state.substr(position + key.size()));
    };
    const auto ready_p95 = number_field("readyP95Ms");
    const auto initialized_p95 = number_field("initializedP95Ms");
    const auto outbound_p95 = number_field("outboundDeliveryP95Ms");
    const auto socket_p95 = number_field("socketDispatchP95Ms");
    const auto reader_p95 = number_field("fileReaderP95Ms");
    const auto microtask_p95 = number_field("microtaskP95Ms");
    const auto serialization_p95 = number_field("serializationP95Ms");
    const auto protocol_flush_p95 = number_field("protocolFlushDelayP95Ms");
    const auto protocol_flush_max = number_field("protocolFlushDelayMaxMs");
    const auto send_return_p95 = number_field("sendReturnP95Ms");
    const auto drain_p95 = number_field("drainP95Ms");
    std::cout << "Remote protocol timing gate: cycles=20 readyP95Ms="
              << ready_p95 << " initializedP95Ms=" << initialized_p95
              << " outboundDeliveryP95Ms=" << outbound_p95
              << " socketDispatchP95Ms=" << socket_p95
              << " fileReaderP95Ms=" << reader_p95
              << " microtaskP95Ms=" << microtask_p95
              << " serializationP95Ms=" << serialization_p95
              << " protocolFlushDelayP95Ms=" << protocol_flush_p95
              << " protocolFlushDelayMaxMs=" << protocol_flush_max
              << " workerTasksBeforeFlushMax=" << number_field("workerTasksBeforeFlushMax")
              << " portTasksBeforeFlushMax=" << number_field("portTasksBeforeFlushMax")
              << " windowTasksBeforeFlushMax=" << number_field("windowTasksBeforeFlushMax")
              << " socketTasksBeforeFlushMax=" << number_field("socketTasksBeforeFlushMax")
              << " fileReaderTasksBeforeFlushMax=" << number_field("fileReaderTasksBeforeFlushMax")
              << " sourceOldestAgeMaxMs=" << number_field("sourceOldestAgeMaxMs")
              << " workerQueueHighWater=" << number_field("workerQueueHighWater")
              << " portQueueHighWater=" << number_field("portQueueHighWater")
              << " socketQueueHighWater=" << number_field("socketQueueHighWater")
              << " fileReaderQueueHighWater=" << number_field("fileReaderQueueHighWater")
              << " sendReturnP95Ms=" << send_return_p95
              << " drainP95Ms=" << drain_p95
              << " receiveQueueHighWater=" << number_field("receiveQueueHighWater")
              << " bufferedAmountHighWater=" << number_field("bufferedAmountHighWater")
              << "\n";
    require(completed && state.find("\"complete\":true") != std::string::npos
            && state.find("\"error\":null") != std::string::npos
            && state.find("\"cycles\":20") != std::string::npos
            && state.find("\"closeCount\":1") != std::string::npos
            && state.find("\"initApplicationBytes\":1071438") != std::string::npos,
        "remote protocol timing lifecycle did not complete exactly");
    require(ready_p95 <= 500.0 && initialized_p95 <= 500.0
            && outbound_p95 <= 500.0,
        "remote protocol Ready or Initialized p95 exceeded 500 ms");
    require(socket_p95 <= 250.0 && reader_p95 <= 250.0
            && microtask_p95 <= 50.0 && serialization_p95 <= 50.0,
        "remote protocol phase attribution exceeded its direct bound");
    require(protocol_flush_p95 <= 25.0 && protocol_flush_max <= 100.0,
        "remote protocol deferred flush fairness exceeded its p95 or max bound");
    require(send_return_p95 <= 50.0 && drain_p95 <= 50.0,
        "remote protocol WebSocket send or drain exceeded its direct bound");
    require(number_field("workerTasksBeforeFlushMax") <= 4.0
            && number_field("portTasksBeforeFlushMax") <= 4.0
            && number_field("windowTasksBeforeFlushMax") <= 4.0
            && number_field("socketTasksBeforeFlushMax") <= 4.0
            && number_field("fileReaderTasksBeforeFlushMax") <= 4.0,
        "a continuously ready asynchronous source ran an unbounded batch before a due timer");
    require(number_field("workerQueueHighWater") <= 1.0
            && number_field("portQueueHighWater") <= 1.0
            && number_field("socketQueueHighWater") <= 128.0
            && number_field("fileReaderQueueHighWater") <= 128.0,
        "remote protocol task queue exceeded its bound");

    require(webscene_engine_request_low_memory(engine) != 0,
        "protocol timing low-memory request was rejected");
    webscene_runtime_work_metrics work_after{sizeof(webscene_runtime_work_metrics)};
    webscene_engine_memory_metrics memory_after{sizeof(webscene_engine_memory_metrics)};
    for (auto attempt = 0; attempt < 500; ++attempt) {
        require(webscene_engine_get_runtime_work_metrics(engine, &work_after) != 0
                && webscene_engine_get_memory_metrics(engine, &memory_after) != 0,
            "protocol timing final work or memory metrics were unavailable");
        if (memory_after.low_memory_notifications
                > memory_before.low_memory_notifications) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(memory_after.v8_used_heap_bytes
            <= memory_before.v8_used_heap_bytes + 8U * 1024U * 1024U,
        "protocol timing retained more than 8 MiB of V8 heap");
    const auto timers_fired = work_after.timers_fired - work_before.timers_fired;
    const auto microtask_checkpoints =
        work_after.microtask_checkpoints - work_before.microtask_checkpoints;
    const auto signalled_wakes =
        work_after.worker_signalled_wakes - work_before.worker_signalled_wakes;
    const auto scene_builds = work_after.scene_builds - work_before.scene_builds;
    require(timers_fired <= 512U && microtask_checkpoints <= 2048U
            && signalled_wakes <= 256U && scene_builds <= 32U,
        "remote protocol timing exceeded its task, wake, or scene-publication budget");
    std::cout << "Remote protocol work gate: timers=" << timers_fired
              << " microtasks=" << microtask_checkpoints
              << " signalledWakes=" << signalled_wakes
              << " sceneBuilds=" << scene_builds
              << " timeoutWakes="
              << work_after.worker_timeout_wakes - work_before.worker_timeout_wakes
              << " heapBefore=" << memory_before.v8_used_heap_bytes
              << " heapAfter=" << memory_after.v8_used_heap_bytes << "\n";

    {
        std::lock_guard lock(sender_mutex);
        if (active_background) active_background->store(true);
    }
    for (auto& sender : senders) {
        if (sender.joinable()) sender.join();
    }
    webscene_engine_destroy(engine);
    server.stop();
    require(ix::uninitNetSystem(), "protocol timing network cleanup failed");
}

void test_native_file_reader_task_source_fairness()
{
    auto* engine = webscene_engine_create(0);
    require(engine != nullptr, "FileReader task-source engine creation failed");
    require(webscene_engine_set_runtime_work_metrics_enabled(engine, 1U) != 0,
        "FileReader task-source work metrics could not be enabled");
    execute(engine, R"JS(
      globalThis.__webSceneFileReaderTaskSource = {
        complete: false, error: null, timerCount: 0, timersBeforeLoad: -1,
        elapsedMs: -1, events: [], bytes: []
      };
      (() => {
        const state = globalThis.__webSceneFileReaderTaskSource;
        for (let index = 0; index < 4096; ++index) {
          setTimeout(() => { ++state.timerCount; }, 0);
        }
        const reader = new FileReader();
        for (const name of ['loadstart', 'progress', 'load', 'loadend']) {
          reader.addEventListener(name, () => state.events.push(name));
        }
        const started = performance.now();
        reader.onload = () => {
          state.timersBeforeLoad = state.timerCount;
          state.elapsedMs = performance.now() - started;
          state.bytes = Array.from(new Uint8Array(reader.result));
        };
        reader.onerror = () => {
          state.error = reader.error?.message || 'FileReader failed';
        };
        reader.onloadend = () => { state.complete = true; };
        reader.readAsArrayBuffer(new Blob([Uint8Array.of(1, 3, 5, 7)]));
      })();
    )JS", "native-file-reader-task-source.js");
    auto state = std::string{};
    for (auto attempt = 0; attempt < 1000; ++attempt) {
        state = evaluate(
            engine,
            "globalThis.__webSceneFileReaderTaskSource",
            "native-file-reader-task-source-state.js");
        if (state.find("\"complete\":true") != std::string::npos) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const auto number_field = [&](std::string_view name) {
        const auto key = std::string("\"") + std::string(name) + "\":";
        const auto position = state.find(key);
        require(position != std::string::npos,
            "FileReader task-source evidence field was not retained: "
                + std::string(name));
        return std::stod(state.substr(position + key.size()));
    };
    std::cout << "FileReader task-source state: " << state << "\n";
    require(state.find("\"complete\":true") != std::string::npos
            && state.find("\"error\":null") != std::string::npos,
        "FileReader task-source lifecycle did not complete");
    require(state.find(
            "\"events\":[\"loadstart\",\"progress\",\"load\",\"loadend\"]")
            != std::string::npos
            && state.find("\"bytes\":[1,3,5,7]") != std::string::npos,
        "FileReader task-source changed bytes or event ordering");
    require(number_field("timersBeforeLoad") <= 4.0,
        "FileReader waited behind the application timer backlog");
    require(number_field("elapsedMs") <= 100.0,
        "FileReader task-source completion exceeded 100 ms");
    std::cout << "FileReader task-source gate: timersBeforeLoad="
              << number_field("timersBeforeLoad")
              << " elapsedMs=" << number_field("elapsedMs") << "\n";
    webscene_engine_destroy(engine);
}

} // namespace
