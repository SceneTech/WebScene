    std::string describe_exception(
        v8::TryCatch& try_catch,
        v8::Local<v8::Context> exception_context = {})
    {
        std::ostringstream result;
        result << to_utf8(isolate, try_catch.Exception());
        auto message = try_catch.Message();
        if (!message.IsEmpty()) {
            result << " at " << to_utf8(isolate, message->GetScriptOrigin().ResourceName());
            auto local_context = exception_context.IsEmpty()
                ? isolate->GetCurrentContext()
                : exception_context;
            const auto line = message->GetLineNumber(local_context).FromMaybe(0);
            if (line > 0) result << ':' << line;
        }
        auto stack = try_catch.StackTrace(exception_context.IsEmpty()
            ? isolate->GetCurrentContext()
            : exception_context);
        v8::Local<v8::Value> stack_value;
        if (stack.ToLocal(&stack_value) && stack_value->IsString()) {
            const auto stack_text = to_utf8(isolate, stack_value);
            if (!stack_text.empty()) result << "\n" << stack_text;
        }
        return result.str();
    }

    void ensure_layout(const dom_node* reusable_client_geometry_subject = nullptr)
    {
        webscene_frame_trace::scope trace(frame_trace, "layout-start", "layout-end", current_input_sequence);
        flush_pending_style_recascades();
        if (!document.dirty()) return;
        if (reusable_client_geometry_subject != nullptr
            && document.can_reuse_client_geometry(
                *reusable_client_geometry_subject)) {
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
            ++client_geometry_layout_reuse_count;
#endif
            return;
        }
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        binding_callback_timer timer(profile_startup ? &startup_layout : nullptr);
        auto* forced_layout_stats = static_cast<binding_callback_stats*>(nullptr);
        if (profile_bindings) {
            auto subject = std::string{"unscoped"};
            if (reusable_client_geometry_subject != nullptr) {
                subject = reusable_client_geometry_subject->tag;
                if (!reusable_client_geometry_subject->id_attribute.empty()) {
                    subject += '#';
                    subject += reusable_client_geometry_subject->id_attribute;
                }
                if (!reusable_client_geometry_subject->class_name.empty()) {
                    subject += '.';
                    subject += reusable_client_geometry_subject->class_name.substr(0, 96);
                }
            }
            forced_layout_stats = &startup_forced_layout_profiles[std::move(subject)];
        }
        binding_callback_timer forced_layout_timer(forced_layout_stats);
#endif
        const auto [width, height, device_scale_factor, preferred_color_scheme] =
            viewport_provider();
        static_cast<void>(device_scale_factor);
        static_cast<void>(preferred_color_scheme);
        document.layout(width, height);
    }

    void refresh_all_shadow_distributions()
    {
        document.refresh_shadow_distributions();
    }

    static void throw_dom_exception(
        v8::Isolate* isolate,
        const char* message,
        const char* name)
    {
        auto local_context = isolate->GetCurrentContext();
        auto exception = v8::Object::New(isolate);
        exception->Set(
            local_context, js_string(isolate, "message"), js_string(isolate, message)).Check();
        exception->Set(
            local_context, js_string(isolate, "name"), js_string(isolate, name)).Check();
        v8::Local<v8::Value> constructor;
        if (local_context->Global()->Get(
                local_context, js_string(isolate, "DOMException"))
                .ToLocal(&constructor)
            && constructor->IsFunction()) {
            v8::Local<v8::Value> arguments[] = {
                js_string(isolate, message),
                js_string(isolate, name)};
            v8::Local<v8::Object> constructed;
            if (constructor.As<v8::Function>()->NewInstance(
                    local_context, 2, arguments).ToLocal(&constructed)) {
                isolate->ThrowException(constructed);
                return;
            }
            v8::Local<v8::Value> prototype;
            if (constructor.As<v8::Function>()->Get(
                    local_context, js_string(isolate, "prototype")).ToLocal(&prototype)
                && prototype->IsObject()) {
                exception->SetPrototype(local_context, prototype).Check();
            }
        }
        isolate->ThrowException(exception);
    }

    static void throw_dom_exception(
        const v8::FunctionCallbackInfo<v8::Value>& info,
        const char* message,
        const char* name)
    {
        throw_dom_exception(info.GetIsolate(), message, name);
    }

    static void attach_shadow(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* host = unwrap_node(info.This());
        if (self == nullptr || host == nullptr
            || host->kind != dom_node_kind::element) {
            info.GetIsolate()->ThrowException(v8::Exception::TypeError(
                js_string(info.GetIsolate(), "attachShadow requires an Element")));
            return;
        }
        static const std::unordered_set<std::string_view> standard_shadow_hosts{
            "article", "aside", "blockquote", "body", "div", "footer",
            "h1", "h2", "h3", "h4", "h5", "h6", "header", "main",
            "nav", "p", "section", "span"};
        if (host->tag.find('-') == std::string::npos
            && !standard_shadow_hosts.contains(host->tag)) {
            throw_dom_exception(
                info, "This element cannot host a shadow tree", "NotSupportedError");
            return;
        }
        if (self->document.shadow_dom(*host) != nullptr
            && self->document.shadow_dom(*host)->root != nullptr) {
            throw_dom_exception(
                info, "This element already hosts a shadow tree", "NotSupportedError");
            return;
        }
        if (info.Length() < 1 || !info[0]->IsObject()) {
            info.GetIsolate()->ThrowException(v8::Exception::TypeError(
                js_string(info.GetIsolate(), "attachShadow requires ShadowRootInit")));
            return;
        }
        auto local_context = info.GetIsolate()->GetCurrentContext();
        auto options = info[0].As<v8::Object>();
        v8::Local<v8::Value> raw_mode;
        if (!options->Get(local_context, js_string(info.GetIsolate(), "mode"))
                .ToLocal(&raw_mode)) {
            return;
        }
        const auto mode = to_utf8(info.GetIsolate(), raw_mode);
        if (mode != "open" && mode != "closed") {
            info.GetIsolate()->ThrowException(v8::Exception::TypeError(
                js_string(info.GetIsolate(), "ShadowRoot mode must be 'open' or 'closed'")));
            return;
        }
        const auto open = mode == "open";
        v8::Local<v8::Value> raw_delegates_focus;
        const auto delegates_focus = options->Get(
                local_context,
                js_string(info.GetIsolate(), "delegatesFocus"))
                .ToLocal(&raw_delegates_focus)
            && raw_delegates_focus->BooleanValue(info.GetIsolate());
        auto& root = self->document.create_node(
            dom_node_kind::document_fragment, "#document-fragment");
        root.parent = host;
        // The ShadowRoot itself generates no box, but its logical children are
        // parsed and mutated as visible participants in the host projection.
        root.visible = true;
        auto& host_state = self->document.mutable_shadow_dom(*host);
        host_state.root = &root;
        host_state.open = open;
        host_state.delegates_focus = delegates_focus;
        auto& root_state = self->document.mutable_shadow_dom(root);
        root_state.host = host;
        root_state.open = open;
        root_state.delegates_focus = delegates_focus;
        self->document.refresh_shadow_distributions();
        self->document.mark_dirty();
        info.GetReturnValue().Set(self->wrap_node(root));
    }

    static void get_shadow_root(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* host = unwrap_node(info.Holder());
        const auto* state = host == nullptr ? nullptr : self->document.shadow_dom(*host);
        auto* root = state != nullptr && state->open
            ? state->root
            : nullptr;
        info.GetReturnValue().Set(root == nullptr
            ? v8::Local<v8::Value>(v8::Null(info.GetIsolate()))
            : v8::Local<v8::Value>(self->wrap_node(*root)));
    }

    static void get_shadow_host(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* root = unwrap_node(info.Holder());
        const auto* state = root == nullptr ? nullptr : self->document.shadow_dom(*root);
        auto* host = state != nullptr
            ? state->host
            : nullptr;
        info.GetReturnValue().Set(host == nullptr
            ? v8::Local<v8::Value>(v8::Null(info.GetIsolate()))
            : v8::Local<v8::Value>(self->wrap_node(*host)));
    }

    static void get_shadow_mode(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* root = unwrap_node(info.Holder());
        const auto* state = root == nullptr ? nullptr : self->document.shadow_dom(*root);
        info.GetReturnValue().Set(js_string(
            info.GetIsolate(), state != nullptr && !state->open ? "closed" : "open"));
    }

    static void get_shadow_delegates_focus(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* root = unwrap_node(info.Holder());
        const auto* state = root == nullptr ? nullptr : self->document.shadow_dom(*root);
        info.GetReturnValue().Set(v8::Boolean::New(
            info.GetIsolate(),
            state != nullptr && state->delegates_focus));
    }

    static void get_assigned_slot(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        self->refresh_all_shadow_distributions();
        auto* node = unwrap_node(info.Holder());
        const auto* state = node == nullptr ? nullptr : self->document.shadow_dom(*node);
        auto* slot = state != nullptr
            ? state->assigned_slot
            : nullptr;
        if (slot != nullptr) {
            const auto* root = self->document.containing_shadow_root(*slot);
            const auto* root_state = root == nullptr
                ? nullptr
                : self->document.shadow_dom(*root);
            if (root_state != nullptr && !root_state->open) slot = nullptr;
        }
        info.GetReturnValue().Set(slot == nullptr
            ? v8::Local<v8::Value>(v8::Null(info.GetIsolate()))
            : v8::Local<v8::Value>(self->wrap_node(*slot)));
    }

    static bool assigned_nodes_flatten_requested(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        if (info.Length() == 0 || !info[0]->IsObject()) return false;
        v8::Local<v8::Value> flatten;
        return info[0].As<v8::Object>()->Get(
                info.GetIsolate()->GetCurrentContext(),
                js_string(info.GetIsolate(), "flatten"))
                .ToLocal(&flatten)
            && flatten->BooleanValue(info.GetIsolate());
    }

    static void append_flattened_slot_nodes(
        native_document& document,
        dom_node& slot,
        std::vector<dom_node*>& result)
    {
        const auto* state = document.shadow_dom(slot);
        const auto& candidates = state != nullptr && !state->assigned_nodes.empty()
            ? state->assigned_nodes
            : slot.children;
        for (auto* candidate : candidates) {
            if (candidate != nullptr
                && candidate->kind == dom_node_kind::element
                && candidate->tag == "slot") {
                append_flattened_slot_nodes(document, *candidate, result);
            } else if (candidate != nullptr) {
                result.push_back(candidate);
            }
        }
    }

    static std::vector<dom_node*> slot_nodes_for_options(
        native_document& document,
        dom_node* slot,
        bool flatten)
    {
        if (slot == nullptr || document.containing_shadow_root(*slot) == nullptr) return {};
        const auto* state = document.shadow_dom(*slot);
        if (!flatten) return state == nullptr
            ? std::vector<dom_node*>{}
            : state->assigned_nodes;
        std::vector<dom_node*> result;
        append_flattened_slot_nodes(document, *slot, result);
        return result;
    }

    static void slot_assigned_nodes(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        self->refresh_all_shadow_distributions();
        auto* slot = unwrap_node(info.This());
        const auto nodes = slot_nodes_for_options(
            self->document, slot, assigned_nodes_flatten_requested(info));
        const auto count = nodes.size();
        auto result = v8::Array::New(info.GetIsolate(), static_cast<int>(count));
        auto local_context = info.GetIsolate()->GetCurrentContext();
        for (uint32_t index = 0; index < count; ++index) {
            result->Set(
                local_context, index,
                self->wrap_node(*nodes[index])).Check();
        }
        info.GetReturnValue().Set(result);
    }

    static void slot_assigned_elements(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        self->refresh_all_shadow_distributions();
        auto* slot = unwrap_node(info.This());
        const auto nodes = slot_nodes_for_options(
            self->document, slot, assigned_nodes_flatten_requested(info));
        std::vector<dom_node*> elements;
        for (auto* node : nodes) {
            if (node != nullptr && node->kind == dom_node_kind::element) {
                elements.push_back(node);
            }
        }
        auto result = v8::Array::New(
            info.GetIsolate(), static_cast<int>(elements.size()));
        auto local_context = info.GetIsolate()->GetCurrentContext();
        for (uint32_t index = 0; index < elements.size(); ++index) {
            result->Set(
                local_context, index, self->wrap_node(*elements[index])).Check();
        }
        info.GetReturnValue().Set(result);
    }

    static void get_style_sheet_rules_source(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* owner = info.Data()->IsObject()
            ? unwrap_node(info.Data().As<v8::Object>())
            : nullptr;
        if (self == nullptr || owner == nullptr) return;
        std::string source;
        if (const auto known = self->stylesheet_cssom_sources.find(owner->id);
            known != self->stylesheet_cssom_sources.end()) {
            source = known->second;
        } else if (owner->tag == "style") {
            append_node_text(*owner, source);
        }
        info.GetReturnValue().Set(js_string(info.GetIsolate(), source.c_str()));
    }

    static void load_style_sheet_import(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        if (self == nullptr || info.Length() < 2) return;
        const auto local_context = info.GetIsolate()->GetCurrentContext();
        v8::Local<v8::String> href_value;
        v8::Local<v8::String> base_value;
        if (!info[0]->ToString(local_context).ToLocal(&href_value)
            || !info[1]->ToString(local_context).ToLocal(&base_value)) {
            return;
        }
        std::string source;
        std::string resolved;
        if (!self->load_parser_text_resource(
                to_wtf8(info.GetIsolate(), href_value),
                to_wtf8(info.GetIsolate(), base_value),
                WEBSCENE_RESOURCE_STYLESHEET,
                WEBSCENE_REQUEST_DESTINATION_STYLE,
                source,
                resolved)) {
            info.GetReturnValue().Set(v8::Null(info.GetIsolate()));
            return;
        }
        auto result = v8::Object::New(info.GetIsolate());
        result->Set(local_context, js_string(info.GetIsolate(), "source"),
            js_string(info.GetIsolate(), source.c_str())).Check();
        result->Set(local_context, js_string(info.GetIsolate(), "url"),
            js_string(info.GetIsolate(), resolved.c_str())).Check();
        info.GetReturnValue().Set(result);
    }

    v8::Local<v8::Object> style_sheet_for(dom_node& node)
    {
        const auto key = wrapper_key(node);
        const auto known = stylesheet_wrappers.find(key);
        if (known != stylesheet_wrappers.end() && !known->second.IsEmpty()) {
            return augment_style_sheet(known->second.Get(isolate));
        }
        const auto local_context = isolate->GetCurrentContext();
        auto result = v8::Object::New(isolate);
        result->Set(
            local_context,
            js_string(isolate, "ownerNode"),
            wrap_node(node)).Check();
        result->Set(local_context, js_string(isolate, "ownerRule"),
            v8::Null(isolate)).Check();
        result->Set(local_context, js_string(isolate, "type"),
            js_string(isolate, "text/css")).Check();
        const auto* cascade_root = css_cascade_root_for_node(node);
        auto base_address = effective_document_base(cascade_root);
        if (const auto known = stylesheet_cssom_addresses.find(node.id);
            known != stylesheet_cssom_addresses.end()) {
            base_address = known->second;
        } else if (node.tag == "link") {
            if (const auto href = node.attributes.find("href");
                href != node.attributes.end()) {
                base_address = resolve_resource_url(href->second, base_address);
            }
        }
        result->Set(local_context, js_string(isolate, "href"),
            node.tag == "link" ? js_string(isolate, base_address.c_str()).As<v8::Value>()
                               : v8::Null(isolate).As<v8::Value>()).Check();
        result->Set(local_context, js_string(isolate, "title"),
            node.attributes.contains("title")
                ? js_string(isolate, node.attributes.at("title").c_str()).As<v8::Value>()
                : v8::Null(isolate).As<v8::Value>()).Check();
        result->DefineOwnProperty(
            local_context,
            js_string(isolate, "__webSceneBaseURL"),
            js_string(isolate, base_address.c_str()),
            static_cast<v8::PropertyAttribute>(
                v8::DontEnum | v8::DontDelete | v8::ReadOnly)).Check();
        result->DefineOwnProperty(
            local_context,
            js_string(isolate, "__webSceneExternalSource"),
            v8::Boolean::New(isolate, node.tag == "link"),
            static_cast<v8::PropertyAttribute>(
                v8::DontEnum | v8::DontDelete | v8::ReadOnly)).Check();
        auto source_reader = v8::Function::New(
            local_context,
            get_style_sheet_rules_source,
            wrap_node(node)).ToLocalChecked();
        result->DefineOwnProperty(
            local_context,
            js_string(isolate, "__webSceneGetRulesSource"),
            source_reader,
            static_cast<v8::PropertyAttribute>(
                v8::DontEnum | v8::DontDelete | v8::ReadOnly)).Check();
        auto import_loader = v8::Function::New(
            local_context,
            load_style_sheet_import).ToLocalChecked();
        result->DefineOwnProperty(
            local_context,
            js_string(isolate, "__webSceneLoadImport"),
            import_loader,
            static_cast<v8::PropertyAttribute>(
                v8::DontEnum | v8::DontDelete | v8::ReadOnly)).Check();
        auto stage_rules = v8::Function::New(
            local_context,
            stage_style_sheet_rules,
            wrap_node(node)).ToLocalChecked();
        result->DefineOwnProperty(
            local_context,
            js_string(isolate, "__webSceneStageRules"),
            stage_rules,
            static_cast<v8::PropertyAttribute>(
                v8::DontEnum | v8::DontDelete | v8::ReadOnly))
            .Check();
        stylesheet_wrappers.emplace(key, v8::Global<v8::Object>(isolate, result));
        return augment_style_sheet(result);
    }

    static void stage_style_sheet_rules(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* owner = info.Data()->IsObject()
            ? unwrap_node(info.Data().As<v8::Object>())
            : nullptr;
        if (self == nullptr || owner == nullptr || info.Length() < 1) return;
        v8::Local<v8::String> converted;
        if (!info[0]->ToString(info.GetIsolate()->GetCurrentContext())
                .ToLocal(&converted)) {
            return;
        }
        auto source = to_wtf8(info.GetIsolate(), converted);
        std::vector<std::string> layer_names;
        constexpr auto maximum_layer_names = uint32_t{4096U};
        constexpr auto maximum_layer_name_bytes = size_t{1024U * 1024U};
        auto layer_name_bytes = size_t{0U};
        if (info.Length() > 1 && info[1]->IsArray()) {
            const auto input = info[1].As<v8::Array>();
            const auto count = std::min(input->Length(), maximum_layer_names);
            layer_names.reserve(count);
            for (uint32_t index = 0; index < count; ++index) {
                v8::Local<v8::Value> value;
                v8::Local<v8::String> name;
                if (!input->Get(info.GetIsolate()->GetCurrentContext(), index)
                        .ToLocal(&value)
                    || !value->ToString(info.GetIsolate()->GetCurrentContext())
                        .ToLocal(&name)) {
                    return;
                }
                auto decoded = to_wtf8(info.GetIsolate(), name);
                if (decoded.empty()
                    || decoded.size() > maximum_layer_name_bytes - std::min(
                        layer_name_bytes, maximum_layer_name_bytes)) {
                    return;
                }
                layer_name_bytes += decoded.size();
                if (std::find(layer_names.begin(), layer_names.end(), decoded)
                    == layer_names.end()) {
                    layer_names.push_back(std::move(decoded));
                }
            }
        }
        self->erase_pending_stylesheet_replacement(owner->id);
        auto* cascade_root = self->css_cascade_root_for_node(*owner);
        auto base_address = self->effective_document_base(cascade_root);
        if (const auto known = self->stylesheet_cssom_addresses.find(owner->id);
            known != self->stylesheet_cssom_addresses.end()) {
            base_address = known->second;
        }
        self->stylesheet_cssom_addresses.insert_or_assign(owner->id, base_address);
        constexpr auto maximum_pending_owners = size_t{1024U};
        constexpr auto maximum_pending_bytes = size_t{32U * 1024U * 1024U};
        if (source.size() > maximum_pending_bytes) {
            // A single source cannot fit in the bounded coalescing queue. Keep
            // the CSSOM source for disconnect/reconnect semantics, but publish
            // connected owners immediately so the same bytes are never copied
            // into pending replacement storage as well.
            self->flush_pending_stylesheet_replacements();
            self->rebuild_cascade_layer_order_for_stylesheet(
                owner->id, layer_names);
            self->stylesheet_cssom_sources.insert_or_assign(
                owner->id, std::move(source));
            self->activate_connected_stylesheet(*owner);
            self->document.mark_dirty();
            info.GetReturnValue().Set(v8::False(info.GetIsolate()));
            return;
        }
        if (self->pending_stylesheet_replacements.size() >= maximum_pending_owners
            || source.size() > maximum_pending_bytes
                - std::min(
                    self->pending_stylesheet_replacement_bytes,
                    maximum_pending_bytes)) {
            self->flush_pending_stylesheet_replacements();
        }
        self->stylesheet_cssom_sources.insert_or_assign(owner->id, source);
        self->pending_stylesheet_replacement_bytes += source.size();
        self->pending_stylesheet_replacements.push_back({
            owner->id,
            cascade_root == nullptr ? 0U : cascade_root->id,
            base_address,
            std::move(source),
            std::move(layer_names)});
        self->document.mark_dirty();
        info.GetReturnValue().Set(v8::True(info.GetIsolate()));
    }

    v8::Local<v8::Object> augment_style_sheet(v8::Local<v8::Object> sheet)
    {
        const auto local_context = isolate->GetCurrentContext();
        const auto global = local_context->Global();
        v8::Local<v8::Value> raw_augment;
        if (!global->Get(
                local_context,
                js_string(isolate, "__webSceneAugmentStyleSheet"))
                .ToLocal(&raw_augment)
            || !raw_augment->IsFunction()) {
            return sheet;
        }
        v8::Local<v8::Value> arguments[]{sheet};
        v8::Local<v8::Value> augmented;
        if (!raw_augment.As<v8::Function>()
                ->Call(local_context, global, 1, arguments)
                .ToLocal(&augmented)
            || !augmented->IsObject()) {
            return sheet;
        }
        return augmented.As<v8::Object>();
    }

    static void get_style_sheet(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* node = unwrap_node(info.Holder());
        if (self == nullptr || node == nullptr || !self->is_connected(*node)) {
            info.GetReturnValue().Set(v8::Null(info.GetIsolate()));
            return;
        }
        info.GetReturnValue().Set(self->style_sheet_for(*node));
    }

    static void get_shadow_style_sheets(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* root = unwrap_node(info.Holder());
        std::vector<dom_node*> styles;
        if (self != nullptr && root != nullptr && self->is_connected(*root)) {
            const auto collect = [&](const auto& visit, dom_node& parent) -> void {
                for (auto* child : parent.children) {
                    if (child == nullptr) continue;
                    if (child->kind == dom_node_kind::element && child->tag == "style") {
                        styles.push_back(child);
                    }
                    visit(visit, *child);
                }
            };
            collect(collect, *root);
        }
        const auto local_context = info.GetIsolate()->GetCurrentContext();
        auto result = v8::Array::New(
            info.GetIsolate(), static_cast<int>(styles.size()));
        for (uint32_t index = 0; index < styles.size(); ++index) {
            result->Set(
                local_context, index,
                self->style_sheet_for(*styles[index])).Check();
        }
        info.GetReturnValue().Set(result);
    }

    static void get_document_style_sheets(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* root = static_cast<dom_node*>(info.Holder()->GetAlignedPointerFromInternalField(
            0,
            v8::kEmbedderDataTypeTagDefault));
        if (root == nullptr) root = &self->active_root();
        std::vector<dom_node*> styles;
        const auto collect = [&](const auto& visit, dom_node& parent) -> void {
            for (auto* child : parent.children) {
                if (child == nullptr) continue;
                auto is_sheet = child->kind == dom_node_kind::element
                    && child->tag == "style";
                if (!is_sheet && child->kind == dom_node_kind::element
                    && child->tag == "link") {
                    const auto rel = child->attributes.find("rel");
                    if (rel != child->attributes.end()) {
                        const auto tokens = lower_html_name(rel->second);
                        size_t cursor = 0;
                        while (cursor < tokens.size()) {
                            while (cursor < tokens.size()
                                && std::isspace(static_cast<unsigned char>(tokens[cursor]))) {
                                ++cursor;
                            }
                            const auto begin = cursor;
                            while (cursor < tokens.size()
                                && !std::isspace(static_cast<unsigned char>(tokens[cursor]))) {
                                ++cursor;
                            }
                            if (tokens.substr(begin, cursor - begin) == "stylesheet") {
                                is_sheet = true;
                                break;
                            }
                        }
                    }
                }
                if (is_sheet) styles.push_back(child);
                visit(visit, *child);
            }
        };
        collect(collect, *root);
        const auto local_context = info.GetIsolate()->GetCurrentContext();
        auto result = v8::Array::New(
            info.GetIsolate(), static_cast<int>(styles.size()));
        for (uint32_t index = 0; index < styles.size(); ++index) {
            result->Set(
                local_context, index,
                self->style_sheet_for(*styles[index])).Check();
        }
        info.GetReturnValue().Set(result);
    }

    static void get_root_node(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* node = unwrap_node(info.This());
        if (self == nullptr || node == nullptr) return;
        auto composed = false;
        if (info.Length() > 0 && info[0]->IsObject()) {
            auto local_context = info.GetIsolate()->GetCurrentContext();
            v8::Local<v8::Value> value;
            composed = info[0].As<v8::Object>()->Get(
                    local_context, js_string(info.GetIsolate(), "composed"))
                    .ToLocal(&value)
                && value->BooleanValue(info.GetIsolate());
        }
        auto* root = node;
        while (root->parent != nullptr) {
            if (self->document.is_shadow_root(*root) && !composed) break;
            root = root->parent;
        }
        if (root == &self->active_root()) {
            auto local_context = info.GetIsolate()->GetCurrentContext();
            v8::Local<v8::Value> document_value;
            if (local_context->Global()->Get(
                    local_context, js_string(info.GetIsolate(), "document"))
                    .ToLocal(&document_value)) {
                info.GetReturnValue().Set(document_value);
                return;
            }
        }
        info.GetReturnValue().Set(self->wrap_node(*root));
    }

    void mark_cssom_layout_dirty(
        dom_node& node,
        std::string_view canonical_property_name)
    {
        if (canonical_property_name == "overscroll-behavior"
            || canonical_property_name == "overscroll-behavior-x"
            || canonical_property_name == "overscroll-behavior-y") {
            return;
        }
        // These properties affect painting or interaction, but never box
        // measurement or flow. Publish the scene without synchronously
        // traversing layout again.
        // Transforms are not flow-affecting, but CSSOM geometry observes
        // their painted bounds and therefore requires a fresh layout.
        constexpr std::array paint_only_properties{
            std::string_view{"background"},
            std::string_view{"background-color"},
            std::string_view{"border-radius"},
            std::string_view{"box-shadow"},
            std::string_view{"color"},
            std::string_view{"cursor"},
            std::string_view{"opacity"},
            std::string_view{"object-fit"},
            std::string_view{"object-position"},
            std::string_view{"user-select"},
            std::string_view{"isolation"},
            std::string_view{"will-change"},
            std::string_view{"pointer-events"},
            std::string_view{"visibility"},
            std::string_view{"-webkit-font-smoothing"}
        };
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        const auto force_layout_invalidation =
            std::getenv("WEBSCENE_FORCE_LAYOUT_INVALIDATION") != nullptr;
#else
        constexpr auto force_layout_invalidation = false;
#endif
        if (!force_layout_invalidation && std::find(
                paint_only_properties.begin(),
                paint_only_properties.end(),
                canonical_property_name) != paint_only_properties.end()) {
            document.mark_scene_changed();
            return;
        }
        const auto changes_only_positioned_subtree =
            canonical_property_name == "width"
            || canonical_property_name == "height"
            || canonical_property_name == "min-width"
            || canonical_property_name == "min-height"
            || canonical_property_name == "max-width"
            || canonical_property_name == "max-height"
            || canonical_property_name == "left"
            || canonical_property_name == "top"
            || canonical_property_name == "right"
            || canonical_property_name == "bottom";
        dom_node* isolated_geometry_root = nullptr;
        if (changes_only_positioned_subtree) {
            for (auto* current = &node; current != nullptr;
                 current = current->parent) {
                if (current->style.position != position_mode::absolute
                    && current->style.position != position_mode::fixed) {
                    continue;
                }
                isolated_geometry_root = current;
                break;
            }
        }
        if (isolated_geometry_root != nullptr) {
            // A descendant can resize an auto-sized positioned ancestor, but
            // that subtree remains outside normal flow. Keep its pending work
            // scoped so geometry reads elsewhere do not traverse the document.
            document.mark_out_of_flow_geometry_dirty(*isolated_geometry_root);
        } else {
            document.mark_dirty();
        }
    }

    css_cascade_state take_active_css_cascade()
    {
        css_cascade_state state;
        state.rules = std::move(css_rules);
        state.stylesheet_owner_id = active_stylesheet_owner_id;
        state.stylesheet_shadow_scope_root_id = active_stylesheet_shadow_scope_root_id;
        state.opacity_keyframes = std::move(opacity_keyframes);
        state.registered_custom_properties =
            std::move(registered_custom_properties);
        state.rules_by_class = std::move(css_rules_by_class);
        state.rules_by_id = std::move(css_rules_by_id);
        state.rules_by_tag = std::move(css_rules_by_tag);
        state.rules_by_attribute = std::move(css_rules_by_attribute);
        state.rules_by_variable_reference = std::move(css_rules_by_variable_reference);
        state.inheritance_candidate_rules =
            std::move(css_inheritance_candidate_rules);
        state.invalidation_rules_by_attribute =
            std::move(css_invalidation_rules_by_attribute);
        state.invalidation_rules_by_class = std::move(css_invalidation_rules_by_class);
        state.child_list_index = std::move(css_child_list_index);
        css_child_list_index.clear();
        state.focus_rules = std::move(css_focus_rules);
        state.unindexed_rules = std::move(unindexed_css_rules);
        state.attribute_dependencies = std::move(css_attribute_dependencies);
        state.descendant_attribute_dependencies =
            std::move(css_descendant_attribute_dependencies);
        state.hover_dependencies = std::move(hover_selector_dependencies);
        state.active_dependencies = std::move(active_selector_dependencies);
        state.variables = std::move(css_variables);
        state.important_variables = std::move(important_css_variables);
        state.cascade_layer_orders = std::move(css_cascade_layer_orders);
        state.stylesheet_layer_names = std::move(css_stylesheet_layer_names);
        state.next_cascade_layer_order = next_css_cascade_layer_order;
        next_css_cascade_layer_order = 1U;
        active_stylesheet_owner_id = 0U;
        active_stylesheet_shadow_scope_root_id = 0U;
        return state;
    }

    void restore_active_css_cascade(css_cascade_state state)
    {
        css_rules = std::move(state.rules);
        active_stylesheet_owner_id = state.stylesheet_owner_id;
        active_stylesheet_shadow_scope_root_id =
            state.stylesheet_shadow_scope_root_id;
        opacity_keyframes = std::move(state.opacity_keyframes);
        registered_custom_properties =
            std::move(state.registered_custom_properties);
        css_rules_by_class = std::move(state.rules_by_class);
        css_rules_by_id = std::move(state.rules_by_id);
        css_rules_by_tag = std::move(state.rules_by_tag);
        css_rules_by_attribute = std::move(state.rules_by_attribute);
        css_rules_by_variable_reference =
            std::move(state.rules_by_variable_reference);
        css_inheritance_candidate_rules =
            std::move(state.inheritance_candidate_rules);
        css_invalidation_rules_by_attribute =
            std::move(state.invalidation_rules_by_attribute);
        css_invalidation_rules_by_class =
            std::move(state.invalidation_rules_by_class);
        css_child_list_index = std::move(state.child_list_index);
        css_focus_rules = std::move(state.focus_rules);
        unindexed_css_rules = std::move(state.unindexed_rules);
        css_attribute_dependencies = std::move(state.attribute_dependencies);
        css_descendant_attribute_dependencies =
            std::move(state.descendant_attribute_dependencies);
        hover_selector_dependencies = std::move(state.hover_dependencies);
        active_selector_dependencies = std::move(state.active_dependencies);
        css_variables = std::move(state.variables);
        important_css_variables = std::move(state.important_variables);
        css_cascade_layer_orders = std::move(state.cascade_layer_orders);
        css_stylesheet_layer_names = std::move(state.stylesheet_layer_names);
        next_css_cascade_layer_order = state.next_cascade_layer_order;
    }

    void activate_css_cascade(dom_node* root)
    {
        if (root == nullptr) root = &document.body();
        if (active_css_cascade_root == nullptr) {
            active_css_cascade_root = &document.body();
        }
        if (active_css_cascade_root == root) return;

        auto incoming = inactive_css_cascades.extract(root);
        inactive_css_cascades.insert_or_assign(
            active_css_cascade_root,
            take_active_css_cascade());
        if (!incoming.empty()) {
            restore_active_css_cascade(std::move(incoming.mapped()));
        }
        active_css_cascade_root = root;
    }

    void activate_css_cascade(v8::Local<v8::Context> local_context)
    {
        if (local_context.IsEmpty()) {
            activate_css_cascade(&document.body());
            return;
        }
        activate_css_cascade(static_cast<dom_node*>(
            local_context->GetAlignedPointerFromEmbedderData(
                1,
                v8::kEmbedderDataTypeTagDefault)));
    }

    dom_node* css_cascade_root_for_node(dom_node& node)
    {
        // A same-origin caller can mutate a node from another browsing
        // context. V8's current context then identifies the caller, not the
        // node's stylesheet realm. Resolve the nearest registered cascade
        // root from the node's own ancestry before applying any recascade.
        for (auto* current = &node; current != nullptr; current = current->parent) {
            if (current == active_css_cascade_root
                || inactive_css_cascades.contains(current)) {
                return current;
            }
        }
        return const_cast<dom_node*>(active_css_cascade_root);
    }

    uint32_t cascade_layer_scope_id(uint32_t stylesheet_owner_id)
    {
        if (const auto adopted = adopted_stylesheet_owner_scopes.find(
                stylesheet_owner_id);
            adopted != adopted_stylesheet_owner_scopes.end()) {
            return adopted->second;
        }
        if (stylesheet_owner_id != 0U) {
            auto* owner = document.find_by_native_id(stylesheet_owner_id);
            if (owner != nullptr) {
                for (const auto& [frame_id, provisional_body] : provisional_frame_bodies) {
                    const auto* frame = document.find_by_native_id(frame_id);
                    auto* provisional_root = provisional_body;
                    while (provisional_root != nullptr
                        && provisional_root->parent != nullptr
                        && provisional_root->parent != frame) {
                        provisional_root = provisional_root->parent;
                    }
                    if (provisional_root != nullptr
                        && subtree_contains(*provisional_root, owner)) {
                        return provisional_root->id;
                    }
                }
                if (auto* root = css_cascade_root_for_node(*owner); root != nullptr) {
                    return root->id;
                }
            }
        }
        return active_css_cascade_root == nullptr
            ? document.body().id : active_css_cascade_root->id;
    }

    uint32_t allocate_adopted_stylesheet_owner_id()
    {
        while (next_adopted_stylesheet_owner_id != 0U) {
            const auto candidate = next_adopted_stylesheet_owner_id++;
            if (document.find_by_native_id(candidate) == nullptr
                && !adopted_stylesheet_owner_scopes.contains(candidate)) {
                return candidate;
            }
        }
        next_adopted_stylesheet_owner_id = 0xf0000000U;
        return allocate_adopted_stylesheet_owner_id();
    }

    void erase_adopted_stylesheet_rules(
        adopted_stylesheet_publication& publication)
    {
        std::unordered_set<uint32_t> owners;
        for (const auto& entry : publication.entries) owners.insert(entry.owner_id);
        std::erase_if(css_rules, [&](const auto& rule) {
            return owners.contains(rule.stylesheet_owner_id);
        });
        for (const auto& entry : publication.entries) {
            rebuild_cascade_layer_order_for_stylesheet(entry.owner_id, {});
            adopted_stylesheet_owner_scopes.erase(entry.owner_id);
        }
    }

    void append_adopted_stylesheet_rules(
        adopted_stylesheet_publication& publication)
    {
        for (auto& entry : publication.entries) {
            adopted_stylesheet_owner_scopes.insert_or_assign(
                entry.owner_id, publication.root_id);
            if (!entry.source.empty()) {
                add_stylesheet(
                    entry.source,
                    publication.base_address,
                    entry.owner_id,
                    publication.shadow_scope_root_id);
            }
        }
    }

    void reappend_adopted_stylesheets_for_cascade_root(uint32_t cascade_root_id)
    {
        std::vector<adopted_stylesheet_publication*> publications;
        for (auto& [root_id, publication] : adopted_stylesheet_publications) {
            static_cast<void>(root_id);
            if (publication.cascade_root_id == cascade_root_id) {
                publications.push_back(&publication);
            }
        }
        std::sort(publications.begin(), publications.end(), [](const auto* left, const auto* right) {
            return left->root_id < right->root_id;
        });
        for (auto* publication : publications) erase_adopted_stylesheet_rules(*publication);
        for (auto* publication : publications) append_adopted_stylesheet_rules(*publication);
    }

    static void publish_adopted_stylesheets(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* root = unwrap_node(info.This());
        if (self == nullptr || root == nullptr
            || (root != &self->document.body()
                && !self->document.is_shadow_root(*root))
            || info.Length() < 1 || !info[0]->IsArray()) {
            info.GetReturnValue().Set(v8::False(info.GetIsolate()));
            return;
        }
        constexpr auto maximum_sheets = uint32_t{1024U};
        constexpr auto maximum_bytes = size_t{32U * 1024U * 1024U};
        const auto input = info[0].As<v8::Array>();
        if (input->Length() > maximum_sheets) {
            info.GetReturnValue().Set(v8::False(info.GetIsolate()));
            return;
        }
        std::vector<std::string> sources;
        sources.reserve(input->Length());
        size_t bytes = 0U;
        const auto context = info.GetIsolate()->GetCurrentContext();
        for (uint32_t index = 0; index < input->Length(); ++index) {
            v8::Local<v8::Value> value;
            v8::Local<v8::String> string;
            if (!input->Get(context, index).ToLocal(&value)
                || !value->ToString(context).ToLocal(&string)) {
                return;
            }
            auto source = to_wtf8(info.GetIsolate(), string);
            if (source.size() > maximum_bytes - std::min(bytes, maximum_bytes)) {
                info.GetReturnValue().Set(v8::False(info.GetIsolate()));
                return;
            }
            bytes += source.size();
            sources.push_back(std::move(source));
        }

        self->flush_pending_stylesheet_replacements();
        auto* cascade_root = self->css_cascade_root_for_node(*root);
        if (cascade_root == nullptr) {
            info.GetReturnValue().Set(v8::False(info.GetIsolate()));
            return;
        }
        auto* previous_root = const_cast<dom_node*>(self->active_css_cascade_root);
        self->activate_css_cascade(cascade_root);
        auto& publication = self->adopted_stylesheet_publications[root->id];
        self->erase_adopted_stylesheet_rules(publication);
        publication.root_id = root->id;
        publication.cascade_root_id = cascade_root->id;
        publication.shadow_scope_root_id = self->document.is_shadow_root(*root)
            ? root->id : 0U;
        publication.base_address = self->effective_document_base(cascade_root);
        while (publication.entries.size() < sources.size()) {
            publication.entries.push_back({
                self->allocate_adopted_stylesheet_owner_id(), {}});
        }
        publication.entries.resize(sources.size());
        for (size_t index = 0; index < sources.size(); ++index) {
            publication.entries[index].source = std::move(sources[index]);
        }
        if (publication.entries.empty()) {
            self->adopted_stylesheet_publications.erase(root->id);
        } else {
            self->append_adopted_stylesheet_rules(publication);
        }
        self->rebuild_css_rule_indexes_and_root_variables();
        self->schedule_style_recascade(*cascade_root, true, "adopted-stylesheet-change");
        self->document.mark_dirty();
        if (previous_root != nullptr && previous_root != cascade_root) {
            self->activate_css_cascade(previous_root);
        }
        info.GetReturnValue().Set(v8::True(info.GetIsolate()));
    }

    std::string cascade_layer_registration_key(
        std::string_view name, uint32_t stylesheet_owner_id)
    {
        return std::to_string(cascade_layer_scope_id(stylesheet_owner_id))
            + ':' + std::string(name);
    }

    void register_document_base(
        dom_node& root,
        std::string navigation_address,
        std::string effective_address)
    {
        document_base_caches.insert_or_assign(
            &root,
            document_base_cache{
                std::move(navigation_address),
                effective_address,
                std::move(effective_address),
                false});
    }

    void invalidate_document_base_root(dom_node* root)
    {
        if (root == nullptr) root = &document.body();
        auto& cache = document_base_caches[root];
        if (cache.navigation_address.empty()) {
            cache.navigation_address = frame_base_addresses.contains(root)
                ? frame_base_addresses[root]
                : document_base_address;
            cache.fallback_address = cache.navigation_address;
        }
        cache.dirty = true;
        recascade_local_link_document_transition(root);
    }

    void invalidate_document_base(dom_node& node)
    {
        invalidate_document_base_root(css_cascade_root_for_node(node));
    }

    bool subtree_contains_base(const dom_node& node) const
    {
        if (node.tag == "base") return true;
        return std::any_of(node.children.begin(), node.children.end(), [&](const auto* child) {
            return child != nullptr && subtree_contains_base(*child);
        });
    }

    const std::string& effective_document_base(const dom_node* root)
    {
        if (root == nullptr) root = &document.body();
        auto [found, inserted] = document_base_caches.try_emplace(root);
        auto& cache = found->second;
        if (inserted || cache.navigation_address.empty()) {
            cache.navigation_address = frame_base_addresses.contains(root)
                ? frame_base_addresses[root]
                : document_base_address;
            cache.fallback_address = cache.navigation_address;
            cache.effective_address = cache.navigation_address;
            cache.dirty = true;
        }
        if (!cache.dirty) return cache.effective_address;
        cache.effective_address = cache.fallback_address;
        for (auto* candidate : document.query_selector_all(*root, "base")) {
            if (candidate == nullptr || !is_connected(*candidate)) continue;
            const auto href = candidate->attributes.find("href");
            if (href == candidate->attributes.end() || href->second.empty()) continue;
            cache.effective_address = resolve_resource_url(
                href->second,
                cache.navigation_address);
            break;
        }
        cache.dirty = false;
        if (frame_base_addresses.contains(root))
            frame_base_addresses[root] = cache.effective_address;
        return cache.effective_address;
    }

    const std::string& navigation_address_for_root(dom_node* root)
    {
        if (root == nullptr || root == &document.body()) return document_base_address;
        const auto found = document_base_caches.find(root);
        return found == document_base_caches.end()
            ? document_base_address
            : found->second.navigation_address;
    }

    const std::string& current_navigation_address()
    {
        dom_node* root = nullptr;
        const auto local_context = isolate->GetCurrentContext();
        if (!local_context.IsEmpty()) {
            root = static_cast<dom_node*>(
                local_context->GetAlignedPointerFromEmbedderData(
                    1, v8::kEmbedderDataTypeTagDefault));
        }
        return navigation_address_for_root(root);
    }

    dom_node* active_nested_frame_for_context(
        v8::Local<v8::Context> local_context)
    {
        if (local_context.IsEmpty() || context.IsEmpty()
            || local_context == context.Get(isolate)) return nullptr;
        for (const auto& [frame_id, frame_context_value] : actual_frame_contexts) {
            if (frame_context_value.IsEmpty()
                || frame_context_value.Get(isolate) != local_context) continue;
            auto* frame = document.find_by_native_id(frame_id);
            return frame != nullptr && frame->tag == "iframe"
                    && frame->parent != nullptr
                ? frame
                : nullptr;
        }
        auto* root = static_cast<dom_node*>(
            local_context->GetAlignedPointerFromEmbedderData(
                1, v8::kEmbedderDataTypeTagDefault));
        if (root == nullptr) return nullptr;
        dom_node* frame = nullptr;
        for (auto* ancestor = root->parent;
             ancestor != nullptr; ancestor = ancestor->parent) {
            if (ancestor->tag == "iframe") {
                frame = ancestor;
                break;
            }
        }
        if (frame == nullptr || frame->parent == nullptr) return nullptr;
        const auto active = actual_frame_contexts.find(frame->id);
        return active != actual_frame_contexts.end()
                && !active->second.IsEmpty()
                && active->second.Get(isolate) == local_context
            ? frame
            : nullptr;
    }

    v8::Local<v8::Context> context_for_node(const dom_node& node)
    {
        for (auto* ancestor = node.parent; ancestor != nullptr; ancestor = ancestor->parent) {
            if (ancestor->tag != "iframe") continue;
            const auto owner = actual_frame_contexts.find(ancestor->id);
            if (owner != actual_frame_contexts.end() && !owner->second.IsEmpty()) {
                return owner->second.Get(isolate);
            }
        }
        return context.Get(isolate);
    }

    dom_node& active_root()
    {
        auto local_context = isolate->GetCurrentContext();
        auto* root = static_cast<dom_node*>(local_context->GetAlignedPointerFromEmbedderData(
            1,
            v8::kEmbedderDataTypeTagDefault));
        if (root == nullptr) root = &document.body();
        activate_css_cascade(root);
        return *root;
    }

    static dom_node& exposed_body_for_root(dom_node& root)
    {
        auto* html = root.tag == "html" ? &root : nullptr;
        if (html == nullptr) {
            const auto found = std::find_if(
                root.children.begin(),
                root.children.end(),
                [](const auto* child) { return child != nullptr && child->tag == "html"; });
            if (found != root.children.end()) html = *found;
        }
        if (html != nullptr) {
            const auto found = std::find_if(
                html->children.begin(),
                html->children.end(),
                [](const auto* child) { return child != nullptr && child->tag == "body"; });
            if (found != html->children.end()) return **found;
        }
        return root;
    }

    bool in_frame_context() const
    {
        if (context.IsEmpty()) return false;
        const auto current_context = isolate->GetCurrentContext();
        return !current_context.IsEmpty()
            && current_context != context.Get(isolate);
    }

    const std::string& current_base_address()
    {
        const auto current_context = isolate->GetCurrentContext();
        if (!current_context.IsEmpty()) {
            const auto* root = static_cast<const dom_node*>(
                current_context->GetAlignedPointerFromEmbedderData(
                    1,
                    v8::kEmbedderDataTypeTagDefault));
            if (root != nullptr) return effective_document_base(root);
        }
        return effective_document_base(&document.body());
    }

    bool is_document_object(v8::Local<v8::Object> candidate) const
    {
        if (!document_object.IsEmpty()
            && candidate->StrictEquals(document_object.Get(isolate))) return true;
        for (const auto& [_, document_value] : frame_documents) {
            if (!document_value.IsEmpty()
                && candidate->StrictEquals(document_value.Get(isolate))) return true;
        }
        for (const auto& [_, document_value] : actual_frame_documents) {
            if (!document_value.IsEmpty()
                && candidate->StrictEquals(document_value.Get(isolate))) return true;
        }
        return false;
    }

    uint64_t wrapper_key(const dom_node& node) const
    {
        return static_cast<uint64_t>(node.id) | (in_frame_context() ? (1ULL << 63U) : 0ULL);
    }

    template <typename Callback>
    void visit_subtree(dom_node& root, Callback&& callback) const
    {
        callback(root);
        for (auto* child : root.children) {
            if (child != nullptr) visit_subtree(*child, callback);
        }
        const auto* shadow = document.shadow_dom(root);
        if (shadow != nullptr && shadow->root != nullptr) {
            visit_subtree(*shadow->root, callback);
        }
    }

    // Navigation task cleanup is required even when Inspector is omitted.
    void cancel_detached_frame_context_tasks(
        v8::Local<v8::Context> local_context)
    {
        cancel_native_host_promises_for_context(
            local_context,
            "The frame navigated before the native host completed the request");
        cancel_crypto_tasks_for_context(local_context);
        std::erase_if(pending_fetches, [&](auto& fetch) {
            if (fetch.context.Get(isolate) != local_context) return false;
            if (fetch.cancel) fetch.cancel();
            return true;
        });
        uint64_t cancelled_animation_frames = 0;
        const auto cancelled_timers = std::erase_if(
            timers,
            [&](const auto& timer) {
                if (timer.context.Get(isolate) != local_context) return false;
                if (timer.animation_frame) ++cancelled_animation_frames;
#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8_INSPECTOR)
                inspector_async_task_canceled(timer.id);
#endif
                return true;
            });
        if (work_metrics_enabled.load(std::memory_order_relaxed)) {
            work.animation_frames_cancelled += cancelled_animation_frames;
            work.timers_cancelled +=
                cancelled_timers - cancelled_animation_frames;
        }

        // A frame callback can synchronously remove its own iframe. Prevent a
        // repeating task that is currently outside `timers` from being queued
        // again after the callback returns.
        if (active_timer_id != 0U
            && isolate->GetCurrentContext() == local_context) {
            active_timer_cancelled = true;
        }

        std::erase_if(
            pending_window_messages,
            [&](const auto& task) {
                if (task.target_context.Get(isolate) != local_context) return false;
                const auto bytes = clone_packet_size(task.packet);
                pending_window_message_bytes = bytes > pending_window_message_bytes
                    ? 0U : pending_window_message_bytes - bytes;
                return true;
            });
        for (auto& binding : message_port_bindings) {
            if (binding != nullptr
                && binding->context.Get(isolate) == local_context) {
                close_message_port_binding(*binding);
            }
        }
        retire_broadcast_channels_for_context(local_context);
        retire_abort_signal_compositions_for_context(local_context);
        std::erase_if(
            pending_dialog_close_events,
            [&](const auto& task) {
                return task.context.Get(isolate) == local_context;
            });
        std::erase_if(
            pending_details_toggle_events,
            [&](const auto& task) {
                return task.context.Get(isolate) == local_context;
            });
        std::erase_if(
            pending_programmatic_scroll_events,
            [&](const auto& task) {
                return task.context.Get(isolate) == local_context;
            });
        std::erase_if(
            connected_resources,
            [&](const auto& task) {
                return task.context.Get(isolate) == local_context;
            });
        std::erase_if(
            resize_observers,
            [&](const auto& observer) {
                return observer != nullptr
                    && observer->context.Get(isolate) == local_context;
            });
        for (auto& [type, listeners] : frame_event_listeners) {
            static_cast<void>(type);
            std::erase_if(listeners, [&](auto& listener) {
                if (listener.context.Get(isolate) != local_context) return false;
                clear_event_listener(listener);
                return true;
            });
        }
        for (auto iterator = websocket_bindings.begin();
             iterator != websocket_bindings.end();) {
            if (iterator->second.context.Get(isolate) != local_context) {
                ++iterator;
                continue;
            }
            static_cast<void>(websocket_transport.close(
                iterator->first,
                1001U,
                "Frame browsing context was discarded"));
            websocket_transport.release(iterator->first);
            iterator = websocket_bindings.erase(iterator);
        }
    }

    void destroy_detached_frame_contexts(dom_node& root)
    {
        std::unordered_set<uint32_t> node_ids;
        std::vector<dom_node*> detached_frames;
        std::vector<v8::Local<v8::Context>> detached_contexts;
        visit_subtree(root, [&](dom_node& node) {
            node_ids.insert(node.id);
            if (node.tag == "iframe") detached_frames.push_back(&node);
        });
        // Descendant documents unload before their owner document. The HTML
        // standard leaves sibling order unspecified, so reverse tree order is
        // sufficient while still making every descendant precede its owner.
        for (auto frame = detached_frames.rbegin();
             frame != detached_frames.rend(); ++frame) {
            dispatch_iframe_unload(**frame);
        }
        for (const auto id : node_ids) {
            const auto found = actual_frame_contexts.find(id);
            if (found != actual_frame_contexts.end()
                && !found->second.IsEmpty()) {
                const auto local_context = found->second.Get(isolate);
                const auto* frame_root = static_cast<const dom_node*>(
                    local_context->GetAlignedPointerFromEmbedderData(
                        1,
                        v8::kEmbedderDataTypeTagDefault));
                frame_base_addresses.erase(frame_root);
                document_base_caches.erase(frame_root);
                local_link_selector_subjects.erase(frame_root);
                selector_target_caches.erase(frame_root);
                if (active_css_cascade_root == frame_root) {
                    activate_css_cascade(&document.body());
                }
                inactive_css_cascades.erase(frame_root);
                detached_contexts.push_back(local_context);
                cancel_detached_frame_context_tasks(local_context);
#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8_INSPECTOR)
                destroy_inspector_context(local_context);
#endif
                if (!frame_context.IsEmpty()
                    && frame_context.Get(isolate) == local_context) {
                    frame_context.Reset();
                    frame_base_address.clear();
                }
            }
            actual_frame_contexts.erase(id);
            frame_hydration_states.erase(id);
            retire_iframe_navigation(id);
            cross_origin_frame_locations.erase(id);
            frame_security_origins.erase(id);
            frame_beforeunload_dispatches.erase(id);
            frame_pagehide_dispatched.erase(id);
            frame_unload_dispatched.erase(id);
            actual_frame_windows.erase(id);
            actual_frame_documents.erase(id);
            frame_session_storage.erase(id);
            frame_local_storage.erase(id);
            frame_load_listeners.erase(id);
            frame_dom_content_loaded_listeners.erase(id);
        }
        for (auto& [type, listeners] : frame_event_listeners) {
            static_cast<void>(type);
            std::erase_if(listeners, [&](auto& listener) {
                const auto listener_context = listener.context.Get(isolate);
                const auto detached = std::any_of(
                    detached_contexts.begin(),
                    detached_contexts.end(),
                    [&](const auto context) {
                        return listener_context == context;
                    });
                if (detached) clear_event_listener(listener);
                return detached;
            });
        }
        retire_service_worker_clients(detached_contexts);
    }

    void weaken_detached_subtree_wrappers(dom_node& root)
    {
        // Detaching an iframe discards its browsing context immediately. DOM
        // wrappers may remain alive for JavaScript references, but Inspector
        // execution contexts and remote objects must not survive until the
        // detached native subtree is eventually reclaimed.
        // A persistent V8 handle is a GC root. Keeping every historical DOM
        // wrapper strong therefore retains React's expando graph forever after
        // the corresponding native node is removed. Model a detached DOM tree
        // as one weakly rooted object graph: any JavaScript reference to any
        // wrapper keeps the complete tree alive, while an unreachable tree can
        // be reclaimed by V8 as a cycle.
        for (const auto frame_realm : {false, true}) {
            if (frame_realm && frame_context.IsEmpty()) continue;
            auto local_context = frame_realm ? frame_context.Get(isolate) : context.Get(isolate);
            if (local_context.IsEmpty()) continue;
            v8::Context::Scope context_scope(local_context);
            std::vector<v8::Local<v8::Object>> wrappers;
            const auto collect = [&](auto& cache, uint64_t key) {
                const auto known = cache.find(key);
                if (known == cache.end() || known->second.IsEmpty()) return;
                wrappers.push_back(known->second.Get(isolate));
            };
            visit_subtree(root, [&](dom_node& node) {
                const auto key = static_cast<uint64_t>(node.id)
                    | (frame_realm ? (1ULL << 63U) : 0ULL);
                collect(node_wrappers, key);
                collect(class_list_wrappers, key);
                collect(sandbox_token_list_wrappers, key);
                collect(style_wrappers, key);
                collect(stylesheet_wrappers, key);
                collect(computed_style_wrappers, key);
                collect(canvas_contexts, key);
            });
            if (wrappers.empty()) continue;

            auto anchor = v8::Object::New(isolate);
            auto members = v8::Array::New(isolate, static_cast<int>(wrappers.size()));
            for (size_t index = 0; index < wrappers.size(); ++index) {
                members->Set(
                    local_context,
                    static_cast<uint32_t>(index),
                    wrappers[index]).Check();
            }
            anchor->Set(
                local_context,
                js_string(isolate, "members"),
                members).Check();
            const auto anchor_key = v8::Private::ForApi(
                isolate,
                js_string(isolate, "webscene.detached-dom-anchor"));
            for (auto wrapper : wrappers) {
                wrapper->SetPrivate(local_context, anchor_key, anchor).Check();
            }

            const auto weaken = [](auto& cache, uint64_t key) {
                const auto known = cache.find(key);
                if (known != cache.end() && !known->second.IsEmpty()
                    && !known->second.IsWeak()) {
                    known->second.SetWeak();
                }
            };
            visit_subtree(root, [&](dom_node& node) {
                const auto key = static_cast<uint64_t>(node.id)
                    | (frame_realm ? (1ULL << 63U) : 0ULL);
                weaken(node_wrappers, key);
                weaken(class_list_wrappers, key);
                weaken(sandbox_token_list_wrappers, key);
                weaken(style_wrappers, key);
                weaken(stylesheet_wrappers, key);
                weaken(computed_style_wrappers, key);
                weaken(canvas_contexts, key);
            });
        }
        destroy_detached_frame_contexts(root);
        register_detached_subtree(root);
    }

    bool root_has_weak_wrapper(const dom_node& root) const
    {
        const auto weak = [](const auto& cache, uint64_t key) {
            const auto known = cache.find(key);
            return known != cache.end()
                && !known->second.IsEmpty()
                && known->second.IsWeak();
        };
        for (const auto frame_realm : {false, true}) {
            const auto key = static_cast<uint64_t>(root.id)
                | (frame_realm ? (1ULL << 63U) : 0ULL);
            if (weak(node_wrappers, key)
                || weak(class_list_wrappers, key)
                || weak(sandbox_token_list_wrappers, key)
                || weak(style_wrappers, key)
                || weak(stylesheet_wrappers, key)
                || weak(computed_style_wrappers, key)
                || weak(canvas_contexts, key)) {
                return true;
            }
        }
        return false;
    }

    bool retain_reconnected_subtree_wrappers(dom_node& root)
    {
        if (!is_connected(root)) return false;

        // A style recascade is also used to initialize newly inserted DOM, so
        // it is the common connection boundary for every mutation API. Most
        // recascades, however, are ordinary class/style changes on an already
        // connected tree. Do not walk that tree unless it contains a root that
        // was actually weakened while detached (or its own wrapper is weak
        // because it was extracted from a tracked detached ancestor).
        auto reconnects_tracked_root = root_has_weak_wrapper(root);
        if (!reconnects_tracked_root) {
            for (const auto root_id : detached_dom_roots) {
                const auto* detached_root = document.find_by_native_id(root_id);
                if (detached_root != nullptr
                    && (detached_root == &root
                        || subtree_contains(root, detached_root))) {
                    reconnects_tracked_root = true;
                    break;
                }
            }
        }
        if (!reconnects_tracked_root) return false;

#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        ++reconnected_wrapper_retention_subtrees;
#endif
        for (const auto frame_realm : {false, true}) {
            if (frame_realm && frame_context.IsEmpty()) continue;
            auto local_context = frame_realm ? frame_context.Get(isolate) : context.Get(isolate);
            if (local_context.IsEmpty()) continue;
            v8::Context::Scope context_scope(local_context);
            const auto anchor_key = v8::Private::ForApi(
                isolate,
                js_string(isolate, "webscene.detached-dom-anchor"));
            const auto retain = [&](auto& cache, uint64_t key) {
                const auto known = cache.find(key);
                if (known == cache.end() || known->second.IsEmpty()) return;
                if (known->second.IsWeak()) known->second.ClearWeak();
                known->second.Get(isolate)->DeletePrivate(local_context, anchor_key).Check();
            };
            visit_subtree(root, [&](dom_node& node) {
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
                if (!frame_realm) ++reconnected_wrapper_retention_nodes;
#endif
                const auto key = static_cast<uint64_t>(node.id)
                    | (frame_realm ? (1ULL << 63U) : 0ULL);
                retain(node_wrappers, key);
                retain(class_list_wrappers, key);
                retain(sandbox_token_list_wrappers, key);
                retain(style_wrappers, key);
                retain(stylesheet_wrappers, key);
                retain(computed_style_wrappers, key);
                retain(canvas_contexts, key);
            });
        }
        std::erase_if(detached_dom_roots, [&](const auto root_id) {
            const auto* candidate = document.find_by_native_id(root_id);
            return candidate == nullptr
                || (is_connected(*candidate)
                    && (candidate == &root || subtree_contains(root, candidate)));
        });
        detached_dom_root_ids.clear();
        detached_dom_root_ids.insert(
            detached_dom_roots.begin(), detached_dom_roots.end());
        return true;
    }

    class custom_element_reaction_scope final {
        implementation& owner_;
        custom_element_reaction_scope* previous_;
        v8::TryCatch operation_exception_;
        v8::Local<v8::Context> context_;
        v8::Local<v8::Function> end_;
    public:
        explicit custom_element_reaction_scope(implementation& owner)
            : owner_(owner), previous_(owner.active_custom_element_reaction_scope),
              operation_exception_(owner.isolate)
        {
            owner_.active_custom_element_reaction_scope = this;
            ensure_started();
        }
        void ensure_started()
        {
            if (!owner_.custom_elements_active || !end_.IsEmpty()) return;
            // The first registry definition may happen during argument
            // conversion. Lazily start every enclosing operation then, while
            // cold documents make no JS reaction calls at all.
            if (previous_ != nullptr) previous_->ensure_started();
            auto* isolate = owner_.isolate;
            context_ = isolate->GetCurrentContext();
            v8::Local<v8::Value> begin, end;
            if (!context_->Global()->Get(context_, js_string(isolate,
                    "__webSceneCustomElementsBeginReactions")).ToLocal(&begin)
                || !begin->IsFunction()
                || !context_->Global()->Get(context_, js_string(isolate,
                    "__webSceneCustomElementsEndReactions")).ToLocal(&end)
                || !end->IsFunction()) return;
            v8::Local<v8::Value> result;
            if (begin.As<v8::Function>()->Call(context_, context_->Global(),
                    0, nullptr).ToLocal(&result)) end_ = end.As<v8::Function>();
        }
        ~custom_element_reaction_scope()
        {
            owner_.active_custom_element_reaction_scope = previous_;
            // A rejected native operation must still pop its reaction queue,
            // without swallowing the exception or entering JS with it pending.
            auto exception = operation_exception_.Exception();
            const bool failed = operation_exception_.HasCaught();
            if (failed) operation_exception_.Reset();
            if (!end_.IsEmpty()) {
                v8::TryCatch try_catch(owner_.isolate);
                static_cast<void>(end_->Call(context_, context_->Global(), 0, nullptr));
                if (try_catch.HasCaught()) owner_.last_error =
                    "Custom element reaction checkpoint failed: "
                    + owner_.describe_reported_exception(try_catch, context_);
            }
            if (failed && !exception.IsEmpty()) {
                owner_.isolate->ThrowException(exception);
                static_cast<void>(operation_exception_.ReThrow());
            }
        }
    };

    void notify_custom_elements_subtree(dom_node& root, const char* phase)
    {
        if (!custom_elements_active) return;
        if (active_custom_element_reaction_scope != nullptr)
            active_custom_element_reaction_scope->ensure_started();
        auto local_context = isolate->GetCurrentContext();
        v8::Local<v8::Value> raw_callback;
        if (!local_context->Global()->Get(
                local_context,
                js_string(isolate, "__webSceneCustomElementsNotifySubtree"))
                .ToLocal(&raw_callback)
            || !raw_callback->IsFunction()) {
            return;
        }
        v8::TryCatch try_catch(isolate);
        std::vector<dom_node*> reaction_roots{&root};
        visit_subtree(root, [&](dom_node& node) {
            if (&node != &root && document.is_shadow_root(node)) {
                reaction_roots.push_back(&node);
            }
        });
        for (auto* reaction_root : reaction_roots) {
            v8::Local<v8::Value> arguments[]{
                wrap_node(*reaction_root),
                js_string(isolate, phase)
            };
            static_cast<void>(raw_callback.As<v8::Function>()->Call(
                local_context,
                local_context->Global(),
                2,
                arguments));
            if (try_catch.HasCaught()) break;
        }
        if (try_catch.HasCaught()) {
            last_error = "Custom element subtree reaction failed: "
                + describe_reported_exception(try_catch, local_context);
        }
    }

    void notify_custom_element_attribute(
        dom_node& element,
        const std::string& name,
        const std::optional<std::string>& old_value,
        const std::optional<std::string>& new_value,
        const std::optional<std::string>& namespace_uri = std::nullopt)
    {
        if (!custom_elements_active) return;
        if (active_custom_element_reaction_scope != nullptr)
            active_custom_element_reaction_scope->ensure_started();
        auto local_context = isolate->GetCurrentContext();
        v8::Local<v8::Value> raw_callback;
        if (!local_context->Global()->Get(
                local_context,
                js_string(isolate, "__webSceneCustomElementsNotifyAttribute"))
                .ToLocal(&raw_callback)
            || !raw_callback->IsFunction()) {
            return;
        }
        const auto dom_value = [&](const std::optional<std::string>& value) {
            return value.has_value()
                ? v8::Local<v8::Value>(js_dom_string(isolate, *value))
                : v8::Local<v8::Value>(v8::Null(isolate));
        };
        v8::Local<v8::Value> arguments[]{
            wrap_node(element),
            js_dom_string(isolate, name),
            dom_value(old_value),
            dom_value(new_value),
            dom_value(namespace_uri)
        };
        v8::TryCatch try_catch(isolate);
        static_cast<void>(raw_callback.As<v8::Function>()->Call(
            local_context,
            local_context->Global(),
            5,
            arguments));
        if (try_catch.HasCaught()) {
            last_error = "Custom element attribute reaction failed: "
                + describe_reported_exception(try_catch, local_context);
        }
    }

    void detach_all_children(dom_node& parent)
    {
        auto children = parent.children;
        auto stylesheets_changed = false;
        for (auto* child : children) {
            if (child != nullptr) {
              clear_selections_containing(*child);
              if (is_connected(*child)) {
                notify_custom_elements_subtree(*child, "disconnected");
              }
                stylesheets_changed |= deactivate_connected_stylesheets(*child, false);
            }
        }
        document.remove_all_children(parent);
        for (auto* child : children) {
            if (child == nullptr) continue;
            update_live_form_collection_structure(parent, nullptr, child);
            weaken_detached_subtree_wrappers(*child);
        }
        synchronize_textarea_value_from_children(parent);
        if (stylesheets_changed) recascade_after_stylesheet_change();
    }

    void retire_provisional_frame_document(dom_node& frame)
    {
        const auto known_body = provisional_frame_bodies.find(frame.id);
        if (known_body == provisional_frame_bodies.end()
            || known_body->second == nullptr) {
            return;
        }

        auto* root = known_body->second;
        while (root->parent != nullptr && root->parent != &frame) {
            root = root->parent;
        }
        if (root->parent != &frame) {
            provisional_frame_bodies.erase(known_body);
            return;
        }
        // Accessing contentDocument/contentWindow creates the iframe's initial
        // about:blank document synchronously. Navigation replaces that
        // document; it must not remain as a rendered sibling in front of the
        // loaded document. WebScene retains both browsing-context documents in
        // one native tree so existing JavaScript references stay valid. Retire
        // the old root from layout and paint while leaving normal iframe
        // teardown responsible for reclaiming the complete browsing context.
        root->mutable_authored_style().declarations["display"] = "none";
        root->style.inline_property_mask |= inline_display;
        root->style.display = display_mode::none;
        root->visible = false;
        static_cast<void>(deactivate_connected_stylesheets(*root, false));
        provisional_frame_bodies.erase(known_body);
        document.mark_dirty();
    }

    void register_detached_subtree(dom_node& root)
    {
        if (&root == &document.body() || root.parent != nullptr) return;
        document.reset_detached_style_state(root);
        // Detached leaves are the common text/topology case. A leaf cannot
        // contain an existing root and, after unlinking, cannot still be
        // contained by one. Index it directly so repeated split/normalize
        // cycles do not compare every new leaf with every historical leaf.
        if (root.children.empty()) {
            if (!detached_dom_root_ids.insert(root.id).second) return;
            detached_dom_roots.push_back(root.id);
            ++detached_nodes_since_gc;
            if (detached_nodes_since_gc >= detached_dom_gc_threshold) {
                if (!detached_dom_gc_requested
                    && detached_dom_gc_scan_index >= detached_dom_gc_scan_roots.size()) {
                    detached_dom_gc_retry_count = 0U;
                }
                detached_dom_gc_requested = true;
            }
            return;
        }
        if (detached_dom_root_ids.contains(root.id)) return;
        for (const auto known_id : detached_dom_roots) {
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
            ++detached_dom_root_containment_checks;
#endif
            const auto* known = document.find_by_native_id(known_id);
            if (known != nullptr && subtree_contains(*known, &root)) return;
        }
        std::vector<uint32_t> retained_roots;
        retained_roots.reserve(detached_dom_roots.size() + 1U);
        for (const auto known_id : detached_dom_roots) {
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
            ++detached_dom_root_containment_checks;
#endif
            const auto* known = document.find_by_native_id(known_id);
            if (known != nullptr && !subtree_contains(root, known)) {
                retained_roots.push_back(known_id);
            }
        }
        size_t count = 0U;
        visit_subtree(root, [&](const dom_node&) { ++count; });
        retained_roots.push_back(root.id);
        detached_dom_roots = std::move(retained_roots);
        detached_dom_root_ids.clear();
        detached_dom_root_ids.insert(
            detached_dom_roots.begin(), detached_dom_roots.end());
        detached_nodes_since_gc += count;
        if (detached_nodes_since_gc >= detached_dom_gc_threshold) {
            if (!detached_dom_gc_requested
                && detached_dom_gc_scan_index
                    >= detached_dom_gc_scan_roots.size()) {
                detached_dom_gc_retry_count = 0U;
            }
            detached_dom_gc_requested = true;
        }
    }

    bool subtree_has_live_wrappers(dom_node& root) const
    {
        auto live = false;
        const auto inspect = [&](const auto& cache, uint64_t key) {
            const auto known = cache.find(key);
            return known != cache.end() && !known->second.IsEmpty();
        };
        visit_subtree(root, [&](dom_node& node) {
            for (const auto frame_realm : {false, true}) {
                const auto key = static_cast<uint64_t>(node.id)
                    | (frame_realm ? (1ULL << 63U) : 0ULL);
                live = live
                    || inspect(node_wrappers, key)
                    || inspect(detached_documents, node.id)
                    || inspect(class_list_wrappers, key)
                    || inspect(sandbox_token_list_wrappers, key)
                    || inspect(style_wrappers, key)
                    || inspect(stylesheet_wrappers, key)
                    || inspect(computed_style_wrappers, key)
                    || inspect(canvas_contexts, key);
            }
        });
        return live;
    }

    void release_detached_subtrees(const std::vector<dom_node*>& roots)
    {
        std::unordered_set<dom_node*> nodes;
        std::unordered_set<uint32_t> node_ids;
        for (auto* root : roots) {
            if (root == nullptr) continue;
            visit_subtree(*root, [&](dom_node& node) {
                nodes.insert(&node);
                node_ids.insert(node.id);
            });
        }
        if (nodes.empty()) return;
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        ++detached_dom_release_batches;
        detached_dom_release_roots += roots.size();
#endif
        const auto erase_wrappers = [&](auto& cache) {
            std::erase_if(cache, [&](const auto& entry) {
                return node_ids.contains(static_cast<uint32_t>(entry.first));
            });
        };
        erase_wrappers(node_wrappers);
        erase_wrappers(class_list_wrappers);
        erase_wrappers(sandbox_token_list_wrappers);
        erase_wrappers(style_wrappers);
        erase_wrappers(stylesheet_wrappers);
        erase_wrappers(computed_style_wrappers);
        erase_wrappers(canvas_contexts);
        std::erase_if(custom_states, [&](const auto& entry) {
            return node_ids.contains(entry.first);
        });
        for (auto* node : nodes) remove_live_form_control(*node);
        std::erase_if(live_form_controls, [&](const auto& entry) {
            return nodes.contains(entry.first);
        });
        std::erase_if(live_form_owner_by_control, [&](const auto& entry) {
            return nodes.contains(entry.first) || nodes.contains(entry.second);
        });
        const auto erase_released_index_nodes = [&](auto& index) {
            std::erase_if(index, [&](auto& entry) {
                std::erase_if(entry.second, [&](const auto* node) {
                    return nodes.contains(const_cast<dom_node*>(node));
                });
                return entry.second.empty();
            });
        };
        erase_released_index_nodes(explicit_form_validity_dependents);
        erase_released_index_nodes(form_validity_owners_by_id);
        erase_released_index_nodes(label_targets_by_id);
        erase_released_index_nodes(explicit_labels_by_for);
        std::erase_if(detached_documents, [&](const auto& entry) {
            return node_ids.contains(static_cast<uint32_t>(entry.first));
        });
        std::erase_if(detached_document_roots, [&](const auto id) {
            return node_ids.contains(static_cast<uint32_t>(id));
        });
        std::erase_if(hidden_frame_document_roots, [&](const auto id) {
            return node_ids.contains(id);
        });
        for (const auto id : node_ids) {
            erase_pending_stylesheet_replacement(id);
            stylesheet_cssom_sources.erase(id);
            stylesheet_cssom_addresses.erase(id);
            if (auto adopted = adopted_stylesheet_publications.find(id);
                adopted != adopted_stylesheet_publications.end()) {
                erase_adopted_stylesheet_rules(adopted->second);
                adopted_stylesheet_publications.erase(adopted);
            }
        }
        std::erase_if(canvas_states, [&](const auto& entry) {
            return node_ids.contains(static_cast<uint32_t>(entry.first));
        });
        std::erase_if(pending_frame_hydrations, [&](const auto* node) {
            return nodes.contains(const_cast<dom_node*>(node));
        });
        std::erase_if(connected_resources, [&](const auto& resource) {
            return nodes.contains(resource.node);
        });
        std::erase_if(frame_base_addresses, [&](const auto& entry) {
            return nodes.contains(const_cast<dom_node*>(entry.first));
        });
        std::erase_if(document_base_caches, [&](const auto& entry) {
            return nodes.contains(const_cast<dom_node*>(entry.first));
        });
        std::erase_if(local_link_selector_subjects, [&](const auto& entry) {
            return nodes.contains(const_cast<dom_node*>(entry.first));
        });
        std::erase_if(selector_target_caches, [&](const auto& entry) {
            return nodes.contains(const_cast<dom_node*>(entry.first));
        });
        if (nodes.contains(const_cast<dom_node*>(active_css_cascade_root))) {
            activate_css_cascade(&document.body());
        }
        std::erase_if(inactive_css_cascades, [&](const auto& entry) {
            return nodes.contains(const_cast<dom_node*>(entry.first));
        });
        for (auto& observer : resize_observers) {
            std::erase_if(observer->nodes, [&](const auto* node) {
                return nodes.contains(const_cast<dom_node*>(node));
            });
            for (const auto id : node_ids) {
                observer->delivered_sizes.erase(id);
                observer->observed_boxes.erase(id);
            }
        }
        std::erase_if(provisional_frame_bodies, [&](const auto& entry) {
            return node_ids.contains(entry.first) || nodes.contains(entry.second);
        });
        for (const auto id : node_ids) {
            frame_documents.erase(id);
            frame_windows.erase(id);
            cross_origin_frame_locations.erase(id);
            frame_security_origins.erase(id);
            frame_window_scroll_offsets.erase(id);
            actual_frame_windows.erase(id);
            if (const auto found = actual_frame_contexts.find(id);
                found != actual_frame_contexts.end()
                && !found->second.IsEmpty()) {
#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8_INSPECTOR)
                destroy_inspector_context(found->second.Get(isolate));
#endif
            }
            actual_frame_contexts.erase(id);
            frame_hydration_states.erase(id);
            retire_iframe_navigation(id);
            frame_beforeunload_dispatches.erase(id);
            frame_pagehide_dispatched.erase(id);
            frame_unload_dispatches.erase(id);
            frame_unload_dispatched.erase(id);
            actual_frame_documents.erase(id);
            frame_session_storage.erase(id);
            frame_local_storage.erase(id);
            frame_load_listeners.erase(id);
            frame_dom_content_loaded_listeners.erase(id);
            window_selections.erase(id);
        }
        if (node_ids.contains(focused_document_owner_id)) {
          focused_document_owner_id = 0U;
        }
        std::erase_if(window_selections, [&](const auto &entry) {
          const auto &selection = entry.second;
          return node_ids.contains(entry.first) ||
                 node_ids.contains(selection.anchor_node_id) ||
                 node_ids.contains(selection.focus_node_id);
        });
        if (!frame_context.IsEmpty()) {
            auto local_frame_context = frame_context.Get(isolate);
            auto* frame_root = static_cast<dom_node*>(
                local_frame_context->GetAlignedPointerFromEmbedderData(
                    1,
                    v8::kEmbedderDataTypeTagDefault));
            if (nodes.contains(frame_root)) {
                frame_context.Reset();
                frame_base_address.clear();
            }
        }
        for (auto& [type, listeners] : frame_event_listeners) {
            static_cast<void>(type);
            std::erase_if(listeners, [&](auto& listener) {
                if (!node_ids.contains(listener.target)) return false;
                clear_event_listener(listener);
                return true;
            });
        }
        if (nodes.contains(active_element)) active_element = nullptr;
        if (nodes.contains(pointer_capture_target)) pointer_capture_target = nullptr;
        if (nodes.contains(pointer_down_target)) pointer_down_target = nullptr;
        if (middle_paste_target_id != 0U
            && node_ids.contains(middle_paste_target_id)) {
            middle_paste_target_id = 0U;
        }
        if (nodes.contains(active_pointer_target)) active_pointer_target = nullptr;
        if (nodes.contains(scrollbar_drag_target)) scrollbar_drag_target = nullptr;
        if (nodes.contains(scrollbar_hover_target)) scrollbar_hover_target = nullptr;
        if (nodes.contains(hover_target)) hover_target = nullptr;
        if (nodes.contains(current_related_target)) current_related_target = nullptr;
        released_detached_dom_nodes += document.erase_detached_subtrees(roots);
        synchronize_external_dom_memory_accounting();
    }

    void synchronize_external_dom_memory_accounting()
    {
        if (isolate == nullptr) return;
        const auto node_bytes = document.node_count() * sizeof(dom_node);
        const auto bounded_bytes = static_cast<int64_t>(std::min<size_t>(
            node_bytes,
            static_cast<size_t>(std::numeric_limits<int64_t>::max())));
        const auto delta = bounded_bytes - external_dom_memory_accounted_bytes;
        if (delta == 0) return;
        isolate->AdjustAmountOfExternalAllocatedMemory(delta);
        external_dom_memory_accounted_bytes = bounded_bytes;
    }

    void release_external_dom_memory_accounting()
    {
        if (isolate == nullptr || external_dom_memory_accounted_bytes == 0) return;
        isolate->AdjustAmountOfExternalAllocatedMemory(
            -external_dom_memory_accounted_bytes);
        external_dom_memory_accounted_bytes = 0;
    }

    void collect_detached_dom_if_requested()
    {
        if (detached_dom_gc_scan_index >= detached_dom_gc_scan_roots.size()) {
            detached_dom_gc_scan_roots.clear();
            detached_dom_gc_scan_index = 0U;
            if (!detached_dom_gc_requested) return;

            // A mutation can request collection while its V8 HandleScope is
            // still alive. Defer the first collection attempt to the next
            // pump so ephemeral wrappers from that completed JavaScript turn
            // are no longer conservatively retained.
            if (!detached_dom_gc_deferred_once) {
                detached_dom_gc_deferred_once = true;
                return;
            }
            detached_dom_gc_deferred_once = false;

            // Account the C++ DOM as external memory so V8's normal allocation
            // pressure participates in deciding when weak wrappers are
            // collected. At a genuinely idle boundary, offer a short deadline
            // for incremental GC work. LowMemoryNotification is deliberately
            // not used here: it means system memory pressure and performs an
            // aggressive collect-all pass rather than browser-style idle work.
            detached_dom_gc_requested = false;
            detached_nodes_since_gc = 0U;
            detached_dom_gc_cycle_retained_roots = 0U;
            detached_dom_gc_scan_roots = detached_dom_roots;
            synchronize_external_dom_memory_accounting();
            constexpr auto idle_gc_budget_seconds = 0.001;
            v8::platform::RunIdleTasks(
                v8_platform.get(),
                isolate,
                idle_gc_budget_seconds);
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
            ++detached_dom_idle_gc_notifications;
#endif
        }

        const auto slice_started = std::chrono::steady_clock::now();
        constexpr auto maximum_slice_scan_time = std::chrono::milliseconds(1);
        std::vector<uint32_t> removed_root_ids;
        std::vector<dom_node*> released_roots;
        removed_root_ids.reserve(maximum_detached_dom_release_roots_per_slice);
        released_roots.reserve(maximum_detached_dom_release_roots_per_slice);
        size_t inspected_roots = 0U;
        while (detached_dom_gc_scan_index < detached_dom_gc_scan_roots.size()
            && inspected_roots < maximum_detached_dom_release_roots_per_slice) {
            const auto root_id =
                detached_dom_gc_scan_roots[detached_dom_gc_scan_index++];
            ++inspected_roots;
            auto* root = document.find_by_native_id(root_id);
            if (root == nullptr) {
                removed_root_ids.push_back(root_id);
                continue;
            }
            if (is_connected(*root)) {
                retain_reconnected_subtree_wrappers(*root);
                removed_root_ids.push_back(root_id);
                continue;
            }
            if (subtree_has_live_wrappers(*root)) {
                ++detached_dom_gc_cycle_retained_roots;
                if (std::chrono::steady_clock::now() - slice_started
                    >= maximum_slice_scan_time) {
                    break;
                }
                continue;
            }
            removed_root_ids.push_back(root_id);
            released_roots.push_back(root);
            if (std::chrono::steady_clock::now() - slice_started
                >= maximum_slice_scan_time) {
                break;
            }
        }
        if (!removed_root_ids.empty()) {
            std::erase_if(detached_dom_roots, [&](const auto root_id) {
                return std::find(
                    removed_root_ids.begin(),
                    removed_root_ids.end(),
                    root_id) != removed_root_ids.end();
            });
            for (const auto root_id : removed_root_ids)
                detached_dom_root_ids.erase(root_id);
        }
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        ++detached_dom_release_slices;
        detached_dom_release_max_roots_per_slice = std::max<uint64_t>(
            detached_dom_release_max_roots_per_slice,
            released_roots.size());
#endif
        release_detached_subtrees(released_roots);

        if (detached_dom_gc_scan_index >= detached_dom_gc_scan_roots.size()) {
            detached_dom_gc_scan_roots.clear();
            detached_dom_gc_scan_index = 0U;
            // V8 can conservatively retain weak handles during the first
            // collection immediately following a large script turn. Retry one
            // later idle cycle when that happened at scale; a genuinely live
            // detached wrapper is then retained without creating a GC loop.
            if (detached_dom_gc_cycle_retained_roots > 1U
                && detached_dom_gc_retry_count == 0U
                && !detached_dom_gc_requested) {
                detached_dom_gc_retry_count = 1U;
                detached_dom_gc_deferred_once = false;
                detached_dom_gc_requested = true;
            }
        }
    }

    bool has_pending_detached_dom_collection() const noexcept
    {
        return detached_dom_gc_requested
            || detached_dom_gc_scan_index < detached_dom_gc_scan_roots.size();
    }

    v8::Local<v8::Object> wrap_node(dom_node& node)
    {
        const auto key = wrapper_key(node);
        auto known = node_wrappers.find(key);
        if (known != node_wrappers.end() && !known->second.IsEmpty()) {
            if (is_connected(node) && known->second.IsWeak()) known->second.ClearWeak();
            return known->second.Get(isolate);
        }
        if (known != node_wrappers.end()) node_wrappers.erase(known);
        auto local_context = isolate->GetCurrentContext();
#if defined(WEBSCENE_NATIVE_ENGINE_GENERATED_DOM_BINDINGS)
        auto selected_template = node.kind == dom_node_kind::element
            ? (node.namespace_uri() == dom_node::html_namespace_uri
                ? (node.tag == "slot"
                    ? html_slot_element_template.Get(isolate)
                    : node.tag == "a"
                        ? html_anchor_element_template.Get(isolate)
                    : node.tag == "iframe"
                        ? html_iframe_element_template.Get(isolate)
                    : node.tag == "dialog"
                        ? html_dialog_element_template.Get(isolate)
                    : node.tag == "details"
                        ? html_details_element_template.Get(isolate)
                    : node.tag == "button"
                        ? html_button_element_template.Get(isolate)
                    : node.tag == "input"
                        ? html_input_element_template.Get(isolate)
                    : node.tag == "textarea"
                        ? html_text_area_element_template.Get(isolate)
                    : node.tag == "meter"
                        ? html_meter_element_template.Get(isolate)
                    : node.tag == "output"
                        ? html_output_element_template.Get(isolate)
                    : node.tag == "progress"
                        ? html_progress_element_template.Get(isolate)
                    : node.tag == "form"
                        ? html_form_element_template.Get(isolate)
                    : node.tag == "fieldset"
                        ? html_field_set_element_template.Get(isolate)
                    : node.tag == "select"
                        ? html_select_element_template.Get(isolate)
                    : node.tag == "option"
                        ? html_option_element_template.Get(isolate)
                    : node.tag == "meta"
                        ? html_meta_element_template.Get(isolate)
                    : node.tag == "script"
                        ? html_script_element_template.Get(isolate)
                    : node.tag == "template"
                        ? html_template_element_template.Get(isolate)
                    : node.tag == "style"
                        ? html_style_element_template.Get(isolate)
                    : node.tag == "td" || node.tag == "th"
                        ? html_table_cell_element_template.Get(isolate)
                    : html_element_template.Get(isolate))
                : node.namespace_uri() == "http://www.w3.org/2000/svg"
                    ? svg_element_template.Get(isolate) : element_template.Get(isolate))
            : node.kind == dom_node_kind::document_fragment
                ? (document.is_shadow_root(node)
                    ? shadow_root_template.Get(isolate)
                    : document_fragment_template.Get(isolate))
                : node.kind == dom_node_kind::text
                    ? text_template.Get(isolate)
                    : node.kind == dom_node_kind::comment
                        ? comment_template.Get(isolate)
                        : node.kind == dom_node_kind::processing_instruction
                            ? processing_instruction_template.Get(isolate)
                            : node_template.Get(isolate);
        auto object = selected_template->InstanceTemplate()
#else
        auto object = element_template.Get(isolate)->InstanceTemplate()
#endif
            ->NewInstance(local_context)
            .ToLocalChecked();
#if !defined(WEBSCENE_NATIVE_ENGINE_GENERATED_DOM_BINDINGS)
        v8::Local<v8::Value> element_constructor;
        const auto has_element_constructor = local_context->Global()
            ->Get(local_context, js_string(isolate, "Element"))
            .ToLocal(&element_constructor)
            && element_constructor->IsFunction();
        if (has_element_constructor) {
            v8::Local<v8::Value> element_prototype;
            if (element_constructor.As<v8::Function>()
                    ->Get(
                        local_context,
                        js_string(isolate, "prototype"))
                    .ToLocal(&element_prototype)
                && element_prototype->IsObject()) {
                object->SetPrototype(local_context, element_prototype).Check();
            }
        }
#endif
        object->SetAlignedPointerInInternalField(
            0,
            &node,
            v8::kEmbedderDataTypeTagDefault);
#if defined(WEBSCENE_NATIVE_ENGINE_GENERATED_DOM_BINDINGS)
        object->SetInternalField(1, v8::Undefined(isolate));
#endif
        if (node.tag == "button" || node.tag == "fieldset" || node.tag == "input"
            || node.tag == "object" || node.tag == "output" || node.tag == "select"
            || node.tag == "textarea") {
            object->SetNativeDataProperty(
                local_context,
                js_string(isolate, "form"),
                get_form_owner).Check();
        }
#if defined(WEBSCENE_NATIVE_ENGINE_ENABLE_MEDIA)
        attach_media_prototype(local_context,node,object);
#endif
        auto& wrapper = node_wrappers[key];
        wrapper.Reset(isolate, object);
        if (!is_connected(node)) wrapper.SetWeak();
        return object;
    }

    v8::Local<v8::Object> wrap_style(dom_node& node)
    {
        const auto key = wrapper_key(node);
        auto known = style_wrappers.find(key);
        if (known != style_wrappers.end() && !known->second.IsEmpty()) {
            if (is_connected(node) && known->second.IsWeak()) known->second.ClearWeak();
            return known->second.Get(isolate);
        }
        if (known != style_wrappers.end()) style_wrappers.erase(known);
        auto object = style_template.Get(isolate)
            ->NewInstance(isolate->GetCurrentContext())
            .ToLocalChecked();
        object->SetAlignedPointerInInternalField(
            0,
            &node,
            v8::kEmbedderDataTypeTagDefault);
        object->SetInternalField(1, v8::False(isolate));
        auto& wrapper = style_wrappers[key];
        wrapper.Reset(isolate, object);
        if (!is_connected(node)) wrapper.SetWeak();
        return object;
    }

    v8::Local<v8::Object> wrap_class_list(dom_node& node)
    {
        const auto key = wrapper_key(node);
        auto known = class_list_wrappers.find(key);
        if (known != class_list_wrappers.end() && !known->second.IsEmpty()) {
            if (is_connected(node) && known->second.IsWeak()) known->second.ClearWeak();
            return known->second.Get(isolate);
        }
        if (known != class_list_wrappers.end()) class_list_wrappers.erase(known);
        auto local_context = isolate->GetCurrentContext();
        auto constructor = ensure_dom_token_list_template()
            ->GetFunction(local_context).ToLocalChecked();
        v8::Local<v8::Value> arguments[] = {
            v8::External::New(
                isolate,
                &node,
                v8::kExternalPointerTypeTagDefault),
            v8::Integer::New(
                isolate,
                static_cast<int>(reflected_token_list_kind::class_names))};
        auto object = constructor->NewInstance(local_context, 2, arguments).ToLocalChecked();
        auto& wrapper = class_list_wrappers[key];
        wrapper.Reset(isolate, object);
        if (!is_connected(node)) wrapper.SetWeak();
        return object;
    }

    v8::Local<v8::Object> wrap_sandbox_token_list(dom_node& node)
    {
        const auto key = wrapper_key(node);
        auto known = sandbox_token_list_wrappers.find(key);
        if (known != sandbox_token_list_wrappers.end() && !known->second.IsEmpty()) {
            if (is_connected(node) && known->second.IsWeak()) known->second.ClearWeak();
            return known->second.Get(isolate);
        }
        if (known != sandbox_token_list_wrappers.end()) {
            sandbox_token_list_wrappers.erase(known);
        }
        auto local_context = isolate->GetCurrentContext();
        auto constructor = ensure_dom_token_list_template()
            ->GetFunction(local_context).ToLocalChecked();
        v8::Local<v8::Value> arguments[] = {
            v8::External::New(
                isolate,
                &node,
                v8::kExternalPointerTypeTagDefault),
            v8::Integer::New(
                isolate,
                static_cast<int>(reflected_token_list_kind::iframe_sandbox))};
        auto object = constructor->NewInstance(local_context, 2, arguments).ToLocalChecked();
        auto& wrapper = sandbox_token_list_wrappers[key];
        wrapper.Reset(isolate, object);
        if (!is_connected(node)) wrapper.SetWeak();
        return object;
    }

    v8::Local<v8::Object> wrap_computed_style(dom_node& node)
    {
        const auto key = wrapper_key(node);
        auto known = computed_style_wrappers.find(key);
        if (known != computed_style_wrappers.end() && !known->second.IsEmpty()) {
            if (is_connected(node) && known->second.IsWeak()) known->second.ClearWeak();
            return known->second.Get(isolate);
        }
        if (known != computed_style_wrappers.end()) computed_style_wrappers.erase(known);
        auto object = style_template.Get(isolate)
            ->NewInstance(isolate->GetCurrentContext())
            .ToLocalChecked();
        object->SetAlignedPointerInInternalField(
            0,
            &node,
            v8::kEmbedderDataTypeTagDefault);
        object->SetInternalField(1, v8::True(isolate));
        auto& wrapper = computed_style_wrappers[key];
        wrapper.Reset(isolate, object);
        if (!is_connected(node)) wrapper.SetWeak();
        return object;
    }

    void enqueue_connected_resource_if_needed(
        dom_node& node,
        v8::Local<v8::Object> wrapper,
        v8::Local<v8::Context> resource_context)
    {
        for (auto* ancestor = &node; ancestor != nullptr; ancestor = ancestor->parent) {
            if (detached_document_roots.contains(ancestor->id)) return;
        }
        if (node.tag == "script") {
            bool module_script = false;
            const auto authored_type = node.attributes.find("type");
            if (authored_type != node.attributes.end()) {
                auto type = lower_html_name(authored_type->second);
                const auto first = type.find_first_not_of(" \t\r\n\f");
                const auto last = type.find_last_not_of(" \t\r\n\f");
                type = first == std::string::npos
                    ? std::string{}
                    : type.substr(first, last - first + 1U);
                module_script = type == "module";
                const auto executable = type.empty() || type == "module"
                    || type == "text/javascript"
                    || type == "application/javascript"
                    || type == "application/ecmascript"
                    || type == "text/ecmascript";
                if (!executable) return;
            }
            if (!is_connected(node)
                || node.script_state == script_execution_state::already_started) {
                return;
            }
            for (auto* ancestor = node.parent; ancestor != nullptr;
                 ancestor = ancestor->parent) {
                if (ancestor->tag != "iframe") continue;
                if (!iframe_sandbox_allows(*ancestor, "allow-scripts")) {
                    node.script_state = script_execution_state::already_started;
                    return;
                }
                break;
            }
            // The already-started flag is set before evaluation, matching the
            // browser's one-shot lifecycle even when execution reports an
            // error or script code synchronously reparents its own element.
            node.script_state = script_execution_state::already_started;
            const auto source_iterator = node.attributes.find("src");
            if (source_iterator == node.attributes.end()
                || source_iterator->second.empty()) {
                std::string source;
                append_node_text(node, source);
                if (!source.empty()) {
                    const auto name = (in_frame_context()
                            ? frame_base_address
                            : document_base_address)
                        + "#dynamic-inline-script-" + std::to_string(node.id);
                    v8::Local<v8::Value> document_value;
                    v8::Local<v8::Value> previous_current_script;
                    auto restore_current_script = false;
                    auto document_object_value = resource_context->Global()->Get(
                        resource_context,
                        js_string(isolate, "document"));
                    if (document_object_value.ToLocal(&document_value)
                        && document_value->IsObject()) {
                        restore_current_script = document_value.As<v8::Object>()->Get(
                            resource_context,
                            js_string(isolate, "currentScript")).ToLocal(
                                &previous_current_script);
                        document_value.As<v8::Object>()->Set(
                            resource_context,
                            js_string(isolate, "currentScript"),
                            wrapper).Check();
                    }
                    std::string error;
                    if (module_script && !document_value.IsEmpty() && document_value->IsObject())
                        document_value.As<v8::Object>()->Set(resource_context,js_string(isolate,"currentScript"),v8::Null(isolate)).Check();
                    const auto executed = module_script
                        ? execute_module(resource_context, source, name, error)
                        : execute_in_context(resource_context, source, name, error, false);
                    if (!document_value.IsEmpty() && document_value->IsObject()) {
                        document_value.As<v8::Object>()->Set(
                            resource_context,
                            js_string(isolate, "currentScript"),
                            restore_current_script
                                ? previous_current_script
                                : v8::Null(isolate)).Check();
                    }
                    if (!executed) {
                        frame_last_error_value = "Connected inline script failed: " + error;
                        ++frame_script_error_count;
                        return;
                    }
                    ++frame_script_execution_count;
                }
                return;
            }
        }
        if (node.tag == "img") {
            const auto source = node.attributes.find("src");
            if (source != node.attributes.end()) {
                const auto& image = node.replaced_image();
                if (image.complete && image.source == source->second) return;
            }
            // Assigning src schedules an image load even while the element is
            // detached. Appending that same element must not enqueue a second
            // load or dispatch a duplicate load event. A pending task reads the
            // node's latest src, so coalescing also handles src replacement.
            if (std::any_of(
                    connected_resources.begin(),
                    connected_resources.end(),
                    [&node](const auto& task) {
                        return task.node == &node;
                    })) {
                return;
            }
        }
        auto resource_element =
            node.tag == "link" || node.tag == "script" || node.tag == "img";
        v8::Local<v8::Value> onload;
        const auto has_onload = wrapper->Get(
                resource_context,
                js_string(isolate, "onload")).ToLocal(&onload)
            && onload->IsFunction();
        if (!resource_element && !has_onload) return;
        connected_resources.push_back(connected_resource_task{
            &node,
            v8::Global<v8::Context>(isolate, resource_context),
            v8::Global<v8::Object>(isolate, wrapper),
            {}, {}, {}, false, false, {}, 0U, 0U});
    }

    void enqueue_connected_resources_subtree(
        dom_node& root,
        v8::Local<v8::Object> wrapper,
        v8::Local<v8::Context> resource_context)
    {
        enqueue_connected_resource_if_needed(root, wrapper, resource_context);
        for (auto* child : root.children) {
            if (child == nullptr) continue;
            enqueue_connected_resources_subtree(
                *child,
                wrap_node(*child),
                resource_context);
        }
    }

    void enqueue_connected_image_resources_subtree(
        dom_node& root,
        v8::Local<v8::Context> resource_context)
    {
        if (root.tag == "img") {
            enqueue_connected_resource_if_needed(
                root,
                wrap_node(root),
                resource_context);
        }
        for (auto* child : root.children) {
            if (child != nullptr) {
                enqueue_connected_image_resources_subtree(
                    *child,
                    resource_context);
            }
        }
    }

    bool install_embed_fallback(dom_node& node)
    {
        if (!webscene::embed_fallback::enabled() || node.attributes.contains("frame-html")
            || node.attributes.contains("srcdoc")) return false;
        const auto source = node.attributes.find("src");
        if (source == node.attributes.end()) return false;
        const auto resolved = resolve_resource_url(source->second, current_base_address());
        const auto title = node.attributes.find("title");
        auto html = webscene::embed_fallback::document(resolved,
            title == node.attributes.end() ? std::string_view{} : title->second);
        if (!html) return false;
        node.attributes["object-html"] = std::move(*html);
        node.attributes["data-webscene-embed-fallback"] = "youtube";
        node.attributes["data-webscene-remote-result"] = "loaded";
        return true;
    }

    uint64_t current_iframe_navigation_generation(uint32_t frame_id)
    {
        std::lock_guard lock(document_generation_mutex);
        return frame_navigation_generations.try_emplace(frame_id, 1U)
            .first->second;
    }

    bool dispatch_iframe_beforeunload(dom_node& frame)
    {
        const auto known = actual_frame_contexts.find(frame.id);
        if (known == actual_frame_contexts.end() || known->second.IsEmpty()) {
            return true;
        }
        if (frame_pagehide_dispatched.contains(frame.id)) return true;
        // HTML blocks a navigation targeted at the browsing context which is
        // already prompting to unload. A sibling frame remains independent.
        if (!frame_beforeunload_dispatches.insert(frame.id).second) return false;
        auto local_context = known->second.Get(isolate);
        v8::Context::Scope context_scope(local_context);
        auto target = local_context->Global();
        v8::Local<v8::Value> dispatcher;
        if (!target->Get(local_context, js_string(isolate, "dispatchEvent"))
                .ToLocal(&dispatcher)
            || !dispatcher->IsFunction()) {
            frame_beforeunload_dispatches.erase(frame.id);
            return true;
        }
        auto event = create_event_instance(local_context);
        event->Set(local_context, js_string(isolate, "type"),
            js_string(isolate, "beforeunload")).Check();
        event->Set(local_context, js_string(isolate, "bubbles"),
            v8::False(isolate)).Check();
        event->Set(local_context, js_string(isolate, "cancelable"),
            v8::True(isolate)).Check();
        event->Set(local_context, js_string(isolate, "defaultPrevented"),
            v8::False(isolate)).Check();
        v8::TryCatch try_catch(isolate);
        v8::Local<v8::Value> result;
        v8::Local<v8::Value> arguments[] = {event};
        const auto dispatched = dispatcher.As<v8::Function>()->Call(
            local_context, target, 1, arguments).ToLocal(&result);
        perform_microtask_checkpoint();
        frame_beforeunload_dispatches.erase(frame.id);
        if (!dispatched) {
            frame_last_error_value = "Nested beforeunload dispatch failed: "
                + describe_reported_exception(try_catch, local_context);
            ++frame_script_error_count;
            report_document_script_error(frame_last_error_value);
            return true;
        }
        return !result->IsFalse();
    }

    void dispatch_iframe_unload(dom_node& frame)
    {
        const auto known = actual_frame_contexts.find(frame.id);
        if (known == actual_frame_contexts.end() || known->second.IsEmpty()
            || frame_unload_dispatched.contains(frame.id)
            || !frame_unload_dispatches.insert(frame.id).second) return;
        frame_unload_dispatched.insert(frame.id);
        auto local_context = known->second.Get(isolate);
        v8::Context::Scope context_scope(local_context);
        auto window = local_context->Global();
        auto document_value = actual_frame_documents.find(frame.id);
        auto document_target = document_value == actual_frame_documents.end()
                || document_value->second.IsEmpty()
            ? v8::Local<v8::Object>{}
            : document_value->second.Get(isolate);
        const auto dispatch = [&](v8::Local<v8::Object> target,
                                  std::string_view type,
                                  bool persisted = false) {
            if (target.IsEmpty()) return;
            v8::Local<v8::Value> dispatcher;
            if (!target->Get(local_context, js_string(isolate, "dispatchEvent"))
                    .ToLocal(&dispatcher)
                || !dispatcher->IsFunction()) return;
            auto event = create_event_instance(local_context);
            event->Set(local_context, js_string(isolate, "type"),
                js_string(isolate, std::string(type).c_str())).Check();
            event->Set(local_context, js_string(isolate, "bubbles"),
                v8::False(isolate)).Check();
            event->Set(local_context, js_string(isolate, "cancelable"),
                v8::False(isolate)).Check();
            if (type == "pagehide") {
                event->Set(local_context, js_string(isolate, "persisted"),
                    v8::Boolean::New(isolate, persisted)).Check();
            }
            v8::TryCatch try_catch(isolate);
            v8::Local<v8::Value> arguments[] = {event};
            static_cast<void>(dispatcher.As<v8::Function>()->Call(
                local_context, target, 1, arguments));
            perform_microtask_checkpoint();
        };
        if (frame_pagehide_dispatched.insert(frame.id).second) {
            dispatch(window, "pagehide");
        }
        if (!document_target.IsEmpty()) {
            auto* root = static_cast<dom_node*>(
                document_target->GetAlignedPointerFromInternalField(
                    0,
                    v8::kEmbedderDataTypeTagDefault));
            if (root != nullptr
                && hidden_frame_document_roots.insert(root->id).second) {
                if (document_visible) {
                    dispatch(document_target, "visibilitychange");
                }
            }
        }
        dispatch(window, "unload");
        frame_unload_dispatches.erase(frame.id);
    }

    void record_nested_cross_document_navigation(
        dom_node& frame,
        const std::optional<std::string>& requested_source,
        bool replace)
    {
        constexpr size_t maximum_history_entries = 1024U;
        constexpr size_t maximum_history_url_bytes = 8192U;
        auto history = nested_history_states.find(frame.id);
        if (history == nested_history_states.end()) return;
        auto& state = history->second;
        if (state.entries.empty() || state.index >= state.entries.size()) return;

        const auto owner_base =
            effective_document_base(css_cascade_root_for_node(frame));
        auto target = requested_source.has_value()
            && !requested_source->empty()
            ? resolve_resource_url(*requested_source, owner_base)
            : std::string("about:blank");
        if (target.size() > maximum_history_url_bytes) target.clear();

        const auto reclaim = [&](size_t first) {
            for (auto index = first; index < state.entries.size(); ++index) {
                if (state.entries[index].state.has_value()) {
                    state.state_bytes -= clone_packet_size(
                        *state.entries[index].state);
                }
            }
            state.entries.erase(
                state.entries.begin() + static_cast<std::ptrdiff_t>(first),
                state.entries.end());
        };
        const auto document_sequence = ++state.next_document_sequence;
        if (replace) {
            if (state.entries[state.index].state.has_value()) {
                state.state_bytes -= clone_packet_size(
                    *state.entries[state.index].state);
            }
            state.entries[state.index] = history_entry_state{
                std::move(target), std::nullopt, document_sequence};
        } else {
            reclaim(state.index + 1U);
            if (state.entries.size() >= maximum_history_entries) {
                if (state.entries.front().state.has_value()) {
                    state.state_bytes -= clone_packet_size(
                        *state.entries.front().state);
                }
                state.entries.erase(state.entries.begin());
                if (state.index != 0U) --state.index;
            }
            state.entries.push_back(history_entry_state{
                std::move(target), std::nullopt, document_sequence});
            state.index = state.entries.size() - 1U;
        }
        state.traversing = false;
    }

    bool begin_iframe_navigation(dom_node& frame)
    {
        // The initiating navigation owns the whole unload sequence. A handler
        // cannot recursively advance the generation or retire its own realm.
        if (frame_unload_dispatches.contains(frame.id)) return false;
        const auto requested_source = [&]() -> std::optional<std::string> {
            const auto source = frame.attributes.find("src");
            return source == frame.attributes.end()
                ? std::nullopt
                : std::optional<std::string>{source->second};
        }();
        const auto restore_requested_source = [&] {
            if (requested_source.has_value()) {
                frame.attributes["src"] = *requested_source;
            } else {
                frame.attributes.erase("src");
            }
        };
        if (!dispatch_iframe_beforeunload(frame)) {
            restore_requested_source();
            return false;
        }
        // A beforeunload handler cannot replace the navigation which caused
        // its own dispatch. Preserve the initiator's reflected source.
        restore_requested_source();
        if (frame.parent == nullptr) return false;
        if (const auto outgoing = actual_frame_contexts.find(frame.id);
            outgoing != actual_frame_contexts.end()
            && !outgoing->second.IsEmpty()) {
            auto outgoing_context = outgoing->second.Get(isolate);
            if (!frame_history_traversals.contains(frame.id))
                capture_nested_form_state(outgoing_context, true);
            if (auto* outgoing_root = static_cast<dom_node*>(
                    outgoing_context->GetAlignedPointerFromEmbedderData(
                        1, v8::kEmbedderDataTypeTagDefault));
                outgoing_root != nullptr)
                pending_form_restores.erase(outgoing_root->id);
        }
        if (active_drag_session.has_value()
            && active_drag_session->frame_owner_id == frame.id) {
            active_drag_session.reset();
        }
        dispatch_iframe_unload(frame);
        // pagehide, visibilitychange, or unload can synchronously author a
        // competing source or detach the owner. The original navigation wins
        // only while its browsing context is still connected.
        restore_requested_source();
        if (frame.parent == nullptr) return false;
        if (const auto outgoing = actual_frame_contexts.find(frame.id);
            outgoing != actual_frame_contexts.end()
            && !outgoing->second.IsEmpty()) {
            const auto outgoing_context = outgoing->second.Get(isolate);
            cancel_detached_frame_context_tasks(outgoing_context);
            retire_service_worker_clients(
                std::vector<v8::Local<v8::Context>>{outgoing_context});
        }
        if (frame_history_traversals.contains(frame.id)) {
            // History traversal selected the retained entry before initiating
            // this replacement. Keep the list and its traversal guard alive
            // until the replacement realm dispatches popstate.
        } else if (frame_reload_navigations.contains(frame.id)) {
            if (auto history = nested_history_states.find(frame.id);
                history != nested_history_states.end()) {
                history->second.traversing = false;
            }
        } else {
            const auto replace =
                frame_history_replace_navigations.erase(frame.id) != 0U;
            record_nested_cross_document_navigation(
                frame, requested_source, replace);
        }
        {
            std::lock_guard lock(host_request_mutex);
            std::erase_if(download_requests, [&](const auto& request) {
                if (request == nullptr) return false;
                const auto* target = document.find_by_native_id(
                    static_cast<uint32_t>(request->view.target_node_id));
                return request->view.frame_owner_node_id == frame.id
                    || (target != nullptr && subtree_contains(frame, target));
            });
        }
        discard_primary_selection_publication_for_frame(frame.id);
        {
            std::lock_guard lock(document_generation_mutex);
            ++frame_navigation_generations.try_emplace(frame.id, 0U)
                .first->second;
        }
        frame.attributes.erase("object-html");
        frame.attributes.erase("frame-html");
        frame.attributes.erase("data-webscene-remote-result");
        if (auto preparation = pending_frame_preparations.find(frame.id);
            preparation != pending_frame_preparations.end()) {
            if (preparation->second.future.valid()) {
                retired_frame_preparations.push_back(
                    std::move(preparation->second.future));
            }
            pending_frame_preparations.erase(preparation);
        }
        std::erase(pending_frame_hydrations, &frame);
        return true;
    }

    bool reload_nested_context(v8::Local<v8::Context> local_context)
    {
        auto* frame = active_nested_frame_for_context(local_context);
        if (frame == nullptr
            || frame_beforeunload_dispatches.contains(frame->id)
            || frame_unload_dispatches.contains(frame->id)) return false;
        const auto history = nested_history_states.find(frame->id);
        if (history != nested_history_states.end()
            && history->second.traversing) return false;
        auto* root = static_cast<dom_node*>(
            local_context->GetAlignedPointerFromEmbedderData(
                1, v8::kEmbedderDataTypeTagDefault));
        const auto address = navigation_address_for_root(root);
        if (address.empty()) return false;
        const auto authored_source = frame->attributes.find("src");
        frame_reload_reflected_sources.insert_or_assign(
            frame->id,
            authored_source == frame->attributes.end()
                ? std::nullopt
                : std::optional<std::string>{authored_source->second});
        frame->attributes["src"] = address;
        frame_reload_navigations.insert(frame->id);
        if (!begin_iframe_navigation(*frame)) {
            restore_nested_reload_reflected_source(*frame);
            frame_reload_navigations.erase(frame->id);
            return true;
        }
        enqueue_iframe_hydration_if_needed(*frame);
        record_feature(
            "web-api", "Location.reload", "partially-supported",
            "same-origin nested reload uses guarded document replacement",
            "web-api-binding");
        return true;
    }

    void restore_nested_reload_reflected_source(dom_node& frame)
    {
        const auto known = frame_reload_reflected_sources.find(frame.id);
        if (known == frame_reload_reflected_sources.end()) return;
        if (known->second.has_value()) {
            frame.attributes["src"] = *known->second;
        } else {
            frame.attributes.erase("src");
        }
        frame_reload_reflected_sources.erase(known);
    }

    void retire_iframe_navigation(uint32_t frame_id)
    {
        discard_primary_selection_publication_for_frame(frame_id);
        {
            std::lock_guard lock(document_generation_mutex);
            frame_navigation_generations.erase(frame_id);
        }
        if (auto preparation = pending_frame_preparations.find(frame_id);
            preparation != pending_frame_preparations.end()) {
            if (preparation->second.future.valid()) {
                retired_frame_preparations.push_back(
                    std::move(preparation->second.future));
            }
            pending_frame_preparations.erase(preparation);
        }
        nested_history_states.erase(frame_id);
        if (const auto root = frame_documents.find(frame_id);
            root != frame_documents.end() && root->second != nullptr)
            pending_form_restores.erase(root->second->id);
        frame_reload_navigations.erase(frame_id);
        frame_history_traversals.erase(frame_id);
        frame_history_replace_navigations.erase(frame_id);
        frame_reload_reflected_sources.erase(frame_id);
    }

    void enqueue_iframe_hydration_if_needed(dom_node& node)
    {
        if (!is_connected(node)) return;
        if (node.tag != "iframe") {
            for (auto* child : node.children) {
                if (child != nullptr) {
                    enqueue_iframe_hydration_if_needed(*child);
                }
            }
            return;
        }
        // Intercept before either document prefetch or script preparation. A
        // fallback is an engine-owned document; no player resources are loaded.
        const auto fallback = install_embed_fallback(node);
        const auto source = node.attributes.find("src");
        if (!fallback && source != node.attributes.end()) {
            if (node.attributes.erase("data-webscene-embed-fallback") != 0) {
                node.attributes.erase("object-html");
                node.attributes.erase("data-webscene-remote-result");
            }
            const auto object = object_urls.find(source->second);
            if (object != object_urls.end()) {
                node.attributes["object-html"] = object->second;
            } else {
                const auto& base = current_base_address();
                const auto resolved = resolve_resource_url(source->second, base);
                if (resolved.starts_with("http://")
                    || resolved.starts_with("https://")
                    || resolved.starts_with("file://")) {
                    node.attributes["data-webscene-remote-attempt"] = resolved;
                    node.attributes["src"] = resolved;
                    if (prefetch_iframe_documents) {
                        start_resource_prefetch(
                            resolved,
                            {},
                            WEBSCENE_RESOURCE_DOCUMENT);
                        start_frame_preparation(node, resolved);
                        node.attributes["data-webscene-remote-result"] = "pending";
                    } else {
                        std::string html;
                        std::string resolved_name;
                        if (load_text_resource(
                                resolved,
                                {},
                                WEBSCENE_RESOURCE_DOCUMENT,
                                html,
                                resolved_name)) {
                            node.attributes["object-html"] = std::move(html);
                            node.attributes["data-webscene-remote-result"] = "loaded";
                        } else {
                            node.attributes["data-webscene-remote-result"] = "failed";
                        }
                    }
                }
            }
        }
        const auto remote_result = node.attributes.find("data-webscene-remote-result");
        const auto remote_pending = remote_result != node.attributes.end()
            && remote_result->second == "pending";
        if (!node.attributes.contains("object-html")
            && !node.attributes.contains("frame-html")
            && !remote_pending) return;
        if (std::find(pending_frame_hydrations.begin(), pending_frame_hydrations.end(), &node)
            == pending_frame_hydrations.end()) {
            pending_frame_hydrations.push_back(&node);
        }
    }

    v8::Local<v8::Object> frame_document(dom_node& node)
    {
        auto known = frame_documents.find(node.id);
        if (known != frame_documents.end()) return known->second.Get(isolate);
        auto local_context = isolate->GetCurrentContext();
        auto object = frame_document_template.Get(isolate)->NewInstance(local_context).ToLocalChecked();
        object->Set(local_context, js_string(isolate, "readyState"), js_string(isolate, "complete")).Check();
        object->Set(local_context, js_string(isolate, "oninput"), v8::Null(isolate)).Check();
        const auto source = node.attributes.contains("src")
            ? resolve_resource_url(node.attributes["src"], document_base_address)
            : std::string("about:blank");
        set_document_address(local_context, object, source);
        auto& html = document.create_element("html");
        auto& head = document.create_element("head");
        auto& body = document.create_element("body");
        document.append_child(node, html);
        document.append_child(html, head);
        document.append_child(html, body);
        object->SetAlignedPointerInInternalField(
            0,
            &html,
            v8::kEmbedderDataTypeTagDefault);
        provisional_frame_bodies[node.id] = &body;
        frame_documents[node.id].Reset(isolate, object);
        return object;
    }

    v8::Local<v8::Object> frame_window(dom_node& node)
    {
        auto known = frame_windows.find(node.id);
        if (known != frame_windows.end()) return known->second.Get(isolate);
        auto local_context = context.Get(isolate);
        auto object = frame_window_template.Get(isolate)->NewInstance(local_context).ToLocalChecked();
        object->SetAlignedPointerInInternalField(
            0,
            &node,
            v8::kEmbedderDataTypeTagDefault);
        // The masking interceptor enforces the current committed origin. Its
        // own backing properties still have to be populated when the first
        // observation happens after a cross-origin commit.
        initializing_frame_window_id = node.id;
        auto location = v8::Object::New(isolate);
        const auto source = node.attributes.contains("src") ? node.attributes["src"] : "about:blank";
        location->Set(local_context, js_string(isolate, "href"), js_string(isolate, source.c_str())).Check();
        object->Set(local_context, js_string(isolate, "location"), location).Check();
        const auto actual_document = actual_frame_documents.find(node.id);
        object->Set(
            local_context,
            js_string(isolate, "document"),
            actual_document != actual_frame_documents.end()
                    && !actual_document->second.IsEmpty()
                ? v8::Local<v8::Value>(actual_document->second.Get(isolate))
                : v8::Local<v8::Value>(frame_document(node))).Check();
        object->Set(local_context, js_string(isolate, "window"), object).Check();
        object->Set(local_context, js_string(isolate, "self"), object).Check();
        object->Set(local_context, js_string(isolate, "parent"), local_context->Global()).Check();
        object->Set(local_context, js_string(isolate, "top"), local_context->Global()).Check();
        object->Set(local_context, js_string(isolate, "opener"), v8::Null(isolate)).Check();
        object->Set(
            local_context,
            js_string(isolate, "postMessage"),
            v8::Function::New(local_context, window_post_message).ToLocalChecked()).Check();
        object->Set(
            local_context,
            js_string(isolate, "close"),
            v8::Function::New(local_context, cross_origin_window_noop).ToLocalChecked()).Check();
        object->Set(
            local_context,
            js_string(isolate, "blur"),
            v8::Function::New(local_context, cross_origin_window_noop).ToLocalChecked()).Check();
        install_screen(local_context, object);
        install_performance_clock(local_context, object);
        install_performance_timeline_on_object(local_context, object);
        initializing_frame_window_id = 0U;
        frame_windows[node.id].Reset(isolate, object);
        return object;
    }

    bool compiled_css_selector_list_matches(
        const dom_node& node,
        const compiled_css_selector_list& selectors,
        const dom_node* scope_root = nullptr) const
    {
        return std::any_of(
            selectors.selectors.begin(),
            selectors.selectors.end(),
            [&](const auto& selector) {
                return !selector.compounds.empty()
                    && compiled_css_selector_matches(
                        node,
                        selector,
                        selector.compounds.size() - 1U,
                        scope_root);
            });
    }

    void collect_selector_matches(
        dom_node& node,
        const compiled_css_selector_list& selector,
        bool include_node,
        std::vector<dom_node*>& result,
        const dom_node* scope_root) const
    {
        if (include_node
            && !node.tag.empty()
            && node.tag.front() != '#'
            && compiled_css_selector_list_matches(
                node,
                selector,
                scope_root)) result.push_back(&node);
        for (auto* child : node.children) {
            if (child != nullptr) collect_selector_matches(
                *child,
                selector,
                true,
                result,
                scope_root);
        }
    }

    dom_node* find_selector_match(
        dom_node& node,
        const compiled_css_selector_list& selector,
        bool include_node,
        const dom_node* scope_root) const
    {
        if (include_node
            && !node.tag.empty()
            && node.tag.front() != '#'
            && compiled_css_selector_list_matches(
                node,
                selector,
                scope_root)) {
            return &node;
        }
        for (auto* child : node.children) {
            if (child == nullptr) continue;
            if (auto* match = find_selector_match(
                    *child,
                    selector,
                    true,
                    scope_root);
                match != nullptr) {
                return match;
            }
        }
        return nullptr;
    }

    dom_node* find_element_by_id(dom_node& root, const std::string& id)
    {
        const auto generation = document.scene_generation();
        if (element_id_index_generation != generation) {
            element_id_indexes.clear();
            element_named_indexes.clear();
            document_named_form_indexes.clear();
            element_id_index_generation = generation;
        }
        auto [root_index, inserted] = element_id_indexes.try_emplace(&root);
        if (inserted) {
            const auto collect = [&](const auto& recurse, dom_node& node) -> void {
                if (!node.id_attribute.empty()) {
                    root_index->second.try_emplace(node.id_attribute, &node);
                }
                for (auto* child : node.children) {
                    if (child != nullptr) recurse(recurse, *child);
                }
            };
            collect(collect, root);
        }
        const auto match = root_index->second.find(id);
        return match == root_index->second.end()
            ? nullptr
            : match->second;
    }

    dom_node* find_named_element(
        dom_node& root,
        const std::string& name,
        bool forms_only)
    {
        const auto generation = document.scene_generation();
        if (element_id_index_generation != generation) {
            element_id_indexes.clear();
            element_named_indexes.clear();
            document_named_form_indexes.clear();
            element_id_index_generation = generation;
        }
        auto& indexes = forms_only
            ? document_named_form_indexes
            : element_named_indexes;
        auto [root_index, inserted] = indexes.try_emplace(&root);
        if (inserted) {
            const auto collect = [&](const auto& recurse, dom_node& node) -> void {
                if (!forms_only || node.tag == "form") {
                    if (!node.id_attribute.empty()) {
                        root_index->second.try_emplace(node.id_attribute, &node);
                    }
                    const auto authored_name = node.attributes.find("name");
                    if (authored_name != node.attributes.end()
                        && !authored_name->second.empty()) {
                        root_index->second.try_emplace(authored_name->second, &node);
                    }
                }
                for (auto* child : node.children) {
                    if (child != nullptr) recurse(recurse, *child);
                }
            };
            collect(collect, root);
        }
        const auto match = root_index->second.find(name);
        return match == root_index->second.end()
            ? nullptr
            : match->second;
    }

    bool is_valid_dom_selector_list(std::string_view selector) const
    {
#if defined(WEBSCENE_NATIVE_ENGINE_SERVO_SELECTORS)
        // DOM selector APIs validate and then immediately match the same selector.
        // Reuse the compiled-selector cache so frequently repeated querySelector,
        // matches, and closest calls do not cross the Rust FFI and allocate a full
        // Servo parse result on every invocation.
        return !cached_css_selector_list(selector)->selectors.empty();
#else
        selector = trim_css_view(selector);
        if (selector.empty()) return false;

        static const std::unordered_set<std::string> recognized_pseudo_classes{
            "root", "scope", "empty",
            "first-child", "last-child", "only-child",
            "first-of-type", "last-of-type", "only-of-type",
            "nth-child", "nth-last-child", "nth-of-type", "nth-last-of-type",
            "not", "is", "where", "has",
            "hover", "active", "focus", "focus-visible", "focus-within",
            "disabled", "enabled", "checked", "indeterminate", "default",
            "required", "optional", "valid", "invalid", "in-range", "out-of-range",
            "read-only", "read-write", "placeholder-shown", "autofill",
            "link", "visited", "any-link", "local-link", "target", "target-within",
            "lang", "dir", "defined", "fullscreen", "modal", "open",
            "picture-in-picture", "user-valid", "user-invalid", "blank"};
        static const std::unordered_set<std::string> recognized_pseudo_elements{
            "before", "after", "first-letter", "first-line", "selection", "marker",
            "placeholder", "backdrop", "file-selector-button", "cue", "cue-region",
            "grammar-error", "spelling-error", "target-text"};
        static const std::unordered_set<std::string> selector_argument_pseudos{
            "not", "is", "where", "has"};
        static const std::unordered_set<std::string> value_argument_pseudos{
            "nth-child", "nth-last-child", "nth-of-type", "nth-last-of-type", "lang", "dir"};

        auto bracket_depth = 0;
        auto parenthesis_depth = 0;
        char quote = 0;
        auto item_has_token = false;
        for (size_t index = 0; index < selector.size(); ++index) {
            const auto character = selector[index];
            if (quote != 0) {
                if (character == quote && (index == 0U || selector[index - 1U] != '\\')) quote = 0;
                continue;
            }
            if (character == '\\') {
                index = skip_css_escape_sequence(selector, index) - 1U;
                item_has_token = true;
                continue;
            }
            if (character == '\'' || character == '"') {
                quote = character;
                item_has_token = true;
                continue;
            }
            if (character == '[') {
                ++bracket_depth;
                item_has_token = true;
                continue;
            }
            if (character == ']') {
                if (--bracket_depth < 0) return false;
                continue;
            }
            if (bracket_depth != 0) continue;
            if (character == '(') {
                ++parenthesis_depth;
                continue;
            }
            if (character == ')') {
                if (--parenthesis_depth < 0) return false;
                continue;
            }
            if (character == ',' && parenthesis_depth == 0) {
                if (!item_has_token) return false;
                item_has_token = false;
                continue;
            }
            if (std::isspace(static_cast<unsigned char>(character))) continue;
            if (character == '{' || character == '}' || character == '<' || character == '%') {
                return false;
            }
            if (character == '.' || character == '#') {
                auto cursor = index + 1U;
                if (cursor >= selector.size()) return false;
                const auto identifier = read_css_identifier(selector, cursor);
                if (identifier.empty()) return false;
                index = cursor - 1U;
                item_has_token = true;
                continue;
            }
            if (character != ':') {
                item_has_token = true;
                continue;
            }

            auto cursor = index + 1U;
            auto is_pseudo_element = cursor < selector.size() && selector[cursor] == ':';
            if (is_pseudo_element) ++cursor;
            auto name = lower_html_name(read_css_identifier(selector, cursor));
            if (name.empty()) return false;
            is_pseudo_element = is_pseudo_element || name == "before" || name == "after";
            if (is_pseudo_element
                ? !recognized_pseudo_elements.contains(name)
                : !recognized_pseudo_classes.contains(name)) return false;

            const auto has_argument = cursor < selector.size() && selector[cursor] == '(';
            const auto requires_argument = selector_argument_pseudos.contains(name)
                || value_argument_pseudos.contains(name);
            if (requires_argument != has_argument || (is_pseudo_element && has_argument)) return false;
            if (has_argument) {
                const auto argument_start = cursor + 1U;
                auto nested_depth = 1;
                char argument_quote = 0;
                auto close = argument_start;
                for (; close < selector.size() && nested_depth > 0; ++close) {
                    const auto nested = selector[close];
                    if (argument_quote != 0) {
                        if (nested == argument_quote
                            && (close == 0U || selector[close - 1U] != '\\')) argument_quote = 0;
                    } else if (nested == '\\') {
                        close = skip_css_escape_sequence(selector, close) - 1U;
                    } else if (nested == '\'' || nested == '"') {
                        argument_quote = nested;
                    } else if (nested == '(') {
                        ++nested_depth;
                    } else if (nested == ')') {
                        --nested_depth;
                    }
                }
                if (nested_depth != 0 || close <= argument_start + 1U
                    || trim_css_view(selector.substr(argument_start, close - argument_start - 1U)).empty()) {
                    return false;
                }
            }
            index = cursor - 1U;
            item_has_token = true;
        }
        return quote == 0 && bracket_depth == 0 && parenthesis_depth == 0 && item_has_token;
#endif
    }

    static void throw_selector_syntax_error(
        const v8::FunctionCallbackInfo<v8::Value>& info,
        std::string_view selector)
    {
        auto* isolate = info.GetIsolate();
        const auto context = isolate->GetCurrentContext();
        v8::Local<v8::Value> constructor_value;
        if (context->Global()->Get(context, js_string(isolate, "DOMException")).ToLocal(&constructor_value)
            && constructor_value->IsFunction()) {
            const auto message = "'" + std::string(selector) + "' is not a valid selector.";
            v8::Local<v8::Value> arguments[]{
                js_string(isolate, message.c_str()),
                js_string(isolate, "SyntaxError")};
            v8::Local<v8::Object> exception;
            if (constructor_value.As<v8::Function>()->NewInstance(context, 2, arguments).ToLocal(&exception)) {
                isolate->ThrowException(exception);
                return;
            }
        }
        isolate->ThrowException(v8::Exception::SyntaxError(
            js_string(isolate, "The selector is not valid.")));
    }

    std::vector<dom_node*> query_selector_nodes(
        dom_node& root,
        std::string_view selector,
        bool include_root) const
    {
        std::vector<dom_node*> result;
        const auto compiled = cached_css_selector_list(selector);
        collect_selector_matches(root, *compiled, include_root, result, &root);
        return result;
    }

    dom_node* query_selector_node(
        dom_node& root,
        std::string_view selector,
        bool include_root) const
    {
        const auto compiled = cached_css_selector_list(selector);
        return find_selector_match(root, *compiled, include_root, &root);
    }

    v8::Local<v8::Array> selector_results(
        dom_node& root,
        const std::string& selector,
        bool include_root = true)
    {
        auto matches = query_selector_nodes(root, selector, include_root);
        auto result = v8::Array::New(isolate, static_cast<int>(matches.size()));
        auto local_context = isolate->GetCurrentContext();
        for (uint32_t index = 0; index < matches.size(); ++index) {
            result->Set(local_context, index, wrap_node(*matches[index])).Check();
        }
        return result;
    }

    v8::Local<v8::Array> class_results(dom_node& root, const std::string& names)
    {
        std::vector<std::string> required;
        std::istringstream stream(names);
        for (std::string name; stream >> name;) required.push_back(std::move(name));
        auto matches = document.query_selector_all(root, "*");
        std::erase_if(matches, [this, &required](const auto* node) {
            return node == nullptr || required.empty()
                || !std::all_of(required.begin(), required.end(), [this, node](const auto& name) {
                    return has_class(*node, name);
                });
        });
        auto result = v8::Array::New(isolate, static_cast<int>(matches.size()));
        auto local_context = isolate->GetCurrentContext();
        for (uint32_t index = 0; index < matches.size(); ++index) {
            result->Set(local_context, index, wrap_node(*matches[index])).Check();
        }
        result->Set(
            local_context,
            js_string(isolate, "item"),
            v8::Function::New(local_context, collection_item).ToLocalChecked()).Check();
        return result;
    }

    static void collection_item(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto local_context = info.GetIsolate()->GetCurrentContext();
        const auto index = info.Length() > 0
            ? info[0]->Uint32Value(local_context).FromMaybe(0)
            : 0U;
        v8::Local<v8::Value> value;
        info.GetReturnValue().Set(
            info.This()->Get(local_context, index).ToLocal(&value)
                && !value->IsUndefined()
                ? value
                : v8::Local<v8::Value>(v8::Null(info.GetIsolate())));
    }

    static dom_node* html_collection_root(v8::Local<v8::Object> object)
    {
        return object->InternalFieldCount() < 1
            ? nullptr
            : static_cast<dom_node*>(object->GetAlignedPointerFromInternalField(
                0,
                v8::kEmbedderDataTypeTagDefault));
    }

    std::vector<dom_node*> document_link_nodes(dom_node& root) const
    {
        return query_selector_nodes(root, "a[href], area[href]", true);
    }

    dom_node* document_link_named_item(dom_node& root, std::string_view name) const
    {
        if (name.empty()) return nullptr;
        for (auto* node : document_link_nodes(root)) {
            if (node == nullptr) continue;
            if (node->id_attribute == name) return node;
            const auto authored_name = node->attributes.find("name");
            if (authored_name != node->attributes.end()
                && authored_name->second == name) return node;
        }
        return nullptr;
    }

    static bool is_labelable_control(const dom_node& node)
    {
        if (node.form_control().form_associated_custom_element) return true;
        if (node.tag == "input") return !forms::input_type_is(node, "hidden");
        return node.tag == "button" || node.tag == "meter"
            || node.tag == "output" || node.tag == "progress"
            || node.tag == "select" || node.tag == "textarea";
    }

    bool tree_order_before(const dom_node& left, const dom_node& right) const
    {
        if (&left == &right) return false;
        std::vector<const dom_node*> left_path;
        std::vector<const dom_node*> right_path;
        for (auto* node = &left; node != nullptr; node = document.dom_parent(*node))
            left_path.push_back(node);
        for (auto* node = &right; node != nullptr; node = document.dom_parent(*node))
            right_path.push_back(node);
        std::reverse(left_path.begin(), left_path.end());
        std::reverse(right_path.begin(), right_path.end());
        size_t shared = 0U;
        while (shared < left_path.size() && shared < right_path.size()
            && left_path[shared] == right_path[shared]) ++shared;
        if (shared == left_path.size()) return true;
        if (shared == right_path.size() || shared == 0U) return false;
        const auto& siblings = left_path[shared - 1U]->children;
        const auto left_position = std::find(
            siblings.begin(), siblings.end(), left_path[shared]);
        const auto right_position = std::find(
            siblings.begin(), siblings.end(), right_path[shared]);
        return left_position < right_position;
    }

    dom_node* first_labelable_descendant(dom_node& root) const
    {
        for (auto* child : root.children) {
            if (child == nullptr) continue;
            if (is_labelable_control(*child)) return child;
            if (child->tag == "iframe" || document.is_shadow_root(*child)) continue;
            if (auto* nested = first_labelable_descendant(*child); nested != nullptr)
                return nested;
        }
        return nullptr;
    }

    std::vector<dom_node*> control_label_nodes(dom_node& control)
    {
        std::vector<dom_node*> result;
        if (!is_labelable_control(control) || !is_connected(control)) return result;
        ensure_explicit_form_validity_dependents();
        const auto* root = forms::tree_root(document, control);
        if (!control.id_attribute.empty()) {
            auto first_target = static_cast<dom_node*>(nullptr);
            const auto targets = label_targets_by_id.find(control.id_attribute);
            if (targets != label_targets_by_id.end()) {
                for (auto* candidate : targets->second) {
                    if (candidate == nullptr || !is_connected(*candidate)
                        || forms::tree_root(document, *candidate) != root) continue;
                    if (first_target == nullptr
                        || tree_order_before(*candidate, *first_target))
                        first_target = candidate;
                }
            }
            if (first_target == &control) {
                const auto labels = explicit_labels_by_for.find(control.id_attribute);
                if (labels != explicit_labels_by_for.end()) {
                    for (auto* label : labels->second)
                        if (label != nullptr && is_connected(*label)
                            && forms::tree_root(document, *label) == root)
                            result.push_back(label);
                }
            }
        }
        for (auto* ancestor = document.dom_parent(control); ancestor != nullptr;
            ancestor = document.dom_parent(*ancestor)) {
            if (ancestor->tag == "iframe") break;
            if (ancestor->tag != "label" || ancestor->attributes.contains("for"))
                continue;
            if (first_labelable_descendant(*ancestor) == &control)
                result.push_back(ancestor);
        }
        std::sort(result.begin(), result.end(), [&](const auto* left, const auto* right) {
            return tree_order_before(*left, *right);
        });
        result.erase(std::unique(result.begin(), result.end()), result.end());
        return result;
    }

    static void html_collection_constructor(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        if (info.IsConstructCall()
            && info.Length() == 1
            && info[0]->IsExternal()) {
            info.This()->SetAlignedPointerInInternalField(
                0,
                info[0].As<v8::External>()->Value(v8::kExternalPointerTypeTagDefault),
                v8::kEmbedderDataTypeTagDefault);
            info.GetReturnValue().Set(info.This());
            return;
        }
        info.GetIsolate()->ThrowException(v8::Exception::TypeError(
            js_string(info.GetIsolate(), "Illegal constructor")));
    }

    v8::Local<v8::FunctionTemplate> ensure_html_collection_template()
    {
        if (!html_collection_template.IsEmpty()) {
            return html_collection_template.Get(isolate);
        }
        auto html_collection = v8::FunctionTemplate::New(
            isolate,
            html_collection_constructor);
        html_collection->SetClassName(js_string(isolate, "HTMLCollection"));
        html_collection->InstanceTemplate()->SetInternalFieldCount(1);
        auto signature = v8::Signature::New(isolate, html_collection);
        auto length_symbol = v8::Symbol::New(
            isolate,
            js_string(isolate, "WebScene.HTMLCollection.length"));
        html_collection->InstanceTemplate()->SetNativeDataProperty(
            length_symbol,
            get_html_collection_length,
            nullptr,
            js_string(isolate, "length"),
            v8::PropertyAttribute::None);
        html_collection->PrototypeTemplate()->SetAccessorProperty(
            js_string(isolate, "length"),
            v8::FunctionTemplate::New(
                isolate,
                html_collection_length_get,
                length_symbol,
                signature,
                0),
            v8::Local<v8::FunctionTemplate>(),
            v8::PropertyAttribute::None);
        html_collection->InstanceTemplate()->SetHandler(
            v8::IndexedPropertyHandlerConfiguration(html_collection_indexed_getter));
        html_collection->InstanceTemplate()->SetHandler(
            v8::NamedPropertyHandlerConfiguration(
                html_collection_named_getter,
                nullptr,
                nullptr,
                nullptr,
                nullptr,
                {},
                v8::PropertyHandlerFlags::kNonMasking));
        html_collection->PrototypeTemplate()->Set(
            js_string(isolate, "item"),
            v8::FunctionTemplate::New(
                isolate,
                html_collection_item,
                v8::Local<v8::Value>(),
                signature,
                1,
                v8::ConstructorBehavior::kThrow));
        html_collection->PrototypeTemplate()->Set(
            js_string(isolate, "namedItem"),
            v8::FunctionTemplate::New(
                isolate,
                html_collection_named_item,
                v8::Local<v8::Value>(),
                signature,
                1,
                v8::ConstructorBehavior::kThrow));
        html_collection->PrototypeTemplate()->Set(
            v8::Symbol::GetToStringTag(isolate),
            js_string(isolate, "HTMLCollection"),
            static_cast<v8::PropertyAttribute>(
                v8::PropertyAttribute::ReadOnly
                | v8::PropertyAttribute::DontEnum));
        html_collection_template.Reset(isolate, html_collection);
        return html_collection;
    }

    static void get_html_collection_constructor(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        if (self == nullptr) return;
        info.GetReturnValue().Set(self->ensure_html_collection_template()
            ->GetFunction(info.GetIsolate()->GetCurrentContext()).ToLocalChecked());
    }

    static void node_list_constructor(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        if (info.IsConstructCall() && info.Length() == 1
            && info[0]->IsExternal()) {
            info.This()->SetAlignedPointerInInternalField(
                0,
                info[0].As<v8::External>()->Value(
                    v8::kExternalPointerTypeTagDefault),
                v8::kEmbedderDataTypeTagDefault);
            info.GetReturnValue().Set(info.This());
            return;
        }
        info.GetIsolate()->ThrowException(v8::Exception::TypeError(
            js_string(info.GetIsolate(), "Illegal constructor")));
    }

    static dom_node* node_list_root(v8::Local<v8::Object> object)
    {
        return object->InternalFieldCount() < 1
            ? nullptr
            : static_cast<dom_node*>(object->GetAlignedPointerFromInternalField(
                0, v8::kEmbedderDataTypeTagDefault));
    }

    static void get_node_list_length(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self=current(info.GetIsolate());
        auto* root=node_list_root(info.Holder());
        if(self==nullptr || root==nullptr) return;
        info.GetReturnValue().Set(v8::Integer::NewFromUnsigned(
            info.GetIsolate(),
            static_cast<uint32_t>(self->control_label_nodes(*root).size())));
    }

    static v8::Intercepted node_list_indexed_getter(
        uint32_t index,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self=current(info.GetIsolate());
        auto* root=node_list_root(info.Holder());
        if(self==nullptr || root==nullptr) return v8::Intercepted::kNo;
        const auto nodes=self->control_label_nodes(*root);
        if(index>=nodes.size()) return v8::Intercepted::kNo;
        info.GetReturnValue().Set(self->wrap_node(*nodes[index]));
        return v8::Intercepted::kYes;
    }

    static void node_list_item(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self=current(info.GetIsolate());
        auto* root=node_list_root(info.This());
        if(self==nullptr || root==nullptr) return;
        const auto index=info.Length()>0
            ? info[0]->Uint32Value(
                info.GetIsolate()->GetCurrentContext()).FromMaybe(0U):0U;
        const auto nodes=self->control_label_nodes(*root);
        info.GetReturnValue().Set(index<nodes.size()
            ? v8::Local<v8::Value>(self->wrap_node(*nodes[index]))
            : v8::Local<v8::Value>(v8::Null(info.GetIsolate())));
    }

    static void node_list_iterator(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self=current(info.GetIsolate());
        auto* root=node_list_root(info.This());
        if(self==nullptr || root==nullptr) return;
        const auto nodes=self->control_label_nodes(*root);
        auto context=info.GetIsolate()->GetCurrentContext();
        auto values=v8::Array::New(
            info.GetIsolate(),static_cast<int>(nodes.size()));
        for(uint32_t index=0U;index<nodes.size();++index)
            values->Set(context,index,self->wrap_node(*nodes[index])).Check();
        v8::Local<v8::Value> iterator;
        if(!values->Get(context,v8::Symbol::GetIterator(info.GetIsolate()))
                .ToLocal(&iterator) || !iterator->IsFunction()) return;
        v8::Local<v8::Value> result;
        if(iterator.As<v8::Function>()->Call(context,values,0,nullptr)
                .ToLocal(&result)) info.GetReturnValue().Set(result);
    }

    v8::Local<v8::FunctionTemplate> ensure_node_list_template()
    {
        if(!node_list_template.IsEmpty()) return node_list_template.Get(isolate);
        auto node_list=v8::FunctionTemplate::New(isolate,node_list_constructor);
        node_list->SetClassName(js_string(isolate,"NodeList"));
        node_list->InstanceTemplate()->SetInternalFieldCount(1);
        node_list->InstanceTemplate()->SetNativeDataProperty(
            js_string(isolate,"length"),get_node_list_length);
        node_list->InstanceTemplate()->SetHandler(
            v8::IndexedPropertyHandlerConfiguration(node_list_indexed_getter));
        node_list->PrototypeTemplate()->Set(
            js_string(isolate,"item"),
            v8::FunctionTemplate::New(
                isolate,node_list_item,v8::Local<v8::Value>(),
                v8::Local<v8::Signature>(),1,
                v8::ConstructorBehavior::kThrow));
        node_list->PrototypeTemplate()->Set(
            v8::Symbol::GetIterator(isolate),
            v8::FunctionTemplate::New(
                isolate,node_list_iterator,v8::Local<v8::Value>(),
                v8::Local<v8::Signature>(),0,
                v8::ConstructorBehavior::kThrow));
        node_list->PrototypeTemplate()->Set(
            v8::Symbol::GetToStringTag(isolate),
            js_string(isolate,"NodeList"),
            static_cast<v8::PropertyAttribute>(
                v8::PropertyAttribute::ReadOnly
                | v8::PropertyAttribute::DontEnum));
        node_list_template.Reset(isolate,node_list);
        return node_list;
    }

    static void get_node_list_constructor(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self=current(info.GetIsolate());
        if(self==nullptr) return;
        info.GetReturnValue().Set(self->ensure_node_list_template()
            ->GetFunction(info.GetIsolate()->GetCurrentContext()).ToLocalChecked());
    }

    v8::Local<v8::Value> label_collection_for(dom_node& node)
    {
        if (!is_labelable_control(node)) return v8::Null(isolate);
        auto local_context = isolate->GetCurrentContext();
        auto holder = wrap_node(node);
        auto private_key = v8::Private::ForApi(
            isolate,
            js_string(isolate, "WebScene.form-control.labels"));
        v8::Local<v8::Value> known;
        if (holder->GetPrivate(local_context, private_key).ToLocal(&known)
            && known->IsObject()) return known;
        auto constructor = ensure_node_list_template()
            ->GetFunction(local_context).ToLocalChecked();
        v8::Local<v8::Value> arguments[]{v8::External::New(
            isolate, &node, v8::kExternalPointerTypeTagDefault)};
        auto collection = constructor->NewInstance(
            local_context, 1, arguments).ToLocalChecked();
        holder->SetPrivate(local_context, private_key, collection).Check();
        return collection;
    }

    static void get_html_collection_length(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* root = html_collection_root(info.Holder());
        if (self == nullptr || root == nullptr) return;
        info.GetReturnValue().Set(v8::Integer::NewFromUnsigned(
            info.GetIsolate(),
            static_cast<uint32_t>(self->document_link_nodes(*root).size())));
    }

    static void html_collection_length_get(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto symbol = info.Data().As<v8::Symbol>();
        v8::Local<v8::Value> value;
        if (info.This()->Get(
                info.GetIsolate()->GetCurrentContext(),
                symbol).ToLocal(&value)) {
            info.GetReturnValue().Set(value);
        }
    }

    static v8::Intercepted html_collection_indexed_getter(
        uint32_t index,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* root = html_collection_root(info.Holder());
        if (self == nullptr || root == nullptr) return v8::Intercepted::kNo;
        const auto links = self->document_link_nodes(*root);
        if (index >= links.size()) return v8::Intercepted::kNo;
        info.GetReturnValue().Set(self->wrap_node(*links[index]));
        return v8::Intercepted::kYes;
    }

    static v8::Intercepted html_collection_named_getter(
        v8::Local<v8::Name> property,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        if (!property->IsString()) return v8::Intercepted::kNo;
        auto* self = current(info.GetIsolate());
        auto* root = html_collection_root(info.Holder());
        if (self == nullptr || root == nullptr) return v8::Intercepted::kNo;
        const auto name = to_utf8(info.GetIsolate(), property.As<v8::Value>());
        auto* match = self->document_link_named_item(*root, name);
        if (match == nullptr) return v8::Intercepted::kNo;
        info.GetReturnValue().Set(self->wrap_node(*match));
        return v8::Intercepted::kYes;
    }

    static dom_node* require_html_collection(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* root = html_collection_root(info.This());
        if (root == nullptr) {
            info.GetIsolate()->ThrowException(v8::Exception::TypeError(
                js_string(info.GetIsolate(), "Illegal invocation")));
        }
        return root;
    }

    static void html_collection_item(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* root = require_html_collection(info);
        if (self == nullptr || root == nullptr) return;
        const auto index = info.Length() > 0
            ? info[0]->Uint32Value(info.GetIsolate()->GetCurrentContext()).FromMaybe(0U)
            : 0U;
        const auto links = self->document_link_nodes(*root);
        info.GetReturnValue().Set(index < links.size()
            ? v8::Local<v8::Value>(self->wrap_node(*links[index]))
            : v8::Local<v8::Value>(v8::Null(info.GetIsolate())));
    }

    static void html_collection_named_item(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* root = require_html_collection(info);
        if (self == nullptr || root == nullptr) return;
        const auto name = info.Length() > 0
            ? to_utf8(info.GetIsolate(), info[0])
            : std::string{};
        auto* match = self->document_link_named_item(*root, name);
        info.GetReturnValue().Set(match == nullptr
            ? v8::Local<v8::Value>(v8::Null(info.GetIsolate()))
            : v8::Local<v8::Value>(self->wrap_node(*match)));
    }

    const std::vector<dom_node*>& form_control_collection_nodes(dom_node& form)
    {
        static const std::vector<dom_node*> empty;
        ensure_explicit_form_validity_dependents();
        const auto known = live_form_controls.find(&form);
        return known == live_form_controls.end() ? empty : known->second;
    }

    std::vector<dom_node*> form_control_named_nodes(
        dom_node& form, std::string_view name)
    {
        std::vector<dom_node*> result;
        if (name.empty()) return result;
        for (auto* node : form_control_collection_nodes(form)) {
            if (node == nullptr) continue;
            const auto authored_name = node->attributes.find("name");
            if (node->id_attribute == name
                || (authored_name != node->attributes.end()
                    && authored_name->second == name)) result.push_back(node);
        }
        return result;
    }

    static dom_node* form_controls_collection_form(v8::Local<v8::Object> object)
    {
        return object->InternalFieldCount() < 1
            ? nullptr
            : static_cast<dom_node*>(object->GetAlignedPointerFromInternalField(
                0, v8::kEmbedderDataTypeTagDefault));
    }

    static void html_form_controls_collection_constructor(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        if (info.IsConstructCall() && info.Length() == 2
            && info[0]->IsExternal() && info[1]->IsObject()) {
            info.This()->SetAlignedPointerInInternalField(
                0,
                info[0].As<v8::External>()->Value(
                    v8::kExternalPointerTypeTagDefault),
                v8::kEmbedderDataTypeTagDefault);
            info.This()->SetInternalField(1, info[1]);
            info.GetReturnValue().Set(info.This());
            return;
        }
        info.GetIsolate()->ThrowException(v8::Exception::TypeError(
            js_string(info.GetIsolate(), "Illegal constructor")));
    }

    static void get_form_controls_collection_length(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* form = form_controls_collection_form(info.Holder());
        if (self == nullptr || form == nullptr) return;
        info.GetReturnValue().Set(v8::Integer::NewFromUnsigned(
            info.GetIsolate(),
            static_cast<uint32_t>(
                self->form_control_collection_nodes(*form).size())));
    }

    static v8::Intercepted form_controls_collection_indexed_getter(
        uint32_t index,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* form = form_controls_collection_form(info.Holder());
        if (self == nullptr || form == nullptr) return v8::Intercepted::kNo;
        const auto& nodes = self->form_control_collection_nodes(*form);
        if (index >= nodes.size()) return v8::Intercepted::kNo;
        info.GetReturnValue().Set(self->wrap_node(*nodes[index]));
        return v8::Intercepted::kYes;
    }

    static std::string radio_node_list_name(v8::Local<v8::Object> object)
    {
        if (object->InternalFieldCount() < 3
            || !object->GetInternalField(2)->IsValue()) return {};
        return to_utf8(
            v8::Isolate::GetCurrent(),
            object->GetInternalField(2).As<v8::Value>());
    }

    v8::Local<v8::Object> create_radio_node_list(
        dom_node& form, std::string_view name)
    {
        auto context = isolate->GetCurrentContext();
        auto constructor = ensure_radio_node_list_template()
            ->GetFunction(context).ToLocalChecked();
        v8::Local<v8::Value> arguments[]{
            v8::External::New(
                isolate, &form, v8::kExternalPointerTypeTagDefault),
            wrap_node(form),
            js_dom_string(isolate, name)};
        return constructor->NewInstance(context, 3, arguments).ToLocalChecked();
    }

    v8::Local<v8::Value> form_control_named_value(
        dom_node& form, std::string_view name)
    {
        auto matches = form_control_named_nodes(form, name);
        if (matches.empty()) return v8::Null(isolate);
        if (matches.size() == 1U) return wrap_node(*matches.front());
        return create_radio_node_list(form, name);
    }

    static v8::Intercepted form_controls_collection_named_getter(
        v8::Local<v8::Name> property,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        if (!property->IsString()) return v8::Intercepted::kNo;
        auto* self = current(info.GetIsolate());
        auto* form = form_controls_collection_form(info.Holder());
        if (self == nullptr || form == nullptr) return v8::Intercepted::kNo;
        const auto name = to_utf8(info.GetIsolate(), property.As<v8::Value>());
        const auto matches = self->form_control_named_nodes(*form, name);
        if (matches.empty()) return v8::Intercepted::kNo;
        info.GetReturnValue().Set(matches.size() == 1U
            ? v8::Local<v8::Value>(self->wrap_node(*matches.front()))
            : v8::Local<v8::Value>(self->create_radio_node_list(*form, name)));
        return v8::Intercepted::kYes;
    }

    static void form_controls_collection_named_enumerator(
        const v8::PropertyCallbackInfo<v8::Array>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* form = form_controls_collection_form(info.Holder());
        std::vector<std::string> names;
        std::unordered_set<std::string> seen;
        if (self != nullptr && form != nullptr) {
            for (auto* node : self->form_control_collection_nodes(*form)) {
                if (node == nullptr) continue;
                if (!node->id_attribute.empty()
                    && seen.insert(node->id_attribute).second)
                    names.push_back(node->id_attribute);
                const auto name = node->attributes.find("name");
                if (name != node->attributes.end() && !name->second.empty()
                    && seen.insert(name->second).second)
                    names.push_back(name->second);
            }
        }
        auto result = v8::Array::New(
            info.GetIsolate(), static_cast<int>(names.size()));
        auto context = info.GetIsolate()->GetCurrentContext();
        for (uint32_t index = 0; index < names.size(); ++index)
            result->Set(context, index,
                js_dom_string(info.GetIsolate(), names[index])).Check();
        info.GetReturnValue().Set(result);
    }

    static void form_controls_collection_item(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* form = form_controls_collection_form(info.This());
        if (self == nullptr || form == nullptr) return;
        const auto index = info.Length() > 0
            ? info[0]->Uint32Value(
                info.GetIsolate()->GetCurrentContext()).FromMaybe(0U)
            : 0U;
        const auto& nodes = self->form_control_collection_nodes(*form);
        info.GetReturnValue().Set(index < nodes.size()
            ? v8::Local<v8::Value>(self->wrap_node(*nodes[index]))
            : v8::Local<v8::Value>(v8::Null(info.GetIsolate())));
    }

    static void form_controls_collection_named_item(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* form = form_controls_collection_form(info.This());
        if (self == nullptr || form == nullptr) return;
        const auto name = info.Length() > 0
            ? to_utf8(info.GetIsolate(), info[0]) : std::string{};
        info.GetReturnValue().Set(self->form_control_named_value(*form, name));
    }

    v8::Local<v8::FunctionTemplate> ensure_html_form_controls_collection_template()
    {
        if (!html_form_controls_collection_template.IsEmpty())
            return html_form_controls_collection_template.Get(isolate);
        auto collection = v8::FunctionTemplate::New(
            isolate, html_form_controls_collection_constructor);
        collection->SetClassName(
            js_string(isolate, "HTMLFormControlsCollection"));
        collection->Inherit(ensure_html_collection_template());
        collection->InstanceTemplate()->SetInternalFieldCount(2);
        collection->InstanceTemplate()->SetNativeDataProperty(
            js_string(isolate, "length"), get_form_controls_collection_length);
        collection->InstanceTemplate()->SetHandler(
            v8::IndexedPropertyHandlerConfiguration(
                form_controls_collection_indexed_getter));
        collection->InstanceTemplate()->SetHandler(
            v8::NamedPropertyHandlerConfiguration(
                form_controls_collection_named_getter,
                nullptr, nullptr, nullptr,
                form_controls_collection_named_enumerator,
                {}, v8::PropertyHandlerFlags::kNonMasking));
        collection->PrototypeTemplate()->Set(
            js_string(isolate, "item"),
            v8::FunctionTemplate::New(
                isolate, form_controls_collection_item,
                v8::Local<v8::Value>(), v8::Local<v8::Signature>(), 1,
                v8::ConstructorBehavior::kThrow));
        collection->PrototypeTemplate()->Set(
            js_string(isolate, "namedItem"),
            v8::FunctionTemplate::New(
                isolate, form_controls_collection_named_item,
                v8::Local<v8::Value>(), v8::Local<v8::Signature>(), 1,
                v8::ConstructorBehavior::kThrow));
        collection->PrototypeTemplate()->Set(
            v8::Symbol::GetToStringTag(isolate),
            js_string(isolate, "HTMLFormControlsCollection"),
            static_cast<v8::PropertyAttribute>(
                v8::PropertyAttribute::ReadOnly
                | v8::PropertyAttribute::DontEnum));
        html_form_controls_collection_template.Reset(isolate, collection);
        return collection;
    }

    static void get_html_form_controls_collection_constructor(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        if (self == nullptr) return;
        info.GetReturnValue().Set(
            self->ensure_html_form_controls_collection_template()
                ->GetFunction(info.GetIsolate()->GetCurrentContext())
                .ToLocalChecked());
    }

    static void radio_node_list_constructor(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        if (info.IsConstructCall() && info.Length() == 3
            && info[0]->IsExternal() && info[1]->IsObject()
            && info[2]->IsString()) {
            info.This()->SetAlignedPointerInInternalField(
                0,
                info[0].As<v8::External>()->Value(
                    v8::kExternalPointerTypeTagDefault),
                v8::kEmbedderDataTypeTagDefault);
            info.This()->SetInternalField(1, info[1]);
            info.This()->SetInternalField(2, info[2]);
            info.GetReturnValue().Set(info.This());
            return;
        }
        info.GetIsolate()->ThrowException(v8::Exception::TypeError(
            js_string(info.GetIsolate(), "Illegal constructor")));
    }

    std::vector<dom_node*> radio_node_list_nodes(v8::Local<v8::Object> object)
    {
        auto* form = form_controls_collection_form(object);
        return form == nullptr ? std::vector<dom_node*>{}
            : form_control_named_nodes(*form, radio_node_list_name(object));
    }

    static void get_radio_node_list_length(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        if (self == nullptr) return;
        info.GetReturnValue().Set(v8::Integer::NewFromUnsigned(
            info.GetIsolate(),
            static_cast<uint32_t>(
                self->radio_node_list_nodes(info.Holder()).size())));
    }

    static v8::Intercepted radio_node_list_indexed_getter(
        uint32_t index,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        if (self == nullptr) return v8::Intercepted::kNo;
        const auto nodes = self->radio_node_list_nodes(info.Holder());
        if (index >= nodes.size()) return v8::Intercepted::kNo;
        info.GetReturnValue().Set(self->wrap_node(*nodes[index]));
        return v8::Intercepted::kYes;
    }

    static void radio_node_list_item(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        if (self == nullptr) return;
        const auto nodes = self->radio_node_list_nodes(info.This());
        const auto index = info.Length() > 0
            ? info[0]->Uint32Value(
                info.GetIsolate()->GetCurrentContext()).FromMaybe(0U)
            : 0U;
        info.GetReturnValue().Set(index < nodes.size()
            ? v8::Local<v8::Value>(self->wrap_node(*nodes[index]))
            : v8::Local<v8::Value>(v8::Null(info.GetIsolate())));
    }

    static void radio_node_list_iterator(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        if (self == nullptr) return;
        const auto nodes = self->radio_node_list_nodes(info.This());
        auto context = info.GetIsolate()->GetCurrentContext();
        auto values = v8::Array::New(
            info.GetIsolate(), static_cast<int>(nodes.size()));
        for (uint32_t index = 0; index < nodes.size(); ++index)
            values->Set(context, index, self->wrap_node(*nodes[index])).Check();
        v8::Local<v8::Value> iterator;
        if (!values->Get(context, v8::Symbol::GetIterator(info.GetIsolate()))
                .ToLocal(&iterator) || !iterator->IsFunction()) return;
        v8::Local<v8::Value> result;
        if (iterator.As<v8::Function>()->Call(context, values, 0, nullptr)
                .ToLocal(&result)) info.GetReturnValue().Set(result);
    }

    static void get_radio_node_list_value(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        if (self == nullptr) return;
        for (auto* node : self->radio_node_list_nodes(info.Holder())) {
            if (node == nullptr || !forms::input_type_is(*node, "radio")
                || !forms::input_checked(*node)) continue;
            auto value = forms::text_control_value(*node);
            if (!node->attributes.contains("value")) value = "on";
            info.GetReturnValue().Set(js_dom_string(info.GetIsolate(), value));
            return;
        }
        info.GetReturnValue().Set(js_string(info.GetIsolate(), ""));
    }

    static void set_radio_node_list_value(
        v8::Local<v8::Name>,
        v8::Local<v8::Value> value,
        const v8::PropertyCallbackInfo<void>& info)
    {
        auto* self = current(info.GetIsolate());
        if (self == nullptr) return;
        const auto expected = to_wtf8(info.GetIsolate(), value);
        for (auto* node : self->radio_node_list_nodes(info.Holder())) {
            if (node == nullptr || !forms::input_type_is(*node, "radio")) continue;
            auto candidate = forms::text_control_value(*node);
            if (!node->attributes.contains("value")) candidate = "on";
            if (candidate != expected) continue;
            for (auto* member : self->radio_group_members(*node))
                if (member != node) self->set_native_checkedness(*member, false);
            self->set_native_checkedness(*node, true);
            return;
        }
    }

    v8::Local<v8::FunctionTemplate> ensure_radio_node_list_template()
    {
        if (!radio_node_list_template.IsEmpty())
            return radio_node_list_template.Get(isolate);
        auto list = v8::FunctionTemplate::New(isolate, radio_node_list_constructor);
        list->SetClassName(js_string(isolate, "RadioNodeList"));
        list->Inherit(ensure_node_list_template());
        list->InstanceTemplate()->SetInternalFieldCount(3);
        list->InstanceTemplate()->SetNativeDataProperty(
            js_string(isolate, "length"), get_radio_node_list_length);
        list->InstanceTemplate()->SetNativeDataProperty(
            js_string(isolate, "value"),
            get_radio_node_list_value, set_radio_node_list_value);
        list->InstanceTemplate()->SetHandler(
            v8::IndexedPropertyHandlerConfiguration(
                radio_node_list_indexed_getter));
        list->PrototypeTemplate()->Set(
            js_string(isolate, "item"),
            v8::FunctionTemplate::New(
                isolate, radio_node_list_item,
                v8::Local<v8::Value>(), v8::Local<v8::Signature>(), 1,
                v8::ConstructorBehavior::kThrow));
        list->PrototypeTemplate()->Set(
            v8::Symbol::GetIterator(isolate),
            v8::FunctionTemplate::New(
                isolate, radio_node_list_iterator,
                v8::Local<v8::Value>(), v8::Local<v8::Signature>(), 0,
                v8::ConstructorBehavior::kThrow));
        list->PrototypeTemplate()->Set(
            v8::Symbol::GetToStringTag(isolate),
            js_string(isolate, "RadioNodeList"),
            static_cast<v8::PropertyAttribute>(
                v8::PropertyAttribute::ReadOnly
                | v8::PropertyAttribute::DontEnum));
        radio_node_list_template.Reset(isolate, list);
        return list;
    }

    static void get_radio_node_list_constructor(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        if (self == nullptr) return;
        info.GetReturnValue().Set(self->ensure_radio_node_list_template()
            ->GetFunction(info.GetIsolate()->GetCurrentContext()).ToLocalChecked());
    }

    static v8::Intercepted form_named_getter(
        v8::Local<v8::Name> property,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        if (!property->IsString()) return v8::Intercepted::kNo;
        auto* self = current(info.GetIsolate());
        auto* form = unwrap_node(info.Holder());
        if (self == nullptr || form == nullptr || form->tag != "form")
            return v8::Intercepted::kNo;
        const auto name = to_utf8(info.GetIsolate(), property.As<v8::Value>());
        const auto matches = self->form_control_named_nodes(*form, name);
        if (matches.empty()) return v8::Intercepted::kNo;
        info.GetReturnValue().Set(matches.size() == 1U
            ? v8::Local<v8::Value>(self->wrap_node(*matches.front()))
            : v8::Local<v8::Value>(self->create_radio_node_list(*form, name)));
        return v8::Intercepted::kYes;
    }

    static void get_body(
        v8::Local<v8::Name> property,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        self->record_feature(
            "dom", "Document.location", "supported", {}, "dom-binding");
        auto* root = static_cast<dom_node*>(info.Holder()->GetAlignedPointerFromInternalField(
            0,
            v8::kEmbedderDataTypeTagDefault));
        if (root == nullptr) root = &self->active_root();
        const auto property_name = to_utf8(info.GetIsolate(), property);
        if (root->xml_mode && root->tag == "#document-root") {
            if (property_name == "documentElement") {
                const auto found = std::find_if(
                    root->children.begin(),
                    root->children.end(),
                    [](const auto* child) {
                        return child != nullptr && !child->tag.starts_with('#');
                    });
                info.GetReturnValue().Set(found == root->children.end()
                    ? v8::Local<v8::Value>(v8::Null(info.GetIsolate()))
                    : v8::Local<v8::Value>(self->wrap_node(**found)));
            } else {
                info.GetReturnValue().Set(v8::Null(info.GetIsolate()));
            }
            return;
        }
        auto* html = root->tag == "html" ? root : nullptr;
        if (html == nullptr && root->tag == "body") {
            if (root->parent != nullptr && root->parent->tag == "html") {
                html = root->parent;
            } else {
                const auto found = std::find_if(
                    root->children.begin(),
                    root->children.end(),
                    [](const auto* child) { return child != nullptr && child->tag == "html"; });
                if (found != root->children.end()) html = *found;
            }
        }
        if (html != nullptr && property_name == "documentElement") {
            info.GetReturnValue().Set(self->wrap_node(*html));
            return;
        }
        if (html != nullptr) {
            const auto tag = property_name == "head" ? "head" : "body";
            const auto found = std::find_if(
                html->children.begin(),
                html->children.end(),
                [tag](const auto* child) { return child != nullptr && child->tag == tag; });
            if (found != html->children.end()) {
                info.GetReturnValue().Set(self->wrap_node(**found));
                return;
            }
        }
        info.GetReturnValue().Set(self->wrap_node(*root));
    }

    static dom_node* document_element_node(implementation& self, v8::Local<v8::Object> holder)
    {
        auto* root = static_cast<dom_node*>(holder->GetAlignedPointerFromInternalField(
            0,
            v8::kEmbedderDataTypeTagDefault));
        if (root == nullptr) root = &self.active_root();
        if (root->tag == "html") return root;
        if (root->tag == "body") {
            const auto html = std::find_if(
                root->children.begin(), root->children.end(), [](const auto* child) {
                    return child != nullptr && child->tag == "html";
                });
            return html == root->children.end() ? root : *html;
        }
        if (root->tag == "#document-root") {
            const auto element = std::find_if(
                root->children.begin(), root->children.end(), [](const auto* child) {
                    return child != nullptr && !child->tag.starts_with('#');
                });
            return element == root->children.end() ? nullptr : *element;
        }
        return root;
    }

    static void get_document_boundary_child(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* element = document_element_node(*self, info.Holder());
        info.GetReturnValue().Set(element == nullptr
            ? v8::Local<v8::Value>(v8::Null(info.GetIsolate()))
            : v8::Local<v8::Value>(self->wrap_node(*element)));
    }

    static void get_document_child_nodes(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* element = document_element_node(*self, info.Holder());
        auto result = v8::Array::New(info.GetIsolate(), element == nullptr ? 0 : 1);
        if (element != nullptr) {
            result->Set(
                info.GetIsolate()->GetCurrentContext(),
                0,
                self->wrap_node(*element)).Check();
        }
        info.GetReturnValue().Set(result);
    }

    static void get_document_implementation(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        info.GetReturnValue().Set(self->dom_implementation_object.Get(info.GetIsolate()));
    }

    static void get_document_doctype(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* root = static_cast<dom_node*>(info.Holder()->GetAlignedPointerFromInternalField(
            0,
            v8::kEmbedderDataTypeTagDefault));
        if (root == nullptr) root = &self->active_root();
        auto* container = root->tag == "html" ? root->parent : root;
        if (container != nullptr) {
            for (auto* child : container->children) {
                if (child != nullptr && child->kind == dom_node_kind::document_type) {
                    info.GetReturnValue().Set(self->wrap_node(*child));
                    return;
                }
            }
        }
        info.GetReturnValue().Set(v8::Null(info.GetIsolate()));
    }

    static void get_scrolling_element(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        self->record_feature(
            "dom",
            "Document.scrollingElement",
            "partially-supported",
            "standards-mode documentElement projection onto the native viewport scroll owner",
            "dom-binding");
        auto* root = static_cast<dom_node*>(info.Holder()->GetAlignedPointerFromInternalField(
            0,
            v8::kEmbedderDataTypeTagDefault));
        if (root == nullptr) root = &self->active_root();
        auto* html = root->tag == "html" ? root : nullptr;
        if (html == nullptr) {
            const auto found = std::find_if(
                root->children.begin(),
                root->children.end(),
                [](const auto* child) { return child != nullptr && child->tag == "html"; });
            if (found != root->children.end()) html = *found;
        }
        info.GetReturnValue().Set(self->wrap_node(html == nullptr ? *root : *html));
    }

    static void get_document_location(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        self->record_feature(
            "dom", "Document.links", "supported", {}, "dom-binding");
        auto* root = static_cast<dom_node*>(info.Holder()->GetAlignedPointerFromInternalField(
            0,
            v8::kEmbedderDataTypeTagDefault));
        if (root != nullptr && self->detached_document_roots.contains(root->id)) {
            info.GetReturnValue().Set(v8::Null(info.GetIsolate()));
            return;
        }
        for (const auto& [frame_id, document_value] : self->actual_frame_documents) {
            if (document_value.IsEmpty()
                || !document_value.Get(info.GetIsolate())->StrictEquals(info.Holder())) {
                continue;
            }
            const auto frame_context = self->actual_frame_contexts.find(frame_id);
            if (frame_context == self->actual_frame_contexts.end()
                || frame_context->second.IsEmpty()) break;
            auto local_frame_context = frame_context->second.Get(info.GetIsolate());
            v8::Context::Scope frame_scope(local_frame_context);
            v8::Local<v8::Value> location;
            if (local_frame_context->Global()->Get(
                    local_frame_context,
                    js_string(info.GetIsolate(), "location")).ToLocal(&location)) {
                info.GetReturnValue().Set(location);
            }
            return;
        }
        auto local_context = info.GetIsolate()->GetCurrentContext();
        v8::Local<v8::Value> location;
        if (local_context->Global()->Get(
                local_context,
                js_string(info.GetIsolate(), "location")).ToLocal(&location)) {
            info.GetReturnValue().Set(location);
        }
    }

    static dom_node* document_element_for(
        v8::Isolate* isolate,
        v8::Local<v8::Object> holder)
    {
        auto* self = current(isolate);
        if (self == nullptr) return nullptr;
        auto* root = static_cast<dom_node*>(holder->GetAlignedPointerFromInternalField(
            0,
            v8::kEmbedderDataTypeTagDefault));
        if (root == nullptr) root = &self->active_root();
        if (root->tag == "html") return root;
        if (root->parent != nullptr && root->parent->tag == "html") {
            return root->parent;
        }
        const auto found = std::find_if(
            root->children.begin(),
            root->children.end(),
            [](const auto* child) { return child != nullptr && child->tag == "html"; });
        return found == root->children.end() ? root : *found;
    }

    static void get_document_dir(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* element = document_element_for(info.GetIsolate(), info.Holder());
        const auto authored = element == nullptr
            ? attribute_collection::const_iterator{}
            : element->attributes.find("dir");
        info.GetReturnValue().Set(js_dom_string(
            info.GetIsolate(),
            element == nullptr || authored == element->attributes.end()
                ? std::string{}
                : authored->second));
    }

    static void set_document_dir(
        v8::Local<v8::Name>,
        v8::Local<v8::Value> value,
        const v8::PropertyCallbackInfo<void>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* element = document_element_for(info.GetIsolate(), info.Holder());
        if (self == nullptr || element == nullptr) return;
        const auto direction = to_wtf8(info.GetIsolate(), value);
        if (const auto authored = element->attributes.find("dir");
            authored != element->attributes.end() && authored->second == direction) {
            return;
        }
        element->attributes["dir"] = direction;
        if (self->attribute_requires_recascade("dir")) {
            self->recascade_connected_subtree(*element, "document.dir");
        } else {
            self->document.mark_dirty();
        }
    }

    static void get_document_hidden(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        const auto* self = current(info.GetIsolate());
        const auto* root = static_cast<const dom_node*>(
            info.Holder()->GetAlignedPointerFromInternalField(
                0,
                v8::kEmbedderDataTypeTagDefault));
        info.GetReturnValue().Set(v8::Boolean::New(
            info.GetIsolate(),
            self != nullptr
                && ((root != nullptr
                        && self->hidden_frame_document_roots.contains(root->id))
                    || !self->document_visible)));
    }

    static void get_document_visibility_state(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        const auto* self = current(info.GetIsolate());
        const auto* root = static_cast<const dom_node*>(
            info.Holder()->GetAlignedPointerFromInternalField(
                0,
                v8::kEmbedderDataTypeTagDefault));
        const auto hidden = self != nullptr
            && ((root != nullptr
                    && self->hidden_frame_document_roots.contains(root->id))
                || !self->document_visible);
        info.GetReturnValue().Set(js_string(
            info.GetIsolate(),
            hidden ? "hidden" : "visible"));
    }

    static void get_document_links(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* root = static_cast<dom_node*>(info.Holder()->GetAlignedPointerFromInternalField(
            0,
            v8::kEmbedderDataTypeTagDefault));
        if (root == nullptr) root = &self->active_root();
        auto isolate = info.GetIsolate();
        auto local_context = info.GetIsolate()->GetCurrentContext();
        auto private_key = v8::Private::ForApi(
            isolate,
            js_string(isolate, "WebScene.Document.links"));
        v8::Local<v8::Value> known;
        if (info.Holder()->GetPrivate(local_context, private_key).ToLocal(&known)
            && known->IsObject()) {
            info.GetReturnValue().Set(known);
            return;
        }
        v8::Local<v8::Value> constructor_value;
        if (!local_context->Global()->Get(
                local_context,
                js_string(isolate, "HTMLCollection")).ToLocal(&constructor_value)
            || !constructor_value->IsFunction()) return;
        v8::Local<v8::Value> arguments[]{v8::External::New(
            isolate,
            root,
            v8::kExternalPointerTypeTagDefault)};
        auto collection = constructor_value.As<v8::Function>()
            ->NewInstance(local_context, 1, arguments).ToLocalChecked();
        info.Holder()->SetPrivate(local_context, private_key, collection).Check();
        info.GetReturnValue().Set(collection);
    }

    static void get_document_cookie(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        self->record_feature(
            "dom",
            "Document.cookie",
            "supported",
            "bounded URL-scoped cookie jar with HTTP ingress and script visibility enforcement",
            "dom-binding");
        auto* root = static_cast<dom_node*>(info.Holder()->GetAlignedPointerFromInternalField(
            0,
            v8::kEmbedderDataTypeTagDefault));
        if (root != nullptr && self->detached_document_roots.contains(root->id)) {
            info.GetReturnValue().Set(js_string(info.GetIsolate(), ""));
            return;
        }
        const auto& address = self->current_base_address();
        resource_request_context context{
            WEBSCENE_RESOURCE_INITIATOR_SUBRESOURCE,
            resource_origin(address),
            address,
            WEBSCENE_FETCH_MODE_SAME_ORIGIN,
            WEBSCENE_REQUEST_DESTINATION_NONE,
            "GET", {}, {}, WEBSCENE_FETCH_CREDENTIALS_INCLUDE, {}};
        auto value = self->cookie_header_for(address, context, true);
        info.GetReturnValue().Set(js_string(info.GetIsolate(), value.c_str()));
    }

    static void set_document_cookie(
        v8::Local<v8::Name>,
        v8::Local<v8::Value> raw_value,
        const v8::PropertyCallbackInfo<void>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* root = static_cast<dom_node*>(info.Holder()->GetAlignedPointerFromInternalField(
            0,
            v8::kEmbedderDataTypeTagDefault));
        if (root != nullptr && self->detached_document_roots.contains(root->id)) return;
        self->store_cookie(
            self->current_base_address(),
            to_utf8(info.GetIsolate(), raw_value),
            false);
    }

    static void create_html_document(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        const auto local_context = info.GetIsolate()->GetCurrentContext();
        self->record_feature(
            "dom",
            "DOMImplementation.createHTMLDocument",
            "supported",
            {},
            "dom-api");

        auto& html = self->document.create_element("html");
        auto& head = self->document.create_element("head");
        auto& body = self->document.create_element("body");
        self->document.append_child(html, head);
        self->document.append_child(html, body);
        self->detached_document_roots.insert(html.id);

        if (info.Length() > 0) {
            const auto title_text = to_utf8(info.GetIsolate(), info[0]);
            if (!title_text.empty()) {
                auto& title = self->document.create_element("title");
                auto& text = self->document.create_element("#text");
                text.text_content = title_text;
                self->document.append_child(title, text);
                self->document.append_child(head, title);
            }
        }

        info.GetReturnValue().Set(wrap_detached_document(
            *self, html, local_context, "about:blank"));
    }

    static v8::Local<v8::Object> wrap_detached_document(
        implementation& self,
        dom_node& root,
        v8::Local<v8::Context> local_context,
        const char* address)
    {
        self.detached_document_roots.insert(root.id);
        self.register_detached_subtree(root);
        const auto known = self.detached_documents.find(root.id);
        if (known != self.detached_documents.end() && !known->second.IsEmpty()) {
            return known->second.Get(self.isolate);
        }
        auto detached = self.document_template.Get(self.isolate)
            ->NewInstance(local_context).ToLocalChecked();
        detached->SetAlignedPointerInInternalField(
            0,
            &root,
            v8::kEmbedderDataTypeTagDefault);
        detached->Set(
            local_context,
            js_string(self.isolate, "oninput"),
            v8::Null(self.isolate)).Check();
        self.set_document_address(local_context, detached, address);
        v8::Local<v8::Value> constructor;
        if (local_context->Global()->Get(
                local_context, js_string(self.isolate, "Document"))
                .ToLocal(&constructor)
            && constructor->IsFunction()) {
            v8::Local<v8::Value> prototype;
            if (constructor.As<v8::Function>()->Get(
                    local_context, js_string(self.isolate, "prototype"))
                    .ToLocal(&prototype)
                && prototype->IsObject()) {
                detached->SetPrototype(local_context, prototype).Check();
            }
        }
        auto& retained = self.detached_documents[root.id];
        retained.Reset(self.isolate, detached);
        retained.SetWeak();
        return detached;
    }

    v8::Local<v8::Object> create_empty_document(v8::Local<v8::Context> local_context)
    {
        auto& root = document.create_element("#document-root");
        root.visible = false;
        detached_document_roots.insert(root.id);
        return wrap_detached_document(*this, root, local_context, "about:blank");
    }

    static void document_constructor(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        if (!info.IsConstructCall()) {
            info.GetIsolate()->ThrowException(v8::Exception::TypeError(
                js_string(info.GetIsolate(),
                    "Failed to construct 'Document': Please use the 'new' operator")));
            return;
        }
        auto* self = current(info.GetIsolate());
        if (self != nullptr) {
            info.GetReturnValue().Set(self->create_empty_document(
                info.GetIsolate()->GetCurrentContext()));
        }
    }

    static void clone_document(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        if (self != nullptr) {
            info.GetReturnValue().Set(self->create_empty_document(
                info.GetIsolate()->GetCurrentContext()));
        }
    }

    static void create_xml_document(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        const auto local_context = info.GetIsolate()->GetCurrentContext();
        auto& root = self->document.create_element("#document-root");
        root.visible = false;
        root.xml_mode = true;
        self->detached_document_roots.insert(root.id);
        if (info.Length() > 1) {
            const auto qualified_name = to_utf8(info.GetIsolate(), info[1]);
            if (!qualified_name.empty()) {
                auto& element = self->document.create_element(qualified_name);
                element.xml_mode = true;
                if (info.Length() > 0) {
                    const auto namespace_uri = to_utf8(info.GetIsolate(), info[0]);
                    if (!namespace_uri.empty()) element.set_namespace(namespace_uri);
                }
                self->document.append_child(root, element);
            }
        }
        info.GetReturnValue().Set(wrap_detached_document(
            *self,
            root,
            local_context,
            "about:blank"));
    }

    static void create_element(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto tag = info.Length() > 0 ? to_utf8(info.GetIsolate(), info[0]) : std::string("div");
        auto* document_root = info.This()->InternalFieldCount() > 0
            ? static_cast<dom_node*>(info.This()->GetAlignedPointerFromInternalField(
                0,
                v8::kEmbedderDataTypeTagDefault))
            : nullptr;
        const auto xml_mode = document_root != nullptr && document_root->xml_mode;
        if (!xml_mode) tag = lower_html_name(tag);
        self->record_feature(
            "html",
            "element:" + tag,
            "supported",
            {},
            "html-element");
        auto& node = self->document.create_element(std::move(tag));
        node.xml_mode = xml_mode;
        self->apply_css_rules(node);
        info.GetReturnValue().Set(self->wrap_node(node));
    }

    static void create_element_ns(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto tag = info.Length() > 1 ? to_utf8(info.GetIsolate(), info[1]) : std::string("svg");
        self->record_feature(
            "html",
            "element:" + lower_html_name(tag),
            "supported",
            {},
            "html-element");
        auto& node = self->document.create_element(std::move(tag));
        node.set_namespace(info.Length() > 0
            ? to_utf8(info.GetIsolate(), info[0])
            : "http://www.w3.org/2000/svg");
        self->apply_css_rules(node);
        info.GetReturnValue().Set(self->wrap_node(node));
    }

    static void create_text_node(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto& node = self->document.create_element("#text");
        auto* document_root = info.This()->InternalFieldCount() > 0
            ? static_cast<dom_node*>(info.This()->GetAlignedPointerFromInternalField(
                0,
                v8::kEmbedderDataTypeTagDefault))
            : nullptr;
        node.xml_mode = document_root != nullptr && document_root->xml_mode;
        node.text_content = info.Length() > 0 ? to_wtf8(info.GetIsolate(), info[0]) : std::string{};
        info.GetReturnValue().Set(self->wrap_node(node));
    }

    static void text_constructor(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        if (!info.IsConstructCall()) {
            info.GetIsolate()->ThrowException(v8::Exception::TypeError(
                js_string(info.GetIsolate(),
                    "Failed to construct 'Text': Please use the 'new' operator")));
            return;
        }
        auto* self = current(info.GetIsolate());
        if (self == nullptr) return;
        auto& node = self->document.create_element("#text");
        node.text_content = info.Length() > 0
            ? to_wtf8(info.GetIsolate(), info[0])
            : std::string{};
        info.GetReturnValue().Set(self->wrap_node(node));
    }

    static void create_comment(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto& node = self->document.create_element("#comment");
        auto* document_root = info.This()->InternalFieldCount() > 0
            ? static_cast<dom_node*>(info.This()->GetAlignedPointerFromInternalField(
                0,
                v8::kEmbedderDataTypeTagDefault))
            : nullptr;
        node.xml_mode = document_root != nullptr && document_root->xml_mode;
        node.visible = false;
        node.text_content = info.Length() > 0 ? to_wtf8(info.GetIsolate(), info[0]) : std::string{};
        info.GetReturnValue().Set(self->wrap_node(node));
    }

    static void comment_constructor(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        if (!info.IsConstructCall()) {
            info.GetIsolate()->ThrowException(v8::Exception::TypeError(
                js_string(info.GetIsolate(),
                    "Failed to construct 'Comment': Please use the 'new' operator")));
            return;
        }
        auto* self = current(info.GetIsolate());
        if (self == nullptr) return;
        auto& node = self->document.create_element("#comment");
        node.text_content = info.Length() > 0
            ? to_wtf8(info.GetIsolate(), info[0])
            : std::string{};
        info.GetReturnValue().Set(self->wrap_node(node));
    }

    static void create_processing_instruction(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        if (info.Length() < 2) {
            info.GetIsolate()->ThrowException(v8::Exception::TypeError(
                js_string(info.GetIsolate(),
                    "Document.createProcessingInstruction requires 2 arguments")));
            return;
        }
        auto* self = current(info.GetIsolate());
        if (self == nullptr) return;
        const auto target = to_wtf8(info.GetIsolate(), info[0]);
        const auto data = to_wtf8(info.GetIsolate(), info[1]);
        if (target.empty()) {
            throw_dom_exception(info, "The target is not a valid XML name", "InvalidCharacterError");
            return;
        }
        if (data.find("?>") != std::string::npos) {
            throw_dom_exception(info, "The data contains an invalid processing-instruction terminator", "InvalidCharacterError");
            return;
        }
        auto& node = self->document.create_element("#processing-instruction");
        node.set_namespace({}, target);
        node.text_content = data;
        info.GetReturnValue().Set(self->wrap_node(node));
    }

    static attr_state* unwrap_attr(v8::Local<v8::Object> object)
    {
        if (object.IsEmpty() || object->InternalFieldCount() < 2) return nullptr;
        const auto value = object->GetInternalField(1).As<v8::Value>();
        return value->IsExternal()
            ? static_cast<attr_state*>(value.As<v8::External>()->Value(
                v8::kExternalPointerTypeTagDefault))
            : nullptr;
    }

    static std::string attribute_attachment_key(const dom_node& owner, std::string_view name)
    {
        return std::to_string(owner.id) + ":" + std::string(name);
    }

    v8::Local<v8::Object> wrap_attr(attr_state& state)
    {
        const auto known = attr_wrappers.find(state.id);
        if (known != attr_wrappers.end() && !known->second.IsEmpty()) {
            return known->second.Get(isolate);
        }
        auto object = attr_template.Get(isolate)->InstanceTemplate()
            ->NewInstance(isolate->GetCurrentContext()).ToLocalChecked();
        object->SetAlignedPointerInInternalField(
            0, nullptr, v8::kEmbedderDataTypeTagDefault);
        object->SetInternalField(1, v8::External::New(
            isolate, &state, v8::kExternalPointerTypeTagDefault));
        attr_wrappers.emplace(state.id, v8::Global<v8::Object>(isolate, object));
        return object;
    }

    attr_state& create_attr_state(
        std::string name,
        std::string namespace_uri = {},
        dom_node* owner = nullptr,
        std::string value = {})
    {
        auto state = std::make_unique<attr_state>();
        state->id = next_attr_id++;
        state->namespace_uri = std::move(namespace_uri);
        state->name = std::move(name);
        state->value = std::move(value);
        state->owner = owner;
        auto* result = state.get();
        attr_states.emplace(result->id, std::move(state));
        if (owner != nullptr) {
            attached_attributes[attribute_attachment_key(*owner, result->name)] = result;
        }
        return *result;
    }

    attr_state& attached_attr(dom_node& owner, std::string_view name)
    {
        const auto key = attribute_attachment_key(owner, name);
        const auto known = attached_attributes.find(key);
        if (known != attached_attributes.end()) return *known->second;
        const auto value = owner.attributes.find(name);
        return create_attr_state(
            std::string(name), std::string(owner.attributes.namespace_uri(name)), &owner,
            value == owner.attributes.end() ? std::string{} : value->second);
    }

    void detach_attr(dom_node& owner, std::string_view name, std::string value)
    {
        const auto key = attribute_attachment_key(owner, name);
        const auto known = attached_attributes.find(key);
        if (known == attached_attributes.end()) return;
        known->second->value = std::move(value);
        known->second->owner = nullptr;
        attached_attributes.erase(known);
    }

    static void create_attribute_with_namespace(
        const v8::FunctionCallbackInfo<v8::Value>& info,
        bool namespaced)
    {
        auto* self = current(info.GetIsolate());
        if (self == nullptr) return;
        const auto name_index = namespaced ? 1 : 0;
        if (info.Length() <= name_index) {
            info.GetIsolate()->ThrowException(v8::Exception::TypeError(
                js_string(info.GetIsolate(), "Attribute creation requires a name")));
            return;
        }
        auto name = to_utf8(info.GetIsolate(), info[name_index]);
        auto* document_root = info.This()->InternalFieldCount() > 0
            ? static_cast<dom_node*>(info.This()->GetAlignedPointerFromInternalField(
                0, v8::kEmbedderDataTypeTagDefault))
            : nullptr;
        if (!namespaced && (document_root == nullptr || !document_root->xml_mode)) {
            name = lower_html_name(name);
        }
        const auto namespace_uri = namespaced && !info[0]->IsNullOrUndefined()
            ? to_wtf8(info.GetIsolate(), info[0])
            : std::string{};
        auto& state = self->create_attr_state(std::move(name), namespace_uri);
        self->record_feature(
            "dom",
            namespaced ? "Document.createAttributeNS" : "Document.createAttribute",
            "supported", {}, "dom-api");
        info.GetReturnValue().Set(self->wrap_attr(state));
    }

    static void create_attribute(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        create_attribute_with_namespace(info, false);
    }

    static void create_attribute_ns(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        create_attribute_with_namespace(info, true);
    }

    static void get_attr_name(
        v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        const auto* state = unwrap_attr(info.Holder());
        if (state != nullptr) info.GetReturnValue().Set(js_dom_string(info.GetIsolate(), state->name));
    }

    static void get_attr_namespace_uri(
        v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        const auto* state = unwrap_attr(info.Holder());
        info.GetReturnValue().Set(state == nullptr || state->namespace_uri.empty()
            ? v8::Local<v8::Value>(v8::Null(info.GetIsolate()))
            : v8::Local<v8::Value>(js_dom_string(info.GetIsolate(), state->namespace_uri)));
    }

    static void get_attr_value(
        v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        const auto* state = unwrap_attr(info.Holder());
        if (state == nullptr) return;
        auto value = state->value;
        if (state->owner != nullptr) {
            const auto match = state->owner->attributes.find(state->name);
            if (match != state->owner->attributes.end()) value = match->second;
        }
        info.GetReturnValue().Set(js_dom_string(info.GetIsolate(), value));
    }

    static void set_attr_value(
        v8::Local<v8::Name>, v8::Local<v8::Value> value,
        const v8::PropertyCallbackInfo<void>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* state = unwrap_attr(info.Holder());
        if (self == nullptr || state == nullptr) return;
        const auto text = to_wtf8(info.GetIsolate(), value);
        if (state->owner == nullptr) {
            state->value = text;
            return;
        }
        const auto old_value = state->owner->attributes.contains(state->name)
            ? std::optional<std::string>(state->owner->attributes.at(state->name))
            : std::nullopt;
        const auto radio_group_state=state->namespace_uri.empty()
            && self->radio_group_attribute_may_change_membership(
                *state->owner,state->name)
            ? self->capture_radio_group_selector_state()
            : std::vector<radio_group_selector_state>{};
        state->owner->attributes[state->name] = text;
        if (state->namespace_uri.empty()
            && state->owner->tag == "base" && state->name == "href") {
            self->invalidate_document_base(*state->owner);
        }
        if (state->namespace_uri.empty() && (state->name == "width" || state->name == "height"))
            reset_canvas_backing_store(*self, *state->owner);
        if (state->name == "id") state->owner->id_attribute = text;
        else if (state->name == "class") state->owner->class_name = text;
        if(state->namespace_uri.empty() && self->attribute_requires_recascade(state->name))
            self->recascade_attribute_selector_transition(
                *state->owner,state->name,old_value,text);
        else self->document.mark_dirty();
        self->recascade_radio_group_selector_state_changes(radio_group_state);
        if(state->namespace_uri.empty())
            self->recascade_form_attribute_dependencies(
                *state->owner,state->name,old_value,text);
        if(state->namespace_uri.empty() && state->owner->tag=="option"
            && (state->name=="selected" || state->name=="value"))
            self->recascade_containing_select_validity(*state->owner);
        self->notify_custom_element_attribute(
            *state->owner, state->name, old_value, text,
            state->namespace_uri.empty()
                ? std::nullopt
                : std::optional<std::string>(state->namespace_uri));
    }

    static void get_attr_owner_element(
        v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        const auto* state = unwrap_attr(info.Holder());
        info.GetReturnValue().Set(state == nullptr || state->owner == nullptr
            ? v8::Local<v8::Value>(v8::Null(info.GetIsolate()))
            : v8::Local<v8::Value>(self->wrap_node(*state->owner)));
    }

    static void get_attr_node_type(
        v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        info.GetReturnValue().Set(v8::Integer::New(info.GetIsolate(), 2));
    }

    static void get_attr_owner_document(
        v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        info.GetReturnValue().Set(self->document_object.Get(info.GetIsolate()));
    }

    static void get_attr_specified(
        v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        info.GetReturnValue().Set(v8::True(info.GetIsolate()));
    }

    static void set_attribute_node(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* owner = unwrap_node(info.This());
        auto* state = info.Length() > 0 && info[0]->IsObject()
            ? unwrap_attr(info[0].As<v8::Object>())
            : nullptr;
        if (self == nullptr || owner == nullptr || state == nullptr) {
            info.GetIsolate()->ThrowException(v8::Exception::TypeError(
                js_string(info.GetIsolate(), "setAttributeNode requires an Attr")));
            return;
        }
        if (state->owner != nullptr && state->owner != owner) {
            throw_dom_exception(info, "The attribute is already in use", "InUseAttributeError");
            return;
        }
        attr_state* previous = nullptr;
        if (owner->attributes.contains(state->name)) {
            previous = &self->attached_attr(*owner, state->name);
        }
        const auto value = state->owner == nullptr
            ? state->value
            : owner->attributes.at(state->name);
        const auto old_value = owner->attributes.contains(state->name)
            ? std::optional<std::string>(owner->attributes.at(state->name))
            : std::nullopt;
        const auto radio_group_state=state->namespace_uri.empty()
            && self->radio_group_attribute_may_change_membership(
                *owner,state->name)
            ? self->capture_radio_group_selector_state()
            : std::vector<radio_group_selector_state>{};
        if (state->namespace_uri.empty()) {
            owner->attributes[state->name] = value;
        } else {
            const auto colon = state->name.find(':');
            owner->attributes.set_namespaced(
                state->name,
                colon == std::string::npos
                    ? state->name : state->name.substr(colon + 1U),
                state->namespace_uri,
                value);
        }
        if (state->namespace_uri.empty()
            && owner->tag == "base" && state->name == "href") {
            self->invalidate_document_base(*owner);
        }
        if (state->namespace_uri.empty() && (state->name == "width" || state->name == "height"))
            reset_canvas_backing_store(*self, *owner);
        if (state->name == "id") owner->id_attribute = value;
        else if (state->name == "class") owner->class_name = value;
        if(state->namespace_uri.empty() && self->attribute_requires_recascade(state->name))
            self->recascade_attribute_selector_transition(
                *owner,state->name,old_value,value);
        else self->document.mark_dirty();
        self->recascade_radio_group_selector_state_changes(radio_group_state);
        if(state->namespace_uri.empty())
            self->recascade_form_attribute_dependencies(
                *owner,state->name,old_value,value);
        if(state->namespace_uri.empty() && owner->tag=="option"
            && (state->name=="selected" || state->name=="value"))
            self->recascade_containing_select_validity(*owner);
        self->notify_custom_element_attribute(
            *owner, state->name, old_value, value,
            state->namespace_uri.empty()
                ? std::nullopt
                : std::optional<std::string>(state->namespace_uri));
        if (previous != nullptr && previous != state) {
            previous->value = old_value.value_or(std::string{});
            previous->owner = nullptr;
        }
        state->owner = owner;
        self->attached_attributes[attribute_attachment_key(*owner, state->name)] = state;
        info.GetReturnValue().Set(previous == nullptr || previous == state
            ? v8::Local<v8::Value>(v8::Null(info.GetIsolate()))
            : v8::Local<v8::Value>(self->wrap_attr(*previous)));
    }

    static void remove_attribute_node(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* owner = unwrap_node(info.This());
        auto* state = info.Length() > 0 && info[0]->IsObject()
            ? unwrap_attr(info[0].As<v8::Object>())
            : nullptr;
        if (self == nullptr || owner == nullptr || state == nullptr
            || state->owner != owner || !owner->attributes.contains(state->name)) {
            throw_dom_exception(info, "The attribute was not found", "NotFoundError");
            return;
        }
        const auto old_value = owner->attributes.at(state->name);
        const auto radio_group_state=state->namespace_uri.empty()
            && self->radio_group_attribute_may_change_membership(
                *owner,state->name)
            ? self->capture_radio_group_selector_state()
            : std::vector<radio_group_selector_state>{};
        owner->attributes.erase(state->name);
        if (state->namespace_uri.empty()
            && owner->tag == "base" && state->name == "href") {
            self->invalidate_document_base(*owner);
        }
        if (state->namespace_uri.empty() && (state->name == "width" || state->name == "height"))
            reset_canvas_backing_store(*self, *owner);
        if (state->name == "id") owner->id_attribute.clear();
        else if (state->name == "class") owner->class_name.clear();
        state->value = old_value;
        state->owner = nullptr;
        self->attached_attributes.erase(attribute_attachment_key(*owner, state->name));
        if(state->namespace_uri.empty() && self->attribute_requires_recascade(state->name))
            self->recascade_attribute_selector_transition(
                *owner,state->name,old_value,std::nullopt);
        else self->document.mark_dirty();
        self->recascade_radio_group_selector_state_changes(radio_group_state);
        if(state->namespace_uri.empty())
            self->recascade_form_attribute_dependencies(
                *owner,state->name,old_value,std::nullopt);
        if(state->namespace_uri.empty() && owner->tag=="option"
            && (state->name=="selected" || state->name=="value"))
            self->recascade_containing_select_validity(*owner);
        self->notify_custom_element_attribute(
            *owner, state->name, old_value, std::nullopt,
            state->namespace_uri.empty()
                ? std::nullopt
                : std::optional<std::string>(state->namespace_uri));
        info.GetReturnValue().Set(info[0]);
    }

    static void create_document_fragment(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto& node = self->document.create_element("#document-fragment");
        auto* document_root = info.This()->InternalFieldCount() > 0
            ? static_cast<dom_node*>(info.This()->GetAlignedPointerFromInternalField(
                0,
                v8::kEmbedderDataTypeTagDefault))
            : nullptr;
        node.xml_mode = document_root != nullptr && document_root->xml_mode;
        node.visible = false;
        info.GetReturnValue().Set(self->wrap_node(node));
    }

    static dom_node& clone_dom_subtree(
        implementation& self,
        const dom_node& original,
        bool deep)
    {
        auto& clone = self.document.create_node(original.kind, original.tag);
        clone.set_namespace(
            original.namespace_uri(),
            original.namespace_prefix());
        clone.id_attribute = original.id_attribute;
        clone.class_name = original.class_name;
        clone.text_content = original.text_content;
        clone.script_state = original.script_state;
        clone.xml_mode = original.xml_mode;
        if (original.has_form_control()) {
            clone.mutable_form_control() = original.form_control();
        }
        if (original.replaced_image().complete) {
            // Cloning a decoded image preserves its current request state.
            // Component renderers commonly decode an off-tree image and then
            // clone it into the visible subtree; dropping this state leaves
            // the clone permanently at its fallback background.
            clone.mutable_replaced_image() = original.replaced_image();
        }
        clone.attributes = original.attributes;
        clone.style = original.style;
        if (original.has_authored_style()) {
            clone.mutable_authored_style() = original.authored_style();
        }
        clone.visible = true;
        if (deep) {
            for (const auto* child : original.children) {
                if (child == nullptr) continue;
                auto& child_clone = clone_dom_subtree(self, *child, true);
                self.document.append_child(clone, child_clone);
            }
            if (original.template_contents != nullptr) {
                auto& clone_contents = self.document.parser_template_contents(clone);
                for (const auto* child : original.template_contents->children) {
                    if (child == nullptr) continue;
                    auto& child_clone = clone_dom_subtree(self, *child, true);
                    self.document.append_child(clone_contents, child_clone);
                }
            }
        }
        return clone;
    }

    static void clone_node(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* source = unwrap_node(info.This());
        if (source == nullptr) {
            info.GetIsolate()->ThrowException(v8::Exception::TypeError(
                js_string(info.GetIsolate(), "cloneNode requires a native DOM node")));
            return;
        }
        const auto deep = info.Length() > 0 && info[0]->BooleanValue(info.GetIsolate());
        auto& clone = clone_dom_subtree(*self, *source, deep);
        self->notify_custom_elements_subtree(clone, "connected");
        info.GetReturnValue().Set(self->wrap_node(clone));
    }

    static void document_import_node(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* source = info.Length() > 0 && info[0]->IsObject()
            ? unwrap_node(info[0].As<v8::Object>())
            : nullptr;
        if (self == nullptr || source == nullptr) {
            info.GetIsolate()->ThrowException(v8::Exception::TypeError(
                js_string(info.GetIsolate(), "importNode requires a native DOM node")));
            return;
        }
        const auto deep = info.Length() > 1
            && info[1]->BooleanValue(info.GetIsolate());
        auto& clone = clone_dom_subtree(*self, *source, deep);
        info.GetReturnValue().Set(self->wrap_node(clone));
    }

    static void get_element_by_id(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        const auto id = info.Length() > 0 ? to_wtf8(info.GetIsolate(), info[0]) : std::string{};
        auto* root = static_cast<dom_node*>(info.This()->GetAlignedPointerFromInternalField(
            0,
            v8::kEmbedderDataTypeTagDefault));
        auto* node = self->find_element_by_id(
            root == nullptr ? self->active_root() : *root,
            id);
        if (node == nullptr) {
            info.GetReturnValue().Set(v8::Null(info.GetIsolate()));
        } else {
            info.GetReturnValue().Set(self->wrap_node(*node));
        }
    }

    static void document_query_selector_all(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(self->profile(binding_category::dom_query));
        const auto selector = info.Length() > 0 ? to_wtf8(info.GetIsolate(), info[0]) : std::string{};
        if (!self->is_valid_dom_selector_list(selector)) {
            throw_selector_syntax_error(info, selector);
            return;
        }
        auto* root = static_cast<dom_node*>(info.This()->GetAlignedPointerFromInternalField(
            0,
            v8::kEmbedderDataTypeTagDefault));
        auto& query_root = root == nullptr ? self->active_root() : *root;
        const auto include_root = &query_root != &self->document.body()
            || std::none_of(
                query_root.children.begin(),
                query_root.children.end(),
                [](const auto* child) { return child != nullptr && child->tag == "html"; });
        info.GetReturnValue().Set(self->selector_results(
            query_root,
            selector,
            include_root));
    }

    static void document_query_selector(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(self->profile(binding_category::dom_query));
        const auto selector = info.Length() > 0 ? to_wtf8(info.GetIsolate(), info[0]) : std::string{};
        if (!self->is_valid_dom_selector_list(selector)) {
            throw_selector_syntax_error(info, selector);
            return;
        }
        auto* root = static_cast<dom_node*>(info.This()->GetAlignedPointerFromInternalField(
            0,
            v8::kEmbedderDataTypeTagDefault));
        auto& query_root = root == nullptr ? self->active_root() : *root;
        const auto include_root = &query_root != &self->document.body()
            || std::none_of(
                query_root.children.begin(),
                query_root.children.end(),
                [](const auto* child) { return child != nullptr && child->tag == "html"; });
        auto* match = self->query_selector_node(query_root, selector, include_root);
        info.GetReturnValue().Set(match == nullptr
            ? v8::Local<v8::Value>(v8::Null(info.GetIsolate()))
            : v8::Local<v8::Value>(self->wrap_node(*match)));
    }

    static void document_element_from_point(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        self->ensure_layout();
        auto* root = static_cast<dom_node*>(info.This()->GetAlignedPointerFromInternalField(
            0,
            v8::kEmbedderDataTypeTagDefault));
        const auto local_context = info.GetIsolate()->GetCurrentContext();
        const auto x = info.Length() > 0
            ? static_cast<float>(info[0]->NumberValue(local_context).FromMaybe(0)) : 0;
        const auto y = info.Length() > 1
            ? static_cast<float>(info[1]->NumberValue(local_context).FromMaybe(0)) : 0;
        auto* node = self->document.hit_test(
            root == nullptr ? self->active_root() : *root,
            x,
            y);
        info.GetReturnValue().Set(node == nullptr
            ? v8::Local<v8::Value>(v8::Null(info.GetIsolate()))
            : v8::Local<v8::Value>(self->wrap_node(*node)));
    }

    static void document_caret_range_from_point(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        self->ensure_layout();
        auto* root = static_cast<dom_node*>(info.This()->GetAlignedPointerFromInternalField(
            0,
            v8::kEmbedderDataTypeTagDefault));
        const auto local_context = info.GetIsolate()->GetCurrentContext();
        const auto x = info.Length() > 0
            ? static_cast<float>(info[0]->NumberValue(local_context).FromMaybe(0)) : 0;
        const auto y = info.Length() > 1
            ? static_cast<float>(info[1]->NumberValue(local_context).FromMaybe(0)) : 0;
        auto* hit = self->document.hit_test(
            root == nullptr ? self->active_root() : *root,
            x,
            y);
        if (hit == nullptr) {
            info.GetReturnValue().Set(v8::Null(info.GetIsolate()));
            return;
        }

        const auto find_text_node = [&](const auto& recurse, dom_node& node) -> dom_node* {
            if (node.tag == "#text" && !node.text_content.empty()) return &node;
            dom_node* fallback = nullptr;
            for (auto* child : node.children) {
                if (child == nullptr) continue;
                auto* candidate = recurse(recurse, *child);
                if (candidate == nullptr) continue;
                if (fallback == nullptr) fallback = candidate;
                const auto& box = candidate->layout;
                if (x >= box.x && x <= box.x + box.width
                    && y >= box.y && y <= box.y + box.height) {
                    return candidate;
                }
            }
            return fallback;
        };
        auto* text = find_text_node(find_text_node, *hit);
        auto* container = text == nullptr ? hit : text;
        const auto offset = text == nullptr
            ? 0U
            : self->document.text_caret_offset_at_x(*text, x);

        auto range = v8::Object::New(info.GetIsolate());
        range->Set(
            local_context,
            js_string(info.GetIsolate(), "startContainer"),
            self->wrap_node(*container)).Check();
        range->Set(
            local_context,
            js_string(info.GetIsolate(), "startOffset"),
            v8::Integer::NewFromUnsigned(
                info.GetIsolate(),
                static_cast<uint32_t>(offset))).Check();
        range->Set(
            local_context,
            js_string(info.GetIsolate(), "endContainer"),
            self->wrap_node(*container)).Check();
        range->Set(
            local_context,
            js_string(info.GetIsolate(), "endOffset"),
            v8::Integer::NewFromUnsigned(
                info.GetIsolate(),
                static_cast<uint32_t>(offset))).Check();
        range->Set(
            local_context,
            js_string(info.GetIsolate(), "collapsed"),
            v8::True(info.GetIsolate())).Check();
        info.GetReturnValue().Set(range);
    }

    static void document_get_elements_by_tag_name(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        const auto tag = info.Length() > 0 ? to_utf8(info.GetIsolate(), info[0]) : std::string{};
        auto* root = static_cast<dom_node*>(info.This()->GetAlignedPointerFromInternalField(
            0,
            v8::kEmbedderDataTypeTagDefault));
        auto& query_root = root == nullptr ? self->active_root() : *root;
        const auto include_root = &query_root != &self->document.body()
            || std::none_of(
                query_root.children.begin(),
                query_root.children.end(),
                [](const auto* child) { return child != nullptr && child->tag == "html"; });
        info.GetReturnValue().Set(self->selector_results(query_root, tag, include_root));
    }

    static void document_get_elements_by_class_name(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(self->profile(binding_category::dom_query));
        const auto names = info.Length() > 0 ? to_utf8(info.GetIsolate(), info[0]) : std::string{};
        auto* root = static_cast<dom_node*>(info.This()->GetAlignedPointerFromInternalField(
            0,
            v8::kEmbedderDataTypeTagDefault));
        info.GetReturnValue().Set(self->class_results(
            root == nullptr ? self->active_root() : *root,
            names));
    }

    static void document_get_elements_by_name(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(self->profile(binding_category::dom_query));
        const auto name = info.Length() > 0
            ? to_wtf8(info.GetIsolate(), info[0])
            : std::string{};
        auto* root = static_cast<dom_node*>(info.This()->GetAlignedPointerFromInternalField(
            0,
            v8::kEmbedderDataTypeTagDefault));
        auto& query_root = root == nullptr ? self->active_root() : *root;
        std::vector<dom_node*> matches;
        const auto collect = [&](const auto& visit, dom_node& parent) -> void {
            for (auto* child : parent.children) {
                if (child == nullptr) continue;
                if (child->kind == dom_node_kind::element) {
                    const auto authored_name = child->attributes.find("name");
                    if (authored_name != child->attributes.end()
                        && authored_name->second == name) {
                        matches.push_back(child);
                    }
                }
                visit(visit, *child);
            }
        };
        if (query_root.kind == dom_node_kind::element) {
            const auto authored_name = query_root.attributes.find("name");
            if (authored_name != query_root.attributes.end()
                && authored_name->second == name) {
                matches.push_back(&query_root);
            }
        }
        collect(collect, query_root);
        const auto local_context = info.GetIsolate()->GetCurrentContext();
        auto result = v8::Array::New(
            info.GetIsolate(), static_cast<int>(matches.size()));
        for (uint32_t index = 0; index < matches.size(); ++index) {
            result->Set(
                local_context, index,
                self->wrap_node(*matches[index])).Check();
        }
        info.GetReturnValue().Set(result);
    }

    static void element_query_selector_all(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(self->profile(binding_category::dom_query));
        auto* root = unwrap_node(info.This());
        if (root == nullptr) return;
        const auto selector = info.Length() > 0 ? to_wtf8(info.GetIsolate(), info[0]) : std::string{};
        if (!self->is_valid_dom_selector_list(selector)) {
            throw_selector_syntax_error(info, selector);
            return;
        }
        info.GetReturnValue().Set(self->selector_results(*root, selector, false));
    }

    static void element_query_selector(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(self->profile(binding_category::dom_query));
        auto* root = unwrap_node(info.This());
        if (root == nullptr) return;
        const auto selector = info.Length() > 0 ? to_wtf8(info.GetIsolate(), info[0]) : std::string{};
        if (!self->is_valid_dom_selector_list(selector)) {
            throw_selector_syntax_error(info, selector);
            return;
        }
        auto* match = self->query_selector_node(*root, selector, false);
        info.GetReturnValue().Set(match == nullptr
            ? v8::Local<v8::Value>(v8::Null(info.GetIsolate()))
            : v8::Local<v8::Value>(self->wrap_node(*match)));
    }

    static void element_get_elements_by_tag_name(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* root = unwrap_node(info.This());
        if (root == nullptr) return;
        const auto tag = info.Length() > 0 ? to_utf8(info.GetIsolate(), info[0]) : std::string{};
        info.GetReturnValue().Set(self->selector_results(*root, tag, false));
    }

    static void element_get_elements_by_class_name(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(self->profile(binding_category::dom_query));
        auto* root = unwrap_node(info.This());
        if (root == nullptr) return;
        const auto names = info.Length() > 0 ? to_utf8(info.GetIsolate(), info[0]) : std::string{};
        info.GetReturnValue().Set(self->class_results(*root, names));
    }

    static void element_contains(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* root = unwrap_node(info.This());
        auto* candidate = info.Length() > 0 && info[0]->IsObject()
            ? unwrap_node(info[0].As<v8::Object>())
            : nullptr;
        for (auto* node = candidate; root != nullptr && node != nullptr;
            node = self->document.dom_parent(*node)) {
            if (node == root) {
                info.GetReturnValue().Set(v8::True(info.GetIsolate()));
                return;
            }
        }
        info.GetReturnValue().Set(v8::False(info.GetIsolate()));
    }

    static void document_contains(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        if (self == nullptr || info.Length() < 1 || !info[0]->IsObject()) {
            info.GetReturnValue().Set(v8::False(info.GetIsolate()));
            return;
        }
        if (info[0].As<v8::Object>()->StrictEquals(info.This())) {
            info.GetReturnValue().Set(v8::True(info.GetIsolate()));
            return;
        }
        auto* candidate = unwrap_node(info[0].As<v8::Object>());
        auto* root = info.This()->InternalFieldCount() > 0
            ? static_cast<dom_node*>(info.This()->GetAlignedPointerFromInternalField(
                0,
                v8::kEmbedderDataTypeTagDefault))
            : nullptr;
        if (root == nullptr) root = &self->active_root();
        for (auto* node = candidate; node != nullptr;
            node = self->document.dom_parent(*node)) {
            if (node == root) {
                info.GetReturnValue().Set(v8::True(info.GetIsolate()));
                return;
            }
        }
        info.GetReturnValue().Set(v8::False(info.GetIsolate()));
    }

    static void element_compare_document_position(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* reference = unwrap_node(info.This());
        auto* other = info.Length() > 0 && info[0]->IsObject()
            ? unwrap_node(info[0].As<v8::Object>())
            : nullptr;
        const auto main_document = self->document_object.Get(info.GetIsolate());
        const auto reference_is_document = !main_document.IsEmpty()
            && info.This()->StrictEquals(main_document);
        const auto other_is_document = !main_document.IsEmpty()
            && info.Length() > 0
            && info[0]->StrictEquals(main_document);
        if (reference_is_document != other_is_document) {
            auto* candidate = reference_is_document ? other : reference;
            auto connected = false;
            for (auto* ancestor = candidate; ancestor != nullptr;
                ancestor = self->document.dom_parent(*ancestor)) {
                if (ancestor == &self->active_root()) {
                    connected = true;
                    break;
                }
            }
            if (connected) {
                info.GetReturnValue().Set(v8::Integer::New(
                    info.GetIsolate(), reference_is_document ? 4 | 16 : 2 | 8));
                return;
            }
        }
        if (reference == other && reference != nullptr) {
            info.GetReturnValue().Set(v8::Integer::New(info.GetIsolate(), 0));
            return;
        }
        if (reference == nullptr || other == nullptr) {
            info.GetReturnValue().Set(v8::Integer::New(info.GetIsolate(), 1 | 32));
            return;
        }

        const auto build_path = [self](dom_node* node) {
            std::vector<dom_node*> path;
            for (; node != nullptr; node = self->document.dom_parent(*node)) {
                path.push_back(node);
            }
            std::reverse(path.begin(), path.end());
            return path;
        };
        const auto reference_path = build_path(reference);
        const auto other_path = build_path(other);
        if (reference_path.empty() || other_path.empty()
            || reference_path.front() != other_path.front()) {
            const auto order = other->id < reference->id ? 2 : 4;
            info.GetReturnValue().Set(v8::Integer::New(info.GetIsolate(), 1 | 32 | order));
            return;
        }

        size_t shared = 0U;
        const auto maximum_shared = std::min(reference_path.size(), other_path.size());
        while (shared < maximum_shared && reference_path[shared] == other_path[shared]) {
            ++shared;
        }
        if (shared == other_path.size()) {
            info.GetReturnValue().Set(v8::Integer::New(info.GetIsolate(), 2 | 8));
            return;
        }
        if (shared == reference_path.size()) {
            info.GetReturnValue().Set(v8::Integer::New(info.GetIsolate(), 4 | 16));
            return;
        }

        auto* parent = reference_path[shared - 1U];
        auto* reference_child = reference_path[shared];
        auto* other_child = other_path[shared];
        for (auto* child : parent->children) {
            if (child == other_child) {
                info.GetReturnValue().Set(v8::Integer::New(info.GetIsolate(), 2));
                return;
            }
            if (child == reference_child) {
                info.GetReturnValue().Set(v8::Integer::New(info.GetIsolate(), 4));
                return;
            }
        }
        info.GetReturnValue().Set(v8::Integer::New(info.GetIsolate(), 1 | 32));
    }

    static void element_matches(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* node = unwrap_node(info.This());
        if (node == nullptr || info.Length() < 1) {
            info.GetReturnValue().Set(v8::False(info.GetIsolate()));
            return;
        }
        const auto selector = to_wtf8(info.GetIsolate(), info[0]);
        if (!self->is_valid_dom_selector_list(selector)) {
            throw_selector_syntax_error(info, selector);
            return;
        }
        const auto compiled = self->cached_css_selector_list(selector);
        info.GetReturnValue().Set(v8::Boolean::New(
            info.GetIsolate(),
            self->compiled_css_selector_list_matches(
                *node,
                *compiled,
                node)));
    }

    static bool nodes_are_equal(const dom_node& left, const dom_node& right)
    {
        if (left.tag != right.tag
            || left.text_content != right.text_content
            || left.attributes != right.attributes
            || left.children.size() != right.children.size()) return false;
        for (size_t index = 0; index < left.children.size(); ++index) {
            const auto* left_child = left.children[index];
            const auto* right_child = right.children[index];
            if (left_child == nullptr || right_child == nullptr) {
                if (left_child != right_child) return false;
                continue;
            }
            if (!nodes_are_equal(*left_child, *right_child)) return false;
        }
        return true;
    }

    static void element_is_same_node(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* other = info.Length() > 0 && info[0]->IsObject()
            ? unwrap_node(info[0].As<v8::Object>())
            : nullptr;
        info.GetReturnValue().Set(v8::Boolean::New(
            info.GetIsolate(),
            unwrap_node(info.This()) == other && other != nullptr));
    }

    static void element_is_equal_node(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* node = unwrap_node(info.This());
        auto* other = info.Length() > 0 && info[0]->IsObject()
            ? unwrap_node(info[0].As<v8::Object>())
            : nullptr;
        info.GetReturnValue().Set(v8::Boolean::New(
            info.GetIsolate(),
            node != nullptr && other != nullptr && nodes_are_equal(*node, *other)));
    }

    static void element_closest(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* node = unwrap_node(info.This());
        if (node == nullptr || info.Length() < 1) {
            info.GetReturnValue().Set(v8::Null(info.GetIsolate()));
            return;
        }
        const auto selector = to_wtf8(info.GetIsolate(), info[0]);
        if (!self->is_valid_dom_selector_list(selector)) {
            throw_selector_syntax_error(info, selector);
            return;
        }
        const auto compiled = self->cached_css_selector_list(selector);
        auto* document_root = self->css_cascade_root_for_node(*node);
        for (auto* candidate = node; candidate != nullptr;
            candidate = self->document.dom_parent(*candidate)) {
            if (self->compiled_css_selector_list_matches(
                    *candidate,
                    *compiled)) {
                info.GetReturnValue().Set(self->wrap_node(*candidate));
                return;
            }
            // Retained layout nests a child document below its iframe owner,
            // but DOM ancestry APIs remain scoped to the node's Document.
            // In particular, Code OSS repeatedly calls closest() while
            // collecting data-vscode-context values for a webview menu.
            if (candidate == document_root) break;
        }
        info.GetReturnValue().Set(v8::Null(info.GetIsolate()));
    }

    static void element_focus(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* node = unwrap_node(info.This());
        if (self->is_programmatically_focusable(node)) {
            webscene_input_event synthetic{};
            self->set_active_element(node, synthetic);
        }
    }

    static void element_blur(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* node = unwrap_node(info.This());
        if (self->active_element == node) {
            webscene_input_event synthetic{};
            self->set_active_element(nullptr, synthetic);
        }
    }

    static void element_click(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* node = unwrap_node(info.This());
        if (node == nullptr || node->attributes.contains("disabled")) return;
        checkable_click_activation checkable_activation;
        auto form_control_changed = false;
        if (node->tag == "input") {
            if (!self->begin_checkable_click_activation(*node, checkable_activation)) {
                form_control_changed =
                    self->apply_non_checkable_form_control_click_default(*node);
            }
        } else if (node->tag == "option") {
            if (node->parent != nullptr) {
                for (auto* sibling : node->parent->children) {
                    if (sibling != nullptr && sibling->tag == "option") {
                        sibling->attributes.erase("selected");
                    }
                }
            }
            node->attributes["selected"] = std::string{};
            self->recascade_connected_subtree(*node, "id-property");
        }
        webscene_input_event synthetic{};
        synthetic.kind = WEBSCENE_INPUT_POINTER_UP;
        synthetic.x = node->layout.x + node->layout.width / 2.0;
        synthetic.y = node->layout.y + node->layout.height / 2.0;
        bool default_prevented = false;
        if (self->dispatch_input_event_type(
                synthetic,
                "click",
                *node,
                &default_prevented,
                nullptr,
                false)) {
            if (checkable_activation.active) {
                if (default_prevented) {
                    self->restore_checkable_click_activation(checkable_activation);
                } else {
                    form_control_changed = checkable_activation.changed;
                }
            }
            if (!default_prevented && self->is_submit_button(node)) {
                if(auto* form=self->form_owner(*node);form!=nullptr)
                    static_cast<void>(self->dispatch_form_submit(
                        synthetic,*form,node));
            } else if (!default_prevented) {
                static_cast<void>(self->apply_details_summary_click_default(
                    *node, synthetic));
                self->queue_external_navigation(*node);
            }
            if (form_control_changed && self->is_connected(*node)) {
                self->dispatch_input_event_type(
                    synthetic, "input", *node, nullptr, nullptr, false);
                self->dispatch_input_event_type(
                    synthetic, "change", *node, nullptr, nullptr, false);
            }
        }
    }

    static void element_set_selection_range(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto local_context = info.GetIsolate()->GetCurrentContext();
        const auto start = info.Length() > 0 && info[0]->IsNumber()
            ? info[0]->Int32Value(local_context).FromMaybe(0)
            : 0;
        const auto end = info.Length() > 1 && info[1]->IsNumber()
            ? info[1]->Int32Value(local_context).FromMaybe(start)
            : start;
        info.This()->Set(
            local_context,
            js_string(info.GetIsolate(), "selectionStart"),
            v8::Integer::New(info.GetIsolate(), std::max(0, start))).Check();
        info.This()->Set(
            local_context,
            js_string(info.GetIsolate(), "selectionEnd"),
            v8::Integer::New(info.GetIsolate(), std::max(start, end))).Check();
        if (info.Length() > 2) {
            info.This()->Set(
                local_context,
                js_string(info.GetIsolate(), "selectionDirection"),
                info[2]).Check();
        }
    }

    static void element_set_range_text(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* node = unwrap_node(info.This());
        if (node == nullptr || !forms::supports_text_selection(node)) return;
        forms::ensure_text_value(*node);
        auto& control = node->mutable_form_control();
        const auto old_start = forms::utf16_offset_from_utf8_selection(
            control.value,
            {control.selection_start, control.selection_start_utf16_suboffset});
        const auto old_end = forms::utf16_offset_from_utf8_selection(
            control.value,
            {control.selection_end, control.selection_end_utf16_suboffset});
        const auto replacement = info.Length() > 0
            ? forms::utf16_units_from_utf8(to_wtf8(info.GetIsolate(), info[0]))
            : std::vector<uint16_t>{};
        auto start = old_start;
        auto end = old_end;
        const auto context = info.GetIsolate()->GetCurrentContext();
        if (info.Length() >= 3) {
            start = static_cast<size_t>(std::max(
                0, info[1]->Int32Value(context).FromMaybe(0)));
            end = static_cast<size_t>(std::max(
                0, info[2]->Int32Value(context).FromMaybe(0)));
        }
        const auto previous_value = control.value;
        auto units = forms::utf16_units_from_utf8(control.value);
        start = std::min(start, units.size());
        end = std::min(end, units.size());
        if (start > end) std::swap(start, end);
        units.erase(
            units.begin() + static_cast<std::ptrdiff_t>(start),
            units.begin() + static_cast<std::ptrdiff_t>(end));
        units.insert(
            units.begin() + static_cast<std::ptrdiff_t>(start),
            replacement.begin(), replacement.end());
        control.value = forms::wtf8_from_utf16_units(units);
        control.value_initialized = true;
        control.dirty_value = true;

        const auto replacement_end = start + replacement.size();
        auto selection_start = old_start;
        auto selection_end = old_end;
        const auto mode = info.Length() > 3
            ? to_utf8(info.GetIsolate(), info[3])
            : std::string{"preserve"};
        if (mode == "select") {
            selection_start = start;
            selection_end = replacement_end;
        } else if (mode == "start") {
            selection_start = selection_end = start;
        } else if (mode == "end") {
            selection_start = selection_end = replacement_end;
        } else {
            const auto delta = static_cast<std::ptrdiff_t>(replacement.size())
                - static_cast<std::ptrdiff_t>(end - start);
            const auto adjust = [&](size_t position) {
                if (position <= start) return position;
                if (position >= end) return static_cast<size_t>(
                    static_cast<std::ptrdiff_t>(position) + delta);
                return start;
            };
            selection_start = adjust(old_start);
            selection_end = adjust(old_end);
        }
        const auto start_offset = forms::utf8_selection_from_utf16_offset(
            control.value, selection_start);
        const auto end_offset = forms::utf8_selection_from_utf16_offset(
            control.value, selection_end);
        control.selection_start = start_offset.byte_offset;
        control.selection_start_utf16_suboffset = start_offset.utf16_suboffset;
        control.selection_end = end_offset.byte_offset;
        control.selection_end_utf16_suboffset = end_offset.utf16_suboffset;
        control.selection_direction = text_selection_direction::none;
        control.selection_explicitly_set = true;
        self->mark_primary_selection_publication_dirty(node);
        if(self->is_connected(*node)) {
            self->recascade_form_value_selector_transition(
                *node,previous_value,control.value,std::nullopt,false);
        } else {
            control.value_changed_by_user=false;
            self->document.mark_dirty();
        }
    }

    static void element_select(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* node = unwrap_node(info.This());
        if (node != nullptr) {
            webscene_input_event synthetic{};
            self->set_active_element(node, synthetic);
        }
        auto local_context = info.GetIsolate()->GetCurrentContext();
        v8::Local<v8::Value> value;
        auto length = 0;
        if (info.This()->Get(
                local_context,
                js_string(info.GetIsolate(), "value")).ToLocal(&value)
            && !value->IsNullOrUndefined()) {
            length = static_cast<int>(decode_wtf8_to_utf16(
                to_wtf8(info.GetIsolate(), value)).size());
        }
        v8::Local<v8::Value> arguments[] = {
            v8::Integer::New(info.GetIsolate(), 0),
            v8::Integer::New(info.GetIsolate(), length)};
        info.This()->Set(
            local_context,
            js_string(info.GetIsolate(), "selectionStart"),
            arguments[0]).Check();
        info.This()->Set(
            local_context,
            js_string(info.GetIsolate(), "selectionEnd"),
            arguments[1]).Check();
        info.This()->Set(
            local_context,
            js_string(info.GetIsolate(), "selectionDirection"),
            js_string(info.GetIsolate(), "none")).Check();
    }

    static void get_active_element(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* root = static_cast<dom_node*>(info.Holder()->GetAlignedPointerFromInternalField(
            0,
            v8::kEmbedderDataTypeTagDefault));
        if (root == nullptr) root = &self->active_root();
        dom_node* node = self->document.is_shadow_root(*root)
            ? nullptr
            : &exposed_body_for_root(*root);
        const auto root_owner = self->focus_document_owner(root);
        if (root_owner != self->focused_document_owner_id) {
          if (auto *owner = find_node_by_native_id(
                  *root, self->focused_document_owner_id);
              owner != nullptr && owner->tag == "iframe") {
            node = owner;
          }
        } else if (self->active_element != nullptr) {
          const auto *active_shadow_root =
              self->document.containing_shadow_root(*self->active_element);
          if (!self->document.is_shadow_root(*root) &&
              active_shadow_root != nullptr &&
              self->focus_document_owner(root) ==
                  self->focus_document_owner(self->active_element)) {
            const auto *state = self->document.shadow_dom(*active_shadow_root);
            if (state != nullptr && state->host != nullptr)
              node = state->host;
            info.GetReturnValue().Set(self->wrap_node(*node));
            return;
          }
          const auto active_owner =
              self->focus_document_owner(self->active_element);
          if (root_owner == active_owner &&
              self->subtree_contains(*root, self->active_element)) {
            node = self->active_element;
          } else if (root_owner == 0U && active_owner != 0U) {
            if (auto *owner = find_node_by_native_id(*root, active_owner);
                owner != nullptr) {
              node = owner;
            }
          }
        }
        info.GetReturnValue().Set(node == nullptr
            ? v8::Local<v8::Value>(v8::Null(info.GetIsolate()))
            : v8::Local<v8::Value>(self->wrap_node(*node)));
    }

    static void get_provisional_frame_active_element(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* frame = provisional_frame_owner(info.Holder());
        if (frame == nullptr) return;
        const auto known = self->provisional_frame_bodies.find(frame->id);
        if (known == self->provisional_frame_bodies.end() || known->second == nullptr) return;
        auto* root = known->second;
        auto* node = self->active_element != nullptr
                && self->subtree_contains(*root, self->active_element)
            ? self->active_element
            : root;
        info.GetReturnValue().Set(self->wrap_node(*node));
    }

    static void get_provisional_frame_default_view(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* frame = provisional_frame_owner(info.Holder());
        if (frame != nullptr) info.GetReturnValue().Set(self->frame_window(*frame));
    }

    static dom_node* provisional_frame_owner(v8::Local<v8::Object> document)
    {
        auto* node = unwrap_node(document);
        for (auto* ancestor = node; ancestor != nullptr; ancestor = ancestor->parent) {
            if (ancestor->tag == "iframe") return ancestor;
        }
        return node != nullptr && node->tag == "iframe" ? node : nullptr;
    }

    static void get_provisional_frame_element(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* frame = unwrap_node(info.Holder());
        if (frame != nullptr) info.GetReturnValue().Set(self->wrap_node(*frame));
    }

    static void document_has_focus(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      auto *self = current(info.GetIsolate());
      auto *root = info.This()->InternalFieldCount() > 0
                       ? static_cast<dom_node *>(
                             info.This()->GetAlignedPointerFromInternalField(
                                 0, v8::kEmbedderDataTypeTagDefault))
                       : nullptr;
      const auto owner_id =
          root == nullptr ? 0U : self->focus_document_owner(root);
      info.GetReturnValue().Set(v8::Boolean::New(
          info.GetIsolate(),
          self != nullptr && self->document_focused &&
              (owner_id == 0U || owner_id == self->focused_document_owner_id)));
    }

    static void get_document_fullscreen_element(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        if (self == nullptr || self->fullscreen_element == nullptr
            || !self->is_connected(*self->fullscreen_element)) {
            info.GetReturnValue().Set(v8::Null(info.GetIsolate()));
            return;
        }
        info.GetReturnValue().Set(self->wrap_node(*self->fullscreen_element));
    }

    static void get_document_fullscreen_enabled(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        info.GetReturnValue().Set(v8::True(info.GetIsolate()));
    }

    static void queue_fullscreen_request(
        const v8::FunctionCallbackInfo<v8::Value>& info,
        dom_node* target,
        bool enter)
    {
        auto* self = current(info.GetIsolate());
        auto local_context = info.GetIsolate()->GetCurrentContext();
        auto resolver = v8::Promise::Resolver::New(local_context).ToLocalChecked();
        info.GetReturnValue().Set(resolver->GetPromise());
        const auto reject = [&](const char* message, const char* name) {
            auto error = v8::Exception::Error(js_string(info.GetIsolate(), message));
            if (error->IsObject()) {
                error.As<v8::Object>()->Set(
                    local_context,
                    js_string(info.GetIsolate(), "name"),
                    js_string(info.GetIsolate(), name)).Check();
            }
            resolver->Reject(local_context, error).Check();
        };
        if (self == nullptr || local_context != self->context.Get(info.GetIsolate())
            || (enter && (target == nullptr || !self->is_connected(*target)))) {
            reject("Fullscreen requires a connected element in the active top-level document",
                "TypeError");
            return;
        }
        if (!enter && self->fullscreen_element == nullptr) {
            resolver->Resolve(local_context, v8::Undefined(info.GetIsolate())).Check();
            return;
        }
        constexpr size_t maximum_pending_window_operations = 16U;
        if (self->host_promise_targets.size() >= maximum_pending_window_operations) {
            reject("Too many pending native window operations", "QuotaExceededError");
            return;
        }
        const auto request_id = ++self->next_host_request_id;
        auto request = std::make_unique<native_host_request>();
        request->view.request_id = request_id;
        request->view.kind = enter
            ? WEBSCENE_HOST_REQUEST_FULLSCREEN_ENTER_V1
            : WEBSCENE_HOST_REQUEST_FULLSCREEN_EXIT_V1;
        request->view.target_node_id = enter ? target->id : 0U;
        host_promise_target promise_target;
        promise_target.operation = enter ? "enterFullscreen" : "exitFullscreen";
        promise_target.target_node_id = enter ? target->id : 0U;
        promise_target.context.Reset(info.GetIsolate(), local_context);
        promise_target.resolver.Reset(info.GetIsolate(), resolver);
        self->host_promise_targets.emplace(request_id, std::move(promise_target));
        if (!self->enqueue_typed_host_request(std::move(request))) {
            self->host_promise_targets.erase(request_id);
            reject("The host request queue is full", "QuotaExceededError");
        }
    }

    static void element_request_fullscreen(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        queue_fullscreen_request(info, unwrap_node(info.This()), true);
    }

    static void document_exit_fullscreen(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        queue_fullscreen_request(info, nullptr, false);
    }

    static void document_exec_command(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* isolate = info.GetIsolate();
        auto* self = current(isolate);
        bool document_receiver = self != nullptr && self->is_document_object(info.This());
        if (self != nullptr && !document_receiver) {
            for (const auto& [_, value] : self->detached_documents) {
                if (!value.IsEmpty() && info.This()->StrictEquals(value.Get(isolate))) {
                    document_receiver = true;
                    break;
                }
            }
        }
        if (!document_receiver || info.Length() == 0) {
            isolate->ThrowException(v8::Exception::TypeError(js_string(isolate,
                "Document.execCommand requires a Document receiver and a command argument")));
            return;
        }
        const auto context = isolate->GetCurrentContext();
        v8::Local<v8::String> command;
        if (!info[0]->ToString(context).ToLocal(&command)) return;
        if (info.Length() > 2 && !info[2]->IsUndefined()) {
            v8::Local<v8::String> value;
            if (!info[2]->ToString(context).ToLocal(&value)) return;
        }
        auto* root = unwrap_node(info.This());
        if (root != nullptr && (root->xml_mode || root->tag == "#document-root")) {
            throw_dom_exception(info, "execCommand is only supported on HTML documents",
                "InvalidStateError");
            return;
        }
        v8::String::Utf8Value command_utf8(isolate, command);
        std::string command_name = *command_utf8 == nullptr
            ? std::string{} : std::string(*command_utf8, command_utf8.length());
        std::transform(
            command_name.begin(), command_name.end(), command_name.begin(),
            [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
        if (command_name == "copy" || command_name == "cut"
            || command_name == "paste") {
            if (!self->dispatching_user_input
                || self->executing_legacy_clipboard_command) {
                info.GetReturnValue().Set(false);
                return;
            }
            auto* target = self->active_element == nullptr
                ? &self->active_root() : self->active_element;
            if (self->document.is_inert(*target)) {
                info.GetReturnValue().Set(false);
                return;
            }
            self->executing_legacy_clipboard_command = true;
            const bool dispatched = self->dispatch_clipboard_shortcut(
                *target, command_name.c_str());
            self->executing_legacy_clipboard_command = false;
            if (dispatched) {
                self->record_feature(
                    "web-api", "Document.execCommand", "supported",
                    "copy/cut/paste dispatch browser clipboard events through the bounded native clipboard bridge",
                    "web-api-binding");
            }
            info.GetReturnValue().Set(dispatched);
            return;
        }
        if (command_name == "selectall") {
            auto* selection_root = root;
            if (selection_root == nullptr) {
                selection_root = static_cast<dom_node*>(
                    context->GetAlignedPointerFromEmbedderData(
                        1, v8::kEmbedderDataTypeTagDefault));
            }
            if (selection_root == nullptr) {
                selection_root = &self->document.body();
            }
            auto& selection = self->window_selections
                .try_emplace(selection_root->id).first->second;
            selection.root_id = selection_root->id;
            self->refresh_selection_text_nodes(selection, *selection_root);
            if (selection.text_nodes.empty()) {
                selection.has_range = false;
                selection.anchor_node_id = 0U;
                selection.focus_node_id = 0U;
            selection.anchor_offset = 0U;
            selection.focus_offset = 0U;
            self->selection_paint_changed();
            info.GetReturnValue().Set(false);
                return;
            }
            auto* first = selection.text_nodes.front();
            auto* last = selection.text_nodes.back();
            selection.has_range = true;
            selection.anchor_node_id = first->id;
            selection.focus_node_id = last->id;
            selection.anchor_offset = 0U;
            selection.focus_offset = decode_wtf8_to_utf16(
                last->text_content).size();
            self->selection_paint_changed();
            self->record_feature(
                "web-api", "Document.execCommand", "supported",
                "selectAll publishes a realm-local Selection over visible document text",
                "web-api-binding");
            info.GetReturnValue().Set(true);
            return;
        }
        // Other legacy editing commands still have no executor. Return false
        // without mutating the DOM so callers can choose their supported
        // asynchronous or application-level fallback.
        self->record_feature("web-api", "Document.execCommand", "unsupported",
            "unsupported legacy editing commands return false without mutation",
            "web-api-binding");
        info.GetReturnValue().Set(false);
    }

    static void document_query_command_supported(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* isolate = info.GetIsolate();
        auto* self = current(isolate);
        bool document_receiver = self != nullptr
            && self->is_document_object(info.This());
        if (self != nullptr && !document_receiver) {
            for (const auto& [_, value] : self->detached_documents) {
                if (!value.IsEmpty() && info.This()->StrictEquals(value.Get(isolate))) {
                    document_receiver = true;
                    break;
                }
            }
        }
        if (!document_receiver || info.Length() == 0) {
            isolate->ThrowException(v8::Exception::TypeError(js_string(
                isolate,
                "Document.queryCommandSupported requires a Document receiver and a command argument")));
            return;
        }
        const auto context = isolate->GetCurrentContext();
        v8::Local<v8::String> command;
        if (!info[0]->ToString(context).ToLocal(&command)) return;
        auto* root = unwrap_node(info.This());
        if (root != nullptr && (root->xml_mode || root->tag == "#document-root")) {
            info.GetReturnValue().Set(false);
            return;
        }
        v8::String::Utf8Value command_utf8(isolate, command);
        std::string command_name = *command_utf8 == nullptr
            ? std::string{} : std::string(*command_utf8, command_utf8.length());
        std::transform(
            command_name.begin(), command_name.end(), command_name.begin(),
            [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
        const auto supported = command_name == "copy" || command_name == "cut"
            || command_name == "paste" || command_name == "selectall";
        if (supported) {
            self->record_feature(
                "web-api", "Document.queryCommandSupported", "supported",
                "reports only bounded legacy editing commands implemented by WebScene",
                "web-api-binding");
        }
        info.GetReturnValue().Set(supported);
    }

    static void return_empty_string(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        info.GetReturnValue().Set(js_string(info.GetIsolate(), ""));
    }

    static void location_to_string(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto local_context = info.GetIsolate()->GetCurrentContext();
        v8::Local<v8::Value> href;
        if (info.This()->Get(
                local_context,
                js_string(info.GetIsolate(), "href")).ToLocal(&href)) {
            info.GetReturnValue().Set(href);
        }
    }

    static v8::Local<v8::Private> location_href_key(v8::Isolate* isolate)
    {
        return v8::Private::ForApi(
            isolate, js_string(isolate, "WebScene.Location.href"));
    }

    static void get_location_href(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto local_context = info.GetIsolate()->GetCurrentContext();
        v8::Local<v8::Value> href;
        if (info.Holder()->GetPrivate(
                local_context, location_href_key(info.GetIsolate()))
                .ToLocal(&href)) {
            info.GetReturnValue().Set(href);
        }
    }

    static void get_location_hash(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto local_context=info.GetIsolate()->GetCurrentContext();
        v8::Local<v8::Value> href_value;
        if(!info.Holder()->GetPrivate(
                local_context,location_href_key(info.GetIsolate()))
                .ToLocal(&href_value))return;
        const auto href=to_utf8(info.GetIsolate(),href_value);
        const auto fragment=href.find('#');
        info.GetReturnValue().Set(js_dom_string(
            info.GetIsolate(),fragment==std::string::npos
                ?std::string_view{}:std::string_view(href).substr(fragment)));
    }

    static void set_location_hash(
        v8::Local<v8::Name>,v8::Local<v8::Value> value,
        const v8::PropertyCallbackInfo<void>& info)
    {
        auto* self=current(info.GetIsolate());
        if(self==nullptr)return;
        auto local_context=info.GetIsolate()->GetCurrentContext();
        auto* root=static_cast<dom_node*>(
            local_context->GetAlignedPointerFromEmbedderData(
                1,v8::kEmbedderDataTypeTagDefault));
        if(root==nullptr)return;
        auto previous=self->navigation_address_for_root(root);
        auto fragment=to_utf8(info.GetIsolate(),value);
        constexpr size_t maximum_fragment_bytes=8192U;
        if(fragment.size()>maximum_fragment_bytes)return;
        if(!fragment.empty()&&!fragment.starts_with('#'))fragment.insert(0U,1U,'#');
        auto resolved=previous.substr(0U,previous.find('#'))+fragment;
        if(resolved==previous)return;
        if(!self->apply_same_document_history_url(local_context,resolved))return;
        self->dispatch_same_document_history_event(
            local_context,"hashchange",v8::Null(info.GetIsolate()),
            previous,resolved);
    }

    static v8::Local<v8::Private> history_state_key(v8::Isolate* isolate)
    {
        return v8::Private::ForApi(
            isolate, js_string(isolate, "WebScene.History.state"));
    }

    static v8::Local<v8::Private> history_length_key(v8::Isolate* isolate)
    {
        return v8::Private::ForApi(
            isolate, js_string(isolate, "WebScene.History.length"));
    }

    static void get_history_state(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto local_context = info.GetIsolate()->GetCurrentContext();
        v8::Local<v8::Value> value;
        if (info.Holder()->GetPrivate(
                local_context, history_state_key(info.GetIsolate())).ToLocal(&value)) {
            info.GetReturnValue().Set(value);
        }
    }

    static void get_history_length(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto local_context = info.GetIsolate()->GetCurrentContext();
        v8::Local<v8::Value> value;
        if (info.Holder()->GetPrivate(
                local_context, history_length_key(info.GetIsolate())).ToLocal(&value)) {
            info.GetReturnValue().Set(value);
        }
    }

    nested_history_state* nested_history_for_context(
        v8::Local<v8::Context> local_context,
        bool create)
    {
        auto* frame = active_nested_frame_for_context(local_context);
        if (frame == nullptr) return nullptr;
        auto known = nested_history_states.find(frame->id);
        if (known != nested_history_states.end()) return &known->second;
        if (!create) return nullptr;
        auto address = navigation_address_for_root(static_cast<dom_node*>(
            local_context->GetAlignedPointerFromEmbedderData(
                1, v8::kEmbedderDataTypeTagDefault)));
        constexpr size_t maximum_history_url_bytes = 8192U;
        if (address.size() > maximum_history_url_bytes) address.clear();
        auto inserted = nested_history_states.try_emplace(frame->id).first;
        inserted->second.entries.push_back(
            history_entry_state{std::move(address), std::nullopt, 1U});
        return &inserted->second;
    }

    bool dispatch_same_document_history_event(
        v8::Local<v8::Context> local_context,
        std::string_view type,
        v8::Local<v8::Value> state,
        std::string_view old_url = {},
        std::string_view new_url = {})
    {
        auto target = local_context->Global();
        v8::Local<v8::Value> dispatcher;
        if (!target->Get(local_context, js_string(isolate, "dispatchEvent"))
                .ToLocal(&dispatcher)
            || !dispatcher->IsFunction()) return false;
        auto event = create_event_instance(local_context);
        event->Set(local_context, js_string(isolate, "type"),
            js_string(isolate, std::string(type).c_str())).Check();
        event->Set(local_context, js_string(isolate, "bubbles"),
            v8::False(isolate)).Check();
        event->Set(local_context, js_string(isolate, "cancelable"),
            v8::False(isolate)).Check();
        if (type == "popstate") {
            event->Set(local_context, js_string(isolate, "state"), state).Check();
        } else if (type == "hashchange") {
            event->Set(local_context, js_string(isolate, "oldURL"),
                js_string(isolate, std::string(old_url).c_str())).Check();
            event->Set(local_context, js_string(isolate, "newURL"),
                js_string(isolate, std::string(new_url).c_str())).Check();
        }
        v8::TryCatch try_catch(isolate);
        v8::Local<v8::Value> arguments[]{event};
        static_cast<void>(dispatcher.As<v8::Function>()->Call(
            local_context, target, 1, arguments));
        perform_microtask_checkpoint();
        if (!try_catch.HasCaught()) return true;
        frame_last_error_value = "Nested history event dispatch failed: "
            + describe_reported_exception(try_catch, local_context);
        ++frame_script_error_count;
        report_document_script_error(frame_last_error_value);
        return false;
    }

    static std::string strip_url_query_and_fragment(std::string value)
    {
        if (const auto suffix = value.find_first_of("?#"); suffix != std::string::npos) {
            value.resize(suffix);
        }
        return value;
    }

    static std::string normalize_local_file_url(std::string value)
    {
        if (!lower_html_name(value).starts_with("file:")) return {};
        const auto suffix_offset = value.find_first_of("?#", 5U);
        const auto suffix = suffix_offset == std::string::npos
            ? std::string{}
            : value.substr(suffix_offset);
        auto path = value.substr(
            5U,
            suffix_offset == std::string::npos
                ? std::string::npos
                : suffix_offset - 5U);
        // Packaged documents use local file URLs without an authority. V8's
        // URL implementation serializes those as either file:/path or
        // file:///path; canonicalize both while rejecting file://host/path.
        if (path.starts_with("//") && !path.starts_with("///")) return {};
        const auto first_non_slash = path.find_first_not_of('/');
        path = first_non_slash == std::string::npos
            ? std::string("/")
            : "/" + path.substr(first_non_slash);
        return "file://" + path + suffix;
    }

    bool apply_same_document_history_url(
        v8::Local<v8::Context> local_context,
        const std::string& resolved)
    {
        if (!context.IsEmpty() && local_context != context.Get(isolate)
            && active_nested_frame_for_context(local_context) == nullptr) {
            return false;
        }
        auto* root = static_cast<dom_node*>(
            local_context->GetAlignedPointerFromEmbedderData(
                1, v8::kEmbedderDataTypeTagDefault));
        if (root == nullptr) return false;
        const auto previous_hash=url_fragment(navigation_address_for_root(root));
        if (context.IsEmpty() || local_context == context.Get(isolate)) {
            document_base_address = resolved;
            top_level_location_address = resolved;
        } else {
            frame_base_addresses.insert_or_assign(root, resolved);
        }
        auto& cache = document_base_caches[root];
        cache.navigation_address = resolved;
        cache.fallback_address = resolved;
        cache.effective_address = resolved;
        cache.dirty = true;
        set_context_location(local_context, local_context->Global(), resolved);
        recascade_local_link_document_transition(root);
        recascade_target_document_transition(
            root,previous_hash,url_fragment(resolved));
        return true;
    }

    static void history_update_state(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* isolate = info.GetIsolate();
        auto local_context = isolate->GetCurrentContext();
        if (self == nullptr || info.Length() < 2) {
            isolate->ThrowException(v8::Exception::TypeError(
                js_string(isolate,
                    "History state mutation requires state and unused arguments")));
            return;
        }
        if (!info.This()->HasPrivate(
                local_context, history_length_key(isolate)).FromMaybe(false)) {
            isolate->ThrowException(v8::Exception::TypeError(
                js_string(isolate, "Illegal invocation")));
            return;
        }
        auto* nested_frame = self->active_nested_frame_for_context(local_context);
        if (!self->context.IsEmpty()
            && local_context != self->context.Get(isolate)
            && nested_frame == nullptr) {
            throw_dom_exception(
                info, "History realm is no longer active", "InvalidStateError");
            return;
        }
        if (nested_frame != nullptr
            && (self->frame_beforeunload_dispatches.contains(nested_frame->id)
                || self->frame_unload_dispatches.contains(nested_frame->id))) {
            throw_dom_exception(
                info, "History mutation is unavailable while unloading",
                "InvalidStateError");
            return;
        }
        const bool replace = info.Data()->IsTrue();
        constexpr size_t maximum_history_url_bytes = 8192U;
        auto current_address = self->current_navigation_address();
        if (current_address.empty()) {
            current_address = self->top_level_location_address;
        }
        std::string resolved = current_address;
        const bool has_url = info.Length() > 2 && !info[2]->IsNullOrUndefined();
        if (has_url) {
            const auto authored = to_utf8(isolate, info[2]);
            if (authored.size() > maximum_history_url_bytes
                || authored.find_first_of("\r\n\0", 0U, 3U) != std::string::npos) {
                throw_dom_exception(info, "History URL is invalid", "SecurityError");
                return;
            }
            resolved = authored.empty()
                ? current_address
                : resolve_resource_url(authored, current_address);
        }
        if (resource_scheme(resolved) == "file") {
            resolved = normalize_local_file_url(std::move(resolved));
        }
        const auto current_scheme = resource_scheme(current_address);
        const auto resolved_scheme = resource_scheme(resolved);
        const bool same_origin_http =
            (current_scheme == "http" || current_scheme == "https")
            && resolved_scheme == current_scheme
            && resource_origin(resolved) == resource_origin(current_address);
        const bool same_file_document = current_scheme == "file"
            && resolved_scheme == "file"
            && strip_url_query_and_fragment(resolved)
                == strip_url_query_and_fragment(current_address);
        if (resolved.empty() || resolved.size() > maximum_history_url_bytes
            || (has_url && !same_origin_http && !same_file_document)) {
            throw_dom_exception(
                info,
                "History URL must retain the current document origin",
                "SecurityError");
            return;
        }

        clone_packet packet;
        if (!serialize_clone(
                info,
                info[0],
                v8::Undefined(isolate),
                packet,
                1024U * 1024U)) {
            return;
        }
        v8::Local<v8::Value> state;
        if (!self->deserialize_clone(isolate, local_context, packet).ToLocal(&state)) {
            if (!isolate->HasPendingException()) {
                throw_dom_exception(
                    info, "History state could not be cloned", "DataCloneError");
            }
            return;
        }

        auto history = info.This();
        auto* nested_history = self->nested_history_for_context(
            local_context, true);
        if (nested_history != nullptr)
            self->capture_nested_form_state(local_context, false);
        v8::Local<v8::Value> length_value;
        uint32_t length = 1U;
        if (history->GetPrivate(
                local_context, history_length_key(isolate)).ToLocal(&length_value)) {
            length = length_value->Uint32Value(local_context).FromMaybe(1U);
        }
        constexpr size_t maximum_history_entries = 1024U;
        constexpr size_t maximum_history_state_bytes = 16U * 1024U * 1024U;
        const auto retained_nested_entries = nested_history == nullptr
            ? 0U
            : nested_history->index + 1U;
        if (!replace
            && (nested_history == nullptr
                    ? length >= maximum_history_entries
                    : retained_nested_entries >= maximum_history_entries)) {
            throw_dom_exception(
                info, "History entry limit reached", "SecurityError");
            return;
        }
        size_t reclaimed_state_bytes = 0U;
        if (nested_history != nullptr) {
            const auto first_reclaimed = replace
                ? nested_history->index
                : retained_nested_entries;
            for (auto index = first_reclaimed;
                 index < nested_history->entries.size(); ++index) {
                if (nested_history->entries[index].state.has_value()) {
                    reclaimed_state_bytes += clone_packet_size(
                        *nested_history->entries[index].state);
                }
            }
            const auto packet_bytes = clone_packet_size(packet);
            if (packet_bytes > maximum_history_state_bytes
                || nested_history->state_bytes - reclaimed_state_bytes
                        > maximum_history_state_bytes - packet_bytes) {
                throw_dom_exception(
                    info, "History state budget reached", "QuotaExceededError");
                return;
            }
        }
        if (!self->apply_same_document_history_url(local_context, resolved)) {
            throw_dom_exception(
                info, "History realm is no longer active", "InvalidStateError");
            return;
        }
        history->SetPrivate(
            local_context, history_state_key(isolate), state).Check();
        if (nested_history != nullptr) {
            const auto document_sequence =
                nested_history->entries[nested_history->index]
                    .document_sequence;
            nested_history->state_bytes -= reclaimed_state_bytes;
            if (replace) {
                nested_history->entries[nested_history->index] =
                    history_entry_state{
                        resolved, std::move(packet), document_sequence};
            } else {
                nested_history->entries.resize(retained_nested_entries);
                nested_history->entries.push_back(
                    history_entry_state{
                        resolved, std::move(packet), document_sequence});
                nested_history->index = nested_history->entries.size() - 1U;
            }
            nested_history->state_bytes += clone_packet_size(
                *nested_history->entries[nested_history->index].state);
            history->SetPrivate(
                local_context,
                history_length_key(isolate),
                v8::Integer::NewFromUnsigned(
                    isolate,
                    static_cast<uint32_t>(nested_history->entries.size()))).Check();
            self->capture_nested_form_state(local_context, false);
        } else if (!replace) {
            history->SetPrivate(
                local_context,
                history_length_key(isolate),
                v8::Integer::NewFromUnsigned(isolate, length + 1U)).Check();
        }
        self->record_feature(
            "web-api",
            replace ? "History.replaceState" : "History.pushState",
            replace ? "supported" : "partially-supported",
            replace
                ? "bounded same-document state and URL mutation without host navigation"
                : "bounded same-document state and URL mutation; traversal is tracked separately",
            "web-api-binding");
    }

    static std::string url_fragment(std::string_view url)
    {
        const auto offset = url.find('#');
        return offset == std::string_view::npos
            ? std::string{}
            : std::string(url.substr(offset));
    }

    void perform_nested_history_traversal(
        v8::Local<v8::Context> local_context,
        v8::Local<v8::Object> history_object,
        int32_t delta)
    {
        auto* frame = active_nested_frame_for_context(local_context);
        auto* history = nested_history_for_context(local_context, false);
        if (frame == nullptr || history == nullptr || !history->traversing) return;
        if (delta == 0) {
            history->traversing = false;
            reload_nested_context(local_context);
            return;
        }
        const auto target = static_cast<int64_t>(history->index)
            + static_cast<int64_t>(delta);
        if (target < 0 || target >= static_cast<int64_t>(history->entries.size())) {
            history->traversing = false;
            return;
        }

        capture_nested_form_state(local_context, true);

        const auto frame_id = frame->id;
        const auto previous_index = history->index;
        const auto old_url = current_navigation_address();
        const auto entry = history->entries[static_cast<size_t>(target)];
        const auto cross_document = entry.document_sequence
            != history->entries[history->index].document_sequence;
        if (cross_document) {
            const auto authored_source = frame->attributes.find("src");
            frame_reload_reflected_sources.insert_or_assign(
                frame_id,
                authored_source == frame->attributes.end()
                    ? std::nullopt
                    : std::optional<std::string>{authored_source->second});
            history->index = static_cast<size_t>(target);
            frame->attributes["src"] = entry.url;
            frame_history_traversals.insert(frame_id);
            if (entry.url.empty() || !begin_iframe_navigation(*frame)) {
                if (auto current = nested_history_states.find(frame_id);
                    current != nested_history_states.end()) {
                    current->second.index = previous_index;
                    current->second.traversing = false;
                }
                frame_history_traversals.erase(frame_id);
                restore_nested_reload_reflected_source(*frame);
                return;
            }
            enqueue_iframe_hydration_if_needed(*frame);
            return;
        }
        v8::Local<v8::Value> state = v8::Null(isolate);
        if (entry.state.has_value()
            && !deserialize_clone(
                isolate, local_context, *entry.state).ToLocal(&state)) {
            history->traversing = false;
            return;
        }
        history->index = static_cast<size_t>(target);
        if (!apply_same_document_history_url(local_context, entry.url)) {
            history->traversing = false;
            return;
        }
        if (!history->entries[history->index].form_state.controls.empty()) {
            auto* root = static_cast<dom_node*>(
                local_context->GetAlignedPointerFromEmbedderData(
                    1, v8::kEmbedderDataTypeTagDefault));
            if (root != nullptr) restore_form_state(
                *root, history->entries[history->index].form_state, false);
        }
        history_object->SetPrivate(
            local_context, history_state_key(isolate), state).Check();
        dispatch_same_document_history_event(
            local_context, "popstate", state);
        auto current = nested_history_states.find(frame_id);
        if (current != nested_history_states.end()
            && active_nested_frame_for_context(local_context) != nullptr
            && url_fragment(old_url) != url_fragment(entry.url)) {
            dispatch_same_document_history_event(
                local_context,
                "hashchange",
                v8::Null(isolate),
                old_url,
                entry.url);
            current = nested_history_states.find(frame_id);
        }
        if (current != nested_history_states.end()) {
            current->second.traversing = false;
        }
        record_feature(
            "web-api", "History.go", "partially-supported",
            "bounded nested same-document traversal with popstate and hashchange",
            "web-api-binding");
    }

    void complete_nested_cross_document_history_traversal(
        dom_node& frame,
        v8::Local<v8::Context> local_context)
    {
        if (frame_history_traversals.erase(frame.id) == 0U) return;
        auto history = nested_history_states.find(frame.id);
        if (history == nested_history_states.end()
            || history->second.index >= history->second.entries.size()) {
            return;
        }
        auto& state = history->second;
        v8::Local<v8::Value> restored_state = v8::Null(isolate);
        const auto& entry = state.entries[state.index];
        if (entry.state.has_value()
            && !deserialize_clone(
                isolate, local_context, *entry.state).ToLocal(&restored_state)) {
            state.traversing = false;
            return;
        }
        dispatch_same_document_history_event(
            local_context, "popstate", restored_state);
        if (auto current = nested_history_states.find(frame.id);
            current != nested_history_states.end()) {
            current->second.traversing = false;
        }
        record_feature(
            "web-api", "History.go", "partially-supported",
            "bounded nested cross-document traversal with active-realm replacement",
            "web-api-binding");
    }

    static void history_traversal_task(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto local_context = info.GetIsolate()->GetCurrentContext();
        if (self == nullptr || !info.Data()->IsArray()) return;
        auto data = info.Data().As<v8::Array>();
        v8::Local<v8::Value> history_value;
        v8::Local<v8::Value> delta_value;
        if (!data->Get(local_context, 0U).ToLocal(&history_value)
            || !history_value->IsObject()
            || !data->Get(local_context, 1U).ToLocal(&delta_value)) return;
        self->perform_nested_history_traversal(
            local_context,
            history_value.As<v8::Object>(),
            delta_value->Int32Value(local_context).FromMaybe(0));
    }

    static void history_traverse(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* isolate = info.GetIsolate();
        auto local_context = isolate->GetCurrentContext();
        if (self == nullptr
            || !info.This()->HasPrivate(
                local_context, history_length_key(isolate)).FromMaybe(false)) {
            isolate->ThrowException(v8::Exception::TypeError(
                js_string(isolate, "Illegal invocation")));
            return;
        }
        auto* frame = self->active_nested_frame_for_context(local_context);
        auto* history = self->nested_history_for_context(local_context, false);
        if (frame == nullptr || history == nullptr || history->traversing
            || self->frame_beforeunload_dispatches.contains(frame->id)
            || self->frame_unload_dispatches.contains(frame->id)) return;
        int32_t delta = info.Data()->Int32Value(local_context).FromMaybe(0);
        if (delta == 0 && info.Length() != 0) {
            const auto number = info[0]->NumberValue(local_context);
            if (number.IsNothing() || !std::isfinite(number.FromJust())) return;
            const auto value = number.FromJust();
            if (value > static_cast<double>(std::numeric_limits<int32_t>::max())
                || value < static_cast<double>(std::numeric_limits<int32_t>::min())) {
                return;
            }
            delta = static_cast<int32_t>(value);
        }
        auto data = v8::Array::New(isolate, 2);
        data->Set(local_context, 0U, info.This()).Check();
        data->Set(local_context, 1U, v8::Integer::New(isolate, delta)).Check();
        auto callback = v8::Function::New(
            local_context, history_traversal_task, data).ToLocalChecked();
        v8::Local<v8::Value> timer_value;
        auto global = local_context->Global();
        if (!global->Get(local_context, js_string(isolate, "setTimeout"))
                .ToLocal(&timer_value)
            || !timer_value->IsFunction()) return;
        history->traversing = true;
        v8::Local<v8::Value> arguments[]{
            callback, v8::Integer::New(isolate, 0)};
        if (timer_value.As<v8::Function>()->Call(
                local_context, global, 2, arguments).IsEmpty()) {
            history->traversing = false;
        }
    }

    void install_history(
        v8::Local<v8::Context> local_context,
        v8::Local<v8::Object> global)
    {
        v8::Local<v8::Value> existing;
        if (global->Get(
                local_context, js_string(isolate, "history")).ToLocal(&existing)
            && existing->IsObject()) {
            return;
        }
        auto history = v8::Object::New(isolate);
        history->SetPrivate(
            local_context,
            history_state_key(isolate),
            v8::Null(isolate)).Check();
        history->SetPrivate(
            local_context,
            history_length_key(isolate),
            v8::Integer::New(isolate, 1)).Check();
        if (auto* nested = nested_history_for_context(local_context, true);
            nested != nullptr) {
            history->SetPrivate(
                local_context,
                history_length_key(isolate),
                v8::Integer::NewFromUnsigned(
                    isolate,
                    static_cast<uint32_t>(nested->entries.size()))).Check();
            const auto& entry = nested->entries[nested->index];
            v8::Local<v8::Value> state = v8::Null(isolate);
            if (entry.state.has_value()) {
                static_cast<void>(deserialize_clone(
                    isolate, local_context, *entry.state).ToLocal(&state));
            }
            history->SetPrivate(
                local_context, history_state_key(isolate), state).Check();
            if (auto* frame = active_nested_frame_for_context(local_context);
                frame != nullptr) {
                frame_reload_navigations.erase(frame->id);
            }
        }
        history->SetNativeDataProperty(
            local_context,
            js_string(isolate, "state"),
            get_history_state).Check();
        history->SetNativeDataProperty(
            local_context,
            js_string(isolate, "length"),
            get_history_length).Check();
        history->Set(
            local_context,
            js_string(isolate, "scrollRestoration"),
            js_string(isolate, "auto")).Check();
        history->Set(
            local_context,
            js_string(isolate, "back"),
            v8::Function::New(
                local_context,
                history_traverse,
                v8::Integer::New(isolate, -1)).ToLocalChecked()).Check();
        history->Set(
            local_context,
            js_string(isolate, "forward"),
            v8::Function::New(
                local_context,
                history_traverse,
                v8::Integer::New(isolate, 1)).ToLocalChecked()).Check();
        history->Set(
            local_context,
            js_string(isolate, "go"),
            v8::Function::New(
                local_context,
                history_traverse,
                v8::Integer::New(isolate, 0)).ToLocalChecked()).Check();
        history->Set(
            local_context,
            js_string(isolate, "pushState"),
            v8::Function::New(
                local_context,
                history_update_state,
                v8::False(isolate),
                2).ToLocalChecked()).Check();
        history->Set(
            local_context,
            js_string(isolate, "replaceState"),
            v8::Function::New(
                local_context,
                history_update_state,
                v8::True(isolate),
                2).ToLocalChecked()).Check();
        global->Set(local_context, js_string(isolate, "history"), history).Check();
    }

    static bool request_location_navigation(
        v8::Isolate* isolate,
        v8::Local<v8::Context> local_context,
        v8::Local<v8::Value> value,
        bool replace)
    {
        auto* self = current(isolate);
        return self != nullptr && self->queue_top_level_navigation(
            local_context, to_utf8(isolate, value), replace);
    }

    static void set_location_href(
        v8::Local<v8::Name>,
        v8::Local<v8::Value> value,
        const v8::PropertyCallbackInfo<void>& info)
    {
        auto local_context = info.GetIsolate()->GetCurrentContext();
        if (!request_location_navigation(
                info.GetIsolate(), local_context, value, false)) {
            info.GetIsolate()->ThrowException(v8::Exception::Error(
                js_string(info.GetIsolate(),
                    "WebScene rejected the same-window navigation request")));
        }
    }

    static void location_assign(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto local_context = info.GetIsolate()->GetCurrentContext();
        if (info.Length() == 0 || !request_location_navigation(
                info.GetIsolate(), local_context, info[0], false)) {
            info.GetIsolate()->ThrowException(v8::Exception::Error(
                js_string(info.GetIsolate(),
                    "WebScene rejected the same-window navigation request")));
        }
    }

    static void location_replace(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto local_context = info.GetIsolate()->GetCurrentContext();
        if (info.Length() == 0 || !request_location_navigation(
                info.GetIsolate(), local_context, info[0], true)) {
            info.GetIsolate()->ThrowException(v8::Exception::Error(
                js_string(info.GetIsolate(),
                    "WebScene rejected the same-window navigation request")));
        }
    }

    static void location_reload(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto local_context = info.GetIsolate()->GetCurrentContext();
        if (self == nullptr) return;
        if (local_context != self->context.Get(info.GetIsolate())) {
            self->reload_nested_context(local_context);
            return;
        }
        if (!self->queue_top_level_window_action(
                local_context, WEBSCENE_HOST_REQUEST_WINDOW_RELOAD_V1)) {
            info.GetIsolate()->ThrowException(v8::Exception::Error(
                js_string(info.GetIsolate(), "WebScene rejected the reload request")));
        }
    }

    static void return_null(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        info.GetReturnValue().Set(v8::Null(info.GetIsolate()));
    }

    static uint32_t selection_root_id(v8::Local<v8::Object> receiver) {
      if (receiver.IsEmpty() || receiver->InternalFieldCount() == 0)
        return 0U;
      auto data = receiver->GetInternalField(0);
      if (!data->IsValue())
        return 0U;
      auto value = data.As<v8::Value>();
      return value->IsUint32() ? value.As<v8::Uint32>()->Value() : 0U;
    }

    window_selection_state *selection_for(v8::Local<v8::Object> receiver) {
      const auto root_id = selection_root_id(receiver);
      const auto found = window_selections.find(root_id);
      return found == window_selections.end() ? nullptr : &found->second;
    }

    static bool selection_contains_node(const dom_node &root,
                                        const dom_node &node) {
      for (auto *current_node = &node; current_node != nullptr;
           current_node = current_node->parent) {
        if (current_node == &root)
          return true;
      }
      return false;
    }

    bool validate_selection(window_selection_state &selection) {
      if (!selection.has_range)
        return false;
      auto *root = document.find_by_native_id(selection.root_id);
      auto *anchor = document.find_by_native_id(selection.anchor_node_id);
      auto *focus = document.find_by_native_id(selection.focus_node_id);
      if (root == nullptr || anchor == nullptr || focus == nullptr ||
          !selection_contains_node(*root, *anchor) ||
          !selection_contains_node(*root, *focus)) {
        selection.has_range = false;
        selection.anchor_node_id = 0U;
        selection.focus_node_id = 0U;
        selection.anchor_offset = 0U;
        selection.focus_offset = 0U;
        return false;
      }
      const auto anchor_length =
          decode_wtf8_to_utf16(anchor->text_content).size();
      const auto focus_length =
          decode_wtf8_to_utf16(focus->text_content).size();
      selection.anchor_offset =
          std::min(selection.anchor_offset, anchor_length);
      selection.focus_offset = std::min(selection.focus_offset, focus_length);
      return true;
    }

    void refresh_selection_text_nodes(window_selection_state &selection,
                                      dom_node &root) {
      if (selection.text_generation == document.scene_generation())
        return;
      selection.text_nodes.clear();
      const auto collect = [&](const auto &recurse, dom_node &node,
                               bool hidden) -> void {
        hidden = hidden || !node.visible ||
                 node.style.display == display_mode::none ||
                 node.attributes.contains("hidden") || node.tag == "script" ||
                 node.tag == "style" || node.tag == "noscript" ||
                 node.tag == "template";
        if (hidden)
          return;
        if (node.kind == dom_node_kind::text && !node.text_content.empty())
          selection.text_nodes.push_back(&node);
        for (auto *child : node.children) {
          if (child != nullptr)
            recurse(recurse, *child, hidden);
        }
      };
      collect(collect, root, false);
      selection.text_generation = document.scene_generation();
    }

    v8::Local<v8::Object> wrap_selection_node(dom_node &node) {
      auto node_context = context_for_node(node);
      v8::Context::Scope node_scope(node_context);
      return wrap_node(node);
    }

    void clear_selections_containing(const dom_node &subtree) {
      auto selection_changed = false;
      if (active_outbound_drag.has_value()) {
        auto* source = document.find_by_native_id(
            active_outbound_drag->source_node_id);
        if (source != nullptr && subtree_contains(subtree, source)) {
          active_outbound_drag.reset();
          std::lock_guard lock(host_request_mutex);
          std::erase_if(outbound_drag_requests, [&](const auto& request) {
            return request != nullptr
                && request->view.source_node_id == source->id;
          });
        }
      }
      for (auto &[root_id, selection] : window_selections) {
        static_cast<void>(root_id);
        selection.text_generation = std::numeric_limits<uint64_t>::max();
        selection.text_nodes.clear();
        if (!selection.has_range)
          continue;
        auto *anchor = document.find_by_native_id(selection.anchor_node_id);
        auto *focus = document.find_by_native_id(selection.focus_node_id);
        if ((anchor != nullptr && subtree_contains(subtree, anchor)) ||
            (focus != nullptr && subtree_contains(subtree, focus))) {
          selection_changed = true;
          selection.has_range = false;
          selection.anchor_node_id = 0U;
          selection.focus_node_id = 0U;
          selection.anchor_offset = 0U;
          selection.focus_offset = 0U;
        }
      }
      if (selection_changed) selection_paint_changed();
    }

    static window_selection_state *
    selection_callback_state(v8::Isolate *isolate,
                             v8::Local<v8::Object> receiver) {
      auto *self = current(isolate);
      return self == nullptr ? nullptr : self->selection_for(receiver);
    }

    static void
    selection_anchor_node_get(v8::Local<v8::Name>,
                              const v8::PropertyCallbackInfo<v8::Value> &info) {
      auto *self = current(info.GetIsolate());
      auto *selection =
          self == nullptr ? nullptr : self->selection_for(info.Holder());
      if (selection == nullptr || !self->validate_selection(*selection)) {
        info.GetReturnValue().Set(v8::Null(info.GetIsolate()));
        return;
      }
      auto *node = self->document.find_by_native_id(selection->anchor_node_id);
      info.GetReturnValue().Set(self->wrap_selection_node(*node));
    }

    static void
    selection_focus_node_get(v8::Local<v8::Name>,
                             const v8::PropertyCallbackInfo<v8::Value> &info) {
      auto *self = current(info.GetIsolate());
      auto *selection =
          self == nullptr ? nullptr : self->selection_for(info.Holder());
      if (selection == nullptr || !self->validate_selection(*selection)) {
        info.GetReturnValue().Set(v8::Null(info.GetIsolate()));
        return;
      }
      auto *node = self->document.find_by_native_id(selection->focus_node_id);
      info.GetReturnValue().Set(self->wrap_selection_node(*node));
    }

    static void selection_anchor_offset_get(
        v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value> &info) {
      auto *selection =
          selection_callback_state(info.GetIsolate(), info.Holder());
      auto *self = current(info.GetIsolate());
      const auto offset =
          selection != nullptr && self->validate_selection(*selection)
              ? selection->anchor_offset
              : 0U;
      info.GetReturnValue().Set(v8::Number::New(info.GetIsolate(), offset));
    }

    static void selection_focus_offset_get(
        v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value> &info) {
      auto *selection =
          selection_callback_state(info.GetIsolate(), info.Holder());
      auto *self = current(info.GetIsolate());
      const auto offset =
          selection != nullptr && self->validate_selection(*selection)
              ? selection->focus_offset
              : 0U;
      info.GetReturnValue().Set(v8::Number::New(info.GetIsolate(), offset));
    }

    static void
    selection_range_count_get(v8::Local<v8::Name>,
                              const v8::PropertyCallbackInfo<v8::Value> &info) {
      auto *selection =
          selection_callback_state(info.GetIsolate(), info.Holder());
      auto *self = current(info.GetIsolate());
      info.GetReturnValue().Set(v8::Integer::New(
          info.GetIsolate(),
          selection != nullptr && self->validate_selection(*selection) ? 1
                                                                       : 0));
    }

    static void selection_is_collapsed_get(
        v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value> &info) {
      auto *selection =
          selection_callback_state(info.GetIsolate(), info.Holder());
      auto *self = current(info.GetIsolate());
      const auto valid =
          selection != nullptr && self->validate_selection(*selection);
      info.GetReturnValue().Set(v8::Boolean::New(
          info.GetIsolate(),
          !valid || (selection->anchor_node_id == selection->focus_node_id &&
                     selection->anchor_offset == selection->focus_offset)));
    }

    static void
    selection_type_get(v8::Local<v8::Name>,
                       const v8::PropertyCallbackInfo<v8::Value> &info) {
      auto *selection =
          selection_callback_state(info.GetIsolate(), info.Holder());
      auto *self = current(info.GetIsolate());
      const auto valid =
          selection != nullptr && self->validate_selection(*selection);
      const auto collapsed =
          !valid || (selection->anchor_node_id == selection->focus_node_id &&
                     selection->anchor_offset == selection->focus_offset);
      info.GetReturnValue().Set(js_string(info.GetIsolate(), !valid ? "None"
                                                             : collapsed
                                                                 ? "Caret"
                                                                 : "Range"));
    }

    static void selection_remove_all_ranges(
        const v8::FunctionCallbackInfo<v8::Value> &info) {
      auto *self = current(info.GetIsolate());
      auto *selection =
          selection_callback_state(info.GetIsolate(), info.This());
      if (selection == nullptr)
        return;
      selection->has_range = false;
      selection->anchor_node_id = 0U;
      selection->focus_node_id = 0U;
      selection->anchor_offset = 0U;
      selection->focus_offset = 0U;
      if (self != nullptr) self->selection_paint_changed();
    }

    static void
    selection_collapse(const v8::FunctionCallbackInfo<v8::Value> &info) {
      auto *self = current(info.GetIsolate());
      auto *selection =
          self == nullptr ? nullptr : self->selection_for(info.This());
      if (selection == nullptr)
        return;
      if (info.Length() == 0 || info[0]->IsNullOrUndefined()) {
        selection_remove_all_ranges(info);
        return;
      }
      auto *node =
          info[0]->IsObject() ? unwrap_node(info[0].As<v8::Object>()) : nullptr;
      auto *root = self->document.find_by_native_id(selection->root_id);
      if (node == nullptr || root == nullptr ||
          !selection_contains_node(*root, *node)) {
        info.GetIsolate()->ThrowException(v8::Exception::TypeError(
            js_string(info.GetIsolate(),
                      "Selection.collapse node is outside this document")));
        return;
      }
      const auto requested =
          info.Length() > 1
              ? info[1]
                    ->IntegerValue(info.GetIsolate()->GetCurrentContext())
                    .FromMaybe(0)
              : 0;
      const auto maximum = node->kind == dom_node_kind::text
                               ? decode_wtf8_to_utf16(node->text_content).size()
                               : node->children.size();
      if (requested < 0 || static_cast<size_t>(requested) > maximum) {
        info.GetIsolate()->ThrowException(v8::Exception::RangeError(
            js_string(info.GetIsolate(),
                      "Selection.collapse offset is outside the node")));
        return;
      }
      selection->has_range = true;
      selection->anchor_node_id = node->id;
      selection->focus_node_id = node->id;
      selection->anchor_offset = static_cast<size_t>(requested);
      selection->focus_offset = static_cast<size_t>(requested);
      self->selection_paint_changed();
    }

    static void
    selection_to_string(const v8::FunctionCallbackInfo<v8::Value> &info) {
      auto *self = current(info.GetIsolate());
      auto *selection =
          self == nullptr ? nullptr : self->selection_for(info.This());
      if (selection == nullptr || !self->validate_selection(*selection)) {
        info.GetReturnValue().Set(js_string(info.GetIsolate(), ""));
        return;
      }
      auto *root = self->document.find_by_native_id(selection->root_id);
      auto *anchor = self->document.find_by_native_id(selection->anchor_node_id);
      auto *focus = self->document.find_by_native_id(selection->focus_node_id);
      if (root == nullptr || anchor == nullptr || focus == nullptr) {
        info.GetReturnValue().Set(js_string(info.GetIsolate(), ""));
        return;
      }
      if (anchor == focus) {
        auto units = decode_wtf8_to_utf16(anchor->text_content);
        const auto start =
            std::min(selection->anchor_offset, selection->focus_offset);
        const auto end =
            std::max(selection->anchor_offset, selection->focus_offset);
        if (end <= start) {
          info.GetReturnValue().Set(js_string(info.GetIsolate(), ""));
          return;
        }
        info.GetReturnValue().Set(
            v8::String::NewFromTwoByte(info.GetIsolate(), units.data() + start,
                                       v8::NewStringType::kNormal,
                                       static_cast<int>(end - start))
                .ToLocalChecked());
        return;
      }
      self->refresh_selection_text_nodes(*selection, *root);
      const auto anchor_it = std::find(selection->text_nodes.begin(),
                                       selection->text_nodes.end(), anchor);
      const auto focus_it = std::find(selection->text_nodes.begin(),
                                      selection->text_nodes.end(), focus);
      if (anchor_it == selection->text_nodes.end() ||
          focus_it == selection->text_nodes.end()) {
        info.GetReturnValue().Set(js_string(info.GetIsolate(), ""));
        return;
      }
      const auto forward = anchor_it < focus_it;
      auto first_it = forward ? anchor_it : focus_it;
      auto last_it = forward ? focus_it : anchor_it;
      const auto first_offset = forward ? selection->anchor_offset
                                        : selection->focus_offset;
      const auto last_offset = forward ? selection->focus_offset
                                       : selection->anchor_offset;
      std::vector<uint16_t> selected;
      for (auto it = first_it; it <= last_it; ++it) {
        auto units = decode_wtf8_to_utf16((*it)->text_content);
        const auto begin = it == first_it
            ? std::min(first_offset, units.size()) : 0U;
        const auto end = it == last_it
            ? std::min(last_offset, units.size()) : units.size();
        if (end > begin)
          selected.insert(selected.end(), units.begin() + begin,
                          units.begin() + end);
      }
      if (selected.empty()) {
        info.GetReturnValue().Set(js_string(info.GetIsolate(), ""));
        return;
      }
      info.GetReturnValue().Set(
          v8::String::NewFromTwoByte(info.GetIsolate(), selected.data(),
                                     v8::NewStringType::kNormal,
                                     static_cast<int>(selected.size()))
              .ToLocalChecked());
    }

    static void
    selection_get_range_at(const v8::FunctionCallbackInfo<v8::Value> &info) {
      auto *self = current(info.GetIsolate());
      auto *selection =
          self == nullptr ? nullptr : self->selection_for(info.This());
      const auto index =
          info.Length() > 0
              ? info[0]
                    ->IntegerValue(info.GetIsolate()->GetCurrentContext())
                    .FromMaybe(-1)
              : -1;
      if (index != 0 || selection == nullptr ||
          !self->validate_selection(*selection)) {
        info.GetIsolate()->ThrowException(v8::Exception::RangeError(js_string(
            info.GetIsolate(), "Selection range index is out of bounds")));
        return;
      }
      auto local_context = info.GetIsolate()->GetCurrentContext();
      auto range = v8::Object::New(info.GetIsolate());
      auto *anchor =
          self->document.find_by_native_id(selection->anchor_node_id);
      auto *focus = self->document.find_by_native_id(selection->focus_node_id);
      range
          ->Set(local_context, js_string(info.GetIsolate(), "startContainer"),
                self->wrap_selection_node(*anchor))
          .Check();
      range
          ->Set(local_context, js_string(info.GetIsolate(), "startOffset"),
                v8::Number::New(info.GetIsolate(), selection->anchor_offset))
          .Check();
      range
          ->Set(local_context, js_string(info.GetIsolate(), "endContainer"),
                self->wrap_selection_node(*focus))
          .Check();
      range
          ->Set(local_context, js_string(info.GetIsolate(), "endOffset"),
                v8::Number::New(info.GetIsolate(), selection->focus_offset))
          .Check();
      range
          ->Set(local_context, js_string(info.GetIsolate(), "collapsed"),
                v8::Boolean::New(
                    info.GetIsolate(),
                    selection->anchor_node_id == selection->focus_node_id &&
                        selection->anchor_offset == selection->focus_offset))
          .Check();
      info.GetReturnValue().Set(range);
    }

    v8::Local<v8::Object> ensure_selection(dom_node &root) {
      auto [entry, inserted] = window_selections.try_emplace(root.id);
      auto &selection = entry->second;
      selection.root_id = root.id;
      if (!inserted && !selection.object.IsEmpty()) {
        return selection.object.Get(isolate);
      }
      auto local_context = isolate->GetCurrentContext();
      auto object_template = v8::ObjectTemplate::New(isolate);
      object_template->SetInternalFieldCount(1);
      object_template->SetNativeDataProperty(js_string(isolate, "anchorNode"),
                                             selection_anchor_node_get);
      object_template->SetNativeDataProperty(js_string(isolate, "baseNode"),
                                             selection_anchor_node_get);
      object_template->SetNativeDataProperty(js_string(isolate, "focusNode"),
                                             selection_focus_node_get);
      object_template->SetNativeDataProperty(js_string(isolate, "extentNode"),
                                             selection_focus_node_get);
      object_template->SetNativeDataProperty(js_string(isolate, "anchorOffset"),
                                             selection_anchor_offset_get);
      object_template->SetNativeDataProperty(js_string(isolate, "baseOffset"),
                                             selection_anchor_offset_get);
      object_template->SetNativeDataProperty(js_string(isolate, "focusOffset"),
                                             selection_focus_offset_get);
      object_template->SetNativeDataProperty(js_string(isolate, "extentOffset"),
                                             selection_focus_offset_get);
      object_template->SetNativeDataProperty(js_string(isolate, "rangeCount"),
                                             selection_range_count_get);
      object_template->SetNativeDataProperty(js_string(isolate, "isCollapsed"),
                                             selection_is_collapsed_get);
      object_template->SetNativeDataProperty(js_string(isolate, "type"),
                                             selection_type_get);
      object_template->Set(
          js_string(isolate, "removeAllRanges"),
          v8::FunctionTemplate::New(isolate, selection_remove_all_ranges));
      object_template->Set(
          js_string(isolate, "empty"),
          v8::FunctionTemplate::New(isolate, selection_remove_all_ranges));
      object_template->Set(
          js_string(isolate, "removeRange"),
          v8::FunctionTemplate::New(isolate, selection_remove_all_ranges));
      object_template->Set(
          js_string(isolate, "collapse"),
          v8::FunctionTemplate::New(isolate, selection_collapse));
      object_template->Set(
          js_string(isolate, "getRangeAt"),
          v8::FunctionTemplate::New(isolate, selection_get_range_at));
      object_template->Set(
          js_string(isolate, "toString"),
          v8::FunctionTemplate::New(isolate, selection_to_string));
      auto object =
          object_template->NewInstance(local_context).ToLocalChecked();
      object->SetInternalField(0,
                               v8::Integer::NewFromUnsigned(isolate, root.id));
      selection.object.Reset(isolate, object);
      return object;
    }

    static void get_selection(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
      auto *self = current(info.GetIsolate());
      if (self == nullptr) {
        info.GetReturnValue().Set(v8::Null(info.GetIsolate()));
        return;
      }
      auto local_context = info.GetIsolate()->GetCurrentContext();
      auto *root = static_cast<dom_node *>(
          local_context->GetAlignedPointerFromEmbedderData(
              1, v8::kEmbedderDataTypeTagDefault));
      if (root == nullptr)
        root = &self->document.body();
      info.GetReturnValue().Set(self->ensure_selection(*root));
    }

    static bool window_find_word_unit(uint16_t unit) {
      return (unit >= '0' && unit <= '9') || (unit >= 'A' && unit <= 'Z') ||
             (unit >= 'a' && unit <= 'z') || unit == '_' || unit >= 0x80U;
    }

    static uint16_t window_find_fold_ascii(uint16_t unit) {
      return unit >= 'A' && unit <= 'Z'
                 ? static_cast<uint16_t>(unit + ('a' - 'A'))
                 : unit;
    }

    static bool window_find_matches(const std::vector<uint16_t> &text,
                                    const std::vector<uint16_t> &query,
                                    size_t offset, bool case_sensitive,
                                    bool whole_word) {
      if (query.empty() || offset > text.size() ||
          query.size() > text.size() - offset)
        return false;
      for (size_t index = 0U; index < query.size(); ++index) {
        const auto left = case_sensitive
                              ? text[offset + index]
                              : window_find_fold_ascii(text[offset + index]);
        const auto right = case_sensitive
                               ? query[index]
                               : window_find_fold_ascii(query[index]);
        if (left != right)
          return false;
      }
      if (!whole_word)
        return true;
      const auto before_word =
          offset > 0U && window_find_word_unit(text[offset - 1U]);
      const auto after = offset + query.size();
      const auto after_word =
          after < text.size() && window_find_word_unit(text[after]);
      return !before_word && !after_word;
    }

    static bool window_find_matches_ascii(std::string_view text,
                                          std::string_view query, size_t offset,
                                          bool case_sensitive,
                                          bool whole_word) {
      if (query.empty() || offset > text.size() ||
          query.size() > text.size() - offset)
        return false;
      for (size_t index = 0U; index < query.size(); ++index) {
        const auto left = static_cast<uint16_t>(
            static_cast<unsigned char>(text[offset + index]));
        const auto right =
            static_cast<uint16_t>(static_cast<unsigned char>(query[index]));
        if ((case_sensitive ? left : window_find_fold_ascii(left)) !=
            (case_sensitive ? right : window_find_fold_ascii(right))) {
          return false;
        }
      }
      if (!whole_word)
        return true;
      const auto before_word =
          offset > 0U &&
          window_find_word_unit(static_cast<uint8_t>(text[offset - 1U]));
      const auto after = offset + query.size();
      const auto after_word =
          after < text.size() &&
          window_find_word_unit(static_cast<uint8_t>(text[after]));
      return !before_word && !after_word;
    }

    static void window_find(const v8::FunctionCallbackInfo<v8::Value> &info) {
      auto *self = current(info.GetIsolate());
      if (self == nullptr || info.Length() == 0) {
        info.GetReturnValue().Set(v8::False(info.GetIsolate()));
        return;
      }
      const auto query_text = to_wtf8(info.GetIsolate(), info[0]);
      const auto query = decode_wtf8_to_utf16(query_text);
      constexpr size_t maximum_query_units = 1024U * 1024U;
      if (query.empty() || query.size() > maximum_query_units) {
        info.GetReturnValue().Set(v8::False(info.GetIsolate()));
        return;
      }
      const auto case_sensitive =
          info.Length() > 1 && info[1]->BooleanValue(info.GetIsolate());
      const auto backwards =
          info.Length() > 2 && info[2]->BooleanValue(info.GetIsolate());
      const auto wrap_around =
          info.Length() > 3 && info[3]->BooleanValue(info.GetIsolate());
      const auto whole_word =
          info.Length() > 4 && info[4]->BooleanValue(info.GetIsolate());
      auto local_context = info.GetIsolate()->GetCurrentContext();
      auto *root = static_cast<dom_node *>(
          local_context->GetAlignedPointerFromEmbedderData(
              1, v8::kEmbedderDataTypeTagDefault));
      if (root == nullptr)
        root = &self->document.body();
      auto &selection =
          self->window_selections.try_emplace(root->id).first->second;
      selection.root_id = root->id;
      static_cast<void>(self->validate_selection(selection));

      self->refresh_selection_text_nodes(selection, *root);
      const auto &text_nodes = selection.text_nodes;
      if (text_nodes.empty()) {
        info.GetReturnValue().Set(v8::False(info.GetIsolate()));
        return;
      }

      const auto ascii_query =
          std::all_of(query_text.begin(), query_text.end(),
                      [](unsigned char value) { return value < 0x80U; });
      const auto text_length = [&](const dom_node &node) {
        if (ascii_query &&
            std::all_of(node.text_content.begin(), node.text_content.end(),
                        [](unsigned char value) { return value < 0x80U; })) {
          return node.text_content.size();
        }
        return decode_wtf8_to_utf16(node.text_content).size();
      };
      size_t current_index = backwards ? text_nodes.size() - 1U : 0U;
      size_t current_offset =
          backwards ? text_length(*text_nodes[current_index]) : 0U;
      auto has_current = false;
      if (selection.has_range) {
        const auto current = std::find_if(
            text_nodes.begin(), text_nodes.end(), [&](const auto *node) {
              return node->id == (backwards ? selection.anchor_node_id
                                            : selection.focus_node_id);
            });
        if (current != text_nodes.end()) {
          current_index = static_cast<size_t>(current - text_nodes.begin());
          current_offset =
              backwards ? selection.anchor_offset : selection.focus_offset;
          has_current = true;
        }
      }

      struct match_result {
        size_t node_index;
        size_t offset;
      };
      std::optional<match_result> match;
      const auto find_forward = [&](size_t first_node, size_t first_offset,
                                    size_t last_node,
                                    std::optional<size_t> last_limit) {
        for (size_t node_index = first_node;
             !match.has_value() && node_index <= last_node; ++node_index) {
          const auto &raw = text_nodes[node_index]->text_content;
          const auto ascii_text =
              ascii_query &&
              std::all_of(raw.begin(), raw.end(),
                          [](unsigned char value) { return value < 0x80U; });
          const auto units =
              ascii_text ? std::vector<uint16_t>{} : decode_wtf8_to_utf16(raw);
          const auto text_size = ascii_text ? raw.size() : units.size();
          const auto start = node_index == first_node ? first_offset : 0U;
          const auto limit = last_limit.has_value() && node_index == last_node
                                 ? std::min(*last_limit, text_size)
                                 : text_size;
          for (size_t offset = std::min(start, text_size);
               offset + query.size() <= limit; ++offset) {
            const auto matches =
                ascii_text
                    ? window_find_matches_ascii(raw, query_text, offset,
                                                case_sensitive, whole_word)
                    : window_find_matches(units, query, offset, case_sensitive,
                                          whole_word);
            if (matches) {
              match = match_result{node_index, offset};
              break;
            }
          }
        }
      };
      const auto find_backward = [&](size_t first_node, size_t first_limit,
                                     size_t last_node,
                                     std::optional<size_t> last_floor) {
        for (size_t node_index = first_node; !match.has_value();) {
          const auto &raw = text_nodes[node_index]->text_content;
          const auto ascii_text =
              ascii_query &&
              std::all_of(raw.begin(), raw.end(),
                          [](unsigned char value) { return value < 0x80U; });
          const auto units =
              ascii_text ? std::vector<uint16_t>{} : decode_wtf8_to_utf16(raw);
          const auto text_size = ascii_text ? raw.size() : units.size();
          auto limit = node_index == first_node
                           ? std::min(first_limit, text_size)
                           : text_size;
          const auto floor = last_floor.has_value() && node_index == last_node
                                 ? std::min(*last_floor, text_size)
                                 : 0U;
          while (limit >= query.size() && limit - query.size() >= floor) {
            const auto offset = limit - query.size();
            const auto matches =
                ascii_text
                    ? window_find_matches_ascii(raw, query_text, offset,
                                                case_sensitive, whole_word)
                    : window_find_matches(units, query, offset, case_sensitive,
                                          whole_word);
            if (matches) {
              match = match_result{node_index, offset};
              break;
            }
            if (limit == 0U)
              break;
            --limit;
          }
          if (match.has_value() || node_index == last_node)
            break;
          --node_index;
        }
      };

      if (!backwards) {
        find_forward(current_index, current_offset, text_nodes.size() - 1U,
                     std::nullopt);
        if (!match.has_value() && wrap_around && has_current) {
          find_forward(0U, 0U, current_index, current_offset);
        } else if (!match.has_value() && wrap_around && !has_current) {
          find_forward(0U, 0U, text_nodes.size() - 1U, std::nullopt);
        }
      } else {
        find_backward(current_index, current_offset, 0U, std::nullopt);
        if (!match.has_value() && wrap_around && has_current) {
          const auto last_index = text_nodes.size() - 1U;
          const auto last_size = text_length(*text_nodes[last_index]);
          find_backward(last_index, last_size, current_index, current_offset);
        }
      }
      if (!match.has_value()) {
        info.GetReturnValue().Set(v8::False(info.GetIsolate()));
        return;
      }
      auto *node = text_nodes[match->node_index];
      selection.has_range = true;
      selection.anchor_node_id = node->id;
      selection.focus_node_id = node->id;
      selection.anchor_offset = match->offset;
      selection.focus_offset = match->offset + query.size();
      self->selection_paint_changed();
      info.GetReturnValue().Set(v8::True(info.GetIsolate()));
    }

    static bool insertion_would_create_cycle(
        const dom_node* parent,
        const dom_node* child) noexcept
    {
        if (parent == nullptr || child == nullptr) return false;
        for (auto* ancestor = parent; ancestor != nullptr; ancestor = ancestor->parent) {
            if (ancestor == child) return true;
        }
        return false;
    }

    static void throw_hierarchy_request(v8::Isolate* isolate, std::string_view operation)
    {
        const auto message = std::string(operation)
            + " cannot insert an ancestor into its descendant";
        isolate->ThrowException(v8::Exception::TypeError(js_string(
            isolate,
            message.c_str())));
    }

    static void append_child(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        custom_element_reaction_scope reactions(*self);
        binding_callback_timer binding_timer(
            self->profile(binding_category::dom_mutation),
            self->profile_named("Node.appendChild"));
        auto* parent = unwrap_node(info.This());
        auto* child = info.Length() > 0 && info[0]->IsObject()
            ? unwrap_node(info[0].As<v8::Object>())
            : nullptr;
        if (parent == nullptr || child == nullptr) {
            info.GetIsolate()->ThrowException(v8::Exception::TypeError(
                js_string(info.GetIsolate(), "appendChild requires a native node")));
            return;
        }
        const auto radio_group_state=self->capture_radio_group_selector_state();
        if (parent != nullptr && child != nullptr && child->tag == "#document-fragment") {
            if (std::any_of(
                    child->children.begin(),
                    child->children.end(),
                    [parent](const auto* fragment_child) {
                        return insertion_would_create_cycle(parent, fragment_child);
                    })) {
                throw_hierarchy_request(info.GetIsolate(), "appendChild");
                return;
            }
            // The DOM insertion algorithm returns when a DocumentFragment has
            // no children. There are no range, observer, adoption, lifecycle,
            // focus, or selection effects to route through visual scheduling.
            if (child->children.empty()) {
                info.GetReturnValue().Set(info[0]);
                return;
            }
            auto children = std::move(child->children);
            const auto changes_base = std::any_of(
                children.begin(), children.end(), [&](const auto* candidate) {
                    return candidate != nullptr && self->subtree_contains_base(*candidate);
                });
            child->children.clear();
            for (auto* fragment_child : children) {
                if (fragment_child == nullptr) continue;
                fragment_child->parent = nullptr;
                fragment_child->visible = true;
                if (!self->document.append_child(*parent, *fragment_child)) continue;
#if defined(WEBSCENE_NATIVE_ENGINE_FRAGMENT_ATTACH_CONTROL)
                self->recascade_connected_subtree(*fragment_child);
                self->recascade_connected_node(*parent);
#endif
            }
#if !defined(WEBSCENE_NATIVE_ENGINE_FRAGMENT_ATTACH_CONTROL)
            // DocumentFragment insertion is one tree mutation. Match structural
            // selectors against the completed sibling list and recascade the
            // affected parent subtree once. Styling every child and parent as
            // it is transferred repeats selector matching and leaves earlier
            // siblings with transient :last-child/:nth-child results.
            self->recascade_completed_child_list(*parent, children);
#endif
            self->recascade_radio_group_selector_state_changes(radio_group_state);
            for (auto* fragment_child : children) {
                if (fragment_child == nullptr || fragment_child->parent != parent) continue;
                self->activate_connected_stylesheet(*fragment_child);
                self->enqueue_connected_resources_subtree(
                    *fragment_child,
                    self->wrap_node(*fragment_child),
                    info.GetIsolate()->GetCurrentContext());
                self->enqueue_iframe_hydration_if_needed(*fragment_child);
                self->notify_custom_elements_subtree(*fragment_child, "connected");
            }
            synchronize_textarea_value_from_children(*parent);
            if (changes_base) self->invalidate_document_base(*parent);
            self->document.mark_dirty();
            info.GetReturnValue().Set(info[0]);
            return;
        }
        if (insertion_would_create_cycle(parent, child)) {
            throw_hierarchy_request(info.GetIsolate(), "appendChild");
            return;
        }
        auto* previous_parent = child->parent;
        auto* previous_root = previous_parent != nullptr && self->subtree_contains_base(*child)
            ? self->css_cascade_root_for_node(*previous_parent)
            : nullptr;
        if (child != nullptr && child->parent != nullptr) {
            if (self->is_connected(*child)) {
                self->notify_custom_elements_subtree(*child, "disconnected");
            }
            auto* old_parent = child->parent;
            std::erase(old_parent->children, child);
            synchronize_textarea_value_from_children(*old_parent);
            child->parent = nullptr;
        }
        if (!self->document.append_child(*parent, *child)) {
            info.GetIsolate()->ThrowException(v8::Exception::TypeError(
                js_string(info.GetIsolate(), "appendChild requires a detached native node")));
            return;
        }
        // The completed child list changes existing siblings too, including
        // functional selectors containing combinators. Reparenting also
        // invalidates the source list, not only the inserted node.
        if (previous_parent != nullptr && previous_parent != parent)
            self->recascade_changed_child_list(*previous_parent, nullptr, child);
        self->recascade_changed_child_list(*parent, child);
        self->recascade_radio_group_selector_state_changes(radio_group_state);
        self->activate_connected_stylesheet(*child);
        self->enqueue_connected_resources_subtree(
            *child,
            info[0].As<v8::Object>(),
            info.GetIsolate()->GetCurrentContext());
        self->enqueue_iframe_hydration_if_needed(*child);
        self->notify_custom_elements_subtree(*child, "connected");
        if (self->subtree_contains_base(*child)) {
            if (previous_root != nullptr) self->invalidate_document_base_root(previous_root);
            self->invalidate_document_base(*parent);
        }
        synchronize_textarea_value_from_children(*parent);
        info.GetReturnValue().Set(info[0]);
    }

    static void has_child_nodes(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(
            self->profile(binding_category::dom_query),
            self->profile_named("Node.hasChildNodes"));
        const auto* node = unwrap_node(info.This());
        info.GetReturnValue().Set(v8::Boolean::New(
            info.GetIsolate(),
            node != nullptr && !node->children.empty()));
    }

#include "webscene_v8_runtime_variadic_dom.inc"

    static void append_nodes(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        insert_variadic_nodes(info, variadic_insertion::append);
    }

    static void remove_child(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        custom_element_reaction_scope reactions(*self);
        binding_callback_timer binding_timer(
            self->profile(binding_category::dom_mutation),
            self->profile_named("Node.removeChild"));
        auto* parent = unwrap_node(info.This());
        auto* child = info.Length() > 0 && info[0]->IsObject()
            ? unwrap_node(info[0].As<v8::Object>())
            : nullptr;
        if (parent == nullptr || child == nullptr || child->parent != parent) {
            info.GetIsolate()->ThrowException(v8::Exception::TypeError(
                js_string(info.GetIsolate(), "removeChild requires a child of this native node")));
            return;
        }
        const auto radio_group_state=self->capture_radio_group_selector_state();
        if (!self->blur_active_element_before_detach(*child)) return;
        if (self->is_connected(*child)) {
            self->notify_custom_elements_subtree(*child, "disconnected");
        }
        self->clear_selections_containing(*child);
        self->deactivate_connected_stylesheets(*child);
        const auto changes_base = self->subtree_contains_base(*child);
        std::erase(parent->children, child);
        child->parent = nullptr;
        self->recascade_changed_child_list(*parent, nullptr, child);
        self->recascade_radio_group_selector_state_changes(radio_group_state);
        self->weaken_detached_subtree_wrappers(*child);
        if (changes_base) self->invalidate_document_base(*parent);
        synchronize_textarea_value_from_children(*parent);
        self->document.mark_dirty();
        info.GetReturnValue().Set(info[0]);
    }

    static void replace_child(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        custom_element_reaction_scope reactions(*self);
        auto* parent = unwrap_node(info.This());
        auto* replacement = info.Length() > 0 && info[0]->IsObject()
            ? unwrap_node(info[0].As<v8::Object>())
            : nullptr;
        auto* previous = info.Length() > 1 && info[1]->IsObject()
            ? unwrap_node(info[1].As<v8::Object>())
            : nullptr;
        if (parent == nullptr || replacement == nullptr || previous == nullptr
            || previous->parent != parent) {
            info.GetIsolate()->ThrowException(v8::Exception::TypeError(
                js_string(info.GetIsolate(), "replaceChild requires a child of this native node")));
            return;
        }
        if (replacement == previous) {
            info.GetReturnValue().Set(info[1]);
            return;
        }
        const auto radio_group_state=self->capture_radio_group_selector_state();
        std::vector<dom_node*> previous_parents;
        std::vector<dom_node*> replacements;
        if (replacement->tag == "#document-fragment") {
            replacements = replacement->children;
        } else {
            replacements.push_back(replacement);
        }
        if (std::any_of(
                replacements.begin(),
                replacements.end(),
                [parent](const auto* candidate) {
                    return insertion_would_create_cycle(parent, candidate);
                })) {
            throw_hierarchy_request(info.GetIsolate(), "replaceChild");
            return;
        }
        if (!self->blur_active_element_before_detach(*previous)) return;
        for (auto* candidate : replacements) {
            if (candidate == nullptr || candidate == previous) continue;
            if (candidate->parent != nullptr) {
                if (self->is_connected(*candidate)) {
                    self->notify_custom_elements_subtree(*candidate, "disconnected");
                }
                auto* old_parent = candidate->parent;
                previous_parents.push_back(old_parent);
                std::erase(old_parent->children, candidate);
                synchronize_textarea_value_from_children(*old_parent);
                candidate->parent = nullptr;
            }
        }
        if (replacement->tag == "#document-fragment") {
            replacement->children.clear();
        }
        const auto iterator = std::find(parent->children.begin(), parent->children.end(), previous);
        if (iterator == parent->children.end()) return;
        if (self->is_connected(*previous)) {
            self->notify_custom_elements_subtree(*previous, "disconnected");
        }
        self->clear_selections_containing(*previous);
        self->deactivate_connected_stylesheets(*previous, false);
        const auto insertion_index = static_cast<size_t>(
            iterator - parent->children.begin());
        parent->children.erase(iterator);
        previous->parent = nullptr;
        self->weaken_detached_subtree_wrappers(*previous);
        auto next_index = insertion_index;
        for (auto* candidate : replacements) {
            if (candidate == nullptr || candidate == previous) continue;
            candidate->parent = parent;
            candidate->visible = true;
            parent->children.insert(
                parent->children.begin()
                    + static_cast<std::ptrdiff_t>(next_index++),
                candidate);
        }
        self->recascade_completed_child_list(*parent, replacements, previous_parents);
        self->recascade_form_structural_dependencies(*parent, nullptr, previous);
        self->recascade_radio_group_selector_state_changes(radio_group_state);
        for (auto* candidate : replacements) {
            if (candidate == nullptr || candidate->parent != parent) continue;
            self->activate_connected_stylesheet(*candidate);
            self->enqueue_connected_resources_subtree(
                *candidate,
                self->wrap_node(*candidate),
                info.GetIsolate()->GetCurrentContext());
            self->enqueue_iframe_hydration_if_needed(*candidate);
            self->notify_custom_elements_subtree(*candidate, "connected");
        }
        synchronize_textarea_value_from_children(*parent);
        self->document.mark_dirty();
        info.GetReturnValue().Set(info[1]);
    }

    static void replace_children(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        insert_variadic_nodes(info, variadic_insertion::replace_children);
    }

    static void remove_element(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        custom_element_reaction_scope reactions(*self);
        auto* node = unwrap_node(info.This());
        if (node == nullptr || node->parent == nullptr) return;
        auto* parent = node->parent;
        const auto radio_group_state=self->capture_radio_group_selector_state();
        if (!self->blur_active_element_before_detach(*node)) return;
        if (self->is_connected(*node)) {
            self->notify_custom_elements_subtree(*node, "disconnected");
        }
        self->clear_selections_containing(*node);
        self->deactivate_connected_stylesheets(*node);
        const auto changes_base = self->subtree_contains_base(*node);
        std::erase(node->parent->children, node);
        node->parent = nullptr;
        self->recascade_changed_child_list(*parent, nullptr, node);
        self->recascade_radio_group_selector_state_changes(radio_group_state);
        node->visible = false;
        self->weaken_detached_subtree_wrappers(*node);
        if (changes_base) self->invalidate_document_base(*parent);
        synchronize_textarea_value_from_children(*parent);
        self->document.mark_dirty();
    }

    static void replace_with(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        insert_variadic_nodes(info, variadic_insertion::replace_with);
    }

    static void insert_adjacent_html(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        custom_element_reaction_scope reactions(*self);
        binding_callback_timer binding_timer(
            self->profile(binding_category::dom_mutation),
            self->profile_named("Element.insertAdjacentHTML"));
        auto* reference = unwrap_node(info.This());
        if (reference == nullptr || info.Length() < 2) return;
        const auto position = lower_html_name(to_utf8(info.GetIsolate(), info[0]));

        dom_node* parent = nullptr;
        size_t insertion_index = 0U;
        if (position == "afterbegin" || position == "beforeend") {
            parent = reference;
            insertion_index =
                position == "afterbegin" ? 0U : parent->children.size();
        } else if ((position == "beforebegin" || position == "afterend")
            && reference->parent != nullptr) {
            parent = reference->parent;
            const auto found = std::find(
                parent->children.begin(), parent->children.end(), reference);
            if (found == parent->children.end()) return;
            insertion_index =
                static_cast<size_t>(found - parent->children.begin())
                + (position == "afterend" ? 1U : 0U);
        } else {
            info.GetIsolate()->ThrowException(v8::Exception::SyntaxError(
                js_string(info.GetIsolate(), "Invalid insertAdjacentHTML position")));
            return;
        }

        const auto html = to_utf8(info.GetIsolate(), info[1]);
        if (!html.empty() && !self->require_runtime_html()) return;
        const auto radio_group_state=self->capture_radio_group_selector_state();
        auto& fragment = self->document.create_element("#document-fragment");
        fragment.visible = false;
        if (!html.empty()) self->parse_inner_html(fragment, html, parent);
        auto children = std::move(fragment.children);
        fragment.children.clear();
        for (auto* child : children) {
            if (child == nullptr) continue;
            child->parent = parent;
            child->visible = true;
            parent->children.insert(
                parent->children.begin()
                    + static_cast<std::ptrdiff_t>(insertion_index++),
                child);
        }
        self->recascade_completed_child_list(*parent, children);
        self->recascade_radio_group_selector_state_changes(radio_group_state);
        for (auto* child : children) {
            if (child == nullptr || child->parent != parent) continue;
            self->activate_connected_stylesheet(*child);
            self->enqueue_connected_resources_subtree(
                *child,
                self->wrap_node(*child),
                info.GetIsolate()->GetCurrentContext());
            self->enqueue_iframe_hydration_if_needed(*child);
            self->notify_custom_elements_subtree(*child, "connected");
        }
        synchronize_textarea_value_from_children(*parent);
        self->document.mark_dirty();
    }

    static void insert_adjacent_element(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        custom_element_reaction_scope reactions(*self);
        binding_callback_timer binding_timer(
            self->profile(binding_category::dom_mutation),
            self->profile_named("Element.insertAdjacentElement"));
        auto* reference = unwrap_node(info.This());
        auto* child = info.Length() > 1 && info[1]->IsObject()
            ? unwrap_node(info[1].As<v8::Object>())
            : nullptr;
        if (reference == nullptr || child == nullptr || reference == child || info.Length() < 1) return;
        const auto position = to_utf8(info.GetIsolate(), info[0]);
        dom_node* parent = nullptr;
        size_t index = 0;
        if (position == "afterbegin" || position == "beforeend") {
            parent = reference;
            index = position == "afterbegin" ? 0U : parent->children.size();
        } else if ((position == "beforebegin" || position == "afterend")
            && reference->parent != nullptr) {
            parent = reference->parent;
            const auto iterator = std::find(parent->children.begin(), parent->children.end(), reference);
            if (iterator == parent->children.end()) return;
            index = static_cast<size_t>(iterator - parent->children.begin())
                + (position == "afterend" ? 1U : 0U);
        } else {
            return;
        }
        if (insertion_would_create_cycle(parent, child)) {
            throw_hierarchy_request(info.GetIsolate(), "insertAdjacentElement");
            return;
        }
        const auto radio_group_state=self->capture_radio_group_selector_state();
        auto* previous_parent = child->parent;
        if (child->parent != nullptr) {
            if (self->is_connected(*child)) {
                self->notify_custom_elements_subtree(*child, "disconnected");
            }
            auto* old_parent = child->parent;
            const auto old = std::find(old_parent->children.begin(), old_parent->children.end(), child);
            if (old != old_parent->children.end()) {
                const auto old_index = static_cast<size_t>(old - old_parent->children.begin());
                old_parent->children.erase(old);
                synchronize_textarea_value_from_children(*old_parent);
                if (old_parent == parent && old_index < index) --index;
            }
            child->parent = nullptr;
        }
        index = std::min(index, parent->children.size());
        child->parent = parent;
        parent->children.insert(parent->children.begin() + static_cast<std::ptrdiff_t>(index), child);
        self->document.mark_dirty();
        if (previous_parent != nullptr && previous_parent != parent)
            self->recascade_changed_child_list(*previous_parent, nullptr, child);
        self->recascade_changed_child_list(*parent, child);
        self->recascade_radio_group_selector_state_changes(radio_group_state);
        self->activate_connected_stylesheet(*child);
        self->notify_custom_elements_subtree(*child, "connected");
        synchronize_textarea_value_from_children(*parent);
        info.GetReturnValue().Set(info[1]);
    }

    static void prepend_child(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        insert_variadic_nodes(info, variadic_insertion::prepend);
    }

    static void insert_sibling(const v8::FunctionCallbackInfo<v8::Value>& info, bool after)
    {
        insert_variadic_nodes(info, after ? variadic_insertion::after : variadic_insertion::before);
    }

    static void insert_before_self(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        insert_sibling(info, false);
    }

    static void insert_after_self(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        insert_sibling(info, true);
    }

    static void insert_before(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        custom_element_reaction_scope reactions(*self);
        binding_callback_timer binding_timer(
            self->profile(binding_category::dom_mutation),
            self->profile_named("Node.insertBefore"));
        self->record_feature(
            "dom",
            "method:Node.insertBefore",
            "supported",
            {},
            "dom-binding");
        auto* parent = unwrap_node(info.This());
        auto* child = info.Length() > 0 && info[0]->IsObject()
            ? unwrap_node(info[0].As<v8::Object>())
            : nullptr;
        auto* before = info.Length() > 1 && info[1]->IsObject()
            ? unwrap_node(info[1].As<v8::Object>())
            : nullptr;
        if (parent == nullptr || child == nullptr) return;
        const auto radio_group_state=self->capture_radio_group_selector_state();
        if (child->tag == "#document-fragment") {
            if (std::any_of(
                    child->children.begin(),
                    child->children.end(),
                    [parent](const auto* fragment_child) {
                        return insertion_would_create_cycle(parent, fragment_child);
                    })) {
                throw_hierarchy_request(info.GetIsolate(), "insertBefore");
                return;
            }
            auto position = before == nullptr
                ? parent->children.end()
                : std::find(parent->children.begin(), parent->children.end(), before);
            if (before != nullptr && position == parent->children.end()) {
                info.GetIsolate()->ThrowException(v8::Exception::TypeError(
                    js_string(info.GetIsolate(), "insertBefore reference is not a child")));
                return;
            }
            auto insertion_index = static_cast<size_t>(position - parent->children.begin());
            auto children = std::move(child->children);
            const auto changes_base = std::any_of(
                children.begin(), children.end(), [&](const auto* candidate) {
                    return candidate != nullptr && self->subtree_contains_base(*candidate);
                });
            child->children.clear();
            for (auto* fragment_child : children) {
                if (fragment_child == nullptr) continue;
                fragment_child->parent = parent;
                fragment_child->visible = true;
                parent->children.insert(
                    parent->children.begin() + static_cast<std::ptrdiff_t>(insertion_index++),
                    fragment_child);
            }
            self->recascade_completed_child_list(*parent, children);
            self->recascade_radio_group_selector_state_changes(radio_group_state);
            for (auto* fragment_child : children) {
                if (fragment_child == nullptr || fragment_child->parent != parent) continue;
                self->activate_connected_stylesheet(*fragment_child);
                self->notify_custom_elements_subtree(
                    *fragment_child, "connected");
            }
            synchronize_textarea_value_from_children(*parent);
            if (changes_base) self->invalidate_document_base(*parent);
            self->document.mark_dirty();
            info.GetReturnValue().Set(info[0]);
            return;
        }
        if (insertion_would_create_cycle(parent, child)) {
            throw_hierarchy_request(info.GetIsolate(), "insertBefore");
            return;
        }
        // The DOM pre-insertion algorithm validates the reference before it
        // mutates either tree. In particular, insertBefore(node, node) on the
        // node's current parent is a no-op. Detaching first made that valid
        // self-reference disappear from parent->children, then incorrectly
        // threw and left the node detached. It also made every rejected
        // foreign reference partially mutate the source tree.
        if (before != nullptr) {
            const auto reference = std::find(
                parent->children.begin(), parent->children.end(), before);
            if (reference == parent->children.end()) {
                info.GetIsolate()->ThrowException(v8::Exception::TypeError(
                    js_string(info.GetIsolate(), "insertBefore reference is not a child")));
                return;
            }
            if (before == child) {
                self->record_feature(
                    "dom",
                    "insertBefore:self-reference",
                    "supported",
                    {},
                    "dom-pre-insertion");
                self->record_composition(
                    "dom-insertbefore-self-reference",
                    child->id);
                info.GetReturnValue().Set(info[0]);
                return;
            }
        }
        auto* previous_parent = child->parent;
        auto* previous_root = previous_parent != nullptr && self->subtree_contains_base(*child)
            ? self->css_cascade_root_for_node(*previous_parent)
            : nullptr;
        if (child->parent != nullptr) {
            if (self->is_connected(*child)) {
                self->notify_custom_elements_subtree(*child, "disconnected");
            }
            auto* old_parent = child->parent;
            std::erase(old_parent->children, child);
            synchronize_textarea_value_from_children(*old_parent);
            child->parent = nullptr;
        }
        if (before == nullptr) {
            if (self->document.append_child(*parent, *child)) {
                if (previous_parent != nullptr && previous_parent != parent)
                    self->recascade_changed_child_list(
                        *previous_parent, nullptr, child);
                self->recascade_changed_child_list(*parent, child);
                self->recascade_radio_group_selector_state_changes(radio_group_state);
                self->activate_connected_stylesheet(*child);
                self->notify_custom_elements_subtree(*child, "connected");
                if (self->subtree_contains_base(*child)) {
                    if (previous_root != nullptr)
                        self->invalidate_document_base_root(previous_root);
                    self->invalidate_document_base(*parent);
                }
                synchronize_textarea_value_from_children(*parent);
                info.GetReturnValue().Set(info[0]);
            }
            return;
        }
        const auto position = std::find(parent->children.begin(), parent->children.end(), before);
        // `before` was validated before detaching `child`; because the
        // self-reference case returned above, it must still be present.
        if (position == parent->children.end()) return;
        child->parent = parent;
        parent->children.insert(position, child);
        self->document.mark_dirty();
        if (previous_parent != nullptr && previous_parent != parent)
            self->recascade_changed_child_list(*previous_parent, nullptr, child);
        self->recascade_changed_child_list(*parent, child);
        self->recascade_radio_group_selector_state_changes(radio_group_state);
        self->activate_connected_stylesheet(*child);
        self->notify_custom_elements_subtree(*child, "connected");
        if (self->subtree_contains_base(*child)) {
            if (previous_root != nullptr) self->invalidate_document_base_root(previous_root);
            self->invalidate_document_base(*parent);
        }
        synchronize_textarea_value_from_children(*parent);
        info.GetReturnValue().Set(info[0]);
    }
