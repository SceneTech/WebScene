struct response_cookie_server final {
    std::mutex mutex;
    std::string fetch_cookie;
    uint32_t fetch_credentials{99U};
};

size_t load_response_cookie_resource_v4(
    void* user_data,
    uint32_t,
    const char* url,
    size_t url_length,
    const char*,
    size_t,
    int64_t,
    const webscene_resource_request_context_v4* context,
    webscene_resource_response_v4* response,
    char* destination,
    size_t destination_capacity)
{
    auto& server = *static_cast<response_cookie_server*>(user_data);
    const std::string address(url, url_length);
    static constexpr std::string_view document = R"HTML(
      <!doctype html><script>
      globalThis.__responseCookie = { done: false };
      document.cookie = 'client=visible; Path=/; Secure; SameSite=Lax';
      fetch('/app/api', { credentials: 'include' }).then(async response => {
        globalThis.__responseCookie = {
          done: false,
          status: response.status,
          statusText: response.statusText,
          url: response.url,
          redirected: response.redirected,
          contentType: response.headers.get('content-type'),
          trace: response.headers.get('x-trace'),
          setCookieHidden: response.headers.get('set-cookie') === null,
          body: await response.text(),
          visible: document.cookie
        };
        try {
          await fetch('https://other.test/api', { mode: 'cors' });
          globalThis.__responseCookie.corsBlocked = false;
        } catch { globalThis.__responseCookie.corsBlocked = true; }
        const opaque = await fetch('https://other.test/api', { mode: 'no-cors' });
        globalThis.__responseCookie.opaque = opaque.type === 'opaque'
          && opaque.status === 0 && opaque.url === '' && await opaque.text() === '';
        try {
          await fetch('/redirect', { redirect: 'error' });
          globalThis.__responseCookie.redirectRejected = false;
        } catch { globalThis.__responseCookie.redirectRejected = true; }
        try { await fetch('/invalid-status'); globalThis.__responseCookie.invalidStatus = false; }
        catch { globalThis.__responseCookie.invalidStatus = true; }
        try { await fetch('/too-many-headers'); globalThis.__responseCookie.headerLimit = false; }
        catch { globalThis.__responseCookie.headerLimit = true; }
        globalThis.__responseCookie.done = true;
      }).catch(error => globalThis.__responseCookie = { done: true, error: String(error) });
      </script>)HTML";
    static constexpr std::string_view payload = "accepted";
    const auto is_document = address.ends_with("/index.html");
    const auto body = is_document ? document : payload;

    static const webscene_resource_header_v4 document_headers[]{
        {sizeof(webscene_resource_header_v4), "Set-Cookie", 10U,
            "sid=secret; Path=/; Secure; HttpOnly; SameSite=Lax", 50U},
        {sizeof(webscene_resource_header_v4), "Set-Cookie", 10U,
            "theme=dark; Path=/app; Secure; SameSite=Strict", 46U},
        {sizeof(webscene_resource_header_v4), "Set-Cookie", 10U,
            "rejected=1; Domain=evil.test; Path=/", 36U}};
    static const webscene_resource_header_v4 api_headers[]{
        {sizeof(webscene_resource_header_v4), "Content-Type", 12U,
            "text/plain;charset=UTF-8", 24U},
        {sizeof(webscene_resource_header_v4), "X-Trace", 7U, "response-v4", 11U},
        {sizeof(webscene_resource_header_v4), "Set-Cookie", 10U,
            "api=hidden; Path=/; Secure; HttpOnly", 36U}};
    const auto is_api = address.ends_with("/app/api");
    const auto is_cross = address.starts_with("https://other.test/");
    const auto is_redirect = address.ends_with("/redirect");
    if (is_api && context != nullptr) {
        std::lock_guard lock(server.mutex);
        server.fetch_credentials = context->credentials;
        server.fetch_cookie.assign(
            context->cookie == nullptr ? "" : context->cookie,
            context->cookie_length);
    }
    if (response != nullptr) {
        response->status = is_api ? 201U : is_redirect ? 302U : 200U;
        response->status_text = is_api ? "Created" : "OK";
        response->status_text_length = is_api ? 7U : 2U;
        response->final_url = is_api ? "https://auth.test/app/final"
            : is_cross ? "https://other.test/api"
            : is_redirect ? "https://auth.test/redirected"
            : "https://auth.test/index.html";
        response->final_url_length = std::strlen(response->final_url);
        response->headers = is_api ? api_headers : is_document ? document_headers : nullptr;
        response->header_count = is_api ? std::size(api_headers)
            : is_document ? std::size(document_headers) : 0U;
        if (address.ends_with("/invalid-status")) response->status = 700U;
        if (address.ends_with("/too-many-headers")) response->header_count = 65U;
    }
    constexpr size_t header_size = 2U + sizeof(uint32_t) + sizeof(int64_t) * 2U;
    const auto required = header_size + body.size();
    if (destination == nullptr || destination_capacity < required) return required;
    std::memset(destination, 0, header_size);
    destination[0] = 1;
    std::copy(body.begin(), body.end(), destination + header_size);
    return required;
}

void test_response_header_cookie_contracts()
{
    response_cookie_server server;
    webscene_engine_options options{};
    options.struct_size = sizeof(options);
    options.resource_load_callback_v4 = load_response_cookie_resource_v4;
    options.resource_load_v4_user_data = &server;
    auto* engine = webscene_engine_create_with_options(&options);
    require(engine != nullptr, "response/cookie engine creation failed");
    constexpr std::string_view address{"https://auth.test/index.html"};
    require(webscene_engine_load_url(engine, address.data(), address.size()) != 0U,
        "response/cookie navigation failed");
    std::string result;
    for (auto attempt = 0; attempt < 500; ++attempt) {
        result = evaluate(engine, "globalThis.__responseCookie", "response-cookie-state.js");
        if (result.find("\"done\":true") != std::string::npos) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(result.find("\"status\":201") != std::string::npos
            && result.find("\"statusText\":\"Created\"") != std::string::npos
            && result.find("\"url\":\"https://auth.test/app/final\"") != std::string::npos
            && result.find("\"redirected\":true") != std::string::npos
            && result.find("\"contentType\":\"text/plain;charset=UTF-8\"") != std::string::npos
            && result.find("\"trace\":\"response-v4\"") != std::string::npos
            && result.find("\"setCookieHidden\":true") != std::string::npos
            && result.find("\"corsBlocked\":true") != std::string::npos
            && result.find("\"opaque\":true") != std::string::npos
            && result.find("\"redirectRejected\":true") != std::string::npos
            && result.find("\"invalidStatus\":true") != std::string::npos
            && result.find("\"headerLimit\":true") != std::string::npos
            && result.find("\"body\":\"accepted\"") != std::string::npos,
        "v4 response metadata did not reach the Fetch Response: " + result);
    require(result.find("client=visible") != std::string::npos
            && result.find("sid=secret") == std::string::npos
            && result.find("api=hidden") == std::string::npos
            && result.find("rejected=1") == std::string::npos,
        "document.cookie visibility/security contract failed: " + result);
    {
        std::lock_guard lock(server.mutex);
        require(server.fetch_credentials == WEBSCENE_FETCH_CREDENTIALS_INCLUDE,
            "fetch credentials mode did not reach resource callback v4");
        require(server.fetch_cookie.find("sid=secret") != std::string::npos
                && server.fetch_cookie.find("theme=dark") != std::string::npos
                && server.fetch_cookie.find("client=visible") != std::string::npos
                && server.fetch_cookie.find("rejected=1") == std::string::npos,
            "eligible cookies were not bounded and propagated to fetch: "
                + server.fetch_cookie);
    }
    const auto mutation = evaluate(engine, R"JS((() => {
      document.cookie = 'gone=one; Path=/; Secure';
      document.cookie = 'gone=; Path=/; Secure; Max-Age=0';
      const started = performance.now();
      for (let index = 0; index < 300; ++index) {
        document.cookie = `bounded_${index}=value; Path=/; Secure`;
      }
      document.cookie = 'replace=one; Path=/; Secure';
      document.cookie = 'replace=two; Path=/; Secure';
      for (let index = 0; index < 5000; ++index) void document.cookie;
      const visible = document.cookie;
      return {
        count: visible ? visible.split('; ').length : 0,
        replaced: visible.includes('replace=two') && !visible.includes('replace=one'),
        deleted: !visible.includes('gone='),
        elapsed: performance.now() - started
      };
    })())JS", "response-cookie-bounds.js");
    require(mutation.find("\"replaced\":true") != std::string::npos
            && mutation.find("\"deleted\":true") != std::string::npos,
        "cookie replacement/deletion semantics failed: " + mutation);
    const auto count_key = mutation.find("\"count\":");
    require(count_key != std::string::npos
            && std::stoi(mutation.substr(count_key + 8U)) <= 256,
        "cookie jar exceeded its bounded entry count: " + mutation);
    const auto elapsed_key = mutation.find("\"elapsed\":");
    require(elapsed_key != std::string::npos
            && std::stod(mutation.substr(elapsed_key + 10U)) < 3000.0,
        "bounded cookie mutation/read gate exceeded three seconds: " + mutation);
    webscene_engine_destroy(engine);
}
