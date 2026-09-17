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
    require(
        evaluate(engine, "document.cookie", "native-websocket-cookie-visibility.js") == "\"\"",
        "HTTP-only navigation cookie leaked to document.cookie");
    const auto source = std::string(R"JS(
        globalThis.__webSceneWebSocketTest = {
          events: [], text: null, blobBinary: null, arrayBufferBinary: null,
          closed: false, closeCode: 0,
          closeReason: '', error: null
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
        });
    )JS";
    execute(engine, source, "native-websocket-browser-api.js");

    std::string state;
    for (auto attempt = 0; attempt < 500; ++attempt) {
        state = evaluate(
            engine,
            "globalThis.__webSceneWebSocketTest",
            "native-websocket-browser-api-state.js");
        if (state.find("\"closed\":true") != std::string::npos) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    require(
        state.find("\"events\":[\"open\",\"message\",\"message\",\"message\",\"close\"]")
                != std::string::npos
            && state.find("\"text\":\"native-text\"") != std::string::npos
            && state.find("\"blobBinary\":[0,1,254,255]") != std::string::npos
            && state.find("\"arrayBufferBinary\":[9,8,7]") != std::string::npos
            && state.find("\"closeCode\":1000") != std::string::npos
            && state.find("\"closeReason\":\"complete\"") != std::string::npos
            && state.find("\"error\":null") != std::string::npos,
        "browser WebSocket API did not complete text/binary/close echo lifecycle");
    {
        std::lock_guard lock(server_state_mutex);
        require(
            observed_origin == "http://127.0.0.1:" + std::to_string(port),
            "browser WebSocket API did not forward the document origin");
        require(
            observed_cookie.find("ws_auth=loopback") != std::string::npos,
            "browser WebSocket API did not forward eligible cookie state");
    }

    webscene_engine_destroy(engine);
    server.stop();
    require(ix::uninitNetSystem(), "IXWebSocket network cleanup failed");
}

} // namespace
