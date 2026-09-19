    static void highlight_collected(
        const v8::WeakCallbackInfo<highlight_binding>& info)
    {
        auto* binding = info.GetParameter();
        if (binding == nullptr || binding->released
            || binding->registration_count != 0U) return;
        for (auto& wrapper : binding->range_wrappers) wrapper.Reset();
        binding->range_wrappers.clear();
        binding->ranges.clear();
        binding->wrapper.Reset();
        binding->released = true;
    }

    static highlight_binding* unwrap_highlight(v8::Local<v8::Object> object)
    {
        if (object.IsEmpty() || object->InternalFieldCount() == 0) return nullptr;
        auto field = object->GetInternalField(0);
        return field->IsExternal()
            ? static_cast<highlight_binding*>(field.As<v8::External>()->Value())
            : nullptr;
    }

    bool highlight_binding_is_live(const highlight_binding& binding) const
    {
        return !binding.released
            && binding.owner == this
            && binding.document_generation
                == document_generation.load(std::memory_order_acquire)
            && document.find_by_native_id(binding.root_id) != nullptr;
    }

    highlight_binding* require_live_highlight(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* binding = unwrap_highlight(info.This());
        if (self == nullptr || binding == nullptr
            || !self->highlight_binding_is_live(*binding)) {
            info.GetIsolate()->ThrowException(v8::Exception::TypeError(
                js_string(info.GetIsolate(), "Illegal Highlight receiver")));
            return nullptr;
        }
        return binding;
    }

    bool reserve_highlight_binding()
    {
        std::erase_if(highlight_bindings,
            [](const auto& binding) { return binding == nullptr || binding->released; });
        if (highlight_bindings.size() < maximum_live_highlights) return true;
        isolate->LowMemoryNotification();
        std::erase_if(highlight_bindings,
            [](const auto& binding) { return binding == nullptr || binding->released; });
        return highlight_bindings.size() < maximum_live_highlights;
    }

    void refresh_custom_highlight_paint()
    {
        struct resolved_highlight_style final {
            uint32_t foreground_rgba{};
            uint32_t background_rgba{};
            bool foreground_specified{false};
            bool background_specified{false};
            bool foreground_important{false};
            bool background_important{false};
        };
        std::vector<native_document::custom_highlight_range> published;
        for (const auto& [root_id, registry] : highlight_registries) {
            auto* root = document.find_by_native_id(root_id);
            if (root == nullptr) continue;
            std::unordered_map<std::string, resolved_highlight_style> styles;
            for (const auto& rule : css_rules) {
                if (!rule.media_matches) continue;
                std::string origin;
                std::string name;
                if (!css::split_custom_highlight_selector(
                        rule.selector(), origin, name)
                    || (!origin.empty() && origin != "*")) continue;
                auto& style = styles[name];
                for (const auto& declaration : rule.declarations()) {
                    const auto foreground = declaration.name == "color";
                    const auto background = declaration.name == "background-color";
                    if (!foreground && !background) continue;
                    auto resolved = declaration.value.find("var(") != std::string::npos
                        ? resolve_value(*root, declaration.value, css_variables)
                        : declaration.value;
                    const auto color = native_document::parse_color(resolved);
                    if (!css::is_explicit_color_token(resolved, color)) continue;
                    auto& specified = foreground
                        ? style.foreground_specified : style.background_specified;
                    auto& important = foreground
                        ? style.foreground_important : style.background_important;
                    auto& rgba = foreground
                        ? style.foreground_rgba : style.background_rgba;
                    if (specified && important && !declaration.important) continue;
                    specified = true;
                    important = declaration.important;
                    rgba = color;
                }
            }
            for (uint32_t order = 0U; order < registry.order.size(); ++order) {
                const auto entry = registry.entries.find(registry.order[order]);
                const auto style = styles.find(registry.order[order]);
                if (entry == registry.entries.end() || style == styles.end()
                    || entry->second.highlight == nullptr) continue;
                const auto& highlight = *entry->second.highlight;
                if (!highlight_binding_is_live(highlight)) continue;
                for (const auto* range : highlight.ranges) {
                    if (range == nullptr || !range_binding_is_live(*range)
                        || range->root_id != root_id) continue;
                    auto* start = document.find_by_native_id(range->start_node_id);
                    auto* end = document.find_by_native_id(range->end_node_id);
                    if (start == nullptr || end == nullptr
                        || !range_node_is_in_root(*root, *start)
                        || !range_node_is_in_root(*root, *end)
                        || (start == end && range->start_offset == range->end_offset)) continue;
                    published.push_back(native_document::custom_highlight_range{
                        root_id,
                        range->start_node_id,
                        range->end_node_id,
                        range->start_offset,
                        range->end_offset,
                        style->second.foreground_rgba,
                        style->second.background_rgba,
                        highlight.priority,
                        order,
                        style->second.foreground_specified,
                        style->second.background_specified});
                }
            }
        }
        struct selection_rule_entry final {
            const css_rule* rule{};
            std::string origin;
        };
        std::vector<selection_rule_entry> selection_rules;
        for (const auto& rule : css_rules) {
            if (!rule.media_matches) continue;
            std::string origin;
            if (css::split_selection_selector(rule.selector(), origin)) {
                selection_rules.push_back({&rule, std::move(origin)});
            }
        }
        constexpr auto selection_priority = std::numeric_limits<int32_t>::max();
        uint32_t selection_order = 0U;
        for (auto& [root_id, selection] : window_selections) {
            if (selection_rules.empty()) break;
            auto* root = document.find_by_native_id(root_id);
            if (root == nullptr || !validate_selection(selection)
                || (selection.anchor_node_id == selection.focus_node_id
                    && selection.anchor_offset == selection.focus_offset)) continue;
            refresh_selection_text_nodes(selection, *root);
            auto* anchor = document.find_by_native_id(selection.anchor_node_id);
            auto* focus = document.find_by_native_id(selection.focus_node_id);
            const auto anchor_position = std::find(
                selection.text_nodes.begin(), selection.text_nodes.end(), anchor);
            const auto focus_position = std::find(
                selection.text_nodes.begin(), selection.text_nodes.end(), focus);
            if (anchor_position == selection.text_nodes.end()
                || focus_position == selection.text_nodes.end()) continue;
            const auto forward = anchor_position < focus_position
                || (anchor_position == focus_position
                    && selection.anchor_offset <= selection.focus_offset);
            const auto first = forward ? anchor_position : focus_position;
            const auto last = forward ? focus_position : anchor_position;
            const auto first_offset = forward
                ? selection.anchor_offset : selection.focus_offset;
            const auto last_offset = forward
                ? selection.focus_offset : selection.anchor_offset;
            for (auto iterator = first; iterator <= last; ++iterator) {
                auto* text = *iterator;
                if (text == nullptr) continue;
                auto* origin_node = text->parent;
                while (origin_node != nullptr
                    && origin_node->kind != dom_node_kind::element) {
                    origin_node = origin_node->parent;
                }
                if (origin_node == nullptr) continue;
                std::vector<const css_rule*> matching;
                for (const auto& entry : selection_rules) {
                    if (entry.origin.empty() || entry.origin == "*"
                        || css_selector_matches(*origin_node, entry.origin)) {
                        matching.push_back(entry.rule);
                    }
                }
                if (matching.empty()) continue;
                std::stable_sort(
                    matching.begin(), matching.end(),
                    [](const css_rule* left, const css_rule* right) {
                        return css::cascade_rule_precedes(left, right, false);
                    });
                resolved_highlight_style style;
                const css::cascaded_rule_order cascade(matching);
                css::for_each_cascaded_declaration(
                    cascade,
                    false,
                    [&](const css_declaration& declaration) {
                        const auto foreground = declaration.name == "color";
                        const auto background = declaration.name == "background-color";
                        if (!foreground && !background) return;
                        auto resolved = declaration.value.find("var(") != std::string::npos
                            ? resolve_value(*origin_node, declaration.value, css_variables)
                            : declaration.value;
                        const auto color = native_document::parse_color(resolved);
                        if (!css::is_explicit_color_token(resolved, color)) return;
                        if (foreground) {
                            style.foreground_rgba = color;
                            style.foreground_specified = true;
                        } else {
                            style.background_rgba = color;
                            style.background_specified = true;
                        }
                    },
                    [](const css_declaration& declaration) -> std::string_view {
                        return declaration.name;
                    });
                if (!style.foreground_specified && !style.background_specified) continue;
                const auto text_length = decode_wtf8_to_utf16(text->text_content).size();
                const auto start_offset = iterator == first
                    ? std::min(first_offset, text_length) : 0U;
                const auto end_offset = iterator == last
                    ? std::min(last_offset, text_length) : text_length;
                if (start_offset >= end_offset) continue;
                published.push_back(native_document::custom_highlight_range{
                    root_id,
                    text->id,
                    text->id,
                    start_offset,
                    end_offset,
                    style.foreground_rgba,
                    style.background_rgba,
                    selection_priority,
                    selection_order++,
                    style.foreground_specified,
                    style.background_specified});
            }
        }
        document.set_custom_highlight_ranges(std::move(published));
    }

    void selection_paint_changed()
    {
        refresh_custom_highlight_paint();
    }

    void registered_highlight_changed(const highlight_binding& binding)
    {
        if (binding.registration_count != 0U) refresh_custom_highlight_paint();
    }

    void registered_range_changed(const range_binding& range)
    {
        for (const auto& highlight : highlight_bindings) {
            if (highlight == nullptr || highlight->registration_count == 0U) continue;
            if (std::find(
                    highlight->ranges.begin(), highlight->ranges.end(), &range)
                == highlight->ranges.end()) continue;
            refresh_custom_highlight_paint();
            return;
        }
    }

    static bool read_highlight_range(
        const v8::FunctionCallbackInfo<v8::Value>& info,
        const highlight_binding& highlight,
        v8::Local<v8::Value> value,
        range_binding*& range,
        v8::Local<v8::Object>& wrapper)
    {
        auto* self = current(info.GetIsolate());
        if (self == nullptr || !value->IsObject()) {
            info.GetIsolate()->ThrowException(v8::Exception::TypeError(
                js_string(info.GetIsolate(), "Highlight entries must be Range objects")));
            return false;
        }
        wrapper = value.As<v8::Object>();
        range = unwrap_range(wrapper);
        if (range == nullptr || !self->range_binding_is_live(*range)
            || range->root_id != highlight.root_id) {
            info.GetIsolate()->ThrowException(v8::Exception::TypeError(
                js_string(info.GetIsolate(), "Highlight Range belongs to another document")));
            return false;
        }
        return true;
    }

    static void highlight_constructor(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        if (self == nullptr || !info.IsConstructCall()) {
            info.GetIsolate()->ThrowException(v8::Exception::TypeError(
                js_string(info.GetIsolate(), "Highlight must be constructed with new")));
            return;
        }
        if (!self->reserve_highlight_binding()) {
            throw_dom_exception(info, "Too many live Highlight objects", "QuotaExceededError");
            return;
        }
        auto* root = self->range_root_for_context(info.GetIsolate()->GetCurrentContext());
        if (root == nullptr) return;
        auto binding = std::make_unique<highlight_binding>();
        binding->owner = self;
        binding->document_generation = self->document_generation.load(std::memory_order_acquire);
        binding->root_id = root->id;
        for (int index = 0; index < info.Length(); ++index) {
            range_binding* range = nullptr;
            v8::Local<v8::Object> wrapper;
            if (!read_highlight_range(info, *binding, info[index], range, wrapper)) return;
            if (std::find(binding->ranges.begin(), binding->ranges.end(), range)
                != binding->ranges.end()) continue;
            if (binding->ranges.size() >= maximum_ranges_per_highlight) {
                throw_dom_exception(info, "Highlight contains too many ranges", "QuotaExceededError");
                return;
            }
            binding->ranges.push_back(range);
            binding->range_wrappers.emplace_back(info.GetIsolate(), wrapper);
        }
        auto* raw = binding.get();
        info.This()->SetInternalField(0, v8::External::New(info.GetIsolate(), raw));
        raw->wrapper.Reset(info.GetIsolate(), info.This());
        raw->wrapper.SetWeak(raw, highlight_collected, v8::WeakCallbackType::kParameter);
        self->highlight_bindings.push_back(std::move(binding));
        info.GetReturnValue().Set(info.This());
    }

    static void highlight_add(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* binding = self == nullptr ? nullptr : self->require_live_highlight(info);
        if (binding == nullptr) return;
        range_binding* range = nullptr;
        v8::Local<v8::Object> wrapper;
        if (info.Length() < 1
            || !read_highlight_range(info, *binding, info[0], range, wrapper)) return;
        if (std::find(binding->ranges.begin(), binding->ranges.end(), range)
            == binding->ranges.end()) {
            if (binding->ranges.size() >= maximum_ranges_per_highlight) {
                throw_dom_exception(info, "Highlight contains too many ranges", "QuotaExceededError");
                return;
            }
            binding->ranges.push_back(range);
            binding->range_wrappers.emplace_back(info.GetIsolate(), wrapper);
            self->registered_highlight_changed(*binding);
        }
        info.GetReturnValue().Set(info.This());
    }

    static void highlight_delete(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* binding = self == nullptr ? nullptr : self->require_live_highlight(info);
        if (binding == nullptr) return;
        auto* range = info.Length() > 0 && info[0]->IsObject()
            ? unwrap_range(info[0].As<v8::Object>()) : nullptr;
        const auto iterator = std::find(binding->ranges.begin(), binding->ranges.end(), range);
        if (iterator == binding->ranges.end()) {
            info.GetReturnValue().Set(v8::False(info.GetIsolate()));
            return;
        }
        const auto index = static_cast<size_t>(std::distance(binding->ranges.begin(), iterator));
        binding->range_wrappers[index].Reset();
        binding->range_wrappers.erase(binding->range_wrappers.begin() + index);
        binding->ranges.erase(iterator);
        self->registered_highlight_changed(*binding);
        info.GetReturnValue().Set(v8::True(info.GetIsolate()));
    }

    static void highlight_clear(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* binding = self == nullptr ? nullptr : self->require_live_highlight(info);
        if (binding == nullptr || binding->ranges.empty()) return;
        for (auto& wrapper : binding->range_wrappers) wrapper.Reset();
        binding->range_wrappers.clear();
        binding->ranges.clear();
        self->registered_highlight_changed(*binding);
    }

    static void highlight_has(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* binding = self == nullptr ? nullptr : self->require_live_highlight(info);
        if (binding == nullptr) return;
        auto* range = info.Length() > 0 && info[0]->IsObject()
            ? unwrap_range(info[0].As<v8::Object>()) : nullptr;
        info.GetReturnValue().Set(v8::Boolean::New(
            info.GetIsolate(),
            std::find(binding->ranges.begin(), binding->ranges.end(), range)
                != binding->ranges.end()));
    }

    static void highlight_size_get(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* binding = unwrap_highlight(info.Holder());
        const auto size = self != nullptr && binding != nullptr
                && self->highlight_binding_is_live(*binding)
            ? binding->ranges.size() : 0U;
        info.GetReturnValue().Set(v8::Integer::NewFromUnsigned(
            info.GetIsolate(), static_cast<uint32_t>(size)));
    }

    static void highlight_priority_get(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* binding = unwrap_highlight(info.Holder());
        info.GetReturnValue().Set(v8::Integer::New(
            info.GetIsolate(),
            self != nullptr && binding != nullptr
                    && self->highlight_binding_is_live(*binding)
                ? binding->priority : 0));
    }

    static void highlight_priority_set(
        v8::Local<v8::Name>,
        v8::Local<v8::Value> value,
        const v8::PropertyCallbackInfo<void>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* binding = unwrap_highlight(info.Holder());
        if (self == nullptr || binding == nullptr
            || !self->highlight_binding_is_live(*binding)) return;
        const auto priority = value
            ->Int32Value(info.GetIsolate()->GetCurrentContext()).FromMaybe(0);
        if (binding->priority == priority) return;
        binding->priority = priority;
        self->registered_highlight_changed(*binding);
    }

    static v8::MaybeLocal<v8::Object> array_iterator(
        v8::Isolate* isolate,
        v8::Local<v8::Context> local_context,
        v8::Local<v8::Array> array)
    {
        v8::Local<v8::Value> iterator_value;
        if (!array->Get(local_context, v8::Symbol::GetIterator(isolate))
                .ToLocal(&iterator_value)
            || !iterator_value->IsFunction()) return {};
        v8::Local<v8::Value> result;
        if (!iterator_value.As<v8::Function>()->Call(
                local_context, array, 0, nullptr).ToLocal(&result)
            || !result->IsObject()) return {};
        return result.As<v8::Object>();
    }

    static void highlight_iterate(
        const v8::FunctionCallbackInfo<v8::Value>& info,
        bool entries)
    {
        auto* self = current(info.GetIsolate());
        auto* binding = self == nullptr ? nullptr : self->require_live_highlight(info);
        if (binding == nullptr) return;
        auto local_context = info.GetIsolate()->GetCurrentContext();
        auto array = v8::Array::New(
            info.GetIsolate(), static_cast<int>(binding->range_wrappers.size()));
        for (uint32_t index = 0U; index < binding->range_wrappers.size(); ++index) {
            auto range = binding->range_wrappers[index].Get(info.GetIsolate());
            if (entries) {
                auto pair = v8::Array::New(info.GetIsolate(), 2);
                pair->Set(local_context, 0U, range).Check();
                pair->Set(local_context, 1U, range).Check();
                array->Set(local_context, index, pair).Check();
            } else {
                array->Set(local_context, index, range).Check();
            }
        }
        v8::Local<v8::Object> iterator;
        if (array_iterator(info.GetIsolate(), local_context, array).ToLocal(&iterator))
            info.GetReturnValue().Set(iterator);
    }

    static void highlight_values(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        highlight_iterate(info, false);
    }

    static void highlight_entries(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        highlight_iterate(info, true);
    }

    static void highlight_for_each(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* binding = self == nullptr ? nullptr : self->require_live_highlight(info);
        if (binding == nullptr) return;
        if (info.Length() < 1 || !info[0]->IsFunction()) {
            info.GetIsolate()->ThrowException(v8::Exception::TypeError(
                js_string(info.GetIsolate(), "Highlight.forEach requires a callback")));
            return;
        }
        auto local_context = info.GetIsolate()->GetCurrentContext();
        auto receiver = info.Length() > 1 ? info[1] : v8::Undefined(info.GetIsolate());
        for (auto& wrapper : binding->range_wrappers) {
            auto range = wrapper.Get(info.GetIsolate());
            v8::Local<v8::Value> arguments[]{range, range, info.This()};
            if (info[0].As<v8::Function>()->Call(
                    local_context, receiver, 3, arguments).IsEmpty()) return;
        }
    }

    static uint32_t highlight_registry_root(v8::Local<v8::Object> object)
    {
        if (object.IsEmpty() || object->InternalFieldCount() == 0) return 0U;
        const auto field = object->GetInternalField(0);
        return field->IsUint32() ? field.As<v8::Uint32>()->Value() : 0U;
    }

    highlight_registry_state* highlight_registry_for(v8::Local<v8::Object> object)
    {
        const auto root_id = highlight_registry_root(object);
        const auto found = highlight_registries.find(root_id);
        return found == highlight_registries.end() ? nullptr : &found->second;
    }

    static bool read_highlight_name(
        const v8::FunctionCallbackInfo<v8::Value>& info,
        std::string& name)
    {
        if (info.Length() < 1) {
            info.GetIsolate()->ThrowException(v8::Exception::TypeError(
                js_string(info.GetIsolate(), "Highlight registry requires a name")));
            return false;
        }
        name = to_wtf8(info.GetIsolate(), info[0]);
        if (name.empty() || name.size() > maximum_highlight_name_bytes) {
            throw_dom_exception(info, "Highlight name is empty or too long", "SyntaxError");
            return false;
        }
        return true;
    }

    static void highlight_registry_set(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* registry = self == nullptr ? nullptr : self->highlight_registry_for(info.This());
        std::string name;
        if (registry == nullptr || !read_highlight_name(info, name)) return;
        auto* highlight = info.Length() > 1 && info[1]->IsObject()
            ? unwrap_highlight(info[1].As<v8::Object>()) : nullptr;
        const auto root_id = highlight_registry_root(info.This());
        if (highlight == nullptr || !self->highlight_binding_is_live(*highlight)
            || highlight->root_id != root_id) {
            info.GetIsolate()->ThrowException(v8::Exception::TypeError(
                js_string(info.GetIsolate(), "Registry values must be same-document Highlight objects")));
            return;
        }
        const auto known = registry->entries.find(name);
        if (known != registry->entries.end()) {
            if (known->second.highlight != highlight) {
                if (known->second.highlight != nullptr
                    && known->second.highlight->registration_count != 0U)
                    --known->second.highlight->registration_count;
                known->second.wrapper.Reset();
                known->second.highlight = highlight;
                known->second.wrapper.Reset(info.GetIsolate(), info[1].As<v8::Object>());
                ++highlight->registration_count;
                self->refresh_custom_highlight_paint();
            }
            info.GetReturnValue().Set(info.This());
            return;
        }
        if (registry->entries.size() >= maximum_registered_highlights_per_root) {
            throw_dom_exception(info, "Highlight registry is full", "QuotaExceededError");
            return;
        }
        highlight_registry_entry entry;
        entry.highlight = highlight;
        entry.wrapper.Reset(info.GetIsolate(), info[1].As<v8::Object>());
        registry->order.push_back(name);
        registry->entries.emplace(name, std::move(entry));
        ++highlight->registration_count;
        self->refresh_custom_highlight_paint();
        info.GetReturnValue().Set(info.This());
    }

    static void highlight_registry_get_or_has(
        const v8::FunctionCallbackInfo<v8::Value>& info,
        bool has)
    {
        auto* self = current(info.GetIsolate());
        auto* registry = self == nullptr ? nullptr : self->highlight_registry_for(info.This());
        std::string name;
        if (registry == nullptr || !read_highlight_name(info, name)) return;
        const auto found = registry->entries.find(name);
        if (has) {
            info.GetReturnValue().Set(v8::Boolean::New(
                info.GetIsolate(), found != registry->entries.end()));
        } else if (found == registry->entries.end()) {
            info.GetReturnValue().Set(v8::Undefined(info.GetIsolate()));
        } else {
            info.GetReturnValue().Set(found->second.wrapper.Get(info.GetIsolate()));
        }
    }

    static void highlight_registry_get(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        highlight_registry_get_or_has(info, false);
    }

    static void highlight_registry_has(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        highlight_registry_get_or_has(info, true);
    }

    static void highlight_registry_delete(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* registry = self == nullptr ? nullptr : self->highlight_registry_for(info.This());
        std::string name;
        if (registry == nullptr || !read_highlight_name(info, name)) return;
        const auto found = registry->entries.find(name);
        if (found == registry->entries.end()) {
            info.GetReturnValue().Set(v8::False(info.GetIsolate()));
            return;
        }
        if (found->second.highlight != nullptr
            && found->second.highlight->registration_count != 0U)
            --found->second.highlight->registration_count;
        found->second.wrapper.Reset();
        registry->entries.erase(found);
        std::erase(registry->order, name);
        self->refresh_custom_highlight_paint();
        info.GetReturnValue().Set(v8::True(info.GetIsolate()));
    }

    static void highlight_registry_clear(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* registry = self == nullptr ? nullptr : self->highlight_registry_for(info.This());
        if (registry == nullptr || registry->entries.empty()) return;
        for (auto& [name, entry] : registry->entries) {
            static_cast<void>(name);
            if (entry.highlight != nullptr && entry.highlight->registration_count != 0U)
                --entry.highlight->registration_count;
            entry.wrapper.Reset();
        }
        registry->entries.clear();
        registry->order.clear();
        self->refresh_custom_highlight_paint();
    }

    static void highlight_registry_size_get(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* registry = self == nullptr ? nullptr : self->highlight_registry_for(info.Holder());
        info.GetReturnValue().Set(v8::Integer::NewFromUnsigned(
            info.GetIsolate(),
            registry == nullptr ? 0U : static_cast<uint32_t>(registry->entries.size())));
    }

    static void highlight_registry_iterate(
        const v8::FunctionCallbackInfo<v8::Value>& info,
        int kind)
    {
        auto* self = current(info.GetIsolate());
        auto* registry = self == nullptr ? nullptr : self->highlight_registry_for(info.This());
        if (registry == nullptr) return;
        auto local_context = info.GetIsolate()->GetCurrentContext();
        auto array = v8::Array::New(
            info.GetIsolate(), static_cast<int>(registry->order.size()));
        uint32_t output_index = 0U;
        for (const auto& name : registry->order) {
            const auto found = registry->entries.find(name);
            if (found == registry->entries.end()) continue;
            auto highlight = found->second.wrapper.Get(info.GetIsolate());
            if (kind == 0) {
                array->Set(local_context, output_index++, js_string(info.GetIsolate(), name)).Check();
            } else if (kind == 1) {
                array->Set(local_context, output_index++, highlight).Check();
            } else {
                auto pair = v8::Array::New(info.GetIsolate(), 2);
                pair->Set(local_context, 0U, js_string(info.GetIsolate(), name)).Check();
                pair->Set(local_context, 1U, highlight).Check();
                array->Set(local_context, output_index++, pair).Check();
            }
        }
        v8::Local<v8::Object> iterator;
        if (array_iterator(info.GetIsolate(), local_context, array).ToLocal(&iterator))
            info.GetReturnValue().Set(iterator);
    }

    static void highlight_registry_keys(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        highlight_registry_iterate(info, 0);
    }

    static void highlight_registry_values(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        highlight_registry_iterate(info, 1);
    }

    static void highlight_registry_entries(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        highlight_registry_iterate(info, 2);
    }

    static void highlight_registry_for_each(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* registry = self == nullptr ? nullptr : self->highlight_registry_for(info.This());
        if (registry == nullptr) return;
        if (info.Length() < 1 || !info[0]->IsFunction()) {
            info.GetIsolate()->ThrowException(v8::Exception::TypeError(
                js_string(info.GetIsolate(), "HighlightRegistry.forEach requires a callback")));
            return;
        }
        auto local_context = info.GetIsolate()->GetCurrentContext();
        auto receiver = info.Length() > 1 ? info[1] : v8::Undefined(info.GetIsolate());
        for (const auto& name : registry->order) {
            const auto found = registry->entries.find(name);
            if (found == registry->entries.end()) continue;
            auto highlight = found->second.wrapper.Get(info.GetIsolate());
            v8::Local<v8::Value> arguments[]{
                highlight, js_string(info.GetIsolate(), name), info.This()};
            if (info[0].As<v8::Function>()->Call(
                    local_context, receiver, 3, arguments).IsEmpty()) return;
        }
    }

    v8::Local<v8::Object> ensure_highlight_registry(
        v8::Local<v8::Context> local_context,
        uint32_t root_id)
    {
        auto& state = highlight_registries[root_id];
        if (!state.wrapper.IsEmpty()) return state.wrapper.Get(isolate);
        auto registry_template = v8::ObjectTemplate::New(isolate);
        registry_template->SetInternalFieldCount(1);
        registry_template->SetNativeDataProperty(
            js_string(isolate, "size"), highlight_registry_size_get);
        registry_template->Set(
            js_string(isolate, "set"),
            v8::FunctionTemplate::New(isolate, highlight_registry_set));
        registry_template->Set(
            js_string(isolate, "get"),
            v8::FunctionTemplate::New(isolate, highlight_registry_get));
        registry_template->Set(
            js_string(isolate, "has"),
            v8::FunctionTemplate::New(isolate, highlight_registry_has));
        registry_template->Set(
            js_string(isolate, "delete"),
            v8::FunctionTemplate::New(isolate, highlight_registry_delete));
        registry_template->Set(
            js_string(isolate, "clear"),
            v8::FunctionTemplate::New(isolate, highlight_registry_clear));
        registry_template->Set(
            js_string(isolate, "keys"),
            v8::FunctionTemplate::New(isolate, highlight_registry_keys));
        registry_template->Set(
            js_string(isolate, "values"),
            v8::FunctionTemplate::New(isolate, highlight_registry_values));
        registry_template->Set(
            js_string(isolate, "entries"),
            v8::FunctionTemplate::New(isolate, highlight_registry_entries));
        registry_template->Set(
            js_string(isolate, "forEach"),
            v8::FunctionTemplate::New(isolate, highlight_registry_for_each));
        registry_template->Set(
            v8::Symbol::GetIterator(isolate),
            v8::FunctionTemplate::New(isolate, highlight_registry_entries));
        auto registry = registry_template->NewInstance(local_context).ToLocalChecked();
        registry->SetInternalField(
            0, v8::Integer::NewFromUnsigned(isolate, root_id));
        state.wrapper.Reset(isolate, registry);
        return registry;
    }

    void clear_highlight_bindings()
    {
        for (auto& [root_id, registry] : highlight_registries) {
            static_cast<void>(root_id);
            for (auto& [name, entry] : registry.entries) {
                static_cast<void>(name);
                if (entry.highlight != nullptr
                    && entry.highlight->registration_count != 0U)
                    --entry.highlight->registration_count;
                entry.wrapper.Reset();
            }
            registry.entries.clear();
            registry.order.clear();
            registry.wrapper.Reset();
        }
        highlight_registries.clear();
        for (auto& binding : highlight_bindings) {
            if (binding == nullptr) continue;
            for (auto& wrapper : binding->range_wrappers) wrapper.Reset();
            binding->range_wrappers.clear();
            binding->ranges.clear();
            binding->wrapper.Reset();
        }
        highlight_bindings.clear();
    }
