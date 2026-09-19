    static constexpr size_t maximum_outbound_drag_requests = 8U;
    static constexpr size_t maximum_outbound_drag_items = 32U;
    static constexpr size_t maximum_outbound_drag_metadata = 1024U * 1024U;

    std::unique_ptr<native_outbound_drag_request> take_outbound_drag_request()
    {
        std::lock_guard lock(host_request_mutex);
        if (outbound_drag_requests.empty()) return {};
        auto request = std::move(outbound_drag_requests.front());
        outbound_drag_requests.pop_front();
        return request;
    }

    v8::Local<v8::Object> create_outbound_drag_data_transfer(dom_node& target)
    {
        auto local_context = context_for_node(target);
        v8::Context::Scope scope(local_context);
        v8::Local<v8::Value> factory;
        v8::Local<v8::Value> result;
        v8::Local<v8::Value> arguments[]{
            v8::Array::New(isolate), v8::True(isolate)};
        if (!local_context->Global()->Get(local_context,
                js_string(isolate, "__webSceneCreateDragDataTransfer"))
                .ToLocal(&factory)
            || !factory->IsFunction()
            || !factory.As<v8::Function>()->Call(
                local_context, local_context->Global(), 2, arguments)
                .ToLocal(&result)
            || !result->IsObject()) return v8::Object::New(isolate);
        return result.As<v8::Object>();
    }

    static uint32_t outbound_operations_for_effect(std::string_view effect)
    {
        if (effect == "none") return 0U;
        if (effect == "copy") return WEBSCENE_OUTBOUND_DRAG_OPERATION_COPY_V1;
        if (effect == "link") return WEBSCENE_OUTBOUND_DRAG_OPERATION_LINK_V1;
        if (effect == "move") return WEBSCENE_OUTBOUND_DRAG_OPERATION_MOVE_V1;
        if (effect == "copyLink") return WEBSCENE_OUTBOUND_DRAG_OPERATION_COPY_V1
            | WEBSCENE_OUTBOUND_DRAG_OPERATION_LINK_V1;
        if (effect == "copyMove") return WEBSCENE_OUTBOUND_DRAG_OPERATION_COPY_V1
            | WEBSCENE_OUTBOUND_DRAG_OPERATION_MOVE_V1;
        if (effect == "linkMove") return WEBSCENE_OUTBOUND_DRAG_OPERATION_LINK_V1
            | WEBSCENE_OUTBOUND_DRAG_OPERATION_MOVE_V1;
        return WEBSCENE_OUTBOUND_DRAG_OPERATION_COPY_V1
            | WEBSCENE_OUTBOUND_DRAG_OPERATION_LINK_V1
            | WEBSCENE_OUTBOUND_DRAG_OPERATION_MOVE_V1;
    }

    dom_node* outbound_drag_candidate(dom_node* hit)
    {
        for (auto* candidate = hit; candidate != nullptr;
             candidate = candidate->parent) {
            const auto draggable = candidate->attributes.find("draggable");
            if (draggable != candidate->attributes.end()) {
                if (draggable->second == "false") return nullptr;
                if (draggable->second == "true") return candidate;
            }
            if (candidate->tag == "a" && candidate->attributes.contains("href"))
                return candidate;
            if (candidate->tag == "img" && candidate->attributes.contains("src"))
                return candidate;
        }
        if (hit == nullptr) return nullptr;
        auto* root = css_cascade_root_for_node(*hit);
        const auto root_id = root == nullptr ? document.body().id : root->id;
        if (const auto known = window_selections.find(root_id);
            known != window_selections.end() && validate_selection(known->second)
            && (known->second.anchor_node_id != known->second.focus_node_id
                || known->second.anchor_offset != known->second.focus_offset))
            return hit;
        return nullptr;
    }

    std::string outbound_selection_text(dom_node& source)
    {
        auto* root = css_cascade_root_for_node(source);
        const auto root_id = root == nullptr ? document.body().id : root->id;
        const auto known = window_selections.find(root_id);
        if (known == window_selections.end() || !validate_selection(known->second)
            || known->second.object.IsEmpty()) return {};
        auto local_context = context_for_node(source);
        v8::Context::Scope scope(local_context);
        auto selection = known->second.object.Get(isolate);
        v8::Local<v8::Value> function;
        v8::Local<v8::Value> result;
        if (!selection->Get(local_context, js_string(isolate, "toString"))
                .ToLocal(&function)
            || !function->IsFunction()
            || !function.As<v8::Function>()->Call(
                local_context, selection, 0, nullptr).ToLocal(&result)) return {};
        return to_utf8(isolate, result);
    }

    bool add_outbound_item(native_outbound_drag_request& request,
        uint32_t kind, std::string mime, std::string value,
        std::string name = {}, std::string grant = {}, uint64_t byte_size = 0U)
    {
        if (request.owned_items.size() >= maximum_outbound_drag_items) return false;
        const auto bytes = mime.size() + value.size() + name.size() + grant.size();
        size_t current = request.source_origin.size();
        for (const auto& item : request.owned_items)
            current += item.mime_type.size() + item.value.size()
                + item.name.size() + item.grant_reference.size();
        if (current + bytes > maximum_outbound_drag_metadata
            || mime.size() > 256U || grant.size() > 1024U
            || (kind == WEBSCENE_OUTBOUND_DRAG_ITEM_URI_V1
                && value.size() > 8192U)) return false;
        native_outbound_drag_item item;
        item.view.kind = kind;
        item.view.byte_size = byte_size;
        item.mime_type = std::move(mime);
        item.value = std::move(value);
        item.name = std::move(name);
        item.grant_reference = std::move(grant);
        request.owned_items.push_back(std::move(item));
        return true;
    }

    bool collect_outbound_script_items(dom_node& source,
        v8::Local<v8::Object> transfer, native_outbound_drag_request& request)
    {
        auto local_context = context_for_node(source);
        v8::Context::Scope scope(local_context);
        v8::Local<v8::Value> strings_value;
        if (!transfer->Get(local_context, js_string(isolate, "_strings"))
                .ToLocal(&strings_value)
            || !strings_value->IsObject()) return true;
        auto strings = strings_value.As<v8::Object>();
        v8::Local<v8::Array> names;
        if (!strings->GetOwnPropertyNames(local_context).ToLocal(&names)) return false;
        const auto count = std::min<uint32_t>(
            names->Length(), static_cast<uint32_t>(maximum_outbound_drag_items));
        for (uint32_t index = 0U; index < count; ++index) {
            v8::Local<v8::Value> name;
            v8::Local<v8::Value> value;
            if (!names->Get(local_context, index).ToLocal(&name)
                || !strings->Get(local_context, name).ToLocal(&value)) continue;
            auto mime = to_utf8(isolate, name);
            auto text = to_utf8(isolate, value);
            const auto uri = mime == "text/uri-list";
            if (!add_outbound_item(request,
                    uri ? WEBSCENE_OUTBOUND_DRAG_ITEM_URI_V1
                        : WEBSCENE_OUTBOUND_DRAG_ITEM_TEXT_V1,
                    std::move(mime), std::move(text))) return false;
        }
        return true;
    }

    bool begin_outbound_drag(const webscene_input_event& input, dom_node& source)
    {
        constexpr auto activation_lifetime = std::chrono::seconds(5);
        const auto visibly_connected = [&] {
            if (!is_connected(source) || document.is_inert(source)) return false;
            for (auto* node = &source; node != nullptr; node = node->parent) {
                if (!node->visible || node->attributes.contains("hidden")
                    || node->style.display == display_mode::none) return false;
            }
            return source.layout.width > 0.0F && source.layout.height > 0.0F;
        };
        if (active_outbound_drag.has_value() || !visibly_connected()
            || last_user_activation.time_since_epoch().count() == 0
            || std::chrono::steady_clock::now() - last_user_activation
                > activation_lifetime) return true;

        auto transfer = create_outbound_drag_data_transfer(source);
        active_outbound_drag.emplace();
        active_outbound_drag->source_node_id = source.id;
        active_outbound_drag->transfer.Reset(isolate, transfer);
        bool prevented = false;
        if (!dispatch_input_event_type(input, "dragstart", source, &prevented)) {
            active_outbound_drag.reset();
            return false;
        }
        if (prevented || !visibly_connected()) {
            active_outbound_drag.reset();
            return true;
        }

        auto request = std::make_unique<native_outbound_drag_request>();
        request->view.request_id = ++next_host_request_id;
        request->view.document_generation =
            document_generation.load(std::memory_order_acquire);
        request->view.node_generation = document.scene_generation();
        request->view.source_node_id = source.id;
        request->view.pointer_x = static_cast<float>(input.x);
        request->view.pointer_y = static_cast<float>(input.y);
        request->source_origin = security_origin_for_realm(context_for_node(source));
        if (request->source_origin.size() > 4096U) {
            active_outbound_drag.reset(); return true;
        }
        for (auto* ancestor = source.parent; ancestor != nullptr;
             ancestor = ancestor->parent) {
            if (ancestor->tag != "iframe") continue;
            request->view.frame_owner_node_id = ancestor->id;
            request->view.frame_generation =
                current_iframe_navigation_generation(ancestor->id);
            break;
        }

        if (source.tag == "a") {
            const auto href = source.attributes.find("href");
            if (href != source.attributes.end()) {
                auto uri = resolve_resource_url(
                    href->second, effective_document_base(css_cascade_root_for_node(source)));
                if (!uri.empty() && !add_outbound_item(*request,
                        WEBSCENE_OUTBOUND_DRAG_ITEM_URI_V1,
                        "text/uri-list", std::move(uri))) {
                    active_outbound_drag.reset(); return true;
                }
            }
        }
        if (source.tag == "img") {
            auto uri = source.replaced_image().resolved_source;
            if (uri.empty()) uri = resolve_resource_url(
                source.attributes["src"],
                effective_document_base(css_cascade_root_for_node(source)));
            std::string name;
            if (const auto alt = source.attributes.find("alt");
                alt != source.attributes.end()) name = alt->second;
            std::string mime = "application/octet-stream";
            if (uri.starts_with("data:image/")) {
                if (const auto separator = uri.find_first_of(";,");
                    separator != std::string::npos)
                    mime = uri.substr(5U, separator - 5U);
            }
            if (!uri.empty() && !add_outbound_item(*request,
                    WEBSCENE_OUTBOUND_DRAG_ITEM_IMAGE_V1,
                    std::move(mime), std::move(uri), std::move(name))) {
                active_outbound_drag.reset(); return true;
            }
        }
        if (!collect_outbound_script_items(source, transfer, *request)) {
            active_outbound_drag.reset(); return true;
        }
        if (request->owned_items.empty()) {
            auto text = outbound_selection_text(source);
            if (text.empty()) append_node_text(source, text);
            if (text.empty() || !add_outbound_item(*request,
                    WEBSCENE_OUTBOUND_DRAG_ITEM_TEXT_V1,
                    "text/plain", std::move(text))) {
                active_outbound_drag.reset(); return true;
            }
        }

        v8::Local<v8::Value> effect;
        request->view.allowed_operations =
            transfer->Get(context_for_node(source), js_string(isolate, "effectAllowed"))
                    .ToLocal(&effect)
                ? outbound_operations_for_effect(to_utf8(isolate, effect))
                : WEBSCENE_OUTBOUND_DRAG_OPERATION_COPY_V1;
        if (request->view.allowed_operations == 0U) {
            active_outbound_drag.reset(); return true;
        }
        v8::Local<v8::Value> image_value;
        if (transfer->Get(context_for_node(source),
                js_string(isolate, "_dragImageElement")).ToLocal(&image_value)
            && image_value->IsObject()) {
            if (auto* image = unwrap_node(image_value.As<v8::Object>());
                image != nullptr && is_connected(*image) && image->visible
                && !document.is_inert(*image)) {
                request->view.drag_image_node_id = image->id;
                request->view.drag_image_width = std::clamp(image->layout.width, 0.0F, 4096.0F);
                request->view.drag_image_height = std::clamp(image->layout.height, 0.0F, 4096.0F);
                v8::Local<v8::Value> x;
                v8::Local<v8::Value> y;
                if (transfer->Get(context_for_node(source), js_string(isolate, "_dragImageX")).ToLocal(&x))
                    request->view.drag_image_hotspot_x = std::clamp(
                        static_cast<float>(x->NumberValue(context_for_node(source)).FromMaybe(0)),
                        -4096.0F, 4096.0F);
                if (transfer->Get(context_for_node(source), js_string(isolate, "_dragImageY")).ToLocal(&y))
                    request->view.drag_image_hotspot_y = std::clamp(
                        static_cast<float>(y->NumberValue(context_for_node(source)).FromMaybe(0)),
                        -4096.0F, 4096.0F);
            }
        }
        if (request->view.drag_image_node_id == 0U) {
            request->view.drag_image_node_id = source.id;
            request->view.drag_image_width = std::clamp(source.layout.width, 0.0F, 4096.0F);
            request->view.drag_image_height = std::clamp(source.layout.height, 0.0F, 4096.0F);
            request->view.drag_image_hotspot_x = std::clamp(
                static_cast<float>(input.x - source.layout.x), -4096.0F, 4096.0F);
            request->view.drag_image_hotspot_y = std::clamp(
                static_cast<float>(input.y - source.layout.y), -4096.0F, 4096.0F);
        }

        active_outbound_drag->request_id = request->view.request_id;
        active_outbound_drag->document_generation = request->view.document_generation;
        active_outbound_drag->node_generation = request->view.node_generation;
        active_outbound_drag->frame_generation = request->view.frame_generation;
        active_outbound_drag->frame_owner_id =
            static_cast<uint32_t>(request->view.frame_owner_node_id);
        active_outbound_drag->allowed_operations = request->view.allowed_operations;
        {
            std::lock_guard lock(host_request_mutex);
            if (outbound_drag_requests.size() >= maximum_outbound_drag_requests) {
                active_outbound_drag.reset(); return true;
            }
            outbound_drag_requests.push_back(std::move(request));
        }
        if (host_request_available) host_request_available();
        return true;
    }

    bool maybe_begin_outbound_drag(const webscene_input_event& input)
    {
        if (input.kind != WEBSCENE_INPUT_POINTER_MOVE || pointer_down_target == nullptr
            || outbound_drag_threshold_crossed
            || std::hypot(input.x - pointer_down_x, input.y - pointer_down_y) <= 4.0)
            return true;
        outbound_drag_threshold_crossed = true;
        auto* candidate = outbound_drag_candidate(pointer_down_target);
        return candidate == nullptr ? true : begin_outbound_drag(input, *candidate);
    }

    void complete_outbound_drag(native_outbound_drag_completion& completion)
    {
        if (!active_outbound_drag.has_value()
            || active_outbound_drag->request_id != completion.value.request_id
            || active_outbound_drag->document_generation
                != completion.value.document_generation) return;
        if (completion.value.status == WEBSCENE_OUTBOUND_DRAG_COMPLETED_V1
            && (completion.value.selected_operation == 0U
                || (completion.value.selected_operation
                    & (completion.value.selected_operation - 1U)) != 0U
                || (completion.value.selected_operation
                    & active_outbound_drag->allowed_operations) == 0U)) return;
        auto state = std::move(*active_outbound_drag);
        active_outbound_drag.reset();
        auto* source = document.find_by_native_id(state.source_node_id);
        if (source == nullptr || !is_connected(*source)
            || document_generation.load(std::memory_order_acquire)
                != state.document_generation
            || document.scene_generation() != state.node_generation
            || (state.frame_owner_id != 0U
                && current_iframe_navigation_generation(state.frame_owner_id)
                    != state.frame_generation)) return;
        active_outbound_drag.emplace(std::move(state));
        auto local_context = context_for_node(*source);
        v8::Context::Scope context_scope(local_context);
        const char* effect = completion.value.selected_operation
                == WEBSCENE_OUTBOUND_DRAG_OPERATION_COPY_V1 ? "copy"
            : completion.value.selected_operation
                == WEBSCENE_OUTBOUND_DRAG_OPERATION_LINK_V1 ? "link"
            : completion.value.selected_operation
                == WEBSCENE_OUTBOUND_DRAG_OPERATION_MOVE_V1 ? "move"
            : "none";
        active_outbound_drag->transfer.Get(isolate)->Set(
            local_context, js_string(isolate, "dropEffect"),
            js_string(isolate, effect)).Check();
        auto input = webscene_input_event{WEBSCENE_INPUT_POINTER_MOVE,
            completion.value.modifiers, 0U, completion.value.x,
            completion.value.y, 0.0, 0.0};
        static_cast<void>(dispatch_input_event_type(input, "dragend", *source));
        active_outbound_drag.reset();
    }
