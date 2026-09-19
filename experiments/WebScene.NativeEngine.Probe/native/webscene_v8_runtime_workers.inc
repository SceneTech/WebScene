    struct worker_state {
        std::mutex mutex;
        std::condition_variable ready;
        std::deque<clone_packet> incoming, outgoing;
        size_t incoming_bytes{};
        size_t outgoing_bytes{};
        std::deque<std::string> errors;
        bool stopped{false};
        bool wake_requested{false};
        bool wrapper_weak{false};
        bool wrapper_collected{false};
        bool terminal_failure_reported{false};
        bool startup_settled{false};
        uint64_t generation{1U};
        uint64_t low_memory_requested{};
        uint64_t low_memory_completed{};
        v8::Isolate* running_isolate{};
        std::function<void()> notify_parent;
        std::thread thread;
        v8::Global<v8::Object> wrapper;
        v8::Global<v8::Context> realm;
        void stop() {
            {
                std::lock_guard lock(mutex);
                ++generation;
                stopped = true;
                if (running_isolate) running_isolate->TerminateExecution();
                incoming.clear(); outgoing.clear(); errors.clear();
                incoming_bytes = 0U; outgoing_bytes = 0U;
            }
            ready.notify_all();
            if (thread.joinable()) thread.join();
        }
        ~worker_state() { stop(); }
    };
    std::vector<std::unique_ptr<worker_state>> dedicated_workers;
    worker_state* worker_owner{};
    uint64_t worker_owner_generation{};
    bool force_dedicated_isolate{false};
    bool worker_module_realm{false};
    static constexpr size_t maximum_dedicated_workers = 64U;
    // Stopped Worker wrappers can remain reachable from JavaScript and must
    // keep their callback data alive. Bound those lightweight bindings
    // separately from the threads/isolates counted above.
    static constexpr size_t maximum_worker_bindings = 256U;
    static constexpr size_t maximum_worker_queue_messages = 256U;
    static constexpr size_t maximum_worker_queue_bytes = 16U * 1024U * 1024U;
    static constexpr size_t maximum_worker_errors = 16U;
    static constexpr size_t maximum_worker_error_length = 4096U;
    static constexpr size_t maximum_worker_redirect_hops = 20U;
    static constexpr size_t maximum_worker_redirect_url_bytes = 8U * 1024U;
    static constexpr size_t maximum_worker_redirect_metadata_bytes = 64U * 1024U;
    static constexpr size_t maximum_worker_response_headers = 64U;
    static constexpr size_t maximum_worker_status_text_bytes = 1024U;

    bool worker_generation_is_current() const
    {
        if (worker_owner == nullptr) return true;
        std::lock_guard lock(worker_owner->mutex);
        return !worker_owner->stopped
            && worker_owner->generation == worker_owner_generation;
    }

    bool validate_worker_redirect_hops(
        const std::string& requested_url,
        const std::string& final_url,
        uint32_t mode,
        uint32_t credentials,
        const std::string& origin,
        const resource_response& response,
        std::string& error) const
    {
        if (response.headers.size() > maximum_worker_response_headers
            || response.status_text.size() > maximum_worker_status_text_bytes
            || final_url.size() > maximum_worker_redirect_url_bytes) {
            error = "Worker response metadata is invalid";
            return false;
        }
        size_t metadata_bytes = response.status_text.size() + final_url.size();
        for (const auto& [name, value] : response.headers) {
            if (name.empty() || name.find('\0') != std::string::npos
                || name.find_first_of("\r\n") != std::string::npos
                || value.find('\0') != std::string::npos
                || value.find_first_of("\r\n") != std::string::npos
                || name.size()
                    > maximum_worker_redirect_metadata_bytes - metadata_bytes
                || value.size() > maximum_worker_redirect_metadata_bytes
                    - metadata_bytes - name.size()) {
                error = "Worker response metadata is invalid";
                return false;
            }
            metadata_bytes += name.size() + value.size();
        }
        if (response.redirect_hops.empty()) {
            if (final_url != requested_url) {
                error = "Worker redirect chain metadata is required";
                return false;
            }
            return true;
        }
        if (!response.has_http_metadata) {
            error = "Worker redirect metadata requires an HTTP response";
            return false;
        }
        if (response.redirect_hops.size() > maximum_worker_redirect_hops) {
            error = "Worker redirect limit exceeded";
            return false;
        }
        const auto valid_network_url = [](const std::string& url) {
            const auto scheme = resource_scheme(url);
            return (scheme == "http" || scheme == "https")
                && !resource_origin(url).empty()
                && std::none_of(
                    url.begin(), url.end(), [](unsigned char value) {
                        return value <= 0x20U || value == 0x7fU;
                    });
        };
        const auto redirect_status = [](uint32_t status) {
            return status == 301U || status == 302U || status == 303U
                || status == 307U || status == 308U;
        };
        std::vector<std::string_view> visited{requested_url};
        auto expected_source = std::string_view(requested_url);
        for (const auto& hop : response.redirect_hops) {
            if (hop.source_url.size() > maximum_worker_redirect_url_bytes
                || hop.destination_url.size() > maximum_worker_redirect_url_bytes
                || hop.cors_allow_origin.size() > maximum_worker_redirect_url_bytes) {
                error = "Worker redirect metadata is invalid";
                return false;
            }
            const auto hop_bytes = hop.source_url.size()
                + hop.destination_url.size()
                + hop.cors_allow_origin.size();
            if (hop_bytes > maximum_worker_redirect_metadata_bytes
                - metadata_bytes) {
                error = "Worker redirect metadata is invalid";
                return false;
            }
            metadata_bytes += hop_bytes;
            if (hop.source_url != expected_source
                || !redirect_status(hop.status)
                || !valid_network_url(hop.source_url)
                || !valid_network_url(hop.destination_url)) {
                error = "Worker redirect metadata is invalid";
                return false;
            }
            if (std::find(
                    visited.begin(),
                    visited.end(),
                    std::string_view(hop.destination_url))
                != visited.end()) {
                error = "Worker redirect loop rejected";
                return false;
            }
            if (resource_scheme(hop.source_url) == "https"
                && resource_scheme(hop.destination_url) != "https") {
                error = "Worker redirect downgrade rejected";
                return false;
            }
            const auto source_origin = resource_origin(hop.source_url);
            const auto destination_origin = resource_origin(
                hop.destination_url);
            if (mode == WEBSCENE_FETCH_MODE_SAME_ORIGIN
                && (source_origin != origin
                    || destination_origin != origin)) {
                error = "Worker redirect violates same-origin policy";
                return false;
            }
            if (hop.credentials_forwarded
                && (credentials == WEBSCENE_FETCH_CREDENTIALS_OMIT
                    || (credentials == WEBSCENE_FETCH_CREDENTIALS_SAME_ORIGIN
                        && destination_origin != origin))) {
                error = "Worker redirect forwarded disallowed credentials";
                return false;
            }
            if (mode == WEBSCENE_FETCH_MODE_CORS
                && source_origin != origin) {
                const auto cors_allowed = hop.cors_allow_origin == origin
                    || (hop.cors_allow_origin == "*"
                        && credentials
                            != WEBSCENE_FETCH_CREDENTIALS_INCLUDE);
                if (!cors_allowed
                    || (credentials == WEBSCENE_FETCH_CREDENTIALS_INCLUDE
                        && !hop.cors_credentials_allowed)) {
                    error = "Worker redirect response blocked by CORS";
                    return false;
                }
            }
            visited.push_back(hop.destination_url);
            expected_source = hop.destination_url;
        }
        if (expected_source != final_url) {
            error = "Worker redirect chain is incomplete";
            return false;
        }
        return true;
    }

    bool load_worker_script_resource(
        const std::string& value,
        const std::string& base,
        uint32_t mode,
        std::string& content,
        std::string& resolved_name,
        std::string& error)
    {
        if (!worker_generation_is_current()) {
            error = "Worker script load was cancelled";
            return false;
        }
        const auto requested_url = resolve_resource_url(value, base);
        const auto origin = worker_script_fetch_policy_active
            ? worker_script_fetch_origin
            : current_security_origin();
        const auto credentials = worker_script_fetch_policy_active
            ? worker_script_fetch_credentials
            : WEBSCENE_FETCH_CREDENTIALS_SAME_ORIGIN;
        if (resource_scheme(origin) == "https"
            && resource_scheme(requested_url) == "http") {
            error = "Worker script security downgrade rejected";
            return false;
        }
        if (mode == WEBSCENE_FETCH_MODE_SAME_ORIGIN
            && resource_origin(requested_url) != origin) {
            error = "Worker script violates same-origin policy: " + requested_url;
            return false;
        }
        resource_request_context request_context{
            WEBSCENE_RESOURCE_INITIATOR_SUBRESOURCE,
            origin,
            base,
            mode,
            WEBSCENE_REQUEST_DESTINATION_SCRIPT,
            "GET",
            {},
            {},
            credentials,
            {}};
        if (!load_resource_callback) {
            const auto loaded = load_text_resource(
                    requested_url,
                    {},
                    WEBSCENE_RESOURCE_SCRIPT,
                    content,
                    resolved_name,
                    nullptr,
                    request_context);
            if (!worker_generation_is_current()) {
                content.clear();
                error = "Worker script load was cancelled";
                return false;
            }
            if (!loaded) {
                error = "Unable to load worker script: " + requested_url;
                return false;
            }
            return true;
        }

        if (!worker_generation_is_current()) {
            error = "Worker script load was cancelled";
            return false;
        }
        request_context.cookie = cookie_header_for(requested_url, request_context);
        resource_response response;
        auto loaded = false;
        try {
            loaded = load_resource_callback(
                WEBSCENE_RESOURCE_SCRIPT,
                requested_url,
                request_context,
                {},
                0,
                response);
        } catch (...) {
            loaded = false;
        }
        if (!worker_generation_is_current()) {
            error = "Worker script load was cancelled";
            return false;
        }
        if (!loaded) {
            error = "Unable to load worker script: " + requested_url;
            return false;
        }
        resolved_name = response.final_url.empty()
            ? requested_url
            : response.final_url;
        if (response.has_http_metadata
            && (response.status < 200U || response.status >= 300U)) {
            error = "Worker script response has an unsuccessful status";
            return false;
        }
        if (!validate_worker_redirect_hops(
                requested_url,
                resolved_name,
                mode,
                credentials,
                origin,
                response,
                error)) {
            return false;
        }

        const auto final_origin = resource_origin(resolved_name);
        if (mode == WEBSCENE_FETCH_MODE_SAME_ORIGIN
            && final_origin != origin) {
            error = "Worker script redirect violates same-origin policy";
            return false;
        }
        const auto cross_origin_request =
            resource_origin(requested_url) != origin || final_origin != origin;
        if (mode == WEBSCENE_FETCH_MODE_CORS && cross_origin_request) {
            auto cors_allowed = false;
            auto credentials_allowed = false;
            for (const auto& [raw_name, raw_value] : response.headers) {
                const auto name = lower_html_name(raw_name);
                if (name == "access-control-allow-origin"
                    && (raw_value == origin
                        || (raw_value == "*"
                            && credentials != WEBSCENE_FETCH_CREDENTIALS_INCLUDE))) {
                    cors_allowed = true;
                } else if (name == "access-control-allow-credentials"
                    && lower_html_name(raw_value) == "true") {
                    credentials_allowed = true;
                }
            }
            if (!response.has_http_metadata || !cors_allowed
                || (credentials == WEBSCENE_FETCH_CREDENTIALS_INCLUDE
                    && !credentials_allowed)) {
                error = "Worker script response blocked by CORS";
                return false;
            }
        }
        accept_response_cookies(requested_url, request_context, response);
        content = std::move(response.content);
        return true;
    }

    static void worker_wrapper_collected(
        const v8::WeakCallbackInfo<worker_state>& info)
    {
        auto* state = info.GetParameter();
        if (state == nullptr) return;
        state->wrapper.Reset();
        state->realm.Reset();
        std::lock_guard lock(state->mutex);
        state->wrapper_collected = true;
    }

    void release_stopped_worker_wrapper(worker_state& state)
    {
        {
            std::lock_guard lock(state.mutex);
            if (!state.stopped || state.wrapper_weak
                || state.wrapper_collected) {
                return;
            }
        }
        // Join before weakening the wrapper. No worker thread may retain or
        // publish more parent-realm work after this ownership transition.
        state.stop();
        if (state.wrapper.IsEmpty()) {
            std::lock_guard lock(state.mutex);
            state.wrapper_collected = true;
            return;
        }
        state.wrapper.SetWeak(
            &state,
            worker_wrapper_collected,
            v8::WeakCallbackType::kParameter);
        std::lock_guard lock(state.mutex);
        state.wrapper_weak = true;
    }

    void reclaim_collected_worker_bindings()
    {
        std::erase_if(dedicated_workers, [](const auto& worker) {
            if (worker == nullptr) return true;
            std::lock_guard lock(worker->mutex);
            return worker->stopped && worker->wrapper_collected;
        });
    }

    bool reserve_worker_binding()
    {
        for (const auto& worker : dedicated_workers) {
            if (worker != nullptr) release_stopped_worker_wrapper(*worker);
        }
        reclaim_collected_worker_bindings();
        if (dedicated_workers.size() < maximum_worker_bindings) return true;

        // Native worker bindings are outside V8's heap. Give unreachable,
        // already-terminated wrappers one collection opportunity before
        // reporting the explicit retained-wrapper bound.
        isolate->LowMemoryNotification();
        reclaim_collected_worker_bindings();
        return dedicated_workers.size() < maximum_worker_bindings;
    }

    size_t active_worker_count() const
    {
        size_t count = 0U;
        for (const auto& worker : dedicated_workers) {
            if (worker == nullptr) continue;
            std::lock_guard lock(worker->mutex);
            if (!worker->stopped) ++count;
        }
        return count;
    }

    void request_worker_low_memory()
    {
        std::vector<std::pair<worker_state*, uint64_t>> pending;
        for (const auto& worker : dedicated_workers) {
            if (worker == nullptr) continue;
            uint64_t generation = 0U;
            {
                std::lock_guard lock(worker->mutex);
                if (worker->stopped || worker->running_isolate == nullptr) continue;
                generation = ++worker->low_memory_requested;
                worker->wake_requested = true;
            }
            pending.emplace_back(worker.get(), generation);
            worker->ready.notify_all();
        }
        for (const auto& [worker, generation] : pending) {
            std::unique_lock lock(worker->mutex);
            worker->ready.wait(lock, [&] {
                return worker->stopped
                    || worker->low_memory_completed >= generation;
            });
        }
    }

    void install_dedicated_worker_global_scope(v8::Local<v8::Context> realm) {
        constexpr std::string_view source = R"JS(
          (() => {
            function WorkerGlobalScope() {
              throw new TypeError('Illegal constructor');
            }
            function DedicatedWorkerGlobalScope() {
              throw new TypeError('Illegal constructor');
            }
            Object.setPrototypeOf(
              WorkerGlobalScope.prototype,
              EventTarget.prototype);
            Object.setPrototypeOf(WorkerGlobalScope, EventTarget);
            Object.setPrototypeOf(
              DedicatedWorkerGlobalScope.prototype,
              WorkerGlobalScope.prototype);
            Object.setPrototypeOf(
              DedicatedWorkerGlobalScope,
              WorkerGlobalScope);
            Object.defineProperty(
              WorkerGlobalScope.prototype,
              Symbol.toStringTag,
              {value:'WorkerGlobalScope', configurable:true});
            Object.defineProperty(
              DedicatedWorkerGlobalScope.prototype,
              Symbol.toStringTag,
              {value:'DedicatedWorkerGlobalScope', configurable:true});
            Object.defineProperties(globalThis, {
              WorkerGlobalScope: {
                value: WorkerGlobalScope,
                writable: true,
                configurable: true
              },
              DedicatedWorkerGlobalScope: {
                value: DedicatedWorkerGlobalScope,
                writable: true,
                configurable: true
              }
            });
            Object.setPrototypeOf(
              globalThis,
              DedicatedWorkerGlobalScope.prototype);
          })();
        )JS";
        auto script = v8::Script::Compile(
            realm,
            js_dom_string(isolate, std::string(source))).ToLocalChecked();
        script->Run(realm).ToLocalChecked();
    }

    void install_worker_constructor(v8::Local<v8::Context> local_context) {
        auto worker_constructor = v8::Function::New(
            local_context,
            worker_construct,
            {},
            1).ToLocalChecked();
        v8::Local<v8::Value> event_target;
        v8::Local<v8::Value> worker_prototype;
        v8::Local<v8::Value> event_prototype;
        if (local_context->Global()->Get(
                local_context,
                js_string(isolate, "EventTarget")).ToLocal(&event_target)
            && event_target->IsFunction()
            && worker_constructor->Get(
                local_context,
                js_string(isolate, "prototype")).ToLocal(&worker_prototype)
            && event_target.As<v8::Object>()->Get(
                local_context,
                js_string(isolate, "prototype")).ToLocal(&event_prototype)) {
            worker_prototype.As<v8::Object>()->SetPrototype(
                local_context,
                event_prototype).FromMaybe(false);
        }
        local_context->Global()->Set(
            local_context,
            js_string(isolate, "Worker"),
            worker_constructor).Check();
    }

    static v8::Local<v8::Private> worker_state_private(v8::Isolate* isolate)
    {
        return v8::Private::ForApi(
            isolate, js_string(isolate, "WebScene.Worker.State"));
    }

    static worker_state* worker_state_from_receiver(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto realm = info.GetIsolate()->GetCurrentContext();
        v8::Local<v8::Value> value;
        if (info.This()->IsObject()
            && info.This().As<v8::Object>()->GetPrivate(
                realm, worker_state_private(info.GetIsolate())).ToLocal(&value)
            && value->IsExternal()) {
            return static_cast<worker_state*>(
                value.As<v8::External>()->Value(
                    v8::kExternalPointerTypeTagDefault));
        }
        info.GetIsolate()->ThrowException(v8::Exception::TypeError(
            js_string(info.GetIsolate(), "Illegal invocation")));
        return nullptr;
    }

    static v8::MaybeLocal<v8::Value> worker_transfer(
        const v8::FunctionCallbackInfo<v8::Value>& info) {
        if (info.Length() < 2) return v8::Undefined(info.GetIsolate());
        if (info[1]->IsObject() && !info[1]->IsArray())
            return info[1].As<v8::Object>()->Get(info.GetIsolate()->GetCurrentContext(), js_string(info.GetIsolate(), "transfer"));
        return info[1];
    }
    static void worker_post_message_impl(const v8::FunctionCallbackInfo<v8::Value>& info) {
        auto* state = worker_state_from_receiver(info);
        if (state == nullptr) return;
        v8::Local<v8::Value> transfer;
        if (!worker_transfer(info).ToLocal(&transfer)) return;
        size_t remaining_bytes = 0U;
        {
            std::lock_guard lock(state->mutex);
            if (state->stopped) return;
            if (state->incoming.size() >= maximum_worker_queue_messages
                || state->incoming_bytes >= maximum_worker_queue_bytes) {
                throw_dom_exception(info,"Worker message queue capacity exhausted","QuotaExceededError");
                return;
            }
            remaining_bytes = maximum_worker_queue_bytes - state->incoming_bytes;
        }
        clone_packet packet;
        if (!serialize_clone(info, info[0], transfer, packet, remaining_bytes)) return;
        const auto bytes = clone_packet_size(packet);
        {
            std::lock_guard lock(state->mutex);
            if (state->stopped) return;
            if (state->incoming.size() >= maximum_worker_queue_messages
                || bytes > maximum_worker_queue_bytes - state->incoming_bytes) {
                throw_dom_exception(info,"Worker message queue capacity exhausted","QuotaExceededError");
                return;
            }
            state->incoming_bytes += bytes;
            state->incoming.push_back(std::move(packet));
        }
        state->ready.notify_one();
    }
    static void worker_post_parent_impl(const v8::FunctionCallbackInfo<v8::Value>& info) {
        auto* self = current(info.GetIsolate());
        if (!self || !self->worker_owner) return;
        auto* state = self->worker_owner;
        v8::Local<v8::Value> transfer;
        if (!worker_transfer(info).ToLocal(&transfer)) return;
        size_t remaining_bytes = 0U;
        {
            std::lock_guard lock(state->mutex);
            if (state->stopped) return;
            if (state->outgoing.size() >= maximum_worker_queue_messages
                || state->outgoing_bytes >= maximum_worker_queue_bytes) {
                throw_dom_exception(info,"Worker message queue capacity exhausted","QuotaExceededError");
                return;
            }
            remaining_bytes = maximum_worker_queue_bytes - state->outgoing_bytes;
        }
        clone_packet packet;
        if (!serialize_clone(info, info[0], transfer, packet, remaining_bytes)) return;
        const auto bytes = clone_packet_size(packet);
        {
            std::lock_guard lock(state->mutex);
            if (state->stopped) return;
            if (state->outgoing.size() >= maximum_worker_queue_messages
                || bytes > maximum_worker_queue_bytes - state->outgoing_bytes) {
                throw_dom_exception(info,"Worker message queue capacity exhausted","QuotaExceededError");
                return;
            }
            state->outgoing_bytes += bytes;
            state->outgoing.push_back(std::move(packet));
        }
        if (state->notify_parent) state->notify_parent();
    }
    static void worker_terminate(const v8::FunctionCallbackInfo<v8::Value>& info) {
        auto* state = worker_state_from_receiver(info);
        if (state == nullptr) return;
        state->stop();
        if (auto* self = current(info.GetIsolate()); self != nullptr)
            self->release_stopped_worker_wrapper(*state);
    }
    static void worker_close(const v8::FunctionCallbackInfo<v8::Value>& info) {
        auto* self = current(info.GetIsolate());
        if (self && self->worker_owner) {
            auto* state = self->worker_owner;
            {
                std::lock_guard lock(state->mutex);
                ++state->generation;
                state->stopped = true;
                state->incoming.clear();
                state->outgoing.clear();
                state->errors.clear();
                state->incoming_bytes = 0U;
                state->outgoing_bytes = 0U;
            }
            state->ready.notify_all();
            if (state->notify_parent) state->notify_parent();
        }
    }

    static void worker_import_scripts(const v8::FunctionCallbackInfo<v8::Value>& info) {
        auto* self = current(info.GetIsolate());
        if (!self || !self->worker_owner) return;
        if (self->worker_module_realm) {
            info.GetIsolate()->ThrowException(v8::Exception::TypeError(
                js_string(info.GetIsolate(),"importScripts is unavailable in a module worker")));
            return;
        }
        auto realm = info.GetIsolate()->GetCurrentContext();
        for (int index = 0; index < info.Length(); ++index) {
            const auto url = resolve_resource_url(
                to_utf8(info.GetIsolate(),info[index]),
                self->current_base_address());
            std::string source,resolved,error;
            if (!self->load_worker_script_resource(
                    url,
                    self->current_base_address(),
                    WEBSCENE_FETCH_MODE_SAME_ORIGIN,
                    source,
                    resolved,
                    error)
                || !self->execute_in_context(realm,source,resolved,error)) {
                info.GetIsolate()->ThrowException(v8::Exception::Error(
                    js_dom_string(info.GetIsolate(),
                        error.empty() ? "Unable to load worker script: "+url : error)));
                return;
            }
        }
    }
    bool dispatch_worker_message(v8::Local<v8::Context> realm,
        v8::Local<v8::Object> target, const clone_packet* packet, const std::string& error = {}) {
        v8::Context::Scope scope(realm);
        v8::TryCatch caught(isolate);
        auto event = v8::Object::New(isolate);
        event->Set(realm, js_string(isolate,"type"), js_string(isolate, packet ? "message" : "error")).Check();
        if (packet) {
            v8::Local<v8::Value> value;
            std::vector<v8::Local<v8::Object>> ports;
            if (!deserialize_clone(isolate, realm, *packet, &ports).ToLocal(&value)) {
                caught.Reset();
                event->Set(realm,js_string(isolate,"type"),js_string(isolate,"messageerror")).Check();
            } else {
                event->Set(realm, js_string(isolate,"data"), value).Check();
                auto port_array=v8::Array::New(isolate,static_cast<int>(ports.size()));
                for(uint32_t index=0;index<ports.size();++index)
                    port_array->Set(realm,index,ports[index]).Check();
                event->Set(realm,js_string(isolate,"ports"),port_array).Check();
            }
        } else event->Set(realm, js_string(isolate,"message"), js_dom_string(isolate,error)).Check();
        v8::Local<v8::Value> dispatch;
        if (!target->Get(realm, js_string(isolate,"dispatchEvent")).ToLocal(&dispatch) || !dispatch->IsFunction()) return false;
        v8::Local<v8::Value> args[]{event};
        if (dispatch.As<v8::Function>()->Call(realm,target,1,args).IsEmpty()) {
            last_error = describe_reported_exception(caught,realm); return false;
        }
        perform_microtask_checkpoint();
        return true;
    }
    bool has_worker_messages() const {
        for (auto& state : dedicated_workers) {
            std::lock_guard lock(state->mutex);
            if (!state->outgoing.empty() || !state->errors.empty()
                || (state->stopped && !state->wrapper_weak
                    && !state->wrapper_collected)) {
                return true;
            }
        }
        return false;
    }
    bool drain_worker_message() {
        for (auto& state : dedicated_workers) {
            std::optional<clone_packet> packet; std::string error;
            {
                std::lock_guard lock(state->mutex);
                if (!state->errors.empty()) { error = std::move(state->errors.front()); state->errors.pop_front(); }
                else if (!state->outgoing.empty()) {
                    packet = std::move(state->outgoing.front()); state->outgoing.pop_front();
                    const auto bytes=clone_packet_size(*packet);
                    state->outgoing_bytes=bytes>state->outgoing_bytes?0U:state->outgoing_bytes-bytes;
                }
                else if (!state->stopped || state->wrapper_weak
                    || state->wrapper_collected) continue;
            }
            if (!packet && error.empty()) {
                release_stopped_worker_wrapper(*state);
                reclaim_collected_worker_bindings();
                return true;
            }
            return dispatch_worker_message(state->realm.Get(isolate),state->wrapper.Get(isolate),
                packet ? &*packet : nullptr,error);
        }
        return true;
    }
    void stop_workers() { for (auto& worker : dedicated_workers) worker->stop(); }

    static void worker_construct_impl(const v8::FunctionCallbackInfo<v8::Value>& info) {
        auto* self = current(info.GetIsolate()); if (!self) return;
        auto* isolate = info.GetIsolate(); auto realm = isolate->GetCurrentContext();
        if (!info.IsConstructCall() || info.Length() < 1) {
            isolate->ThrowException(v8::Exception::TypeError(js_string(isolate,"Worker requires new and a URL"))); return;
        }
        const auto url = resolve_resource_url(to_utf8(isolate,info[0]),self->current_base_address());
        bool module = false;
        uint32_t worker_credentials = WEBSCENE_FETCH_CREDENTIALS_SAME_ORIGIN;
        std::string worker_name;
        if (info.Length() > 1 && !info[1]->IsUndefined() && !info[1]->IsNull()) {
            if (!info[1]->IsObject()) {
                isolate->ThrowException(v8::Exception::TypeError(
                    js_string(isolate,"WorkerOptions must be an object")));
                return;
            }
            v8::Local<v8::Value> type;
            if (!info[1].As<v8::Object>()->Get(realm,js_string(isolate,"type")).ToLocal(&type)) return;
            if (!type->IsUndefined()) {
                std::string text;
                if (!storage_string(isolate,type,text)) return;
                if(text!="classic"&&text!="module"){
                    isolate->ThrowException(v8::Exception::TypeError(js_string(isolate,"Invalid WorkerType")));return;
                }
                module=text=="module";
            }
            v8::Local<v8::Value> credentials;
            if (!info[1].As<v8::Object>()->Get(
                    realm,js_string(isolate,"credentials")).ToLocal(&credentials)) return;
            if (!credentials->IsUndefined()) {
                std::string text;
                if (!storage_string(isolate,credentials,text)) return;
                if (text == "omit") {
                    worker_credentials = WEBSCENE_FETCH_CREDENTIALS_OMIT;
                } else if (text == "same-origin") {
                    worker_credentials = WEBSCENE_FETCH_CREDENTIALS_SAME_ORIGIN;
                } else if (text == "include") {
                    worker_credentials = WEBSCENE_FETCH_CREDENTIALS_INCLUDE;
                } else {
                    isolate->ThrowException(v8::Exception::TypeError(
                        js_string(isolate,"Invalid Worker credentials mode")));
                    return;
                }
            }
            v8::Local<v8::Value> name;
            if(!info[1].As<v8::Object>()->Get(realm,js_string(isolate,"name")).ToLocal(&name))return;
            if(!name->IsUndefined()
                && !storage_string(isolate,name,worker_name)) return;
        }
        // Capture a same-origin object URL before entering another isolate. The
        // worker owns its source even if the parent revokes the URL immediately.
        std::optional<std::string> embedded_source;
        if (url.starts_with("blob:")) {
            const auto found = self->find_binary_object_url(
                url, self->current_security_origin());
            if (!found) {
                throw_dom_exception(info,"Worker Blob URL is unavailable for this origin","SecurityError"); return;
            }
            embedded_source = found->bytes;
        }
        if (!embedded_source
            && resource_origin(url) != self->current_security_origin()) {
            throw_dom_exception(info,"Worker script must be same origin","SecurityError"); return;
        }
        if (self->active_worker_count() >= maximum_dedicated_workers) {
            isolate->ThrowException(v8::Exception::RangeError(js_string(isolate,"Worker capacity exhausted"))); return;
        }
        if (!self->reserve_worker_binding()) {
            isolate->ThrowException(v8::Exception::RangeError(
                js_string(isolate,"Retained Worker wrapper capacity exhausted"))); return;
        }
        v8::Local<v8::Value> target_constructor;
        if (!realm->Global()->Get(realm,js_string(isolate,"EventTarget")).ToLocal(&target_constructor) || !target_constructor->IsFunction()) return;
        v8::Local<v8::Object> target;
        if (!target_constructor.As<v8::Function>()->NewInstance(realm,0,nullptr).ToLocal(&target)) return;
        v8::Local<v8::Value> prototype;
        if(info.NewTarget().As<v8::Object>()->Get(realm,js_string(isolate,"prototype")).ToLocal(&prototype)
            &&prototype->IsObject())target->SetPrototype(realm,prototype).FromMaybe(false);
        auto state = std::make_unique<worker_state>();
        auto* raw = state.get();
        raw->notify_parent = self->runtime_work_available;
        raw->wrapper.Reset(isolate,target);raw->realm.Reset(isolate,realm);
        auto data=v8::External::New(isolate,raw,v8::kExternalPointerTypeTagDefault);
        target->SetPrivate(realm,worker_state_private(isolate),data).Check();
        target->Set(realm,js_string(isolate,"postMessage"),v8::Function::New(realm,worker_post_message,{},1).ToLocalChecked()).Check();
        target->Set(realm,js_string(isolate,"terminate"),v8::Function::New(realm,worker_terminate).ToLocalChecked()).Check();
        auto loader=self->load_resource_callback;
        auto root=self->resource_root;
        auto object_url_registry=self->object_url_registry;
        auto cookie_jar=self->cookie_jar;
        auto worker_origin=self->current_security_origin();
        const auto worker_generation = raw->generation;
        self->dedicated_workers.push_back(std::move(state));
        raw->thread=std::thread([raw,url,module,worker_credentials,worker_generation,worker_origin=std::move(worker_origin),embedded_source=std::move(embedded_source),worker_name=std::move(worker_name),loader=std::move(loader),root=std::move(root),object_url_registry=std::move(object_url_registry),cookie_jar=std::move(cookie_jar)] {
            auto report_runtime_error=[&](const std::string& text) {
                {
                    std::lock_guard lock(raw->mutex);
                    if(raw->stopped || raw->generation != worker_generation)return;
                    if(raw->errors.size()>=maximum_worker_errors)raw->errors.pop_front();
                    raw->errors.push_back(text.substr(0U,maximum_worker_error_length));
                }
                if(raw->notify_parent)raw->notify_parent();
            };
            auto finish_with_error=[&](const std::string& text) {
                {
                    std::lock_guard lock(raw->mutex);
                    if (raw->stopped || raw->generation != worker_generation
                        || raw->terminal_failure_reported) return;
                    raw->terminal_failure_reported = true;
                    raw->startup_settled = true;
                    raw->stopped = true;
                    raw->incoming.clear();
                    raw->outgoing.clear();
                    raw->incoming_bytes = 0U;
                    raw->outgoing_bytes = 0U;
                    if (raw->errors.size() >= maximum_worker_errors)
                        raw->errors.pop_front();
                    raw->errors.push_back(
                        text.substr(0U,maximum_worker_error_length));
                }
                raw->ready.notify_all();
                if(raw->notify_parent)raw->notify_parent();
            };
            try {
                native_document document;
                v8_dom_runtime runtime(document,[]{return v8_dom_runtime::viewport_metrics{1,1,1,0};},{},loader);
                auto* child=runtime.impl_.get();
                child->object_url_registry = object_url_registry;
                child->cookie_jar = cookie_jar;
                child->inherited_worker_origin = worker_origin;
                child->worker_script_fetch_policy_active = true;
                child->worker_script_fetch_origin = worker_origin;
                child->worker_script_fetch_credentials = worker_credentials;
                struct running_guard {
                    worker_state* state;implementation* child;
                    ~running_guard(){
                        std::lock_guard lock(state->mutex);state->running_isolate=nullptr;
                        if(child->isolate)child->isolate->CancelTerminateExecution();
                    }
                } guard{raw,child};
                child->force_dedicated_isolate=true;child->worker_owner=raw;child->worker_owner_generation=worker_generation;child->worker_module_realm=module;
                child->runtime_work_available=[raw,worker_generation]{
                    {
                        std::lock_guard lock(raw->mutex);
                        if (raw->stopped
                            || raw->generation != worker_generation) return;
                        raw->wake_requested=true;
                    }
                    raw->ready.notify_one();
                };
                runtime.set_resource_root(root.string());
                if(!runtime.initialize())throw std::runtime_error(runtime.last_error());
                {
                    std::lock_guard lock(raw->mutex);
                    if(raw->stopped || raw->generation != worker_generation)return;
                    raw->running_isolate=child->isolate;
                }
                {
                    v8::Isolate::Scope entered(child->isolate);v8::HandleScope handles(child->isolate);
                    auto realm=child->context.Get(child->isolate);v8::Context::Scope scope(realm);
                    auto global=realm->Global();
                    child->document_base_address=url;
                    child->set_context_location(realm,global,url);
                    global->Set(realm,js_string(child->isolate,"postMessage"),v8::Function::New(realm,worker_post_parent,{},1).ToLocalChecked()).Check();
                    global->Set(realm,js_string(child->isolate,"close"),v8::Function::New(realm,worker_close).ToLocalChecked()).Check();
                    global->Set(realm,js_string(child->isolate,"importScripts"),v8::Function::New(realm,worker_import_scripts).ToLocalChecked()).Check();
                    global->Set(realm,js_string(child->isolate,"self"),global).Check();
                    global->Set(realm,js_string(child->isolate,"name"),js_dom_string(child->isolate,worker_name)).Check();
                    global->Delete(realm,js_string(child->isolate,"document")).FromMaybe(false);
                    global->Delete(realm,js_string(child->isolate,"window")).FromMaybe(false);
                    child->install_dedicated_worker_global_scope(realm);
                    std::string source,resolved,error;
                    if (embedded_source) { source = *embedded_source; resolved = url; }
                    if(!embedded_source && !child->load_worker_script_resource(
                            url,
                            child->current_base_address(),
                            WEBSCENE_FETCH_MODE_SAME_ORIGIN,
                            source,
                            resolved,
                            error)) {
                        finish_with_error(error.empty()
                            ? "Unable to load worker: "+url
                            : error);
                    } else {
                        child->document_base_address = resolved;
                        child->set_context_location(realm,global,resolved);
                        if(embedded_source) {
                            v8::Local<v8::Value> location;
                            if(global->Get(realm,js_string(child->isolate,"location")).ToLocal(&location)
                                &&location->IsObject())
                                location.As<v8::Object>()->Set(realm,js_string(child->isolate,"origin"),js_dom_string(child->isolate,worker_origin)).Check();
                            global->Set(realm,js_string(child->isolate,"origin"),js_dom_string(child->isolate,worker_origin)).Check();
                        }
                        if(!(module ? child->execute_module(realm,source,resolved,error)
                                    : child->execute_in_context(realm,source,resolved,error))) {
                            finish_with_error(error.empty()
                                ? "Unable to execute worker: "+resolved
                                : error);
                        } else {
                            std::lock_guard lock(raw->mutex);
                            if (!raw->stopped
                                && raw->generation == worker_generation) {
                                raw->startup_settled = true;
                            }
                        }
                    }
                }
                while(true) {
                    std::optional<clone_packet> packet;
                    uint64_t low_memory_generation = 0U;
                    {
                        std::unique_lock lock(raw->mutex);
                        raw->ready.wait_for(lock,runtime.recommended_idle_wait(std::chrono::milliseconds(1000)),
                            [&]{return raw->stopped||raw->wake_requested
                                ||raw->low_memory_requested>raw->low_memory_completed
                                ||!raw->incoming.empty();});
                        raw->wake_requested=false;
                        if(raw->stopped)break;
                        if (raw->low_memory_requested > raw->low_memory_completed)
                            low_memory_generation = raw->low_memory_requested;
                        if(!raw->incoming.empty()){
                            packet=std::move(raw->incoming.front());raw->incoming.pop_front();
                            const auto bytes=clone_packet_size(*packet);
                            raw->incoming_bytes=bytes>raw->incoming_bytes?0U:raw->incoming_bytes-bytes;
                        }
                    }
                    v8::Isolate::Scope entered(child->isolate);v8::HandleScope handles(child->isolate);
                    auto realm=child->context.Get(child->isolate);v8::Context::Scope scope(realm);
                    if (low_memory_generation != 0U) {
                        child->compact_retained_native_capacity();
                        child->message_port_wake->consume_reachability_change();
                        child->refresh_message_port_reachability();
                        child->isolate->LowMemoryNotification();
                        {
                            std::lock_guard lock(raw->mutex);
                            raw->low_memory_completed = std::max(
                                raw->low_memory_completed,
                                low_memory_generation);
                        }
                        raw->ready.notify_all();
                    }
                    if(packet&&!child->dispatch_worker_message(realm,realm->Global(),&*packet))report_runtime_error(child->last_error);
                    child->drain_tasks();
                    child->perform_microtask_checkpoint();
                    if (!child->promote_pending_promise_error()) report_runtime_error(child->last_error);
                }
                {std::lock_guard lock(raw->mutex);raw->running_isolate=nullptr;}
                child->isolate->CancelTerminateExecution();
            } catch(const std::exception& e) {
                {std::lock_guard lock(raw->mutex);raw->running_isolate=nullptr;}
                finish_with_error(e.what());
            }
            {
                std::lock_guard lock(raw->mutex);
                raw->running_isolate = nullptr;
                raw->stopped = true;
            }
            if (raw->notify_parent) raw->notify_parent();
        });
        info.GetReturnValue().Set(target);
    }
    static void worker_construct(const v8::FunctionCallbackInfo<v8::Value>& info) {
        try {worker_construct_impl(info);}
        catch(const std::exception& e){
            info.GetIsolate()->ThrowException(v8::Exception::Error(js_string(info.GetIsolate(),e.what())));
        }
    }
    static void worker_post_message(const v8::FunctionCallbackInfo<v8::Value>& info) {
        try {worker_post_message_impl(info);}
        catch(const std::exception& e){info.GetIsolate()->ThrowException(v8::Exception::RangeError(js_string(info.GetIsolate(),e.what())));}
    }
    static void worker_post_parent(const v8::FunctionCallbackInfo<v8::Value>& info) {
        try {worker_post_parent_impl(info);}
        catch(const std::exception& e){info.GetIsolate()->ThrowException(v8::Exception::RangeError(js_string(info.GetIsolate(),e.what())));}
    }
