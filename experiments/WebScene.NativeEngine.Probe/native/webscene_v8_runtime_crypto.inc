    static v8::Local<v8::Value> crypto_dom_exception(
        v8::Isolate* isolate,
        v8::Local<v8::Context> local_context,
        const char* message,
        const char* name)
    {
        v8::Local<v8::Value> constructor;
        if (local_context->Global()->Get(
                local_context, js_string(isolate, "DOMException"))
                .ToLocal(&constructor)
            && constructor->IsFunction()) {
            v8::Local<v8::Value> arguments[]{
                js_string(isolate, message), js_string(isolate, name)};
            v8::Local<v8::Object> exception;
            if (constructor.As<v8::Function>()->NewInstance(
                    local_context, 2, arguments).ToLocal(&exception)) {
                return exception;
            }
        }
        auto fallback = v8::Exception::Error(js_string(isolate, message));
        fallback.As<v8::Object>()->Set(
            local_context, js_string(isolate, "name"), js_string(isolate, name)).Check();
        return fallback;
    }

    static bool subtle_receiver_is_valid(
        v8::Isolate* isolate,
        v8::Local<v8::Context> local_context,
        v8::Local<v8::Value> receiver)
    {
        if (!receiver->IsObject()) return false;
        const auto brand = v8::Private::ForApi(
            isolate, js_string(isolate, "WebScene.SubtleCrypto"));
        return receiver.As<v8::Object>()->HasPrivate(local_context, brand).FromMaybe(false);
    }

    static bool copy_digest_input(
        v8::Local<v8::Value> value,
        secure_bytes& output,
        std::size_t& output_size)
    {
        v8::Local<v8::ArrayBuffer> buffer;
        std::size_t offset = 0U;
        std::size_t size = 0U;
        if (value->IsArrayBuffer()) {
            buffer = value.As<v8::ArrayBuffer>();
            size = buffer->ByteLength();
        } else if (value->IsArrayBufferView()) {
            const auto view = value.As<v8::ArrayBufferView>();
            buffer = view->Buffer();
            offset = view->ByteOffset();
            size = view->ByteLength();
        } else {
            return false;
        }
        if (buffer->WasDetached()) return false;
        const auto backing = buffer->GetBackingStore();
        output_size = size;
        if (size == 0U) {
            output = secure_bytes{};
            return true;
        }
        if (backing == nullptr || backing->Data() == nullptr
            || offset > backing->ByteLength()
            || size > backing->ByteLength() - offset) {
            return false;
        }
        const auto* begin = static_cast<const std::uint8_t*>(backing->Data()) + offset;
        output = secure_bytes(std::span<const std::uint8_t>{begin, size});
        return true;
    }

    static bool normalize_digest_algorithm(
        v8::Isolate* isolate,
        v8::Local<v8::Context> local_context,
        v8::Local<v8::Value> value,
        crypto_digest_algorithm& algorithm)
    {
        if (value->IsObject() && !value->IsString()) {
            if (!value.As<v8::Object>()->Get(
                    local_context, js_string(isolate, "name")).ToLocal(&value)) {
                return false;
            }
        }
        v8::Local<v8::String> text;
        if (!value->ToString(local_context).ToLocal(&text)) return false;
        v8::String::Utf8Value utf8(isolate, text);
        if (*utf8 == nullptr) return false;
        auto name = std::string{*utf8, static_cast<std::size_t>(utf8.length())};
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char character) {
            return static_cast<char>(std::toupper(character));
        });
        if (name == "SHA-1") {
            algorithm = crypto_digest_algorithm::sha1;
            return true;
        }
        if (name == "SHA-256") {
            algorithm = crypto_digest_algorithm::sha256;
            return true;
        }
        return false;
    }

    static void subtle_digest(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* isolate = info.GetIsolate();
        auto local_context = isolate->GetCurrentContext();
        if (!subtle_receiver_is_valid(isolate, local_context, info.This())) {
            isolate->ThrowException(v8::Exception::TypeError(
                js_string(isolate, "Illegal invocation")));
            return;
        }
        auto* self = current(isolate);
        if (self == nullptr) return;
        auto resolver = v8::Promise::Resolver::New(local_context).ToLocalChecked();
        info.GetReturnValue().Set(resolver->GetPromise());

        const auto reject = [&](v8::Local<v8::Value> error) {
            resolver->Reject(local_context, error).FromMaybe(false);
        };
        if (info.Length() < 2) {
            reject(v8::Exception::TypeError(
                js_string(isolate, "SubtleCrypto.digest requires algorithm and data")));
            return;
        }
        crypto_digest_algorithm algorithm{};
        v8::TryCatch caught(isolate);
        if (!normalize_digest_algorithm(isolate, local_context, info[0], algorithm)) {
            if (caught.HasCaught()) {
                const auto exception = caught.Exception();
                caught.Reset();
                reject(exception);
            } else {
                reject(crypto_dom_exception(
                    isolate, local_context,
                    "The requested digest algorithm is not supported",
                    "NotSupportedError"));
            }
            return;
        }
        secure_bytes copied_input;
        std::size_t copied_input_size = 0U;
        if (!copy_digest_input(info[1], copied_input, copied_input_size)) {
            reject(v8::Exception::TypeError(
                js_string(isolate, "SubtleCrypto.digest data must be a BufferSource")));
            return;
        }
        if (copied_input_size > maximum_digest_input_bytes) {
            reject(crypto_dom_exception(
                isolate, local_context,
                "SubtleCrypto.digest input exceeds the 16 MiB runtime bound",
                "OperationError"));
            return;
        }
        if (self->pending_digests.size() >= maximum_pending_digests
            || copied_input_size > maximum_pending_digest_bytes
                - std::min(self->pending_digest_bytes, maximum_pending_digest_bytes)) {
            reject(crypto_dom_exception(
                isolate, local_context,
                "SubtleCrypto.digest request capacity is exhausted",
                "OperationError"));
            return;
        }

        pending_digest_task task;
        task.context.Reset(isolate, local_context);
        task.resolver.Reset(isolate, resolver);
        task.input_size = copied_input_size;
        const auto token = task.stop.get_token();
        const auto notify = self->runtime_work_available;
        const auto result_size = crypto_digest_size(algorithm);
        task.future = std::async(
            std::launch::async,
            [algorithm,
                input = std::move(copied_input),
                result_size,
                token,
                notify]() mutable {
                async_digest_result result;
                result.size = result_size;
                result.status = crypto_digest(
                    algorithm,
                    input.view(),
                    std::span<std::uint8_t>{result.bytes}.first(result_size),
                    token);
                input.clear();
                if (notify) notify();
                return result;
            }).share();
        self->pending_digest_bytes += task.input_size;
        self->pending_digests.push_back(std::move(task));
        self->record_feature(
            "web-api",
            "SubtleCrypto.digest",
            "supported",
            "bounded asynchronous SHA-1 and SHA-256 through pinned Mbed TLS",
            "native-binding");
    }

    static void illegal_webcrypto_constructor(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        info.GetIsolate()->ThrowException(v8::Exception::TypeError(
            js_string(info.GetIsolate(), "Illegal constructor")));
    }

    void install_subtle_crypto(
        v8::Local<v8::Context> local_context,
        v8::Local<v8::Object> crypto)
    {
        auto subtle_constructor = v8::Function::New(
            local_context, illegal_webcrypto_constructor, {}, 0).ToLocalChecked();
        subtle_constructor->SetName(js_string(isolate, "SubtleCrypto"));
        auto subtle_prototype = subtle_constructor->Get(
            local_context, js_string(isolate, "prototype")).ToLocalChecked().As<v8::Object>();
        auto digest = v8::Function::New(
            local_context, subtle_digest, {}, 2).ToLocalChecked();
        digest->SetName(js_string(isolate, "digest"));
        subtle_prototype->DefineOwnProperty(
            local_context, js_string(isolate, "digest"), digest, v8::DontEnum).Check();
        subtle_prototype->DefineOwnProperty(
            local_context,
            v8::Symbol::GetToStringTag(isolate),
            js_string(isolate, "SubtleCrypto"),
            static_cast<v8::PropertyAttribute>(v8::ReadOnly | v8::DontEnum)).Check();
        auto subtle = v8::Object::New(isolate);
        subtle->SetPrototype(local_context, subtle_prototype).Check();
        subtle->SetPrivate(
            local_context,
            v8::Private::ForApi(isolate, js_string(isolate, "WebScene.SubtleCrypto")),
            v8::True(isolate)).Check();
        crypto->DefineOwnProperty(
            local_context,
            js_string(isolate, "subtle"),
            subtle,
            static_cast<v8::PropertyAttribute>(v8::ReadOnly | v8::DontEnum)).Check();
        local_context->Global()->DefineOwnProperty(
            local_context,
            js_string(isolate, "SubtleCrypto"),
            subtle_constructor,
            v8::DontEnum).Check();

        auto key_constructor = v8::Function::New(
            local_context, illegal_webcrypto_constructor, {}, 0).ToLocalChecked();
        key_constructor->SetName(js_string(isolate, "CryptoKey"));
        auto key_prototype = key_constructor->Get(
            local_context, js_string(isolate, "prototype")).ToLocalChecked().As<v8::Object>();
        key_prototype->DefineOwnProperty(
            local_context,
            v8::Symbol::GetToStringTag(isolate),
            js_string(isolate, "CryptoKey"),
            static_cast<v8::PropertyAttribute>(v8::ReadOnly | v8::DontEnum)).Check();
        local_context->Global()->DefineOwnProperty(
            local_context,
            js_string(isolate, "CryptoKey"),
            key_constructor,
            v8::DontEnum).Check();
    }

    bool has_ready_digest_task() const noexcept
    {
        return std::any_of(
            pending_digests.begin(), pending_digests.end(), [](const auto& task) {
                return task.future.valid()
                    && task.future.wait_for(std::chrono::milliseconds::zero())
                        == std::future_status::ready;
            });
    }

    bool drain_digest_task()
    {
        const auto ready = std::find_if(
            pending_digests.begin(), pending_digests.end(), [](const auto& task) {
                return task.future.valid()
                    && task.future.wait_for(std::chrono::milliseconds::zero())
                        == std::future_status::ready;
            });
        if (ready == pending_digests.end()) return true;
        auto task = std::move(*ready);
        pending_digests.erase(ready);
        pending_digest_bytes = task.input_size > pending_digest_bytes
            ? 0U : pending_digest_bytes - task.input_size;
        auto task_context = task.context.Get(isolate);
        auto resolver = task.resolver.Get(isolate);
        async_digest_result result;
        try {
            result = task.future.get();
        } catch (...) {
            result.status = crypto_provider_status::provider_failure;
        }
        if (task_context.IsEmpty() || resolver.IsEmpty()) return true;
        v8::Context::Scope task_context_scope(task_context);
        bool settled = false;
        if (result.status == crypto_provider_status::success) {
            auto output = v8::ArrayBuffer::New(isolate, result.size);
            if (result.size != 0U) {
                std::memcpy(output->GetBackingStore()->Data(), result.bytes.data(), result.size);
            }
            settled = resolver->Resolve(task_context, output).FromMaybe(false);
        } else {
            const auto cancelled = result.status == crypto_provider_status::cancelled;
            settled = resolver->Reject(
                task_context,
                crypto_dom_exception(
                    isolate,
                    task_context,
                    cancelled ? "The digest operation was cancelled"
                              : "The cryptographic provider failed the digest operation",
                    cancelled ? "AbortError" : "OperationError"))
                .FromMaybe(false);
        }
        perform_microtask_checkpoint();
        return settled;
    }

    void cancel_crypto_tasks_for_context(v8::Local<v8::Context> local_context)
    {
        for (auto& task : pending_digests) {
            if (task.context.Get(isolate) != local_context) continue;
            task.stop.request_stop();
            task.context.Reset();
            task.resolver.Reset();
        }
    }
