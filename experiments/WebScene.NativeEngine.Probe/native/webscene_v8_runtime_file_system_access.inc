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
            js_string(isolate, "__webSceneReadFileGrant"),
            v8::Function::New(local_context, request_file_grant_read)
                .ToLocalChecked()).Check();
        global->Set(local_context,
            js_string(isolate, "__webSceneWriteFileGrant"),
            v8::Function::New(local_context, request_file_grant_write)
                .ToLocalChecked()).Check();
        constexpr std::string_view source = R"JS(
          (() => {
            const requestPanel = globalThis.__webSceneRequestFilePanel;
            const compareGrants = globalThis.__webSceneCompareFileGrants;
            const readFileGrant = globalThis.__webSceneReadFileGrant;
            const writeFileGrant = globalThis.__webSceneWriteFileGrant;
            const createNativeFile = globalThis.__webSceneCreateNativeFile;
            delete globalThis.__webSceneRequestFilePanel;
            delete globalThis.__webSceneCompareFileGrants;
            delete globalThis.__webSceneReadFileGrant;
            delete globalThis.__webSceneWriteFileGrant;
            delete globalThis.__webSceneCreateNativeFile;
            const states = new WeakMap();
            const handles = new Map();
            const textEncoder = new TextEncoder();
            const permission = (state, mode) => {
              if (mode !== 'read' && mode !== 'readwrite') {
                throw new TypeError(`Unsupported permission mode ${mode}`);
              }
              const read = (state.capabilities & 5) !== 0;
              const write = (state.capabilities & 26) !== 0;
              return mode === 'read' ? read : write;
            };
            const stateFor = value => {
              const state = states.get(value);
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
                return permission(state, String(options.mode || 'read'))
                  ? 'granted' : 'denied';
              });
            }
            function requestPermission(options = {}) {
              return Promise.resolve().then(() => {
                const state = stateFor(this);
                return permission(state, String(options.mode || 'read'))
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
            const abortWriterState = async state => {
              const transaction = state.transaction;
              state.transaction = undefined;
              if (transaction) {
                try { await writeFileGrant(4, null, transaction, 0, null, 0); }
                catch { }
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
                await abortWriterState(state);
                throw error;
              }
            };
            const queueWriterCommand = (stream, command) => {
              if (!writerStates.has(stream)) {
                return Promise.reject(new TypeError('Illegal invocation'));
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
                if (!permission(state, 'readwrite')) {
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
                    await abortWriterState(writerState);
                    throw error;
                  }
                });
            }
            function getFileHandle(name, options = {}) {
              stateFor(this); return unavailable('getFileHandle');
            }
            function getDirectoryHandle(name, options = {}) {
              stateFor(this); return unavailable('getDirectoryHandle');
            }
            function removeEntry(name, options = {}) {
              stateFor(this); return unavailable('removeEntry');
            }
            function resolve(possibleDescendant) {
              stateFor(this); return unavailable('resolve');
            }
            Object.defineProperties(FileSystemFileHandle.prototype, {
              getFile: { enumerable: true, configurable: true, writable: true,
                value: getFile },
              createWritable: { enumerable: true, configurable: true, writable: true,
                value: createWritable },
              [Symbol.toStringTag]: { configurable: true, value: 'FileSystemFileHandle' }
            });
            Object.defineProperties(FileSystemDirectoryHandle.prototype, {
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
            const bytesKey = bytes => Array.from(bytes, value =>
              value.toString(16).padStart(2, '0')).join('');
            const createHandle = raw => {
              const key = bytesKey(raw.grant);
              const existing = handles.get(key);
              if (existing) {
                const state = stateFor(existing);
                if (state.kind !== (raw.kind === 1 ? 'file' : 'directory')
                    || state.name !== raw.name
                    || state.capabilities !== raw.capabilities) {
                  throw new DOMException('Native grant metadata changed', 'DataError');
                }
                return existing;
              }
              if (handles.size >= 4096) {
                throw new DOMException('Too many retained file system handles', 'QuotaExceededError');
              }
              const kind = raw.kind === 1 ? 'file' : 'directory';
              const handle = Object.create(kind === 'file'
                ? FileSystemFileHandle.prototype : FileSystemDirectoryHandle.prototype);
              states.set(handle, Object.freeze({
                key, kind, name: String(raw.name),
                capabilities: Number(raw.capabilities), token: raw.grant.slice()
              }));
              handles.set(key, handle);
              return handle;
            };
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
            const normalize = (kind, options) => {
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
              showOpenFilePicker: { configurable: true, enumerable: true,
                writable: true, value: showOpenFilePicker },
              showDirectoryPicker: { configurable: true, enumerable: true,
                writable: true, value: showDirectoryPicker },
              showSaveFilePicker: { configurable: true, enumerable: true,
                writable: true, value: showSaveFilePicker }
            });
          })();
        )JS";
        auto script = v8::Script::Compile(
            local_context,
            js_string(isolate, std::string(source).c_str())).ToLocalChecked();
        script->Run(local_context).ToLocalChecked();
    }
