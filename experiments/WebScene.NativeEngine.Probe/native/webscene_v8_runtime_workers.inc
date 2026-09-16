    struct worker_state {
        std::mutex mutex;
        std::condition_variable ready;
        std::deque<clone_packet> incoming, outgoing;
        size_t incoming_bytes{};
        size_t outgoing_bytes{};
        std::deque<std::string> errors;
        bool stopped{false};
        bool wake_requested{false};
        v8::Isolate* running_isolate{};
        std::function<void()> notify_parent;
        std::thread thread;
        v8::Global<v8::Object> wrapper;
        v8::Global<v8::Context> realm;
        void stop() {
            {
                std::lock_guard lock(mutex);
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
    bool force_dedicated_isolate{false};
    bool worker_module_realm{false};
    static constexpr size_t maximum_dedicated_workers = 64U;
    static constexpr size_t maximum_worker_queue_messages = 256U;
    static constexpr size_t maximum_worker_queue_bytes = 16U * 1024U * 1024U;
    static constexpr size_t maximum_worker_errors = 16U;
    static constexpr size_t maximum_worker_error_length = 4096U;

    static v8::MaybeLocal<v8::Value> worker_transfer(
        const v8::FunctionCallbackInfo<v8::Value>& info) {
        if (info.Length() < 2) return v8::Undefined(info.GetIsolate());
        if (info[1]->IsObject() && !info[1]->IsArray())
            return info[1].As<v8::Object>()->Get(info.GetIsolate()->GetCurrentContext(), js_string(info.GetIsolate(), "transfer"));
        return info[1];
    }
    static void worker_post_message_impl(const v8::FunctionCallbackInfo<v8::Value>& info) {
        auto* state = static_cast<worker_state*>(info.Data().As<v8::External>()->Value(v8::kExternalPointerTypeTagDefault));
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
        auto* state = static_cast<worker_state*>(info.Data().As<v8::External>()->Value(v8::kExternalPointerTypeTagDefault));
        state->stop();
    }
    static void worker_close(const v8::FunctionCallbackInfo<v8::Value>& info) {
        auto* self = current(info.GetIsolate());
        if (self && self->worker_owner) {
            std::lock_guard lock(self->worker_owner->mutex); self->worker_owner->stopped = true;
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
            if (resource_origin(url) != resource_origin(self->current_base_address())) {
                throw_dom_exception(info,"Worker script must be same origin","SecurityError");
                return;
            }
            std::string source,resolved,error;
            if (!self->load_text_resource(url,{},WEBSCENE_RESOURCE_SCRIPT,source,resolved)
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
            if (!state->outgoing.empty() || !state->errors.empty()) return true;
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
                else continue;
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
        // Until worker CORS/credentials are qualified, enforce a same-origin script.
        // Capture a same-origin object URL before entering another isolate. The
        // worker owns its source even if the parent revokes the URL immediately.
        std::optional<std::string> embedded_source;
        if (url.starts_with("blob:")) {
            const auto found = self->object_url_binary.find(url);
            if (found == self->object_url_binary.end()
                || found->second.origin != resource_origin(self->current_base_address())) {
                throw_dom_exception(info,"Worker Blob URL is unavailable for this origin","SecurityError"); return;
            }
            embedded_source.emplace(found->second.bytes.begin(), found->second.bytes.end());
        }
        if (!embedded_source
            && resource_origin(url) != resource_origin(self->current_base_address())) {
            throw_dom_exception(info,"Worker script must be same origin","SecurityError"); return;
        }
        bool module = false;
        std::string worker_name;
        if (info.Length()>1 && info[1]->IsObject()) {
            v8::Local<v8::Value> type;
            if (!info[1].As<v8::Object>()->Get(realm,js_string(isolate,"type")).ToLocal(&type)) return;
            if (!type->IsUndefined()) {
                auto text=to_utf8(isolate,type);
                if(text!="classic"&&text!="module"){
                    isolate->ThrowException(v8::Exception::TypeError(js_string(isolate,"Invalid WorkerType")));return;
                }
                module=text=="module";
            }
            v8::Local<v8::Value> name;
            if(!info[1].As<v8::Object>()->Get(realm,js_string(isolate,"name")).ToLocal(&name))return;
            if(!name->IsUndefined())worker_name=to_utf8(isolate,name);
        }
        if (self->dedicated_workers.size() >= maximum_dedicated_workers) {
            isolate->ThrowException(v8::Exception::RangeError(js_string(isolate,"Worker capacity exhausted"))); return;
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
        target->Set(realm,js_string(isolate,"postMessage"),v8::Function::New(realm,worker_post_message,data,1).ToLocalChecked()).Check();
        target->Set(realm,js_string(isolate,"terminate"),v8::Function::New(realm,worker_terminate,data).ToLocalChecked()).Check();
        auto loader=self->load_resource_callback;
        auto root=self->resource_root;
        auto worker_origin=resource_origin(self->current_base_address());
        self->dedicated_workers.push_back(std::move(state));
        raw->thread=std::thread([raw,url,module,worker_origin=std::move(worker_origin),embedded_source=std::move(embedded_source),worker_name=std::move(worker_name),loader=std::move(loader),root=std::move(root)] {
            auto report=[&](const std::string& text) {
                {
                    std::lock_guard lock(raw->mutex);
                    if(raw->stopped)return;
                    if(raw->errors.size()>=maximum_worker_errors)raw->errors.pop_front();
                    raw->errors.push_back(text.substr(0U,maximum_worker_error_length));
                }
                if(raw->notify_parent)raw->notify_parent();
            };
            try {
                native_document document;
                v8_dom_runtime runtime(document,[]{return v8_dom_runtime::viewport_metrics{1,1,1,0};},{},loader);
                auto* child=runtime.impl_.get();
                struct running_guard {
                    worker_state* state;implementation* child;
                    ~running_guard(){
                        std::lock_guard lock(state->mutex);state->running_isolate=nullptr;
                        if(child->isolate)child->isolate->CancelTerminateExecution();
                    }
                } guard{raw,child};
                child->force_dedicated_isolate=true;child->worker_owner=raw;child->worker_module_realm=module;
                child->runtime_work_available=[raw]{
                    {std::lock_guard lock(raw->mutex);raw->wake_requested=true;}
                    raw->ready.notify_one();
                };
                runtime.set_resource_root(root.string());
                if(!runtime.initialize())throw std::runtime_error(runtime.last_error());
                {
                    std::lock_guard lock(raw->mutex);
                    if(raw->stopped)return;
                    raw->running_isolate=child->isolate;
                }
                {
                    v8::Isolate::Scope entered(child->isolate);v8::HandleScope handles(child->isolate);
                    auto realm=child->context.Get(child->isolate);v8::Context::Scope scope(realm);
                    auto global=realm->Global();
                    child->document_base_address=url;
                    child->set_context_location(realm,global,url);
                    if(embedded_source) {
                        v8::Local<v8::Value> location;
                        if(global->Get(realm,js_string(child->isolate,"location")).ToLocal(&location)
                            &&location->IsObject())
                            location.As<v8::Object>()->Set(realm,js_string(child->isolate,"origin"),js_dom_string(child->isolate,worker_origin)).Check();
                        global->Set(realm,js_string(child->isolate,"origin"),js_dom_string(child->isolate,worker_origin)).Check();
                    }
                    global->Set(realm,js_string(child->isolate,"postMessage"),v8::Function::New(realm,worker_post_parent,{},1).ToLocalChecked()).Check();
                    global->Set(realm,js_string(child->isolate,"close"),v8::Function::New(realm,worker_close).ToLocalChecked()).Check();
                    global->Set(realm,js_string(child->isolate,"importScripts"),v8::Function::New(realm,worker_import_scripts).ToLocalChecked()).Check();
                    global->Set(realm,js_string(child->isolate,"self"),global).Check();
                    global->Set(realm,js_string(child->isolate,"name"),js_dom_string(child->isolate,worker_name)).Check();
                    global->Delete(realm,js_string(child->isolate,"document")).FromMaybe(false);
                    global->Delete(realm,js_string(child->isolate,"window")).FromMaybe(false);
                    std::string source,resolved,error;
                    if (embedded_source) { source = *embedded_source; resolved = url; }
                    if(!embedded_source && !child->load_text_resource(url,{},WEBSCENE_RESOURCE_SCRIPT,source,resolved))report("Unable to load worker: "+url);
                    else if(!(module ? child->execute_module(realm,source,resolved,error)
                                      : child->execute_in_context(realm,source,resolved,error)))report(error);
                }
                while(true) {
                    std::optional<clone_packet> packet;
                    {
                        std::unique_lock lock(raw->mutex);
                        raw->ready.wait_for(lock,runtime.recommended_idle_wait(std::chrono::milliseconds(1000)),
                            [&]{return raw->stopped||raw->wake_requested||!raw->incoming.empty();});
                        raw->wake_requested=false;
                        if(raw->stopped)break;
                        if(!raw->incoming.empty()){
                            packet=std::move(raw->incoming.front());raw->incoming.pop_front();
                            const auto bytes=clone_packet_size(*packet);
                            raw->incoming_bytes=bytes>raw->incoming_bytes?0U:raw->incoming_bytes-bytes;
                        }
                    }
                    v8::Isolate::Scope entered(child->isolate);v8::HandleScope handles(child->isolate);
                    auto realm=child->context.Get(child->isolate);v8::Context::Scope scope(realm);
                    if(packet&&!child->dispatch_worker_message(realm,realm->Global(),&*packet))report(child->last_error);
                    child->drain_tasks();
                }
                {std::lock_guard lock(raw->mutex);raw->running_isolate=nullptr;}
                child->isolate->CancelTerminateExecution();
            } catch(const std::exception& e) {
                {std::lock_guard lock(raw->mutex);raw->running_isolate=nullptr;}
                report(e.what());
            }
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
