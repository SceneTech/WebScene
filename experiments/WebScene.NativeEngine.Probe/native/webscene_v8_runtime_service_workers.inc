    static std::string service_worker_origin(std::string_view address)
    {
        if (address.starts_with("file://")) return "file://";
        return resource_origin(std::string(address));
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
        std::string origin)
    {
        auto state = std::make_shared<service_worker_runtime_state>();
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
                        js_string(child->isolate, "__webSceneServiceWorkerClaim"),
                        v8::Function::New(
                            realm,
                            [](const v8::FunctionCallbackInfo<v8::Value>& info) {
                                auto* runtime = static_cast<service_worker_runtime_state*>(
                                    info.Data().As<v8::External>()->Value(
                                        v8::kExternalPointerTypeTagDefault));
                                std::lock_guard lock(runtime->mutex);
                                if (!runtime->stopped) runtime->claimed = true;
                            },
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
                        const claim = globalThis.__webSceneServiceWorkerClaim;
                        delete globalThis.__webSceneServiceWorkerClaim;
                        const clients = Object.freeze({
                          get() { return Promise.resolve(undefined); },
                          matchAll() { return Promise.resolve([]); },
                          claim() { claim(); return Promise.resolve(); }
                        });
                        Object.defineProperties(globalThis, {
                          clients: {value:clients, enumerable:true},
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
                    {
                        std::unique_lock lock(state->mutex);
                        state->ready.wait_for(lock, std::chrono::milliseconds(100), [&] {
                            return state->stopped;
                        });
                        if (state->stopped) break;
                    }
                    v8::Isolate::Scope isolate_scope(child->isolate);
                    v8::HandleScope handles(child->isolate);
                    auto realm = child->context.Get(child->isolate);
                    v8::Context::Scope context_scope(realm);
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
                    candidate, source, client_origin);
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
                    candidate, source, client_origin);
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
              postMessage: {value() {
                throw new DOMException('The service worker is not active.', 'InvalidStateError');
              }, writable:true, configurable:true}
            });
            const hydrate = snapshot => {
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
                  && String(location.href).startsWith(snapshot.scope)
                  && controller !== next.active) {
                controller = next.active;
                Promise.resolve().then(() => container.dispatchEvent(
                  new Event('controllerchange')));
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
            }
            ready.notify_all();
            if (thread.joinable()) thread.join();
        }

        ~service_worker_runtime_state() { stop(); }
    };
