    static std::string service_worker_origin(std::string_view address)
    {
        if (address.starts_with("file://")) return "file://";
        return resource_origin(std::string(address));
    }

    static constexpr size_t maximum_service_worker_queue_messages = 256U;
    static constexpr size_t maximum_service_worker_queue_bytes =
        16U * 1024U * 1024U;

    static void set_service_worker_client_properties(
        v8::Isolate* isolate,
        v8::Local<v8::Context> context,
        v8::Local<v8::Object> object,
        const service_worker_client_descriptor& client)
    {
        const auto set = [&](const char* name, std::string_view value) {
            object->Set(
                context,
                js_string(isolate, name),
                js_dom_string(isolate, value)).Check();
        };
        set("id", client.id);
        set("url", client.url);
        set("type", client.type);
        set("frameType", client.frame_type);
        object->Set(
            context,
            js_string(isolate, "generation"),
            v8::Number::New(isolate, static_cast<double>(client.generation))).Check();
    }

    static v8::MaybeLocal<v8::Value> service_worker_transfer(
        const v8::FunctionCallbackInfo<v8::Value>& info,
        int index)
    {
        if (info.Length() <= index) return v8::Undefined(info.GetIsolate());
        if (info[index]->IsObject() && !info[index]->IsArray()) {
            return info[index].As<v8::Object>()->Get(
                info.GetIsolate()->GetCurrentContext(),
                js_string(info.GetIsolate(), "transfer"));
        }
        return info[index];
    }

    static void service_worker_global_control(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* runtime = static_cast<service_worker_runtime_state*>(
            info.Data().As<v8::External>()->Value(
                v8::kExternalPointerTypeTagDefault));
        if (runtime == nullptr || info.Length() < 1) return;
        auto* isolate = info.GetIsolate();
        auto context = isolate->GetCurrentContext();
        const auto operation = to_utf8(isolate, info[0]);
        if (operation == "claim") {
            std::lock_guard lock(runtime->mutex);
            if (!runtime->stopped) runtime->claimed = true;
            return;
        }
        if (operation == "get" || operation == "list") {
            std::vector<service_worker_client_descriptor> clients;
            {
                std::lock_guard lock(runtime->mutex);
                if (operation == "get") {
                    if (info.Length() < 2) return;
                    const auto id = to_utf8(isolate, info[1]);
                    if (const auto found = runtime->clients.find(id);
                        found != runtime->clients.end()) {
                        clients.push_back(found->second);
                    }
                } else {
                    const auto requested_type = info.Length() > 1
                        ? to_utf8(isolate, info[1]) : std::string{};
                    for (const auto& [id, client] : runtime->clients) {
                        static_cast<void>(id);
                        if (requested_type.empty() || requested_type == "all"
                            || client.type == requested_type) {
                            clients.push_back(client);
                        }
                    }
                }
            }
            std::ranges::sort(clients, {}, [](const auto& client) {
                return client.id;
            });
            if (operation == "get") {
                if (clients.empty()) {
                    info.GetReturnValue().Set(v8::Undefined(isolate));
                    return;
                }
                auto value = v8::Object::New(isolate);
                set_service_worker_client_properties(
                    isolate, context, value, clients.front());
                info.GetReturnValue().Set(value);
                return;
            }
            auto result = v8::Array::New(isolate, static_cast<int>(clients.size()));
            for (uint32_t index = 0U; index < clients.size(); ++index) {
                auto value = v8::Object::New(isolate);
                set_service_worker_client_properties(
                    isolate, context, value, clients[index]);
                result->Set(context, index, value).Check();
            }
            info.GetReturnValue().Set(result);
            return;
        }
        if (operation == "post") {
            if (info.Length() < 3) return;
            const auto client_id = to_utf8(isolate, info[1]);
            uint64_t generation = 0U;
            size_t remaining_bytes = 0U;
            {
                std::lock_guard lock(runtime->mutex);
                const auto client = runtime->clients.find(client_id);
                if (runtime->stopped || client == runtime->clients.end()) {
                    throw_dom_exception(
                        info, "The service worker client is unavailable.",
                        "InvalidStateError");
                    return;
                }
                if (runtime->outgoing.size()
                        >= maximum_service_worker_queue_messages
                    || runtime->outgoing_bytes
                        >= maximum_service_worker_queue_bytes) {
                    throw_dom_exception(
                        info, "Service worker message queue capacity exhausted.",
                        "QuotaExceededError");
                    return;
                }
                generation = client->second.generation;
                remaining_bytes = maximum_service_worker_queue_bytes
                    - runtime->outgoing_bytes;
            }
            v8::Local<v8::Value> transfer;
            if (!service_worker_transfer(info, 3).ToLocal(&transfer)) return;
            clone_packet packet;
            if (!serialize_clone(
                    info, info[2], transfer, packet, remaining_bytes)) return;
            const auto bytes = clone_packet_size(packet);
            {
                std::lock_guard lock(runtime->mutex);
                if (runtime->stopped
                    || runtime->outgoing.size()
                        >= maximum_service_worker_queue_messages
                    || bytes > maximum_service_worker_queue_bytes
                        - runtime->outgoing_bytes) {
                    throw_dom_exception(
                        info, "Service worker message queue capacity exhausted.",
                        "QuotaExceededError");
                    return;
                }
                runtime->outgoing_bytes += bytes;
                runtime->outgoing_high_water = std::max(
                    runtime->outgoing_high_water, runtime->outgoing.size() + 1U);
                runtime->outgoing.push_back(service_worker_message{
                    client_id, generation, std::move(packet)});
            }
            if (runtime->notify_client) runtime->notify_client();
            return;
        }
        isolate->ThrowException(v8::Exception::TypeError(
            js_string(isolate, "Unknown service worker global operation")));
    }

    static bool is_service_worker_secure_origin(std::string_view origin)
    {
        if (origin == "file://") return true;
        if (origin.starts_with("https://")) return true;
        if (!origin.starts_with("http://")) return false;
        auto authority = origin.substr(7U);
        if (authority.starts_with('[')) {
            if (const auto close = authority.find(']');
                close != std::string_view::npos) {
                authority = authority.substr(0U, close + 1U);
            }
        } else if (const auto colon = authority.find(':');
            colon != std::string_view::npos) {
            authority = authority.substr(0U, colon);
        }
        return authority == "localhost" || authority == "127.0.0.1"
            || authority == "[::1]";
    }

    static std::string service_worker_default_scope(std::string script_url)
    {
        if (const auto suffix = script_url.find_first_of("?#");
            suffix != std::string::npos) {
            script_url.resize(suffix);
        }
        const auto slash = script_url.rfind('/');
        return slash == std::string::npos
            ? script_url
            : script_url.substr(0U, slash + 1U);
    }

    static void set_service_worker_string(
        v8::Isolate* isolate,
        v8::Local<v8::Context> context,
        v8::Local<v8::Object> object,
        const char* name,
        std::string_view value)
    {
        object->Set(
            context,
            js_string(isolate, name),
            js_dom_string(isolate, value)).Check();
    }

    v8::Local<v8::Object> service_worker_registration_snapshot(
        v8::Local<v8::Context> local_context,
        const service_worker_registration_state& registration)
    {
        auto result = v8::Object::New(isolate);
        set_service_worker_string(
            isolate, local_context, result, "scope", registration.scope);
        set_service_worker_string(
            isolate, local_context, result, "scriptURL", registration.script_url);
        set_service_worker_string(
            isolate, local_context, result, "type", registration.worker_type);
        set_service_worker_string(
            isolate, local_context, result, "updateViaCache", registration.update_via_cache);
        auto state = std::string_view{"installing"};
        auto claimed = false;
        if (registration.runtime != nullptr) {
            std::lock_guard lock(registration.runtime->mutex);
            if (registration.runtime->activated) state = "activated";
            claimed = registration.runtime->claimed;
        }
        set_service_worker_string(isolate, local_context, result, "state", state);
        result->Set(
            local_context,
            js_string(isolate, "claimed"),
            v8::Boolean::New(isolate, claimed)).Check();
        result->Set(
            local_context,
            js_string(isolate, "generation"),
            v8::Number::New(isolate, static_cast<double>(registration.generation))).Check();
        return result;
    }

    std::shared_ptr<service_worker_runtime_state> start_service_worker_runtime(
        const service_worker_registration_state& registration,
        std::string source,
        std::string origin,
        std::vector<service_worker_client_descriptor> clients)
    {
        auto state = std::make_shared<service_worker_runtime_state>();
        for (auto& client : clients) {
            state->clients.emplace(client.id, std::move(client));
        }
        state->notify_client = runtime_work_available;
        auto loader = load_resource_callback;
        auto root = resource_root;
        auto object_urls = object_url_registry;
        auto cookies = cookie_jar;
        const auto script_url = registration.script_url;
        const auto module = registration.worker_type == "module";
        state->thread = std::thread([
            state,
            loader = std::move(loader),
            root = std::move(root),
            object_urls = std::move(object_urls),
            cookies = std::move(cookies),
            script_url,
            module,
            source = std::move(source),
            origin = std::move(origin)]() mutable {
            const auto fail = [&](std::string value) {
                {
                    std::lock_guard lock(state->mutex);
                    if (!state->stopped) state->error = std::move(value);
                }
                state->ready.notify_all();
            };
            try {
                native_document document;
                v8_dom_runtime runtime(
                    document,
                    [] { return v8_dom_runtime::viewport_metrics{1, 1, 1, 0}; },
                    {},
                    loader);
                auto* child = runtime.impl_.get();
                child->object_url_registry = object_urls;
                child->cookie_jar = cookies;
                child->inherited_worker_origin = origin;
                child->force_dedicated_isolate = true;
                child->service_worker_realm = true;
                runtime.set_resource_root(root.string());
                if (!runtime.initialize()) throw std::runtime_error(runtime.last_error());
                {
                    std::lock_guard lock(state->mutex);
                    if (state->stopped) return;
                    state->running_isolate = child->isolate;
                }
                {
                    v8::Isolate::Scope isolate_scope(child->isolate);
                    v8::HandleScope handles(child->isolate);
                    auto realm = child->context.Get(child->isolate);
                    v8::Context::Scope context_scope(realm);
                    auto global = realm->Global();
                    child->document_base_address = script_url;
                    child->set_context_location(realm, global, script_url);
                    global->Set(
                        realm,
                        js_string(child->isolate, "origin"),
                        js_dom_string(child->isolate, origin)).Check();
                    global->Set(realm, js_string(child->isolate, "self"), global).Check();
                    global->Delete(realm, js_string(child->isolate, "document")).FromMaybe(false);
                    global->Delete(realm, js_string(child->isolate, "window")).FromMaybe(false);
                    global->Set(
                        realm,
                        js_string(child->isolate, "__webSceneServiceWorkerGlobalControl"),
                        v8::Function::New(
                            realm,
                            service_worker_global_control,
                            v8::External::New(
                                child->isolate,
                                state.get(),
                                v8::kExternalPointerTypeTagDefault))
                            .ToLocalChecked()).Check();
                    constexpr std::string_view bootstrap = R"JS(
                      (() => {
                        function WorkerGlobalScope() {
                          throw new TypeError('Illegal constructor');
                        }
                        function ServiceWorkerGlobalScope() {
                          throw new TypeError('Illegal constructor');
                        }
                        Object.setPrototypeOf(
                          WorkerGlobalScope.prototype, EventTarget.prototype);
                        Object.setPrototypeOf(
                          ServiceWorkerGlobalScope.prototype,
                          WorkerGlobalScope.prototype);
                        Object.defineProperty(
                          WorkerGlobalScope.prototype, Symbol.toStringTag,
                          {value:'WorkerGlobalScope', configurable:true});
                        Object.defineProperty(
                          ServiceWorkerGlobalScope.prototype, Symbol.toStringTag,
                          {value:'ServiceWorkerGlobalScope', configurable:true});
                        Object.defineProperties(globalThis, {
                          WorkerGlobalScope: {
                            value:WorkerGlobalScope, writable:true, configurable:true
                          },
                          ServiceWorkerGlobalScope: {
                            value:ServiceWorkerGlobalScope, writable:true, configurable:true
                          }
                        });
                        Object.setPrototypeOf(
                          globalThis, ServiceWorkerGlobalScope.prototype);
                        const control = globalThis.__webSceneServiceWorkerGlobalControl;
                        delete globalThis.__webSceneServiceWorkerGlobalControl;
                        function Client() { throw new TypeError('Illegal constructor'); }
                        function WindowClient() { throw new TypeError('Illegal constructor'); }
                        function Clients() { throw new TypeError('Illegal constructor'); }
                        Object.setPrototypeOf(WindowClient.prototype, Client.prototype);
                        Object.defineProperty(Client.prototype, Symbol.toStringTag,
                          {value:'Client', configurable:true});
                        Object.defineProperty(WindowClient.prototype, Symbol.toStringTag,
                          {value:'WindowClient', configurable:true});
                        Object.defineProperty(Clients.prototype, Symbol.toStringTag,
                          {value:'Clients', configurable:true});
                        const clientState = new WeakMap();
                        const clientWrappers = new Map();
                        const clientSnapshot = value => {
                          const snapshot = clientState.get(value);
                          if (!snapshot) throw new TypeError('Illegal invocation');
                          return snapshot;
                        };
                        const hydrateClient = snapshot => {
                          if (!snapshot) return undefined;
                          let value = clientWrappers.get(snapshot.id);
                          if (!value) {
                            value = Object.create(WindowClient.prototype);
                            clientWrappers.set(snapshot.id, value);
                          }
                          clientState.set(value, snapshot);
                          return value;
                        };
                        Object.defineProperty(
                          globalThis,
                          Symbol.for('webscene.internal.serviceWorkerClient'),
                          {value:hydrateClient});
                        Object.defineProperties(Client.prototype, {
                          id: {get() { return clientSnapshot(this).id; },
                            enumerable:true, configurable:true},
                          type: {get() { return clientSnapshot(this).type; },
                            enumerable:true, configurable:true},
                          url: {get() { return clientSnapshot(this).url; },
                            enumerable:true, configurable:true},
                          postMessage: {value(message, options = []) {
                            control('post', clientSnapshot(this).id, message, options);
                          }, writable:true, enumerable:true, configurable:true}
                        });
                        Object.defineProperties(WindowClient.prototype, {
                          focused: {get() { clientSnapshot(this); return true; },
                            enumerable:true, configurable:true},
                          visibilityState: {get() {
                            clientSnapshot(this); return 'visible';
                          }, enumerable:true, configurable:true},
                          frameType: {get() { return clientSnapshot(this).frameType; },
                            enumerable:true, configurable:true},
                          focus: {value() {
                            return Promise.resolve().then(() => {
                              clientSnapshot(this);
                              return this;
                            });
                          }, writable:true, enumerable:true, configurable:true}
                        });
                        const clientsState = new WeakSet();
                        const requireClients = value => {
                          if (!clientsState.has(value)) {
                            throw new TypeError('Illegal invocation');
                          }
                        };
                        Object.defineProperties(Clients.prototype, {
                          get: {value:function(id) {
                            return Promise.resolve().then(() => {
                              requireClients(this);
                              return hydrateClient(control('get', String(id)));
                            });
                          }, writable:true, enumerable:true, configurable:true},
                          matchAll: {value:function(options = {}) {
                            return Promise.resolve().then(() => {
                              requireClients(this);
                              const type = options.type === undefined
                                ? 'all' : String(options.type);
                              return control('list', type).map(hydrateClient);
                            });
                          }, writable:true, enumerable:true, configurable:true},
                          claim: {value:function() {
                            return Promise.resolve().then(() => {
                              requireClients(this);
                              control('claim');
                            });
                          }, writable:true, enumerable:true, configurable:true}
                        });
                        const clients = Object.create(Clients.prototype);
                        clientsState.add(clients);
                        Object.defineProperties(globalThis, {
                          Client: {value:Client, writable:true, configurable:true},
                          WindowClient: {value:WindowClient, writable:true, configurable:true},
                          Clients: {value:Clients, writable:true, configurable:true},
                          clients: {value:clients, enumerable:true, configurable:true},
                          skipWaiting: {
                            value() { return Promise.resolve(); },
                            writable:true, configurable:true
                          },
                          __webSceneRunServiceWorkerLifecycle: {
                            value: async function() {
                              for (const type of ['install', 'activate']) {
                                const promises = [];
                                const event = new Event(type);
                                Object.defineProperty(event, 'waitUntil', {
                                  value(value) { promises.push(Promise.resolve(value)); }
                                });
                                dispatchEvent(event);
                                await Promise.all(promises);
                              }
                              return true;
                            },
                            configurable:true
                          }
                        });
                      })();
                    )JS";
                    std::string error;
                    if (!child->execute_in_context(
                            realm, bootstrap, "webscene:service-worker-bootstrap", error)) {
                        throw std::runtime_error(error);
                    }
                    if (!(module
                            ? child->execute_module(realm, source, script_url, error)
                            : child->execute_in_context(realm, source, script_url, error))) {
                        throw std::runtime_error(error);
                    }
                    child->perform_microtask_checkpoint();
                    v8::Local<v8::Value> lifecycle_value;
                    if (!global->Get(
                            realm,
                            js_string(child->isolate, "__webSceneRunServiceWorkerLifecycle"))
                            .ToLocal(&lifecycle_value)
                        || !lifecycle_value->IsFunction()) {
                        throw std::runtime_error("Service worker lifecycle dispatcher is unavailable");
                    }
                    v8::Local<v8::Value> lifecycle_result;
                    if (!lifecycle_value.As<v8::Function>()->Call(
                            realm, global, 0, nullptr).ToLocal(&lifecycle_result)
                        || !lifecycle_result->IsPromise()) {
                        throw std::runtime_error("Service worker lifecycle dispatch failed");
                    }
                    auto lifecycle = lifecycle_result.As<v8::Promise>();
                    for (auto attempt = 0; attempt < 1000
                         && lifecycle->State() == v8::Promise::kPending; ++attempt) {
                        child->drain_tasks();
                        child->perform_microtask_checkpoint();
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    }
                    if (lifecycle->State() != v8::Promise::kFulfilled) {
                        throw std::runtime_error(
                            lifecycle->State() == v8::Promise::kRejected
                                ? to_utf8(child->isolate, lifecycle->Result())
                                : "Service worker lifecycle timed out");
                    }
                }
                {
                    std::lock_guard lock(state->mutex);
                    if (!state->stopped) state->activated = true;
                }
                state->ready.notify_all();
                for (;;) {
                    std::optional<service_worker_message> message;
                    {
                        std::unique_lock lock(state->mutex);
                        state->ready.wait_for(lock, std::chrono::milliseconds(100), [&] {
                            return state->stopped || !state->incoming.empty();
                        });
                        if (state->stopped) break;
                        if (!state->incoming.empty()) {
                            message = std::move(state->incoming.front());
                            state->incoming.pop_front();
                            const auto bytes = clone_packet_size(message->packet);
                            state->incoming_bytes = bytes > state->incoming_bytes
                                ? 0U : state->incoming_bytes - bytes;
                        }
                    }
                    v8::Isolate::Scope isolate_scope(child->isolate);
                    v8::HandleScope handles(child->isolate);
                    auto realm = child->context.Get(child->isolate);
                    v8::Context::Scope context_scope(realm);
                    if (message.has_value()) {
                        v8::TryCatch caught(child->isolate);
                        std::vector<v8::Local<v8::Object>> ports;
                        v8::Local<v8::Value> data;
                        const auto decoded = child->deserialize_clone(
                            child->isolate,
                            realm,
                            message->packet,
                            &ports).ToLocal(&data);
                        if (!decoded) caught.Reset();
                        auto event = v8::Object::New(child->isolate);
                        event->Set(
                            realm,
                            js_string(child->isolate, "type"),
                            js_string(child->isolate,
                                decoded ? "message" : "messageerror")).Check();
                        if (decoded) {
                            event->Set(
                                realm, js_string(child->isolate, "data"), data).Check();
                        }
                        event->Set(
                            realm,
                            js_string(child->isolate, "waitUntil"),
                            v8::Function::New(
                                realm,
                                [](const v8::FunctionCallbackInfo<v8::Value>&) {})
                                .ToLocalChecked()).Check();
                        auto port_array = v8::Array::New(
                            child->isolate, static_cast<int>(ports.size()));
                        for (uint32_t index = 0U; index < ports.size(); ++index) {
                            port_array->Set(realm, index, ports[index]).Check();
                        }
                        event->Set(
                            realm,
                            js_string(child->isolate, "ports"),
                            port_array).Check();
                        v8::Local<v8::Value> hydrate_value;
                        if (auto symbol = v8::Symbol::For(
                                child->isolate,
                                js_string(child->isolate,
                                    "webscene.internal.serviceWorkerClient"));
                            realm->Global()->Get(realm, symbol).ToLocal(&hydrate_value)
                            && hydrate_value->IsFunction()) {
                            service_worker_client_descriptor client;
                            {
                                std::lock_guard lock(state->mutex);
                                if (const auto found = state->clients.find(
                                        message->client_id);
                                    found != state->clients.end()
                                    && found->second.generation
                                        == message->client_generation) {
                                    client = found->second;
                                }
                            }
                            if (!client.id.empty()) {
                                auto snapshot = v8::Object::New(child->isolate);
                                set_service_worker_client_properties(
                                    child->isolate, realm, snapshot, client);
                                v8::Local<v8::Value> argument[]{snapshot};
                                v8::Local<v8::Value> source;
                                if (hydrate_value.As<v8::Function>()->Call(
                                        realm,
                                        realm->Global(),
                                        1,
                                        argument).ToLocal(&source)) {
                                    event->Set(
                                        realm,
                                        js_string(child->isolate, "source"),
                                        source).Check();
                                }
                            }
                        }
                        v8::Local<v8::Value> dispatch;
                        if (realm->Global()->Get(
                                realm,
                                js_string(child->isolate, "dispatchEvent"))
                                .ToLocal(&dispatch)
                            && dispatch->IsFunction()) {
                            v8::Local<v8::Value> argument[]{event};
                            if (dispatch.As<v8::Function>()->Call(
                                    realm,
                                    realm->Global(),
                                    1,
                                    argument).IsEmpty()) {
                                throw std::runtime_error(
                                    child->describe_reported_exception(caught, realm));
                            }
                        }
                    }
                    child->drain_tasks();
                    child->perform_microtask_checkpoint();
                }
                {
                    std::lock_guard lock(state->mutex);
                    state->running_isolate = nullptr;
                }
                child->isolate->CancelTerminateExecution();
            } catch (const std::exception& exception) {
                {
                    std::lock_guard lock(state->mutex);
                    state->running_isolate = nullptr;
                }
                fail(exception.what());
            }
        });
        return state;
    }

    std::vector<service_worker_client_descriptor> service_worker_clients_for_scope(
        std::string_view scope)
    {
        std::vector<service_worker_client_descriptor> result;
        for (const auto& [id, client] : service_worker_clients) {
            static_cast<void>(id);
            if (!client.context.IsEmpty()
                && client.descriptor.url.starts_with(scope)) {
                result.push_back(client.descriptor);
            }
        }
        return result;
    }

    static void service_worker_control(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        if (self == nullptr || info.Length() < 1) return;
        auto* isolate = info.GetIsolate();
        auto local_context = isolate->GetCurrentContext();
        const auto operation = to_utf8(isolate, info[0]);
        auto client_url = self->current_base_address();
        if (client_url.empty()) {
            v8::Local<v8::Value> location_value;
            v8::Local<v8::Value> href_value;
            if (local_context->Global()->Get(
                    local_context, js_string(isolate, "location"))
                    .ToLocal(&location_value)
                && location_value->IsObject()
                && location_value.As<v8::Object>()->Get(
                    local_context, js_string(isolate, "href"))
                    .ToLocal(&href_value)) {
                client_url = to_utf8(isolate, href_value);
            }
        }
        const auto client_origin = service_worker_origin(client_url);
        if (!is_service_worker_secure_origin(client_origin)) {
            throw_dom_exception(
                info,
                "Service workers require a potentially trustworthy origin.",
                "SecurityError");
            return;
        }

        const auto snapshot = [&](const service_worker_registration_state& value) {
            info.GetReturnValue().Set(
                self->service_worker_registration_snapshot(local_context, value));
        };
        if (operation == "client") {
            service_worker_client_state* known = nullptr;
            for (auto& [id, client] : self->service_worker_clients) {
                static_cast<void>(id);
                if (!client.context.IsEmpty()
                    && client.context.Get(isolate) == local_context) {
                    known = &client;
                    break;
                }
            }
            if (known == nullptr) {
                service_worker_client_descriptor descriptor;
                descriptor.id = "webscene-client-"
                    + std::to_string(self->next_service_worker_client_id++);
                descriptor.url = client_url;
                descriptor.frame_type = self->in_frame_context()
                    ? "nested" : "top-level";
                descriptor.generation = self->document_generation.load(
                    std::memory_order_acquire);
                service_worker_client_state client;
                client.descriptor = descriptor;
                client.context.Reset(isolate, local_context);
                auto [inserted, created] = self->service_worker_clients.emplace(
                    descriptor.id, std::move(client));
                static_cast<void>(created);
                known = &inserted->second;
                for (auto& [scope, registration] :
                    self->service_worker_registrations) {
                    if (registration.runtime == nullptr
                        || !descriptor.url.starts_with(scope)) continue;
                    std::lock_guard lock(registration.runtime->mutex);
                    if (!registration.runtime->stopped) {
                        registration.runtime->clients[descriptor.id] = descriptor;
                    }
                }
            }
            auto value = v8::Object::New(isolate);
            set_service_worker_client_properties(
                isolate, local_context, value, known->descriptor);
            info.GetReturnValue().Set(value);
            return;
        }
        if (operation == "bind") {
            if (info.Length() < 3 || !info[2]->IsObject()) return;
            const auto id = to_utf8(isolate, info[1]);
            if (auto client = self->service_worker_clients.find(id);
                client != self->service_worker_clients.end()
                && !client->second.context.IsEmpty()
                && client->second.context.Get(isolate) == local_context) {
                client->second.container.Reset(isolate, info[2].As<v8::Object>());
                info.GetReturnValue().Set(v8::True(isolate));
            } else {
                info.GetReturnValue().Set(v8::False(isolate));
            }
            return;
        }
        if (operation == "controller") {
            if (info.Length() < 2) return;
            const auto id = to_utf8(isolate, info[1]);
            const auto client = self->service_worker_clients.find(id);
            if (client == self->service_worker_clients.end()) {
                info.GetReturnValue().Set(v8::Null(isolate));
                return;
            }
            service_worker_registration_state* matched = nullptr;
            for (auto& [scope, registration] : self->service_worker_registrations) {
                if (registration.runtime == nullptr
                    || !client->second.descriptor.url.starts_with(scope)) continue;
                std::lock_guard lock(registration.runtime->mutex);
                if (!registration.runtime->activated
                    || !registration.runtime->claimed) continue;
                if (matched == nullptr || scope.size() > matched->scope.size()) {
                    matched = &registration;
                }
            }
            if (matched == nullptr) info.GetReturnValue().Set(v8::Null(isolate));
            else snapshot(*matched);
            return;
        }
        if (operation == "post") {
            if (info.Length() < 5) return;
            const auto scope = to_utf8(isolate, info[1]);
            const auto client_id = to_utf8(isolate, info[2]);
            const auto registration = self->service_worker_registrations.find(scope);
            const auto client = self->service_worker_clients.find(client_id);
            if (registration == self->service_worker_registrations.end()
                || registration->second.runtime == nullptr
                || client == self->service_worker_clients.end()) {
                throw_dom_exception(
                    info, "The service worker is not active.", "InvalidStateError");
                return;
            }
            auto runtime = registration->second.runtime;
            size_t remaining_bytes = 0U;
            {
                std::lock_guard lock(runtime->mutex);
                const auto runtime_client = runtime->clients.find(client_id);
                if (runtime->stopped || !runtime->activated
                    || !runtime->claimed
                    || runtime_client == runtime->clients.end()
                    || runtime_client->second.generation
                        != client->second.descriptor.generation) {
                    throw_dom_exception(
                        info, "The service worker client is unavailable.",
                        "InvalidStateError");
                    return;
                }
                if (runtime->incoming.size()
                        >= maximum_service_worker_queue_messages
                    || runtime->incoming_bytes
                        >= maximum_service_worker_queue_bytes) {
                    throw_dom_exception(
                        info, "Service worker message queue capacity exhausted.",
                        "QuotaExceededError");
                    return;
                }
                remaining_bytes = maximum_service_worker_queue_bytes
                    - runtime->incoming_bytes;
            }
            v8::Local<v8::Value> transfer;
            if (!service_worker_transfer(info, 4).ToLocal(&transfer)) return;
            clone_packet packet;
            if (!serialize_clone(
                    info, info[3], transfer, packet, remaining_bytes)) return;
            const auto bytes = clone_packet_size(packet);
            {
                std::lock_guard lock(runtime->mutex);
                if (runtime->stopped
                    || runtime->incoming.size()
                        >= maximum_service_worker_queue_messages
                    || bytes > maximum_service_worker_queue_bytes
                        - runtime->incoming_bytes) {
                    throw_dom_exception(
                        info, "Service worker message queue capacity exhausted.",
                        "QuotaExceededError");
                    return;
                }
                runtime->incoming_bytes += bytes;
                runtime->incoming_high_water = std::max(
                    runtime->incoming_high_water, runtime->incoming.size() + 1U);
                runtime->incoming.push_back(service_worker_message{
                    client_id,
                    client->second.descriptor.generation,
                    std::move(packet)});
            }
            runtime->ready.notify_one();
            return;
        }
        if (operation == "register") {
            if (info.Length() < 2) {
                isolate->ThrowException(v8::Exception::TypeError(
                    js_string(isolate, "ServiceWorkerContainer.register requires a script URL")));
                return;
            }
            const auto script_url = resolve_resource_url(
                to_utf8(isolate, info[1]), client_url);
            if (service_worker_origin(script_url) != client_origin) {
                throw_dom_exception(
                    info,
                    "Service worker scripts must be same origin.",
                    "SecurityError");
                return;
            }
            const auto worker_type = info.Length() > 2
                ? to_utf8(isolate, info[2]) : std::string{"classic"};
            if (worker_type != "classic" && worker_type != "module") {
                isolate->ThrowException(v8::Exception::TypeError(
                    js_string(isolate, "Invalid ServiceWorker type")));
                return;
            }
            const auto update_via_cache = info.Length() > 3
                ? to_utf8(isolate, info[3]) : std::string{"imports"};
            if (update_via_cache != "imports" && update_via_cache != "all"
                && update_via_cache != "none") {
                isolate->ThrowException(v8::Exception::TypeError(
                    js_string(isolate, "Invalid ServiceWorker updateViaCache value")));
                return;
            }
            const auto maximum_scope = service_worker_default_scope(script_url);
            auto scope = info.Length() > 4 && !info[4]->IsUndefined()
                ? resolve_resource_url(to_utf8(isolate, info[4]), client_url)
                : maximum_scope;
            if (const auto suffix = scope.find_first_of("?#");
                suffix != std::string::npos) {
                scope.resize(suffix);
            }
            if (service_worker_origin(scope) != client_origin
                || !scope.starts_with(maximum_scope)) {
                throw_dom_exception(
                    info,
                    "The requested service worker scope exceeds the script directory.",
                    "SecurityError");
                return;
            }

            std::string source;
            std::string resolved;
            const resource_request_context request_context{
                WEBSCENE_RESOURCE_INITIATOR_SUBRESOURCE,
                client_origin,
                client_url,
                WEBSCENE_FETCH_MODE_SAME_ORIGIN,
                WEBSCENE_REQUEST_DESTINATION_SCRIPT,
                "GET", {}, {}, WEBSCENE_FETCH_CREDENTIALS_SAME_ORIGIN, {}};
            if (!self->load_text_resource(
                    script_url,
                    client_url,
                    WEBSCENE_RESOURCE_SCRIPT,
                    source,
                    resolved,
                    nullptr,
                    request_context)
                || service_worker_origin(resolved) != client_origin) {
                isolate->ThrowException(v8::Exception::TypeError(
                    js_string(isolate, "Failed to fetch the service worker script")));
                return;
            }
            const auto digest = to_hex(sha256(
                reinterpret_cast<const uint8_t*>(source.data()), source.size()));
            auto [known, inserted] = self->service_worker_registrations.try_emplace(
                scope,
                service_worker_registration_state{
                    scope, resolved, worker_type, update_via_cache, digest, 1U, {}});
            auto needs_start = inserted;
            if (!inserted) {
                auto& registration = known->second;
                if (registration.script_url != resolved
                    || registration.worker_type != worker_type
                    || registration.source_digest != digest) {
                    needs_start = true;
                    registration.script_url = resolved;
                    registration.worker_type = worker_type;
                    registration.source_digest = digest;
                    ++registration.generation;
                }
                registration.update_via_cache = update_via_cache;
            }
            if (needs_start || known->second.runtime == nullptr) {
                auto candidate = known->second;
                candidate.runtime = self->start_service_worker_runtime(
                    candidate,
                    source,
                    client_origin,
                    self->service_worker_clients_for_scope(candidate.scope));
                std::unique_lock lock(candidate.runtime->mutex);
                candidate.runtime->ready.wait_for(
                    lock,
                    std::chrono::seconds(1),
                    [&] {
                        return candidate.runtime->activated
                            || !candidate.runtime->error.empty();
                    });
                if (!candidate.runtime->error.empty()) {
                    const auto error = candidate.runtime->error;
                    lock.unlock();
                    candidate.runtime->stop();
                    if (inserted) self->service_worker_registrations.erase(scope);
                    isolate->ThrowException(v8::Exception::TypeError(
                        js_dom_string(isolate, error)));
                    return;
                }
                if (!candidate.runtime->activated) {
                    lock.unlock();
                    candidate.runtime->stop();
                    if (inserted) self->service_worker_registrations.erase(scope);
                    throw_dom_exception(
                        info,
                        "Service worker activation exceeded one second.",
                        "TimeoutError");
                    return;
                }
                lock.unlock();
                auto previous = std::move(known->second.runtime);
                known->second.runtime = std::move(candidate.runtime);
                if (previous != nullptr) previous->stop();
            }
            snapshot(known->second);
            return;
        }

        if (operation == "get" || operation == "unregister"
            || operation == "update") {
            const auto value = info.Length() > 1 && !info[1]->IsUndefined()
                ? resolve_resource_url(to_utf8(isolate, info[1]), client_url)
                : client_url;
            service_worker_registration_state* matched = nullptr;
            for (auto& [scope, registration] : self->service_worker_registrations) {
                if (service_worker_origin(scope) != client_origin || !value.starts_with(scope)) {
                    continue;
                }
                if (matched == nullptr || scope.size() > matched->scope.size()) {
                    matched = &registration;
                }
            }
            if (operation == "get") {
                if (matched == nullptr) {
                    info.GetReturnValue().Set(v8::Null(isolate));
                } else {
                    snapshot(*matched);
                }
                return;
            }
            if (matched == nullptr) {
                if (operation == "unregister") {
                    info.GetReturnValue().Set(v8::False(isolate));
                } else {
                    throw_dom_exception(
                        info, "The service worker registration is unavailable.",
                        "InvalidStateError");
                }
                return;
            }
            if (operation == "unregister") {
                const auto scope = matched->scope;
                self->service_worker_registrations.erase(scope);
                info.GetReturnValue().Set(v8::True(isolate));
                return;
            }

            std::string source;
            std::string resolved;
            const resource_request_context request_context{
                WEBSCENE_RESOURCE_INITIATOR_SUBRESOURCE,
                client_origin,
                client_url,
                WEBSCENE_FETCH_MODE_SAME_ORIGIN,
                WEBSCENE_REQUEST_DESTINATION_SCRIPT,
                "GET", {}, {}, WEBSCENE_FETCH_CREDENTIALS_SAME_ORIGIN, {}};
            if (!self->load_text_resource(
                    matched->script_url,
                    client_url,
                    WEBSCENE_RESOURCE_SCRIPT,
                    source,
                    resolved,
                    nullptr,
                    request_context)) {
                isolate->ThrowException(v8::Exception::TypeError(
                    js_string(isolate, "Failed to update the service worker script")));
                return;
            }
            const auto digest = to_hex(sha256(
                reinterpret_cast<const uint8_t*>(source.data()), source.size()));
            if (digest != matched->source_digest) {
                auto candidate = *matched;
                candidate.source_digest = digest;
                ++candidate.generation;
                candidate.runtime = self->start_service_worker_runtime(
                    candidate,
                    source,
                    client_origin,
                    self->service_worker_clients_for_scope(candidate.scope));
                std::unique_lock lock(candidate.runtime->mutex);
                candidate.runtime->ready.wait_for(
                    lock,
                    std::chrono::seconds(1),
                    [&] {
                        return candidate.runtime->activated
                            || !candidate.runtime->error.empty();
                    });
                if (!candidate.runtime->error.empty()) {
                    const auto error = candidate.runtime->error;
                    lock.unlock();
                    candidate.runtime->stop();
                    isolate->ThrowException(v8::Exception::TypeError(
                        js_dom_string(isolate, error)));
                    return;
                }
                if (!candidate.runtime->activated) {
                    lock.unlock();
                    candidate.runtime->stop();
                    throw_dom_exception(
                        info,
                        "Service worker activation exceeded one second.",
                        "TimeoutError");
                    return;
                }
                lock.unlock();
                auto previous = std::move(matched->runtime);
                matched->source_digest = std::move(candidate.source_digest);
                matched->generation = candidate.generation;
                matched->runtime = std::move(candidate.runtime);
                if (previous != nullptr) previous->stop();
            }
            snapshot(*matched);
            return;
        }

        if (operation == "list") {
            std::vector<const service_worker_registration_state*> registrations;
            for (const auto& [scope, registration] : self->service_worker_registrations) {
                if (service_worker_origin(scope) == client_origin) {
                    registrations.push_back(&registration);
                }
            }
            std::ranges::sort(registrations, {}, [](const auto* value) {
                return value->scope;
            });
            auto result = v8::Array::New(isolate, static_cast<int>(registrations.size()));
            for (uint32_t index = 0U; index < registrations.size(); ++index) {
                result->Set(
                    local_context,
                    index,
                    self->service_worker_registration_snapshot(
                        local_context, *registrations[index])).Check();
            }
            info.GetReturnValue().Set(result);
            return;
        }

        isolate->ThrowException(v8::Exception::TypeError(
            js_string(isolate, "Unknown service worker control operation")));
    }

    bool has_service_worker_messages() const
    {
        for (const auto& [scope, registration] : service_worker_registrations) {
            static_cast<void>(scope);
            if (registration.runtime == nullptr) continue;
            std::lock_guard lock(registration.runtime->mutex);
            if (!registration.runtime->outgoing.empty()) return true;
        }
        return false;
    }

    bool drain_service_worker_message()
    {
        std::optional<service_worker_message> message;
        for (auto& [scope, registration] : service_worker_registrations) {
            static_cast<void>(scope);
            if (registration.runtime == nullptr) continue;
            std::lock_guard lock(registration.runtime->mutex);
            if (registration.runtime->outgoing.empty()) continue;
            message = std::move(registration.runtime->outgoing.front());
            registration.runtime->outgoing.pop_front();
            const auto bytes = clone_packet_size(message->packet);
            registration.runtime->outgoing_bytes =
                bytes > registration.runtime->outgoing_bytes
                    ? 0U : registration.runtime->outgoing_bytes - bytes;
            break;
        }
        if (!message.has_value()) return true;
        const auto client = service_worker_clients.find(message->client_id);
        if (client == service_worker_clients.end()
            || client->second.descriptor.generation
                != message->client_generation
            || client->second.context.IsEmpty()
            || client->second.container.IsEmpty()) {
            return true;
        }
        auto realm = client->second.context.Get(isolate);
        auto container = client->second.container.Get(isolate);
        if (realm.IsEmpty() || container.IsEmpty()) return true;
        v8::Context::Scope scope(realm);
        v8::TryCatch caught(isolate);
        std::vector<v8::Local<v8::Object>> ports;
        v8::Local<v8::Value> data;
        const auto decoded = deserialize_clone(
            isolate, realm, message->packet, &ports).ToLocal(&data);
        if (!decoded) caught.Reset();
        auto event = v8::Object::New(isolate);
        event->Set(
            realm,
            js_string(isolate, "type"),
            js_string(isolate, decoded ? "message" : "messageerror")).Check();
        if (decoded) event->Set(realm, js_string(isolate, "data"), data).Check();
        auto port_array = v8::Array::New(isolate, static_cast<int>(ports.size()));
        for (uint32_t index = 0U; index < ports.size(); ++index) {
            port_array->Set(realm, index, ports[index]).Check();
        }
        event->Set(realm, js_string(isolate, "ports"), port_array).Check();
        v8::Local<v8::Value> source;
        if (container->Get(
                realm, js_string(isolate, "controller")).ToLocal(&source)) {
            event->Set(realm, js_string(isolate, "source"), source).Check();
        }
        v8::Local<v8::Value> dispatch;
        if (!container->Get(
                realm, js_string(isolate, "dispatchEvent")).ToLocal(&dispatch)
            || !dispatch->IsFunction()) return true;
        v8::Local<v8::Value> argument[]{event};
        if (dispatch.As<v8::Function>()->Call(
                realm, container, 1, argument).IsEmpty()) {
            last_error = describe_reported_exception(caught, realm);
            return false;
        }
        perform_microtask_checkpoint();
        return true;
    }

    void retire_service_worker_clients(
        const std::vector<v8::Local<v8::Context>>& retired_contexts)
    {
        std::vector<std::string> retired_ids;
        std::erase_if(service_worker_clients, [&](auto& entry) {
            if (entry.second.context.IsEmpty()) return true;
            const auto realm = entry.second.context.Get(isolate);
            if (std::find(retired_contexts.begin(), retired_contexts.end(), realm)
                == retired_contexts.end()) return false;
            retired_ids.push_back(entry.first);
            entry.second.context.Reset();
            entry.second.container.Reset();
            return true;
        });
        if (retired_ids.empty()) return;
        for (auto& [scope, registration] : service_worker_registrations) {
            static_cast<void>(scope);
            if (registration.runtime == nullptr) continue;
            std::lock_guard lock(registration.runtime->mutex);
            for (const auto& id : retired_ids) {
                registration.runtime->clients.erase(id);
            }
            std::erase_if(
                registration.runtime->incoming,
                [&](const auto& message) {
                    return std::ranges::find(retired_ids, message.client_id)
                        != retired_ids.end();
                });
            std::erase_if(
                registration.runtime->outgoing,
                [&](const auto& message) {
                    return std::ranges::find(retired_ids, message.client_id)
                        != retired_ids.end();
                });
            registration.runtime->incoming_bytes = 0U;
            for (const auto& message : registration.runtime->incoming) {
                registration.runtime->incoming_bytes += clone_packet_size(message.packet);
            }
            registration.runtime->outgoing_bytes = 0U;
            for (const auto& message : registration.runtime->outgoing) {
                registration.runtime->outgoing_bytes += clone_packet_size(message.packet);
            }
        }
    }

    void install_service_worker_control(
        v8::Local<v8::Context> local_context,
        v8::Local<v8::Object> global)
    {
        if (worker_owner != nullptr || service_worker_realm) return;
        if (in_frame_context()) {
            auto* root = static_cast<dom_node*>(
                local_context->GetAlignedPointerFromEmbedderData(
                    1,
                    v8::kEmbedderDataTypeTagDefault));
            for (auto* ancestor = root; ancestor != nullptr; ancestor = ancestor->parent) {
                if (ancestor->tag == "iframe"
                    && !iframe_sandbox_allows(*ancestor, "allow-same-origin")) {
                    return;
                }
            }
        }
        global->Set(
            local_context,
            js_string(isolate, "__webSceneServiceWorkerControl"),
            v8::Function::New(local_context, service_worker_control).ToLocalChecked()).Check();
        constexpr std::string_view source = R"JS(
          (() => {
            const bridge = globalThis.__webSceneServiceWorkerControl;
            const client = bridge('client');
            const registrations = new Map();
            const registrationState = new WeakMap();
            const workerState = new WeakMap();
            let controller = null;
            let resolveReady;
            const ready = new Promise(resolve => { resolveReady = resolve; });
            const illegal = name => function() {
              throw new TypeError(`Illegal constructor: ${name}`);
            };
            const ServiceWorker = illegal('ServiceWorker');
            const ServiceWorkerRegistration = illegal('ServiceWorkerRegistration');
            const ServiceWorkerContainer = illegal('ServiceWorkerContainer');
            const Navigator = illegal('Navigator');
            Object.setPrototypeOf(ServiceWorker.prototype, EventTarget.prototype);
            Object.setPrototypeOf(ServiceWorkerRegistration.prototype, EventTarget.prototype);
            Object.setPrototypeOf(ServiceWorkerContainer.prototype, EventTarget.prototype);
            for (const [prototype, name] of [
              [ServiceWorker.prototype, 'ServiceWorker'],
              [ServiceWorkerRegistration.prototype, 'ServiceWorkerRegistration'],
              [ServiceWorkerContainer.prototype, 'ServiceWorkerContainer'],
              [Navigator.prototype, 'Navigator']
            ]) Object.defineProperty(prototype, Symbol.toStringTag,
              {value:name, configurable:true});
            const eventTarget = prototype => {
              const value = new EventTarget();
              Object.setPrototypeOf(value, prototype);
              return value;
            };
            const worker = snapshot => {
              const value = eventTarget(ServiceWorker.prototype);
              workerState.set(value, snapshot);
              return value;
            };
            Object.defineProperties(ServiceWorker.prototype, {
              scriptURL: {get() { return workerState.get(this).scriptURL; }},
              state: {get() { return workerState.get(this).state; }},
              postMessage: {value(message, options = []) {
                const state = workerState.get(this);
                bridge('post', state.scope, client.id, message, options);
              }, writable:true, configurable:true}
            });
            const hydrate = (snapshot, notifyControllerChange = true) => {
              let value = registrations.get(snapshot.scope);
              if (!value) {
                value = eventTarget(ServiceWorkerRegistration.prototype);
                registrations.set(snapshot.scope, value);
              }
              const prior = registrationState.get(value);
              const nextWorker = prior?.generation === snapshot.generation
                ? prior.installing || prior.active
                : worker(snapshot);
              registrationState.set(value, {
                ...snapshot,
                installing: snapshot.state === 'activated' ? null : nextWorker,
                active: snapshot.state === 'activated' ? nextWorker : null
              });
              const next = registrationState.get(value);
              if (next.active) resolveReady(value);
              if (next.active && snapshot.claimed
                  && client.url.startsWith(snapshot.scope)
                  && controller !== next.active) {
                controller = next.active;
                if (notifyControllerChange) {
                  Promise.resolve().then(() => container.dispatchEvent(
                    new Event('controllerchange')));
                }
              }
              return value;
            };
            Object.defineProperties(ServiceWorkerRegistration.prototype, {
              scope: {get() { return registrationState.get(this).scope; }},
              updateViaCache: {get() { return registrationState.get(this).updateViaCache; }},
              installing: {get() { return registrationState.get(this).installing; }},
              waiting: {get() { return null; }},
              active: {get() { return registrationState.get(this).active; }},
              update: {value() {
                const state = registrationState.get(this);
                return Promise.resolve().then(() => hydrate(bridge('update', state.scope)));
              }, writable:true, configurable:true},
              unregister: {value() {
                const state = registrationState.get(this);
                return Promise.resolve().then(() => {
                  const result = bridge('unregister', state.scope);
                  if (result) registrations.delete(state.scope);
                  return result;
                });
              }, writable:true, configurable:true}
            });
            const container = eventTarget(ServiceWorkerContainer.prototype);
            bridge('bind', client.id, container);
            Object.defineProperties(ServiceWorkerContainer.prototype, {
              controller: {get() { return controller; }},
              ready: {get() { return ready; }},
              register: {value(scriptURL, options = {}) {
                const type = options.type === undefined ? 'classic' : String(options.type);
                const updateViaCache = options.updateViaCache === undefined
                  ? 'imports' : String(options.updateViaCache);
                const scope = options.scope === undefined ? undefined : String(options.scope);
                return Promise.resolve().then(() => hydrate(bridge(
                  'register', String(scriptURL), type, updateViaCache, scope)));
              }, writable:true, configurable:true},
              getRegistration: {value(clientURL = location.href) {
                return Promise.resolve().then(() => {
                  const snapshot = bridge('get', String(clientURL));
                  return snapshot === null ? undefined : hydrate(snapshot);
                });
              }, writable:true, configurable:true},
              getRegistrations: {value() {
                return Promise.resolve().then(() => bridge('list').map(hydrate));
              }, writable:true, configurable:true},
              startMessages: {value() {}, writable:true, configurable:true}
            });
            Object.defineProperties(globalThis, {
              ServiceWorker: {value:ServiceWorker, writable:true, configurable:true},
              ServiceWorkerRegistration: {
                value:ServiceWorkerRegistration, writable:true, configurable:true
              },
              ServiceWorkerContainer: {
                value:ServiceWorkerContainer, writable:true, configurable:true
              },
              Navigator: {
                value:Navigator, writable:true, configurable:true
              }
            });
            Object.setPrototypeOf(navigator, Navigator.prototype);
            Object.defineProperty(Navigator.prototype, 'serviceWorker', {
              get() { return container; }, enumerable:true, configurable:true
            });
            const initialController = bridge('controller', client.id);
            if (initialController !== null) hydrate(initialController, false);
            delete globalThis.__webSceneServiceWorkerControl;
          })();
        )JS";
        auto script = v8::Script::Compile(
            local_context,
            js_dom_string(isolate, source)).ToLocalChecked();
        script->Run(local_context).ToLocalChecked();
    }
    struct service_worker_runtime_state {
        std::mutex mutex;
        std::condition_variable ready;
        std::thread thread;
        v8::Isolate* running_isolate{};
        std::unordered_map<std::string, service_worker_client_descriptor> clients;
        std::deque<service_worker_message> incoming;
        std::deque<service_worker_message> outgoing;
        size_t incoming_bytes{};
        size_t outgoing_bytes{};
        size_t incoming_high_water{};
        size_t outgoing_high_water{};
        std::function<void()> notify_client;
        bool stopped{false};
        bool activated{false};
        bool claimed{false};
        std::string error;

        void stop()
        {
            {
                std::lock_guard lock(mutex);
                stopped = true;
                if (running_isolate != nullptr) running_isolate->TerminateExecution();
                clients.clear();
                incoming.clear();
                outgoing.clear();
                incoming_bytes = 0U;
                outgoing_bytes = 0U;
            }
            ready.notify_all();
            if (thread.joinable()) thread.join();
        }

        ~service_worker_runtime_state() { stop(); }
    };
