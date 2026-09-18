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

struct request_header_v5_server final {
    std::mutex mutex;
    std::vector<std::pair<std::string, std::string>> headers;
};

size_t load_request_header_resource_v5(
    void* user_data,
    uint32_t,
    const char* url,
    size_t url_length,
    const char*,
    size_t,
    int64_t,
    const webscene_resource_request_context_v5* context,
    webscene_resource_response_v4* response,
    char* destination,
    size_t destination_capacity)
{
    auto& server = *static_cast<request_header_v5_server*>(user_data);
    const std::string_view address(url, url_length);
    static constexpr std::string_view document = R"HTML(
      <!doctype html><script>
      globalThis.__requestHeadersDone = false;
      fetch('/api', {headers:{
        'Accept':'application/json',
        'X-CSRF-Token':'csrf-probe'
      }}).then(() => globalThis.__requestHeadersDone = true);
      </script>)HTML";
    static constexpr std::string_view payload{"{}"};
    const auto is_api = address.ends_with("/api");
    if (is_api && context != nullptr) {
        std::lock_guard lock(server.mutex);
        server.headers.clear();
        for (size_t index = 0U; index < context->header_count; ++index) {
            const auto& header = context->headers[index];
            server.headers.emplace_back(
                std::string(header.name, header.name_length),
                std::string(header.value, header.value_length));
        }
    }
    if (response != nullptr) {
        response->status = 200U;
        response->status_text = "OK";
        response->status_text_length = 2U;
        response->final_url = url;
        response->final_url_length = url_length;
    }
    const auto body = is_api ? payload : document;
    constexpr size_t header_size = 2U + sizeof(uint32_t) + sizeof(int64_t) * 2U;
    const auto required = header_size + body.size();
    if (destination == nullptr || destination_capacity < required) return required;
    std::memset(destination, 0, header_size);
    destination[0] = 1;
    std::copy(body.begin(), body.end(), destination + header_size);
    return required;
}

void test_request_headers_reach_resource_callback_v5()
{
    request_header_v5_server server;
    webscene_engine_options options{};
    options.struct_size = sizeof(options);
    options.resource_load_callback_v5 = load_request_header_resource_v5;
    options.resource_load_v5_user_data = &server;
    auto* engine = webscene_engine_create_with_options(&options);
    require(engine != nullptr, "request-header v5 engine creation failed");
    constexpr std::string_view address{"https://auth.test/index.html"};
    require(webscene_engine_load_url(engine, address.data(), address.size()) != 0U,
        "request-header v5 navigation failed");
    for (auto attempt = 0; attempt < 500; ++attempt) {
        if (evaluate(engine, "globalThis.__requestHeadersDone === true",
                "request-header-v5-state.js") == "true") break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(
        evaluate(engine, "globalThis.__requestHeadersDone === true",
            "request-header-v5-result.js") == "true",
        "request-header v5 fetch did not complete");
    {
        std::lock_guard lock(server.mutex);
        const auto has_header = [&](std::string_view name, std::string_view value) {
            return std::ranges::any_of(server.headers, [&](const auto& header) {
                return header.first == name && header.second == value;
            });
        };
        require(has_header("accept", "application/json")
                && has_header("x-csrf-token", "csrf-probe"),
            "author request headers did not reach resource callback v5");
    }
    webscene_engine_destroy(engine);
}

struct durable_profile_server final {
    std::atomic<uint32_t> phase{1U};
    std::mutex mutex;
    std::string navigation_cookie;
};

size_t load_durable_profile_resource_v4(
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
    auto& server = *static_cast<durable_profile_server*>(user_data);
    const auto first = server.phase.load(std::memory_order_acquire) == 1U;
    static constexpr std::string_view first_document = R"HTML(
      <!doctype html><script>
        localStorage.setItem('durable-local', 'retained');
        sessionStorage.setItem('session-only', 'discarded');
        document.cookie = 'visible=retained; Path=/; Secure; Max-Age=3600; SameSite=Lax';
        document.cookie = 'temporary=discarded; Path=/; Secure';
        document.cookie = 'doomed=discarded; Path=/; Secure; Max-Age=3600';
        document.cookie = 'doomed=; Path=/; Secure; Max-Age=0';
        let quota = false;
        try { localStorage.setItem('oversized', 'x'.repeat(5 * 1024 * 1024)); }
        catch (error) { quota = error.name === 'QuotaExceededError'; }
        globalThis.__durableProfile = {ready:true, quota};
      </script>)HTML";
    static constexpr std::string_view second_document = R"HTML(
      <!doctype html><script>
        globalThis.__durableProfile = {
          ready: true,
          local: localStorage.getItem('durable-local'),
          session: sessionStorage.getItem('session-only'),
          visible: document.cookie
        };
      </script>)HTML";
    static constexpr char persistent_cookie[] =
        "durable=secret; Path=/; Secure; HttpOnly; SameSite=Lax; Max-Age=3600";
    static constexpr char session_cookie[] =
        "session=discarded; Path=/; Secure; HttpOnly; SameSite=Lax";
    static const webscene_resource_header_v4 headers[]{
        {sizeof(webscene_resource_header_v4), "Set-Cookie", 10U,
            persistent_cookie, sizeof(persistent_cookie) - 1U},
        {sizeof(webscene_resource_header_v4), "Set-Cookie", 10U,
            session_cookie, sizeof(session_cookie) - 1U}};
    if (context != nullptr) {
        std::lock_guard lock(server.mutex);
        server.navigation_cookie.assign(
            context->cookie == nullptr ? "" : context->cookie,
            context->cookie_length);
    }
    if (response != nullptr) {
        response->status = 200U;
        response->status_text = "OK";
        response->status_text_length = 2U;
        response->final_url = url;
        response->final_url_length = url_length;
        response->headers = first ? headers : nullptr;
        response->header_count = first ? std::size(headers) : 0U;
    }
    const auto body = first ? first_document : second_document;
    constexpr size_t header_size = 2U + sizeof(uint32_t) + sizeof(int64_t) * 2U;
    const auto required = header_size + body.size();
    if (destination == nullptr || destination_capacity < required) return required;
    std::memset(destination, 0, header_size);
    destination[0] = 1;
    std::copy(body.begin(), body.end(), destination + header_size);
    return required;
}

void test_durable_profile_restart_contract()
{
    const auto storage_directory = std::filesystem::temp_directory_path()
        / ("webscene-durable-profile-test-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(storage_directory);
    const auto root = storage_directory.string();
    const std::string partition = "native-restart/default";
    durable_profile_server server;
    webscene_engine_options options{};
    options.struct_size = sizeof(options);
    options.resource_load_callback_v4 = load_durable_profile_resource_v4;
    options.resource_load_v4_user_data = &server;
    options.storage_directory = root.data();
    options.storage_directory_length = root.size();
    options.storage_partition_key = partition.data();
    options.storage_partition_key_length = partition.size();
    options.storage_quota_bytes = 4U * 1024U * 1024U;
    constexpr std::string_view address{"https://profile.test/index.html"};
    auto* engine = webscene_engine_create_with_options(&options);
    require(engine != nullptr, "durable profile writer engine creation failed");
    require(webscene_engine_load_url(engine, address.data(), address.size()) != 0U,
        "durable profile writer navigation failed");
    require(evaluate(engine, "globalThis.__durableProfile", "profile-writer.js")
            == R"({"ready":true,"quota":true})",
        "durable profile writer did not initialize state");
    webscene_engine_destroy(engine);

    server.phase.store(2U, std::memory_order_release);
    {
        std::lock_guard lock(server.mutex);
        server.navigation_cookie.clear();
    }
    engine = webscene_engine_create_with_options(&options);
    require(engine != nullptr, "durable profile reader engine creation failed");
    require(webscene_engine_load_url(engine, address.data(), address.size()) != 0U,
        "durable profile reader navigation failed");
    const auto restored = evaluate(
        engine, "globalThis.__durableProfile", "profile-reader.js");
    require(restored.find(R"("local":"retained")") != std::string::npos
            && restored.find(R"("session":null)") != std::string::npos
            && restored.find("visible=retained") != std::string::npos
            && restored.find("temporary=discarded") == std::string::npos
            && restored.find("doomed=discarded") == std::string::npos,
        "durable/local versus session restart contract failed: " + restored);
    {
        std::lock_guard lock(server.mutex);
        require(server.navigation_cookie.find("durable=secret") != std::string::npos
                && server.navigation_cookie.find("visible=retained") != std::string::npos
                && server.navigation_cookie.find("session=discarded") == std::string::npos
                && server.navigation_cookie.find("temporary=discarded") == std::string::npos,
            "persistent/session cookie restart contract failed");
    }
    webscene_engine_destroy(engine);

    const std::string isolated_partition = "native-restart/isolated";
    options.storage_partition_key = isolated_partition.data();
    options.storage_partition_key_length = isolated_partition.size();
    {
        std::lock_guard lock(server.mutex);
        server.navigation_cookie.clear();
    }
    engine = webscene_engine_create_with_options(&options);
    require(engine != nullptr, "isolated profile engine creation failed");
    require(webscene_engine_load_url(engine, address.data(), address.size()) != 0U,
        "isolated profile navigation failed");
    const auto isolated = evaluate(
        engine, "globalThis.__durableProfile", "profile-isolated.js");
    require(isolated.find(R"("local":null)") != std::string::npos
            && isolated.find(R"("session":null)") != std::string::npos,
        "profile partition observed another partition's localStorage: " + isolated);
    {
        std::lock_guard lock(server.mutex);
        require(server.navigation_cookie.empty(),
            "profile partition observed another partition's cookies");
    }
    webscene_engine_destroy(engine);

    require(webscene_profile_clear_data_v1(
            root.data(), root.size(), partition.data(), partition.size(),
            options.storage_quota_bytes,
            WEBSCENE_PROFILE_CLEAR_ALL_SITE_DATA_V1)
            == WEBSCENE_PROFILE_STATUS_OK_V1,
        "durable profile clear-data contract failed");
    std::error_code cleanup_error;
    std::filesystem::remove_all(storage_directory, cleanup_error);
}

struct single_flight_cookie_server final {
    std::atomic<uint32_t> target_requests{0U};
};

size_t load_single_flight_cookie_resource_v4(
    void* user_data,
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
    auto& server = *static_cast<single_flight_cookie_server*>(user_data);
    const std::string_view address(url, url_length);
    const auto target = address == "https://single-flight-cookie.test/index.html";
    static constexpr std::string_view target_body = R"HTML(
      <!doctype html><script>
        globalThis.__singleFlightCookie = document.cookie;
        globalThis.__webSceneComponentReady = true;
      </script>)HTML";
    static constexpr std::string_view warmup_body =
        "<!doctype html><script>globalThis.__webSceneComponentReady=true;</script>";
    const auto body = target ? target_body : warmup_body;
    static const webscene_resource_header_v4 target_headers[]{
        {sizeof(webscene_resource_header_v4), "Set-Cookie", 10U,
            "sid=isolated; Path=/; Secure; HttpOnly", 38U}};
    if (response != nullptr) {
        response->status = 200U;
        response->status_text = "OK";
        response->status_text_length = 2U;
        response->final_url = url;
        response->final_url_length = url_length;
        response->headers = target ? target_headers : nullptr;
        response->header_count = target ? std::size(target_headers) : 0U;
    }
    constexpr size_t header_size = 2U + sizeof(uint32_t) + sizeof(int64_t) * 2U;
    const auto required = header_size + body.size();
    if (destination == nullptr || destination_capacity < required) return required;
    if (target) {
        server.target_requests.fetch_add(1U, std::memory_order_relaxed);
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
    }
    destination[0] = 1;
    destination[1] = 1;
    const uint32_t tag_length = 0U;
    const int64_t last_modified = 0;
    const auto fresh_until = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count() + 3600;
    std::memcpy(destination + 2U, &tag_length, sizeof(tag_length));
    std::memcpy(destination + 2U + sizeof(tag_length),
        &last_modified, sizeof(last_modified));
    std::memcpy(destination + 2U + sizeof(tag_length) + sizeof(last_modified),
        &fresh_until, sizeof(fresh_until));
    std::copy(body.begin(), body.end(), destination + header_size);
    return required;
}

void test_cross_engine_single_flight_keeps_set_cookie_responses_private()
{
    constexpr size_t engine_count = 4U;
    const auto cache_directory = std::filesystem::temp_directory_path()
        / ("webscene-native-single-flight-cookie-test-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(cache_directory);
    const auto cache_path = cache_directory.string();
    single_flight_cookie_server server;
    webscene_engine_options options{};
    options.struct_size = sizeof(options);
    options.compilation_cache_directory = cache_path.data();
    options.compilation_cache_directory_length = cache_path.size();
    options.resource_load_callback_v4 = load_single_flight_cookie_resource_v4;
    options.resource_load_v4_user_data = &server;
    std::array<webscene_engine*, engine_count> engines{};
    for (auto& engine : engines) {
        engine = webscene_engine_create_with_options(&options);
        require(engine != nullptr, "single-flight cookie engine creation failed");
    }
    std::atomic<size_t> ready{0U};
    std::atomic<bool> start{false};
    std::array<std::thread, engine_count> workers;
    std::array<std::string, engine_count> cookies;
    constexpr std::string_view target{
        "https://single-flight-cookie.test/index.html"};
    for (size_t index = 0U; index < engine_count; ++index) {
        workers[index] = std::thread([&, index] {
            const auto warmup = "https://single-flight-cookie.test/warmup-"
                + std::to_string(index) + ".html";
            require(webscene_engine_load_url(
                    engines[index], warmup.data(), warmup.size()) != 0,
                "single-flight cookie warmup failed");
            ready.fetch_add(1U, std::memory_order_release);
            while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
            require(webscene_engine_load_url(
                    engines[index], target.data(), target.size()) != 0,
                "single-flight cookie navigation failed");
            cookies[index] = evaluate(engines[index],
                "globalThis.__singleFlightCookie",
                "single-flight-cookie-result.js");
        });
    }
    while (ready.load(std::memory_order_acquire) != engine_count) {
        std::this_thread::yield();
    }
    start.store(true, std::memory_order_release);
    for (auto& worker : workers) worker.join();
    const auto target_requests = server.target_requests.load(std::memory_order_relaxed);
    require(target_requests == engine_count,
        "a credentialed Set-Cookie response was shared across engine cookie jars: requests="
            + std::to_string(target_requests));
    for (const auto& cookie : cookies) {
        require(cookie == "\"\"",
            "HttpOnly single-flight cookie became script-visible: " + cookie);
    }
    for (auto* engine : engines) webscene_engine_destroy(engine);
    std::error_code cleanup_error;
    std::filesystem::remove_all(cache_directory, cleanup_error);
}
