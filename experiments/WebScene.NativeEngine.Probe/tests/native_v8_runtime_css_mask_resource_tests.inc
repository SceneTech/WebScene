std::string css_mask_test_png(uint32_t width = 1U, uint32_t height = 1U)
{
    constexpr std::array<unsigned char, 70> source{{
        0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a,0x00,0x00,0x00,0x0d,
        0x49,0x48,0x44,0x52,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x01,
        0x08,0x06,0x00,0x00,0x00,0x1f,0x15,0xc4,0x89,0x00,0x00,0x00,
        0x0d,0x49,0x44,0x41,0x54,0x78,0xda,0x63,0x90,0xdd,0x19,0xf2,
        0x1f,0x00,0x04,0x4b,0x02,0x2a,0x81,0xeb,0x0d,0xbc,0x00,0x00,
        0x00,0x00,0x49,0x45,0x4e,0x44,0xae,0x42,0x60,0x82}};
    auto result = std::string(
        reinterpret_cast<const char*>(source.data()), source.size());
    for (auto offset = 0U; offset < 4U; ++offset) {
        result[16U + offset] = static_cast<char>(width >> ((3U - offset) * 8U));
        result[20U + offset] = static_cast<char>(height >> ((3U - offset) * 8U));
    }
    return result;
}

std::string css_mask_test_webp(uint32_t width = 2U, uint32_t height = 3U)
{
    auto result = std::string(30U, '\0');
    result.replace(0U, 4U, "RIFF");
    result[4U] = 22;
    result.replace(8U, 4U, "WEBP");
    result.replace(12U, 4U, "VP8X");
    result[16U] = 10;
    const auto encoded_width = width - 1U;
    const auto encoded_height = height - 1U;
    result[24U] = static_cast<char>(encoded_width);
    result[25U] = static_cast<char>(encoded_width >> 8U);
    result[26U] = static_cast<char>(encoded_width >> 16U);
    result[27U] = static_cast<char>(encoded_height);
    result[28U] = static_cast<char>(encoded_height >> 8U);
    result[29U] = static_cast<char>(encoded_height >> 16U);
    return result;
}

std::string latest_css_mask_resource(webscene_engine* engine)
{
    webscene_engine_request_scene_checkpoint(engine);
    for (auto attempt = 0; attempt < 250; ++attempt) {
        auto result = std::string{};
        const auto* scene = webscene_engine_acquire_latest_scene(engine);
        if (scene != nullptr) {
            for (uint32_t index = 0U; index < scene->header.command_count; ++index) {
                const auto& command = scene->commands[index];
                if (command.kind != 47U || command.flags >= scene->string_count) continue;
                const auto& descriptor = scene->strings[command.flags];
                if (descriptor.byte_offset <= scene->string_byte_count
                    && descriptor.byte_length
                        <= scene->string_byte_count - descriptor.byte_offset) {
                    result.assign(
                        scene->string_bytes + descriptor.byte_offset,
                        descriptor.byte_length);
                }
            }
            webscene_scene_acknowledge(scene);
            webscene_scene_release(scene);
        }
        if (!result.empty()) return result;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return {};
}

void test_css_raster_mask_provider_envelope_and_fail_closed_contract()
{
    constexpr std::string_view address{"https://mask-resource.test/index.html"};
    constexpr std::string_view embedded_png =
        "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mOQ3RnyHwAESwIqgesNvAAAAABJRU5ErkJggg==";
    const auto host_wrapper = std::string{
        "<svg xmlns=\"http://www.w3.org/2000/svg\" "
        "xmlns:xlink=\"http://www.w3.org/1999/xlink\" viewBox=\"0 0 1 1\" "
        "width=\"1\" height=\"1\"><image width=\"1\" height=\"1\" "
        "xlink:href=\"data:image/png;base64,"}
        + std::string{embedded_png} + "\"/></svg>";
    const auto ordinary_svg = std::string{
        "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 4 4\">"
        "<rect width=\"4\" height=\"4\"/><image width=\"1\" height=\"1\" "
        "href=\"data:image/png;base64,"}
        + std::string{embedded_png} + "\"/></svg>";
    auto oversized = std::string(5U * 1024U * 1024U / 2U + 1U, '\0');
    oversized.replace(0U, css_mask_test_png().size(), css_mask_test_png());
    auto truncated = css_mask_test_png();
    truncated.resize(24U);
    resource_server server{.content = {
        {std::string{address}, "<!doctype html><div id='mask'></div>"},
        {"https://mask-resource.test/pixel.png", css_mask_test_png()},
        {"https://mask-resource.test/pixel.webp", css_mask_test_webp()},
        {"https://mask-resource.test/wrapped.png", host_wrapper},
        {"https://mask-resource.test/embedded.svg", ordinary_svg},
        {"https://mask-resource.test/truncated.png", std::move(truncated)},
        {"https://mask-resource.test/oversized.png", std::move(oversized)},
        {"https://mask-resource.test/huge.png", css_mask_test_png(1'000'001U, 1U)},
        {"https://mask-resource.test/unsupported.gif", "GIF89a-invalid"}
    }};
    webscene_engine_options options{};
    options.struct_size = sizeof(options);
    options.resource_load_callback = load_test_resource;
    options.resource_load_user_data = &server;
    auto* engine = webscene_engine_create_with_options(&options);
    require(engine != nullptr, "CSS raster mask provider engine creation failed");
    require(webscene_engine_load_url(engine, address.data(), address.size()) != 0U,
        "CSS raster mask provider navigation failed");
    require(evaluate_until_equals(engine, "location.href", "mask-resource-ready.js",
            R"("https://mask-resource.test/index.html")", 2'500)
            == R"("https://mask-resource.test/index.html")",
        "CSS raster mask provider document did not become ready");
    execute(engine, R"JS(
      Object.assign(document.getElementById('mask').style, {
        width:'40px', height:'30px', background:'red', maskRepeat:'no-repeat',
        maskPosition:'3px 4px', maskSize:'contain'
      });
    )JS", "mask-resource-setup.js");
    const auto set_mask = [&](std::string_view value) {
        execute(engine,
            "document.getElementById('mask').style.maskImage=" + std::string(value),
            "mask-resource-set.js");
        return latest_css_mask_resource(engine);
    };

    const auto png = set_mask("'url(\\\"pixel.png\\\")'");
    require(png.starts_with("webscene-mask-v2\t1\t")
            && png.find("webscene-raster-v2\t1\t1\t") != std::string::npos
            && png.find("0 0 1 1") != std::string::npos
            && png.find(std::string{embedded_png}) != std::string::npos,
        "synchronous PNG mask did not publish the text-safe raster envelope");
    const auto webp = set_mask("'url(\\\"pixel.webp\\\")'");
    require(webp.find("webscene-raster-v2\t2\t3\t") != std::string::npos
            && webp.find("0 0 2 3") != std::string::npos,
        "synchronous WebP mask did not publish dimensions and identity");
    const auto wrapped = set_mask("'url(\\\"wrapped.png\\\")'");
    require(wrapped.find("webscene-raster-v2\t1\t1\t") != std::string::npos,
        "the exact host raster wrapper was not normalized");
    const auto embedded = set_mask("'url(\\\"embedded.svg\\\")'");
    require(embedded.starts_with("webscene-mask-svg-v1\t")
            && embedded.find("<rect") != std::string::npos
            && embedded.find("data:image/png") != std::string::npos,
        "ordinary SVG geometry with an embedded image was discarded");
    const auto multilayer = set_mask(
        "'url(\\\"pixel.png\\\"),url(\\\"pixel.png\\\")'");
    const auto first_identity = multilayer.find("webscene-raster-v2\t1\t1\t");
    const auto identity_begin = first_identity == std::string::npos
        ? std::string::npos
        : first_identity + std::string_view{"webscene-raster-v2\t1\t1\t"}.size();
    const auto identity_end = identity_begin == std::string::npos
        ? std::string::npos : multilayer.find('\t', identity_begin);
    const auto identity = identity_end == std::string::npos
        ? std::string{} : multilayer.substr(identity_begin, identity_end - identity_begin);
    require(multilayer.starts_with("webscene-mask-v2\t2\t")
            && first_identity != std::string::npos
            && identity.size() == 64U
            && multilayer.find(identity, identity_end + 1U) != std::string::npos,
        "multi-layer raster masks did not retain reusable content identity");
    for (const auto value : {
             "'url(\\\"truncated.png\\\")'",
             "'url(\\\"oversized.png\\\")'",
             "'url(\\\"huge.png\\\")'",
             "'url(\\\"unsupported.gif\\\")'"}) {
        require(set_mask(value) == "webscene-mask-invalid-v1",
            "invalid raster mask did not fail closed");
    }
    webscene_engine_destroy(engine);
}

void test_controlled_css_raster_mask_publication_and_stale_rejection()
{
    constexpr std::string_view address{"https://controlled-mask.test/index.html"};
    const auto worker = R"JS(
      const png = width => {
        const bytes = new Uint8Array([137,80,78,71,13,10,26,10,0,0,0,13,
          73,72,68,82,0,0,0,width,0,0,0,1,8,6,0,0,0,31,21,196,137,
          0,0,0,13,73,68,65,84,120,218,99,144,221,25,242,31,0,4,75,
          2,42,129,235,13,188,0,0,0,0,73,69,78,68,174,66,96,130]);
        bytes[16] = width >>> 24; bytes[17] = width >>> 16;
        bytes[18] = width >>> 8; bytes[19] = width;
        return bytes;
      };
      const counts = {};
      self.addEventListener('install', event => event.waitUntil(skipWaiting()));
      self.addEventListener('activate', event => event.waitUntil(clients.claim()));
      self.addEventListener('fetch', event => {
        const path = new URL(event.request.url).pathname;
        counts[path] = (counts[path] || 0) + 1;
        if (!path.startsWith('/mask-')) return;
        let response;
        if (path === '/mask-wrong-mime.png') {
          response = new Response(png(4), {headers:{'content-type':'text/plain'}});
        } else if (path === '/mask-format.gif') {
          response = new Response(new Uint8Array([71,73,70,56,57,97]),
            {headers:{'content-type':'image/gif'}});
        } else if (path === '/mask-large.png') {
          const large = new Uint8Array(2621441); large.set(png(5));
          response = new Response(large, {headers:{'content-type':'image/png'}});
        } else if (path === '/mask-huge.png') {
          response = new Response(png(1000001), {headers:{'content-type':'image/png'}});
        } else {
          response = new Response(png(path.includes('stale') ? 2 : 1),
            {headers:{'content-type':'image/png'}});
        }
        event.respondWith(path.includes('slow') || path.includes('stale')
          ? new Promise(resolve => setTimeout(() => resolve(response), 30))
          : response);
      });
      self.addEventListener('message', event => {
        if (event.data === 'counts') event.source.postMessage(counts);
      });
    )JS";
    resource_server server{.content = {
        {std::string{address}, "<!doctype html><div id='mask' "
            "style='width:40px;height:30px;background:red'></div>"},
        {"https://controlled-mask.test/next.html", "<!doctype html><p>next</p>"},
        {"https://controlled-mask.test/service-worker.js", worker}
    }};
    webscene_engine_options options{};
    options.struct_size = sizeof(options);
    options.resource_load_callback = load_test_resource;
    options.resource_load_user_data = &server;
    auto* engine = webscene_engine_create_with_options(&options);
    require(engine != nullptr, "controlled raster mask engine creation failed");
    require(webscene_engine_load_url(engine, address.data(), address.size()) != 0U,
        "controlled raster mask navigation failed");
    execute(engine, R"JS(
      globalThis.__controlledMaskReady = false;
      (async () => {
        const changed = new Promise(resolve => navigator.serviceWorker.addEventListener(
          'controllerchange', resolve, {once:true}));
        await navigator.serviceWorker.register('service-worker.js');
        if (!navigator.serviceWorker.controller) await changed;
        __controlledMaskReady = true;
      })();
    )JS", "controlled-mask-register.js");
    require(evaluate_until_equals(engine, "__controlledMaskReady",
            "controlled-mask-ready.js", "true", 5'000) == "true",
        "controlled raster mask ServiceWorker did not claim the document");
    execute(engine, R"JS(
      const target = document.getElementById('mask');
      target.style.maskImage = 'url("mask-stale.png")';
      target.style.maskImage = 'url("mask-current.png")';
    )JS", "controlled-mask-replace.js");
    auto current = std::string{};
    for (auto attempt = 0; attempt < 250; ++attempt) {
        current = latest_css_mask_resource(engine);
        if (current.find("webscene-raster-v2\t1\t1\t") != std::string::npos) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(current.find("webscene-raster-v2\t1\t1\t") != std::string::npos
            && current.find("webscene-raster-v2\t2\t1\t") == std::string::npos,
        "a stale controlled mask completion replaced the current URL");
    execute(engine, R"JS(
      const detached = document.createElement('div');
      detached.style.cssText = 'width:10px;height:10px;mask-image:url("mask-slow.png")';
      document.body.append(detached); detached.remove();
    )JS", "controlled-mask-detach.js");
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    require(latest_css_mask_resource(engine).find("webscene-raster-v2\t1\t1\t")
            != std::string::npos,
        "a detached mask request disturbed the retained current mask");
    for (const auto path : {
             "mask-wrong-mime.png", "mask-format.gif", "mask-large.png",
             "mask-huge.png"}) {
        execute(engine,
            "document.getElementById('mask').style.maskImage='url(\\\""
                + std::string(path) + "\\\")'",
            "controlled-mask-failure.js");
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        require(latest_css_mask_resource(engine) == "webscene-mask-invalid-v1",
            "controlled MIME/format/size rejection did not fail closed");
    }
    execute(engine,
        "document.getElementById('mask').style.maskImage='url(\\\"mask-stale.png\\\")'",
        "controlled-mask-navigation-stale.js");
    constexpr std::string_view next{"https://controlled-mask.test/next.html"};
    require(webscene_engine_load_url(engine, next.data(), next.size()) != 0U,
        "controlled raster mask replacement navigation failed");
    require(evaluate_until_equals(engine, "location.href", "controlled-mask-next.js",
            R"("https://controlled-mask.test/next.html")", 2'500)
            == R"("https://controlled-mask.test/next.html")",
        "controlled raster mask replacement document did not load");
    require(latest_css_mask_resource(engine).empty(),
        "a pre-navigation controlled mask completion published into the next document");
    webscene_engine_destroy(engine);
}
