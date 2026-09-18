    static std::string service_worker_origin(std::string_view address)
    {
        if (address.starts_with("file://")) return "file://";
        return resource_origin(std::string(address));
    }

    static constexpr size_t maximum_service_worker_queue_messages = 256U;
    static constexpr size_t maximum_service_worker_queue_bytes =
        16U * 1024U * 1024U;
    static constexpr size_t maximum_service_worker_fetches = 32U;
    static constexpr size_t maximum_service_worker_fetch_body_bytes =
        64U * 1024U * 1024U;
    static constexpr size_t maximum_service_worker_caches = 32U;
    static constexpr size_t maximum_service_worker_cache_entries = 256U;

    struct service_worker_cache_entry final {
        uint32_t status{200U};
        std::string status_text{"OK"};
        std::vector<std::pair<std::string, std::string>> headers;
        std::string body;
        std::string url;
        std::string response_type{"basic"};
        bool redirected{false};
    };

    struct service_worker_cache final {
        std::unordered_map<std::string, service_worker_cache_entry> entries;
        std::vector<std::string> order;
    };

    struct service_worker_cache_storage final {
        std::mutex mutex;
        std::unordered_map<std::string, service_worker_cache> caches;
        std::vector<std::string> order;
        size_t bytes{};
    };

    struct service_worker_body_lease final {
        service_worker_body_lease(
            std::shared_ptr<std::atomic_size_t> value,
            size_t byte_count)
            : queued_bytes(std::move(value)), bytes(byte_count)
        {
        }

        ~service_worker_body_lease()
        {
            if (queued_bytes != nullptr && bytes != 0U) {
                queued_bytes->fetch_sub(bytes, std::memory_order_acq_rel);
            }
        }

        std::shared_ptr<std::atomic_size_t> queued_bytes;
        size_t bytes{};
    };

    struct service_worker_fetch_request final {
        std::string url;
        std::string method{"GET"};
        uint32_t destination{WEBSCENE_REQUEST_DESTINATION_NONE};
        uint32_t mode{WEBSCENE_FETCH_MODE_NONE};
        uint32_t credentials{WEBSCENE_FETCH_CREDENTIALS_SAME_ORIGIN};
        uint32_t redirect{};
        std::vector<std::pair<std::string, std::string>> headers;
        std::string body;
        std::string client_id;
        uint64_t client_generation{};
        std::promise<service_worker_fetch_result> promise;
        std::function<void()> notify_client;
        std::function<void()> notify_worker;
        std::atomic_bool cancelled{false};
        std::atomic_bool settled{false};
        std::atomic_bool active_slot{false};

        bool settle(service_worker_fetch_result result)
        {
            if (settled.exchange(true, std::memory_order_acq_rel)) return false;
            promise.set_value(std::move(result));
            if (notify_client) notify_client();
            return true;
        }

        void cancel()
        {
            cancelled.store(true, std::memory_order_release);
            service_worker_fetch_result result;
            result.handled = true;
            result.error = "Service worker fetch was cancelled";
            settle(std::move(result));
            if (notify_worker) notify_worker();
        }
    };

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
        const auto cache_name = [&]() {
            return info.Length() > 1 ? to_utf8(isolate, info[1]) : std::string{};
        };
        const auto cache_key = [&]() {
            return info.Length() > 2 ? to_utf8(isolate, info[2]) : std::string{};
        };
        const auto validate_cache_identity = [&](const std::string& name,
                                                  const std::string* key = nullptr) {
            if (name.size() > 256U
                || (key != nullptr && (key->empty() || key->size() > 8192U))) {
                isolate->ThrowException(v8::Exception::TypeError(
                    js_string(isolate, "Invalid CacheStorage key")));
                return false;
            }
            return true;
        };
        if (operation == "cache-open") {
            const auto name = cache_name();
            if (!validate_cache_identity(name)) return;
            std::lock_guard lock(runtime->cache_storage->mutex);
            if (!runtime->cache_storage->caches.contains(name)) {
                if (runtime->cache_storage->caches.size()
                    >= maximum_service_worker_caches) {
                    throw_dom_exception(
                        info, "CacheStorage quota exceeded", "QuotaExceededError");
                    return;
                }
                runtime->cache_storage->caches.emplace(name, service_worker_cache{});
                runtime->cache_storage->order.push_back(name);
            }
            info.GetReturnValue().Set(v8::True(isolate));
            return;
        }
        if (operation == "cache-storage-has") {
            const auto name = cache_name();
            if (!validate_cache_identity(name)) return;
            std::lock_guard lock(runtime->cache_storage->mutex);
            info.GetReturnValue().Set(v8::Boolean::New(
                isolate, runtime->cache_storage->caches.contains(name)));
            return;
        }
        if (operation == "cache-storage-delete") {
            const auto name = cache_name();
            if (!validate_cache_identity(name)) return;
            std::lock_guard lock(runtime->cache_storage->mutex);
            const auto found = runtime->cache_storage->caches.find(name);
            if (found == runtime->cache_storage->caches.end()) {
                info.GetReturnValue().Set(v8::False(isolate));
                return;
            }
            for (const auto& [key, entry] : found->second.entries) {
                static_cast<void>(key);
                runtime->cache_storage->bytes -= entry.body.size();
            }
            runtime->cache_storage->caches.erase(found);
            std::erase(runtime->cache_storage->order, name);
            info.GetReturnValue().Set(v8::True(isolate));
            return;
        }
        if (operation == "cache-storage-keys") {
            std::lock_guard lock(runtime->cache_storage->mutex);
            auto result = v8::Array::New(
                isolate, static_cast<int>(runtime->cache_storage->order.size()));
            for (uint32_t index = 0U;
                 index < runtime->cache_storage->order.size(); ++index) {
                result->Set(context, index, js_dom_string(
                    isolate, runtime->cache_storage->order[index])).Check();
            }
            info.GetReturnValue().Set(result);
            return;
        }
        if (operation == "cache-match") {
            const auto name = cache_name();
            const auto key = cache_key();
            if (!validate_cache_identity(name, &key)) return;
            std::lock_guard lock(runtime->cache_storage->mutex);
            const auto cache = runtime->cache_storage->caches.find(name);
            if (cache == runtime->cache_storage->caches.end()) {
                info.GetReturnValue().Set(v8::Undefined(isolate));
                return;
            }
            const auto found = cache->second.entries.find(key);
            if (found == cache->second.entries.end()) {
                info.GetReturnValue().Set(v8::Undefined(isolate));
                return;
            }
            const auto& entry = found->second;
            auto result = v8::Object::New(isolate);
            result->Set(context, js_string(isolate, "status"),
                v8::Integer::NewFromUnsigned(isolate, entry.status)).Check();
            result->Set(context, js_string(isolate, "statusText"),
                js_dom_string(isolate, entry.status_text)).Check();
            result->Set(context, js_string(isolate, "url"),
                js_dom_string(isolate, entry.url)).Check();
            result->Set(context, js_string(isolate, "type"),
                js_dom_string(isolate, entry.response_type)).Check();
            result->Set(context, js_string(isolate, "redirected"),
                v8::Boolean::New(isolate, entry.redirected)).Check();
            auto headers = v8::Array::New(
                isolate, static_cast<int>(entry.headers.size()));
            for (uint32_t index = 0U; index < entry.headers.size(); ++index) {
                auto pair = v8::Array::New(isolate, 2);
                pair->Set(context, 0,
                    js_dom_string(isolate, entry.headers[index].first)).Check();
                pair->Set(context, 1,
                    js_dom_string(isolate, entry.headers[index].second)).Check();
                headers->Set(context, index, pair).Check();
            }
            result->Set(context, js_string(isolate, "headers"), headers).Check();
            auto body = v8::ArrayBuffer::New(isolate, entry.body.size());
            if (!entry.body.empty()) {
                std::memcpy(body->GetBackingStore()->Data(),
                    entry.body.data(), entry.body.size());
            }
            result->Set(context, js_string(isolate, "body"), body).Check();
            info.GetReturnValue().Set(result);
            return;
        }
        if (operation == "cache-delete-entry") {
            const auto name = cache_name();
            const auto key = cache_key();
            if (!validate_cache_identity(name, &key)) return;
            std::lock_guard lock(runtime->cache_storage->mutex);
            const auto cache = runtime->cache_storage->caches.find(name);
            if (cache == runtime->cache_storage->caches.end()) {
                info.GetReturnValue().Set(v8::False(isolate));
                return;
            }
            const auto found = cache->second.entries.find(key);
            if (found == cache->second.entries.end()) {
                info.GetReturnValue().Set(v8::False(isolate));
                return;
            }
            runtime->cache_storage->bytes -= found->second.body.size();
            cache->second.entries.erase(found);
            std::erase(cache->second.order, key);
            info.GetReturnValue().Set(v8::True(isolate));
            return;
        }
        if (operation == "cache-entry-keys") {
            const auto name = cache_name();
            if (!validate_cache_identity(name)) return;
            std::lock_guard lock(runtime->cache_storage->mutex);
            const auto cache = runtime->cache_storage->caches.find(name);
            const auto count = cache == runtime->cache_storage->caches.end()
                ? 0U : static_cast<uint32_t>(cache->second.order.size());
            auto result = v8::Array::New(isolate, static_cast<int>(count));
            if (cache != runtime->cache_storage->caches.end()) {
                for (uint32_t index = 0U; index < count; ++index) {
                    result->Set(context, index, js_dom_string(
                        isolate, cache->second.order[index])).Check();
                }
            }
            info.GetReturnValue().Set(result);
            return;
        }
        if (operation == "cache-put") {
            const auto name = cache_name();
            const auto key = cache_key();
            if (!validate_cache_identity(name, &key) || info.Length() < 4
                || !info[3]->IsObject()) return;
            auto value = info[3].As<v8::Object>();
            const auto property = [&](const char* property_name) {
                v8::Local<v8::Value> result;
                if (!value->Get(context, js_string(isolate, property_name)).ToLocal(&result)) {
                    return v8::Local<v8::Value>{};
                }
                return result;
            };
            service_worker_cache_entry entry;
            const auto status = property("status");
            const auto status_text = property("statusText");
            const auto url = property("url");
            const auto response_type = property("type");
            const auto redirected = property("redirected");
            const auto headers_value = property("headers");
            const auto body_value = property("body");
            if (status.IsEmpty() || status_text.IsEmpty() || url.IsEmpty()
                || response_type.IsEmpty() || redirected.IsEmpty()
                || headers_value.IsEmpty() || body_value.IsEmpty()
                || !headers_value->IsArray() || !body_value->IsArrayBufferView()) {
                isolate->ThrowException(v8::Exception::TypeError(
                    js_string(isolate, "Invalid cache response record")));
                return;
            }
            entry.status = status->Uint32Value(context).FromMaybe(0U);
            entry.status_text = to_utf8(isolate, status_text);
            entry.url = to_utf8(isolate, url);
            entry.response_type = to_utf8(isolate, response_type);
            entry.redirected = redirected->BooleanValue(isolate);
            if ((entry.status != 0U
                    && (entry.status < 200U || entry.status > 599U))
                || entry.status_text.size() > 1024U || entry.url.size() > 8192U
                || entry.response_type.size() > 64U) {
                isolate->ThrowException(v8::Exception::TypeError(
                    js_string(isolate, "Invalid cache response metadata")));
                return;
            }
            auto headers = headers_value.As<v8::Array>();
            size_t header_bytes = 0U;
            if (headers->Length() > 64U) {
                isolate->ThrowException(v8::Exception::TypeError(
                    js_string(isolate, "Cache response has too many headers")));
                return;
            }
            for (uint32_t index = 0U; index < headers->Length(); ++index) {
                v8::Local<v8::Value> pair_value;
                if (!headers->Get(context, index).ToLocal(&pair_value)
                    || !pair_value->IsArray()) {
                    isolate->ThrowException(v8::Exception::TypeError(
                        js_string(isolate, "Invalid cache response headers")));
                    return;
                }
                auto pair = pair_value.As<v8::Array>();
                v8::Local<v8::Value> header_name;
                v8::Local<v8::Value> header_value;
                if (pair->Length() != 2U
                    || !pair->Get(context, 0).ToLocal(&header_name)
                    || !pair->Get(context, 1).ToLocal(&header_value)) return;
                auto header = std::pair{
                    to_utf8(isolate, header_name), to_utf8(isolate, header_value)};
                header_bytes += header.first.size() + header.second.size();
                if (header_bytes > 64U * 1024U) {
                    isolate->ThrowException(v8::Exception::TypeError(
                        js_string(isolate, "Cache response headers are too large")));
                    return;
                }
                entry.headers.push_back(std::move(header));
            }
            auto body = body_value.As<v8::ArrayBufferView>();
            if (body->ByteLength() > maximum_service_worker_fetch_body_bytes) {
                throw_dom_exception(
                    info, "CacheStorage quota exceeded", "QuotaExceededError");
                return;
            }
            const auto backing = body->Buffer()->GetBackingStore();
            if (body->ByteLength() != 0U) {
                const auto* bytes = static_cast<const char*>(backing->Data())
                    + body->ByteOffset();
                entry.body.assign(bytes, body->ByteLength());
            }
            std::lock_guard lock(runtime->cache_storage->mutex);
            const auto cache = runtime->cache_storage->caches.find(name);
            if (cache == runtime->cache_storage->caches.end()) {
                isolate->ThrowException(v8::Exception::TypeError(
                    js_string(isolate, "Cache is unavailable")));
                return;
            }
            const auto prior = cache->second.entries.find(key);
            const auto prior_bytes = prior == cache->second.entries.end()
                ? 0U : prior->second.body.size();
            if (runtime->cache_storage->bytes - prior_bytes
                    > maximum_service_worker_fetch_body_bytes - entry.body.size()
                || (prior == cache->second.entries.end()
                    && cache->second.entries.size()
                        >= maximum_service_worker_cache_entries)) {
                throw_dom_exception(
                    info, "CacheStorage quota exceeded", "QuotaExceededError");
                return;
            }
            runtime->cache_storage->bytes = runtime->cache_storage->bytes
                - prior_bytes + entry.body.size();
            if (prior == cache->second.entries.end()) cache->second.order.push_back(key);
            cache->second.entries.insert_or_assign(key, std::move(entry));
            info.GetReturnValue().Set(v8::True(isolate));
            return;
        }
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
        if (registration.runtime != nullptr
            && registration.runtime->cache_storage != nullptr) {
            state->cache_storage = registration.runtime->cache_storage;
        }
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
                    constexpr std::string_view bootstrap_parts[] = {
                        R"JS(
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
                        Object.defineProperty(ReadableStream.prototype, 'tee', {
                          enumerable:true, configurable:true, writable:true,
                          value:function() {
                            if (!(this instanceof ReadableStream)) {
                              throw new TypeError('Illegal invocation');
                            }
                            if (this.locked) throw new TypeError('ReadableStream is locked');
                            const reader = this.getReader();
                            const controllers = [undefined, undefined];
                            const cancelled = [false, false];
                            const reasons = [undefined, undefined];
                            let resolveCancellation;
                            const cancellation = new Promise(resolve => {
                              resolveCancellation = resolve;
                            });
                            let reading;
                            let finished = false;
                            const pump = () => {
                              if (finished) return Promise.resolve();
                              if (!reading) {
                                reading = reader.read().then(({value, done}) => {
                                  if (done) {
                                    finished = true;
                                    for (let index = 0; index < controllers.length; ++index) {
                                      if (!cancelled[index]) controllers[index].close();
                                    }
                                    if (cancelled[0] || cancelled[1]) resolveCancellation();
                                    return;
                                  }
                                  for (let index = 0; index < controllers.length; ++index) {
                                    if (!cancelled[index]) controllers[index].enqueue(value);
                                  }
                                }, error => {
                                  finished = true;
                                  for (let index = 0; index < controllers.length; ++index) {
                                    if (!cancelled[index]) controllers[index].error(error);
                                  }
                                  if (cancelled[0] || cancelled[1]) resolveCancellation();
                                }).finally(() => { reading = undefined; });
                              }
                              return reading;
                            };
                            const branch = index => new ReadableStream({
                              start(controller) { controllers[index] = controller; },
                              pull() { return pump(); },
                              cancel(reason) {
                                if (cancelled[index]) return cancellation;
                                cancelled[index] = true;
                                reasons[index] = reason;
                                if (cancelled[0] && cancelled[1] && !finished) {
                                  finished = true;
                                  resolveCancellation(reader.cancel([reasons[0], reasons[1]]));
                                }
                                return cancellation;
                              }
                            });
                            return [branch(0), branch(1)];
                          }
                        });
)JS",
                        R"JS(
                        const extendableEventState = new WeakMap();
                        class ExtendableEvent extends Event {
                          constructor(type, init = {}) {
                            super(type, init);
                            extendableEventState.set(this, {
                              dispatching:false, pending:[], response:undefined,
                              responded:false, handledResolve:undefined,
                              handled:undefined
                            });
                            const state = extendableEventState.get(this);
                            state.handled = new Promise(resolve => state.handledResolve = resolve);
                          }
                          waitUntil(value) {
                            const state = extendableEventState.get(this);
                            if (!state || !state.dispatching) {
                              throw new DOMException(
                                'The event is not being dispatched.', 'InvalidStateError');
                            }
                            state.pending.push(Promise.resolve(value));
                          }
                        }
                        Object.defineProperty(ExtendableEvent.prototype, Symbol.toStringTag,
                          {value:'ExtendableEvent', configurable:true});
                        const fetchEventState = new WeakMap();
                        class FetchEvent extends ExtendableEvent {
                          constructor(type, init) {
                            if (!init || !(init.request instanceof Request)) {
                              throw new TypeError('FetchEvent requires a Request');
                            }
                            super(type, init);
                            fetchEventState.set(this, {
                              request:init.request,
                              clientId:String(init.clientId ?? ''),
                              resultingClientId:String(init.resultingClientId ?? ''),
                              replacesClientId:String(init.replacesClientId ?? ''),
                              preloadResponse:Promise.resolve(init.preloadResponse),
                            });
                          }
                          get request() {
                            const state = fetchEventState.get(this);
                            if (!state) throw new TypeError('Illegal invocation');
                            return state.request;
                          }
                          get clientId() {
                            const state = fetchEventState.get(this);
                            if (!state) throw new TypeError('Illegal invocation');
                            return state.clientId;
                          }
                          get resultingClientId() {
                            const state = fetchEventState.get(this);
                            if (!state) throw new TypeError('Illegal invocation');
                            return state.resultingClientId;
                          }
                          get replacesClientId() {
                            const state = fetchEventState.get(this);
                            if (!state) throw new TypeError('Illegal invocation');
                            return state.replacesClientId;
                          }
                          get preloadResponse() {
                            const state = fetchEventState.get(this);
                            if (!state) throw new TypeError('Illegal invocation');
                            return state.preloadResponse;
                          }
                          get handled() {
                            const state = extendableEventState.get(this);
                            if (!state) throw new TypeError('Illegal invocation');
                            return state.handled;
                          }
                          respondWith(value) {
                            const state = extendableEventState.get(this);
                            if (!state || !state.dispatching || state.responded) {
                              throw new DOMException(
                                'respondWith() is unavailable.', 'InvalidStateError');
                            }
                            state.responded = true;
                            state.response = Promise.resolve(value).then(response => {
                              if (!(response instanceof Response)) {
                                throw new TypeError('FetchEvent response must be a Response');
                              }
                              return response;
                            });
                            state.response.then(
                              () => state.handledResolve(),
                              () => state.handledResolve());
                          }
                        }
                        Object.defineProperty(FetchEvent.prototype, Symbol.toStringTag,
                          {value:'FetchEvent', configurable:true});
                        for (const [prototype, names] of [
                          [ExtendableEvent.prototype, ['waitUntil']],
                          [FetchEvent.prototype, [
                            'request', 'clientId', 'resultingClientId',
                            'replacesClientId', 'preloadResponse', 'handled',
                            'respondWith']]
                        ]) {
                          for (const name of names) {
                            const descriptor = Object.getOwnPropertyDescriptor(prototype, name);
                            Object.defineProperty(
                              prototype, name, {...descriptor, enumerable:true});
                          }
                        }
                        const dispatchFetch = async init => {
                          const event = new FetchEvent('fetch', init);
                          const state = extendableEventState.get(event);
                          state.dispatching = true;
                          try { dispatchEvent(event); }
                          finally { state.dispatching = false; }
                          if (!state.responded) return undefined;
                          const response = await state.response;
                          await Promise.all(state.pending);
                          return response;
                        };
)JS",
                        R"JS(
                        const dispatchFetchRecord = async init => {
                          let response = await dispatchFetch(init);
                          if (response === undefined) return {handled:false};
                          let responseUrl = response.url;
                          let responseRedirected = response.redirected;
                          const redirectStatuses = new Set([301, 302, 303, 307, 308]);
                          if (redirectStatuses.has(response.status)) {
                            if (init.request.redirect === 'error') {
                              throw new TypeError('Service worker redirect is disallowed');
                            }
                            if (init.request.redirect === 'manual') {
                              if (response.body !== null && !response.body.locked) {
                                await response.body.cancel('Returning opaque redirect');
                              }
                              return {handled:true, status:0, statusText:'', headers:[],
                                body:new Uint8Array(), url:'', redirected:false,
                                type:'opaqueredirect'};
                            }
                            if (init.request.redirect === 'follow') {
                              let currentUrl = init.request.url;
                              let method = init.request.method;
                              const headers = new Headers(init.request.headers);
                              const visited = new Set([currentUrl]);
                              for (let redirects = 0; redirectStatuses.has(response.status);
                                   ++redirects) {
                                if (redirects >= 20) {
                                  throw new TypeError('Service worker redirect limit exceeded');
                                }
                                const location = response.headers.get('location');
                                if (location === null) break;
                                let target;
                                try { target = new URL(location, currentUrl); }
                                catch { throw new TypeError('Invalid service worker redirect'); }
                                if (target.protocol !== 'http:' && target.protocol !== 'https:') {
                                  throw new TypeError('Invalid service worker redirect protocol');
                                }
                                if (visited.has(target.href)) {
                                  throw new TypeError('Service worker redirect loop');
                                }
                                visited.add(target.href);
                                if (response.body !== null && !response.body.locked) {
                                  await response.body.cancel('Following service worker redirect');
                                }
                                if (response.status === 303
                                    || ((response.status === 301 || response.status === 302)
                                      && method === 'POST')) {
                                  method = 'GET';
                                  for (const name of ['content-encoding', 'content-language',
                                    'content-length', 'content-location', 'content-type']) {
                                    headers.delete(name);
                                  }
                                }
                                if (new URL(currentUrl).origin !== target.origin) {
                                  for (const name of ['authorization', 'proxy-authorization',
                                    'cookie', 'cookie2']) headers.delete(name);
                                }
                                currentUrl = target.href;
                                response = await fetch(currentUrl, {
                                  method, headers,
                                  credentials:init.request.credentials,
                                  mode:init.request.mode,
                                  redirect:'manual'
                                });
                                responseUrl = response.url || currentUrl;
                                responseRedirected = true;
                              }
                            }
                          }
                          const chunks = [];
                          let bodyLength = 0;
                          if (response.body !== null) {
                            const reader = response.body.getReader();
                            for (;;) {
                              const {value, done} = await reader.read();
                              if (done) break;
                              if (!(value instanceof Uint8Array)) {
                                await reader.cancel(
                                  'Service worker response body requires Uint8Array chunks');
                                throw new TypeError(
                                  'Service worker response body requires Uint8Array chunks');
                              }
                              const bytes = value;
                              bodyLength += bytes.byteLength;
                              if (bodyLength > 64 * 1024 * 1024) {
                                await reader.cancel('Service worker response body limit');
                                throw new DOMException(
                                  'Service worker response body exceeds 64 MiB',
                                  'QuotaExceededError');
                              }
                              chunks.push(bytes);
                            }
                          }
                          const body = new Uint8Array(bodyLength);
                          let bodyOffset = 0;
                          for (const chunk of chunks) {
                            body.set(chunk, bodyOffset);
                            bodyOffset += chunk.byteLength;
                          }
                          return {
                            handled:true,
                            status:response.status,
                            statusText:response.statusText,
                            headers:[...response.headers],
                            body,
                            url:responseUrl,
                            redirected:responseRedirected,
                            type:response.type
                          };
                        };
)JS",
                        R"JS(
                        const cacheState = new WeakMap();
                        const cacheStorageState = new WeakMap();
                        const cacheKey = request => {
                          const value = request instanceof Request
                            ? request : new Request(request);
                          return `${value.method} ${value.url}`;
                        };
                        const requireCache = value => {
                          const state = cacheState.get(value);
                          if (!state) throw new TypeError('Illegal invocation');
                          return state;
                        };
                        const requireCacheStorage = value => {
                          const state = cacheStorageState.get(value);
                          if (!state) throw new TypeError('Illegal invocation');
                          return state;
                        };
                        class Cache {
                          constructor() { throw new TypeError('Illegal constructor'); }
                          match(request) {
                            const state = requireCache(this);
                            const stored = control(
                              'cache-match', state.name, cacheKey(request));
                            return Promise.resolve(stored === undefined ? undefined
                              : new Response(stored.body, {
                                status:stored.status, statusText:stored.statusText,
                                headers:stored.headers, url:stored.url,
                                redirected:stored.redirected, type:stored.type
                              }));
                          }
                          async put(request, response) {
                            const state = requireCache(this);
                            const key = cacheKey(request);
                            if (!key.startsWith('GET ')) {
                              throw new TypeError('Cache.put requires a GET request');
                            }
                            if (!(response instanceof Response)) {
                              throw new TypeError('Cache.put requires a Response');
                            }
                            if (response.status === 206 || response.bodyUsed) {
                              throw new TypeError('Response cannot be cached');
                            }
                            const clone = response.clone();
                            const body = new Uint8Array(await clone.arrayBuffer());
                            control(
                              'cache-put', state.name, key, {
                                status:response.status, statusText:response.statusText,
                                headers:[...response.headers], body,
                                url:response.url, redirected:response.redirected,
                                type:response.type
                              });
                          }
                          delete(request) {
                            const state = requireCache(this);
                            return Promise.resolve(control(
                              'cache-delete-entry', state.name, cacheKey(request)));
                          }
                          keys() {
                            const keys = control(
                              'cache-entry-keys', requireCache(this).name);
                            return Promise.resolve(keys.map(key =>
                              new Request(key.slice(key.indexOf(' ') + 1), {
                                method:key.slice(0, key.indexOf(' '))})));
                          }
                        }
                        class CacheStorage {
                          constructor() { throw new TypeError('Illegal constructor'); }
                          open(name) {
                            const storage = requireCacheStorage(this);
                            const key = String(name);
                            control('cache-open', key);
                            let value = storage.wrappers.get(key);
                            if (!value) {
                              value = Object.create(Cache.prototype);
                              cacheState.set(value, {name:key});
                              storage.wrappers.set(key, value);
                            }
                            return Promise.resolve(value);
                          }
                          has(name) {
                            requireCacheStorage(this);
                            return Promise.resolve(control(
                              'cache-storage-has', String(name)));
                          }
                          delete(name) {
                            const key = String(name);
                            const storage = requireCacheStorage(this);
                            const deleted = control(
                              'cache-storage-delete', key);
                            if (deleted) storage.wrappers.delete(key);
                            return Promise.resolve(deleted);
                          }
                          keys() {
                            requireCacheStorage(this);
                            return Promise.resolve(control(
                              'cache-storage-keys'));
                          }
                          async match(request) {
                            requireCacheStorage(this);
                            for (const name of control(
                                'cache-storage-keys')) {
                              const stored = control(
                                'cache-match', name, cacheKey(request));
                              const response = stored === undefined ? undefined
                                : new Response(stored.body, {
                                  status:stored.status, statusText:stored.statusText,
                                  headers:stored.headers, url:stored.url,
                                  redirected:stored.redirected, type:stored.type
                                });
                              if (response) return response;
                            }
                            return undefined;
                          }
                        }
                        for (const [prototype, tag, names] of [
                          [Cache.prototype, 'Cache', ['match', 'put', 'delete', 'keys']],
                          [CacheStorage.prototype, 'CacheStorage',
                            ['open', 'has', 'delete', 'keys', 'match']]
                        ]) {
                          Object.defineProperty(prototype, Symbol.toStringTag,
                            {value:tag, configurable:true});
                          for (const name of names) {
                            const descriptor = Object.getOwnPropertyDescriptor(prototype, name);
                            Object.defineProperty(prototype, name,
                              {...descriptor, enumerable:true});
                          }
                        }
                        const caches = Object.create(CacheStorage.prototype);
                        cacheStorageState.set(caches, {wrappers:new Map()});
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
                          ExtendableEvent: {
                            value:ExtendableEvent, writable:true, configurable:true
                          },
                          FetchEvent: {
                            value:FetchEvent, writable:true, configurable:true
                          },
                          Cache: {value:Cache, writable:true, configurable:true},
                          CacheStorage: {
                            value:CacheStorage, writable:true, configurable:true
                          },
                          caches: {value:caches, enumerable:true, configurable:true},
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
                          },
                          __webSceneDispatchServiceWorkerFetch: {
                            value:dispatchFetch, configurable:true
                          },
                          __webSceneDispatchServiceWorkerFetchRecord: {
                            value:dispatchFetchRecord, configurable:true
                          }
                        });
                      })();
                    )JS",
                    };
                    std::string bootstrap;
                    for (const auto part : bootstrap_parts) bootstrap.append(part);
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
                struct pending_fetch final {
                    std::shared_ptr<service_worker_fetch_request> request;
                    v8::Global<v8::Promise> promise;
                };
                std::vector<pending_fetch> pending_fetches;
                const auto release_fetch_slot = [&](const auto& request) {
                    std::lock_guard lock(state->mutex);
                    if (request != nullptr
                        && request->active_slot.exchange(
                            false, std::memory_order_acq_rel)
                        && state->active_fetches > 0U) {
                        --state->active_fetches;
                    }
                };
                for (;;) {
                    std::optional<service_worker_message> message;
                    std::shared_ptr<service_worker_fetch_request> fetch_request;
                    {
                        std::unique_lock lock(state->mutex);
                        state->ready.wait_for(
                            lock,
                            pending_fetches.empty()
                                ? std::chrono::milliseconds(100)
                                : std::chrono::milliseconds(1),
                            [&] {
                            return state->stopped || !state->incoming.empty()
                                || !state->fetches.empty();
                        });
                        if (state->stopped) break;
                        if (!state->incoming.empty()) {
                            message = std::move(state->incoming.front());
                            state->incoming.pop_front();
                            const auto bytes = clone_packet_size(message->packet);
                            state->incoming_bytes = bytes > state->incoming_bytes
                                ? 0U : state->incoming_bytes - bytes;
                        }
                        if (!state->fetches.empty()) {
                            fetch_request = std::move(state->fetches.front());
                            state->fetches.pop_front();
                        }
                    }
                    v8::Isolate::Scope isolate_scope(child->isolate);
                    v8::HandleScope handles(child->isolate);
                    auto realm = child->context.Get(child->isolate);
                    v8::Context::Scope context_scope(realm);
                    if (fetch_request != nullptr) {
                        service_worker_fetch_result failure;
                        failure.handled = true;
                        if (fetch_request->cancelled.load(std::memory_order_acquire)) {
                            failure.error = "Service worker fetch was cancelled";
                            release_fetch_slot(fetch_request);
                            fetch_request->settle(std::move(failure));
                            fetch_request.reset();
                        }
                    }
                    if (fetch_request != nullptr) {
                        service_worker_fetch_result failure;
                        failure.handled = true;
                        v8::TryCatch caught(child->isolate);
                        v8::Local<v8::Value> helper_value;
                        v8::Local<v8::Value> request_constructor_value;
                        if (!realm->Global()->Get(
                                realm,
                                js_string(child->isolate,
                                    "__webSceneDispatchServiceWorkerFetchRecord"))
                                .ToLocal(&helper_value)
                            || !helper_value->IsFunction()
                            || !realm->Global()->Get(
                                realm,
                                js_string(child->isolate, "Request"))
                                .ToLocal(&request_constructor_value)
                            || !request_constructor_value->IsFunction()) {
                            failure.error = "Service worker fetch dispatcher is unavailable";
                            fetch_request->settle(std::move(failure));
                            release_fetch_slot(fetch_request);
                        } else {
                            auto options = v8::Object::New(child->isolate);
                            options->Set(
                                realm,
                                js_string(child->isolate, "method"),
                                js_dom_string(child->isolate, fetch_request->method)).Check();
                            const auto destination = fetch_request->destination
                                    == WEBSCENE_REQUEST_DESTINATION_SCRIPT ? "script"
                                : fetch_request->destination
                                    == WEBSCENE_REQUEST_DESTINATION_STYLE ? "style"
                                : fetch_request->destination
                                    == WEBSCENE_REQUEST_DESTINATION_IMAGE ? "image"
                                : fetch_request->destination
                                    == WEBSCENE_REQUEST_DESTINATION_DOCUMENT ? "document"
                                : fetch_request->destination
                                    == WEBSCENE_REQUEST_DESTINATION_FONT ? "font"
                                : "";
                            options->Set(
                                realm,
                                js_string(child->isolate, "destination"),
                                js_string(child->isolate, destination)).Check();
                            const auto mode = fetch_request->mode
                                    == WEBSCENE_FETCH_MODE_SAME_ORIGIN ? "same-origin"
                                : fetch_request->mode == WEBSCENE_FETCH_MODE_NO_CORS
                                    ? "no-cors" : "cors";
                            const auto credentials = fetch_request->credentials
                                    == WEBSCENE_FETCH_CREDENTIALS_OMIT ? "omit"
                                : fetch_request->credentials == WEBSCENE_FETCH_CREDENTIALS_INCLUDE
                                    ? "include" : "same-origin";
                            const auto redirect = fetch_request->redirect == 1U ? "error"
                                : fetch_request->redirect == 2U ? "manual" : "follow";
                            options->Set(realm, js_string(child->isolate, "mode"),
                                js_string(child->isolate, mode)).Check();
                            options->Set(realm, js_string(child->isolate, "credentials"),
                                js_string(child->isolate, credentials)).Check();
                            options->Set(realm, js_string(child->isolate, "redirect"),
                                js_string(child->isolate, redirect)).Check();
                            auto headers = v8::Array::New(
                                child->isolate,
                                static_cast<int>(fetch_request->headers.size()));
                            for (uint32_t index = 0U;
                                 index < fetch_request->headers.size(); ++index) {
                                auto pair = v8::Array::New(child->isolate, 2);
                                pair->Set(
                                    realm, 0,
                                    js_dom_string(child->isolate,
                                        fetch_request->headers[index].first)).Check();
                                pair->Set(
                                    realm, 1,
                                    js_dom_string(child->isolate,
                                        fetch_request->headers[index].second)).Check();
                                headers->Set(realm, index, pair).Check();
                            }
                            options->Set(
                                realm, js_string(child->isolate, "headers"), headers).Check();
                            if (fetch_request->method != "GET"
                                && fetch_request->method != "HEAD"
                                && !fetch_request->body.empty()) {
                                auto body = v8::ArrayBuffer::New(
                                    child->isolate,
                                    fetch_request->body.size());
                                std::memcpy(
                                    body->GetBackingStore()->Data(),
                                    fetch_request->body.data(),
                                    fetch_request->body.size());
                                options->Set(
                                    realm,
                                    js_string(child->isolate, "body"),
                                    body).Check();
                            }
                            v8::Local<v8::Value> request_arguments[]{
                                js_dom_string(child->isolate, fetch_request->url), options};
                            v8::Local<v8::Object> request_value;
                            if (!request_constructor_value.As<v8::Function>()->NewInstance(
                                    realm, 2, request_arguments).ToLocal(&request_value)) {
                                failure.error = child->describe_reported_exception(caught, realm);
                                fetch_request->settle(std::move(failure));
                                release_fetch_slot(fetch_request);
                            } else {
                                auto init = v8::Object::New(child->isolate);
                                init->Set(
                                    realm, js_string(child->isolate, "request"),
                                    request_value).Check();
                                init->Set(
                                    realm, js_string(child->isolate, "clientId"),
                                    js_dom_string(child->isolate,
                                        fetch_request->client_id)).Check();
                                init->Set(
                                    realm, js_string(child->isolate, "resultingClientId"),
                                    js_string(child->isolate, "")).Check();
                                init->Set(
                                    realm, js_string(child->isolate, "replacesClientId"),
                                    js_string(child->isolate, "")).Check();
                                v8::Local<v8::Value> arguments[]{init};
                                v8::Local<v8::Value> dispatched;
                                if (!helper_value.As<v8::Function>()->Call(
                                        realm, realm->Global(), 1, arguments)
                                        .ToLocal(&dispatched)
                                    || !dispatched->IsPromise()) {
                                    failure.error = child->describe_reported_exception(
                                        caught, realm);
                                    fetch_request->settle(std::move(failure));
                                    release_fetch_slot(fetch_request);
                                } else {
                                    pending_fetch pending;
                                    pending.request = std::move(fetch_request);
                                    pending.promise.Reset(
                                        child->isolate, dispatched.As<v8::Promise>());
                                    pending_fetches.push_back(std::move(pending));
                                }
                            }
                        }
                    }
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
                    for (auto iterator = pending_fetches.begin();
                         iterator != pending_fetches.end();) {
                        auto promise = iterator->promise.Get(child->isolate);
                        auto client_retired = false;
                        {
                            std::lock_guard lock(state->mutex);
                            const auto client = state->clients.find(
                                iterator->request->client_id);
                            client_retired = client == state->clients.end()
                                || client->second.generation
                                    != iterator->request->client_generation;
                        }
                        const auto cancelled = iterator->request->cancelled.load(
                            std::memory_order_acquire);
                        if (client_retired || cancelled) {
                            service_worker_fetch_result retired;
                            retired.handled = true;
                            retired.error = cancelled
                                ? "Service worker fetch was cancelled"
                                : "Service worker fetch client was retired";
                            release_fetch_slot(iterator->request);
                            iterator->request->settle(std::move(retired));
                            iterator->promise.Reset();
                            iterator = pending_fetches.erase(iterator);
                            continue;
                        }
                        if (promise->State() == v8::Promise::kPending) {
                            ++iterator;
                            continue;
                        }
                        service_worker_fetch_result result;
                        result.handled = true;
                        if (promise->State() == v8::Promise::kRejected) {
                            result.error = to_utf8(child->isolate, promise->Result());
                        } else if (!promise->Result()->IsObject()) {
                            result.error = "Service worker fetch result is invalid";
                        } else {
                            auto object = promise->Result().As<v8::Object>();
                            const auto property = [&](const char* name)
                                -> v8::Local<v8::Value> {
                                v8::Local<v8::Value> value;
                                if (!object->Get(
                                        realm, js_string(child->isolate, name))
                                        .ToLocal(&value)) return v8::Undefined(child->isolate);
                                return value;
                            };
                            result.handled = property("handled")->BooleanValue(
                                child->isolate);
                            if (result.handled) {
                                result.status = property("status")->Uint32Value(
                                    realm).FromMaybe(0U);
                            result.status_text = to_utf8(
                                    child->isolate, property("statusText"));
                                result.url = to_utf8(child->isolate, property("url"));
                                result.response_type = to_utf8(
                                    child->isolate, property("type"));
                                result.redirected = property("redirected")->BooleanValue(
                                    child->isolate);
                                auto body = property("body");
                                if (!body->IsArrayBufferView()) {
                                    result.error = "Service worker response body is invalid";
                                } else {
                                    auto view = body.As<v8::ArrayBufferView>();
                                    if (view->ByteLength()
                                        > maximum_service_worker_fetch_body_bytes) {
                                        result.error = "Service worker response body exceeds 64 MiB";
                                    } else {
                                        const auto body_bytes = view->ByteLength();
                                        auto queued = state->queued_response_bytes;
                                        auto current = queued->load(std::memory_order_acquire);
                                        while (current <= maximum_service_worker_fetch_body_bytes
                                            && body_bytes
                                                <= maximum_service_worker_fetch_body_bytes - current
                                            && !queued->compare_exchange_weak(
                                                current,
                                                current + body_bytes,
                                                std::memory_order_acq_rel,
                                                std::memory_order_acquire)) {
                                        }
                                        if (current > maximum_service_worker_fetch_body_bytes
                                            || body_bytes
                                                > maximum_service_worker_fetch_body_bytes - current) {
                                            result.error =
                                                "Service worker aggregate response bodies exceed 64 MiB";
                                        } else {
                                            const auto backing =
                                                view->Buffer()->GetBackingStore();
                                            const auto* bytes = static_cast<const char*>(
                                                backing->Data()) + view->ByteOffset();
                                            auto lease =
                                                std::make_shared<service_worker_body_lease>(
                                                    queued, body_bytes);
                                            result.body.assign(bytes, body_bytes);
                                            result.body_lease = std::move(lease);
                                        }
                                    }
                                }
                                auto headers = property("headers");
                                if (headers->IsArray()) {
                                    auto values = headers.As<v8::Array>();
                                    for (uint32_t index = 0U;
                                         index < values->Length(); ++index) {
                                        v8::Local<v8::Value> entry_value;
                                        if (!values->Get(realm, index).ToLocal(&entry_value)
                                            || !entry_value->IsArray()) continue;
                                        auto entry = entry_value.As<v8::Array>();
                                        v8::Local<v8::Value> name;
                                        v8::Local<v8::Value> value;
                                        if (entry->Get(realm, 0).ToLocal(&name)
                                            && entry->Get(realm, 1).ToLocal(&value)) {
                                            result.headers.emplace_back(
                                                to_utf8(child->isolate, name),
                                                to_utf8(child->isolate, value));
                                        }
                                    }
                                }
                            }
                        }
                        release_fetch_slot(iterator->request);
                        iterator->request->settle(std::move(result));
                        iterator->promise.Reset();
                        iterator = pending_fetches.erase(iterator);
                    }
                }
                for (auto& pending : pending_fetches) {
                    pending.request->cancelled.store(true, std::memory_order_release);
                    pending.request->active_slot.store(false, std::memory_order_release);
                    service_worker_fetch_result retired;
                    retired.handled = true;
                    retired.error = "Service worker fetch runtime stopped";
                    pending.request->settle(std::move(retired));
                    pending.promise.Reset();
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
        auto client_url = self->current_navigation_address();
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
                // Each Window client has its own monotonically increasing
                // generation. A replacement iframe can be created within the
                // same top-level document generation, so the document counter
                // alone cannot distinguish a late response from its retired
                // predecessor.
                descriptor.generation =
                    self->next_service_worker_client_generation++;
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
                auto runtime = matched->runtime;
                self->service_worker_registrations.erase(scope);
                if (runtime != nullptr) runtime->stop();
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

    bool has_claimed_service_worker_for_url(std::string_view url)
    {
        for (const auto& [scope, registration] : service_worker_registrations) {
            if (registration.runtime == nullptr || !url.starts_with(scope)) continue;
            std::lock_guard lock(registration.runtime->mutex);
            if (!registration.runtime->stopped
                && registration.runtime->activated
                && registration.runtime->claimed) {
                return true;
            }
        }
        return false;
    }

    std::shared_future<service_worker_fetch_result>
    enqueue_controlled_service_worker_fetch(
        const std::string& url,
        const resource_request_context& request_context,
        std::shared_ptr<service_worker_fetch_request>* request_handle = nullptr,
        uint32_t redirect = 0U)
    {
        if (request_handle != nullptr) request_handle->reset();
        if (service_worker_realm || isolate == nullptr || context.IsEmpty()) {
            return {};
        }
        const auto local_context = isolate->GetCurrentContext();
        service_worker_client_state* client = nullptr;
        for (auto& [id, candidate] : service_worker_clients) {
            static_cast<void>(id);
            if (!candidate.context.IsEmpty()
                && candidate.context.Get(isolate) == local_context) {
                client = &candidate;
                break;
            }
        }
        if (client == nullptr) return {};
        std::shared_ptr<service_worker_runtime_state> runtime;
        size_t matched_scope = 0U;
        for (auto& [scope, registration] : service_worker_registrations) {
            if (registration.runtime == nullptr
                || !client->descriptor.url.starts_with(scope)
                || scope.size() < matched_scope) continue;
            std::lock_guard lock(registration.runtime->mutex);
            if (registration.runtime->stopped
                || !registration.runtime->activated
                || !registration.runtime->claimed) continue;
            runtime = registration.runtime;
            matched_scope = scope.size();
        }
        if (runtime == nullptr) return {};
        auto request = std::make_shared<service_worker_fetch_request>();
        if (request_handle != nullptr) *request_handle = request;
        request->url = url;
        request->method = request_context.method.empty()
            ? "GET" : request_context.method;
        request->destination = request_context.destination;
        request->mode = request_context.mode;
        request->credentials = request_context.credentials;
        request->redirect = redirect;
        request->headers = request_context.headers;
        request->body = request_context.body;
        const auto contains_header = [&](std::string_view name) {
            return std::ranges::any_of(request->headers, [&](const auto& header) {
                return header.first == name;
            });
        };
        if (!request_context.content_type.empty()
            && !contains_header("content-type")) {
            request->headers.emplace_back(
                "content-type", request_context.content_type);
        }
        if (!request_context.cookie.empty() && !contains_header("cookie")) {
            request->headers.emplace_back("cookie", request_context.cookie);
        }
        request->client_id = client->descriptor.id;
        request->client_generation = client->descriptor.generation;
        request->notify_client = runtime_work_available;
        request->notify_worker = [weak = std::weak_ptr(runtime)] {
            if (const auto owner = weak.lock()) owner->ready.notify_all();
        };
        auto future = request->promise.get_future().share();
        {
            std::lock_guard lock(runtime->mutex);
            const auto runtime_client = runtime->clients.find(request->client_id);
            if (runtime->stopped || runtime_client == runtime->clients.end()
                || runtime_client->second.generation
                    != request->client_generation) {
                service_worker_fetch_result result;
                result.handled = true;
                result.error = "Service worker fetch client was retired";
                request->settle(std::move(result));
                return future;
            }
            if (runtime->active_fetches >= maximum_service_worker_fetches) {
                service_worker_fetch_result result;
                result.handled = true;
                result.error = "Service worker fetch queue capacity exhausted";
                request->settle(std::move(result));
                return future;
            }
            ++runtime->active_fetches;
            request->active_slot.store(true, std::memory_order_release);
            runtime->fetch_high_water = std::max(
                runtime->fetch_high_water, runtime->active_fetches);
            runtime->fetches.push_back(request);
        }
        runtime->ready.notify_all();
        return future;
    }

    std::optional<service_worker_fetch_result>
    await_controlled_service_worker_fetch(
        const std::string& url,
        const resource_request_context& request_context,
        std::chrono::steady_clock::duration timeout)
    {
        const auto client_context = isolate->GetCurrentContext();
        std::string client_id;
        uint64_t client_generation = 0U;
        for (const auto& [id, candidate] : service_worker_clients) {
            if (!candidate.context.IsEmpty()
                && candidate.context.Get(isolate) == client_context) {
                client_id = id;
                client_generation = candidate.descriptor.generation;
                break;
            }
        }
        auto future = enqueue_controlled_service_worker_fetch(url, request_context);
        if (!future.valid()) return std::nullopt;
        const auto generation = document_generation.load(std::memory_order_acquire);
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (future.wait_for(std::chrono::seconds(0))
                != std::future_status::ready) {
            const auto current_client = service_worker_clients.find(client_id);
            if (document_generation.load(std::memory_order_acquire) != generation
                || client_id.empty()
                || current_client == service_worker_clients.end()
                || current_client->second.descriptor.generation
                    != client_generation
                || current_client->second.context.IsEmpty()
                || current_client->second.context.Get(isolate)
                    != client_context) {
                service_worker_fetch_result retired;
                retired.handled = true;
                retired.error = "Service worker parser resource client was retired";
                return retired;
            }
            if (has_service_worker_messages() && !drain_service_worker_message()) {
                service_worker_fetch_result failed;
                failed.handled = true;
                failed.error = last_error.empty()
                    ? "Service worker parser resource message delivery failed"
                    : last_error;
                return failed;
            }
            if (std::chrono::steady_clock::now() >= deadline) {
                service_worker_fetch_result timed_out;
                timed_out.handled = true;
                timed_out.error = "Service worker parser resource timed out";
                return timed_out;
            }
            pump_v8_platform_tasks();
            perform_microtask_checkpoint();
            static_cast<void>(future.wait_for(std::chrono::milliseconds(1)));
        }
        try {
            return future.get();
        } catch (const std::exception& error) {
            service_worker_fetch_result failed;
            failed.handled = true;
            failed.error = std::string(
                "Service worker parser resource failed: ") + error.what();
            return failed;
        } catch (...) {
            service_worker_fetch_result failed;
            failed.handled = true;
            failed.error = "Service worker parser resource failed";
            return failed;
        }
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
        std::vector<std::shared_ptr<service_worker_fetch_request>>
            retired_fetches;
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
            std::erase_if(
                registration.runtime->fetches,
                [&](const auto& request) {
                    if (request == nullptr
                        || std::ranges::find(
                            retired_ids, request->client_id)
                            == retired_ids.end()) {
                        return false;
                    }
                    retired_fetches.push_back(request);
                    if (request->active_slot.exchange(
                            false, std::memory_order_acq_rel)
                        && registration.runtime->active_fetches > 0U) {
                        --registration.runtime->active_fetches;
                    }
                    return true;
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
        for (auto& request : retired_fetches) {
            service_worker_fetch_result retired;
            retired.handled = true;
            retired.error = "Service worker fetch client was retired";
            request->settle(std::move(retired));
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
        std::deque<std::shared_ptr<service_worker_fetch_request>> fetches;
        size_t incoming_bytes{};
        size_t outgoing_bytes{};
        size_t incoming_high_water{};
        size_t outgoing_high_water{};
        size_t fetch_high_water{};
        size_t active_fetches{};
        std::shared_ptr<std::atomic_size_t> queued_response_bytes{
            std::make_shared<std::atomic_size_t>(0U)};
        std::shared_ptr<service_worker_cache_storage> cache_storage{
            std::make_shared<service_worker_cache_storage>()};
        std::function<void()> notify_client;
        bool stopped{false};
        bool activated{false};
        bool claimed{false};
        std::string error;

        void stop()
        {
            std::deque<std::shared_ptr<service_worker_fetch_request>> retired_fetches;
            {
                std::lock_guard lock(mutex);
                stopped = true;
                if (running_isolate != nullptr) running_isolate->TerminateExecution();
                clients.clear();
                incoming.clear();
                outgoing.clear();
                retired_fetches.swap(fetches);
                for (const auto& request : retired_fetches) {
                    if (request != nullptr) {
                        request->cancelled.store(true, std::memory_order_release);
                        request->active_slot.store(false, std::memory_order_release);
                    }
                }
                incoming_bytes = 0U;
                outgoing_bytes = 0U;
                active_fetches = 0U;
            }
            for (auto& request : retired_fetches) {
                if (request == nullptr) continue;
                service_worker_fetch_result retired;
                retired.handled = true;
                retired.error = "Service worker fetch runtime stopped";
                request->settle(std::move(retired));
            }
            ready.notify_all();
            if (thread.joinable()) thread.join();
        }

        ~service_worker_runtime_state() { stop(); }
    };
