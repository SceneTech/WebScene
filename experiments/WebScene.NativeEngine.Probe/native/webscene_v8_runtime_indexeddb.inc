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

    static constexpr std::array<std::uint8_t, 8> indexeddb_durable_magic{
        'W', 'S', 'I', 'D', 'B', 'F', 'H', '2'};

    static void append_indexeddb_u32(
        std::vector<std::uint8_t>& bytes, std::uint32_t value)
    {
        for (unsigned shift = 0; shift < 32; shift += 8)
            bytes.push_back(static_cast<std::uint8_t>(value >> shift));
    }

    static void append_indexeddb_u64(
        std::vector<std::uint8_t>& bytes, std::uint64_t value)
    {
        for (unsigned shift = 0; shift < 64; shift += 8)
            bytes.push_back(static_cast<std::uint8_t>(value >> shift));
    }

    static bool read_indexeddb_u32(
        const std::vector<std::uint8_t>& bytes, std::size_t& offset,
        std::uint32_t& value)
    {
        if (offset > bytes.size() || bytes.size() - offset < 4U) return false;
        value = 0;
        for (unsigned shift = 0; shift < 32; shift += 8)
            value |= static_cast<std::uint32_t>(bytes[offset++]) << shift;
        return true;
    }

    static bool read_indexeddb_u64(
        const std::vector<std::uint8_t>& bytes, std::size_t& offset,
        std::uint64_t& value)
    {
        if (offset > bytes.size() || bytes.size() - offset < 8U) return false;
        value = 0;
        for (unsigned shift = 0; shift < 64; shift += 8)
            value |= static_cast<std::uint64_t>(bytes[offset++]) << shift;
        return true;
    }

    static std::vector<std::uint8_t> encode_indexeddb_durable_packet(
        const clone_packet& packet,
        const std::vector<durable_file_grant_locator>& locators)
    {
        if (locators.empty()) return packet.bytes;
        if (locators.size() != packet.file_system_handles.size()
            || locators.size() > WEBSCENE_FILE_GRANT_DURABLE_MAXIMUM_RECORDS_V2)
            return {};
        std::vector<std::uint8_t> result;
        const auto locator_bytes = locators.size()
            * WEBSCENE_FILE_GRANT_DURABLE_LOCATOR_BYTES_V2;
        if (packet.bytes.size() > maximum_clone_packet_bytes
            || locator_bytes > maximum_clone_packet_bytes - packet.bytes.size()
            || 24U > maximum_clone_packet_bytes
                - packet.bytes.size() - locator_bytes) return {};
        result.reserve(24U + locator_bytes + packet.bytes.size());
        result.insert(result.end(), indexeddb_durable_magic.begin(),
            indexeddb_durable_magic.end());
        append_indexeddb_u32(result, 2U);
        append_indexeddb_u32(result, static_cast<std::uint32_t>(locators.size()));
        append_indexeddb_u64(result, packet.bytes.size());
        for (const auto& locator : locators)
            result.insert(result.end(), locator.begin(), locator.end());
        result.insert(result.end(), packet.bytes.begin(), packet.bytes.end());
        return result;
    }

    static bool decode_indexeddb_durable_packet(
        std::vector<std::uint8_t> bytes,
        clone_packet& packet,
        std::vector<durable_file_grant_locator>& locators)
    {
        if (bytes.size() < indexeddb_durable_magic.size()
            || !std::equal(indexeddb_durable_magic.begin(),
                indexeddb_durable_magic.end(), bytes.begin())) {
            packet.bytes = std::move(bytes);
            return true;
        }
        std::size_t offset = indexeddb_durable_magic.size();
        std::uint32_t version{};
        std::uint32_t count{};
        std::uint64_t payload_size{};
        if (!read_indexeddb_u32(bytes, offset, version)
            || !read_indexeddb_u32(bytes, offset, count)
            || !read_indexeddb_u64(bytes, offset, payload_size)
            || version != 2U || count == 0U
            || count > WEBSCENE_FILE_GRANT_DURABLE_MAXIMUM_RECORDS_V2
            || payload_size > maximum_clone_packet_bytes) return false;
        const auto locator_bytes = static_cast<std::size_t>(count)
            * WEBSCENE_FILE_GRANT_DURABLE_LOCATOR_BYTES_V2;
        if (offset > bytes.size() || locator_bytes > bytes.size() - offset
            || payload_size != bytes.size() - offset - locator_bytes)
            return false;
        locators.resize(count);
        for (auto& locator : locators) {
            std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                locator.size(), locator.begin());
            offset += locator.size();
        }
        packet.bytes.assign(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
            bytes.end());
        packet.file_system_handles.resize(count);
        return true;
    }

    static bool empty_durable_locator(
        const durable_file_grant_locator& locator)
    {
        return std::all_of(locator.begin(), locator.end(),
            [](std::uint8_t value) { return value == 0; });
    }

    void finish_indexeddb_operation(
        std::uint64_t id, indexeddb_storage_result result)
    {
        const auto found = pending_indexeddb_promises.find(id);
        if (found == pending_indexeddb_promises.end()) return;
        auto pending = std::move(found->second);
        pending_indexeddb_promises.erase(found);
        terminal_handoff_task_scope terminal_scope(
            terminal_handoff_task_active,
            pending.terminal_handoff_critical);
        auto realm = pending.context.Get(isolate);
        auto resolver = pending.resolver.Get(isolate);
        if (realm.IsEmpty() || resolver.IsEmpty()) return;
        v8::Context::Scope context_scope(realm);
        if (result.status != indexeddb_storage_status::ok
            && result.status != indexeddb_storage_status::not_found) {
            const char* name = "UnknownError";
            if (result.status == indexeddb_storage_status::quota_exceeded)
                name = "QuotaExceededError";
            else if (result.status == indexeddb_storage_status::conflict)
                name = "AbortError";
            else if (result.status == indexeddb_storage_status::unavailable)
                name = "NotSupportedError";
            else if (result.status == indexeddb_storage_status::corrupt)
                name = "DataError";
            resolver->Reject(realm, indexeddb_exception(realm,
                result.message.empty() ? "IndexedDB storage operation failed"
                                       : result.message,
                name)).Check();
            perform_microtask_checkpoint();
            return;
        }
        auto response = v8::Object::New(isolate);
        response->Set(realm, js_string(isolate, "revision"),
            v8::BigInt::NewFromUnsigned(isolate, result.revision)).Check();
        v8::Local<v8::Value> data = v8::Null(isolate);
        if (result.status == indexeddb_storage_status::ok
            && pending.deserialize_data) {
            v8::TryCatch caught(isolate);
            if (!deserialize_clone(isolate, realm, pending.packet).ToLocal(&data)) {
                if (caught.HasCaught()) {
                    last_error = describe_reported_exception(caught, realm);
                    caught.Reset();
                }
                resolver->Reject(realm, indexeddb_exception(realm,
                    "IndexedDB structured data could not be decoded",
                    "DataError")).Check();
                perform_microtask_checkpoint();
                return;
            }
        }
        response->Set(realm, js_string(isolate, "data"), data).Check();
        resolver->Resolve(realm, response).Check();
        perform_microtask_checkpoint();
    }

    void reject_indexeddb_durable_operation(
        std::uint64_t id, std::string message, std::string name)
    {
        const auto found = pending_indexeddb_promises.find(id);
        if (found == pending_indexeddb_promises.end()) return;
        auto pending = std::move(found->second);
        pending_indexeddb_promises.erase(found);
        terminal_handoff_task_scope terminal_scope(
            terminal_handoff_task_active,
            pending.terminal_handoff_critical);
        auto realm = pending.context.Get(isolate);
        auto resolver = pending.resolver.Get(isolate);
        if (realm.IsEmpty() || resolver.IsEmpty()) return;
        v8::Context::Scope context_scope(realm);
        resolver->Reject(realm, indexeddb_exception(realm, message, name.c_str()))
            .Check();
        perform_microtask_checkpoint();
    }

    void enqueue_indexeddb_storage(
        std::uint64_t id, indexeddb_operation_kind operation,
        const std::string& origin, const std::string& database,
        std::uint64_t revision = 0, std::vector<std::uint8_t> bytes = {})
    {
        const auto completion = [this, id](indexeddb_storage_result result) {
            enqueue_indexeddb_completion(id, std::move(result));
        };
        if (operation == indexeddb_operation_kind::load)
            indexeddb->load(origin, database, completion);
        else if (operation == indexeddb_operation_kind::erase)
            indexeddb->erase(origin, database, completion);
        else
            indexeddb->store(origin, database, revision, std::move(bytes), completion);
    }

    void pump_indexeddb_durable_requests()
    {
        if (!file_grant_durable_request_sink) return;
        std::vector<std::uint64_t> failed_operations;
        for (auto& [id, pending] : pending_indexeddb_promises) {
            if (indexeddb_durable_request_targets.size()
                    >= WEBSCENE_FILE_GRANT_DURABLE_MAXIMUM_PENDING_OPERATIONS_V2)
                break;
            if (pending.active_durable_requests != 0) continue;
            const bool exporting = pending.phase
                == indexeddb_operation_phase::export_handles;
            const bool restoring = pending.phase
                == indexeddb_operation_phase::restore_handles;
            const bool revoking = pending.phase
                == indexeddb_operation_phase::revoke_locators;
            if (!exporting && !restoring && !revoking) continue;
            const auto count = exporting
                ? pending.packet.file_system_handles.size()
                : pending.locators.size();
            if (pending.next_durable_index >= count) continue;
            const auto index = pending.next_durable_index++;
            const auto action = static_cast<std::uint32_t>(exporting
                ? WEBSCENE_FILE_GRANT_DURABLE_EXPORT_V2
                : restoring ? WEBSCENE_FILE_GRANT_DURABLE_RESTORE_V2
                            : WEBSCENE_FILE_GRANT_DURABLE_REVOKE_V2);
            webscene_file_grant_durable_request_v2 request{
                sizeof(request), 2U, ++next_host_request_id, action, 0U,
                {}, {}, {}, {}};
            if (exporting) {
                const auto& owner = pending.packet.file_system_handles[index];
                if (owner == nullptr
                    || owner->released.load(std::memory_order_acquire)) {
                    pending.failure_message =
                        "File system handle authority is unavailable";
                    pending.failure_name = "DataCloneError";
                    failed_operations.push_back(id);
                    continue;
                }
                request.grant_id = {owner->token.data(), owner->token.size()};
            } else {
                const auto& locator = pending.locators[index];
                request.locator = {locator.data(), locator.size()};
            }
            request.storage_partition = {storage_partition_key.data(),
                storage_partition_key.size()};
            request.serialized_origin = {pending.origin.data(),
                pending.origin.size()};
            indexeddb_durable_request_targets.emplace(request.request_id,
                indexeddb_durable_request_target{
                    id, index, action, pending.origin});
            ++pending.active_durable_requests;
            if (!file_grant_durable_request_sink(request)) {
                indexeddb_durable_request_targets.erase(request.request_id);
                --pending.active_durable_requests;
                if (action != WEBSCENE_FILE_GRANT_DURABLE_REVOKE_V2) {
                    pending.failure_message =
                        "The native durable file grant authority rejected the request";
                    pending.failure_name = "NotAllowedError";
                }
                failed_operations.push_back(id);
            }
        }
        bool repump = false;
        for (const auto id : failed_operations) {
            const auto found = pending_indexeddb_promises.find(id);
            if (found == pending_indexeddb_promises.end()) continue;
            auto& pending = found->second;
            if (pending.phase == indexeddb_operation_phase::revoke_locators) {
                if (pending.next_durable_index >= pending.locators.size()) {
                    if (pending.cleanup_after_failure) {
                        const auto message = pending.failure_message;
                        const auto name = pending.failure_name;
                        reject_indexeddb_durable_operation(id, message, name);
                    } else if (pending.terminal_result) {
                        auto result = std::move(*pending.terminal_result);
                        finish_indexeddb_operation(id, std::move(result));
                    }
                    continue;
                }
                repump = true;
                continue;
            }
            if (pending.phase == indexeddb_operation_phase::export_handles) {
                std::vector<durable_file_grant_locator> cleanup;
                for (const auto& locator : pending.locators)
                    if (!empty_durable_locator(locator)) cleanup.push_back(locator);
                if (!cleanup.empty()) {
                    pending.locators = std::move(cleanup);
                    pending.next_durable_index = 0;
                    pending.phase = indexeddb_operation_phase::revoke_locators;
                    pending.cleanup_after_failure = true;
                    repump = true;
                    continue;
                }
            }
            reject_indexeddb_durable_operation(
                id, pending.failure_message, pending.failure_name);
        }
        if (repump) pump_indexeddb_durable_requests();
    }

    void advance_indexeddb_durable_operation(std::uint64_t id)
    {
        const auto found = pending_indexeddb_promises.find(id);
        if (found == pending_indexeddb_promises.end()) return;
        auto& pending = found->second;
        if (pending.active_durable_requests != 0) return;
        const auto count = pending.phase == indexeddb_operation_phase::export_handles
            ? pending.packet.file_system_handles.size() : pending.locators.size();
        if (!pending.failure_name.empty()) {
            if (pending.phase == indexeddb_operation_phase::export_handles) {
                std::vector<durable_file_grant_locator> cleanup;
                for (const auto& locator : pending.locators)
                    if (!empty_durable_locator(locator)) cleanup.push_back(locator);
                if (!cleanup.empty()) {
                    pending.locators = std::move(cleanup);
                    pending.next_durable_index = 0;
                    pending.phase = indexeddb_operation_phase::revoke_locators;
                    pending.cleanup_after_failure = true;
                    pump_indexeddb_durable_requests();
                    return;
                }
            }
            reject_indexeddb_durable_operation(
                id, pending.failure_message, pending.failure_name);
            return;
        }
        if (pending.next_durable_index < count) {
            pump_indexeddb_durable_requests();
            return;
        }
        if (pending.phase == indexeddb_operation_phase::export_handles) {
            auto bytes = encode_indexeddb_durable_packet(
                pending.packet, pending.locators);
            if (bytes.empty()) {
                pending.failure_message = "IndexedDB durable handle envelope is too large";
                pending.failure_name = "QuotaExceededError";
                advance_indexeddb_durable_operation(id);
                return;
            }
            pending.phase = indexeddb_operation_phase::commit_storage;
            enqueue_indexeddb_storage(id, indexeddb_operation_kind::store,
                pending.origin, pending.database, pending.revision,
                std::move(bytes));
            return;
        }
        if (pending.phase == indexeddb_operation_phase::restore_handles) {
            indexeddb_storage_result result;
            result.status = indexeddb_storage_status::ok;
            result.revision = pending.revision;
            finish_indexeddb_operation(id, std::move(result));
            return;
        }
        if (pending.phase == indexeddb_operation_phase::revoke_locators) {
            if (pending.cleanup_after_failure) {
                reject_indexeddb_durable_operation(
                    id, pending.failure_message, pending.failure_name);
            } else if (pending.terminal_result) {
                finish_indexeddb_operation(id, std::move(*pending.terminal_result));
            }
        }
    }

    void complete_native_file_grant_durable(
        file_grant_durable_completion_data_v2& completion)
    {
        const auto target = indexeddb_durable_request_targets.find(
            completion.request_id);
        if (target == indexeddb_durable_request_targets.end()) return;
        const auto request = target->second;
        indexeddb_durable_request_targets.erase(target);
        const auto found = pending_indexeddb_promises.find(
            request.indexeddb_operation_id);
        if (found == pending_indexeddb_promises.end()) {
            if (request.action == WEBSCENE_FILE_GRANT_DURABLE_EXPORT_V2
                && completion.action == request.action
                && completion.status == WEBSCENE_FILE_GRANT_DURABLE_SUCCESS_V2
                && completion.locator.size()
                    == WEBSCENE_FILE_GRANT_DURABLE_LOCATOR_BYTES_V2
                && file_grant_durable_request_sink) {
                const webscene_file_grant_durable_request_v2 revoke{
                    sizeof(revoke), 2U, ++next_host_request_id,
                    WEBSCENE_FILE_GRANT_DURABLE_REVOKE_V2, 0U, {},
                    {completion.locator.data(), completion.locator.size()},
                    {storage_partition_key.data(), storage_partition_key.size()},
                    {request.serialized_origin.data(),
                        request.serialized_origin.size()}};
                (void)file_grant_durable_request_sink(revoke);
            }
            return;
        }
        auto& pending = found->second;
        if (pending.active_durable_requests != 0)
            --pending.active_durable_requests;
        const bool action_matches = completion.action == request.action;
        const bool success = action_matches
            && completion.status == WEBSCENE_FILE_GRANT_DURABLE_SUCCESS_V2;
        if (request.action == WEBSCENE_FILE_GRANT_DURABLE_EXPORT_V2 && success) {
            std::copy(completion.locator.begin(), completion.locator.end(),
                pending.locators[request.index].begin());
        } else if (request.action == WEBSCENE_FILE_GRANT_DURABLE_RESTORE_V2
            && success) {
            auto owner = std::make_shared<file_grant_owner>();
            owner->token = completion.grant_id;
            const auto realm = pending.context.Get(isolate);
            owner->origin = realm.IsEmpty()
                ? std::string{} : registered_file_system_origin(realm);
            owner->name = completion.display_name;
            owner->kind = completion.kind;
            owner->capabilities = completion.capabilities;
            owner->dispatcher = file_grant_releases;
            const std::string key(reinterpret_cast<const char*>(owner->token.data()),
                owner->token.size());
            file_grant_owners[key] = owner;
            pending.packet.file_system_handles[request.index] = std::move(owner);
        } else if (request.action == WEBSCENE_FILE_GRANT_DURABLE_REVOKE_V2) {
            // Revocation follows a committed replacement/deletion or cleans up
            // a failed export. It is best effort and never rolls back storage.
        } else {
            pending.failure_message = "Native durable file grant operation failed";
            pending.failure_name = completion.status
                    == WEBSCENE_FILE_GRANT_DURABLE_CANCELLED_V2
                ? "AbortError"
                : completion.status == WEBSCENE_FILE_GRANT_DURABLE_DENIED_V2
                    ? "SecurityError"
                    : completion.status == WEBSCENE_FILE_GRANT_DURABLE_NOT_FOUND_V2
                        ? "NotFoundError"
                        : completion.status == WEBSCENE_FILE_GRANT_DURABLE_LIMIT_V2
                            ? "QuotaExceededError" : "DataError";
        }
        advance_indexeddb_durable_operation(request.indexeddb_operation_id);
        pump_indexeddb_durable_requests();
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
        pending_indexeddb_promise pending;
        pending.context.Reset(info.GetIsolate(), realm);
        pending.resolver.Reset(info.GetIsolate(), resolver);
        pending.deserialize_data = operation == "load";
        pending.terminal_handoff_critical =
            self->window_close_lifecycle_dispatching
            || self->terminal_handoff_task_active;
        pending.origin = origin;
        pending.database = database;
        if (operation == "load") {
            pending.operation = indexeddb_operation_kind::load;
        } else if (operation == "delete") {
            pending.operation = indexeddb_operation_kind::erase;
        } else if (operation == "store") {
            if (info.Length() < 4 || !info[2]->IsBigInt()) {
                info.GetIsolate()->ThrowException(v8::Exception::TypeError(
                    js_string(info.GetIsolate(),
                        "IndexedDB store requires a revision and value")));
                return;
            }
            bool lossless = false;
            const auto revision = info[2].As<v8::BigInt>()->Uint64Value(&lossless);
            if (!lossless || !serialize_clone(
                    info, info[3], v8::Undefined(info.GetIsolate()), pending.packet,
                    maximum_clone_packet_bytes, true)) {
                return;
            }
            if (pending.packet.file_system_handles.size()
                > WEBSCENE_FILE_GRANT_DURABLE_MAXIMUM_RECORDS_V2) {
                info.GetIsolate()->ThrowException(self->indexeddb_exception(realm,
                    "Too many durable file system handles", "QuotaExceededError"));
                return;
            }
            pending.operation = indexeddb_operation_kind::store;
            pending.revision = revision;
        } else {
            info.GetIsolate()->ThrowException(v8::Exception::TypeError(
                js_string(info.GetIsolate(), "Unknown IndexedDB storage operation")));
            return;
        }
        self->pending_indexeddb_promises.emplace(id, std::move(pending));
        self->enqueue_indexeddb_storage(id, indexeddb_operation_kind::load,
            origin, database);
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
        auto& operation = pending->second;
        auto& result = completion.result;
        if (operation.phase == indexeddb_operation_phase::load_storage) {
            if (operation.operation == indexeddb_operation_kind::erase
                && result.status == indexeddb_storage_status::corrupt) {
                operation.phase = indexeddb_operation_phase::erase_storage;
                enqueue_indexeddb_storage(completion.id,
                    indexeddb_operation_kind::erase,
                    operation.origin, operation.database);
                return true;
            }
            if (result.status != indexeddb_storage_status::ok
                && result.status != indexeddb_storage_status::not_found) {
                finish_indexeddb_operation(completion.id, std::move(result));
                return true;
            }
            if (operation.operation == indexeddb_operation_kind::load) {
                operation.revision = result.revision;
                if (result.status == indexeddb_storage_status::not_found) {
                    finish_indexeddb_operation(completion.id, std::move(result));
                    return true;
                }
                if (!decode_indexeddb_durable_packet(std::move(result.bytes),
                        operation.packet, operation.locators)) {
                    reject_indexeddb_durable_operation(completion.id,
                        "IndexedDB durable handle envelope is invalid", "DataError");
                    return true;
                }
                if (operation.locators.empty()) {
                    indexeddb_storage_result loaded;
                    loaded.status = indexeddb_storage_status::ok;
                    loaded.revision = operation.revision;
                    finish_indexeddb_operation(completion.id, std::move(loaded));
                    return true;
                }
                operation.phase = indexeddb_operation_phase::restore_handles;
                operation.next_durable_index = 0;
                pump_indexeddb_durable_requests();
                return true;
            }
            if (operation.operation == indexeddb_operation_kind::store
                && result.revision != operation.revision) {
                indexeddb_storage_result conflict;
                conflict.status = indexeddb_storage_status::conflict;
                conflict.revision = result.revision;
                conflict.message = "IndexedDB revision changed";
                finish_indexeddb_operation(completion.id, std::move(conflict));
                return true;
            }
            clone_packet prior_packet;
            if (result.status == indexeddb_storage_status::ok
                && !decode_indexeddb_durable_packet(std::move(result.bytes),
                    prior_packet, operation.prior_locators)) {
                reject_indexeddb_durable_operation(completion.id,
                    "IndexedDB durable handle envelope is invalid", "DataError");
                return true;
            }
            if (operation.operation == indexeddb_operation_kind::erase) {
                operation.phase = indexeddb_operation_phase::erase_storage;
                enqueue_indexeddb_storage(completion.id,
                    indexeddb_operation_kind::erase,
                    operation.origin, operation.database);
                return true;
            }
            operation.locators.resize(operation.packet.file_system_handles.size());
            operation.phase = indexeddb_operation_phase::export_handles;
            operation.next_durable_index = 0;
            if (operation.locators.empty()) {
                operation.phase = indexeddb_operation_phase::commit_storage;
                enqueue_indexeddb_storage(completion.id,
                    indexeddb_operation_kind::store, operation.origin,
                    operation.database, operation.revision,
                    std::move(operation.packet.bytes));
            } else {
                pump_indexeddb_durable_requests();
            }
            return true;
        }
        if (operation.phase == indexeddb_operation_phase::commit_storage
            || operation.phase == indexeddb_operation_phase::erase_storage) {
            if (result.status != indexeddb_storage_status::ok) {
                if (operation.phase == indexeddb_operation_phase::commit_storage
                    && !operation.locators.empty()) {
                    operation.failure_message = result.message.empty()
                        ? "IndexedDB storage operation failed" : result.message;
                    operation.failure_name = result.status
                            == indexeddb_storage_status::conflict
                        ? "AbortError" : result.status
                                == indexeddb_storage_status::quota_exceeded
                            ? "QuotaExceededError" : "UnknownError";
                    operation.cleanup_after_failure = true;
                    operation.phase = indexeddb_operation_phase::revoke_locators;
                    operation.next_durable_index = 0;
                    pump_indexeddb_durable_requests();
                    return true;
                }
                finish_indexeddb_operation(completion.id, std::move(result));
                return true;
            }
            operation.terminal_result = result;
            operation.locators = std::move(operation.prior_locators);
            operation.next_durable_index = 0;
            operation.phase = indexeddb_operation_phase::revoke_locators;
            if (operation.locators.empty())
                finish_indexeddb_operation(completion.id, std::move(result));
            else
                pump_indexeddb_durable_requests();
        }
        return true;
    }
