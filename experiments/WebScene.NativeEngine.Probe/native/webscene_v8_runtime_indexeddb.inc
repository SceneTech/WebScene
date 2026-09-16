    std::string indexeddb_origin(v8::Local<v8::Context> realm)
    {
        v8::Local<v8::Value> location_value;
        if (!realm->Global()->Get(realm, js_string(isolate, "location"))
                .ToLocal(&location_value)
            || !location_value->IsObject()) return "null";
        auto location = location_value.As<v8::Object>();
        const auto property = [&](const char* name) {
            v8::Local<v8::Value> value;
            return location->Get(realm, js_string(isolate, name)).ToLocal(&value)
                ? to_utf8(isolate, value) : std::string{};
        };
        auto origin = property("origin");
        auto hostname = property("hostname");
        auto protocol = property("protocol");
        if (hostname.empty()) {
            const auto href = property("href");
            if (protocol.empty()) {
                const auto scheme_end = href.find(':');
                if (scheme_end != std::string::npos)
                    protocol = href.substr(0U, scheme_end + 1U);
            }
            const auto authority_start = href.find("://");
            if (authority_start != std::string::npos) {
                const auto authority_end = href.find_first_of(
                    "/?#", authority_start + 3U);
                auto authority = href.substr(
                    authority_start + 3U,
                    (authority_end == std::string::npos ? href.size() : authority_end)
                        - authority_start - 3U);
                if (const auto at = authority.rfind('@'); at != std::string::npos)
                    authority.erase(0U, at + 1U);
                if (authority.starts_with("[")) {
                    const auto close = authority.find(']');
                    hostname = authority.substr(
                        0U, close == std::string::npos ? authority.size() : close + 1U);
                } else {
                    const auto colon = authority.find(':');
                    hostname = authority.substr(0U, colon);
                }
                if (origin.empty()) origin = protocol + "//" + authority;
            }
        }
        std::transform(hostname.begin(), hostname.end(), hostname.begin(),
            [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
        if (hostname == "localhost" || hostname.ends_with(".localhost")
            || hostname.starts_with("127.")
            || hostname == "[::1]" || hostname == "::1") {
            origin = protocol + "//loopback";
        }
        return origin;
    }

    v8::Local<v8::Value> indexeddb_exception(
        v8::Local<v8::Context> realm,
        const std::string& message,
        const char* name)
    {
        v8::Local<v8::Value> constructor_value;
        if (realm->Global()->Get(realm, js_string(isolate, "DOMException"))
                .ToLocal(&constructor_value)
            && constructor_value->IsFunction()) {
            v8::Local<v8::Value> arguments[] = {
                js_string(isolate, message.c_str()), js_string(isolate, name)};
            v8::Local<v8::Object> result;
            if (constructor_value.As<v8::Function>()
                    ->NewInstance(realm, 2, arguments).ToLocal(&result)) return result;
        }
        return v8::Exception::Error(js_string(isolate, message.c_str()));
    }

    void enqueue_indexeddb_completion(uint64_t id, indexeddb_storage_result result)
    {
        {
            std::lock_guard lock(indexeddb_completion_mutex);
            indexeddb_completions.push_back({id, std::move(result)});
            indexeddb_work_ready.store(true, std::memory_order_release);
        }
        if (runtime_work_available) runtime_work_available();
    }

    static void indexeddb_storage_operation(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto realm = info.GetIsolate()->GetCurrentContext();
        if (self == nullptr || self->indexeddb == nullptr
            || !self->indexeddb->available()) {
            info.GetIsolate()->ThrowException(self == nullptr
                ? v8::Exception::Error(js_string(
                    info.GetIsolate(), "IndexedDB runtime is unavailable"))
                : self->indexeddb_exception(
                    realm, "Persistent storage was not configured by the host",
                    "NotSupportedError"));
            return;
        }
        if (info.Length() < 2 || !info[0]->IsString() || !info[1]->IsString()) {
            info.GetIsolate()->ThrowException(v8::Exception::TypeError(
                js_string(info.GetIsolate(), "Invalid IndexedDB storage operation")));
            return;
        }
        if (self->pending_indexeddb_promises.size() >= 256U) {
            info.GetIsolate()->ThrowException(self->indexeddb_exception(
                realm, "IndexedDB operation queue is full", "QuotaExceededError"));
            return;
        }
        const auto operation = to_utf8(info.GetIsolate(), info[0]);
        const auto database = to_utf8(info.GetIsolate(), info[1]);
        const auto origin = self->indexeddb_origin(realm);
        if (database.empty() || database.size() > 4096U
            || origin.empty() || origin == "null") {
            info.GetIsolate()->ThrowException(self->indexeddb_exception(
                realm, "IndexedDB is unavailable for this origin", "SecurityError"));
            return;
        }
        auto resolver = v8::Promise::Resolver::New(realm).ToLocalChecked();
        const auto id = self->next_indexeddb_operation_id++;
        self->pending_indexeddb_promises.emplace(id, pending_indexeddb_promise{
            v8::Global<v8::Context>(info.GetIsolate(), realm),
            v8::Global<v8::Promise::Resolver>(info.GetIsolate(), resolver),
            operation == "load"});
        const auto completion = [self, id](indexeddb_storage_result result) {
            self->enqueue_indexeddb_completion(id, std::move(result));
        };
        if (operation == "load") {
            self->indexeddb->load(origin, database, completion);
        } else if (operation == "delete") {
            self->indexeddb->erase(origin, database, completion);
        } else if (operation == "store") {
            if (info.Length() < 4 || !info[2]->IsBigInt()) {
                self->pending_indexeddb_promises.erase(id);
                info.GetIsolate()->ThrowException(v8::Exception::TypeError(
                    js_string(info.GetIsolate(),
                        "IndexedDB store requires a revision and value")));
                return;
            }
            bool lossless = false;
            const auto revision = info[2].As<v8::BigInt>()->Uint64Value(&lossless);
            clone_packet packet;
            if (!lossless || !serialize_clone(
                    info, info[3], v8::Undefined(info.GetIsolate()), packet)) {
                self->pending_indexeddb_promises.erase(id);
                return;
            }
            self->indexeddb->store(
                origin, database, revision, std::move(packet.bytes), completion);
        } else {
            self->pending_indexeddb_promises.erase(id);
            info.GetIsolate()->ThrowException(v8::Exception::TypeError(
                js_string(info.GetIsolate(), "Unknown IndexedDB storage operation")));
            return;
        }
        info.GetReturnValue().Set(resolver->GetPromise());
    }

    void install_indexeddb(v8::Local<v8::Context> realm)
    {
        if (indexeddb == nullptr) return;
        auto global = realm->Global();
        global->DefineOwnProperty(
            realm,
            js_string(isolate, "__webSceneIndexedDBStorage"),
            v8::Function::New(realm, indexeddb_storage_operation).ToLocalChecked(),
            v8::PropertyAttribute::DontEnum).Check();
        auto script = v8::Script::Compile(
            realm,
            js_string(isolate, std::string(indexeddb_compatibility_source).c_str()))
            .ToLocalChecked();
        script->Run(realm).ToLocalChecked();
    }

    bool drain_indexeddb_completion()
    {
        indexeddb_completion completion;
        {
            std::lock_guard lock(indexeddb_completion_mutex);
            if (indexeddb_completions.empty()) {
                indexeddb_work_ready.store(false, std::memory_order_release);
                return true;
            }
            completion = std::move(indexeddb_completions.front());
            indexeddb_completions.pop_front();
            indexeddb_work_ready.store(
                !indexeddb_completions.empty(), std::memory_order_release);
        }
        const auto pending = pending_indexeddb_promises.find(completion.id);
        if (pending == pending_indexeddb_promises.end()) return true;
        auto realm = pending->second.context.Get(isolate);
        auto resolver = pending->second.resolver.Get(isolate);
        const auto deserialize_data = pending->second.deserialize_data;
        pending_indexeddb_promises.erase(pending);
        if (realm.IsEmpty() || resolver.IsEmpty()) return true;
        v8::Context::Scope context_scope(realm);
        auto& result = completion.result;
        if (result.status == indexeddb_storage_status::ok
            || result.status == indexeddb_storage_status::not_found) {
            auto response = v8::Object::New(isolate);
            response->Set(realm, js_string(isolate, "revision"),
                v8::BigInt::NewFromUnsigned(isolate, result.revision)).Check();
            v8::Local<v8::Value> data = v8::Null(isolate);
            if (result.status == indexeddb_storage_status::ok && deserialize_data) {
                clone_packet packet;
                packet.bytes = std::move(result.bytes);
                if (!deserialize_clone(isolate, realm, packet).ToLocal(&data)) {
                    resolver->Reject(realm, indexeddb_exception(
                        realm, "IndexedDB structured data could not be decoded",
                        "DataError")).Check();
                    perform_microtask_checkpoint();
                    return true;
                }
            }
            response->Set(realm, js_string(isolate, "data"), data).Check();
            resolver->Resolve(realm, response).Check();
        } else {
            const char* name = "UnknownError";
            if (result.status == indexeddb_storage_status::quota_exceeded)
                name = "QuotaExceededError";
            else if (result.status == indexeddb_storage_status::conflict)
                name = "AbortError";
            else if (result.status == indexeddb_storage_status::unavailable)
                name = "NotSupportedError";
            else if (result.status == indexeddb_storage_status::corrupt)
                name = "DataError";
            resolver->Reject(realm, indexeddb_exception(
                realm,
                result.message.empty() ? "IndexedDB storage operation failed"
                                       : result.message,
                name)).Check();
        }
        perform_microtask_checkpoint();
        return true;
    }
