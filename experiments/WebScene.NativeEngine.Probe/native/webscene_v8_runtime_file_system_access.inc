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

    void install_file_system_access_api(v8::Local<v8::Context> local_context)
    {
        auto global = local_context->Global();
        global->Set(local_context, js_string(isolate, "__webSceneRequestFilePanel"),
            v8::Function::New(local_context, request_file_panel).ToLocalChecked()).Check();
        constexpr std::string_view source = R"JS(
          (() => {
            const requestPanel = globalThis.__webSceneRequestFilePanel;
            delete globalThis.__webSceneRequestFilePanel;
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
            Object.setPrototypeOf(FileSystemFileHandle.prototype, FileSystemHandle.prototype);
            Object.setPrototypeOf(FileSystemDirectoryHandle.prototype, FileSystemHandle.prototype);
            function isSameEntry(other) {
              return Promise.resolve().then(() =>
                stateFor(this).key === stateFor(other).key);
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
            function getFile() { stateFor(this); return unavailable('getFile'); }
            function createWritable(options = {}) {
              stateFor(this); return unavailable('createWritable');
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
