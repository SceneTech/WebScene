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

    static v8::Local<v8::Private> crypto_private(
        v8::Isolate* isolate,
        const char* name)
    {
        return v8::Private::ForApi(isolate, js_string(isolate, name));
    }

    static std::uintptr_t crypto_realm_id(
        v8::Isolate* isolate,
        v8::Local<v8::Context> local_context)
    {
        v8::Local<v8::Value> value;
        if (!local_context->Global()->GetPrivate(
                local_context,
                crypto_private(isolate, "WebScene.CryptoRealm")).ToLocal(&value)
            || !value->IsBigInt()) {
            return 0U;
        }
        bool lossless = false;
        const auto result = value.As<v8::BigInt>()->Uint64Value(&lossless);
        return lossless ? static_cast<std::uintptr_t>(result) : 0U;
    }

    static std::string base64url_encode(std::span<const std::uint8_t> input)
    {
        constexpr char alphabet[] =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
        std::string output;
        output.reserve((input.size() * 4U + 2U) / 3U);
        for (std::size_t index = 0U; index < input.size(); index += 3U) {
            const auto remaining = input.size() - index;
            const auto bits = static_cast<std::uint32_t>(input[index]) << 16U
                | (remaining > 1U ? static_cast<std::uint32_t>(input[index + 1U]) << 8U : 0U)
                | (remaining > 2U ? static_cast<std::uint32_t>(input[index + 2U]) : 0U);
            output.push_back(alphabet[(bits >> 18U) & 63U]);
            output.push_back(alphabet[(bits >> 12U) & 63U]);
            if (remaining > 1U) output.push_back(alphabet[(bits >> 6U) & 63U]);
            if (remaining > 2U) output.push_back(alphabet[bits & 63U]);
        }
        return output;
    }

    static bool base64url_decode(std::string_view input, secure_bytes& output)
    {
        if (input.size() % 4U == 1U
            || input.size()
                > (std::numeric_limits<std::size_t>::max() - 2U) / 3U) {
            return false;
        }
        auto decode = [](char character) -> int {
            if (character >= 'A' && character <= 'Z') return character - 'A';
            if (character >= 'a' && character <= 'z') return character - 'a' + 26;
            if (character >= '0' && character <= '9') return character - '0' + 52;
            if (character == '-') return 62;
            if (character == '_') return 63;
            return -1;
        };
        // JWK `k` is key material even when the encoding is malformed. Keep
        // partially decoded bytes in the zeroizing container so every early
        // return scrubs the prefix already consumed from attacker-controlled
        // input.
        secure_bytes decoded(input.size() * 3U / 4U + 2U);
        auto decoded_bytes = decoded.mutable_view();
        std::size_t decoded_size = 0U;
        std::uint32_t bits = 0U;
        unsigned available = 0U;
        for (const auto character : input) {
            const auto value = decode(character);
            if (value < 0) return false;
            bits = (bits << 6U) | static_cast<std::uint32_t>(value);
            available += 6U;
            if (available >= 8U) {
                available -= 8U;
                decoded_bytes[decoded_size++] =
                    static_cast<std::uint8_t>(bits >> available);
                bits &= available == 0U ? 0U : (1U << available) - 1U;
            }
        }
        if (available != 0U && bits != 0U) return false;
        output = secure_bytes(decoded.view().first(decoded_size));
        return true;
    }

    static void zeroize_crypto_text(std::string& value) noexcept
    {
        if (value.empty()) return;
        crypto_zeroize(std::span<std::uint8_t>{
            reinterpret_cast<std::uint8_t*>(value.data()), value.size()});
    }

    static bool read_usages(
        v8::Isolate* isolate,
        v8::Local<v8::Context> local_context,
        v8::Local<v8::Value> value,
        std::span<const std::string_view> allowed,
        std::vector<std::string>& output)
    {
        if (!value->IsArray()) return false;
        const auto array = value.As<v8::Array>();
        output.clear();
        for (std::uint32_t index = 0U; index < array->Length(); ++index) {
            v8::Local<v8::Value> item;
            if (!array->Get(local_context, index).ToLocal(&item)) return false;
            const auto usage = to_utf8(isolate, item);
            if (std::find(allowed.begin(), allowed.end(), usage) == allowed.end()
                || std::find(output.begin(), output.end(), usage) != output.end()) {
                return false;
            }
            output.push_back(usage);
        }
        return true;
    }

    static void crypto_key_collected(
        const v8::WeakCallbackInfo<crypto_key_binding>& info)
    {
        auto* binding = info.GetParameter();
        if (binding == nullptr || binding->released || binding->owner == nullptr) return;
        static_cast<void>(binding->owner->crypto_keys.destroy(binding->key, binding->realm));
        binding->wrapper.Reset();
        binding->released = true;
    }

    bool reserve_crypto_key_slot()
    {
        const auto remove_released = [this] {
            std::erase_if(crypto_key_bindings,
                [](const auto& binding) { return binding == nullptr || binding->released; });
        };
        remove_released();
        if (crypto_keys.size() < maximum_crypto_keys) return true;

        // CryptoKey material lives outside V8's heap. A consumer can therefore
        // create many short-lived keys without giving V8 enough heap pressure to
        // run the weak callbacks that zeroize and remove the native records.
        // Collect unreachable wrappers before reporting bounded-capacity failure.
        isolate->LowMemoryNotification();
        remove_released();
        return crypto_keys.size() < maximum_crypto_keys;
    }

    v8::MaybeLocal<v8::Object> create_crypto_key_wrapper(
        v8::Local<v8::Context> local_context,
        crypto_key_store::handle key,
        const crypto_key_metadata& metadata)
    {
        v8::Local<v8::Value> constructor_value;
        if (!local_context->Global()->Get(
                local_context, js_string(isolate, "CryptoKey")).ToLocal(&constructor_value)
            || !constructor_value->IsFunction()) {
            return {};
        }
        v8::Local<v8::Value> prototype_value;
        if (!constructor_value.As<v8::Function>()->Get(
                local_context, js_string(isolate, "prototype")).ToLocal(&prototype_value)
            || !prototype_value->IsObject()) {
            return {};
        }
        auto wrapper = v8::Object::New(isolate);
        if (!wrapper->SetPrototype(local_context, prototype_value).FromMaybe(false)) return {};
        wrapper->SetPrivate(
            local_context,
            crypto_private(isolate, "WebScene.CryptoKey"),
            v8::BigInt::NewFromUnsigned(isolate, key)).Check();
        wrapper->DefineOwnProperty(
            local_context, js_string(isolate, "type"), js_string(isolate, "secret"),
            static_cast<v8::PropertyAttribute>(v8::ReadOnly | v8::DontDelete)).Check();
        wrapper->DefineOwnProperty(
            local_context, js_string(isolate, "extractable"),
            v8::Boolean::New(isolate, metadata.extractable),
            static_cast<v8::PropertyAttribute>(v8::ReadOnly | v8::DontDelete)).Check();
        auto algorithm = v8::Object::New(isolate);
        algorithm->Set(
            local_context, js_string(isolate, "name"),
            js_string(isolate, metadata.algorithm.c_str())).Check();
        if (metadata.length_bits != 0U) {
            algorithm->Set(
                local_context, js_string(isolate, "length"),
                v8::Integer::NewFromUnsigned(
                    isolate, static_cast<std::uint32_t>(metadata.length_bits))).Check();
        }
        if (!metadata.hash.empty()) {
            auto hash = v8::Object::New(isolate);
            hash->Set(local_context, js_string(isolate, "name"),
                js_string(isolate, metadata.hash.c_str())).Check();
            algorithm->Set(local_context, js_string(isolate, "hash"), hash).Check();
        }
        wrapper->DefineOwnProperty(
            local_context, js_string(isolate, "algorithm"), algorithm,
            static_cast<v8::PropertyAttribute>(v8::ReadOnly | v8::DontDelete)).Check();
        auto usages = v8::Array::New(isolate, static_cast<int>(metadata.usages.size()));
        for (std::uint32_t index = 0U; index < metadata.usages.size(); ++index) {
            usages->Set(local_context, index,
                js_string(isolate, metadata.usages[index].c_str())).Check();
        }
        wrapper->DefineOwnProperty(
            local_context, js_string(isolate, "usages"), usages,
            static_cast<v8::PropertyAttribute>(v8::ReadOnly | v8::DontDelete)).Check();

        auto binding = std::make_unique<crypto_key_binding>();
        binding->owner = this;
        binding->key = key;
        binding->realm = crypto_realm_id(isolate, local_context);
        binding->wrapper.Reset(isolate, wrapper);
        binding->wrapper.SetWeak(
            binding.get(), crypto_key_collected, v8::WeakCallbackType::kParameter);
        crypto_key_bindings.push_back(std::move(binding));
        return wrapper;
    }

    static crypto_key_store::handle crypto_key_handle(
        v8::Isolate* isolate,
        v8::Local<v8::Context> local_context,
        v8::Local<v8::Value> value)
    {
        if (!value->IsObject()) return 0U;
        v8::Local<v8::Value> stored;
        if (!value.As<v8::Object>()->GetPrivate(
                local_context,
                crypto_private(isolate, "WebScene.CryptoKey")).ToLocal(&stored)
            || !stored->IsBigInt()) {
            return 0U;
        }
        bool lossless = false;
        const auto handle = stored.As<v8::BigInt>()->Uint64Value(&lossless);
        return lossless ? handle : 0U;
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

    static bool aes_gcm_algorithm(
        v8::Isolate* isolate,
        v8::Local<v8::Context> local_context,
        v8::Local<v8::Value> value,
        v8::Local<v8::Object>* dictionary = nullptr)
    {
        v8::Local<v8::Value> name = value;
        if (value->IsObject() && !value->IsString()) {
            if (dictionary != nullptr) *dictionary = value.As<v8::Object>();
            if (!value.As<v8::Object>()->Get(
                    local_context, js_string(isolate, "name")).ToLocal(&name)) {
                return false;
            }
        }
        auto normalized = to_utf8(isolate, name);
        std::transform(normalized.begin(), normalized.end(), normalized.begin(),
            [](unsigned char character) { return static_cast<char>(std::toupper(character)); });
        return normalized == "AES-GCM";
    }

    static bool aes_cbc_algorithm(
        v8::Isolate* isolate,
        v8::Local<v8::Context> local_context,
        v8::Local<v8::Value> value,
        v8::Local<v8::Object>* dictionary = nullptr)
    {
        v8::Local<v8::Value> name = value;
        if (value->IsObject() && !value->IsString()) {
            if (dictionary != nullptr) *dictionary = value.As<v8::Object>();
            if (!value.As<v8::Object>()->Get(
                    local_context, js_string(isolate, "name")).ToLocal(&name)) {
                return false;
            }
        }
        auto normalized = to_utf8(isolate, name);
        std::transform(normalized.begin(), normalized.end(), normalized.begin(),
            [](unsigned char character) { return static_cast<char>(std::toupper(character)); });
        return normalized == "AES-CBC";
    }

    static bool hmac_algorithm(
        v8::Isolate* isolate,
        v8::Local<v8::Context> local_context,
        v8::Local<v8::Value> value,
        crypto_digest_algorithm& hash,
        std::string& hash_name)
    {
        if (!value->IsObject() || value->IsString()) return false;
        const auto dictionary = value.As<v8::Object>();
        v8::Local<v8::Value> name, hash_value;
        if (!dictionary->Get(local_context, js_string(isolate, "name")).ToLocal(&name)
            || !dictionary->Get(local_context, js_string(isolate, "hash")).ToLocal(&hash_value)) {
            return false;
        }
        auto normalized = to_utf8(isolate, name);
        std::transform(normalized.begin(), normalized.end(), normalized.begin(),
            [](unsigned char character) { return static_cast<char>(std::toupper(character)); });
        if (normalized != "HMAC"
            || !normalize_digest_algorithm(isolate, local_context, hash_value, hash)) {
            return false;
        }
        hash_name = hash == crypto_digest_algorithm::sha1 ? "SHA-1" : "SHA-256";
        return true;
    }

    static bool hmac_name(
        v8::Isolate* isolate,
        v8::Local<v8::Context> local_context,
        v8::Local<v8::Value> value)
    {
        v8::Local<v8::Value> name = value;
        if (value->IsObject() && !value->IsString()
            && !value.As<v8::Object>()->Get(
                local_context, js_string(isolate, "name")).ToLocal(&name)) {
            return false;
        }
        auto normalized = to_utf8(isolate, name);
        std::transform(normalized.begin(), normalized.end(), normalized.begin(),
            [](unsigned char character) { return static_cast<char>(std::toupper(character)); });
        return normalized == "HMAC";
    }

    static void subtle_generate_key(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* isolate = info.GetIsolate();
        auto local_context = isolate->GetCurrentContext();
        if (!subtle_receiver_is_valid(isolate, local_context, info.This())) {
            isolate->ThrowException(v8::Exception::TypeError(js_string(isolate, "Illegal invocation")));
            return;
        }
        auto resolver = v8::Promise::Resolver::New(local_context).ToLocalChecked();
        info.GetReturnValue().Set(resolver->GetPromise());
        const auto reject = [&](const char* message, const char* name) {
            resolver->Reject(local_context,
                crypto_dom_exception(isolate, local_context, message, name)).FromMaybe(false);
        };
        auto* self = current(isolate);
        if (self == nullptr || info.Length() < 3) {
            resolver->Reject(local_context, v8::Exception::TypeError(
                js_string(isolate, "SubtleCrypto.generateKey requires algorithm, extractable, and usages"))).FromMaybe(false);
            return;
        }
        v8::Local<v8::Object> dictionary;
        if (!aes_gcm_algorithm(isolate, local_context, info[0], &dictionary)
            || dictionary.IsEmpty()) {
            reject("The requested key algorithm is not supported", "NotSupportedError");
            return;
        }
        v8::Local<v8::Value> length_value;
        if (!dictionary->Get(local_context, js_string(isolate, "length")).ToLocal(&length_value)
            || !length_value->IsUint32()) {
            resolver->Reject(local_context, v8::Exception::TypeError(
                js_string(isolate, "AES-GCM length must be 128, 192, or 256"))).FromMaybe(false);
            return;
        }
        const auto length = length_value.As<v8::Uint32>()->Value();
        if (length != 128U && length != 192U && length != 256U) {
            reject("AES-GCM length must be 128, 192, or 256", "OperationError");
            return;
        }
        constexpr std::array<std::string_view, 2U> allowed{"encrypt", "decrypt"};
        std::vector<std::string> usages;
        if (!read_usages(isolate, local_context, info[2], allowed, usages)
            || usages.empty()) {
            reject("AES-GCM key usages must contain encrypt or decrypt", "SyntaxError");
            return;
        }
        if (!self->reserve_crypto_key_slot()) {
            reject("CryptoKey capacity is exhausted", "OperationError");
            return;
        }
        secure_bytes material(length / 8U);
        if (!fill_secure_random(material.mutable_view())) {
            reject("The operating-system secure random source failed", "OperationError");
            return;
        }
        crypto_key_metadata metadata{
            "AES-GCM", std::move(usages), {}, length, info[1]->BooleanValue(isolate)};
        const auto realm = crypto_realm_id(isolate, local_context);
        const auto key = self->crypto_keys.create(realm, metadata, material.view());
        material.clear();
        v8::Local<v8::Object> wrapper;
        if (key == 0U || !self->create_crypto_key_wrapper(
                local_context, key, metadata).ToLocal(&wrapper)) {
            static_cast<void>(self->crypto_keys.destroy(key, realm));
            reject("Unable to create the AES-GCM CryptoKey", "OperationError");
            return;
        }
        resolver->Resolve(local_context, wrapper).FromMaybe(false);
    }

    static bool jwk_allows_usages(
        v8::Isolate* isolate,
        v8::Local<v8::Context> local_context,
        v8::Local<v8::Object> jwk,
        const std::vector<std::string>& requested)
    {
        v8::Local<v8::Value> key_ops;
        if (!jwk->Get(local_context, js_string(isolate, "key_ops")).ToLocal(&key_ops)
            || key_ops->IsUndefined()) {
            return true;
        }
        if (!key_ops->IsArray()) return false;
        std::vector<std::string> provided;
        const auto array = key_ops.As<v8::Array>();
        for (std::uint32_t index = 0U; index < array->Length(); ++index) {
            v8::Local<v8::Value> item;
            if (!array->Get(local_context, index).ToLocal(&item)) return false;
            provided.push_back(to_utf8(isolate, item));
        }
        return std::all_of(requested.begin(), requested.end(), [&](const auto& usage) {
            return std::find(provided.begin(), provided.end(), usage) != provided.end();
        });
    }

    static void subtle_import_hmac(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* isolate = info.GetIsolate();
        auto local_context = isolate->GetCurrentContext();
        auto resolver = v8::Promise::Resolver::New(local_context).ToLocalChecked();
        info.GetReturnValue().Set(resolver->GetPromise());
        const auto reject = [&](v8::Local<v8::Value> error) {
            resolver->Reject(local_context, error).FromMaybe(false);
        };
        const auto dom_reject = [&](const char* message, const char* name) {
            reject(crypto_dom_exception(isolate, local_context, message, name));
        };
        auto* self = current(isolate);
        crypto_digest_algorithm hash{};
        std::string hash_name;
        if (self == nullptr || info.Length() < 5
            || !hmac_algorithm(isolate, local_context, info[2], hash, hash_name)) {
            dom_reject("HMAC requires SHA-1 or SHA-256 hash parameters", "NotSupportedError");
            return;
        }
        constexpr std::array<std::string_view, 1U> allowed{"sign"};
        std::vector<std::string> usages;
        if (!read_usages(isolate, local_context, info[4], allowed, usages)
            || usages.empty()) {
            dom_reject("HMAC key usages must contain sign", "SyntaxError");
            return;
        }
        const auto format = to_utf8(isolate, info[0]);
        secure_bytes material;
        std::size_t material_size = 0U;
        if (format == "raw") {
            if (!copy_digest_input(info[1], material, material_size)) {
                reject(v8::Exception::TypeError(
                    js_string(isolate, "Raw HMAC keyData must be a BufferSource")));
                return;
            }
        } else if (format == "jwk" && info[1]->IsObject()) {
            const auto jwk = info[1].As<v8::Object>();
            v8::Local<v8::Value> kty, encoded, alg, ext;
            if (!jwk->Get(local_context, js_string(isolate, "kty")).ToLocal(&kty)
                || to_utf8(isolate, kty) != "oct"
                || !jwk->Get(local_context, js_string(isolate, "k")).ToLocal(&encoded)
                || !encoded->IsString()) {
                dom_reject("Invalid HMAC JSON Web Key", "DataError");
                return;
            }
            constexpr auto maximum_hmac_jwk_text_bytes =
                (maximum_cipher_input_bytes * 4U + 2U) / 3U;
            if (encoded.As<v8::String>()->Utf8Length(isolate)
                > maximum_hmac_jwk_text_bytes) {
                dom_reject("HMAC JWK keyData exceeds the 16 MiB runtime bound", "DataError");
                return;
            }
            auto encoded_text = to_utf8(isolate, encoded);
            const auto decoded = base64url_decode(encoded_text, material);
            zeroize_crypto_text(encoded_text);
            if (!decoded) {
                dom_reject("Invalid HMAC JSON Web Key", "DataError");
                return;
            }
            material_size = material.size();
            if (jwk->Get(local_context, js_string(isolate, "alg")).ToLocal(&alg)
                && !alg->IsUndefined()
                && to_utf8(isolate, alg) != (hash == crypto_digest_algorithm::sha1 ? "HS1" : "HS256")) {
                dom_reject("HMAC JWK alg does not match hash", "DataError");
                return;
            }
            if (jwk->Get(local_context, js_string(isolate, "ext")).ToLocal(&ext)
                && ext->IsBoolean() && info[3]->BooleanValue(isolate)
                && !ext->BooleanValue(isolate)) {
                dom_reject("A non-extractable JWK cannot be imported as extractable", "DataError");
                return;
            }
            if (!jwk_allows_usages(isolate, local_context, jwk, usages)) {
                dom_reject("HMAC JWK key_ops does not allow sign", "DataError");
                return;
            }
        } else {
            dom_reject("HMAC supports raw and jwk key formats", "NotSupportedError");
            return;
        }
        if (material_size == 0U || material_size > maximum_cipher_input_bytes) {
            dom_reject("HMAC keyData must be non-empty and at most 16 MiB", "DataError");
            return;
        }
        const auto dictionary = info[2].As<v8::Object>();
        v8::Local<v8::Value> length_value;
        if (dictionary->Get(local_context, js_string(isolate, "length")).ToLocal(&length_value)
            && !length_value->IsUndefined()) {
            if (!length_value->IsUint32()
                || length_value.As<v8::Uint32>()->Value() != material_size * 8U) {
                dom_reject("HMAC length must match the imported key material", "DataError");
                return;
            }
        }
        if (!self->reserve_crypto_key_slot()) {
            dom_reject("CryptoKey capacity is exhausted", "OperationError");
            return;
        }
        crypto_key_metadata metadata{
            "HMAC", std::move(usages), hash_name, material_size * 8U,
            info[3]->BooleanValue(isolate)};
        const auto realm = crypto_realm_id(isolate, local_context);
        const auto key = self->crypto_keys.create(realm, metadata, material.view());
        material.clear();
        v8::Local<v8::Object> wrapper;
        if (key == 0U || !self->create_crypto_key_wrapper(
                local_context, key, metadata).ToLocal(&wrapper)) {
            static_cast<void>(self->crypto_keys.destroy(key, realm));
            dom_reject("Unable to create the HMAC CryptoKey", "OperationError");
            return;
        }
        resolver->Resolve(local_context, wrapper).FromMaybe(false);
    }

    static void subtle_import_key(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* isolate = info.GetIsolate();
        auto local_context = isolate->GetCurrentContext();
        if (!subtle_receiver_is_valid(isolate, local_context, info.This())) {
            isolate->ThrowException(v8::Exception::TypeError(js_string(isolate, "Illegal invocation")));
            return;
        }
        crypto_digest_algorithm hmac_hash{};
        std::string hmac_hash_name;
        if (info.Length() > 2
            && hmac_algorithm(
                isolate, local_context, info[2], hmac_hash, hmac_hash_name)) {
            subtle_import_hmac(info);
            return;
        }
        auto resolver = v8::Promise::Resolver::New(local_context).ToLocalChecked();
        info.GetReturnValue().Set(resolver->GetPromise());
        const auto reject = [&](const char* message, const char* name) {
            resolver->Reject(local_context,
                crypto_dom_exception(isolate, local_context, message, name)).FromMaybe(false);
        };
        auto* self = current(isolate);
        if (self == nullptr || info.Length() < 5) {
            resolver->Reject(local_context, v8::Exception::TypeError(
                js_string(isolate, "SubtleCrypto.importKey requires five arguments"))).FromMaybe(false);
            return;
        }
        const auto is_gcm = aes_gcm_algorithm(isolate, local_context, info[2]);
        const auto is_cbc = !is_gcm && aes_cbc_algorithm(isolate, local_context, info[2]);
        if (!is_gcm && !is_cbc) {
            reject("The requested key algorithm is not supported", "NotSupportedError");
            return;
        }
        constexpr std::array<std::string_view, 2U> allowed{"encrypt", "decrypt"};
        constexpr std::array<std::string_view, 1U> cbc_allowed{"decrypt"};
        std::vector<std::string> usages;
        const auto valid_usages = is_gcm
            ? read_usages(isolate, local_context, info[4], allowed, usages)
            : read_usages(isolate, local_context, info[4], cbc_allowed, usages);
        if (!valid_usages
            || usages.empty()) {
            reject(is_gcm
                ? "AES-GCM key usages must contain encrypt or decrypt"
                : "AES-CBC key usages must contain decrypt", "SyntaxError");
            return;
        }
        const auto format = to_utf8(isolate, info[0]);
        secure_bytes material;
        std::size_t material_size = 0U;
        if (format == "raw") {
            if (!copy_digest_input(info[1], material, material_size)) {
                resolver->Reject(local_context, v8::Exception::TypeError(
                    js_string(isolate, "Raw AES keyData must be a BufferSource"))).FromMaybe(false);
                return;
            }
        } else if (format == "jwk" && info[1]->IsObject()) {
            const auto jwk = info[1].As<v8::Object>();
            v8::Local<v8::Value> kty, encoded, alg, ext;
            if (!jwk->Get(local_context, js_string(isolate, "kty")).ToLocal(&kty)
                || to_utf8(isolate, kty) != "oct"
                || !jwk->Get(local_context, js_string(isolate, "k")).ToLocal(&encoded)
                || !encoded->IsString()) {
                reject("Invalid AES JSON Web Key", "DataError");
                return;
            }
            // Unpadded Base64URL for the largest accepted 256-bit AES key is
            // 43 bytes. Reject before allocating a native UTF-8 copy.
            if (encoded.As<v8::String>()->Utf8Length(isolate) > 43U) {
                reject("AES JWK keyData exceeds 256 bits", "DataError");
                return;
            }
            auto encoded_text = to_utf8(isolate, encoded);
            const auto decoded = base64url_decode(encoded_text, material);
            zeroize_crypto_text(encoded_text);
            if (!decoded) {
                reject("Invalid AES JSON Web Key", "DataError");
                return;
            }
            material_size = material.size();
            if (jwk->Get(local_context, js_string(isolate, "alg")).ToLocal(&alg)
                && !alg->IsUndefined()) {
                const auto expected = "A" + std::to_string(material_size * 8U)
                    + (is_gcm ? "GCM" : "CBC");
                if (to_utf8(isolate, alg) != expected) {
                    reject("AES JWK alg does not match key length", "DataError");
                    return;
                }
            }
            if (jwk->Get(local_context, js_string(isolate, "ext")).ToLocal(&ext)
                && ext->IsBoolean() && info[3]->BooleanValue(isolate)
                && !ext->BooleanValue(isolate)) {
                reject("A non-extractable JWK cannot be imported as extractable", "DataError");
                return;
            }
            if (!jwk_allows_usages(isolate, local_context, jwk, usages)) {
                reject("AES JWK key_ops does not allow requested usages", "DataError");
                return;
            }
        } else {
            reject("AES supports raw and jwk key formats", "NotSupportedError");
            return;
        }
        if (material_size != 16U && material_size != 24U && material_size != 32U) {
            reject("AES keyData must contain 128, 192, or 256 bits", "DataError");
            return;
        }
        if (!self->reserve_crypto_key_slot()) {
            reject("CryptoKey capacity is exhausted", "OperationError");
            return;
        }
        crypto_key_metadata metadata{
            is_gcm ? "AES-GCM" : "AES-CBC", std::move(usages), {}, material_size * 8U,
            info[3]->BooleanValue(isolate)};
        const auto realm = crypto_realm_id(isolate, local_context);
        const auto key = self->crypto_keys.create(realm, metadata, material.view());
        material.clear();
        v8::Local<v8::Object> wrapper;
        if (key == 0U || !self->create_crypto_key_wrapper(
                local_context, key, metadata).ToLocal(&wrapper)) {
            static_cast<void>(self->crypto_keys.destroy(key, realm));
            reject("Unable to create the AES CryptoKey", "OperationError");
            return;
        }
        resolver->Resolve(local_context, wrapper).FromMaybe(false);
    }

    static void subtle_export_key(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* isolate = info.GetIsolate();
        auto local_context = isolate->GetCurrentContext();
        if (!subtle_receiver_is_valid(isolate, local_context, info.This())) {
            isolate->ThrowException(v8::Exception::TypeError(js_string(isolate, "Illegal invocation")));
            return;
        }
        auto resolver = v8::Promise::Resolver::New(local_context).ToLocalChecked();
        info.GetReturnValue().Set(resolver->GetPromise());
        const auto reject = [&](const char* message, const char* name) {
            resolver->Reject(local_context,
                crypto_dom_exception(isolate, local_context, message, name)).FromMaybe(false);
        };
        auto* self = current(isolate);
        const auto realm = crypto_realm_id(isolate, local_context);
        const auto key = info.Length() > 1
            ? crypto_key_handle(isolate, local_context, info[1]) : 0U;
        const auto metadata = self == nullptr ? std::nullopt : self->crypto_keys.describe(key, realm);
        if (!metadata
            || (metadata->algorithm != "AES-GCM"
                && metadata->algorithm != "AES-CBC"
                && metadata->algorithm != "HMAC")) {
            reject("The CryptoKey does not belong to this realm or algorithm", "InvalidAccessError");
            return;
        }
        if (!metadata->extractable) {
            reject("The CryptoKey is not extractable", "InvalidAccessError");
            return;
        }
        const auto format = info.Length() > 0 ? to_utf8(isolate, info[0]) : std::string{};
        secure_bytes copy;
        const auto copied = self->crypto_keys.use(key, realm, {}, [&](const auto&, auto bytes) {
            copy = secure_bytes(bytes);
        });
        if (!copied) {
            reject("The CryptoKey is no longer available", "InvalidAccessError");
            return;
        }
        if (format == "raw") {
            auto output = v8::ArrayBuffer::New(isolate, copy.size());
            if (copy.size() != 0U) {
                std::memcpy(output->GetBackingStore()->Data(), copy.view().data(), copy.size());
            }
            copy.clear();
            resolver->Resolve(local_context, output).FromMaybe(false);
            return;
        }
        if (format == "jwk") {
            auto jwk = v8::Object::New(isolate);
            jwk->Set(local_context, js_string(isolate, "kty"), js_string(isolate, "oct")).Check();
            auto encoded = base64url_encode(copy.view());
            copy.clear();
            const auto encoded_value = js_string(isolate, encoded.c_str());
            zeroize_crypto_text(encoded);
            jwk->Set(local_context, js_string(isolate, "k"),
                encoded_value).Check();
            const auto alg = metadata->algorithm == "HMAC"
                ? (metadata->hash == "SHA-1" ? std::string{"HS1"} : std::string{"HS256"})
                : "A" + std::to_string(metadata->length_bits)
                    + (metadata->algorithm == "AES-GCM" ? "GCM" : "CBC");
            jwk->Set(local_context, js_string(isolate, "alg"), js_string(isolate, alg.c_str())).Check();
            jwk->Set(local_context, js_string(isolate, "ext"),
                v8::Boolean::New(isolate, metadata->extractable)).Check();
            auto ops = v8::Array::New(isolate, static_cast<int>(metadata->usages.size()));
            for (std::uint32_t index = 0U; index < metadata->usages.size(); ++index) {
                ops->Set(local_context, index,
                    js_string(isolate, metadata->usages[index].c_str())).Check();
            }
            jwk->Set(local_context, js_string(isolate, "key_ops"), ops).Check();
            resolver->Resolve(local_context, jwk).FromMaybe(false);
            return;
        }
        copy.clear();
        reject("Secret keys support raw and jwk key formats", "NotSupportedError");
    }

    static void subtle_aes_gcm(
        const v8::FunctionCallbackInfo<v8::Value>& info,
        bool encrypt)
    {
        auto* isolate = info.GetIsolate();
        auto local_context = isolate->GetCurrentContext();
        if (!subtle_receiver_is_valid(isolate, local_context, info.This())) {
            isolate->ThrowException(v8::Exception::TypeError(js_string(isolate, "Illegal invocation")));
            return;
        }
        auto resolver = v8::Promise::Resolver::New(local_context).ToLocalChecked();
        info.GetReturnValue().Set(resolver->GetPromise());
        const auto reject = [&](const char* message, const char* name) {
            resolver->Reject(local_context,
                crypto_dom_exception(isolate, local_context, message, name)).FromMaybe(false);
        };
        auto* self = current(isolate);
        if (self == nullptr || info.Length() < 3) {
            resolver->Reject(local_context, v8::Exception::TypeError(
                js_string(isolate, "AES-GCM requires algorithm, key, and data"))).FromMaybe(false);
            return;
        }
        v8::Local<v8::Object> algorithm;
        if (!aes_gcm_algorithm(isolate, local_context, info[0], &algorithm)
            || algorithm.IsEmpty()) {
            reject("The requested cipher algorithm is not supported", "NotSupportedError");
            return;
        }
        v8::Local<v8::Value> iv_value;
        if (!algorithm->Get(local_context, js_string(isolate, "iv")).ToLocal(&iv_value)) return;
        secure_bytes iv, aad, data, key_material;
        std::size_t iv_size = 0U, aad_size = 0U, data_size = 0U;
        if (!copy_digest_input(iv_value, iv, iv_size) || iv_size == 0U || iv_size > 65536U) {
            reject("AES-GCM iv must be a non-empty BufferSource up to 64 KiB", "OperationError");
            return;
        }
        v8::Local<v8::Value> aad_value;
        if (algorithm->Get(local_context, js_string(isolate, "additionalData")).ToLocal(&aad_value)
            && !aad_value->IsUndefined()) {
            if (!copy_digest_input(aad_value, aad, aad_size)) {
                resolver->Reject(local_context, v8::Exception::TypeError(
                    js_string(isolate, "AES-GCM additionalData must be a BufferSource"))).FromMaybe(false);
                return;
            }
        }
        if (!copy_digest_input(info[2], data, data_size)) {
            resolver->Reject(local_context, v8::Exception::TypeError(
                js_string(isolate, "AES-GCM data must be a BufferSource"))).FromMaybe(false);
            return;
        }
        v8::Local<v8::Value> tag_value;
        std::uint32_t tag_bits = 128U;
        if (algorithm->Get(local_context, js_string(isolate, "tagLength")).ToLocal(&tag_value)
            && !tag_value->IsUndefined()) {
            if (!tag_value->IsUint32()) {
                reject("AES-GCM tagLength is invalid", "OperationError");
                return;
            }
            tag_bits = tag_value.As<v8::Uint32>()->Value();
        }
        constexpr std::array<std::uint32_t, 7U> allowed_tags{
            32U, 64U, 96U, 104U, 112U, 120U, 128U};
        if (std::find(allowed_tags.begin(), allowed_tags.end(), tag_bits) == allowed_tags.end()) {
            reject("AES-GCM tagLength is invalid", "OperationError");
            return;
        }
        if (data_size > maximum_cipher_input_bytes || aad_size > maximum_cipher_input_bytes) {
            reject("AES-GCM input exceeds the 16 MiB runtime bound", "OperationError");
            return;
        }
        const auto realm = crypto_realm_id(isolate, local_context);
        const auto key = crypto_key_handle(isolate, local_context, info[1]);
        const auto metadata = self->crypto_keys.describe(key, realm);
        const auto usage = encrypt ? std::string_view{"encrypt"} : std::string_view{"decrypt"};
        if (!metadata || metadata->algorithm != "AES-GCM"
            || !self->crypto_keys.use(key, realm, usage, [&](const auto&, auto bytes) {
                key_material = secure_bytes(bytes);
            })) {
            reject("The CryptoKey does not allow this AES-GCM operation", "InvalidAccessError");
            return;
        }
        const auto request_bytes = data_size + aad_size + iv_size + key_material.size();
        if (self->pending_ciphers.size() >= maximum_pending_ciphers
            || request_bytes > maximum_pending_cipher_bytes
                - std::min(self->pending_cipher_bytes, maximum_pending_cipher_bytes)) {
            reject("AES-GCM request capacity is exhausted", "OperationError");
            return;
        }
        pending_cipher_task task;
        task.context.Reset(isolate, local_context);
        task.resolver.Reset(isolate, resolver);
        task.input_size = request_bytes;
        const auto token = task.stop.get_token();
        const auto notify = self->runtime_work_available;
        task.future = std::async(std::launch::async,
            [encrypt,
                key = std::move(key_material),
                iv = std::move(iv),
                aad = std::move(aad),
                data = std::move(data),
                tag_bytes = static_cast<std::size_t>(tag_bits / 8U),
                token,
                notify]() mutable {
                async_cipher_result result;
                result.status = encrypt
                    ? crypto_aes_gcm_encrypt(
                        key.view(), iv.view(), aad.view(), data.view(),
                        tag_bytes, result.bytes, token)
                    : crypto_aes_gcm_decrypt(
                        key.view(), iv.view(), aad.view(), data.view(),
                        tag_bytes, result.bytes, token);
                key.clear(); iv.clear(); aad.clear(); data.clear();
                if (notify) notify();
                return result;
            });
        self->pending_cipher_bytes += request_bytes;
        self->pending_ciphers.push_back(std::move(task));
        self->record_feature(
            "web-api",
            encrypt ? "SubtleCrypto.encrypt(AES-GCM)"
                    : "SubtleCrypto.decrypt(AES-GCM)",
            "supported",
            "bounded asynchronous authenticated encryption through pinned Mbed TLS",
            "native-binding");
    }

    static void subtle_encrypt(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        subtle_aes_gcm(info, true);
    }

    static void subtle_aes_cbc_decrypt(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* isolate = info.GetIsolate();
        auto local_context = isolate->GetCurrentContext();
        auto resolver = v8::Promise::Resolver::New(local_context).ToLocalChecked();
        info.GetReturnValue().Set(resolver->GetPromise());
        const auto reject = [&](v8::Local<v8::Value> error) {
            resolver->Reject(local_context, error).FromMaybe(false);
        };
        auto* self = current(isolate);
        if (self == nullptr || info.Length() < 3) {
            reject(v8::Exception::TypeError(
                js_string(isolate, "AES-CBC decrypt requires algorithm, key, and data")));
            return;
        }
        v8::Local<v8::Object> algorithm;
        if (!aes_cbc_algorithm(isolate, local_context, info[0], &algorithm)
            || algorithm.IsEmpty()) {
            reject(crypto_dom_exception(isolate, local_context,
                "The requested cipher algorithm is not supported", "NotSupportedError"));
            return;
        }
        v8::Local<v8::Value> iv_value;
        if (!algorithm->Get(local_context, js_string(isolate, "iv")).ToLocal(&iv_value)) return;
        secure_bytes iv, data, key_material;
        std::size_t iv_size = 0U, data_size = 0U;
        if (!copy_digest_input(iv_value, iv, iv_size) || iv_size != 16U) {
            reject(crypto_dom_exception(isolate, local_context,
                "AES-CBC iv must contain exactly 16 bytes", "OperationError"));
            return;
        }
        if (!copy_digest_input(info[2], data, data_size)) {
            reject(v8::Exception::TypeError(
                js_string(isolate, "AES-CBC data must be a BufferSource")));
            return;
        }
        if (data_size == 0U || data_size % 16U != 0U
            || data_size > maximum_cipher_input_bytes) {
            reject(crypto_dom_exception(isolate, local_context,
                "AES-CBC ciphertext must be non-empty, block aligned, and at most 16 MiB",
                "OperationError"));
            return;
        }
        const auto realm = crypto_realm_id(isolate, local_context);
        const auto key = crypto_key_handle(isolate, local_context, info[1]);
        const auto metadata = self->crypto_keys.describe(key, realm);
        if (!metadata || metadata->algorithm != "AES-CBC"
            || !self->crypto_keys.use(key, realm, "decrypt", [&](const auto&, auto bytes) {
                key_material = secure_bytes(bytes);
            })) {
            reject(crypto_dom_exception(isolate, local_context,
                "The CryptoKey does not allow AES-CBC decrypt", "InvalidAccessError"));
            return;
        }
        const auto request_bytes = data_size + iv_size + key_material.size();
        if (self->pending_ciphers.size() >= maximum_pending_ciphers
            || request_bytes > maximum_pending_cipher_bytes
                - std::min(self->pending_cipher_bytes, maximum_pending_cipher_bytes)) {
            reject(crypto_dom_exception(isolate, local_context,
                "AES-CBC request capacity is exhausted", "OperationError"));
            return;
        }
        pending_cipher_task task;
        task.context.Reset(isolate, local_context);
        task.resolver.Reset(isolate, resolver);
        task.input_size = request_bytes;
        const auto token = task.stop.get_token();
        const auto notify = self->runtime_work_available;
        task.future = std::async(std::launch::async,
            [key = std::move(key_material), iv = std::move(iv),
                data = std::move(data), token, notify]() mutable {
                async_cipher_result result;
                result.status = crypto_aes_cbc_decrypt(
                    key.view(), iv.view(), data.view(), result.bytes, token);
                key.clear(); iv.clear(); data.clear();
                if (notify) notify();
                return result;
            });
        self->pending_cipher_bytes += request_bytes;
        self->pending_ciphers.push_back(std::move(task));
        self->record_feature(
            "web-api", "SubtleCrypto.decrypt(AES-CBC)", "supported",
            "bounded asynchronous PKCS#7 decrypt through pinned Mbed TLS",
            "native-binding");
    }

    static void subtle_decrypt(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* isolate = info.GetIsolate();
        auto local_context = isolate->GetCurrentContext();
        if (!subtle_receiver_is_valid(isolate, local_context, info.This())) {
            isolate->ThrowException(v8::Exception::TypeError(js_string(isolate, "Illegal invocation")));
            return;
        }
        if (info.Length() > 0 && aes_cbc_algorithm(isolate, local_context, info[0])) {
            subtle_aes_cbc_decrypt(info);
            return;
        }
        subtle_aes_gcm(info, false);
    }

    static void subtle_sign(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* isolate = info.GetIsolate();
        auto local_context = isolate->GetCurrentContext();
        if (!subtle_receiver_is_valid(isolate, local_context, info.This())) {
            isolate->ThrowException(v8::Exception::TypeError(js_string(isolate, "Illegal invocation")));
            return;
        }
        auto resolver = v8::Promise::Resolver::New(local_context).ToLocalChecked();
        info.GetReturnValue().Set(resolver->GetPromise());
        const auto reject = [&](v8::Local<v8::Value> error) {
            resolver->Reject(local_context, error).FromMaybe(false);
        };
        auto* self = current(isolate);
        if (self == nullptr || info.Length() < 3) {
            reject(v8::Exception::TypeError(
                js_string(isolate, "HMAC sign requires algorithm, key, and data")));
            return;
        }
        if (!hmac_name(isolate, local_context, info[0])) {
            reject(crypto_dom_exception(isolate, local_context,
                "The requested signing algorithm is not supported", "NotSupportedError"));
            return;
        }
        secure_bytes data, key_material;
        std::size_t data_size = 0U;
        if (!copy_digest_input(info[2], data, data_size)) {
            reject(v8::Exception::TypeError(js_string(isolate, "HMAC data must be a BufferSource")));
            return;
        }
        if (data_size > maximum_cipher_input_bytes) {
            reject(crypto_dom_exception(isolate, local_context,
                "HMAC input exceeds the 16 MiB runtime bound", "OperationError"));
            return;
        }
        const auto realm = crypto_realm_id(isolate, local_context);
        const auto key = crypto_key_handle(isolate, local_context, info[1]);
        const auto metadata = self->crypto_keys.describe(key, realm);
        if (!metadata || metadata->algorithm != "HMAC"
            || !self->crypto_keys.use(key, realm, "sign", [&](const auto&, auto bytes) {
                key_material = secure_bytes(bytes);
            })) {
            reject(crypto_dom_exception(isolate, local_context,
                "The CryptoKey does not allow HMAC sign", "InvalidAccessError"));
            return;
        }
        const auto hash = metadata->hash == "SHA-1"
            ? crypto_digest_algorithm::sha1 : crypto_digest_algorithm::sha256;
        const auto request_bytes = data_size + key_material.size();
        if (self->pending_ciphers.size() >= maximum_pending_ciphers
            || request_bytes > maximum_pending_cipher_bytes
                - std::min(self->pending_cipher_bytes, maximum_pending_cipher_bytes)) {
            reject(crypto_dom_exception(isolate, local_context,
                "HMAC request capacity is exhausted", "OperationError"));
            return;
        }
        pending_cipher_task task;
        task.context.Reset(isolate, local_context);
        task.resolver.Reset(isolate, resolver);
        task.input_size = request_bytes;
        const auto token = task.stop.get_token();
        const auto notify = self->runtime_work_available;
        task.future = std::async(std::launch::async,
            [hash, key = std::move(key_material), data = std::move(data), token, notify]() mutable {
                async_cipher_result result;
                result.bytes = secure_bytes(crypto_digest_size(hash));
                result.status = crypto_hmac_sign(
                    hash, key.view(), data.view(), result.bytes.mutable_view(), token);
                key.clear(); data.clear();
                if (result.status != crypto_provider_status::success) result.bytes.clear();
                if (notify) notify();
                return result;
            });
        self->pending_cipher_bytes += request_bytes;
        self->pending_ciphers.push_back(std::move(task));
        self->record_feature(
            "web-api", "SubtleCrypto.sign(HMAC)", "supported",
            "bounded asynchronous SHA-1 and SHA-256 HMAC through pinned Mbed TLS",
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
        const auto realm = next_crypto_realm++;
        local_context->Global()->SetPrivate(
            local_context,
            crypto_private(isolate, "WebScene.CryptoRealm"),
            v8::BigInt::NewFromUnsigned(isolate, realm)).Check();
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
        struct subtle_method final {
            const char* name;
            v8::FunctionCallback callback;
            int length;
        };
        constexpr std::array<subtle_method, 6U> methods{{
            {"generateKey", subtle_generate_key, 3},
            {"importKey", subtle_import_key, 5},
            {"exportKey", subtle_export_key, 2},
            {"encrypt", subtle_encrypt, 3},
            {"decrypt", subtle_decrypt, 3},
            {"sign", subtle_sign, 3}}};
        for (const auto& method : methods) {
            auto function = v8::Function::New(
                local_context, method.callback, {}, method.length).ToLocalChecked();
            function->SetName(js_string(isolate, method.name));
            subtle_prototype->DefineOwnProperty(
                local_context, js_string(isolate, method.name), function, v8::DontEnum).Check();
        }
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

    bool has_ready_cipher_task() const noexcept
    {
        return std::any_of(
            pending_ciphers.begin(), pending_ciphers.end(), [](const auto& task) {
                return task.future.valid()
                    && task.future.wait_for(std::chrono::milliseconds::zero())
                        == std::future_status::ready;
            });
    }

    bool drain_cipher_task()
    {
        const auto ready = std::find_if(
            pending_ciphers.begin(), pending_ciphers.end(), [](const auto& task) {
                return task.future.valid()
                    && task.future.wait_for(std::chrono::milliseconds::zero())
                        == std::future_status::ready;
            });
        if (ready == pending_ciphers.end()) return true;
        auto task = std::move(*ready);
        pending_ciphers.erase(ready);
        pending_cipher_bytes = task.input_size > pending_cipher_bytes
            ? 0U : pending_cipher_bytes - task.input_size;
        auto task_context = task.context.Get(isolate);
        auto resolver = task.resolver.Get(isolate);
        async_cipher_result result;
        try {
            result = task.future.get();
        } catch (...) {
            result.status = crypto_provider_status::provider_failure;
        }
        if (task_context.IsEmpty() || resolver.IsEmpty()) return true;
        v8::Context::Scope task_context_scope(task_context);
        bool settled = false;
        if (result.status == crypto_provider_status::success) {
            auto output = v8::ArrayBuffer::New(isolate, result.bytes.size());
            if (!result.bytes.empty()) {
                std::memcpy(output->GetBackingStore()->Data(),
                    result.bytes.view().data(), result.bytes.size());
            }
            result.bytes.clear();
            settled = resolver->Resolve(task_context, output).FromMaybe(false);
        } else {
            const auto cancelled = result.status == crypto_provider_status::cancelled;
            result.bytes.clear();
            settled = resolver->Reject(
                task_context,
                crypto_dom_exception(
                    isolate, task_context,
                    cancelled ? "The cryptographic operation was cancelled"
                              : "The cryptographic operation failed",
                    cancelled ? "AbortError" : "OperationError"))
                .FromMaybe(false);
        }
        perform_microtask_checkpoint();
        return settled;
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
        for (auto& task : pending_ciphers) {
            if (task.context.Get(isolate) != local_context) continue;
            task.stop.request_stop();
            task.context.Reset();
            task.resolver.Reset();
        }
        const auto realm = crypto_realm_id(isolate, local_context);
        if (realm == 0U) return;
        for (auto& binding : crypto_key_bindings) {
            if (!binding || binding->released || binding->realm != realm) continue;
            static_cast<void>(crypto_keys.destroy(binding->key, realm));
            binding->wrapper.Reset();
            binding->released = true;
        }
        static_cast<void>(crypto_keys.clear_realm(realm));
    }
