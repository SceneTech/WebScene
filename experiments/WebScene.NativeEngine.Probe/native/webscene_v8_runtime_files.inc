    bool queue_file_request(dom_node& node, bool save) {
        auto local=context_for_node(node);
        // The first service slice is for the active top-level application.
        if (local != context.Get(isolate)) {
            isolate->ThrowException(v8::Exception::Error(js_string(isolate,"Native file service does not support iframe requests")));
            return false;
        }
        std::erase_if(file_targets, [&](auto& item) { return item.second.context.Get(isolate)!=local; });
        if(file_targets.size()>=16) {
            isolate->ThrowException(v8::Exception::Error(js_string(isolate,"Too many pending native file requests")));
            return false;
        }
        auto request=std::make_unique<native_file_request>();
        request->view.request_id=++next_host_request_id;
        request->view.kind=save ? 2 : 1;
        request->view.multiple=node.attributes.contains("multiple");
        request->accept=node.attributes.contains("accept") ? node.attributes.at("accept") : "";
        if(save) {
            const auto found=object_url_file_data.find(node.attributes["href"]);
            if(found==object_url_file_data.end() || object_url_canvas_node_ids.contains(node.attributes["href"])) {
                isolate->ThrowException(v8::Exception::Error(js_string(isolate,"Native Save requires a Blob with encoded file bytes")));
                return false;
            }
            request->file=found->second;
            request->file.name=node.attributes["download"];
            // A suggestion is a filename, never a path or a chosen destination.
            const auto slash=request->file.name.find_last_of("/\\");
            if(slash!=std::string::npos) request->file.name.erase(0,slash+1);
            std::erase_if(request->file.name,[](unsigned char c){return c<32 || c==127;});
            if(request->file.name.empty() || request->file.name=="." || request->file.name=="..") request->file.name="Export";
        }
        file_target target; target.node_id=node.id; target.context.Reset(isolate,local);
        file_targets.emplace(request->view.request_id,std::move(target));
        {
            std::lock_guard lock(file_requests_mutex);
            file_requests.push_back(std::move(request));
        }
        if(host_request_available) host_request_available();
        return true;
    }

    void complete_native_file(native_file_completion& completion) {
        const auto found=file_targets.find(completion.id);
        if(found==file_targets.end()) return;
        auto local=found->second.context.Get(isolate);
        const auto node_id=found->second.node_id;
        file_targets.erase(found);
        if(local!=context.Get(isolate)) return; // navigation invalidates old authority
        v8::Context::Scope context_scope(local);
        auto* node=document.find_by_native_id(node_id);
        // Detached save anchors are deliberately allowed; their bytes were
        // captured before the app revoked its URL. Completion has no DOM event.
        if(!node) return;
        webscene_input_event event{};
        if(completion.status) {
            dispatch_input_event_type(event,completion.status==1 ? "cancel" : "error",*node);
            return;
        }
        if(node->tag!="input") return;
        auto files=v8::Array::New(isolate,static_cast<int>(completion.files.size()));
        v8::Local<v8::Value> constructor;
        if(!local->Global()->Get(local,js_string(isolate,"File")).ToLocal(&constructor) || !constructor->IsFunction()) return;
        for(uint32_t i=0;i<completion.files.size();++i) {
            const auto& file=completion.files[i];
            auto buffer=v8::ArrayBuffer::New(isolate,file.bytes.size());
            if(!file.bytes.empty()) std::memcpy(buffer->GetBackingStore()->Data(),file.bytes.data(),file.bytes.size());
            auto parts=v8::Array::New(isolate,1);
            parts->Set(local,0,buffer).Check();
            auto options=v8::Object::New(isolate);
            options->Set(local,js_string(isolate,"type"),js_string(isolate,file.mime.c_str())).Check();
            v8::Local<v8::Value> args[]={parts,js_string(isolate,file.name.c_str()),options};
            v8::Local<v8::Object> value;
            if(!constructor.As<v8::Function>()->NewInstance(local,3,args).ToLocal(&value)) return;
            files->Set(local,i,value).Check();
        }
        // FileList's indexed access, length and item are the supported slice.
        files->Set(local,js_string(isolate,"item"),v8::Function::New(local,[](const v8::FunctionCallbackInfo<v8::Value>& info){
            auto ctx=info.GetIsolate()->GetCurrentContext();
            auto index=info.Length() ? info[0]->Uint32Value(ctx).FromMaybe(0) : 0;
            v8::Local<v8::Value> value;
            if(info.This()->Get(ctx,index).ToLocal(&value)) info.GetReturnValue().Set(value->IsUndefined() ? v8::Null(info.GetIsolate()).As<v8::Value>() : value);
        }).ToLocalChecked()).Check();
        wrap_node(*node)->Set(local,js_string(isolate,"files"),files).Check();
        dispatch_input_event_type(event,"input",*node);
        dispatch_input_event_type(event,"change",*node);
        isolate->PerformMicrotaskCheckpoint();
    }

    void complete_native_host(native_host_completion& completion) {
        const auto found = host_promise_targets.find(completion.id);
        if (found == host_promise_targets.end()) return;
        auto target = std::move(found->second);
        host_promise_targets.erase(found);
        auto local_context = target.context.Get(isolate);
        auto resolver = target.resolver.Get(isolate);
        if (local_context.IsEmpty() || resolver.IsEmpty()
            || local_context != context.Get(isolate)) return;
        v8::Context::Scope context_scope(local_context);
        if (completion.status != 0U) {
            const auto message = completion.error.empty()
                ? completion.status == 1U
                    ? "The native host operation was cancelled"
                    : "The native host operation was denied"
                : completion.error.c_str();
            auto error = v8::Exception::Error(js_string(isolate, message));
            if (error->IsObject()) {
                error.As<v8::Object>()->Set(
                    local_context,
                    js_string(isolate, "name"),
                    js_string(isolate,
                        completion.status == 1U ? "AbortError" : "NotAllowedError")).Check();
            }
            resolver->Reject(local_context, error).Check();
            perform_microtask_checkpoint();
            return;
        }
        if (target.operation == "writeClipboard") {
            resolver->Resolve(local_context, v8::Undefined(isolate)).Check();
            perform_microtask_checkpoint();
            return;
        }
        if (target.operation == "enterFullscreen"
            || target.operation == "exitFullscreen") {
            fullscreen_element = target.operation == "enterFullscreen"
                ? document.find_by_native_id(target.target_node_id)
                : nullptr;
            auto document_target = document_object.Get(isolate);
            if (!document_target.IsEmpty()) {
                auto event = create_event_instance(local_context);
                event->Set(
                    local_context,
                    js_string(isolate, "type"),
                    js_string(isolate, "fullscreenchange")).Check();
                event->Set(
                    local_context,
                    js_string(isolate, "bubbles"),
                    v8::True(isolate)).Check();
                event->Set(
                    local_context,
                    js_string(isolate, "cancelable"),
                    v8::False(isolate)).Check();
                v8::Local<v8::Value> dispatcher;
                if (document_target->Get(
                        local_context,
                        js_string(isolate, "dispatchEvent")).ToLocal(&dispatcher)
                    && dispatcher->IsFunction()) {
                    v8::Local<v8::Value> arguments[] = {event};
                    (void)dispatcher.As<v8::Function>()->Call(
                        local_context, document_target, 1, arguments);
                }
            }
            resolver->Resolve(local_context, v8::Undefined(isolate)).Check();
            perform_microtask_checkpoint();
            return;
        }
        auto buffer = v8::ArrayBuffer::New(isolate, completion.bytes.size());
        if (!completion.bytes.empty()) {
            std::memcpy(
                buffer->GetBackingStore()->Data(),
                completion.bytes.data(),
                completion.bytes.size());
        }
        auto value = v8::Object::New(isolate);
        value->Set(
            local_context,
            js_string(isolate, "type"),
            js_string(isolate, completion.content_type.c_str())).Check();
        value->Set(
            local_context,
            js_string(isolate, "bytes"),
            v8::Uint8Array::New(buffer, 0, completion.bytes.size())).Check();
        resolver->Resolve(local_context, value).Check();
        perform_microtask_checkpoint();
    }

    void cancel_native_host_promises(const char* message) {
        for (auto& [_, target] : host_promise_targets) {
            auto local_context = target.context.Get(isolate);
            auto resolver = target.resolver.Get(isolate);
            if (local_context.IsEmpty() || resolver.IsEmpty()) continue;
            v8::Context::Scope context_scope(local_context);
            auto error = v8::Exception::Error(js_string(isolate, message));
            if (error->IsObject()) {
                error.As<v8::Object>()->Set(
                    local_context,
                    js_string(isolate, "name"),
                    js_string(isolate, "AbortError")).Check();
            }
            resolver->Reject(local_context, error).Check();
        }
        host_promise_targets.clear();
        perform_microtask_checkpoint();
    }
