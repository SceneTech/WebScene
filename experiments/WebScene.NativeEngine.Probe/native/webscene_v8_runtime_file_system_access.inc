    static void reject_file_panel_promise(
        v8::Isolate* isolate,
        v8::Local<v8::Context> local_context,
        v8::Local<v8::Promise::Resolver> resolver,
        std::string_view message,
        std::string_view name)
    {
        auto error = v8::Exception::Error(js_string(isolate, std::string(message).c_str()));
        if (error->IsObject()) {
            error.As<v8::Object>()->Set(
                local_context,
                js_string(isolate, "name"),
                js_string(isolate, std::string(name).c_str())).Check();
        }
        resolver->Reject(local_context, error).Check();
    }

    static bool file_panel_property(
        v8::Local<v8::Context> local_context,
        v8::Local<v8::Object> object,
        const char* name,
        v8::Local<v8::Value>& value)
    {
        return object->Get(
            local_context,
            js_string(v8::Isolate::GetCurrent(), name)).ToLocal(&value);
    }

    static void request_file_grant_same_entry(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto isolate = info.GetIsolate();
        auto local_context = isolate->GetCurrentContext();
        auto resolver = v8::Promise::Resolver::New(local_context).ToLocalChecked();
        info.GetReturnValue().Set(resolver->GetPromise());
        const auto reject = [&](std::string_view message, std::string_view name) {
            reject_file_panel_promise(
                isolate, local_context, resolver, message, name);
        };
        if (self == nullptr || !self->file_grant_same_entry_request_sink) {
            reject("Native file grant identity is unavailable", "NotSupportedError");
            return;
        }
        if (info.Length() != 2 || !info[0]->IsUint8Array()
            || !info[1]->IsUint8Array()) {
            reject("File grant identity requires two opaque tokens", "TypeError");
            return;
        }
        if (self->file_grant_same_entry_promise_targets.size()
            >= file_grant_same_entry_maximum_pending_v2) {
            reject("Too many pending file grant comparisons", "QuotaExceededError");
            return;
        }
        file_grant_same_entry_request_lease_v2 request;
        request.view.request_id = ++self->next_host_request_id;
        const auto copy_token = [](v8::Local<v8::Value> value,
                                   std::vector<std::uint8_t>& destination) {
            const auto bytes = value.As<v8::Uint8Array>();
            if (bytes->ByteLength() == 0
                || bytes->ByteLength() > file_panel_maximum_token_bytes_v2)
                return false;
            const auto store = bytes->Buffer()->GetBackingStore();
            const auto* first = static_cast<const std::uint8_t*>(store->Data())
                + bytes->ByteOffset();
            destination.assign(first, first + bytes->ByteLength());
            return true;
        };
        if (!copy_token(info[0], request.first_grant_id)
            || !copy_token(info[1], request.second_grant_id)) {
            reject("Malformed opaque file grant token", "DataError");
            return;
        }
        request.bind();
        file_grant_same_entry_promise_target target;
        target.context.Reset(isolate, local_context);
        target.resolver.Reset(isolate, resolver);
        self->file_grant_same_entry_promise_targets.emplace(
            request.view.request_id, std::move(target));
        if (!self->file_grant_same_entry_request_sink(request.view)) {
            self->file_grant_same_entry_promise_targets.erase(
                request.view.request_id);
            reject("The native file grant authority rejected the comparison",
                "NotAllowedError");
        }
    }

    static void request_file_grant_ancestry(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto isolate = info.GetIsolate();
        auto local_context = isolate->GetCurrentContext();
        auto resolver = v8::Promise::Resolver::New(local_context).ToLocalChecked();
        info.GetReturnValue().Set(resolver->GetPromise());
        const auto reject = [&](std::string_view message, std::string_view name) {
            reject_file_panel_promise(
                isolate, local_context, resolver, message, name);
        };
        if (self == nullptr || !self->file_grant_ancestry_request_sink) {
            reject("Native file grant ancestry is unavailable",
                "NotSupportedError");
            return;
        }
        if (info.Length() != 2 || !info[0]->IsUint8Array()
            || !info[1]->IsUint8Array()) {
            reject("File grant ancestry requires two opaque tokens", "TypeError");
            return;
        }
        if (self->file_grant_ancestry_promise_targets.size()
            >= WEBSCENE_FILE_GRANT_ANCESTRY_MAXIMUM_PENDING_OPERATIONS_V2) {
            reject("Too many pending file grant ancestry queries",
                "QuotaExceededError");
            return;
        }
        file_grant_ancestry_request_lease_v2 request;
        request.view.request_id = ++self->next_host_request_id;
        const auto copy_token = [](v8::Local<v8::Value> value,
                                   std::vector<std::uint8_t>& destination) {
            const auto bytes = value.As<v8::Uint8Array>();
            if (bytes->ByteLength() == 0
                || bytes->ByteLength() > file_panel_maximum_token_bytes_v2)
                return false;
            const auto store = bytes->Buffer()->GetBackingStore();
            const auto* first = static_cast<const std::uint8_t*>(store->Data())
                + bytes->ByteOffset();
            destination.assign(first, first + bytes->ByteLength());
            return true;
        };
        if (!copy_token(info[0], request.base_directory_grant_id)
            || !copy_token(
                info[1], request.possible_descendant_grant_id)) {
            reject("Malformed opaque file grant token", "DataError");
            return;
        }
        request.bind();
        file_grant_ancestry_promise_target target;
        target.context.Reset(isolate, local_context);
        target.resolver.Reset(isolate, resolver);
        self->file_grant_ancestry_promise_targets.emplace(
            request.view.request_id, std::move(target));
        if (!self->file_grant_ancestry_request_sink(request.view)) {
            self->file_grant_ancestry_promise_targets.erase(
                request.view.request_id);
            reject("The native file grant authority rejected the ancestry query",
                "NotAllowedError");
        }
    }

    static void request_file_grant_read(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto isolate = info.GetIsolate();
        auto local_context = isolate->GetCurrentContext();
        auto resolver = v8::Promise::Resolver::New(local_context).ToLocalChecked();
        info.GetReturnValue().Set(resolver->GetPromise());
        const auto reject = [&](std::string_view message, std::string_view name) {
            reject_file_panel_promise(
                isolate, local_context, resolver, message, name);
        };
        if (self == nullptr || !self->file_grant_read_request_sink) {
            reject("Native file grant reads are unavailable", "NotSupportedError");
            return;
        }
        if (info.Length() != 3 || !info[0]->IsUint8Array()
            || !info[1]->IsNumber() || !info[2]->IsUint32()) {
            reject("Malformed native file read", "TypeError");
            return;
        }
        if (self->file_grant_read_promise_targets.size()
            >= file_grant_read_maximum_pending_v2) {
            reject("Too many pending file reads", "QuotaExceededError");
            return;
        }
        const auto offset_value = info[1].As<v8::Number>()->Value();
        const auto maximum_bytes = info[2].As<v8::Uint32>()->Value();
        constexpr auto maximum_safe_integer = 9007199254740991.0;
        if (!std::isfinite(offset_value) || offset_value < 0
            || offset_value > maximum_safe_integer
            || std::floor(offset_value) != offset_value
            || maximum_bytes == 0
            || maximum_bytes > WEBSCENE_FILE_GRANT_READ_MAXIMUM_BYTES_V2) {
            reject("Native file read range is invalid", "TypeError");
            return;
        }
        const auto token = info[0].As<v8::Uint8Array>();
        if (token->ByteLength() == 0
            || token->ByteLength() > file_panel_maximum_token_bytes_v2) {
            reject("Malformed opaque file grant token", "DataError");
            return;
        }
        file_grant_read_request_lease_v2 request;
        request.view.request_id = ++self->next_host_request_id;
        request.view.offset = static_cast<std::uint64_t>(offset_value);
        request.view.maximum_bytes = maximum_bytes;
        const auto store = token->Buffer()->GetBackingStore();
        const auto* first = static_cast<const std::uint8_t*>(store->Data())
            + token->ByteOffset();
        request.grant_id.assign(first, first + token->ByteLength());
        request.bind();
        file_grant_read_promise_target target;
        target.context.Reset(isolate, local_context);
        target.resolver.Reset(isolate, resolver);
        self->file_grant_read_promise_targets.emplace(
            request.view.request_id, std::move(target));
        if (!self->file_grant_read_request_sink(request.view)) {
            self->file_grant_read_promise_targets.erase(request.view.request_id);
            reject("The native file authority rejected the read", "NotAllowedError");
        }
    }

    static void request_file_grant_write(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto isolate = info.GetIsolate();
        auto local_context = isolate->GetCurrentContext();
        auto resolver = v8::Promise::Resolver::New(local_context).ToLocalChecked();
        info.GetReturnValue().Set(resolver->GetPromise());
        const auto reject = [&](std::string_view message, std::string_view name) {
            reject_file_panel_promise(
                isolate, local_context, resolver, message, name);
        };
        if (self == nullptr || !self->file_grant_write_request_sink) {
            reject("Native file grant writes are unavailable", "NotSupportedError");
            return;
        }
        if (info.Length() != 6 || !info[0]->IsUint32()
            || !info[3]->IsNumber() || !info[5]->IsNumber()) {
            reject("Malformed native file write", "TypeError");
            return;
        }
        if (self->file_grant_write_promise_targets.size()
            >= file_grant_write_maximum_pending_v2) {
            reject("Too many pending file writes", "QuotaExceededError");
            return;
        }
        const auto action = info[0].As<v8::Uint32>()->Value();
        const auto offset_value = info[3].As<v8::Number>()->Value();
        const auto final_value = info[5].As<v8::Number>()->Value();
        constexpr auto maximum_safe_integer = 9007199254740991.0;
        if (!std::isfinite(offset_value) || offset_value < 0
            || std::floor(offset_value) != offset_value
            || offset_value > maximum_safe_integer
            || !std::isfinite(final_value) || final_value < 0
            || std::floor(final_value) != final_value
            || final_value > maximum_safe_integer) {
            reject("File write offsets must be non-negative safe integers",
                "TypeError");
            return;
        }
        file_grant_write_request_lease_v2 request;
        request.view.request_id = ++self->next_host_request_id;
        request.view.action = action;
        request.view.offset = static_cast<std::uint64_t>(offset_value);
        request.view.final_byte_count = static_cast<std::uint64_t>(final_value);
        const auto copy_bytes = [](v8::Local<v8::Value> value,
                                   std::vector<std::uint8_t>& destination,
                                   std::size_t maximum) {
            if (value->IsNullOrUndefined()) return true;
            if (!value->IsUint8Array()) return false;
            const auto bytes = value.As<v8::Uint8Array>();
            if (bytes->ByteLength() == 0 || bytes->ByteLength() > maximum)
                return false;
            const auto store = bytes->Buffer()->GetBackingStore();
            const auto* first = static_cast<const std::uint8_t*>(store->Data())
                + bytes->ByteOffset();
            destination.assign(first, first + bytes->ByteLength());
            return true;
        };
        if (!copy_bytes(info[1], request.grant_id,
                file_panel_maximum_token_bytes_v2)
            || !copy_bytes(info[2], request.transaction_id,
                file_panel_maximum_token_bytes_v2)
            || !copy_bytes(info[4], request.bytes,
                WEBSCENE_FILE_GRANT_WRITE_MAXIMUM_CHUNK_BYTES_V2)) {
            reject("Malformed native file write payload", "DataError");
            return;
        }
        request.bind();
        file_grant_write_promise_target target;
        target.context.Reset(isolate, local_context);
        target.resolver.Reset(isolate, resolver);
        self->file_grant_write_promise_targets.emplace(
            request.view.request_id, std::move(target));
        if (!self->file_grant_write_request_sink(request.view)) {
            self->file_grant_write_promise_targets.erase(request.view.request_id);
            reject("The native file grant authority rejected the write",
                "NotAllowedError");
        }
    }

    static void request_file_grant_directory(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto isolate = info.GetIsolate();
        auto local_context = isolate->GetCurrentContext();
        auto resolver = v8::Promise::Resolver::New(local_context).ToLocalChecked();
        info.GetReturnValue().Set(resolver->GetPromise());
        const auto reject = [&](std::string_view message, std::string_view name) {
            reject_file_panel_promise(
                isolate, local_context, resolver, message, name);
        };
        if (self == nullptr || !self->file_grant_directory_request_sink) {
            reject("Native directory enumeration is unavailable",
                "NotSupportedError");
            return;
        }
        if (info.Length() != 4 || !info[0]->IsUint32()
            || !info[3]->IsUint32()) {
            reject("Malformed native directory request", "TypeError");
            return;
        }
        if (self->file_grant_directory_promise_targets.size()
            >= WEBSCENE_FILE_GRANT_DIRECTORY_MAXIMUM_PENDING_OPERATIONS_V2) {
            reject("Too many pending directory operations", "QuotaExceededError");
            return;
        }
        file_grant_directory_request_lease_v2 request;
        request.view.request_id = ++self->next_host_request_id;
        request.view.action = info[0].As<v8::Uint32>()->Value();
        request.view.maximum_entries = info[3].As<v8::Uint32>()->Value();
        const auto copy_token = [](v8::Local<v8::Value> value,
                                   std::vector<std::uint8_t>& destination) {
            if (value->IsNullOrUndefined()) return true;
            if (!value->IsUint8Array()) return false;
            const auto bytes = value.As<v8::Uint8Array>();
            if (bytes->ByteLength() == 0
                || bytes->ByteLength() > file_panel_maximum_token_bytes_v2)
                return false;
            const auto store = bytes->Buffer()->GetBackingStore();
            const auto* first = static_cast<const std::uint8_t*>(store->Data())
                + bytes->ByteOffset();
            destination.assign(first, first + bytes->ByteLength());
            return true;
        };
        if (!copy_token(info[1], request.directory_grant_id)
            || !copy_token(info[2], request.cursor)) {
            reject("Malformed opaque directory token", "DataError");
            return;
        }
        request.bind();
        file_grant_directory_promise_target target;
        target.context.Reset(isolate, local_context);
        target.resolver.Reset(isolate, resolver);
        self->file_grant_directory_promise_targets.emplace(
            request.view.request_id, std::move(target));
        if (!self->file_grant_directory_request_sink(request.view)) {
            self->file_grant_directory_promise_targets.erase(
                request.view.request_id);
            reject("The native directory authority rejected the request",
                "NotAllowedError");
        }
    }

    static void request_file_grant_create_file(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto isolate = info.GetIsolate();
        auto local_context = isolate->GetCurrentContext();
        auto resolver = v8::Promise::Resolver::New(local_context).ToLocalChecked();
        info.GetReturnValue().Set(resolver->GetPromise());
        const auto reject = [&](std::string_view message, std::string_view name) {
            reject_file_panel_promise(
                isolate, local_context, resolver, message, name);
        };
        if (self == nullptr || !self->file_grant_create_file_request_sink) {
            reject("Native file creation is unavailable", "NotSupportedError");
            return;
        }
        if (info.Length() != 2 || !info[0]->IsUint8Array()
            || !info[1]->IsString()) {
            reject("Malformed native file creation request", "TypeError");
            return;
        }
        if (self->file_grant_create_file_promise_targets.size()
            >= WEBSCENE_FILE_GRANT_CREATE_FILE_MAXIMUM_PENDING_OPERATIONS_V2) {
            reject("Too many pending file creation operations",
                "QuotaExceededError");
            return;
        }
        file_grant_create_file_request_lease_v2 request;
        request.view.request_id = ++self->next_host_request_id;
        const auto token = info[0].As<v8::Uint8Array>();
        if (token->ByteLength() == 0
            || token->ByteLength() > file_panel_maximum_token_bytes_v2) {
            reject("Malformed opaque directory token", "DataError");
            return;
        }
        const auto store = token->Buffer()->GetBackingStore();
        const auto* first = static_cast<const std::uint8_t*>(store->Data())
            + token->ByteOffset();
        request.directory_grant_id.assign(first, first + token->ByteLength());
        v8::String::Utf8Value name(isolate, info[1]);
        if (*name == nullptr || name.length() <= 0
            || static_cast<std::size_t>(name.length())
                > WEBSCENE_FILE_GRANT_CREATE_FILE_MAXIMUM_NAME_BYTES_V2) {
            reject("Malformed file child name", "TypeError");
            return;
        }
        request.display_name.assign(*name,
            static_cast<std::size_t>(name.length()));
        request.bind();
        file_grant_create_file_promise_target target;
        target.context.Reset(isolate, local_context);
        target.resolver.Reset(isolate, resolver);
        self->file_grant_create_file_promise_targets.emplace(
            request.view.request_id, std::move(target));
        if (!self->file_grant_create_file_request_sink(request.view)) {
            self->file_grant_create_file_promise_targets.erase(
                request.view.request_id);
            reject("The native file creation authority rejected the request",
                "NotAllowedError");
        }
    }

    static void request_file_grant_create_directory(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto isolate = info.GetIsolate();
        auto local_context = isolate->GetCurrentContext();
        auto resolver = v8::Promise::Resolver::New(local_context).ToLocalChecked();
        info.GetReturnValue().Set(resolver->GetPromise());
        const auto reject = [&](std::string_view message, std::string_view name) {
            reject_file_panel_promise(
                isolate, local_context, resolver, message, name);
        };
        if (self == nullptr || !self->file_grant_create_directory_request_sink) {
            reject("Native directory creation is unavailable", "NotSupportedError");
            return;
        }
        if (info.Length() != 2 || !info[0]->IsUint8Array()
            || !info[1]->IsString()) {
            reject("Malformed native directory creation request", "TypeError");
            return;
        }
        if (self->file_grant_create_directory_promise_targets.size()
            >= WEBSCENE_FILE_GRANT_CREATE_DIRECTORY_MAXIMUM_PENDING_OPERATIONS_V2) {
            reject("Too many pending directory creation operations",
                "QuotaExceededError");
            return;
        }
        file_grant_create_directory_request_lease_v2 request;
        request.view.request_id = ++self->next_host_request_id;
        const auto token = info[0].As<v8::Uint8Array>();
        if (token->ByteLength() == 0
            || token->ByteLength() > file_panel_maximum_token_bytes_v2) {
            reject("Malformed opaque directory token", "DataError");
            return;
        }
        const auto store = token->Buffer()->GetBackingStore();
        const auto* first = static_cast<const std::uint8_t*>(store->Data())
            + token->ByteOffset();
        request.directory_grant_id.assign(first, first + token->ByteLength());
        v8::String::Utf8Value name(isolate, info[1]);
        if (*name == nullptr || name.length() <= 0
            || static_cast<std::size_t>(name.length())
                > WEBSCENE_FILE_GRANT_CREATE_DIRECTORY_MAXIMUM_NAME_BYTES_V2) {
            reject("Malformed directory child name", "TypeError");
            return;
        }
        request.display_name.assign(*name,
            static_cast<std::size_t>(name.length()));
        request.bind();
        file_grant_create_directory_promise_target target;
        target.context.Reset(isolate, local_context);
        target.resolver.Reset(isolate, resolver);
        self->file_grant_create_directory_promise_targets.emplace(
            request.view.request_id, std::move(target));
        if (!self->file_grant_create_directory_request_sink(request.view)) {
            self->file_grant_create_directory_promise_targets.erase(
                request.view.request_id);
            reject("The native directory creation authority rejected the request",
                "NotAllowedError");
        }
    }

    static void request_file_grant_remove(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto isolate = info.GetIsolate();
        auto local_context = isolate->GetCurrentContext();
        auto resolver = v8::Promise::Resolver::New(local_context).ToLocalChecked();
        info.GetReturnValue().Set(resolver->GetPromise());
        const auto reject = [&](std::string_view message, std::string_view name) {
            reject_file_panel_promise(
                isolate, local_context, resolver, message, name);
        };
        if (self == nullptr || !self->file_grant_remove_request_sink) {
            reject("Native entry removal is unavailable", "NotSupportedError");
            return;
        }
        if (info.Length() != 3 || !info[0]->IsUint8Array()
            || !info[1]->IsString() || !info[2]->IsBoolean()) {
            reject("Malformed native entry removal request", "TypeError");
            return;
        }
        if (self->file_grant_remove_promise_targets.size()
            >= WEBSCENE_FILE_GRANT_REMOVE_MAXIMUM_PENDING_OPERATIONS_V2) {
            reject("Too many pending entry removal operations",
                "QuotaExceededError");
            return;
        }
        file_grant_remove_request_lease_v2 request;
        request.view.request_id = ++self->next_host_request_id;
        const auto token = info[0].As<v8::Uint8Array>();
        if (token->ByteLength() == 0
            || token->ByteLength() > file_panel_maximum_token_bytes_v2) {
            reject("Malformed opaque directory token", "DataError");
            return;
        }
        const auto store = token->Buffer()->GetBackingStore();
        const auto* first = static_cast<const std::uint8_t*>(store->Data())
            + token->ByteOffset();
        request.directory_grant_id.assign(first, first + token->ByteLength());
        v8::String::Utf8Value name(isolate, info[1]);
        if (*name == nullptr || name.length() <= 0
            || static_cast<std::size_t>(name.length())
                > WEBSCENE_FILE_GRANT_REMOVE_MAXIMUM_NAME_BYTES_V2) {
            reject("Malformed entry name", "TypeError");
            return;
        }
        request.display_name.assign(*name,
            static_cast<std::size_t>(name.length()));
        request.view.recursive = info[2]->BooleanValue(isolate) ? 1U : 0U;
        request.bind();
        file_grant_remove_promise_target target;
        target.context.Reset(isolate, local_context);
        target.resolver.Reset(isolate, resolver);
        self->file_grant_remove_promise_targets.emplace(
            request.view.request_id, std::move(target));
        if (!self->file_grant_remove_request_sink(request.view)) {
            self->file_grant_remove_promise_targets.erase(
                request.view.request_id);
            reject("The native removal authority rejected the request",
                "NotAllowedError");
        }
    }

    static void request_file_panel(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto isolate = info.GetIsolate();
        auto local_context = isolate->GetCurrentContext();
        auto resolver = v8::Promise::Resolver::New(local_context).ToLocalChecked();
        info.GetReturnValue().Set(resolver->GetPromise());
        const auto reject = [&](std::string_view message, std::string_view name) {
            reject_file_panel_promise(
                isolate, local_context, resolver, message, name);
        };
        if (self == nullptr || !self->file_panel_request_sink
            || info.Length() < 2 || !info[0]->IsUint32()
            || !info[1]->IsObject()) {
            reject("Native file pickers are unavailable", "NotSupportedError");
            return;
        }
        v8::Local<v8::Value> secure_context;
        if (!local_context->Global()->Get(
                local_context, js_string(isolate, "isSecureContext"))
                .ToLocal(&secure_context)
            || !secure_context->IsTrue()) {
            reject("File pickers require a secure context", "SecurityError");
            return;
        }
        constexpr auto user_activation_lifetime = std::chrono::seconds(5);
        if (self->last_user_activation.time_since_epoch().count() == 0
            || std::chrono::steady_clock::now() - self->last_user_activation
                > user_activation_lifetime) {
            reject("File pickers require recent native user activation",
                "SecurityError");
            return;
        }
        if (self->file_panel_promise_targets.size()
            >= file_panel_maximum_pending_v2) {
            reject("Too many pending file picker requests", "QuotaExceededError");
            return;
        }

        file_panel_request_lease_v2 request;
        request.view.request_id = ++self->next_host_request_id;
        request.view.kind = info[0].As<v8::Uint32>()->Value();
        request.view.flags = 0U;
        request.view.maximum_selection_count = 1U;
        auto options = info[1].As<v8::Object>();
        v8::Local<v8::Value> value;
        const auto boolean_option = [&](const char* name, bool& result) {
            if (!file_panel_property(local_context, options, name, value)
                || !value->IsBoolean()) return false;
            result = value->BooleanValue(isolate);
            return true;
        };
        bool multiple = false;
        bool exclude_accept_all = false;
        bool request_write = false;
        if (!boolean_option("multiple", multiple)
            || !boolean_option("excludeAcceptAllOption", exclude_accept_all)
            || !boolean_option("requestWrite", request_write)) {
            reject("Malformed file picker options", "TypeError");
            return;
        }
        if (multiple) {
            request.view.flags |= WEBSCENE_FILE_PANEL_ALLOW_MULTIPLE_V2;
            request.view.maximum_selection_count =
                static_cast<uint32_t>(file_panel_maximum_entries_v2);
        }
        if (exclude_accept_all)
            request.view.flags |= WEBSCENE_FILE_PANEL_EXCLUDE_ACCEPT_ALL_V2;
        if (request_write)
            request.view.flags |= WEBSCENE_FILE_PANEL_REQUEST_WRITE_V2;
        if (request.view.kind == WEBSCENE_FILE_PANEL_SAVE_FILE_V2) {
            request.view.flags |= WEBSCENE_FILE_PANEL_CAN_CREATE_DIRECTORIES_V2
                | WEBSCENE_FILE_PANEL_CONFIRM_OVERWRITE_V2;
        } else if (request.view.kind == WEBSCENE_FILE_PANEL_OPEN_DIRECTORY_V2) {
            request.view.flags |= WEBSCENE_FILE_PANEL_CAN_CREATE_DIRECTORIES_V2;
        }
        if (!file_panel_property(local_context, options, "suggestedName", value)
            || !value->IsString()) {
            reject("Malformed suggested file name", "TypeError");
            return;
        }
        request.suggested_name = to_utf8(isolate, value);

        if (!file_panel_property(local_context, options, "startIn", value)
            || !value->IsUint8Array()) {
            reject("Malformed initial file picker location", "TypeError");
            return;
        }
        auto start_in = value.As<v8::Uint8Array>();
        auto start_in_store = start_in->Buffer()->GetBackingStore();
        const auto* start_in_bytes = static_cast<const uint8_t*>(
            start_in_store->Data()) + start_in->ByteOffset();
        if (start_in->ByteLength() != 0U) {
            request.initial_location_token.assign(
                start_in_bytes, start_in_bytes + start_in->ByteLength());
        }

        if (!file_panel_property(local_context, options, "types", value)
            || !value->IsArray()) {
            reject("Malformed file picker filters", "TypeError");
            return;
        }
        auto filters = value.As<v8::Array>();
        if (filters->Length() > file_panel_maximum_filters_v2) {
            reject("Too many file picker filters", "TypeError");
            return;
        }
        for (uint32_t filter_index = 0; filter_index < filters->Length();
             ++filter_index) {
            v8::Local<v8::Value> filter_value;
            if (!filters->Get(local_context, filter_index).ToLocal(&filter_value)
                || !filter_value->IsObject()) {
                reject("Malformed file picker filter", "TypeError");
                return;
            }
            auto filter_object = filter_value.As<v8::Object>();
            file_panel_filter_data_v2 filter;
            if (!file_panel_property(
                    local_context, filter_object, "description", value)
                || !value->IsString()) {
                reject("Malformed file picker filter description", "TypeError");
                return;
            }
            filter.description = to_utf8(isolate, value);
            if (!file_panel_property(local_context, filter_object, "accept", value)
                || !value->IsArray()) {
                reject("Malformed file picker accept list", "TypeError");
                return;
            }
            auto accept = value.As<v8::Array>();
            for (uint32_t accept_index = 0; accept_index < accept->Length();
                 ++accept_index) {
                v8::Local<v8::Value> accept_value;
                if (!accept->Get(local_context, accept_index).ToLocal(&accept_value)
                    || !accept_value->IsObject()) {
                    reject("Malformed file picker accept entry", "TypeError");
                    return;
                }
                auto accept_object = accept_value.As<v8::Object>();
                if (!file_panel_property(
                        local_context, accept_object, "mime", value)
                    || !value->IsString()) {
                    reject("Malformed file picker MIME type", "TypeError");
                    return;
                }
                filter.mime_types.push_back(to_utf8(isolate, value));
                if (!file_panel_property(
                        local_context, accept_object, "extensions", value)
                    || !value->IsArray()) {
                    reject("Malformed file picker extension list", "TypeError");
                    return;
                }
                auto extensions = value.As<v8::Array>();
                for (uint32_t extension_index = 0;
                     extension_index < extensions->Length(); ++extension_index) {
                    v8::Local<v8::Value> extension;
                    if (!extensions->Get(local_context, extension_index)
                            .ToLocal(&extension)
                        || !extension->IsString()) {
                        reject("Malformed file picker extension", "TypeError");
                        return;
                    }
                    filter.extensions.push_back(to_utf8(isolate, extension));
                }
            }
            request.filters.push_back(std::move(filter));
        }
        request.bind();

        file_panel_promise_target target;
        target.kind = request.view.kind;
        target.context.Reset(isolate, local_context);
        target.resolver.Reset(isolate, resolver);
        self->file_panel_promise_targets.emplace(
            request.view.request_id, std::move(target));
        // File picker activation is transient and cannot authorize a replay.
        self->last_user_activation = {};
        if (!self->file_panel_request_sink(request.view)) {
            self->file_panel_promise_targets.erase(request.view.request_id);
            reject("The native file picker rejected the request",
                "NotAllowedError");
        }
    }

    void complete_native_file_panel(file_panel_completion_data_v2& completion)
    {
        const auto found = file_panel_promise_targets.find(completion.request_id);
        if (found == file_panel_promise_targets.end()) return;
        auto target = std::move(found->second);
        file_panel_promise_targets.erase(found);
        auto local_context = target.context.Get(isolate);
        auto resolver = target.resolver.Get(isolate);
        if (local_context.IsEmpty() || resolver.IsEmpty()) return;
        v8::Context::Scope context_scope(local_context);
        if (completion.status != WEBSCENE_FILE_PANEL_SUCCESS_V2) {
            const auto name = completion.status == WEBSCENE_FILE_PANEL_CANCELLED_V2
                ? "AbortError"
                : completion.status == WEBSCENE_FILE_PANEL_DENIED_V2
                    ? "NotAllowedError" : "OperationError";
            const auto message = completion.error_message.empty()
                ? completion.status == WEBSCENE_FILE_PANEL_CANCELLED_V2
                    ? "The file picker was cancelled"
                    : completion.status == WEBSCENE_FILE_PANEL_DENIED_V2
                        ? "The file picker permission was denied"
                        : "The file picker failed"
                : completion.error_message;
            reject_file_panel_promise(
                isolate, local_context, resolver, message, name);
            perform_microtask_checkpoint();
            return;
        }
        auto entries = v8::Array::New(
            isolate, static_cast<int>(completion.entries.size()));
        for (uint32_t index = 0; index < completion.entries.size(); ++index) {
            const auto& entry = completion.entries[index];
            auto raw = v8::Object::New(isolate);
            raw->Set(local_context, js_string(isolate, "kind"),
                v8::Integer::NewFromUnsigned(isolate, entry.kind)).Check();
            raw->Set(local_context, js_string(isolate, "capabilities"),
                v8::Integer::NewFromUnsigned(isolate, entry.capabilities)).Check();
            raw->Set(local_context, js_string(isolate, "name"),
                js_string(isolate, entry.display_name.c_str())).Check();
            auto grant_buffer = v8::ArrayBuffer::New(isolate, entry.grant_id.size());
            if (!entry.grant_id.empty()) {
                std::memcpy(grant_buffer->GetBackingStore()->Data(),
                    entry.grant_id.data(), entry.grant_id.size());
            }
            raw->Set(local_context, js_string(isolate, "grant"),
                v8::Uint8Array::New(grant_buffer, 0, entry.grant_id.size())).Check();
            entries->Set(local_context, index, raw).Check();
        }
        resolver->Resolve(local_context, entries).Check();
        perform_microtask_checkpoint();
    }

    void cancel_file_panel_promises(const char* message)
    {
        for (auto& [_, target] : file_panel_promise_targets) {
            auto local_context = target.context.Get(isolate);
            auto resolver = target.resolver.Get(isolate);
            if (local_context.IsEmpty() || resolver.IsEmpty()) continue;
            v8::Context::Scope context_scope(local_context);
            reject_file_panel_promise(
                isolate, local_context, resolver, message, "AbortError");
        }
        file_panel_promise_targets.clear();
        perform_microtask_checkpoint();
    }

    void complete_native_file_grant_same_entry(
        file_grant_same_entry_completion_data_v2& completion)
    {
        const auto found = file_grant_same_entry_promise_targets.find(
            completion.request_id);
        if (found == file_grant_same_entry_promise_targets.end()) return;
        auto target = std::move(found->second);
        file_grant_same_entry_promise_targets.erase(found);
        auto local_context = target.context.Get(isolate);
        auto resolver = target.resolver.Get(isolate);
        if (local_context.IsEmpty() || resolver.IsEmpty()) return;
        v8::Context::Scope context_scope(local_context);
        resolver->Resolve(local_context,
            v8::Boolean::New(isolate,
                completion.admitted && completion.same_entry)).Check();
        perform_microtask_checkpoint();
    }

    void cancel_file_grant_same_entry_promises(const char* message)
    {
        for (auto& [_, target] : file_grant_same_entry_promise_targets) {
            auto local_context = target.context.Get(isolate);
            auto resolver = target.resolver.Get(isolate);
            if (local_context.IsEmpty() || resolver.IsEmpty()) continue;
            v8::Context::Scope context_scope(local_context);
            reject_file_panel_promise(
                isolate, local_context, resolver, message, "AbortError");
        }
        file_grant_same_entry_promise_targets.clear();
        perform_microtask_checkpoint();
    }

    void complete_native_file_grant_ancestry(
        file_grant_ancestry_completion_data_v2& completion)
    {
        const auto found = file_grant_ancestry_promise_targets.find(
            completion.request_id);
        if (found == file_grant_ancestry_promise_targets.end()) return;
        auto target = std::move(found->second);
        file_grant_ancestry_promise_targets.erase(found);
        auto local_context = target.context.Get(isolate);
        auto resolver = target.resolver.Get(isolate);
        if (local_context.IsEmpty() || resolver.IsEmpty()) return;
        v8::Context::Scope context_scope(local_context);
        if (completion.status
            == WEBSCENE_FILE_GRANT_ANCESTRY_NOT_DESCENDANT_V2) {
            resolver->Resolve(local_context, v8::Null(isolate)).Check();
            perform_microtask_checkpoint();
            return;
        }
        if (completion.status != WEBSCENE_FILE_GRANT_ANCESTRY_SUCCESS_V2) {
            const auto name = completion.status
                    == WEBSCENE_FILE_GRANT_ANCESTRY_CANCELLED_V2
                ? "AbortError"
                : completion.status == WEBSCENE_FILE_GRANT_ANCESTRY_DENIED_V2
                    ? "NotAllowedError"
                    : completion.status
                            == WEBSCENE_FILE_GRANT_ANCESTRY_NOT_FOUND_V2
                        ? "NotFoundError"
                        : completion.status
                                == WEBSCENE_FILE_GRANT_ANCESTRY_CHANGED_V2
                            ? "InvalidStateError"
                            : completion.status
                                    == WEBSCENE_FILE_GRANT_ANCESTRY_LIMIT_V2
                                ? "QuotaExceededError" : "NotReadableError";
            reject_file_panel_promise(isolate, local_context, resolver,
                "The native file grant ancestry query failed", name);
            perform_microtask_checkpoint();
            return;
        }
        auto result = v8::Array::New(
            isolate, static_cast<int>(completion.components.size()));
        for (std::size_t index = 0; index < completion.components.size(); ++index) {
            result->Set(local_context, static_cast<std::uint32_t>(index),
                js_string(isolate, completion.components[index].c_str())).Check();
        }
        resolver->Resolve(local_context, result).Check();
        perform_microtask_checkpoint();
    }

    void cancel_file_grant_ancestry_promises(const char* message)
    {
        for (auto& [_, target] : file_grant_ancestry_promise_targets) {
            auto local_context = target.context.Get(isolate);
            auto resolver = target.resolver.Get(isolate);
            if (local_context.IsEmpty() || resolver.IsEmpty()) continue;
            v8::Context::Scope context_scope(local_context);
            reject_file_panel_promise(
                isolate, local_context, resolver, message, "AbortError");
        }
        file_grant_ancestry_promise_targets.clear();
        perform_microtask_checkpoint();
    }

    void complete_native_file_grant_read(
        file_grant_read_completion_data_v2& completion)
    {
        const auto found = file_grant_read_promise_targets.find(
            completion.request_id);
        if (found == file_grant_read_promise_targets.end()) return;
        auto target = std::move(found->second);
        file_grant_read_promise_targets.erase(found);
        auto local_context = target.context.Get(isolate);
        auto resolver = target.resolver.Get(isolate);
        if (local_context.IsEmpty() || resolver.IsEmpty()) return;
        v8::Context::Scope context_scope(local_context);
        if (completion.status != WEBSCENE_FILE_GRANT_READ_SUCCESS_V2) {
            const auto name = completion.status == WEBSCENE_FILE_GRANT_READ_CANCELLED_V2
                ? "AbortError"
                : completion.status == WEBSCENE_FILE_GRANT_READ_DENIED_V2
                    ? "NotAllowedError"
                    : completion.status == WEBSCENE_FILE_GRANT_READ_NOT_FOUND_V2
                        ? "NotFoundError" : "NotReadableError";
            reject_file_panel_promise(isolate, local_context, resolver,
                "The native file read failed", name);
            perform_microtask_checkpoint();
            return;
        }
        constexpr std::uint64_t maximum_safe_integer = 9007199254740991ULL;
        if (completion.byte_count > maximum_safe_integer) {
            reject_file_panel_promise(isolate, local_context, resolver,
                "The native file exceeds JavaScript's safe size range", "NotReadableError");
            perform_microtask_checkpoint();
            return;
        }
        auto result = v8::Object::New(isolate);
        result->Set(local_context, js_string(isolate, "size"),
            v8::Number::New(isolate,
                static_cast<double>(completion.byte_count))).Check();
        result->Set(local_context, js_string(isolate, "lastModified"),
            v8::Number::New(isolate,
                static_cast<double>(completion.modification_time_ns) / 1'000'000.0))
            .Check();
        result->Set(local_context, js_string(isolate, "modificationTimeNs"),
            v8::BigInt::New(isolate, completion.modification_time_ns)).Check();
        result->Set(local_context, js_string(isolate, "offset"),
            v8::Number::New(isolate,
                static_cast<double>(completion.offset))).Check();
        auto buffer = v8::ArrayBuffer::New(isolate, completion.bytes.size());
        if (!completion.bytes.empty()) {
            std::memcpy(buffer->GetBackingStore()->Data(),
                completion.bytes.data(), completion.bytes.size());
        }
        result->Set(local_context, js_string(isolate, "bytes"),
            v8::Uint8Array::New(buffer, 0, completion.bytes.size())).Check();
        result->Set(local_context, js_string(isolate, "eof"),
            v8::Boolean::New(isolate, completion.eof)).Check();
        resolver->Resolve(local_context, result).Check();
        perform_microtask_checkpoint();
    }

    void cancel_file_grant_read_promises(const char* message)
    {
        for (auto& [_, target] : file_grant_read_promise_targets) {
            auto local_context = target.context.Get(isolate);
            auto resolver = target.resolver.Get(isolate);
            if (local_context.IsEmpty() || resolver.IsEmpty()) continue;
            v8::Context::Scope context_scope(local_context);
            reject_file_panel_promise(
                isolate, local_context, resolver, message, "AbortError");
        }
        file_grant_read_promise_targets.clear();
        perform_microtask_checkpoint();
    }

    void complete_native_file_grant_write(
        file_grant_write_completion_data_v2& completion)
    {
        const auto found = file_grant_write_promise_targets.find(
            completion.request_id);
        if (found == file_grant_write_promise_targets.end()) return;
        auto target = std::move(found->second);
        file_grant_write_promise_targets.erase(found);
        auto local_context = target.context.Get(isolate);
        auto resolver = target.resolver.Get(isolate);
        if (local_context.IsEmpty() || resolver.IsEmpty()) return;
        v8::Context::Scope context_scope(local_context);
        if (completion.status != WEBSCENE_FILE_GRANT_WRITE_SUCCESS_V2) {
            const auto name = completion.status == WEBSCENE_FILE_GRANT_WRITE_CANCELLED_V2
                ? "AbortError"
                : completion.status == WEBSCENE_FILE_GRANT_WRITE_DENIED_V2
                    ? "NotAllowedError"
                    : completion.status == WEBSCENE_FILE_GRANT_WRITE_NOT_FOUND_V2
                        ? "NotFoundError"
                        : completion.status == WEBSCENE_FILE_GRANT_WRITE_LIMIT_V2
                            ? "QuotaExceededError" : "NotReadableError";
            reject_file_panel_promise(isolate, local_context, resolver,
                "The native file write failed", name);
            perform_microtask_checkpoint();
            return;
        }
        auto result = v8::Object::New(isolate);
        auto transaction = v8::ArrayBuffer::New(
            isolate, completion.transaction_id.size());
        if (!completion.transaction_id.empty()) {
            std::memcpy(transaction->GetBackingStore()->Data(),
                completion.transaction_id.data(),
                completion.transaction_id.size());
        }
        result->Set(local_context, js_string(isolate, "transaction"),
            v8::Uint8Array::New(transaction, 0,
                completion.transaction_id.size())).Check();
        result->Set(local_context, js_string(isolate, "size"),
            v8::Number::New(isolate,
                static_cast<double>(completion.byte_count))).Check();
        result->Set(local_context, js_string(isolate, "lastModified"),
            v8::Number::New(isolate,
                static_cast<double>(completion.modification_time_ns)
                    / 1'000'000.0)).Check();
        resolver->Resolve(local_context, result).Check();
        perform_microtask_checkpoint();
    }

    void cancel_file_grant_write_promises(const char* message)
    {
        for (auto& [_, target] : file_grant_write_promise_targets) {
            auto local_context = target.context.Get(isolate);
            auto resolver = target.resolver.Get(isolate);
            if (local_context.IsEmpty() || resolver.IsEmpty()) continue;
            v8::Context::Scope context_scope(local_context);
            reject_file_panel_promise(
                isolate, local_context, resolver, message, "AbortError");
        }
        file_grant_write_promise_targets.clear();
        perform_microtask_checkpoint();
    }

    void complete_native_file_grant_directory(
        file_grant_directory_completion_data_v2& completion)
    {
        const auto found = file_grant_directory_promise_targets.find(
            completion.request_id);
        if (found == file_grant_directory_promise_targets.end()) return;
        auto target = std::move(found->second);
        file_grant_directory_promise_targets.erase(found);
        auto local_context = target.context.Get(isolate);
        auto resolver = target.resolver.Get(isolate);
        if (local_context.IsEmpty() || resolver.IsEmpty()) return;
        v8::Context::Scope context_scope(local_context);
        if (completion.status != WEBSCENE_FILE_GRANT_DIRECTORY_SUCCESS_V2) {
            const auto name = completion.status
                    == WEBSCENE_FILE_GRANT_DIRECTORY_CANCELLED_V2
                ? "AbortError"
                : completion.status == WEBSCENE_FILE_GRANT_DIRECTORY_DENIED_V2
                    ? "NotAllowedError"
                    : completion.status
                            == WEBSCENE_FILE_GRANT_DIRECTORY_NOT_FOUND_V2
                        ? "NotFoundError"
                        : completion.status
                                == WEBSCENE_FILE_GRANT_DIRECTORY_CHANGED_V2
                            ? "InvalidStateError"
                            : completion.status
                                    == WEBSCENE_FILE_GRANT_DIRECTORY_LIMIT_V2
                                ? "QuotaExceededError" : "NotReadableError";
            reject_file_panel_promise(isolate, local_context, resolver,
                "The native directory operation failed", name);
            perform_microtask_checkpoint();
            return;
        }
        auto result = v8::Object::New(isolate);
        auto entries = v8::Array::New(
            isolate, static_cast<int>(completion.entries.size()));
        for (std::size_t index = 0; index < completion.entries.size(); ++index) {
            const auto& entry = completion.entries[index];
            auto value = v8::Object::New(isolate);
            value->Set(local_context, js_string(isolate, "kind"),
                v8::Integer::NewFromUnsigned(isolate, entry.kind)).Check();
            value->Set(local_context, js_string(isolate, "capabilities"),
                v8::Integer::NewFromUnsigned(isolate, entry.capabilities)).Check();
            value->Set(local_context, js_string(isolate, "name"),
                js_string(isolate, entry.display_name.c_str())).Check();
            auto grant = v8::ArrayBuffer::New(isolate, entry.grant_id.size());
            if (!entry.grant_id.empty()) std::memcpy(
                grant->GetBackingStore()->Data(), entry.grant_id.data(),
                entry.grant_id.size());
            value->Set(local_context, js_string(isolate, "grant"),
                v8::Uint8Array::New(grant, 0, entry.grant_id.size())).Check();
            entries->Set(local_context, static_cast<std::uint32_t>(index), value)
                .Check();
        }
        auto cursor = v8::ArrayBuffer::New(
            isolate, completion.next_cursor.size());
        if (!completion.next_cursor.empty()) std::memcpy(
            cursor->GetBackingStore()->Data(), completion.next_cursor.data(),
            completion.next_cursor.size());
        result->Set(local_context, js_string(isolate, "entries"), entries).Check();
        result->Set(local_context, js_string(isolate, "cursor"),
            v8::Uint8Array::New(cursor, 0, completion.next_cursor.size())).Check();
        result->Set(local_context, js_string(isolate, "skippedSymlinks"),
            v8::BigInt::NewFromUnsigned(isolate,
                completion.skipped_symlinks)).Check();
        resolver->Resolve(local_context, result).Check();
        perform_microtask_checkpoint();
    }

    void cancel_file_grant_directory_promises(const char* message)
    {
        for (auto& [_, target] : file_grant_directory_promise_targets) {
            auto local_context = target.context.Get(isolate);
            auto resolver = target.resolver.Get(isolate);
            if (local_context.IsEmpty() || resolver.IsEmpty()) continue;
            v8::Context::Scope context_scope(local_context);
            reject_file_panel_promise(
                isolate, local_context, resolver, message, "AbortError");
        }
        file_grant_directory_promise_targets.clear();
        perform_microtask_checkpoint();
    }

    void complete_native_file_grant_create_file(
        file_grant_create_file_completion_data_v2& completion)
    {
        const auto found = file_grant_create_file_promise_targets.find(
            completion.request_id);
        if (found == file_grant_create_file_promise_targets.end()) return;
        auto target = std::move(found->second);
        file_grant_create_file_promise_targets.erase(found);
        auto local_context = target.context.Get(isolate);
        auto resolver = target.resolver.Get(isolate);
        if (local_context.IsEmpty() || resolver.IsEmpty()) return;
        v8::Context::Scope context_scope(local_context);
        if (completion.status != WEBSCENE_FILE_GRANT_CREATE_FILE_SUCCESS_V2) {
            const auto name = completion.status
                    == WEBSCENE_FILE_GRANT_CREATE_FILE_CANCELLED_V2
                ? "AbortError"
                : completion.status == WEBSCENE_FILE_GRANT_CREATE_FILE_DENIED_V2
                    ? "NotAllowedError"
                    : completion.status
                            == WEBSCENE_FILE_GRANT_CREATE_FILE_NOT_FOUND_V2
                        ? "NotFoundError"
                        : completion.status
                                == WEBSCENE_FILE_GRANT_CREATE_FILE_TYPE_MISMATCH_V2
                            ? "TypeMismatchError"
                            : completion.status
                                    == WEBSCENE_FILE_GRANT_CREATE_FILE_CHANGED_V2
                                ? "InvalidStateError"
                                : completion.status
                                        == WEBSCENE_FILE_GRANT_CREATE_FILE_LIMIT_V2
                                    ? "QuotaExceededError"
                                    : "NotReadableError";
            reject_file_panel_promise(isolate, local_context, resolver,
                "The native file creation operation failed", name);
            perform_microtask_checkpoint();
            return;
        }
        auto result = v8::Object::New(isolate);
        result->Set(local_context, js_string(isolate, "kind"),
            v8::Integer::NewFromUnsigned(isolate, completion.kind)).Check();
        result->Set(local_context, js_string(isolate, "capabilities"),
            v8::Integer::NewFromUnsigned(isolate, completion.capabilities))
            .Check();
        result->Set(local_context, js_string(isolate, "name"),
            js_string(isolate, completion.display_name.c_str())).Check();
        auto grant = v8::ArrayBuffer::New(isolate, completion.grant_id.size());
        if (!completion.grant_id.empty()) std::memcpy(
            grant->GetBackingStore()->Data(), completion.grant_id.data(),
            completion.grant_id.size());
        result->Set(local_context, js_string(isolate, "grant"),
            v8::Uint8Array::New(grant, 0, completion.grant_id.size())).Check();
        resolver->Resolve(local_context, result).Check();
        perform_microtask_checkpoint();
    }

    void cancel_file_grant_create_file_promises(const char* message)
    {
        for (auto& [_, target] : file_grant_create_file_promise_targets) {
            auto local_context = target.context.Get(isolate);
            auto resolver = target.resolver.Get(isolate);
            if (local_context.IsEmpty() || resolver.IsEmpty()) continue;
            v8::Context::Scope context_scope(local_context);
            reject_file_panel_promise(
                isolate, local_context, resolver, message, "AbortError");
        }
        file_grant_create_file_promise_targets.clear();
        perform_microtask_checkpoint();
    }

    void complete_native_file_grant_create_directory(
        file_grant_create_directory_completion_data_v2& completion)
    {
        const auto found = file_grant_create_directory_promise_targets.find(
            completion.request_id);
        if (found == file_grant_create_directory_promise_targets.end()) return;
        auto target = std::move(found->second);
        file_grant_create_directory_promise_targets.erase(found);
        auto local_context = target.context.Get(isolate);
        auto resolver = target.resolver.Get(isolate);
        if (local_context.IsEmpty() || resolver.IsEmpty()) return;
        v8::Context::Scope context_scope(local_context);
        if (completion.status != WEBSCENE_FILE_GRANT_CREATE_DIRECTORY_SUCCESS_V2) {
            const auto name = completion.status
                    == WEBSCENE_FILE_GRANT_CREATE_DIRECTORY_CANCELLED_V2
                ? "AbortError"
                : completion.status == WEBSCENE_FILE_GRANT_CREATE_DIRECTORY_DENIED_V2
                    ? "NotAllowedError"
                    : completion.status
                            == WEBSCENE_FILE_GRANT_CREATE_DIRECTORY_NOT_FOUND_V2
                        ? "NotFoundError"
                        : completion.status
                                == WEBSCENE_FILE_GRANT_CREATE_DIRECTORY_TYPE_MISMATCH_V2
                            ? "TypeMismatchError"
                            : completion.status
                                    == WEBSCENE_FILE_GRANT_CREATE_DIRECTORY_CHANGED_V2
                                ? "InvalidStateError"
                                : completion.status
                                        == WEBSCENE_FILE_GRANT_CREATE_DIRECTORY_LIMIT_V2
                                    ? "QuotaExceededError"
                                    : "NotReadableError";
            reject_file_panel_promise(isolate, local_context, resolver,
                "The native directory creation operation failed", name);
            perform_microtask_checkpoint();
            return;
        }
        auto result = v8::Object::New(isolate);
        result->Set(local_context, js_string(isolate, "kind"),
            v8::Integer::NewFromUnsigned(isolate, completion.kind)).Check();
        result->Set(local_context, js_string(isolate, "capabilities"),
            v8::Integer::NewFromUnsigned(isolate, completion.capabilities))
            .Check();
        result->Set(local_context, js_string(isolate, "name"),
            js_string(isolate, completion.display_name.c_str())).Check();
        auto grant = v8::ArrayBuffer::New(isolate, completion.grant_id.size());
        if (!completion.grant_id.empty()) std::memcpy(
            grant->GetBackingStore()->Data(), completion.grant_id.data(),
            completion.grant_id.size());
        result->Set(local_context, js_string(isolate, "grant"),
            v8::Uint8Array::New(grant, 0, completion.grant_id.size())).Check();
        resolver->Resolve(local_context, result).Check();
        perform_microtask_checkpoint();
    }
    void cancel_file_grant_create_directory_promises(const char* message)
    {
        for (auto& [_, target] : file_grant_create_directory_promise_targets) {
            auto local_context = target.context.Get(isolate);
            auto resolver = target.resolver.Get(isolate);
            if (local_context.IsEmpty() || resolver.IsEmpty()) continue;
            v8::Context::Scope context_scope(local_context);
            reject_file_panel_promise(
                isolate, local_context, resolver, message, "AbortError");
        }
        file_grant_create_directory_promise_targets.clear();
        perform_microtask_checkpoint();
    }

    void complete_native_file_grant_remove(
        file_grant_remove_completion_data_v2& completion)
    {
        const auto found = file_grant_remove_promise_targets.find(
            completion.request_id);
        if (found == file_grant_remove_promise_targets.end()) return;
        auto target = std::move(found->second);
        file_grant_remove_promise_targets.erase(found);
        auto local_context = target.context.Get(isolate);
        auto resolver = target.resolver.Get(isolate);
        if (local_context.IsEmpty() || resolver.IsEmpty()) return;
        v8::Context::Scope context_scope(local_context);
        if (completion.status != WEBSCENE_FILE_GRANT_REMOVE_SUCCESS_V2) {
            const auto name = completion.status
                    == WEBSCENE_FILE_GRANT_REMOVE_CANCELLED_V2
                ? "AbortError"
                : completion.status == WEBSCENE_FILE_GRANT_REMOVE_DENIED_V2
                    ? "NotAllowedError"
                    : completion.status == WEBSCENE_FILE_GRANT_REMOVE_NOT_FOUND_V2
                        ? "NotFoundError"
                        : completion.status
                                == WEBSCENE_FILE_GRANT_REMOVE_INVALID_MODIFICATION_V2
                            ? "InvalidModificationError"
                            : completion.status == WEBSCENE_FILE_GRANT_REMOVE_CHANGED_V2
                                ? "InvalidStateError"
                                : completion.status == WEBSCENE_FILE_GRANT_REMOVE_LIMIT_V2
                                    ? "QuotaExceededError"
                                    : "NotReadableError";
            reject_file_panel_promise(isolate, local_context, resolver,
                "The native entry removal operation failed", name);
            perform_microtask_checkpoint();
            return;
        }
        resolver->Resolve(local_context, v8::Undefined(isolate)).Check();
        perform_microtask_checkpoint();
    }
    void cancel_file_grant_remove_promises(const char* message)
    {
        for (auto& [_, target] : file_grant_remove_promise_targets) {
            auto local_context = target.context.Get(isolate);
            auto resolver = target.resolver.Get(isolate);
            if (local_context.IsEmpty() || resolver.IsEmpty()) continue;
            v8::Context::Scope context_scope(local_context);
            reject_file_panel_promise(
                isolate, local_context, resolver, message, "AbortError");
        }
        file_grant_remove_promise_targets.clear();
        perform_microtask_checkpoint();
    }

    void install_file_system_access_api(v8::Local<v8::Context> local_context)
    {
        auto global = local_context->Global();
        global->Set(local_context, js_string(isolate, "__webSceneRequestFilePanel"),
            v8::Function::New(local_context, request_file_panel).ToLocalChecked()).Check();
        global->Set(local_context,
            js_string(isolate, "__webSceneCompareFileGrants"),
            v8::Function::New(local_context, request_file_grant_same_entry)
                .ToLocalChecked()).Check();
        global->Set(local_context,
            js_string(isolate, "__webSceneResolveFileGrants"),
            v8::Function::New(local_context, request_file_grant_ancestry)
                .ToLocalChecked()).Check();
        global->Set(local_context,
            js_string(isolate, "__webSceneReadFileGrant"),
            v8::Function::New(local_context, request_file_grant_read)
                .ToLocalChecked()).Check();
        global->Set(local_context,
            js_string(isolate, "__webSceneWriteFileGrant"),
            v8::Function::New(local_context, request_file_grant_write)
                .ToLocalChecked()).Check();
        global->Set(local_context,
            js_string(isolate, "__webSceneDirectoryGrant"),
            v8::Function::New(local_context, request_file_grant_directory)
                .ToLocalChecked()).Check();
        global->Set(local_context,
            js_string(isolate, "__webSceneCreateFileGrant"),
            v8::Function::New(local_context, request_file_grant_create_file)
                .ToLocalChecked()).Check();
        global->Set(local_context,
            js_string(isolate, "__webSceneCreateDirectoryGrant"),
            v8::Function::New(local_context, request_file_grant_create_directory)
                .ToLocalChecked()).Check();
        global->Set(local_context,
            js_string(isolate, "__webSceneRemoveFileGrantEntry"),
            v8::Function::New(local_context, request_file_grant_remove)
                .ToLocalChecked()).Check();
        global->Set(local_context,
            js_string(isolate, "__webSceneRegisterFileSystemHandle"),
            v8::Function::New(local_context, register_file_system_handle)
                .ToLocalChecked()).Check();
        global->Set(local_context,
            js_string(isolate, "__webSceneRegisterFileSystemHandleRealm"),
            v8::Function::New(local_context, register_file_system_handle_realm)
                .ToLocalChecked()).Check();
        global->Set(local_context,
            js_string(isolate, "__webSceneFileSystemHandleState"),
            v8::Function::New(local_context, file_system_handle_state)
                .ToLocalChecked()).Check();
        constexpr std::array<std::string_view, 4> source_parts{R"JS(
          (() => {
            const requestPanel = globalThis.__webSceneRequestFilePanel;
            const compareGrants = globalThis.__webSceneCompareFileGrants;
            const resolveGrants = globalThis.__webSceneResolveFileGrants;
            const readFileGrant = globalThis.__webSceneReadFileGrant;
            const writeFileGrant = globalThis.__webSceneWriteFileGrant;
            const directoryGrant = globalThis.__webSceneDirectoryGrant;
            const createFileGrant = globalThis.__webSceneCreateFileGrant;
            const createDirectoryGrant =
              globalThis.__webSceneCreateDirectoryGrant;
            const removeGrantEntry = globalThis.__webSceneRemoveFileGrantEntry;
            const registerHandle = globalThis.__webSceneRegisterFileSystemHandle;
            const registerHandleRealm =
              globalThis.__webSceneRegisterFileSystemHandleRealm;
            const nativeHandleState = globalThis.__webSceneFileSystemHandleState;
            const createNativeFile = globalThis.__webSceneCreateNativeFile;
            delete globalThis.__webSceneRequestFilePanel;
            delete globalThis.__webSceneCompareFileGrants;
            delete globalThis.__webSceneResolveFileGrants;
            delete globalThis.__webSceneReadFileGrant;
            delete globalThis.__webSceneWriteFileGrant;
            delete globalThis.__webSceneDirectoryGrant;
            delete globalThis.__webSceneCreateFileGrant;
            delete globalThis.__webSceneCreateDirectoryGrant;
            delete globalThis.__webSceneRemoveFileGrantEntry;
            delete globalThis.__webSceneRegisterFileSystemHandle;
            delete globalThis.__webSceneRegisterFileSystemHandleRealm;
            delete globalThis.__webSceneFileSystemHandleState;
            delete globalThis.__webSceneCreateNativeFile;
            const states = new WeakMap();
            const textEncoder = new TextEncoder();
            const permissionMode = options => {
              if (options == null) return 'read';
              const value = options.mode;
              if (value === undefined) return 'read';
              const mode = String(value);
              if (mode !== 'read' && mode !== 'readwrite') {
                throw new TypeError(`Unsupported permission mode ${mode}`);
              }
              return mode;
            };
            const permission = (state, mode) => {
              const readCapability = state.kind === 'file' ? 1 : 4;
              const read = (state.capabilities & readCapability) !== 0;
              const write = (state.capabilities & 2) !== 0;
              return mode === 'read' ? read : read && write;
            };
            const stateFor = value => {
              let state = states.get(value);
              if (!state) {
                const raw = nativeHandleState(value);
                if (raw) {
                  const token = raw.grant.slice();
                  state = Object.freeze({
                    kind: raw.kind === 1 ? 'file' : 'directory',
                    name: String(raw.name),
                    capabilities: Number(raw.capabilities), token
                  });
                  states.set(value, state);
                }
              }
              if (!state) throw new TypeError('Illegal invocation');
              return state;
            };
            function FileSystemHandle() { throw new TypeError('Illegal constructor'); }
            function FileSystemFileHandle() { throw new TypeError('Illegal constructor'); }
            function FileSystemDirectoryHandle() { throw new TypeError('Illegal constructor'); }
            function FileSystemWritableFileStream() {
              throw new TypeError('Illegal constructor');
            }
            Object.setPrototypeOf(FileSystemFileHandle.prototype, FileSystemHandle.prototype);
            Object.setPrototypeOf(FileSystemDirectoryHandle.prototype, FileSystemHandle.prototype);
            Object.setPrototypeOf(
              FileSystemWritableFileStream.prototype, WritableStream.prototype);
            Object.setPrototypeOf(FileSystemWritableFileStream, WritableStream);
            function isSameEntry(other) {
              try {
                return compareGrants(stateFor(this).token, stateFor(other).token);
              } catch (error) {
                return Promise.reject(error);
              }
            }
            function queryPermission(options = {}) {
              return Promise.resolve().then(() => {
                const state = stateFor(this);
                return permission(state, permissionMode(options))
                  ? 'granted' : 'denied';
              });
            }
            function requestPermission(options = {}) {
              return Promise.resolve().then(() => {
                const state = stateFor(this);
                return permission(state, permissionMode(options))
                  ? 'granted' : 'denied';
              });
            }
            Object.defineProperties(FileSystemHandle.prototype, {
              kind: { enumerable: true, configurable: true, get() { return stateFor(this).kind; } },
              name: { enumerable: true, configurable: true, get() { return stateFor(this).name; } },
              isSameEntry: { enumerable: true, configurable: true, writable: true,
                value: isSameEntry },
              queryPermission: { enumerable: true, configurable: true, writable: true,
                value: queryPermission },
              requestPermission: { enumerable: true, configurable: true, writable: true,
                value: requestPermission },
              [Symbol.toStringTag]: { configurable: true, value: 'FileSystemHandle' }
            });
            const unavailable = operation => Promise.reject(new DOMException(
              `${operation} requires the native grant operation stack`, 'NotSupportedError'));
            function getFile() {
              let state;
              try {
                state = stateFor(this);
                if (state.kind !== 'file') throw new TypeError('Illegal invocation');
                if (!permission(state, 'read')) {
                  throw new DOMException(
                    'The file handle does not grant read access', 'NotAllowedError');
                }
                if (typeof createNativeFile !== 'function') {
                  throw new DOMException('Native File factory is unavailable', 'NotSupportedError');
                }
              } catch (error) { return Promise.reject(error); }
              return readFileGrant(state.token, 0, 1).then(initial => {
                const expectedSize = initial.size;
                const expectedModified = initial.lastModified;
                const expectedModifiedNs = initial.modificationTimeNs;
                const read = async (offset, maximumBytes) => {
                  const result = await readFileGrant(
                    state.token, offset, maximumBytes);
                  if (result.size !== expectedSize
                      || result.modificationTimeNs !== expectedModifiedNs) {
                    throw new DOMException(
                      'The file changed after this File was acquired', 'NotReadableError');
                  }
                  return result;
                };
                return createNativeFile(
                  state.name, expectedModified, expectedSize, read);
              });
            }
            const writerStates = new WeakMap();
            const maximumWriteBytes = 1024 * 1024 * 1024;
            const checkPosition = (value, name) => {
              value = Number(value);
              if (!Number.isSafeInteger(value) || value < 0) {
                throw new TypeError(`${name} must be a non-negative safe integer`);
              }
              if (value > maximumWriteBytes) {
                throw new DOMException(
                  `${name} exceeds the native transaction limit`, 'QuotaExceededError');
              }
              return value;
            };
            const fileWriteBytes = async value => {
              if (value === null || value === undefined) {
                throw new TypeError('File write data is required');
              }
              if (typeof value === 'string') return textEncoder.encode(value);
              if (value instanceof ArrayBuffer) return new Uint8Array(value.slice(0));
              if (ArrayBuffer.isView(value)) return new Uint8Array(
                value.buffer.slice(value.byteOffset, value.byteOffset + value.byteLength));
              if (value instanceof Blob) return new Uint8Array(await value.arrayBuffer());
              throw new TypeError('Unsupported file write data');
            };
            const abortWriterState = async (state, suppressFailure = false) => {
              const transaction = state.transaction;
              state.transaction = undefined;
              if (transaction) {
                try { await writeFileGrant(4, null, transaction, 0, null, 0); }
                catch (error) { if (!suppressFailure) throw error; }
              }
            };
            const performWriterCommand = async (state, command) => {
              try {
                let type = 'write';
                let data = command;
                let position;
                if (command !== null && typeof command === 'object'
                    && !(command instanceof Blob)
                    && !(command instanceof ArrayBuffer)
                    && !ArrayBuffer.isView(command)) {
                  type = String(command.type || '');
                  if (type === 'write') {
                    if (!Object.prototype.hasOwnProperty.call(command, 'data')) {
                      throw new DOMException('write requires data', 'SyntaxError');
                    }
                    data = command.data;
                    if (command.position !== undefined) {
                      position = checkPosition(command.position, 'position');
                    }
                  } else if (type === 'seek') {
                    if (command.position === undefined) {
                      throw new DOMException('seek requires position', 'SyntaxError');
                    }
                    state.cursor = checkPosition(command.position, 'position');
                    return;
                  } else if (type === 'truncate') {
                    if (command.size === undefined) {
                      throw new DOMException('truncate requires size', 'SyntaxError');
                    }
                    state.size = checkPosition(command.size, 'size');
                    return;
                  } else {
                    throw new DOMException('Unknown file write command', 'SyntaxError');
                  }
                }
                const bytes = await fileWriteBytes(data);
                let offset = position === undefined ? state.cursor : position;
                if (offset > maximumWriteBytes - bytes.byteLength) {
                  throw new DOMException(
                    'File write exceeds the native transaction limit',
                    'QuotaExceededError');
                }
                for (let start = 0; start < bytes.byteLength; start += 1024 * 1024) {
                  const chunk = bytes.subarray(start,
                    Math.min(bytes.byteLength, start + 1024 * 1024));
                  await writeFileGrant(2, null, state.transaction,
                    offset + start, chunk, 0);
                }
                state.cursor = offset + bytes.byteLength;
                state.size = Math.max(state.size, state.cursor);
              } catch (error) {
                await abortWriterState(state, true);
                throw error;
              }
            };
            const queueWriterCommand = (stream, command) => {
              if (!writerStates.has(stream)) {
)JS",
        R"JS(                return Promise.reject(new TypeError('Illegal invocation'));
              }
              let writer;
              try { writer = WritableStream.prototype.getWriter.call(stream); }
              catch (error) { return Promise.reject(error); }
              const result = writer.write(command);
              writer.releaseLock();
              return result;
            };
            function write(data) { return queueWriterCommand(this, data); }
            function seek(position) {
              return queueWriterCommand(this, {type:'seek', position});
            }
            function truncate(size) {
              return queueWriterCommand(this, {type:'truncate', size});
            }
            Object.defineProperties(FileSystemWritableFileStream.prototype, {
              write: {enumerable:true, configurable:true, writable:true, value:write},
              seek: {enumerable:true, configurable:true, writable:true, value:seek},
              truncate: {enumerable:true, configurable:true, writable:true, value:truncate},
              [Symbol.toStringTag]: {
                configurable:true, value:'FileSystemWritableFileStream'}
            });
            function createWritable(options = {}) {
              let state;
              try {
                state = stateFor(this);
                if (state.kind !== 'file') throw new TypeError('Illegal invocation');
                if ((state.capabilities & 2) === 0) {
                  throw new DOMException(
                    'The file handle does not grant write access', 'NotAllowedError');
                }
                if (options == null) options = {};
                if (typeof options !== 'object') {
                  throw new TypeError('Writable options must be an object');
                }
                const mode = String(options.mode || 'siloed');
                if (mode !== 'siloed') {
                  throw new DOMException(
                    'Exclusive writable locks require native lock authority',
                    'NotSupportedError');
                }
              } catch (error) { return Promise.reject(error); }
              const keepExistingData = Boolean(options.keepExistingData);
              return writeFileGrant(1, state.token, null, 0, null, 0)
                .then(async begun => {
                  const writerState = {
                    transaction:begun.transaction, cursor:0, size:0
                  };
                  try {
                    if (keepExistingData) {
                      let offset = 0;
                      let expectedSize;
                      let expectedModified;
                      do {
                        const read = await readFileGrant(
                          state.token, offset, 1024 * 1024);
                        if (expectedSize === undefined) {
                          expectedSize = read.size;
                          expectedModified = read.modificationTimeNs;
                        } else if (read.size !== expectedSize
                            || read.modificationTimeNs !== expectedModified) {
                          throw new DOMException(
                            'The file changed while opening the writer',
                            'NotReadableError');
                        }
                        if (read.bytes.byteLength !== 0) {
                          await writeFileGrant(2, null,
                            writerState.transaction, offset, read.bytes, 0);
                          offset += read.bytes.byteLength;
                        }
                        if (read.eof) break;
                      } while (true);
                      writerState.size = expectedSize;
                    }
                    const stream = new WritableStream({
                      write(command) {
                        return performWriterCommand(writerState, command);
                      },
                      async close() {
                        const transaction = writerState.transaction;
                        if (!transaction) throw new TypeError('Writer is closed');
                        writerState.transaction = undefined;
                        await writeFileGrant(
                          3, null, transaction, 0, null, writerState.size);
                      },
                      abort() { return abortWriterState(writerState); }
                    });
                    Object.setPrototypeOf(stream,
                      FileSystemWritableFileStream.prototype);
                    writerStates.set(stream, writerState);
                    return stream;
                  } catch (error) {
                    await abortWriterState(writerState, true);
                    throw error;
                  }
                });
            }
            const validateEntryName = value => {
              const name = String(value);
              if (!name || name === '.' || name === '..'
                  || name.includes('/') || name.includes('\\')
                  || name.includes('\0')) {
                throw new TypeError('Entry name must be one valid path component');
              }
              return name;
            };
            const directoryState = handle => {
              const state = stateFor(handle);
              if (state.kind !== 'directory') throw new TypeError('Illegal invocation');
              if ((state.capabilities & 4) === 0) {
                throw new DOMException(
                  'The directory handle does not grant enumerate access',
                  'NotAllowedError');
              }
              return state;
            };
            const releaseDirectoryCursor = async cursor => {
              if (cursor && cursor.byteLength) {
                await directoryGrant(2, null, cursor, 0);
              }
            };
            async function* iterateDirectory(handle, mode) {
              const state = directoryState(handle);
              let cursor = new Uint8Array();
              let total = 0;
              try {
                do {
                  const page = await directoryGrant(
                    1, state.token, cursor.byteLength ? cursor : null, 32);
                  cursor = page.cursor;
                  if (page.entries.length === 0 && cursor.byteLength !== 0) {
                    throw new DOMException(
                      'Native directory page made no progress', 'DataError');
                  }
                  total += page.entries.length;
                  if (total > 10000) {
                    throw new DOMException(
                      'Native directory snapshot exceeds its entry limit',
                      'QuotaExceededError');
                  }
                  for (const raw of page.entries) {
                    const child = createHandle(raw);
                    if (mode === 'keys') yield child.name;
                    else if (mode === 'values') yield child;
                    else yield [child.name, child];
                  }
                } while (cursor.byteLength !== 0);
              } finally {
                if (cursor.byteLength !== 0) {
                  await releaseDirectoryCursor(cursor);
                }
              }
            }
            function entries() {
              directoryState(this);
              return iterateDirectory(this, 'entries');
            }
            function keys() {
              directoryState(this);
              return iterateDirectory(this, 'keys');
            }
            function values() {
              directoryState(this);
              return iterateDirectory(this, 'values');
            }
            const getExistingHandle = async (handle, name, options, kind) => {
              const state = directoryState(handle);
              name = validateEntryName(name);
              if (options == null) options = {};
              if (typeof options !== 'object') {
                throw new TypeError('Handle options must be an object');
              }
              const create = Boolean(options.create);
              if (create) {
                const created = kind === 'file'
                  ? await createFileGrant(state.token, name)
                  : await createDirectoryGrant(state.token, name);
                return createHandle(created);
              }
              let cursor = new Uint8Array();
              let total = 0;
              try {
                do {
                  const page = await directoryGrant(
                    1, state.token, cursor.byteLength ? cursor : null, 32);
                  cursor = page.cursor;
                  total += page.entries.length;
                  if (total > 10000) {
                    throw new DOMException(
                      'Native directory snapshot exceeds its entry limit',
                      'QuotaExceededError');
                  }
                  for (const raw of page.entries) {
                    if (raw.name !== name) continue;
                    const child = createHandle(raw);
                    if (child.kind !== kind) {
                      throw new DOMException(
                        `The entry is not a ${kind}`, 'TypeMismatchError');
                    }
                    return child;
                  }
                  if (page.entries.length === 0 && cursor.byteLength !== 0) {
                    throw new DOMException(
                      'Native directory page made no progress', 'DataError');
                  }
                } while (cursor.byteLength !== 0);
                throw new DOMException('The entry was not found', 'NotFoundError');
              } finally {
                if (cursor.byteLength !== 0) {
                  await releaseDirectoryCursor(cursor);
                }
              }
            };
            function getFileHandle(name, options = {}) {
              try { return getExistingHandle(this, name, options, 'file'); }
              catch (error) { return Promise.reject(error); }
            }
            function getDirectoryHandle(name, options = {}) {
              try { return getExistingHandle(this, name, options, 'directory'); }
              catch (error) { return Promise.reject(error); }
            }
            function removeEntry(name, options = {}) {
              try {
                const state = directoryState(this);
                name = validateEntryName(name);
                if (options == null) options = {};
                if (typeof options !== 'object') {
                  throw new TypeError('Handle options must be an object');
                }
                return removeGrantEntry(
                  state.token, name, Boolean(options.recursive));
              } catch (error) { return Promise.reject(error); }
            }
            function resolve(possibleDescendant) {
              try {
                return resolveGrants(
                  directoryState(this).token,
                  stateFor(possibleDescendant).token);
              } catch (error) { return Promise.reject(error); }
            }
            Object.defineProperties(FileSystemFileHandle.prototype, {
              getFile: { enumerable: true, configurable: true, writable: true,
                value: getFile },
              createWritable: { enumerable: true, configurable: true, writable: true,
                value: createWritable },
              [Symbol.toStringTag]: { configurable: true, value: 'FileSystemFileHandle' }
            });
            Object.defineProperties(FileSystemDirectoryHandle.prototype, {
              entries: { enumerable: true, configurable: true, writable: true,
                value: entries },
              keys: { enumerable: true, configurable: true, writable: true,
                value: keys },
              values: { enumerable: true, configurable: true, writable: true,
                value: values },
)JS",
        R"JS(              [Symbol.asyncIterator]: { configurable: true, writable: true,
                value: entries },
              getFileHandle: { enumerable: true, configurable: true, writable: true,
                value: getFileHandle },
              getDirectoryHandle: { enumerable: true, configurable: true, writable: true,
                value: getDirectoryHandle },
              removeEntry: { enumerable: true, configurable: true, writable: true,
                value: removeEntry },
              resolve: { enumerable: true, configurable: true, writable: true,
                value: resolve },
              [Symbol.toStringTag]: { configurable: true, value: 'FileSystemDirectoryHandle' }
            });
            const createHandle = raw => {
              const kind = raw.kind === 1 ? 'file' : 'directory';
              const handle = Object.create(kind === 'file'
                ? FileSystemFileHandle.prototype : FileSystemDirectoryHandle.prototype);
              const token = raw.grant.slice();
              states.set(handle, Object.freeze({
                kind, name: String(raw.name),
                capabilities: Number(raw.capabilities), token
              }));
              registerHandle(handle, Number(raw.kind), String(raw.name),
                Number(raw.capabilities), token);
              return handle;
            };
            const observerStates = new WeakMap();
            const observerPollMilliseconds = 250;
            const observerMaximumEntries = 10000;
            const observerMaximumDepth = 64;
            const observerPathKey = components => JSON.stringify(components);
            const observerEntry = (handle, components, signature) => ({
              handle, components, signature
            });
            const observerFileSignature = async handle => {
              const file = await getFile.call(handle);
              return `file:${file.size}:${file.lastModified}`;
            };
            const observerSnapshot = async (root, recursive) => {
              const rootState = stateFor(root);
              const snapshot = new Map();
              if (rootState.kind === 'file') {
                snapshot.set(observerPathKey([]), observerEntry(
                  root, [], await observerFileSignature(root)));
                return snapshot;
              }
              directoryState(root);
              let count = 0;
              const visit = async (directory, prefix, depth) => {
                if (depth > observerMaximumDepth) {
                  throw new DOMException(
                    'Observed directory exceeds the recursion limit',
                    'QuotaExceededError');
                }
                for await (const child of values.call(directory)) {
                  const components = prefix.concat(child.name);
                  if (++count > observerMaximumEntries) {
                    throw new DOMException(
                      'Observed directory exceeds the entry limit',
                      'QuotaExceededError');
                  }
                  const signature = child.kind === 'file'
                    ? await observerFileSignature(child) : 'directory';
                  snapshot.set(observerPathKey(components), observerEntry(
                    child, components, signature));
                  if (recursive && child.kind === 'directory') {
                    await visit(child, components, depth + 1);
                  }
                }
              };
              await visit(root, [], 0);
              return snapshot;
            };
            const observerRecord = (type, root, entry, movedFrom = null) =>
              Object.freeze({
                type,
                root,
                changedHandle: entry ? entry.handle : null,
                relativePathComponents: Object.freeze(
                  entry ? entry.components.slice() : []),
                relativePathMovedFrom: movedFrom
              });
            const stopObservation = observation => {
              observation.active = false;
              if (observation.timer !== undefined) {
                clearTimeout(observation.timer);
                observation.timer = undefined;
              }
            };
            const scheduleObservation = (observer, observation) => {
              if (!observation.active) return;
              observation.timer = setTimeout(
                () => pollObservation(observer, observation),
                observerPollMilliseconds);
            };
            const pollObservation = async (observer, observation) => {
              if (!observation.active || observation.polling) return;
              observation.polling = true;
              try {
                const next = await observerSnapshot(
                  observation.root, observation.recursive);
                if (!observation.active) return;
                const records = [];
                for (const [key, entry] of observation.snapshot) {
                  const current = next.get(key);
                  if (!current) {
                    records.push(observerRecord(
                      'disappeared', observation.root, entry));
                  } else if (entry.signature !== current.signature) {
                    records.push(observerRecord(
                      'modified', observation.root, current));
                  }
                }
                for (const [key, entry] of next) {
                  if (!observation.snapshot.has(key)) {
                    records.push(observerRecord(
                      'appeared', observation.root, entry));
                  }
                }
                observation.snapshot = next;
                if (records.length !== 0) {
                  observerStates.get(observer).callback.call(
                    undefined, Object.freeze(records), observer);
                }
              } catch (error) {
                if (observation.active) {
                  stopObservation(observation);
                  observerStates.get(observer).observations.delete(
                    observation.root);
                  const type = error && error.name === 'NotFoundError'
                    ? 'disappeared' : 'errored';
                  observerStates.get(observer).callback.call(
                    undefined,
                    Object.freeze([observerRecord(
                      type, observation.root, null)]), observer);
                }
              } finally {
                observation.polling = false;
                scheduleObservation(observer, observation);
              }
            };
            function FileSystemObserver(callback) {
              if (!new.target) {
                throw new TypeError(
                  "Failed to construct 'FileSystemObserver': use new");
              }
              if (typeof callback !== 'function') {
                throw new TypeError('FileSystemObserver callback must be callable');
              }
              observerStates.set(this, {callback, observations:new Map()});
            }
            function observe(handle, options = {}) {
              const observer = observerStates.get(this);
              if (!observer) return Promise.reject(new TypeError('Illegal invocation'));
              let handleState;
              try {
                handleState = stateFor(handle);
                if (options == null) options = {};
                if (typeof options !== 'object') {
                  throw new TypeError('Observer options must be an object');
                }
              } catch (error) { return Promise.reject(error); }
              const recursive = handleState.kind === 'directory'
                && Boolean(options.recursive);
              const previous = observer.observations.get(handle);
              if (previous) stopObservation(previous);
              return observerSnapshot(handle, recursive).then(snapshot => {
                const observation = {
                  root:handle, recursive, snapshot,
                  active:true, polling:false, timer:undefined
                };
                observer.observations.set(handle, observation);
                scheduleObservation(this, observation);
              });
            }
            function unobserve(handle) {
              const observer = observerStates.get(this);
              if (!observer) return Promise.reject(new TypeError('Illegal invocation'));
              try { stateFor(handle); }
              catch (error) { return Promise.reject(error); }
              const observation = observer.observations.get(handle);
              if (observation) {
                stopObservation(observation);
                observer.observations.delete(handle);
              }
              return Promise.resolve();
            }
            function disconnect() {
              const observer = observerStates.get(this);
              if (!observer) throw new TypeError('Illegal invocation');
              for (const observation of observer.observations.values()) {
                stopObservation(observation);
              }
              observer.observations.clear();
            }
            Object.defineProperties(FileSystemObserver.prototype, {
              observe: {enumerable:true, configurable:true, writable:true,
                value:observe},
              unobserve: {enumerable:true, configurable:true, writable:true,
                value:unobserve},
              disconnect: {enumerable:true, configurable:true, writable:true,
                value:disconnect},
              [Symbol.toStringTag]: {
                configurable:true, value:'FileSystemObserver'}
            });
            registerHandleRealm(
              FileSystemFileHandle.prototype,
              FileSystemDirectoryHandle.prototype);
            const wellKnown = new Set([
              'desktop', 'documents', 'downloads', 'music', 'pictures', 'videos'
            ]);
            const normalizeStartIn = value => {
              if (value === undefined) return new Uint8Array();
              if (states.has(value)) return stateFor(value).token.slice();
              value = String(value);
              if (!wellKnown.has(value)) throw new TypeError('Invalid startIn value');
              return textEncoder.encode(`well-known:${value}`);
            };
            const mimePattern = /^[!#$%&'*+.^_`|~0-9A-Za-z-]+\/(?:\*|[!#$%&'*+.^_`|~0-9A-Za-z-]+)$/;
            const normalizeTypes = value => {
              if (value === undefined) return [];
              if (!Array.isArray(value) || value.length > 32) {
                throw new TypeError('Picker types must be a bounded array');
              }
              let count = 0;
              return value.map(type => {
                if (!type || typeof type !== 'object' || !type.accept
                    || typeof type.accept !== 'object') {
                  throw new TypeError('Invalid picker type');
                }
                const accept = Object.entries(type.accept).map(([mime, extensions]) => {
                  if (!mimePattern.test(mime) || !Array.isArray(extensions)
                      || extensions.length === 0) throw new TypeError('Invalid picker accept entry');
                  count += 1 + extensions.length;
                  if (count > 64) throw new TypeError('Too many picker accept values');
                  return { mime, extensions: extensions.map(extension => {
                    extension = String(extension);
                    if (!/^\.[^./\\]{1,63}$/.test(extension)) {
                      throw new TypeError('Invalid picker extension');
                    }
                    return extension;
                  }) };
                });
                if (accept.length === 0) throw new TypeError('Empty picker accept map');
                return { description: String(type.description || ''), accept };
              });
            };
)JS",
        R"JS(            const normalize = (kind, options) => {
              if (options == null) options = {};
              if (typeof options !== 'object') throw new TypeError('Picker options must be an object');
              const types = kind === 2 ? [] : normalizeTypes(options.types);
              const excludeAcceptAllOption = Boolean(options.excludeAcceptAllOption);
              if (excludeAcceptAllOption && types.length === 0) {
                throw new TypeError('excludeAcceptAllOption requires at least one type');
              }
              const mode = String(options.mode || 'read');
              if (kind === 2 && mode !== 'read' && mode !== 'readwrite') {
                throw new TypeError('Directory picker mode must be read or readwrite');
              }
              return {
                multiple: kind === 1 && Boolean(options.multiple),
                excludeAcceptAllOption,
                requestWrite: kind === 2 && mode === 'readwrite',
                suggestedName: kind === 3 ? String(options.suggestedName || '') : '',
                startIn: normalizeStartIn(options.startIn),
                types
              };
            };
            const pick = (kind, options) => {
              try {
                const promise = requestPanel(kind, normalize(kind, options));
                return Promise.prototype.then.call(promise, entries => kind === 1
                  ? Array.from(entries, createHandle) : createHandle(entries[0]));
              } catch (error) {
                return Promise.reject(error);
              }
            };
            function showOpenFilePicker(options = {}) { return pick(1, options); }
            function showDirectoryPicker(options = {}) { return pick(2, options); }
            function showSaveFilePicker(options = {}) { return pick(3, options); }
            Object.defineProperties(globalThis, {
              FileSystemHandle: { configurable: true, writable: true, value: FileSystemHandle },
              FileSystemFileHandle: { configurable: true, writable: true, value: FileSystemFileHandle },
              FileSystemDirectoryHandle: { configurable: true, writable: true, value: FileSystemDirectoryHandle },
              FileSystemWritableFileStream: {
                configurable: true, writable: true,
                value: FileSystemWritableFileStream
              },
              FileSystemObserver: {
                configurable: true, writable: true, value: FileSystemObserver
              },
              showOpenFilePicker: { configurable: true, enumerable: true,
                writable: true, value: showOpenFilePicker },
              showDirectoryPicker: { configurable: true, enumerable: true,
                writable: true, value: showDirectoryPicker },
              showSaveFilePicker: { configurable: true, enumerable: true,
                writable: true, value: showSaveFilePicker }
            });
          })();
        )JS"};
        static_assert(embedded_source_parts_are_portable(source_parts));
        auto source = join_embedded_source_parts(source_parts);
        auto script = v8::Script::Compile(
            local_context,
            js_string(isolate, source.c_str())).ToLocalChecked();
        script->Run(local_context).ToLocalChecked();
    }
    static constexpr std::size_t maximum_file_system_handle_bindings = 16384U;

    static v8::Local<v8::Private> file_system_handle_private(v8::Isolate* isolate)
    {
        return v8::Private::ForApi(
            isolate, js_string(isolate, "WebScene.FileSystemHandle"));
    }

    static v8::Local<v8::Private> file_system_file_prototype_private(
        v8::Isolate* isolate)
    {
        return v8::Private::ForApi(
            isolate, js_string(isolate, "WebScene.FileSystemFileHandle.Prototype"));
    }

    static v8::Local<v8::Private> file_system_directory_prototype_private(
        v8::Isolate* isolate)
    {
        return v8::Private::ForApi(isolate,
            js_string(isolate, "WebScene.FileSystemDirectoryHandle.Prototype"));
    }

    static v8::Local<v8::Private> file_system_origin_private(v8::Isolate* isolate)
    {
        return v8::Private::ForApi(
            isolate, js_string(isolate, "WebScene.FileSystemHandle.Origin"));
    }

    std::string security_origin_for_realm(v8::Local<v8::Context> realm)
    {
        v8::Context::Scope scope(realm);
        v8::Local<v8::Value> location;
        v8::Local<v8::Value> origin;
        if (realm->Global()->Get(
                realm, js_string(isolate, "location")).ToLocal(&location)
            && location->IsObject()
            && location.As<v8::Object>()->Get(
                realm, js_string(isolate, "origin")).ToLocal(&origin)
            && !origin->IsNullOrUndefined()) {
            return to_utf8(isolate, origin);
        }
        return std::string("null");
    }

    std::string registered_file_system_origin(v8::Local<v8::Context> realm)
    {
        v8::Local<v8::Value> value;
        if (realm->Global()->GetPrivate(
                realm, file_system_origin_private(isolate)).ToLocal(&value)
            && value->IsString()) return to_utf8(isolate, value);
        return std::string("null");
    }

    file_system_handle_binding* file_system_handle_binding_from_object(
        v8::Local<v8::Context> realm,
        v8::Local<v8::Object> object)
    {
        v8::Local<v8::Value> value;
        if (!object->GetPrivate(
                realm, file_system_handle_private(isolate)).ToLocal(&value)
            || !value->IsExternal()) return nullptr;
        auto* binding = static_cast<file_system_handle_binding*>(
            value.As<v8::External>()->Value(v8::kExternalPointerTypeTagDefault));
        return binding != nullptr && !binding->reclaimed ? binding : nullptr;
    }

    static void file_system_handle_wrapper_collected(
        const v8::WeakCallbackInfo<file_system_handle_binding>& info)
    {
        auto* binding = info.GetParameter();
        if (binding == nullptr) return;
        binding->context.Reset();
        binding->wrapper.Reset();
        binding->owner.reset();
        binding->reclaimed = true;
    }

    static bool copy_file_grant_token(
        v8::Local<v8::Value> value,
        std::vector<std::uint8_t>& destination)
    {
        if (!value->IsUint8Array()) return false;
        auto bytes = value.As<v8::Uint8Array>();
        if (bytes->ByteLength() == 0
            || bytes->ByteLength() > file_panel_maximum_token_bytes_v2)
            return false;
        const auto store = bytes->Buffer()->GetBackingStore();
        const auto* first = static_cast<const std::uint8_t*>(store->Data())
            + bytes->ByteOffset();
        destination.assign(first, first + bytes->ByteLength());
        return true;
    }

    static void register_file_system_handle_realm(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        if (self == nullptr || info.Length() != 2
            || !info[0]->IsObject() || !info[1]->IsObject()) {
            info.GetIsolate()->ThrowException(v8::Exception::TypeError(
                js_string(info.GetIsolate(),
                    "File system handle prototypes required")));
            return;
        }
        auto realm = info.GetIsolate()->GetCurrentContext();
        realm->Global()->SetPrivate(
            realm,
            file_system_file_prototype_private(info.GetIsolate()),
            info[0]).Check();
        realm->Global()->SetPrivate(
            realm,
            file_system_directory_prototype_private(info.GetIsolate()),
            info[1]).Check();
        realm->Global()->SetPrivate(
            realm,
            file_system_origin_private(info.GetIsolate()),
            js_string(info.GetIsolate(),
                self->security_origin_for_realm(realm).c_str())).Check();
    }

    bool bind_file_system_handle(
        v8::Local<v8::Context> realm,
        v8::Local<v8::Object> object,
        std::shared_ptr<file_grant_owner> owner)
    {
        const auto reusable = std::find_if(
            file_system_handle_bindings.begin(),
            file_system_handle_bindings.end(),
            [](const auto& binding) {
                return binding != nullptr && binding->reclaimed;
            });
        if (reusable == file_system_handle_bindings.end()
            && file_system_handle_bindings.size()
                >= maximum_file_system_handle_bindings) return false;
        file_system_handle_binding* raw = nullptr;
        if (reusable != file_system_handle_bindings.end()) {
            raw = reusable->get();
        } else {
            auto binding = std::make_unique<file_system_handle_binding>();
            raw = binding.get();
            file_system_handle_bindings.push_back(std::move(binding));
        }
        raw->owner = std::move(owner);
        raw->reclaimed = false;
        raw->context.Reset(isolate, realm);
        raw->wrapper.Reset(isolate, object);
        raw->wrapper.SetWeak(
            raw,
            file_system_handle_wrapper_collected,
            v8::WeakCallbackType::kParameter);
        object->SetPrivate(
            realm,
            file_system_handle_private(isolate),
            v8::External::New(
                isolate, raw, v8::kExternalPointerTypeTagDefault)).Check();
        return true;
    }

    static void file_system_handle_state(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto realm = info.GetIsolate()->GetCurrentContext();
        auto* binding = self != nullptr && info.Length() == 1
                && info[0]->IsObject()
            ? self->file_system_handle_binding_from_object(
                realm, info[0].As<v8::Object>())
            : nullptr;
        if (binding == nullptr || binding->owner == nullptr
            || binding->owner->released.load(std::memory_order_acquire)) return;
        const auto& owner = *binding->owner;
        auto buffer = v8::ArrayBuffer::New(info.GetIsolate(), owner.token.size());
        std::memcpy(buffer->GetBackingStore()->Data(),
            owner.token.data(), owner.token.size());
        auto result = v8::Object::New(info.GetIsolate());
        result->Set(realm, js_string(info.GetIsolate(), "kind"),
            v8::Integer::NewFromUnsigned(info.GetIsolate(), owner.kind)).Check();
        result->Set(realm, js_string(info.GetIsolate(), "name"),
            js_string(info.GetIsolate(), owner.name.c_str())).Check();
        result->Set(realm, js_string(info.GetIsolate(), "capabilities"),
            v8::Integer::NewFromUnsigned(
                info.GetIsolate(), owner.capabilities)).Check();
        result->Set(realm, js_string(info.GetIsolate(), "grant"),
            v8::Uint8Array::New(buffer, 0, owner.token.size())).Check();
        info.GetReturnValue().Set(result);
    }

    static void register_file_system_handle(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto realm = info.GetIsolate()->GetCurrentContext();
        if (self == nullptr || info.Length() != 5 || !info[0]->IsObject()
            || !info[1]->IsUint32() || !info[3]->IsUint32()) {
            info.GetIsolate()->ThrowException(v8::Exception::TypeError(
                js_string(info.GetIsolate(), "Malformed file system handle")));
            return;
        }
        const auto kind = info[1].As<v8::Uint32>()->Value();
        const auto capabilities = info[3].As<v8::Uint32>()->Value();
        const auto name = to_utf8(info.GetIsolate(), info[2]);
        std::vector<std::uint8_t> token;
        if ((kind != WEBSCENE_FILE_PANEL_ENTRY_FILE_V2
                && kind != WEBSCENE_FILE_PANEL_ENTRY_DIRECTORY_V2)
            || name.empty() || !file_panel_valid_utf8_v2(name)
            || !copy_file_grant_token(info[4], token)) {
            info.GetIsolate()->ThrowException(v8::Exception::TypeError(
                js_string(info.GetIsolate(), "Invalid file system handle metadata")));
            return;
        }
        auto object = info[0].As<v8::Object>();
        if (self->file_system_handle_binding_from_object(realm, object) != nullptr) {
            info.GetIsolate()->ThrowException(v8::Exception::TypeError(
                js_string(info.GetIsolate(), "File system handle already registered")));
            return;
        }
        const std::string key(
            reinterpret_cast<const char*>(token.data()), token.size());
        std::shared_ptr<file_grant_owner> owner;
        if (const auto known = self->file_grant_owners.find(key);
            known != self->file_grant_owners.end()) owner = known->second.lock();
        const auto origin = self->registered_file_system_origin(realm);
        if (owner != nullptr) {
            if (owner->released.load(std::memory_order_acquire)
                || owner->kind != kind || owner->capabilities != capabilities
                || owner->name != name || owner->origin != origin) {
                info.GetIsolate()->ThrowException(v8::Exception::Error(
                    js_string(info.GetIsolate(), "Native grant metadata changed")));
                return;
            }
        } else {
            owner = std::make_shared<file_grant_owner>();
            owner->token = std::move(token);
            owner->origin = origin;
            owner->name = name;
            owner->kind = kind;
            owner->capabilities = capabilities;
            owner->dispatcher = self->file_grant_releases;
            self->file_grant_owners[key] = owner;
        }
        if (!self->bind_file_system_handle(realm, object, std::move(owner))) {
            info.GetIsolate()->ThrowException(v8::Exception::RangeError(
                js_string(info.GetIsolate(),
                    "File system handle binding capacity exhausted")));
        }
    }

    v8::MaybeLocal<v8::Object> create_file_system_handle_wrapper(
        v8::Local<v8::Context> realm,
        const std::shared_ptr<file_grant_owner>& owner)
    {
        if (owner == nullptr || owner->released.load(std::memory_order_acquire)
            || owner->origin == "null"
            || registered_file_system_origin(realm) != owner->origin) return {};
        v8::Local<v8::Value> prototype;
        const auto prototype_key = owner->kind == WEBSCENE_FILE_PANEL_ENTRY_FILE_V2
            ? file_system_file_prototype_private(isolate)
            : file_system_directory_prototype_private(isolate);
        if (!realm->Global()->GetPrivate(
                realm, prototype_key).ToLocal(&prototype)
            || !prototype->IsObject()) return {};
        auto result = v8::Object::New(isolate);
        if (!result->SetPrototype(realm, prototype).FromMaybe(false)
            || !bind_file_system_handle(realm, result, owner)) return {};
        return result;
    }

    void retire_file_system_handles_for_contexts(
        const std::vector<v8::Local<v8::Context>>& contexts)
    {
        for (auto& binding : file_system_handle_bindings) {
            if (binding == nullptr || binding->reclaimed
                || binding->context.IsEmpty()) continue;
            const auto realm = binding->context.Get(isolate);
            if (std::find(contexts.begin(), contexts.end(), realm) == contexts.end())
                continue;
            binding->wrapper.Reset();
            binding->context.Reset();
            binding->owner.reset();
            binding->reclaimed = true;
        }
        std::erase_if(file_grant_owners, [](const auto& entry) {
            return entry.second.expired();
        });
    }

    void release_all_file_system_handles()
    {
        for (auto& binding : file_system_handle_bindings) {
            if (binding == nullptr) continue;
            binding->wrapper.Reset();
            binding->context.Reset();
            binding->owner.reset();
            binding->reclaimed = true;
        }
        file_system_handle_bindings.clear();
        file_grant_owners.clear();
    }
