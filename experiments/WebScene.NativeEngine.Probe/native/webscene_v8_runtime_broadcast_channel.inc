    static broadcast_channel_registry& shared_broadcast_channel_registry()
    {
        static broadcast_channel_registry registry;
        return registry;
    }

    static v8::Local<v8::Private> broadcast_channel_private(
        v8::Isolate* isolate)
    {
        return v8::Private::ForApi(
            isolate,
            js_string(isolate, "WebScene.BroadcastChannel"));
    }

    static v8::Local<v8::Private> broadcast_channel_handler_private(
        v8::Isolate* isolate,
        bool error_handler)
    {
        return v8::Private::ForApi(isolate, js_string(
            isolate,
            error_handler
                ? "WebScene.BroadcastChannel.onmessageerror"
                : "WebScene.BroadcastChannel.onmessage"));
    }

    broadcast_channel_binding* broadcast_channel_binding_from_object(
        v8::Local<v8::Context> realm,
        v8::Local<v8::Object> object)
    {
        v8::Local<v8::Value> value;
        if (!object->GetPrivate(
                realm, broadcast_channel_private(isolate)).ToLocal(&value)
            || !value->IsExternal()) {
            return nullptr;
        }
        auto* binding = static_cast<broadcast_channel_binding*>(
            value.As<v8::External>()->Value(
                v8::kExternalPointerTypeTagDefault));
        return binding != nullptr && !binding->reclaimed ? binding : nullptr;
    }

    static broadcast_channel_binding* require_broadcast_channel_binding(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* runtime = current(info.GetIsolate());
        auto realm = info.GetIsolate()->GetCurrentContext();
        auto* binding = runtime == nullptr || !info.This()->IsObject()
            ? nullptr
            : runtime->broadcast_channel_binding_from_object(
                realm, info.This().As<v8::Object>());
        if (binding == nullptr) {
            info.GetIsolate()->ThrowException(v8::Exception::TypeError(
                js_string(info.GetIsolate(), "Illegal invocation")));
        }
        return binding;
    }

    static bool broadcast_channel_error_handler(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        return info.Data()->BooleanValue(info.GetIsolate());
    }

    static void broadcast_channel_handler_get(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        if (require_broadcast_channel_binding(info) == nullptr) return;
        auto realm = info.GetIsolate()->GetCurrentContext();
        v8::Local<v8::Value> value;
        if (info.This().As<v8::Object>()->GetPrivate(
                realm,
                broadcast_channel_handler_private(
                    info.GetIsolate(),
                    broadcast_channel_error_handler(info))).ToLocal(&value)
            && !value->IsUndefined()) {
            info.GetReturnValue().Set(value);
        } else {
            info.GetReturnValue().Set(v8::Null(info.GetIsolate()));
        }
    }

    static void broadcast_channel_handler_set(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* binding = require_broadcast_channel_binding(info);
        if (binding == nullptr) return;
        auto* runtime = current(info.GetIsolate());
        auto realm = info.GetIsolate()->GetCurrentContext();
        const auto value = info.Length() > 0 && info[0]->IsObject()
            ? info[0]
            : v8::Local<v8::Value>(v8::Null(info.GetIsolate()));
        info.This().As<v8::Object>()->SetPrivate(
            realm,
            broadcast_channel_handler_private(
                info.GetIsolate(),
                broadcast_channel_error_handler(info)),
            value).Check();
        if (runtime != nullptr) {
            runtime->update_broadcast_channel_reachability(*binding);
        }
    }

    static void broadcast_channel_name_get(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* binding = require_broadcast_channel_binding(info);
        if (binding == nullptr || binding->endpoint == nullptr) return;
        std::lock_guard lock(binding->endpoint->mutex);
        info.GetReturnValue().Set(js_dom_string(
            info.GetIsolate(), binding->endpoint->name));
    }

    static void close_broadcast_channel_binding(
        broadcast_channel_binding& binding)
    {
        if (binding.endpoint == nullptr) return;
        std::lock_guard lock(binding.endpoint->mutex);
        binding.endpoint->closed = true;
        binding.endpoint->incoming.clear();
        binding.endpoint->incoming_bytes = 0U;
        binding.endpoint->wake.reset();
    }

    bool broadcast_channel_has_active_listener(
        broadcast_channel_binding& binding)
    {
        auto realm = binding.context.Get(isolate);
        auto wrapper = binding.wrapper.Get(isolate);
        if (realm.IsEmpty() || wrapper.IsEmpty()) return false;
        for (const auto error_handler : {false, true}) {
            v8::Local<v8::Value> handler;
            if (wrapper->GetPrivate(
                    realm,
                    broadcast_channel_handler_private(
                        isolate, error_handler)).ToLocal(&handler)
                && !handler->IsNullOrUndefined()) {
                return true;
            }
        }
        for (const auto* type : {"message", "messageerror"}) {
            const auto listeners = frame_event_listeners.find(type);
            if (listeners == frame_event_listeners.end()) continue;
            for (const auto& registration : listeners->second) {
                if (registration.target == binding.event_target_id
                    && registration.context.Get(isolate) == realm
                    && !registration.callback.IsEmpty()) {
                    return true;
                }
            }
        }
        return false;
    }

    void update_broadcast_channel_reachability(
        broadcast_channel_binding& binding)
    {
        bool open = false;
        if (binding.endpoint != nullptr) {
            std::lock_guard lock(binding.endpoint->mutex);
            open = !binding.endpoint->closed;
        }
        const auto should_root = open
            && broadcast_channel_has_active_listener(binding);
        if (should_root == binding.active_listener_rooted
            || binding.wrapper.IsEmpty()) {
            return;
        }
        if (should_root) {
            binding.wrapper.ClearWeak();
        } else {
            binding.wrapper.SetWeak(
                &binding,
                broadcast_channel_wrapper_collected,
                v8::WeakCallbackType::kParameter);
        }
        binding.active_listener_rooted = should_root;
    }

    void update_broadcast_channel_reachability_from_object(
        v8::Local<v8::Context> realm,
        v8::Local<v8::Object> object)
    {
        auto* binding = broadcast_channel_binding_from_object(realm, object);
        if (binding != nullptr) update_broadcast_channel_reachability(*binding);
    }

    void refresh_broadcast_channel_reachability()
    {
        for (const auto& binding : broadcast_channel_bindings) {
            if (binding != nullptr && binding->active_listener_rooted) {
                update_broadcast_channel_reachability(*binding);
            }
        }
    }

    static void broadcast_channel_wrapper_collected(
        const v8::WeakCallbackInfo<broadcast_channel_binding>& info)
    {
        auto* binding = info.GetParameter();
        if (binding == nullptr) return;
        close_broadcast_channel_binding(*binding);
        binding->endpoint.reset();
        binding->context.Reset();
        binding->wrapper.Reset();
        binding->event_target_id = 0U;
        binding->active_listener_rooted = false;
        binding->reclaimed = true;
    }

    static void broadcast_channel_close(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* binding = require_broadcast_channel_binding(info);
        if (binding == nullptr) return;
        close_broadcast_channel_binding(*binding);
        auto* runtime = current(info.GetIsolate());
        if (runtime != nullptr) {
            runtime->update_broadcast_channel_reachability(*binding);
        }
    }

    static void broadcast_channel_post_message(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* binding = require_broadcast_channel_binding(info);
        if (self == nullptr || binding == nullptr || binding->endpoint == nullptr) {
            return;
        }
        if (info.Length() < 1) {
            info.GetIsolate()->ThrowException(v8::Exception::TypeError(
                js_string(
                    info.GetIsolate(),
                    "BroadcastChannel.postMessage requires a message")));
            return;
        }
        {
            std::lock_guard lock(binding->endpoint->mutex);
            if (binding->endpoint->closed) {
                throw_dom_exception(
                    info,
                    "The BroadcastChannel is closed",
                    "InvalidStateError");
                return;
            }
        }

        clone_packet packet;
        const auto value = info.Length() > 0
            ? info[0]
            : v8::Local<v8::Value>(v8::Undefined(info.GetIsolate()));
        if (!serialize_clone(
                info,
                value,
                v8::Undefined(info.GetIsolate()),
                packet,
                maximum_broadcast_channel_queue_bytes)) {
            return;
        }
        const auto packet_bytes = clone_packet_size(packet);

        std::vector<std::shared_ptr<broadcast_channel_endpoint>> recipients;
        auto& registry = shared_broadcast_channel_registry();
        std::lock_guard registry_lock(registry.mutex);
        std::erase_if(registry.endpoints, [](const auto& candidate) {
            const auto endpoint = candidate.lock();
            if (endpoint == nullptr) return true;
            std::lock_guard lock(endpoint->mutex);
            return endpoint->closed;
        });
        for (const auto& candidate : registry.endpoints) {
            const auto endpoint = candidate.lock();
            if (endpoint == nullptr || endpoint == binding->endpoint) continue;
            std::lock_guard lock(endpoint->mutex);
            if (!endpoint->closed
                && endpoint->partition == binding->endpoint->partition
                && endpoint->origin == binding->endpoint->origin
                && endpoint->name == binding->endpoint->name) {
                recipients.push_back(endpoint);
            }
        }
        if (!recipients.empty()
            && packet_bytes > (64U * 1024U * 1024U) / recipients.size()) {
            throw_dom_exception(
                info,
                "BroadcastChannel fanout exceeds the bounded message size",
                "QuotaExceededError");
            return;
        }
        for (const auto& endpoint : recipients) {
            std::lock_guard lock(endpoint->mutex);
            if (endpoint->closed) continue;
            if (endpoint->incoming.size()
                    >= maximum_broadcast_channel_queue_messages
                || packet_bytes > maximum_broadcast_channel_queue_bytes
                    - endpoint->incoming_bytes) {
                throw_dom_exception(
                    info,
                    "BroadcastChannel queue capacity exhausted",
                    "QuotaExceededError");
                return;
            }
        }

        std::vector<std::shared_ptr<message_port_runtime_wake>> wakes;
        for (const auto& endpoint : recipients) {
            std::lock_guard lock(endpoint->mutex);
            if (endpoint->closed) continue;
            endpoint->incoming_bytes += packet_bytes;
            endpoint->incoming.push_back(
                broadcast_channel_message{packet, binding->endpoint->origin});
            self->broadcast_channel_queue_high_water_messages = std::max(
                self->broadcast_channel_queue_high_water_messages,
                endpoint->incoming.size());
            self->broadcast_channel_queue_high_water_bytes = std::max(
                self->broadcast_channel_queue_high_water_bytes,
                endpoint->incoming_bytes);
            if (auto wake = endpoint->wake.lock(); wake != nullptr) {
                wakes.push_back(std::move(wake));
            }
        }
        for (const auto& wake : wakes) wake->signal();
    }

    static void broadcast_channel_construct(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        if (self == nullptr) return;
        if (!info.IsConstructCall()) {
            info.GetIsolate()->ThrowException(v8::Exception::TypeError(
                js_string(info.GetIsolate(), "BroadcastChannel requires new")));
            return;
        }
        if (info.Length() < 1) {
            info.GetIsolate()->ThrowException(v8::Exception::TypeError(
                js_string(
                    info.GetIsolate(),
                    "BroadcastChannel requires a name")));
            return;
        }
        auto realm = info.GetIsolate()->GetCurrentContext();
        v8::Local<v8::String> name_value;
        if (!(info.Length() > 0 ? info[0] : v8::Undefined(info.GetIsolate()))
                ->ToString(realm).ToLocal(&name_value)) {
            return;
        }
        const auto name = to_utf8(info.GetIsolate(), name_value);
        if (name.size() > maximum_broadcast_channel_name_bytes) {
            info.GetIsolate()->ThrowException(v8::Exception::RangeError(
                js_string(info.GetIsolate(), "BroadcastChannel name is too large")));
            return;
        }
        const auto origin = self->security_origin_for_realm(realm);
        if (origin.empty() || origin == "null") {
            throw_dom_exception(
                info,
                "BroadcastChannel is unavailable for an opaque origin",
                "SecurityError");
            return;
        }

        const auto reusable = std::find_if(
            self->broadcast_channel_bindings.begin(),
            self->broadcast_channel_bindings.end(),
            [](const auto& candidate) {
                return candidate != nullptr && candidate->reclaimed;
            });
        if (reusable == self->broadcast_channel_bindings.end()
            && self->broadcast_channel_bindings.size()
                >= maximum_broadcast_channel_bindings) {
            info.GetIsolate()->ThrowException(v8::Exception::RangeError(
                js_string(
                    info.GetIsolate(),
                    "BroadcastChannel binding capacity exhausted")));
            return;
        }

        v8::Local<v8::Value> event_target_value;
        if (!realm->Global()->Get(
                realm, js_string(info.GetIsolate(), "EventTarget"))
                    .ToLocal(&event_target_value)
            || !event_target_value->IsFunction()) {
            return;
        }
        v8::Local<v8::Object> wrapper;
        if (!event_target_value.As<v8::Function>()->NewInstance(
                realm, 0, nullptr).ToLocal(&wrapper)) {
            return;
        }
        v8::Local<v8::Value> constructor;
        v8::Local<v8::Value> prototype;
        if (realm->Global()->Get(
                realm, js_string(info.GetIsolate(), "BroadcastChannel"))
                    .ToLocal(&constructor)
            && constructor->IsObject()
            && constructor.As<v8::Object>()->Get(
                realm, js_string(info.GetIsolate(), "prototype"))
                    .ToLocal(&prototype)
            && prototype->IsObject()) {
            wrapper->SetPrototype(realm, prototype).Check();
        }

        auto endpoint = std::make_shared<broadcast_channel_endpoint>();
        endpoint->partition = self->storage_partition_key;
        endpoint->origin = origin;
        endpoint->name = name;
        endpoint->wake = self->message_port_wake;

        broadcast_channel_binding* raw = nullptr;
        if (reusable != self->broadcast_channel_bindings.end()) {
            raw = reusable->get();
        } else {
            auto candidate = std::make_unique<broadcast_channel_binding>();
            raw = candidate.get();
            self->broadcast_channel_bindings.push_back(std::move(candidate));
        }
        raw->endpoint = endpoint;
        raw->context.Reset(info.GetIsolate(), realm);
        raw->wrapper.Reset(info.GetIsolate(), wrapper);
        raw->event_target_id = 0U;
        raw->active_listener_rooted = false;
        raw->reclaimed = false;
        raw->wrapper.SetWeak(
            raw,
            broadcast_channel_wrapper_collected,
            v8::WeakCallbackType::kParameter);
        wrapper->SetPrivate(
            realm,
            broadcast_channel_private(info.GetIsolate()),
            v8::External::New(
                info.GetIsolate(),
                raw,
                v8::kExternalPointerTypeTagDefault)).Check();
#if defined(WEBSCENE_NATIVE_ENGINE_GENERATED_DOM_BINDINGS)
        raw->event_target_id = generated_standalone_event_target_id(
            info.GetIsolate(), wrapper).value_or(0U);
#endif

        auto& registry = shared_broadcast_channel_registry();
        {
            std::lock_guard lock(registry.mutex);
            std::erase_if(registry.endpoints, [](const auto& candidate) {
                const auto known = candidate.lock();
                if (known == nullptr) return true;
                std::lock_guard endpoint_lock(known->mutex);
                return known->closed;
            });
            if (registry.endpoints.size() >= maximum_broadcast_channel_bindings) {
                close_broadcast_channel_binding(*raw);
                raw->endpoint.reset();
                raw->context.Reset();
                raw->wrapper.Reset();
                raw->reclaimed = true;
                info.GetIsolate()->ThrowException(v8::Exception::RangeError(
                    js_string(
                        info.GetIsolate(),
                        "BroadcastChannel process capacity exhausted")));
                return;
            }
            registry.endpoints.push_back(endpoint);
        }
        self->record_feature(
            "web-api",
            "BroadcastChannel.constructor",
            "supported",
            {},
            "web-api-binding");
        info.GetReturnValue().Set(wrapper);
    }

    void install_broadcast_channel(v8::Local<v8::Context> realm)
    {
        auto constructor = v8::Function::New(
            realm, broadcast_channel_construct, {}, 1).ToLocalChecked();
        constructor->SetName(js_string(isolate, "BroadcastChannel"));
        v8::Local<v8::Value> prototype_value;
        v8::Local<v8::Value> event_target_value;
        v8::Local<v8::Value> event_target_prototype;
        if (!constructor->Get(
                realm, js_string(isolate, "prototype")).ToLocal(&prototype_value)
            || !prototype_value->IsObject()) {
            return;
        }
        auto prototype = prototype_value.As<v8::Object>();
        if (realm->Global()->Get(
                realm, js_string(isolate, "EventTarget")).ToLocal(&event_target_value)
            && event_target_value->IsFunction()
            && event_target_value.As<v8::Object>()->Get(
                realm, js_string(isolate, "prototype"))
                    .ToLocal(&event_target_prototype)
            && event_target_prototype->IsObject()) {
            prototype->SetPrototype(realm, event_target_prototype).Check();
        }
        for (const auto& [name, error_handler] : {
                 std::pair{"onmessage", false},
                 std::pair{"onmessageerror", true}}) {
            auto data = v8::Boolean::New(isolate, error_handler);
            auto getter = v8::Function::New(
                realm, broadcast_channel_handler_get, data).ToLocalChecked();
            auto setter = v8::Function::New(
                realm, broadcast_channel_handler_set, data, 1).ToLocalChecked();
            getter->SetName(js_string(
                isolate, (std::string("get ") + name).c_str()));
            setter->SetName(js_string(
                isolate, (std::string("set ") + name).c_str()));
            prototype->SetAccessorProperty(
                js_string(isolate, name), getter, setter, v8::None);
        }
        auto name_getter = v8::Function::New(
            realm, broadcast_channel_name_get).ToLocalChecked();
        name_getter->SetName(js_string(isolate, "get name"));
        prototype->SetAccessorProperty(
            js_string(isolate, "name"), name_getter, {}, v8::None);
        auto post_message = v8::Function::New(
            realm, broadcast_channel_post_message, {}, 1).ToLocalChecked();
        post_message->SetName(js_string(isolate, "postMessage"));
        prototype->DefineOwnProperty(
            realm,
            js_string(isolate, "postMessage"),
            post_message,
            v8::DontEnum).Check();
        auto close = v8::Function::New(
            realm, broadcast_channel_close).ToLocalChecked();
        close->SetName(js_string(isolate, "close"));
        prototype->DefineOwnProperty(
            realm,
            js_string(isolate, "close"),
            close,
            v8::DontEnum).Check();
        prototype->DefineOwnProperty(
            realm,
            v8::Symbol::GetToStringTag(isolate),
            js_string(isolate, "BroadcastChannel"),
            static_cast<v8::PropertyAttribute>(v8::ReadOnly | v8::DontEnum))
                .Check();
        realm->Global()->DefineOwnProperty(
            realm,
            js_string(isolate, "BroadcastChannel"),
            constructor,
            v8::None).Check();
    }

    bool has_broadcast_channel_messages() const
    {
        for (const auto& binding : broadcast_channel_bindings) {
            if (binding == nullptr || binding->reclaimed
                || binding->endpoint == nullptr) {
                continue;
            }
            std::lock_guard lock(binding->endpoint->mutex);
            if (!binding->endpoint->incoming.empty()) return true;
        }
        return false;
    }

    bool drain_broadcast_channel_message()
    {
        for (const auto& binding : broadcast_channel_bindings) {
            if (binding == nullptr || binding->reclaimed
                || binding->endpoint == nullptr) {
                continue;
            }
            broadcast_channel_message message;
            {
                std::lock_guard lock(binding->endpoint->mutex);
                if (binding->endpoint->incoming.empty()) continue;
                message = std::move(binding->endpoint->incoming.front());
                binding->endpoint->incoming.pop_front();
                const auto bytes = clone_packet_size(message.packet);
                binding->endpoint->incoming_bytes =
                    bytes > binding->endpoint->incoming_bytes
                    ? 0U
                    : binding->endpoint->incoming_bytes - bytes;
            }

            auto realm = binding->context.Get(isolate);
            auto target = binding->wrapper.Get(isolate);
            if (realm.IsEmpty() || target.IsEmpty()) return true;
            v8::Context::Scope scope(realm);
            v8::TryCatch caught(isolate);
            v8::Local<v8::Value> value;
            const auto decoded = deserialize_clone(
                isolate, realm, message.packet).ToLocal(&value);
            if (!decoded) caught.Reset();
            auto event = create_window_event(
                realm, decoded ? "message" : "messageerror");
            if (decoded) {
                event->Set(
                    realm, js_string(isolate, "data"), value).Check();
            }
            event->Set(
                realm,
                js_string(isolate, "origin"),
                js_string(isolate, message.origin.c_str())).Check();
            event->Set(
                realm,
                js_string(isolate, "lastEventId"),
                js_string(isolate, "")).Check();
            event->Set(
                realm,
                js_string(isolate, "source"),
                v8::Null(isolate)).Check();
            event->Set(
                realm,
                js_string(isolate, "ports"),
                v8::Array::New(isolate)).Check();

            v8::Local<v8::Value> dispatch;
            if (!target->Get(
                    realm, js_string(isolate, "dispatchEvent")).ToLocal(&dispatch)
                || !dispatch->IsFunction()) {
                return true;
            }
            v8::Local<v8::Value> arguments[]{event};
            if (dispatch.As<v8::Function>()->Call(
                    realm, target, 1, arguments).IsEmpty()) {
                last_error = describe_reported_exception(caught, realm);
                return false;
            }
            update_broadcast_channel_reachability(*binding);
            perform_microtask_checkpoint();
            return true;
        }
        return true;
    }

    void retire_broadcast_channels_for_context(
        v8::Local<v8::Context> retired)
    {
        for (auto& binding : broadcast_channel_bindings) {
            if (binding == nullptr || binding->reclaimed
                || binding->context.Get(isolate) != retired) {
                continue;
            }
            close_broadcast_channel_binding(*binding);
            binding->endpoint.reset();
            binding->context.Reset();
            binding->wrapper.Reset();
            binding->event_target_id = 0U;
            binding->active_listener_rooted = false;
            binding->reclaimed = true;
        }
    }

    void stop_broadcast_channels()
    {
        for (auto& binding : broadcast_channel_bindings) {
            if (binding != nullptr && !binding->reclaimed) {
                close_broadcast_channel_binding(*binding);
            }
        }
    }

    void reset_broadcast_channels_for_navigation()
    {
        stop_broadcast_channels();
        broadcast_channel_bindings.clear();
    }
