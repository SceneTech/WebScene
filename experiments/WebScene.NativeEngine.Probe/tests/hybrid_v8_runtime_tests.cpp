#include "webscene_v8_runtime.h"
#include "webscene_native_dom.h"
#include <iostream>
#include <stdexcept>
#include <chrono>
#include <thread>
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void test_compiled_template_shared_document() {
    webscene_native::native_document document;
    webscene_native::v8_dom_runtime runtime(document,
        []{return webscene_native::v8_dom_runtime::viewport_metrics{640,480,1,0};});
    webscene_native::dom_node* constructed = nullptr;
    runtime.register_compiled_template("button", [&](auto& dom, const std::string& data) -> auto& {
        require(&dom == &document, "Template received a different document");
        require(data == R"({"label":"Native"})", "Structured template data changed");
        auto& node = dom.create_element("button");
        node.attributes["id"] = "native-button";
        node.id_attribute = "native-button";
        node.text_content = "Native";
        constructed = &node;
        return node;
    });
    bool duplicate_rejected = false;
    try { runtime.register_compiled_template("button", [](auto& dom, const auto&) -> auto& {
        return dom.body();
    }); } catch (const std::invalid_argument&) { duplicate_rejected = true; }
    require(duplicate_rejected, "Duplicate factory replaced registered template");
    require(runtime.initialize(), "Compiled template runtime failed");
    require(runtime.execute(R"JS(
        const decoder = new TextDecoder('windows-1252', {fatal:true});
        if(decoder.decode(new Uint8Array([0x41,0x80,0x81,0x91,0x9f,0xff])) !== 'A\u20ac\u0081\u2018\u0178\u00ff')
            throw Error('DXF Windows-1252 decoding mismatch');
    )JS", "windows-1252"), runtime.last_error().c_str());
    require(runtime.execute(R"JS(
        const nativeButton = document.createCompiledTemplate('button', {label:'Native'});
        document.body.appendChild(nativeButton);
        if(document.getElementById('native-button') !== nativeButton) throw Error('Identity lost');
        let clicks = 0;
        const listener = () => { clicks++; nativeButton.textContent = 'Clicked'; };
        nativeButton.addEventListener('click', listener);
        nativeButton.click();
        nativeButton.removeEventListener('click', listener);
        nativeButton.click();
        if(clicks !== 1) throw Error('Listener cleanup failed');
        nativeButton.focus();
        if(document.activeElement !== nativeButton) throw Error('Native template focus failed');
        const compatibility = document.createElement('section');
        compatibility.innerHTML = '<b id="runtime-html">Compatible</b>';
        document.body.appendChild(compatibility);
        if(!document.getElementById('runtime-html')) throw Error('Runtime HTML broken');
        const fragment = document.createDocumentFragment();
        const moved = document.createElement('button');
        fragment.appendChild(moved);
        compatibility.replaceChildren('before', fragment, 'after');
        if(fragment.childNodes.length || moved.parentNode !== compatibility || compatibility.childNodes.length !== 3)
            throw Error('replaceChildren did not flatten fragment and preserve text arguments');
        let cycleRejected = false;
        try { compatibility.replaceChildren(compatibility); } catch(e) { cycleRejected = true; }
        if(!cycleRejected || compatibility.childNodes.length !== 3) throw Error('Invalid replacement was destructive');
        compatibility.insertAdjacentHTML('beforeend', '<b id="runtime-html">Compatible</b>');

        let rejected = false;
        try { document.createCompiledTemplate('missing'); } catch(e) { rejected = true; }
        if(!rejected) throw Error('Missing template silently accepted');
    )JS", "compiled-template-shared-document"), runtime.last_error().c_str());
    require(constructed && constructed->children.size() == 1 && constructed->children.front()->text_content == "Clicked", "Native side cannot see JS mutation");
    require(document.find_by_id("runtime-html") != nullptr, "Native side cannot see runtime HTML");
    require(runtime.execute("nativeButton.remove();", "compiled-template-remove"), "Template removal failed");
    require(constructed->parent == nullptr, "Native side cannot see JS removal");
}

void test_blob_worker_source_lifetime() {
    webscene_native::native_document document;
    webscene_native::v8_dom_runtime runtime(document,
        []{return webscene_native::v8_dom_runtime::viewport_metrics{640,480,1,0};});
    bool received = false;
    runtime.register_compiled_template("worker-result", [&](auto& dom, const std::string& value) -> auto& {
        require(value == "42", "Worker result changed"); received = true;
        return dom.create_element("span");
    });
    require(runtime.initialize(), "Blob worker runtime failed");
    require(runtime.execute(R"JS(
        const workerURL = URL.createObjectURL(new Blob(['onmessage=e=>postMessage(e.data+1)'], {type:'text/javascript'}));
        const blobWorker = new Worker(workerURL);
        blobWorker.onmessage = e => document.createCompiledTemplate('worker-result',e.data);
        URL.revokeObjectURL(workerURL);
        let revokedRejected = false;
        try { new Worker(workerURL); } catch(e) { revokedRejected = true; }
        if(!revokedRejected) throw Error('Revoked worker URL accepted');
        blobWorker.postMessage(41);
    )JS", "blob-worker-lifetime"), runtime.last_error().c_str());
    for(unsigned i=0;i<2000 && !received;++i) {
        runtime.pump_task();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    require(received, "Worker lost source after URL revocation");
    require(runtime.execute("blobWorker.terminate()", "worker-cleanup"), runtime.last_error().c_str());
}

void test_worker_message_port_transfer_and_throughput() {
    webscene_native::native_document document;
    webscene_native::v8_dom_runtime runtime(document,
        []{return webscene_native::v8_dom_runtime::viewport_metrics{640,480,1,0};});
    bool completed = false;
    runtime.register_compiled_template("worker-port-result", [&](auto& dom, const std::string& value) -> auto& {
        require(value == "10000", "Worker MessagePort result changed");
        completed = true;
        return dom.create_element("span");
    });
    require(runtime.initialize(), "Worker MessagePort runtime failed");
    require(runtime.execute(R"JS(
        const source = `
          onmessage = event => {
            const port = event.data;
            if (!(port instanceof MessagePort)) throw Error('Transferred value is not a MessagePort');
            if (typeof document !== 'undefined' || typeof importScripts !== 'function')
              throw Error('Worker global shape changed');
            port.onmessage = message => port.postMessage(message.data + 1);
            port.start();
          }`;
        const url = URL.createObjectURL(new Blob([source], {type:'text/javascript'}));
        const worker = new Worker(url, {name:'message-port-contract'});
        URL.revokeObjectURL(url);
        const channel = new MessageChannel();
        if (!(channel.port1 instanceof MessagePort) || !(channel.port1 instanceof EventTarget))
          throw Error('MessagePort interface identity changed');
        let count = 0;
        channel.port1.onmessage = event => {
          count = event.data;
          if (count < 10000) channel.port1.postMessage(count);
          else {
            worker.terminate();
            channel.port1.close();
            document.createCompiledTemplate('worker-port-result', count);
          }
        };
        worker.postMessage(channel.port2, [channel.port2]);
        channel.port2.postMessage('detached endpoint must be inert');
        channel.port1.postMessage(0);
    )JS", "worker-message-port-throughput"), runtime.last_error().c_str());
    const auto started = std::chrono::steady_clock::now();
    while (!completed
        && std::chrono::steady_clock::now() - started < std::chrono::seconds(12)) {
        require(runtime.pump_task(), runtime.last_error().c_str());
        std::this_thread::yield();
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started);
    require(completed, "Worker MessagePort throughput did not complete");
    require(elapsed < std::chrono::seconds(10), "10,000 MessagePort round trips exceeded 10 seconds");
}

void test_message_port_clone_and_queue_bounds() {
    webscene_native::native_document document;
    webscene_native::v8_dom_runtime runtime(document,
        []{return webscene_native::v8_dom_runtime::viewport_metrics{640,480,1,0};});
    require(runtime.initialize(), "MessagePort bound runtime failed");
    require(runtime.execute(R"JS(
        const clonedChannel = new MessageChannel();
        const clone = structuredClone(
          {port: clonedChannel.port2},
          {transfer: [clonedChannel.port2]});
        if (!(clone.port instanceof MessagePort)) throw Error('structuredClone lost MessagePort identity');
        let duplicateRejected = false;
        try { structuredClone(clone.port, {transfer:[clone.port, clone.port]}); }
        catch (error) { duplicateRejected = error.name === 'DataCloneError'; }
        if (!duplicateRejected) throw Error('Duplicate MessagePort transfer was accepted');
        clone.port.close();
        clonedChannel.port1.close();

        const bounded = new MessageChannel();
        const oversized = new ArrayBuffer(16 * 1024 * 1024);
        let byteSaturated = false;
        try { bounded.port1.postMessage(oversized, [oversized]); }
        catch (error) { byteSaturated = error.name === 'QuotaExceededError'; }
        if (!byteSaturated || oversized.byteLength === 0)
          throw Error('MessagePort byte bound detached or accepted an oversized packet');
        for (let index = 0; index < 256; ++index) bounded.port1.postMessage(index);
        let saturated = false;
        try { bounded.port1.postMessage(256); }
        catch (error) { saturated = error.name === 'QuotaExceededError'; }
        if (!saturated) throw Error('MessagePort queue accepted an unbounded backlog');
        bounded.port1.close();
        bounded.port2.close();

        globalThis.closedPortDelivered = false;
        const closing = new MessageChannel();
        closing.port2.onmessage = () => { closedPortDelivered = true; };
        closing.port2.close();
        closing.port2.close();
        closing.port1.postMessage('must not be delivered');
        closing.port1.close();
    )JS", "message-port-clone-and-bounds"), runtime.last_error().c_str());
    require(runtime.execute(
        "if (closedPortDelivered) throw Error('Closed MessagePort received a queued message')",
        "message-port-close"), runtime.last_error().c_str());
}

void test_iframe_worker_extension_host_port_bootstrap() {
    webscene_native::native_document document;
    const std::string root_url = "https://worker.test/index.html";
    const std::string root_html = R"HTML(
      <!doctype html><script>
      addEventListener('message', event => {
        if (event.data !== 'vscode.bootstrap.nls') return;
        const frame = document.querySelector('iframe');
        if (event.source !== frame.contentWindow || event.origin !== origin)
          throw Error('iframe message source or origin changed');
        const channel = new MessageChannel();
        channel.port1.onmessage = response =>
          document.createCompiledTemplate('iframe-port-result', response.data);
        event.source.postMessage(
          {type:'vscode.init', port:channel.port2}, '*', [channel.port2]);
        channel.port1.postMessage(41);
      });
      const frame = document.createElement('iframe');
      document.body.appendChild(frame);
      const child = frame.contentDocument;
      child.open();
      child.write(`<!doctype html><script>
        onmessage = event => {
          if (event.origin !== origin || !(event.data.port instanceof MessagePort)
              || !(event.data.port instanceof EventTarget)
              || event.ports[0] !== event.data.port) {
            throw Error('iframe transfer or same-origin contract changed');
          }
          const port = event.data.port;
          port.onmessage = message => port.postMessage(message.data + 1);
          port.start();
        };
        parent.postMessage('vscode.bootstrap.nls', '*');
      <\/script>`);
      child.close();
      </script>
    )HTML";
    bool completed = false;
    webscene_native::v8_dom_runtime runtime(document,
        []{return webscene_native::v8_dom_runtime::viewport_metrics{640,480,1,0};}, {},
        [&](uint32_t kind, const std::string& url, const auto&, const std::string&, int64_t,
            webscene_native::v8_dom_runtime::resource_response& response) {
            if (kind != WEBSCENE_RESOURCE_DOCUMENT || url != root_url) return false;
            response.content = root_html;
            return true;
        });
    runtime.register_compiled_template("iframe-port-result", [&](auto& dom, const std::string& value) -> auto& {
        require(value == "42", "iframe MessagePort response changed");
        completed = true;
        return dom.create_element("span");
    });
    require(runtime.initialize(), "iframe MessagePort runtime failed");
    require(runtime.load_url(root_url), runtime.last_error().c_str());
    for (unsigned i = 0; i < 5000 && !completed; ++i) {
        require(runtime.pump_task(), runtime.last_error().c_str());
        if (!runtime.has_pending_tasks()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    require(completed, "Code OSS-shaped iframe MessagePort bootstrap did not complete");
}

void test_worker_termination_race() {
    webscene_native::native_document document;
    webscene_native::v8_dom_runtime runtime(document,
        []{return webscene_native::v8_dom_runtime::viewport_metrics{640,480,1,0};});
    require(runtime.initialize(), "Worker termination runtime failed");
    require(runtime.execute(R"JS(
        const url = URL.createObjectURL(new Blob(['while(true) {}'], {type:'text/javascript'}));
        globalThis.terminatingWorker = new Worker(url);
        URL.revokeObjectURL(url);
    )JS", "worker-termination-start"), runtime.last_error().c_str());
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    const auto started = std::chrono::steady_clock::now();
    require(runtime.execute(R"JS(
        for (let index = 0; index < 256; ++index) terminatingWorker.postMessage(index);
        let saturated = false;
        try { terminatingWorker.postMessage(256); }
        catch (error) { saturated = error.name === 'QuotaExceededError'; }
        if (!saturated) throw Error('Worker queue accepted an unbounded backlog');
        terminatingWorker.terminate();
        terminatingWorker.terminate();
    )JS",
        "worker-termination-stop"), runtime.last_error().c_str());
    require(std::chrono::steady_clock::now() - started < std::chrono::seconds(2),
        "Worker termination did not cancel active execution promptly");
}

void test_worker_error_delivery() {
    webscene_native::native_document document;
    webscene_native::v8_dom_runtime runtime(document,
        []{return webscene_native::v8_dom_runtime::viewport_metrics{640,480,1,0};});
    unsigned delivered = 0;
    runtime.register_compiled_template("worker-error-result", [&](auto& dom, const std::string& value) -> auto& {
        require(value == "1", "worker error event shape changed");
        ++delivered;
        return dom.create_element("span");
    });
    require(runtime.initialize(), "worker error runtime failed");
    require(runtime.execute(R"JS(
        const url = URL.createObjectURL(new Blob(
          ['throw new Error("expected-worker-failure")'],
          {type:'text/javascript'}));
        const failingWorker = new Worker(url);
        URL.revokeObjectURL(url);
        failingWorker.onerror = event => {
          document.createCompiledTemplate(
            'worker-error-result',
            event.type === 'error' && event.message.includes('expected-worker-failure') ? 1 : 0);
          failingWorker.terminate();
        };

        const moduleUrl = URL.createObjectURL(new Blob(
          ['export const broken = ;'],
          {type:'text/javascript'}));
        const failingModuleWorker = new Worker(moduleUrl, {type:'module'});
        URL.revokeObjectURL(moduleUrl);
        failingModuleWorker.onerror = event => {
          document.createCompiledTemplate(
            'worker-error-result', event.type === 'error' ? 1 : 0);
          failingModuleWorker.terminate();
        };
    )JS", "worker-error-delivery"), runtime.last_error().c_str());
    for (unsigned index = 0; index < 2000 && delivered < 2U; ++index) {
        require(runtime.pump_task(), runtime.last_error().c_str());
        if (!runtime.has_pending_tasks()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    require(delivered == 2U, "Classic and module worker failures did not dispatch error events");
}

void test_worker_and_port_navigation_shutdown() {
    webscene_native::native_document document;
    const std::string navigation_url = "https://worker.test/after-navigation.html";
    webscene_native::v8_dom_runtime runtime(document,
        []{return webscene_native::v8_dom_runtime::viewport_metrics{640,480,1,0};}, {},
        [&](uint32_t kind, const std::string& url, const auto&, const std::string&, int64_t,
            webscene_native::v8_dom_runtime::resource_response& response) {
            if (kind != WEBSCENE_RESOURCE_DOCUMENT || url != navigation_url) return false;
            response.content = "<!doctype html><title>after navigation</title>";
            return true;
        });
    require(runtime.initialize(), "worker navigation runtime failed");
    require(runtime.execute(R"JS(
        const url = URL.createObjectURL(new Blob(['while(true) {}'], {type:'text/javascript'}));
        globalThis.navigationWorker = new Worker(url);
        URL.revokeObjectURL(url);
        globalThis.navigationChannel = new MessageChannel();
        navigationChannel.port1.postMessage('queued before navigation');
    )JS", "worker-navigation-start"), runtime.last_error().c_str());
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    const auto started = std::chrono::steady_clock::now();
    require(runtime.load_url(navigation_url), runtime.last_error().c_str());
    require(std::chrono::steady_clock::now() - started < std::chrono::seconds(2),
        "Navigation did not cancel worker execution promptly");
    require(runtime.execute(R"JS(
        const afterNavigation = new MessageChannel();
        afterNavigation.port1.close();
        afterNavigation.port2.close();
    )JS", "worker-navigation-resources-reset"), runtime.last_error().c_str());
}

void test_window_messageerror_on_receiver_resource_exhaustion() {
    webscene_native::native_document document;
    webscene_native::v8_dom_runtime runtime(document,
        []{return webscene_native::v8_dom_runtime::viewport_metrics{640,480,1,0};});
    bool completed = false;
    runtime.register_compiled_template("messageerror-result", [&](auto& dom, const std::string& value) -> auto& {
        require(value == "1", "iframe received the wrong structured-clone failure event");
        completed = true;
        return dom.create_element("span");
    });
    require(runtime.initialize(), "window messageerror runtime failed");
    require(runtime.execute(R"JS(
        addEventListener('message', event => {
          if (event.data !== 'messageerror-ready') return;
          const channels = [];
          for (let index = 0; index < 2048; ++index) channels.push(new MessageChannel());
          const transferred = channels[channels.length - 1].port2;
          event.source.postMessage({transferred}, '*', [transferred]);
        });
        const frame = document.createElement('iframe');
        document.body.appendChild(frame);
        const child = frame.contentDocument;
        child.open();
        child.write(`<script>
          onmessageerror = event =>
            document.createCompiledTemplate('messageerror-result', event.type === 'messageerror' ? 1 : 0);
          parent.postMessage('messageerror-ready', '*');
        <\/script>`);
        child.close();
    )JS", "window-messageerror-resource-bound"), runtime.last_error().c_str());
    for (unsigned index = 0; index < 32 && !completed; ++index) {
        require(runtime.pump_task(), runtime.last_error().c_str());
    }
    require(completed, "Window did not dispatch messageerror after clone reconstruction failed");
}

void test_compiled_document_lifecycle(bool strict) {
    webscene_native::native_document document;
    unsigned document_requests = 0;
    webscene_native::v8_dom_runtime runtime(document,
        []{return webscene_native::v8_dom_runtime::viewport_metrics{640,480,1,0};}, {},
        [&](uint32_t kind, const std::string&, const auto&, const std::string&, int64_t, auto&) {
            if (kind == WEBSCENE_RESOURCE_DOCUMENT) ++document_requests;
            return false;
        });
    require(runtime.initialize(), "Compiled root runtime failed");
    webscene_native::v8_dom_runtime::compiled_document package;
    package.allow_runtime_html = !strict;
    package.base_url = "https://compiled.test/app/index.html";
    package.construct = [](auto& dom) {
        auto& html = dom.create_element("html");
        auto& head = dom.create_element("head");
        auto& body = dom.create_element("body");
        auto& button = dom.create_element("button");
        button.id_attribute = "compiled-root-button";
        button.attributes["id"] = button.id_attribute;
        dom.append_child(dom.body(), html);
        dom.append_child(html, head);
        dom.append_child(html, body);
        dom.append_child(body, button);
    };
    package.templates.emplace("parts", [](auto& dom, webscene_native::compiled_template_arguments args) -> auto& {
        auto& fragment = dom.create_element("#document-fragment");
        auto& span = dom.create_element("span");
        webscene_native::compiled_append_argument(dom, span, webscene_native::compiled_argument(args, 0));
        dom.append_child(fragment, span);
        return fragment;
    });
    package.scripts.push_back({{}, R"JS(
        if(!document.getElementById('compiled-root-button')) throw Error('Scripts ran before construction');
        if(document.readyState !== 'loading') throw Error('Incorrect initial lifecycle');
        globalThis.lifecycle = ['script'];
        document.addEventListener('DOMContentLoaded', () => lifecycle.push('dom'));
        window.addEventListener('load', () => lifecycle.push('load'));
    )JS"});
    require(runtime.load_compiled_document(package), runtime.last_error().c_str());
    require(document_requests == 0, "Compiled navigation fetched an HTML shell");
    require(runtime.execute(R"JS(
        const input = document.createDocumentFragment();
        const child = document.createElement('em'); child.textContent = 'native parts';
        input.appendChild(child);
        const output = document.createCompiledTemplateParts('parts', [input]);
        if(input.childNodes.length || output.firstChild.firstChild !== child) throw Error('Native parts lost node identity');
        document.body.appendChild(output);
        if(!child.isConnected || output.childNodes.length) throw Error('Native parts not attached');
        let rejected = false;
        try { document.createCompiledTemplateParts('parts', [child]); } catch(e) { rejected = true; }
        if(!rejected || !child.isConnected) throw Error('Attached argument was moved or accepted');
        if(lifecycle.join(',') !== 'script,dom,load') throw Error('Lifecycle order');
        if(document.readyState !== 'complete') throw Error('Readiness incomplete');
        if(location.href !== 'https://compiled.test/app/index.html') throw Error('Base URL lost');

    )JS", "compiled-document-lifecycle"), runtime.last_error().c_str());
    if (strict) {
        require(runtime.execute(R"JS(
            const control = document.getElementById('compiled-root-button');
            control.textContent = 'Preserved';
            const attempts = [
                () => { control.innerHTML = '<b>Forbidden</b>'; },
                () => control.insertAdjacentHTML('beforeend', '<b>Forbidden</b>'),
                () => new DOMParser().parseFromString('<b>Forbidden</b>', 'text/html'),
                () => document.createRange().createContextualFragment('<b>Forbidden</b>')
            ];
            for(const attempt of attempts) {
                let rejected = false;
                try { attempt(); } catch(e) { rejected = String(e).includes('Runtime HTML'); }
                if(!rejected) throw Error('Strict parser entry not rejected');
                if(control.textContent !== 'Preserved') throw Error('Rejected parse mutated existing content');
            }
            control.innerHTML = '';
            if(control.textContent !== '') throw Error('Empty clear should not require parsing');
        )JS", "compiled-document-strict"), runtime.last_error().c_str());
        require(!runtime.load_url("https://compiled.test/runtime.html"), "Strict document allowed HTML navigation");
    } else {
        require(runtime.execute("document.getElementById('compiled-root-button').innerHTML = '<span>Compatible</span>';",
            "compiled-document-compatible"), runtime.last_error().c_str());
    }
}

int main() {
    try {
        test_blob_worker_source_lifetime();
        test_worker_message_port_transfer_and_throughput();
        test_message_port_clone_and_queue_bounds();
        test_iframe_worker_extension_host_port_bootstrap();
        test_worker_termination_race();
        test_worker_error_delivery();
        test_worker_and_port_navigation_shutdown();
        test_window_messageerror_on_receiver_resource_exhaustion();
        test_compiled_template_shared_document();
        test_compiled_document_lifecycle(false);
        test_compiled_document_lifecycle(true);
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
