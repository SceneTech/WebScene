    static constexpr size_t maximum_clone_packet_bytes = 16U * 1024U * 1024U;

    static size_t clone_packet_size(const clone_packet& packet)
    {
        auto size = packet.bytes.size();
        for (const auto& transfer : packet.transfers) {
            if (transfer == nullptr
                || transfer->ByteLength() > std::numeric_limits<size_t>::max() - size) {
                return std::numeric_limits<size_t>::max();
            }
            size += transfer->ByteLength();
        }
        for (const auto& handle : packet.file_system_handles) {
            if (handle == nullptr
                || handle->token.size() > std::numeric_limits<size_t>::max() - size
                || handle->name.size() > std::numeric_limits<size_t>::max()
                    - size - handle->token.size()) {
                return std::numeric_limits<size_t>::max();
            }
            size += handle->token.size() + handle->name.size();
        }
        return size;
    }

    static v8::Local<v8::Private> message_port_private(v8::Isolate* isolate)
    {
        return v8::Private::ForApi(isolate, js_string(isolate, "WebScene.MessagePort"));
    }

    static v8::Local<v8::Private> message_port_peer_private(v8::Isolate* isolate)
    {
        return v8::Private::ForApi(isolate, js_string(isolate, "WebScene.MessagePort.Peer"));
    }

    static v8::Local<v8::Private> message_port_handler_private(
        v8::Isolate* isolate,
        bool error_handler)
    {
        return v8::Private::ForApi(isolate, js_string(
            isolate,
            error_handler
                ? "WebScene.MessagePort.onmessageerror"
                : "WebScene.MessagePort.onmessage"));
    }

    static bool message_port_error_handler(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        return info.Data()->BooleanValue(info.GetIsolate());
    }

    static bool validate_message_port_handler_receiver(
        const v8::FunctionCallbackInfo<v8::Value>& info,
        implementation*& runtime)
    {
        runtime = current(info.GetIsolate());
        auto realm = info.GetIsolate()->GetCurrentContext();
        if (runtime != nullptr && info.This()->IsObject()
            && runtime->message_port_binding_from_object(
                realm, info.This().As<v8::Object>()) != nullptr) {
            return true;
        }
        info.GetIsolate()->ThrowException(v8::Exception::TypeError(
            js_string(info.GetIsolate(), "Illegal invocation")));
        return false;
    }

    static void message_port_handler_get(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        implementation* runtime = nullptr;
        if (!validate_message_port_handler_receiver(info, runtime)) return;
        auto realm = info.GetIsolate()->GetCurrentContext();
        v8::Local<v8::Value> value;
        if (info.This().As<v8::Object>()->GetPrivate(
                realm,
                message_port_handler_private(
                    info.GetIsolate(), message_port_error_handler(info))).ToLocal(&value)
            && !value->IsUndefined()) {
            info.GetReturnValue().Set(value);
        } else {
            info.GetReturnValue().Set(v8::Null(info.GetIsolate()));
        }
    }

    static void message_port_handler_set(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        implementation* runtime = nullptr;
        if (!validate_message_port_handler_receiver(info, runtime)) return;
        auto realm = info.GetIsolate()->GetCurrentContext();
        auto value = info.Length() > 0 && info[0]->IsObject()
            ? info[0]
            : v8::Local<v8::Value>(v8::Null(info.GetIsolate()));
        info.This().As<v8::Object>()->SetPrivate(
            realm,
            message_port_handler_private(
                info.GetIsolate(), message_port_error_handler(info)),
            value).Check();
        auto* binding = runtime->message_port_binding_from_object(
            realm, info.This().As<v8::Object>());
        if (binding != nullptr) {
            if (!value->IsNullOrUndefined()) binding->started = true;
            runtime->update_message_port_reachability(*binding);
        }
    }

    static void unlink_local_message_port_peer(
        v8::Isolate* isolate,
        message_port_binding& binding)
    {
        auto realm = binding.context.Get(isolate);
        auto wrapper = binding.wrapper.Get(isolate);
        if (realm.IsEmpty() || wrapper.IsEmpty()) return;
        auto key = message_port_peer_private(isolate);
        v8::Local<v8::Value> peer;
        if (wrapper->GetPrivate(realm, key).ToLocal(&peer) && peer->IsObject()) {
            peer.As<v8::Object>()->DeletePrivate(realm, key).Check();
        }
        wrapper->DeletePrivate(realm, key).Check();
    }

    message_port_binding* message_port_binding_from_object(
        v8::Local<v8::Context> realm,
        v8::Local<v8::Object> object)
    {
        v8::Local<v8::Value> value;
        if (!object->GetPrivate(realm, message_port_private(isolate)).ToLocal(&value)
            || !value->IsExternal()) {
            return nullptr;
        }
        return static_cast<message_port_binding*>(
            value.As<v8::External>()->Value(v8::kExternalPointerTypeTagDefault));
    }

    static void clear_message_port_wake(
        const std::shared_ptr<message_port_endpoint>& endpoint)
    {
        if (endpoint == nullptr || endpoint->channel == nullptr) return;
        std::lock_guard lock(endpoint->channel->mutex);
        endpoint->channel->wake[endpoint->side].reset();
    }

    static bool message_port_endpoint_closed(
        const std::shared_ptr<message_port_endpoint>& endpoint)
    {
        if (endpoint == nullptr || endpoint->channel == nullptr) return true;
        std::lock_guard lock(endpoint->channel->mutex);
        return endpoint->channel->closed[endpoint->side];
    }

    static bool message_port_endpoint_entangled(
        const std::shared_ptr<message_port_endpoint>& endpoint)
    {
        if (endpoint == nullptr || endpoint->channel == nullptr) return false;
        std::lock_guard lock(endpoint->channel->mutex);
        return !endpoint->channel->closed[endpoint->side]
            && !endpoint->channel->closed[1U - endpoint->side];
    }

    bool message_port_has_local_peer(const message_port_binding& binding) const
    {
        if (binding.endpoint == nullptr) return false;
        const auto realm = binding.context.Get(isolate);
        return std::any_of(
            message_port_bindings.begin(),
            message_port_bindings.end(),
            [&](const auto& peer) {
                return peer != nullptr && peer.get() != &binding
                    && !peer->transferred && peer->endpoint != nullptr
                    && peer->endpoint->channel == binding.endpoint->channel
                    && peer->endpoint->side != binding.endpoint->side
                    && peer->context.Get(isolate) == realm
                    && !peer->wrapper.IsEmpty();
            });
    }

    bool message_port_has_active_listener(message_port_binding& binding)
    {
        auto realm = binding.context.Get(isolate);
        auto wrapper = binding.wrapper.Get(isolate);
        if (realm.IsEmpty() || wrapper.IsEmpty()) return false;
        for (const auto error_handler : {false, true}) {
            v8::Local<v8::Value> handler;
            if (wrapper->GetPrivate(
                    realm,
                    message_port_handler_private(
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

    void update_message_port_reachability(message_port_binding& binding)
    {
        const auto should_root = binding.started && !binding.transferred
            && message_port_endpoint_entangled(binding.endpoint)
            && !message_port_has_local_peer(binding)
            && message_port_has_active_listener(binding);
        if (should_root == binding.active_listener_rooted
            || binding.wrapper.IsEmpty()) {
            return;
        }
        if (should_root) {
            binding.wrapper.ClearWeak();
        } else {
            binding.wrapper.SetWeak(
                &binding,
                message_port_wrapper_collected,
                v8::WeakCallbackType::kParameter);
        }
        binding.active_listener_rooted = should_root;
    }

    void update_message_port_reachability_from_object(
        v8::Local<v8::Context> realm,
        v8::Local<v8::Object> object)
    {
        auto* binding = message_port_binding_from_object(realm, object);
        if (binding != nullptr) update_message_port_reachability(*binding);
    }

    void refresh_message_port_reachability()
    {
        for (const auto& binding : message_port_bindings) {
            if (binding != nullptr && binding->active_listener_rooted) {
                update_message_port_reachability(*binding);
            }
        }
    }

    static void message_port_wrapper_collected(
        const v8::WeakCallbackInfo<message_port_binding>& info)
    {
        auto* binding = info.GetParameter();
        if (binding == nullptr) return;
        std::shared_ptr<message_port_runtime_wake> peer_wake;
        if (binding->endpoint != nullptr
            && binding->endpoint->channel != nullptr) {
            {
                std::lock_guard lock(binding->endpoint->channel->mutex);
                const auto side = binding->endpoint->side;
                binding->endpoint->channel->closed[side] = true;
                binding->endpoint->channel->incoming[side].clear();
                binding->endpoint->channel->incoming_bytes[side] = 0U;
                binding->endpoint->channel->wake[side].reset();
                peer_wake = binding->endpoint->channel->wake[1U - side].lock();
            }
        }
        binding->endpoint.reset();
        binding->context.Reset();
        binding->wrapper.Reset();
        binding->event_target_id = 0U;
        binding->started = false;
        binding->active_listener_rooted = false;
        binding->transferred = false;
        binding->reclaimed = true;
        if (peer_wake != nullptr) peer_wake->signal(true);
    }

    v8::MaybeLocal<v8::Object> create_message_port_wrapper(
        v8::Local<v8::Context> realm,
        std::shared_ptr<message_port_endpoint> endpoint)
    {
        if (endpoint == nullptr || endpoint->channel == nullptr) return {};
        const auto reusable = std::find_if(
            message_port_bindings.begin(),
            message_port_bindings.end(),
            [](const auto& binding) {
                return binding != nullptr && binding->reclaimed;
            });
        if (reusable == message_port_bindings.end()
            && message_port_bindings.size() >= maximum_message_port_bindings) {
            isolate->ThrowException(v8::Exception::RangeError(
                js_string(isolate, "MessagePort binding capacity exhausted")));
            return {};
        }

        auto global = realm->Global();
        v8::Local<v8::Value> event_target_value;
        if (!global->Get(realm, js_string(isolate, "EventTarget"))
                .ToLocal(&event_target_value)
            || !event_target_value->IsFunction()) {
            return {};
        }
        v8::Local<v8::Object> wrapper;
        if (!event_target_value.As<v8::Function>()->NewInstance(realm, 0, nullptr)
                .ToLocal(&wrapper)) {
            return {};
        }
        v8::Local<v8::Value> constructor_value;
        v8::Local<v8::Value> prototype_value;
        if (global->Get(realm, js_string(isolate, "MessagePort"))
                .ToLocal(&constructor_value)
            && constructor_value->IsFunction()
            && constructor_value.As<v8::Object>()->Get(
                    realm,
                    js_string(isolate, "prototype")).ToLocal(&prototype_value)
            && prototype_value->IsObject()) {
            wrapper->SetPrototype(realm, prototype_value).Check();
        }

        message_port_binding* raw = nullptr;
        if (reusable != message_port_bindings.end()) {
            raw = reusable->get();
        } else {
            auto binding = std::make_unique<message_port_binding>();
            raw = binding.get();
            message_port_bindings.push_back(std::move(binding));
        }
        raw->endpoint = std::move(endpoint);
        raw->event_target_id = 0U;
        raw->started = false;
        raw->active_listener_rooted = false;
        raw->transferred = false;
        raw->reclaimed = false;
        raw->context.Reset(isolate, realm);
        raw->wrapper.Reset(isolate, wrapper);
        raw->wrapper.SetWeak(
            raw,
            message_port_wrapper_collected,
            v8::WeakCallbackType::kParameter);

        auto external = v8::External::New(
            isolate,
            raw,
            v8::kExternalPointerTypeTagDefault);
        wrapper->SetPrivate(realm, message_port_private(isolate), external).Check();
#if defined(WEBSCENE_NATIVE_ENGINE_GENERATED_DOM_BINDINGS)
        raw->event_target_id = generated_standalone_event_target_id(
            isolate, wrapper).value_or(0U);
#endif
        wrapper->Set(
            realm,
            js_string(isolate, "postMessage"),
            v8::Function::New(realm, message_port_post_message, external, 1)
                .ToLocalChecked()).Check();
        wrapper->Set(
            realm,
            js_string(isolate, "start"),
            v8::Function::New(realm, message_port_start, external)
                .ToLocalChecked()).Check();
        wrapper->Set(
            realm,
            js_string(isolate, "close"),
            v8::Function::New(realm, message_port_close, external)
                .ToLocalChecked()).Check();
        {
            std::lock_guard lock(raw->endpoint->channel->mutex);
            raw->endpoint->channel->wake[raw->endpoint->side] = message_port_wake;
        }
        // Trace local entanglement in V8's object graph, not as permanent native
        // roots. React-style schedulers retain only the sending port; its peer
        // must remain reachable. Unreachable pairs can still be collected as a
        // cycle. Also reconnect peers after a same-isolate transfer.
        for (const auto& peer : message_port_bindings) {
            if (peer.get() == raw || peer->transferred || peer->endpoint == nullptr
                || peer->endpoint->channel != raw->endpoint->channel
                || peer->endpoint->side == raw->endpoint->side
                || message_port_endpoint_closed(peer->endpoint)
                || message_port_endpoint_closed(raw->endpoint)) continue;
            auto peer_wrapper = peer->wrapper.Get(isolate);
            if (peer_wrapper.IsEmpty()) continue;
            auto key = message_port_peer_private(isolate);
            wrapper->SetPrivate(realm, key, peer_wrapper).Check();
            peer_wrapper->SetPrivate(peer->context.Get(isolate), key, wrapper).Check();
            break;
        }
        return wrapper;
    }

    struct clone_delegate final : v8::ValueSerializer::Delegate {
        implementation* runtime;
        const v8::FunctionCallbackInfo<v8::Value>& info;
        const std::vector<message_port_binding*>& port_transfers;
        clone_packet& packet;
        bool for_storage;
        v8::ValueSerializer* serializer{};

        clone_delegate(
            implementation* runtime_value,
            const v8::FunctionCallbackInfo<v8::Value>& info_value,
            const std::vector<message_port_binding*>& port_transfer_values,
            clone_packet& packet_value,
            bool for_storage_value)
            : runtime(runtime_value)
            , info(info_value)
            , port_transfers(port_transfer_values)
            , packet(packet_value)
            , for_storage(for_storage_value)
        {
        }

        void ThrowDataCloneError(v8::Local<v8::String> message) override
        {
            const auto text = to_utf8(info.GetIsolate(), message);
            throw_dom_exception(info, text.c_str(), "DataCloneError");
        }

        bool HasCustomHostObject(v8::Isolate*) override { return true; }

        v8::Maybe<bool> IsHostObject(
            v8::Isolate* isolate,
            v8::Local<v8::Object> object) override
        {
            auto realm = isolate->GetCurrentContext();
            return v8::Just(
                runtime->message_port_binding_from_object(realm, object) != nullptr
                || runtime->file_system_handle_binding_from_object(
                    realm, object) != nullptr);
        }

        v8::Maybe<bool> WriteHostObject(
            v8::Isolate* isolate,
            v8::Local<v8::Object> object) override
        {
            const auto* binding = runtime->message_port_binding_from_object(
                isolate->GetCurrentContext(),
                object);
            if (binding != nullptr) {
                const auto found = std::find(
                    port_transfers.begin(), port_transfers.end(), binding);
                if (found == port_transfers.end()) {
                    ThrowDataCloneError(js_string(
                        isolate,
                        "MessagePort must be included in the transfer list"));
                    return v8::Nothing<bool>();
                }
                serializer->WriteUint32(0U);
                serializer->WriteUint32(static_cast<uint32_t>(
                    std::distance(port_transfers.begin(), found)));
                return v8::Just(true);
            }
            auto* handle = runtime->file_system_handle_binding_from_object(
                isolate->GetCurrentContext(), object);
            if (handle == nullptr || handle->owner == nullptr
                || handle->owner->released.load(std::memory_order_acquire)) {
                ThrowDataCloneError(js_string(
                    isolate, "File system handle authority is unavailable"));
                return v8::Nothing<bool>();
            }
            if (handle->owner->origin == "null"
                || runtime->registered_file_system_origin(
                    isolate->GetCurrentContext()) != handle->owner->origin) {
                ThrowDataCloneError(js_string(isolate,
                    "File system handles require a matching non-opaque origin"));
                return v8::Nothing<bool>();
            }
            if (for_storage
                && (runtime->storage_partition_key.empty()
                    || !runtime->file_grant_durable_request_sink)) {
                ThrowDataCloneError(js_string(isolate,
                    "Persistent file system handle authority is unavailable"));
                return v8::Nothing<bool>();
            }
            if (packet.file_system_handles.size()
                >= maximum_file_system_handle_bindings) {
                ThrowDataCloneError(js_string(
                    isolate, "Too many file system handles in structured clone"));
                return v8::Nothing<bool>();
            }
            packet.file_system_handles.push_back(handle->owner);
            serializer->WriteUint32(1U);
            serializer->WriteUint32(static_cast<uint32_t>(
                packet.file_system_handles.size() - 1U));
            return v8::Just(true);
        }
    };

    struct clone_deserialize_delegate final : v8::ValueDeserializer::Delegate {
        implementation* runtime;
        v8::Local<v8::Context> realm;
        const clone_packet& packet;
        v8::ValueDeserializer* deserializer{};
        std::vector<v8::Global<v8::Object>> wrappers;
        std::vector<v8::Global<v8::Object>> file_system_wrappers;

        clone_deserialize_delegate(
            implementation* runtime_value,
            v8::Local<v8::Context> realm_value,
            const clone_packet& packet_value)
            : runtime(runtime_value)
            , realm(realm_value)
            , packet(packet_value)
            , wrappers(packet.port_transfers.size())
            , file_system_wrappers(packet.file_system_handles.size())
        {
        }

        v8::MaybeLocal<v8::Object> file_system_wrapper(uint32_t index)
        {
            if (index >= packet.file_system_handles.size()) return {};
            if (!file_system_wrappers[index].IsEmpty()) {
                return file_system_wrappers[index].Get(runtime->isolate);
            }
            v8::Local<v8::Object> value;
            if (!runtime->create_file_system_handle_wrapper(
                    realm, packet.file_system_handles[index]).ToLocal(&value)) {
                return {};
            }
            file_system_wrappers[index].Reset(runtime->isolate, value);
            return value;
        }

        v8::MaybeLocal<v8::Object> wrapper(uint32_t index)
        {
            if (index >= packet.port_transfers.size()) return {};
            if (!wrappers[index].IsEmpty()) {
                return wrappers[index].Get(runtime->isolate);
            }
            v8::Local<v8::Object> value;
            if (!runtime->create_message_port_wrapper(
                    realm,
                    packet.port_transfers[index]).ToLocal(&value)) {
                return {};
            }
            wrappers[index].Reset(runtime->isolate, value);
            return value;
        }

        v8::MaybeLocal<v8::Object> ReadHostObject(v8::Isolate*) override
        {
            uint32_t kind = 0U;
            uint32_t index = 0U;
            if (!deserializer->ReadUint32(&kind)
                || !deserializer->ReadUint32(&index)) return {};
            if (kind == 0U) return wrapper(index);
            if (kind == 1U) return file_system_wrapper(index);
            return {};
        }
    };

    static bool serialize_clone_impl(
        const v8::FunctionCallbackInfo<v8::Value>& info,
        v8::Local<v8::Value> value,
        v8::Local<v8::Value> transfer,
        clone_packet& packet,
        size_t maximum_bytes = maximum_clone_packet_bytes,
        bool for_storage = false)
    {
        auto* self = current(info.GetIsolate());
        auto* isolate = info.GetIsolate();
        auto realm = isolate->GetCurrentContext();
        std::vector<v8::Local<v8::ArrayBuffer>> buffers;
        std::vector<message_port_binding*> ports;
        if (!transfer->IsUndefined()) {
            if (!transfer->IsArray()) {
                isolate->ThrowException(v8::Exception::TypeError(
                    js_string(isolate, "transfer must be an array")));
                return false;
            }
            auto list = transfer.As<v8::Array>();
            for (uint32_t index = 0; index < list->Length(); ++index) {
                v8::Local<v8::Value> candidate;
                if (!list->Get(realm, index).ToLocal(&candidate)) return false;
                if (candidate->IsArrayBuffer()) {
                    auto buffer = candidate.As<v8::ArrayBuffer>();
                    if (!buffer->IsDetachable() || buffer->WasDetached()
                        || std::find(buffers.begin(), buffers.end(), buffer) != buffers.end()) {
                        throw_dom_exception(
                            info,
                            "Invalid or duplicate transferable",
                            "DataCloneError");
                        return false;
                    }
                    buffers.push_back(buffer);
                    continue;
                }
                auto* port = candidate->IsObject() && self != nullptr
                    ? self->message_port_binding_from_object(
                        realm,
                        candidate.As<v8::Object>())
                    : nullptr;
                if (port == nullptr || port->transferred
                    || message_port_endpoint_closed(port->endpoint)
                    || std::find(ports.begin(), ports.end(), port) != ports.end()) {
                    throw_dom_exception(
                        info,
                        "Invalid, duplicate, or unsupported transferable",
                        "DataCloneError");
                    return false;
                }
                ports.push_back(port);
            }
        }

        clone_delegate delegate(self, info, ports, packet, for_storage);
        v8::ValueSerializer serializer(isolate, &delegate);
        delegate.serializer = &serializer;
        for (uint32_t index = 0; index < buffers.size(); ++index) {
            serializer.TransferArrayBuffer(index, buffers[index]);
        }
        serializer.WriteHeader();
        if (!serializer.WriteValue(realm, value).FromMaybe(false)) return false;
        auto [data, size] = serializer.Release();
        std::unique_ptr<uint8_t, decltype(&std::free)> memory(data, std::free);
        packet.bytes.assign(data, data + size);
        for (auto buffer : buffers) packet.transfers.push_back(buffer->GetBackingStore());
        for (auto* port : ports) packet.port_transfers.push_back(port->endpoint);
        if (clone_packet_size(packet) > maximum_bytes) {
            packet = {};
            throw_dom_exception(
                info,
                "Structured clone exceeds the bounded message size",
                "QuotaExceededError");
            return false;
        }

        // Serialization and quota checks must succeed before sender ownership
        // changes. This preserves all transferables when cloning is rejected.
        for (auto buffer : buffers) {
            if (!buffer->Detach({}).FromMaybe(false)) return false;
        }
        for (auto* port : ports) {
            unlink_local_message_port_peer(info.GetIsolate(), *port);
            clear_message_port_wake(port->endpoint);
            const auto channel = port->endpoint->channel;
            port->started = false;
            port->transferred = true;
            self->update_message_port_reachability(*port);
            for (const auto& peer : self->message_port_bindings) {
                if (peer != nullptr && peer.get() != port
                    && peer->endpoint != nullptr
                    && peer->endpoint->channel == channel) {
                    self->update_message_port_reachability(*peer);
                }
            }
            port->endpoint.reset();
            port->context.Reset();
        }
        return true;
    }

    static bool serialize_clone(
        const v8::FunctionCallbackInfo<v8::Value>& info,
        v8::Local<v8::Value> value,
        v8::Local<v8::Value> transfer,
        clone_packet& packet,
        size_t maximum_bytes = maximum_clone_packet_bytes,
        bool for_storage = false)
    {
        try {
            return serialize_clone_impl(
                info, value, transfer, packet, maximum_bytes, for_storage);
        } catch (const std::exception& exception) {
            info.GetIsolate()->ThrowException(v8::Exception::RangeError(
                js_string(info.GetIsolate(), exception.what())));
            return false;
        }
    }

    v8::MaybeLocal<v8::Value> deserialize_clone(
        v8::Isolate* isolate_value,
        v8::Local<v8::Context> realm,
        const clone_packet& packet,
        std::vector<v8::Local<v8::Object>>* transferred_ports = nullptr)
    {
        clone_deserialize_delegate delegate(this, realm, packet);
        v8::ValueDeserializer deserializer(
            isolate_value,
            packet.bytes.data(),
            packet.bytes.size(),
            &delegate);
        delegate.deserializer = &deserializer;
        for (uint32_t index = 0; index < packet.transfers.size(); ++index) {
            deserializer.TransferArrayBuffer(
                index,
                v8::ArrayBuffer::New(isolate_value, packet.transfers[index]));
        }
        if (!deserializer.ReadHeader(realm).FromMaybe(false)) return {};
        v8::Local<v8::Value> value;
        if (!deserializer.ReadValue(realm).ToLocal(&value)) return {};
        if (transferred_ports != nullptr) {
            transferred_ports->reserve(packet.port_transfers.size());
            for (uint32_t index = 0; index < packet.port_transfers.size(); ++index) {
                v8::Local<v8::Object> port;
                if (!delegate.wrapper(index).ToLocal(&port)) return {};
                transferred_ports->push_back(port);
            }
        }
        return value;
    }

    static void structured_clone(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        if (info.Length() == 0) {
            info.GetIsolate()->ThrowException(v8::Exception::TypeError(
                js_string(info.GetIsolate(), "structuredClone requires a value")));
            return;
        }
        auto realm = info.GetIsolate()->GetCurrentContext();
        if (info.Length() > 1 && !info[1]->IsNullOrUndefined()
            && !info[1]->IsObject()) {
            info.GetIsolate()->ThrowException(v8::Exception::TypeError(
                js_string(info.GetIsolate(), "StructuredSerializeOptions must be a dictionary")));
            return;
        }
        v8::Local<v8::Value> transfer = v8::Undefined(info.GetIsolate());
        if (info.Length() > 1 && info[1]->IsObject()
            && !info[1].As<v8::Object>()->Get(
                    realm,
                    js_string(info.GetIsolate(), "transfer")).ToLocal(&transfer)) {
            return;
        }
        clone_packet packet;
        if (!serialize_clone(info, info[0], transfer, packet)) return;
        v8::Local<v8::Value> result;
        if (current(info.GetIsolate())->deserialize_clone(
                info.GetIsolate(), realm, packet).ToLocal(&result)) {
            info.GetReturnValue().Set(result);
        } else if (!info.GetIsolate()->HasPendingException()) {
            throw_dom_exception(
                info,
                "File system handle could not be deserialized in this origin",
                "DataCloneError");
        }
    }

    static message_port_binding* message_port_binding_from_callback(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        if (!info.Data()->IsExternal()) return nullptr;
        return static_cast<message_port_binding*>(
            info.Data().As<v8::External>()->Value(
                v8::kExternalPointerTypeTagDefault));
    }

    static bool transfer_list_contains(
        v8::Local<v8::Context> realm,
        v8::Local<v8::Value> transfer,
        v8::Local<v8::Object> value)
    {
        if (!transfer->IsArray()) return false;
        auto list = transfer.As<v8::Array>();
        for (uint32_t index = 0; index < list->Length(); ++index) {
            v8::Local<v8::Value> candidate;
            if (list->Get(realm, index).ToLocal(&candidate)
                && candidate->IsObject()
                && candidate.As<v8::Object>()->StrictEquals(value)) {
                return true;
            }
        }
        return false;
    }

    static void message_port_post_message(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* binding = message_port_binding_from_callback(info);
        if (self == nullptr || binding == nullptr || binding->transferred
            || binding->endpoint == nullptr || info.Length() < 1) {
            return;
        }

        auto realm = info.GetIsolate()->GetCurrentContext();
        v8::Local<v8::Value> transfer = v8::Undefined(info.GetIsolate());
        if (info.Length() > 1) {
            if (info[1]->IsObject() && !info[1]->IsArray()) {
                if (!info[1].As<v8::Object>()->Get(
                        realm,
                        js_string(info.GetIsolate(), "transfer")).ToLocal(&transfer)) {
                    return;
                }
            } else {
                transfer = info[1];
            }
        }
        if (transfer_list_contains(realm, transfer, info.This())) {
            throw_dom_exception(
                info,
                "A MessagePort cannot transfer itself",
                "DataCloneError");
            return;
        }

        auto endpoint = binding->endpoint;
        const auto destination = 1U - endpoint->side;
        size_t remaining_bytes = 0U;
        {
            std::lock_guard lock(endpoint->channel->mutex);
            if (endpoint->channel->closed[endpoint->side]
                || endpoint->channel->closed[destination]) {
                return;
            }
            if (endpoint->channel->incoming[destination].size()
                    >= maximum_message_port_queue_messages
                || endpoint->channel->incoming_bytes[destination]
                    >= maximum_message_port_queue_bytes) {
                throw_dom_exception(
                    info,
                    "MessagePort queue capacity exhausted",
                    "QuotaExceededError");
                return;
            }
            remaining_bytes = maximum_message_port_queue_bytes
                - endpoint->channel->incoming_bytes[destination];
        }

        clone_packet packet;
        if (!serialize_clone(info, info[0], transfer, packet, remaining_bytes)) return;
        const auto packet_bytes = clone_packet_size(packet);
        std::shared_ptr<message_port_runtime_wake> wake;
        {
            std::lock_guard lock(endpoint->channel->mutex);
            if (endpoint->channel->closed[endpoint->side]
                || endpoint->channel->closed[destination]) {
                return;
            }
            if (endpoint->channel->incoming[destination].size()
                    >= maximum_message_port_queue_messages
                || packet_bytes > maximum_message_port_queue_bytes
                    - endpoint->channel->incoming_bytes[destination]) {
                throw_dom_exception(
                    info,
                    "MessagePort queue capacity exhausted",
                    "QuotaExceededError");
                return;
            }
            endpoint->channel->incoming_bytes[destination] += packet_bytes;
            endpoint->channel->incoming[destination].push_back(std::move(packet));
            self->message_port_queue_high_water_messages = std::max(
                self->message_port_queue_high_water_messages,
                endpoint->channel->incoming[destination].size());
            self->message_port_queue_high_water_bytes = std::max(
                self->message_port_queue_high_water_bytes,
                endpoint->channel->incoming_bytes[destination]);
            wake = endpoint->channel->wake[destination].lock();
        }
        if (wake != nullptr) wake->signal();
    }

    static void message_port_start(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* binding = message_port_binding_from_callback(info);
        if (self != nullptr && binding != nullptr && !binding->transferred
            && binding->endpoint != nullptr) {
            binding->started = true;
            self->update_message_port_reachability(*binding);
            self->message_port_wake->signal();
        }
    }

    static void close_message_port_binding(message_port_binding& binding)
    {
        if (binding.transferred || binding.endpoint == nullptr
            || binding.endpoint->channel == nullptr) {
            return;
        }
        const auto& endpoint = binding.endpoint;
        std::shared_ptr<message_port_runtime_wake> peer_wake;
        {
            std::lock_guard lock(endpoint->channel->mutex);
            endpoint->channel->closed[endpoint->side] = true;
            endpoint->channel->incoming[endpoint->side].clear();
            endpoint->channel->incoming_bytes[endpoint->side] = 0U;
            endpoint->channel->wake[endpoint->side].reset();
            peer_wake = endpoint->channel->wake[1U - endpoint->side].lock();
        }
        if (peer_wake != nullptr) peer_wake->signal(true);
    }

    static void message_port_close(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* binding = message_port_binding_from_callback(info);
        if (binding != nullptr) {
            const auto channel = binding->endpoint == nullptr
                ? std::shared_ptr<message_port_channel>{}
                : binding->endpoint->channel;
            unlink_local_message_port_peer(info.GetIsolate(), *binding);
            close_message_port_binding(*binding);
            binding->started = false;
            if (self != nullptr) self->update_message_port_reachability(*binding);
            if (self != nullptr && channel != nullptr) {
                for (const auto& peer : self->message_port_bindings) {
                    if (peer != nullptr && peer.get() != binding
                        && peer->endpoint != nullptr
                        && peer->endpoint->channel == channel) {
                        self->update_message_port_reachability(*peer);
                    }
                }
            }
        }
    }

    static void message_port_construct(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        info.GetIsolate()->ThrowException(v8::Exception::TypeError(
            js_string(info.GetIsolate(), "Illegal constructor")));
    }

    static void message_channel_construct(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        if (self == nullptr) return;
        if (!info.IsConstructCall()) {
            info.GetIsolate()->ThrowException(v8::Exception::TypeError(
                js_string(info.GetIsolate(), "MessageChannel requires new")));
            return;
        }
        const auto available_bindings = static_cast<size_t>(std::count_if(
            self->message_port_bindings.begin(),
            self->message_port_bindings.end(),
            [](const auto& binding) {
                return binding != nullptr && binding->reclaimed;
            }));
        if (self->message_port_bindings.size() - available_bindings + 2U
            > maximum_message_port_bindings) {
            info.GetIsolate()->ThrowException(v8::Exception::RangeError(
                js_string(info.GetIsolate(), "MessagePort binding capacity exhausted")));
            return;
        }
        auto channel = std::make_shared<message_port_channel>();
        auto first = std::make_shared<message_port_endpoint>();
        first->channel = channel;
        first->side = 0U;
        auto second = std::make_shared<message_port_endpoint>();
        second->channel = std::move(channel);
        second->side = 1U;
        auto realm = info.GetIsolate()->GetCurrentContext();
        v8::Local<v8::Object> port1;
        v8::Local<v8::Object> port2;
        if (!self->create_message_port_wrapper(realm, std::move(first)).ToLocal(&port1)
            || !self->create_message_port_wrapper(realm, std::move(second)).ToLocal(&port2)) {
            return;
        }
        info.This()->Set(realm, js_string(info.GetIsolate(), "port1"), port1).Check();
        info.This()->Set(realm, js_string(info.GetIsolate(), "port2"), port2).Check();
        info.GetReturnValue().Set(info.This());
    }

    void install_message_channel(v8::Local<v8::Context> realm)
    {
        auto global = realm->Global();
        auto port_constructor = v8::Function::New(
            realm,
            message_port_construct).ToLocalChecked();
        auto channel_constructor = v8::Function::New(
            realm,
            message_channel_construct).ToLocalChecked();

        v8::Local<v8::Value> event_target_value;
        v8::Local<v8::Value> event_target_prototype;
        v8::Local<v8::Value> port_prototype;
        if (global->Get(realm, js_string(isolate, "EventTarget"))
                .ToLocal(&event_target_value)
            && event_target_value->IsFunction()
            && event_target_value.As<v8::Object>()->Get(
                    realm,
                    js_string(isolate, "prototype")).ToLocal(&event_target_prototype)
            && port_constructor->Get(
                    realm,
                    js_string(isolate, "prototype")).ToLocal(&port_prototype)
            && event_target_prototype->IsObject()
            && port_prototype->IsObject()) {
            port_prototype.As<v8::Object>()->SetPrototype(
                realm,
                event_target_prototype).Check();
        }
        if (!port_prototype.IsEmpty() && port_prototype->IsObject()) {
            for (const auto& [name, error_handler] : {
                     std::pair{"onmessage", false},
                     std::pair{"onmessageerror", true}}) {
                auto data = v8::Boolean::New(isolate, error_handler);
                auto getter = v8::Function::New(
                    realm, message_port_handler_get, data).ToLocalChecked();
                auto setter = v8::Function::New(
                    realm, message_port_handler_set, data, 1).ToLocalChecked();
                getter->SetName(js_string(
                    isolate, (std::string("get ") + name).c_str()));
                setter->SetName(js_string(
                    isolate, (std::string("set ") + name).c_str()));
                port_prototype.As<v8::Object>()->SetAccessorProperty(
                    js_string(isolate, name), getter, setter, v8::None);
            }
        }
        global->DefineOwnProperty(
            realm,
            js_string(isolate, "MessagePort"),
            port_constructor,
            v8::None).Check();
        global->DefineOwnProperty(
            realm,
            js_string(isolate, "MessageChannel"),
            channel_constructor,
            v8::None).Check();
    }

    bool has_message_port_messages() const
    {
        for (const auto& binding : message_port_bindings) {
            if (binding == nullptr || binding->transferred
                || binding->endpoint == nullptr) {
                continue;
            }
            std::lock_guard lock(binding->endpoint->channel->mutex);
            if (!binding->endpoint->channel->incoming[binding->endpoint->side].empty()) {
                return true;
            }
        }
        return false;
    }

    bool drain_message_port_message()
    {
        for (const auto& binding : message_port_bindings) {
            if (binding == nullptr || binding->transferred
                || binding->endpoint == nullptr) {
                continue;
            }
            clone_packet packet;
            {
                std::lock_guard lock(binding->endpoint->channel->mutex);
                auto& queue = binding->endpoint->channel->incoming[
                    binding->endpoint->side];
                if (queue.empty()) continue;
                packet = std::move(queue.front());
                queue.pop_front();
                const auto bytes = clone_packet_size(packet);
                auto& queued_bytes = binding->endpoint->channel->incoming_bytes[
                    binding->endpoint->side];
                queued_bytes = bytes > queued_bytes ? 0U : queued_bytes - bytes;
            }

            auto realm = binding->context.Get(isolate);
            auto target = binding->wrapper.Get(isolate);
            if (realm.IsEmpty() || target.IsEmpty()) return true;
            v8::Context::Scope scope(realm);
            v8::TryCatch caught(isolate);
            std::vector<v8::Local<v8::Object>> ports;
            v8::Local<v8::Value> value;
            const auto decoded = deserialize_clone(
                isolate,
                realm,
                packet,
                &ports).ToLocal(&value);
            if (!decoded) caught.Reset();
            auto event = v8::Object::New(isolate);
            event->Set(
                realm,
                js_string(isolate, "type"),
                js_string(isolate, decoded ? "message" : "messageerror")).Check();
            if (decoded) {
                event->Set(realm, js_string(isolate, "data"), value).Check();
            }
            auto port_array = v8::Array::New(isolate, static_cast<int>(ports.size()));
            for (uint32_t index = 0; index < ports.size(); ++index) {
                port_array->Set(realm, index, ports[index]).Check();
            }
            event->Set(realm, js_string(isolate, "ports"), port_array).Check();
            event->Set(realm, js_string(isolate, "origin"), js_string(isolate, "")).Check();
            event->Set(realm, js_string(isolate, "lastEventId"), js_string(isolate, "")).Check();

            v8::Local<v8::Value> dispatch;
            if (!target->Get(realm, js_string(isolate, "dispatchEvent")).ToLocal(&dispatch)
                || !dispatch->IsFunction()) {
                return true;
            }
            v8::Local<v8::Value> arguments[]{event};
            if (dispatch.As<v8::Function>()->Call(
                    realm,
                    target,
                    1,
                    arguments).IsEmpty()) {
                last_error = describe_reported_exception(caught, realm);
                return false;
            }
            update_message_port_reachability(*binding);
            perform_microtask_checkpoint();
            return true;
        }
        return true;
    }

    void stop_message_ports()
    {
        if (message_port_wake != nullptr) message_port_wake->stop();
        for (auto& binding : message_port_bindings) {
            if (binding != nullptr) close_message_port_binding(*binding);
        }
    }

    void reset_message_ports_for_navigation()
    {
        for (auto& binding : message_port_bindings) {
            if (binding != nullptr) close_message_port_binding(*binding);
        }
        message_port_bindings.clear();
        pending_window_messages.clear();
        pending_window_message_bytes = 0U;
    }
