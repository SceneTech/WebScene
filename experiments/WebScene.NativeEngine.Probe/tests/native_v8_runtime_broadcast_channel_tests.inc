void test_origin_partitioned_broadcast_channel_contract()
{
    auto* first = webscene_engine_create(64);
    auto* second = webscene_engine_create(64);
    require(first != nullptr && second != nullptr,
        "BroadcastChannel contract engines were not created");

    execute(first, R"JS(
        globalThis.__broadcastSenderEvents = 0;
        globalThis.__broadcastLocal = [];
        globalThis.__broadcastSender = new BroadcastChannel('vscode.profile');
        globalThis.__broadcastSender.onmessage = () => ++__broadcastSenderEvents;
        globalThis.__broadcastLocalPeer = new BroadcastChannel('vscode.profile');
        __broadcastLocalPeer.addEventListener('message', event => {
          __broadcastLocal.push({ data: event.data, origin: event.origin,
            source: event.source, ports: event.ports.length });
        });
    )JS", "native-broadcast-channel-first-setup.js");
    execute(second, R"JS(
        globalThis.__broadcastRemote = [];
        globalThis.__broadcastRemotePeer = new BroadcastChannel('vscode.profile');
        __broadcastRemotePeer.onmessage = event => {
          __broadcastRemote.push({ data: event.data, origin: event.origin,
            source: event.source, ports: event.ports.length });
        };
    )JS", "native-broadcast-channel-second-setup.js");

    require(evaluate(first, R"JS(
        JSON.stringify([
          typeof BroadcastChannel,
          __broadcastSender instanceof BroadcastChannel,
          __broadcastSender instanceof EventTarget,
          __broadcastSender.name,
          Object.prototype.toString.call(__broadcastSender),
          BroadcastChannel.length,
          BroadcastChannel.prototype.postMessage.length,
          BroadcastChannel.prototype.close.length
        ])
    )JS", "native-broadcast-channel-shape.js")
            == R"(["function",true,true,"vscode.profile","[object BroadcastChannel]",1,1,0])",
        "BroadcastChannel browser shape changed");

    execute(first, R"JS(
        const bytes = new Uint8Array([3, 1, 4]);
        __broadcastSender.postMessage({ sequence: 1, nested: { ok: true }, bytes });
        bytes[0] = 9;
        __broadcastSender.postMessage({ sequence: 2 });
        __broadcastSender.postMessage({ sequence: 3 });
    )JS", "native-broadcast-channel-post.js");

    require(evaluate_until_equals(first,
                "__broadcastLocal.length", "native-broadcast-local-drain.js", "3") == "3",
        "BroadcastChannel did not deliver to another local channel");
    require(evaluate_until_equals(second,
                "__broadcastRemote.length", "native-broadcast-remote-drain.js", "3") == "3",
        "BroadcastChannel did not deliver across runtime instances");
    require(evaluate(first, R"JS(
        JSON.stringify([
          __broadcastSenderEvents,
          __broadcastLocal.map(entry => entry.data.sequence),
          Array.from(__broadcastLocal[0].data.bytes),
          __broadcastLocal[0].data.nested.ok,
          __broadcastLocal.every(entry => entry.source === null && entry.ports === 0),
          __broadcastLocal.every(entry => entry.origin === 'http://127.0.0.1')
        ])
    )JS", "native-broadcast-local-result.js")
            == R"([0,[1,2,3],[3,1,4],true,true,true])",
        "BroadcastChannel sender exclusion, FIFO, clone, or MessageEvent metadata changed");
    require(evaluate(second, R"JS(
        JSON.stringify([
          __broadcastRemote.map(entry => entry.data.sequence),
          Array.from(__broadcastRemote[0].data.bytes),
          __broadcastRemote.every(entry => entry.source === null && entry.ports === 0),
          __broadcastRemote.every(entry => entry.origin === 'http://127.0.0.1')
        ])
    )JS", "native-broadcast-remote-result.js")
            == R"([[1,2,3],[3,1,4],true,true])",
        "BroadcastChannel cross-runtime delivery changed");

    require(evaluate(first, R"JS(
        let cloneError = null;
        let closedError = null;
        try { __broadcastSender.postMessage(() => {}); }
        catch (error) { cloneError = error.name; }
        __broadcastSender.close();
        __broadcastSender.close();
        try { __broadcastSender.postMessage('late'); }
        catch (error) { closedError = error.name; }
        JSON.stringify([cloneError, closedError]);
    )JS", "native-broadcast-channel-errors.js")
            == R"(["DataCloneError","InvalidStateError"])",
        "BroadcastChannel clone or closed-state errors changed");

    webscene_engine_destroy(first);
    webscene_engine_destroy(second);

    auto* replacement = webscene_engine_create(64);
    require(replacement != nullptr,
        "BroadcastChannel replacement engine was not created");
    require(evaluate(replacement, R"JS(
        const channel = new BroadcastChannel('vscode.profile');
        const result = channel.name === 'vscode.profile';
        channel.close();
        result;
    )JS", "native-broadcast-channel-replacement.js") == "true",
        "BroadcastChannel teardown retained stale process registrations");
    webscene_engine_destroy(replacement);
}

void test_blob_iframe_realm_contract()
{
    resource_server server{.content = {
        {"https://owner.test/index.html", "<!doctype html><body></body>"}
    }};
    webscene_engine_options options{};
    options.struct_size = sizeof(options);
    options.resource_load_callback = load_test_resource;
    options.resource_load_user_data = &server;
    auto* engine = webscene_engine_create_with_options(&options);
    require(engine != nullptr, "Blob iframe engine creation failed");
    constexpr std::string_view address{"https://owner.test/index.html"};
    require(webscene_engine_load_url(engine, address.data(), address.size()) != 0,
        "Blob iframe parent navigation failed");
    require(evaluate_until_equals(engine, "location.href", "blob-frame-owner.js",
        R"("https://owner.test/index.html")", 2'500) == R"("https://owner.test/index.html")",
        "Blob iframe parent did not load");

    execute_and_wait(engine, R"JS(
        const checks = { channel: false, sandbox: false };
        const channel = new BroadcastChannel('blob-frame-contract');
        channel.onmessage = event => {
          checks.channel = event.origin === location.origin && event.data === 'range-ready';
        };
        addEventListener('message', event => {
          checks.sandbox = event.origin === 'null' && event.data === 'SecurityError';
        });

        const frame = document.createElement('iframe');
        frame.dataset.probe = 'owner';
        frame.src = URL.createObjectURL(new Blob([`
          <!doctype html><p id="target">text</p><script>
            const range = document.createRange();
            range.selectNodeContents(document.getElementById('target'));
            if (frameElement.dataset.probe === 'owner' && range instanceof Range &&
                range.createContextualFragment('<em>text</em>').querySelector('em')) {
              const sender = new BroadcastChannel('blob-frame-contract');
              sender.postMessage('range-ready');
              sender.close();
            }
          </script>`], { type: 'text/html' }));
        document.body.appendChild(frame);

        const sandboxed = document.createElement('iframe');
        sandboxed.setAttribute('sandbox', 'allow-scripts');
        sandboxed.src = URL.createObjectURL(new Blob([`
          <!doctype html><script>
            try { new BroadcastChannel('blob-frame-contract'); }
            catch (error) { parent.postMessage(error.name, '*'); }
          </script>`], { type: 'text/html' }));
        document.body.appendChild(sandboxed);
        globalThis.blobFrameChecks = () => [
          checks.channel,
          checks.sandbox,
          Boolean(frame.contentDocument?.getElementById('target')),
          sandboxed.contentDocument === null
        ];
    )JS", "blob-frame-contract.js");
    const auto checks = evaluate_until_equals(engine, "blobFrameChecks()", "blob-frame-result.js",
        "[true,true,true,true]", 500);
    require(checks == "[true,true,true,true]",
        "Blob iframe channel, sandbox, document access and sandbox DOM denial: " + checks);
    webscene_engine_destroy(engine);
}
