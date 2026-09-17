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
        if (const auto colon = authority.find(':'); colon != std::string_view::npos) {
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
        set_service_worker_string(
            isolate, local_context, result, "state", "installing");
        result->Set(
            local_context,
            js_string(isolate, "generation"),
            v8::Number::New(isolate, static_cast<double>(registration.generation))).Check();
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
                    scope, resolved, worker_type, update_via_cache, digest, 1U});
            if (!inserted) {
                auto& registration = known->second;
                if (registration.script_url != resolved
                    || registration.worker_type != worker_type
                    || registration.source_digest != digest) {
                    registration.script_url = resolved;
                    registration.worker_type = worker_type;
                    registration.source_digest = digest;
                    ++registration.generation;
                }
                registration.update_via_cache = update_via_cache;
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
                matched->source_digest = digest;
                ++matched->generation;
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
        if (worker_owner != nullptr) return;
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
              registrationState.set(value, {
                ...snapshot,
                installing: prior?.generation === snapshot.generation
                  ? prior.installing : worker(snapshot)
              });
              return value;
            };
            Object.defineProperties(ServiceWorkerRegistration.prototype, {
              scope: {get() { return registrationState.get(this).scope; }},
              updateViaCache: {get() { return registrationState.get(this).updateViaCache; }},
              installing: {get() { return registrationState.get(this).installing; }},
              waiting: {get() { return null; }},
              active: {get() { return null; }},
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
            const ready = new Promise(() => {});
            Object.defineProperties(ServiceWorkerContainer.prototype, {
              controller: {get() { return null; }},
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
