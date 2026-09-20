    static v8::Local<v8::Private> abort_signal_brand_private(
        v8::Isolate* isolate)
    {
        return v8::Private::ForApi(
            isolate, js_string(isolate, "WebScene.AbortSignal.brand"));
    }

    static v8::Local<v8::Private> abort_signal_state_private(
        v8::Isolate* isolate)
    {
        return v8::Private::ForApi(
            isolate, js_string(isolate, "WebScene.AbortSignal.state"));
    }

    static v8::Local<v8::Private> abort_controller_signal_private(
        v8::Isolate* isolate)
    {
        return v8::Private::ForApi(
            isolate, js_string(isolate, "WebScene.AbortController.signal"));
    }

    static bool is_abort_signal(
        v8::Local<v8::Context> realm,
        v8::Local<v8::Value> value)
    {
        if (!value->IsObject()) return false;
        v8::Local<v8::Value> brand;
        return value.As<v8::Object>()->GetPrivate(
                realm, abort_signal_brand_private(v8::Isolate::GetCurrent()))
                    .ToLocal(&brand)
            && brand->IsTrue();
    }

    static v8::MaybeLocal<v8::Object> abort_signal_state(
        v8::Local<v8::Context> realm,
        v8::Local<v8::Value> value)
    {
        if (!is_abort_signal(realm, value)) return {};
        v8::Local<v8::Value> state;
        if (!value.As<v8::Object>()->GetPrivate(
                realm, abort_signal_state_private(v8::Isolate::GetCurrent()))
                    .ToLocal(&state)
            || !state->IsObject()) {
            return {};
        }
        return state.As<v8::Object>();
    }

    static v8::Local<v8::Value> abort_dom_exception(
        v8::Local<v8::Context> realm,
        const char* message,
        const char* name)
    {
        auto* isolate = v8::Isolate::GetCurrent();
        v8::Local<v8::Value> constructor;
        if (realm->Global()->Get(
                realm, js_string(isolate, "DOMException")).ToLocal(&constructor)
            && constructor->IsFunction()) {
            v8::Local<v8::Value> arguments[]{
                js_string(isolate, message), js_string(isolate, name)};
            v8::Local<v8::Object> exception;
            if (constructor.As<v8::Function>()->NewInstance(
                    realm, 2, arguments).ToLocal(&exception)) {
                return exception;
            }
        }
        auto exception = v8::Exception::Error(js_string(isolate, message));
        if (exception->IsObject()) {
            exception.As<v8::Object>()->Set(
                realm, js_string(isolate, "name"), js_string(isolate, name)).Check();
        }
        return exception;
    }

    v8::MaybeLocal<v8::Object> create_abort_signal(
        v8::Local<v8::Context> realm)
    {
        auto* local_isolate = v8::Isolate::GetCurrent();
        v8::Local<v8::Value> event_target_value;
        if (!realm->Global()->Get(
                realm, js_string(local_isolate, "EventTarget"))
                    .ToLocal(&event_target_value)
            || !event_target_value->IsFunction()) {
            return {};
        }
        v8::Local<v8::Object> signal;
        if (!event_target_value.As<v8::Function>()->NewInstance(
                realm, 0, nullptr).ToLocal(&signal)) {
            return {};
        }
        v8::Local<v8::Value> signal_constructor;
        v8::Local<v8::Value> signal_prototype;
        if (!realm->Global()->Get(
                realm, js_string(local_isolate, "AbortSignal"))
                    .ToLocal(&signal_constructor)
            || !signal_constructor->IsFunction()
            || !signal_constructor.As<v8::Object>()->Get(
                realm, js_string(local_isolate, "prototype"))
                    .ToLocal(&signal_prototype)
            || !signal_prototype->IsObject()) {
            return {};
        }
        signal->SetPrototype(realm, signal_prototype).Check();
        auto state = v8::Object::New(local_isolate);
        state->Set(
            realm, js_string(local_isolate, "aborted"),
            v8::False(local_isolate)).Check();
        state->Set(
            realm, js_string(local_isolate, "reason"),
            v8::Undefined(local_isolate)).Check();
        signal->SetPrivate(
            realm, abort_signal_brand_private(local_isolate),
            v8::True(local_isolate)).Check();
        signal->SetPrivate(
            realm, abort_signal_state_private(local_isolate), state).Check();
        signal->Set(
            realm, js_string(local_isolate, "onabort"),
            v8::Null(local_isolate)).Check();
        return signal;
    }

    static bool abort_signal_is_aborted(
        v8::Local<v8::Context> realm,
        v8::Local<v8::Object> signal)
    {
        v8::Local<v8::Object> state;
        if (!abort_signal_state(realm, signal).ToLocal(&state)) return false;
        v8::Local<v8::Value> aborted;
        return state->Get(
                realm, js_string(v8::Isolate::GetCurrent(), "aborted"))
                    .ToLocal(&aborted)
            && aborted->BooleanValue(v8::Isolate::GetCurrent());
    }

    static v8::Local<v8::Value> abort_signal_reason(
        v8::Local<v8::Context> realm,
        v8::Local<v8::Object> signal)
    {
        v8::Local<v8::Object> state;
        v8::Local<v8::Value> reason;
        if (abort_signal_state(realm, signal).ToLocal(&state)
            && state->Get(
                realm, js_string(v8::Isolate::GetCurrent(), "reason"))
                    .ToLocal(&reason)) {
            return reason;
        }
        return v8::Undefined(v8::Isolate::GetCurrent());
    }

    bool abort_signal_abort_object(
        v8::Local<v8::Context> realm,
        v8::Local<v8::Object> signal,
        v8::Local<v8::Value> reason)
    {
        v8::Local<v8::Object> state;
        if (!abort_signal_state(realm, signal).ToLocal(&state)
            || abort_signal_is_aborted(realm, signal)) {
            return false;
        }
        state->Set(
            realm, js_string(isolate, "aborted"), v8::True(isolate)).Check();
        state->Set(realm, js_string(isolate, "reason"), reason).Check();

        const auto remove_aborted = [&](auto& registrations) {
            for (auto& registration : registrations) {
                if (registration.signal.IsEmpty()) continue;
                auto registered_signal = registration.signal.Get(isolate);
                if (registered_signal->StrictEquals(signal)) {
                    clear_event_listener(registration);
                }
            }
        };
        for (auto& [type, registrations] : frame_event_listeners) {
            static_cast<void>(type);
            remove_aborted(registrations);
        }
        remove_aborted(resize_listeners);
        refresh_message_port_reachability();
        refresh_broadcast_channel_reachability();

        auto event = create_event_instance(realm);
        event->Set(
            realm, js_string(isolate, "type"), js_string(isolate, "abort")).Check();
        event->Set(
            realm, js_string(isolate, "bubbles"), v8::False(isolate)).Check();
        event->Set(
            realm, js_string(isolate, "cancelable"), v8::False(isolate)).Check();
        v8::Local<v8::Value> dispatcher;
        if (signal->Get(
                realm, js_string(isolate, "dispatchEvent")).ToLocal(&dispatcher)
            && dispatcher->IsFunction()) {
            v8::Local<v8::Value> arguments[]{event};
            static_cast<void>(dispatcher.As<v8::Function>()->Call(
                realm, signal, 1, arguments));
        }
        return true;
    }

    static v8::Local<v8::Value> default_abort_reason(
        v8::Local<v8::Context> realm)
    {
        return abort_dom_exception(
            realm, "This operation was aborted", "AbortError");
    }

    static void abort_signal_constructor(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        info.GetIsolate()->ThrowException(v8::Exception::TypeError(
            js_string(info.GetIsolate(), "Illegal constructor")));
    }

    static void abort_signal_aborted_get(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto realm = info.GetIsolate()->GetCurrentContext();
        v8::Local<v8::Object> state;
        if (!abort_signal_state(realm, info.This()).ToLocal(&state)) {
            info.GetIsolate()->ThrowException(v8::Exception::TypeError(
                js_string(info.GetIsolate(), "Illegal invocation")));
            return;
        }
        v8::Local<v8::Value> value;
        if (state->Get(
                realm, js_string(info.GetIsolate(), "aborted")).ToLocal(&value)) {
            info.GetReturnValue().Set(value);
        }
    }

    static void abort_signal_reason_get(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto realm = info.GetIsolate()->GetCurrentContext();
        if (!is_abort_signal(realm, info.This())) {
            info.GetIsolate()->ThrowException(v8::Exception::TypeError(
                js_string(info.GetIsolate(), "Illegal invocation")));
            return;
        }
        info.GetReturnValue().Set(abort_signal_reason(realm, info.This()));
    }

    static void abort_signal_throw_if_aborted(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto realm = info.GetIsolate()->GetCurrentContext();
        if (!is_abort_signal(realm, info.This())) {
            info.GetIsolate()->ThrowException(v8::Exception::TypeError(
                js_string(info.GetIsolate(), "Illegal invocation")));
            return;
        }
        if (abort_signal_is_aborted(realm, info.This())) {
            info.GetIsolate()->ThrowException(
                abort_signal_reason(realm, info.This()));
        }
    }

    static void abort_controller_signal_get(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto realm = info.GetIsolate()->GetCurrentContext();
        v8::Local<v8::Value> signal;
        if (!info.This()->GetPrivate(
                realm, abort_controller_signal_private(info.GetIsolate()))
                    .ToLocal(&signal)
            || !is_abort_signal(realm, signal)) {
            info.GetIsolate()->ThrowException(v8::Exception::TypeError(
                js_string(info.GetIsolate(), "Illegal invocation")));
            return;
        }
        info.GetReturnValue().Set(signal);
    }

    static void abort_controller_abort(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        if (self == nullptr) return;
        auto realm = info.GetIsolate()->GetCurrentContext();
        v8::Local<v8::Value> signal_value;
        if (!info.This()->GetPrivate(
                realm, abort_controller_signal_private(info.GetIsolate()))
                    .ToLocal(&signal_value)
            || !is_abort_signal(realm, signal_value)) {
            info.GetIsolate()->ThrowException(v8::Exception::TypeError(
                js_string(info.GetIsolate(), "Illegal invocation")));
            return;
        }
        const auto default_reason = info.Length() == 0 || info[0]->IsUndefined();
        self->abort_signal_abort_object(
            realm,
            signal_value.As<v8::Object>(),
            default_reason ? default_abort_reason(realm) : info[0]);
    }

    static void abort_controller_constructor(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        if (self == nullptr) return;
        if (!info.IsConstructCall()) {
            info.GetIsolate()->ThrowException(v8::Exception::TypeError(
                js_string(info.GetIsolate(),
                    "Failed to construct 'AbortController': Please use the 'new' operator")));
            return;
        }
        auto realm = info.GetIsolate()->GetCurrentContext();
        v8::Local<v8::Object> signal;
        if (!self->create_abort_signal(realm).ToLocal(&signal)) return;
        info.This()->SetPrivate(
            realm,
            abort_controller_signal_private(info.GetIsolate()),
            signal).Check();
        self->record_feature(
            "web-api",
            "AbortController.constructor",
            "supported",
            {},
            "web-api-binding");
        info.GetReturnValue().Set(info.This());
    }

    static void abort_signal_static_abort(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        if (self == nullptr) return;
        auto realm = info.GetIsolate()->GetCurrentContext();
        v8::Local<v8::Object> signal;
        if (!self->create_abort_signal(realm).ToLocal(&signal)) return;
        const auto default_reason = info.Length() == 0 || info[0]->IsUndefined();
        self->abort_signal_abort_object(
            realm,
            signal,
            default_reason ? default_abort_reason(realm) : info[0]);
        info.GetReturnValue().Set(signal);
    }

    static void abort_signal_timeout_fire(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto realm = info.GetIsolate()->GetCurrentContext();
        if (self == nullptr || !is_abort_signal(realm, info.Data())) return;
        self->abort_signal_abort_object(
            realm,
            info.Data().As<v8::Object>(),
            abort_dom_exception(
                realm, "The operation timed out", "TimeoutError"));
    }

    static void abort_signal_static_timeout(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        if (self == nullptr) return;
        auto realm = info.GetIsolate()->GetCurrentContext();
        if (info.Length() < 1) {
            info.GetIsolate()->ThrowException(v8::Exception::TypeError(
                js_string(info.GetIsolate(),
                    "AbortSignal.timeout requires a delay")));
            return;
        }
        v8::Maybe<double> converted = info[0]->NumberValue(realm);
        if (converted.IsNothing()) return;
        const auto delay = converted.FromJust();
        constexpr auto maximum_safe_integer = 9007199254740991.0;
        if (!std::isfinite(delay) || delay < 0.0
            || delay > maximum_safe_integer) {
            info.GetIsolate()->ThrowException(v8::Exception::RangeError(
                js_string(info.GetIsolate(),
                    "AbortSignal.timeout delay is outside the supported range")));
            return;
        }
        if (self->timers.size() >= maximum_timer_tasks) {
            throw_dom_exception(
                info,
                "AbortSignal timeout capacity exhausted",
                "QuotaExceededError");
            return;
        }
        v8::Local<v8::Object> signal;
        if (!self->create_abort_signal(realm).ToLocal(&signal)) return;
        v8::Local<v8::Value> timeout_value;
        if (!realm->Global()->Get(
                realm, js_string(info.GetIsolate(), "setTimeout"))
                    .ToLocal(&timeout_value)
            || !timeout_value->IsFunction()) {
            return;
        }
        auto callback = v8::Function::New(
            realm, abort_signal_timeout_fire, signal).ToLocalChecked();
        v8::Local<v8::Value> arguments[]{
            callback, v8::Number::New(info.GetIsolate(), std::floor(delay))};
        if (timeout_value.As<v8::Function>()->Call(
                realm, realm->Global(), 2, arguments).IsEmpty()) {
            return;
        }
        self->record_feature(
            "web-api", "AbortSignal.timeout", "supported", {},
            "web-api-binding");
        info.GetReturnValue().Set(signal);
    }

    void cleanup_abort_signal_composition(
        abort_signal_composition_binding& binding)
    {
        auto realm = binding.context.Get(isolate);
        if (!realm.IsEmpty()) {
            v8::Context::Scope scope(realm);
            const auto count = std::min(
                binding.sources.size(), binding.listeners.size());
            for (size_t index = 0; index < count; ++index) {
                auto source = binding.sources[index].Get(isolate);
                auto listener = binding.listeners[index].Get(isolate);
                if (source.IsEmpty() || listener.IsEmpty()) continue;
                v8::Local<v8::Value> remove;
                if (!source->Get(
                        realm, js_string(isolate, "removeEventListener"))
                            .ToLocal(&remove)
                    || !remove->IsFunction()) {
                    continue;
                }
                v8::Local<v8::Value> arguments[]{
                    js_string(isolate, "abort"), listener};
                static_cast<void>(remove.As<v8::Function>()->Call(
                    realm, source, 2, arguments));
            }
        }
        for (auto& source : binding.sources) source.Reset();
        for (auto& listener : binding.listeners) listener.Reset();
        binding.sources.clear();
        binding.listeners.clear();
        binding.signal.Reset();
        binding.context.Reset();
        binding.wake.reset();
        binding.garbage_collected = false;
        binding.reclaimed = true;
    }

    static void abort_signal_composition_collected(
        const v8::WeakCallbackInfo<abort_signal_composition_binding>& info)
    {
        auto* binding = info.GetParameter();
        if (binding == nullptr) return;
        binding->signal.Reset();
        binding->garbage_collected = true;
        if (auto wake = binding->wake.lock(); wake != nullptr) {
            wake->signal(true);
        }
    }

    void refresh_abort_signal_compositions()
    {
        for (auto& binding : abort_signal_compositions) {
            if (binding != nullptr && !binding->reclaimed
                && binding->garbage_collected) {
                cleanup_abort_signal_composition(*binding);
            }
        }
    }

    static void abort_signal_composition_source_aborted(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        if (self == nullptr || !info.Data()->IsExternal()) return;
        auto* binding = static_cast<abort_signal_composition_binding*>(
            info.Data().As<v8::External>()->Value(
                v8::kExternalPointerTypeTagDefault));
        if (binding == nullptr || binding->reclaimed) return;
        auto realm = binding->context.Get(info.GetIsolate());
        auto signal = binding->signal.Get(info.GetIsolate());
        if (realm.IsEmpty() || signal.IsEmpty()) {
            self->cleanup_abort_signal_composition(*binding);
            return;
        }
        v8::Context::Scope scope(realm);
        auto source = info.This();
        const auto reason = is_abort_signal(realm, source)
            ? abort_signal_reason(realm, source)
            : default_abort_reason(realm);
        self->abort_signal_abort_object(realm, signal, reason);
        self->cleanup_abort_signal_composition(*binding);
    }

    static void abort_signal_static_any(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        if (self == nullptr) return;
        auto realm = info.GetIsolate()->GetCurrentContext();
        std::vector<v8::Local<v8::Object>> sources;
        sources.reserve(8U);
        if (info.Length() < 1 || !info[0]->IsObject()) {
            info.GetIsolate()->ThrowException(v8::Exception::TypeError(
                js_string(info.GetIsolate(),
                    "AbortSignal.any requires an iterable")));
            return;
        }
        v8::Local<v8::Value> iterator_method;
        if (!info[0].As<v8::Object>()->Get(
                realm, v8::Symbol::GetIterator(info.GetIsolate()))
                    .ToLocal(&iterator_method)
            || !iterator_method->IsFunction()) {
            info.GetIsolate()->ThrowException(v8::Exception::TypeError(
                js_string(info.GetIsolate(),
                    "AbortSignal.any requires an iterable")));
            return;
        }
        v8::Local<v8::Value> iterator_value;
        if (!iterator_method.As<v8::Function>()->Call(
                realm, info[0], 0, nullptr).ToLocal(&iterator_value)) {
            return;
        }
        if (!iterator_value->IsObject()) {
            info.GetIsolate()->ThrowException(v8::Exception::TypeError(
                js_string(info.GetIsolate(),
                    "AbortSignal.any iterator must return an object")));
            return;
        }
        auto iterator = iterator_value.As<v8::Object>();
        v8::Local<v8::Value> next_value;
        if (!iterator->Get(
                realm, js_string(info.GetIsolate(), "next"))
                    .ToLocal(&next_value)
            || !next_value->IsFunction()) {
            info.GetIsolate()->ThrowException(v8::Exception::TypeError(
                js_string(info.GetIsolate(),
                    "AbortSignal.any iterator next is not callable")));
            return;
        }
        const auto close_iterator = [&]() {
            v8::Local<v8::Value> return_value;
            if (iterator->Get(
                    realm, js_string(info.GetIsolate(), "return"))
                        .ToLocal(&return_value)
                && return_value->IsFunction()) {
                static_cast<void>(return_value.As<v8::Function>()->Call(
                    realm, iterator, 0, nullptr));
            }
        };
        size_t input_count = 0U;
        for (;;) {
            v8::Local<v8::Value> step_value;
            if (!next_value.As<v8::Function>()->Call(
                    realm, iterator, 0, nullptr).ToLocal(&step_value)) {
                return;
            }
            if (!step_value->IsObject()) {
                close_iterator();
                info.GetIsolate()->ThrowException(v8::Exception::TypeError(
                    js_string(info.GetIsolate(),
                        "AbortSignal.any iterator result is not an object")));
                return;
            }
            auto step = step_value.As<v8::Object>();
            v8::Local<v8::Value> done;
            if (!step->Get(
                    realm, js_string(info.GetIsolate(), "done")).ToLocal(&done)) {
                return;
            }
            if (done->BooleanValue(info.GetIsolate())) break;
            if (input_count++ >= maximum_abort_signal_fan_in) {
                close_iterator();
                throw_dom_exception(
                    info,
                    "AbortSignal composition fan-in capacity exhausted",
                    "QuotaExceededError");
                return;
            }
            v8::Local<v8::Value> source;
            if (!step->Get(
                    realm, js_string(info.GetIsolate(), "value"))
                    .ToLocal(&source)) {
                return;
            }
            if (!is_abort_signal(realm, source)) {
                close_iterator();
                info.GetIsolate()->ThrowException(v8::Exception::TypeError(
                    js_string(info.GetIsolate(),
                        "AbortSignal.any input contains a non-AbortSignal")));
                return;
            }
            if (std::none_of(
                    sources.begin(), sources.end(),
                    [&](const auto& known) { return known->StrictEquals(source); })) {
                sources.push_back(source.As<v8::Object>());
            }
        }

        v8::Local<v8::Object> signal;
        if (!self->create_abort_signal(realm).ToLocal(&signal)) return;
        for (const auto& source : sources) {
            if (!abort_signal_is_aborted(realm, source)) continue;
            self->abort_signal_abort_object(
                realm, signal, abort_signal_reason(realm, source));
            info.GetReturnValue().Set(signal);
            return;
        }
        if (sources.empty()) {
            info.GetReturnValue().Set(signal);
            return;
        }

        const auto reusable = std::find_if(
            self->abort_signal_compositions.begin(),
            self->abort_signal_compositions.end(),
            [](const auto& candidate) {
                return candidate != nullptr && candidate->reclaimed;
            });
        if (reusable == self->abort_signal_compositions.end()
            && self->abort_signal_compositions.size()
                >= maximum_abort_signal_compositions) {
            throw_dom_exception(
                info,
                "AbortSignal composition capacity exhausted",
                "QuotaExceededError");
            return;
        }
        abort_signal_composition_binding* binding = nullptr;
        if (reusable != self->abort_signal_compositions.end()) {
            binding = reusable->get();
        } else {
            auto candidate =
                std::make_unique<abort_signal_composition_binding>();
            binding = candidate.get();
            self->abort_signal_compositions.push_back(std::move(candidate));
        }
        binding->reclaimed = false;
        binding->garbage_collected = false;
        binding->wake = self->message_port_wake;
        binding->context.Reset(info.GetIsolate(), realm);
        binding->signal.Reset(info.GetIsolate(), signal);
        binding->signal.SetWeak(
            binding,
            abort_signal_composition_collected,
            v8::WeakCallbackType::kParameter);
        binding->sources.reserve(sources.size());
        binding->listeners.reserve(sources.size());
        auto external = v8::External::New(
            info.GetIsolate(), binding, v8::kExternalPointerTypeTagDefault);
        auto options = v8::Object::New(info.GetIsolate());
        options->Set(
            realm, js_string(info.GetIsolate(), "once"),
            v8::True(info.GetIsolate())).Check();
        for (const auto& source : sources) {
            auto listener = v8::Function::New(
                realm,
                abort_signal_composition_source_aborted,
                external).ToLocalChecked();
            v8::Local<v8::Value> add;
            if (!source->Get(
                    realm, js_string(info.GetIsolate(), "addEventListener"))
                        .ToLocal(&add)
                || !add->IsFunction()) {
                self->cleanup_abort_signal_composition(*binding);
                return;
            }
            v8::Local<v8::Value> add_arguments[]{
                js_string(info.GetIsolate(), "abort"), listener, options};
            if (add.As<v8::Function>()->Call(
                    realm, source, 3, add_arguments).IsEmpty()) {
                self->cleanup_abort_signal_composition(*binding);
                return;
            }
            binding->sources.emplace_back(info.GetIsolate(), source);
            binding->listeners.emplace_back(info.GetIsolate(), listener);
        }
        self->record_feature(
            "web-api", "AbortSignal.any", "supported", {},
            "web-api-binding");
        info.GetReturnValue().Set(signal);
    }

    void retire_abort_signal_compositions_for_context(
        v8::Local<v8::Context> retired)
    {
        for (auto& binding : abort_signal_compositions) {
            if (binding != nullptr && !binding->reclaimed
                && binding->context.Get(isolate) == retired) {
                cleanup_abort_signal_composition(*binding);
            }
        }
    }

    void stop_abort_signal_compositions()
    {
        for (auto& binding : abort_signal_compositions) {
            if (binding == nullptr || binding->reclaimed) continue;
            for (auto& source : binding->sources) source.Reset();
            for (auto& listener : binding->listeners) listener.Reset();
            binding->sources.clear();
            binding->listeners.clear();
            binding->signal.Reset();
            binding->context.Reset();
            binding->wake.reset();
            binding->garbage_collected = false;
            binding->reclaimed = true;
        }
    }

    void install_abort_globals(v8::Local<v8::Context> realm)
    {
        auto* local_isolate = v8::Isolate::GetCurrent();
        auto signal_constructor = v8::Function::New(
            realm, abort_signal_constructor).ToLocalChecked();
        signal_constructor->SetName(js_string(local_isolate, "AbortSignal"));
        v8::Local<v8::Value> signal_prototype_value;
        v8::Local<v8::Value> event_target_value;
        v8::Local<v8::Value> event_target_prototype;
        if (!signal_constructor->Get(
                realm, js_string(local_isolate, "prototype"))
                    .ToLocal(&signal_prototype_value)
            || !signal_prototype_value->IsObject()) {
            return;
        }
        auto signal_prototype = signal_prototype_value.As<v8::Object>();
        if (realm->Global()->Get(
                realm, js_string(local_isolate, "EventTarget"))
                    .ToLocal(&event_target_value)
            && event_target_value->IsFunction()
            && event_target_value.As<v8::Object>()->Get(
                realm, js_string(local_isolate, "prototype"))
                    .ToLocal(&event_target_prototype)
            && event_target_prototype->IsObject()) {
            signal_prototype->SetPrototype(
                realm, event_target_prototype).Check();
        }
        auto aborted_getter = v8::Function::New(
            realm, abort_signal_aborted_get).ToLocalChecked();
        auto reason_getter = v8::Function::New(
            realm, abort_signal_reason_get).ToLocalChecked();
        signal_prototype->SetAccessorProperty(
            js_string(local_isolate, "aborted"), aborted_getter, {}, v8::None);
        signal_prototype->SetAccessorProperty(
            js_string(local_isolate, "reason"), reason_getter, {}, v8::None);
        auto throw_if_aborted = v8::Function::New(
            realm, abort_signal_throw_if_aborted).ToLocalChecked();
        throw_if_aborted->SetName(js_string(local_isolate, "throwIfAborted"));
        signal_prototype->DefineOwnProperty(
            realm,
            js_string(local_isolate, "throwIfAborted"),
            throw_if_aborted,
            v8::DontEnum).Check();
        signal_prototype->DefineOwnProperty(
            realm,
            v8::Symbol::GetToStringTag(local_isolate),
            js_string(local_isolate, "AbortSignal"),
            static_cast<v8::PropertyAttribute>(v8::ReadOnly | v8::DontEnum))
                .Check();
        const auto install_static = [&](const char* name, auto callback, int length) {
            auto function = v8::Function::New(
                realm, callback, {}, length).ToLocalChecked();
            function->SetName(js_string(local_isolate, name));
            signal_constructor->DefineOwnProperty(
                realm,
                js_string(local_isolate, name),
                function,
                v8::DontEnum).Check();
        };
        install_static("abort", abort_signal_static_abort, 0);
        install_static("timeout", abort_signal_static_timeout, 1);
        install_static("any", abort_signal_static_any, 1);

        auto controller_constructor = v8::Function::New(
            realm, abort_controller_constructor).ToLocalChecked();
        controller_constructor->SetName(
            js_string(local_isolate, "AbortController"));
        v8::Local<v8::Value> controller_prototype_value;
        if (!controller_constructor->Get(
                realm, js_string(local_isolate, "prototype"))
                    .ToLocal(&controller_prototype_value)
            || !controller_prototype_value->IsObject()) {
            return;
        }
        auto controller_prototype = controller_prototype_value.As<v8::Object>();
        auto signal_getter = v8::Function::New(
            realm, abort_controller_signal_get).ToLocalChecked();
        controller_prototype->SetAccessorProperty(
            js_string(local_isolate, "signal"), signal_getter, {}, v8::None);
        auto abort = v8::Function::New(
            realm, abort_controller_abort, {}, 0).ToLocalChecked();
        abort->SetName(js_string(local_isolate, "abort"));
        controller_prototype->DefineOwnProperty(
            realm,
            js_string(local_isolate, "abort"),
            abort,
            v8::DontEnum).Check();
        controller_prototype->DefineOwnProperty(
            realm,
            v8::Symbol::GetToStringTag(local_isolate),
            js_string(local_isolate, "AbortController"),
            static_cast<v8::PropertyAttribute>(v8::ReadOnly | v8::DontEnum))
                .Check();

        realm->Global()->DefineOwnProperty(
            realm,
            js_string(local_isolate, "AbortSignal"),
            signal_constructor,
            v8::None).Check();
        realm->Global()->DefineOwnProperty(
            realm,
            js_string(local_isolate, "AbortController"),
            controller_constructor,
            v8::None).Check();
    }
