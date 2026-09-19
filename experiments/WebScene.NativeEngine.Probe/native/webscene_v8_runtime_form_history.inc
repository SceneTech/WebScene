    static constexpr size_t maximum_form_restore_controls = 512U;
    static constexpr size_t maximum_form_restore_value_bytes = 64U * 1024U;
    static constexpr size_t maximum_form_restore_selected = 256U;
    static constexpr size_t maximum_form_restore_document_bytes = 256U * 1024U;
    static constexpr size_t maximum_form_restore_history_bytes = 4U * 1024U * 1024U;
    static constexpr size_t maximum_form_restore_visited_nodes = 32768U;

    static uint64_t form_identity_hash(std::string_view value, uint64_t seed)
    {
        auto result = seed;
        for (const auto character : value) {
            result ^= static_cast<uint8_t>(character);
            result *= UINT64_C(1099511628211);
        }
        return result;
    }

    static std::string opaque_form_identity(std::string_view value)
    {
        constexpr char hex[] = "0123456789abcdef";
        const auto first = form_identity_hash(
            value, UINT64_C(14695981039346656037));
        const auto second = form_identity_hash(
            value, UINT64_C(1099511628211) ^ static_cast<uint64_t>(value.size()));
        std::string result(32U, '0');
        for (size_t index = 0U; index < 16U; ++index) {
            const auto source = index < 8U ? first : second;
            const auto shift = static_cast<unsigned>((index & 7U) * 8U);
            const auto byte = static_cast<uint8_t>(source >> shift);
            result[index * 2U] = hex[byte >> 4U];
            result[index * 2U + 1U] = hex[byte & 0xfU];
        }
        return result;
    }

    std::string form_document_identity(std::string_view address) const
    {
        if (address.empty() || address.size() > 8192U) return {};
        auto canonical = std::string(address);
        if (const auto fragment = canonical.find('#');
            fragment != std::string::npos) canonical.resize(fragment);
        const auto origin = resource_origin(canonical);
        if (origin.empty() || origin == "null") return {};
        return opaque_form_identity(origin + "\n" + canonical);
    }

    static std::vector<std::string> autocomplete_tokens(const dom_node& node)
    {
        const auto authored = node.attributes.find("autocomplete");
        if (authored == node.attributes.end()) return {};
        std::istringstream stream(lower_html_name(authored->second));
        std::vector<std::string> result;
        for (std::string token; stream >> token;) result.push_back(std::move(token));
        return result;
    }

    static bool autocomplete_excludes_form_restore(const dom_node& node)
    {
        static constexpr std::array<std::string_view, 15U> secret_tokens{
            "off", "current-password", "new-password", "one-time-code",
            "cc-name", "cc-given-name", "cc-additional-name",
            "cc-family-name", "cc-number", "cc-exp", "cc-exp-month",
            "cc-exp-year", "cc-csc", "cc-type", "transaction-amount"};
        const auto tokens = autocomplete_tokens(node);
        return std::any_of(tokens.begin(), tokens.end(), [](const auto& token) {
            return std::find(secret_tokens.begin(), secret_tokens.end(), token)
                != secret_tokens.end() || token == "transaction-currency";
        });
    }

    bool form_restore_control_eligible(const dom_node& node) const
    {
        if (!is_listed_form_control(node)
            || !forms::will_validate(document, node)
            || document.is_inert(node)
            || autocomplete_excludes_form_restore(node)) return false;
        if (const auto* owner = forms::form_owner(document, node);
            owner != nullptr && autocomplete_excludes_form_restore(*owner)) return false;
        if (node.tag == "input") {
            const auto type = node.attributes.find("type");
            const auto normalized = type == node.attributes.end()
                ? std::string{"text"} : lower_html_name(type->second);
            if (normalized == "password" || normalized == "file"
                || normalized == "hidden" || normalized == "image"
                || normalized == "button" || normalized == "submit"
                || normalized == "reset") return false;
            std::string identity_text = node.id_attribute;
            if (const auto name = node.attributes.find("name");
                name != node.attributes.end()) identity_text += " " + name->second;
            identity_text = lower_html_name(identity_text);
            for (const auto token : {"cardnumber", "creditcard", "cc-number",
                    "card-cvc", "card-cvv", "securitycode", "one-time-code"})
                if (identity_text.find(token) != std::string::npos) return false;
        }
        return true;
    }

    std::vector<dom_node*> bounded_form_restore_nodes(dom_node& root) const
    {
        std::vector<dom_node*> result;
        size_t visited = 0U;
        const auto visit = [&](const auto& recurse, dom_node& node) -> void {
            if (++visited > maximum_form_restore_visited_nodes) return;
            if (node.kind == dom_node_kind::element) result.push_back(&node);
            if (node.tag == "iframe") return;
            for (auto* child : node.children) {
                if (child == nullptr || document.is_shadow_root(*child)) continue;
                recurse(recurse, *child);
                if (visited > maximum_form_restore_visited_nodes) return;
            }
        };
        visit(visit, root);
        if (visited > maximum_form_restore_visited_nodes) result.clear();
        return result;
    }

    std::string form_control_identity(
        dom_node& root, const dom_node& control) const
    {
        std::vector<size_t> path;
        for (auto* current = &control; current != &root;) {
            auto* parent = document.dom_parent(*current);
            if (parent == nullptr || path.size() >= 128U) return {};
            const auto found = std::find(
                parent->children.begin(), parent->children.end(), current);
            if (found == parent->children.end()) return {};
            path.push_back(static_cast<size_t>(found - parent->children.begin()));
            current = parent;
        }
        std::string material = control.tag;
        const auto append_attribute = [&](std::string_view name) {
            material.push_back('\x1f');
            const auto found = control.attributes.find(std::string(name));
            if (found != control.attributes.end() && found->second.size() <= 1024U)
                material += found->second;
        };
        append_attribute("type");
        append_attribute("id");
        append_attribute("name");
        if (const auto* owner = forms::form_owner(document, control); owner != nullptr) {
            material.push_back('\x1f');
            material += owner->id_attribute;
            const auto name = owner->attributes.find("name");
            if (name != owner->attributes.end() && name->second.size() <= 1024U) {
                material.push_back('\x1f');
                material += name->second;
            }
        }
        for (auto iterator = path.rbegin(); iterator != path.rend(); ++iterator)
            material += "/" + std::to_string(*iterator);
        return opaque_form_identity(material);
    }

    std::optional<std::string> capture_custom_form_state(dom_node& node)
    {
        auto local_context = context_for_node(node);
        v8::Context::Scope scope(local_context);
        v8::Local<v8::Value> callback;
        if (!local_context->Global()->Get(
                local_context,
                js_string(isolate, "__webSceneCaptureFormAssociatedState"))
                .ToLocal(&callback) || !callback->IsFunction()) return std::nullopt;
        v8::Local<v8::Value> arguments[]{wrap_node(node)};
        v8::Local<v8::Value> result;
        v8::TryCatch try_catch(isolate);
        if (!callback.As<v8::Function>()->Call(
                local_context, local_context->Global(), 1, arguments)
                .ToLocal(&result) || !result->IsString()) return std::nullopt;
        auto state = to_wtf8(isolate, result);
        return state.size() <= maximum_form_restore_value_bytes
            ? std::optional<std::string>{std::move(state)} : std::nullopt;
    }

    size_t form_state_bytes(const profile_form_document_state& state) const
    {
        size_t result = 0U;
        for (const auto& control : state.controls) {
            result += control.identity.size() + control.value.size()
                + control.selected_indices.size() * sizeof(uint32_t) + 2U;
        }
        return result;
    }

    profile_form_document_state capture_form_state(dom_node& root)
    {
        profile_form_document_state result;
        std::unordered_set<std::string> identities;
        size_t bytes = 0U;
        for (auto* node : bounded_form_restore_nodes(root)) {
            if (node == nullptr || !form_restore_control_eligible(*node)) continue;
            profile_form_control_state state;
            state.identity = form_control_identity(root, *node);
            if (state.identity.empty() || !identities.insert(state.identity).second)
                continue;
            if (node->form_control().form_associated_custom_element) {
                const auto custom = capture_custom_form_state(*node);
                if (!custom.has_value()) continue;
                state.kind = profile_form_control_kind::custom;
                state.value = *custom;
            } else if (node->tag == "select") {
                state.kind = profile_form_control_kind::selected;
                std::vector<dom_node*> options;
                collect_descendants_by_tag(*node, "option", options);
                for (uint32_t index = 0U;
                    index < options.size()
                        && state.selected_indices.size()
                            < maximum_form_restore_selected; ++index)
                    if (forms::option_is_selected(*options[index]))
                        state.selected_indices.push_back(index);
            } else if (node->tag == "input"
                && (forms::input_type_is(*node, "checkbox")
                    || forms::input_type_is(*node, "radio"))) {
                state.kind = profile_form_control_kind::checked;
                state.checked = forms::input_checked(*node);
            } else if (node->tag == "input" || node->tag == "textarea") {
                state.kind = profile_form_control_kind::value;
                state.value = forms::text_control_value(*node);
                if (state.value.size() > maximum_form_restore_value_bytes) continue;
            } else continue;
            const auto addition = state.identity.size() + state.value.size()
                + state.selected_indices.size() * sizeof(uint32_t) + 2U;
            if (result.controls.size() >= maximum_form_restore_controls
                || addition > maximum_form_restore_document_bytes - bytes) break;
            bytes += addition;
            result.controls.push_back(std::move(state));
        }
        return result;
    }

    void dispatch_custom_form_restore(
        dom_node& node, std::string_view value, bool autocomplete)
    {
        auto local_context = context_for_node(node);
        v8::Context::Scope scope(local_context);
        v8::Local<v8::Value> callback;
        if (!local_context->Global()->Get(
                local_context,
                js_string(isolate, "__webSceneRestoreFormAssociatedStateValue"))
                .ToLocal(&callback) || !callback->IsFunction()) return;
        v8::Local<v8::Value> arguments[]{
            wrap_node(node),
            js_dom_string(isolate, value),
            js_string(isolate, autocomplete ? "autocomplete" : "restore")};
        v8::TryCatch try_catch(isolate);
        static_cast<void>(callback.As<v8::Function>()->Call(
            local_context, local_context->Global(), 3, arguments));
        perform_microtask_checkpoint();
    }

    bool restore_form_control(
        dom_node& node,
        const profile_form_control_state& state,
        bool autocomplete)
    {
        if (!form_restore_control_eligible(node)) return false;
        switch (state.kind) {
        case profile_form_control_kind::value:
            if (node.tag != "input" && node.tag != "textarea") return false;
            node.mutable_form_control().value = state.value;
            node.mutable_form_control().value_initialized = true;
            node.mutable_form_control().dirty_value = true;
            node.mutable_form_control().value_changed_by_user = false;
            node.mutable_form_control().user_validity_interacted = false;
            return true;
        case profile_form_control_kind::checked:
            if (node.tag != "input"
                || (!forms::input_type_is(node, "checkbox")
                    && !forms::input_type_is(node, "radio"))) return false;
            node.mutable_form_control().checkedness_initialized = true;
            node.mutable_form_control().checkedness = state.checked;
            node.mutable_form_control().user_validity_interacted = false;
            return true;
        case profile_form_control_kind::selected: {
            if (node.tag != "select") return false;
            std::vector<dom_node*> options;
            collect_descendants_by_tag(node, "option", options);
            std::unordered_set<uint32_t> selected(
                state.selected_indices.begin(), state.selected_indices.end());
            auto any = false;
            for (uint32_t index = 0U; index < options.size(); ++index) {
                auto& option = options[index]->mutable_form_control();
                option.selectedness_initialized = true;
                option.selectedness = selected.contains(index);
                any = any || option.selectedness;
            }
            node.mutable_form_control().selection_explicitly_empty = !any;
            node.mutable_form_control().user_validity_interacted = false;
            return true;
        }
        case profile_form_control_kind::custom:
            if (!node.form_control().form_associated_custom_element) return false;
            dispatch_custom_form_restore(node, state.value, autocomplete);
            node.mutable_form_control().user_validity_interacted = false;
            return true;
        }
        return false;
    }

    void restore_form_state(
        dom_node& root,
        const profile_form_document_state& state,
        bool autocomplete)
    {
        if (state.controls.empty()
            || state.controls.size() > maximum_form_restore_controls
            || form_state_bytes(state) > maximum_form_restore_document_bytes) return;
        std::unordered_map<std::string, dom_node*> candidates;
        for (auto* node : bounded_form_restore_nodes(root)) {
            if (node == nullptr) continue;
            auto identity = form_control_identity(root, *node);
            if (!identity.empty()) candidates.try_emplace(std::move(identity), node);
        }
        pending_form_restore_state pending;
        pending.autocomplete = autocomplete;
        auto changed = false;
        for (const auto& control : state.controls) {
            const auto known = candidates.find(control.identity);
            if (known != candidates.end()
                && restore_form_control(*known->second, control, autocomplete)) {
                changed = true;
            } else if (control.kind == profile_form_control_kind::custom) {
                pending.controls.try_emplace(control.identity, control);
            }
        }
        if (pending.controls.empty()) pending_form_restores.erase(root.id);
        else pending_form_restores.insert_or_assign(root.id, std::move(pending));
        if (changed) {
            recascade_connected_subtree(root, "form-state-restore");
            document.mark_dirty();
        }
    }

    void restore_pending_form_control(dom_node& node)
    {
        auto* root = const_cast<dom_node*>(forms::tree_root(document, node));
        if (root == nullptr) return;
        const auto pending = pending_form_restores.find(root->id);
        if (pending == pending_form_restores.end()) return;
        const auto identity = form_control_identity(*root, node);
        const auto known = pending->second.controls.find(identity);
        if (known == pending->second.controls.end()) return;
        if (!restore_form_control(
                node, known->second, pending->second.autocomplete)) return;
        pending->second.controls.erase(known);
        if (pending->second.controls.empty()) pending_form_restores.erase(pending);
        document.mark_dirty();
    }

    void persist_form_state(
        std::string_view address,
        const profile_form_document_state& state)
    {
        if (profile_storage == nullptr) return;
        const auto identity = form_document_identity(address);
        if (identity.empty()) return;
        profile_storage->replace_form_state(identity, state);
    }

    void store_nested_form_state(
        nested_history_state& history,
        size_t index,
        profile_form_document_state state)
    {
        if (index >= history.entries.size()) return;
        const auto incoming = form_state_bytes(state);
        if (incoming > maximum_form_restore_document_bytes) return;
        history.entries[index].form_state = {};
        size_t retained = 0U;
        for (const auto& entry : history.entries)
            retained += form_state_bytes(entry.form_state);
        for (size_t candidate = 0U;
            retained + incoming > maximum_form_restore_history_bytes
                && candidate < history.entries.size(); ++candidate) {
            if (candidate == index) continue;
            retained -= form_state_bytes(history.entries[candidate].form_state);
            history.entries[candidate].form_state = {};
        }
        if (retained + incoming <= maximum_form_restore_history_bytes)
            history.entries[index].form_state = std::move(state);
    }

    void capture_nested_form_state(
        v8::Local<v8::Context> local_context,
        bool persist)
    {
        auto* frame = active_nested_frame_for_context(local_context);
        auto* history = nested_history_for_context(local_context, false);
        auto* root = static_cast<dom_node*>(
            local_context->GetAlignedPointerFromEmbedderData(
                1, v8::kEmbedderDataTypeTagDefault));
        if (frame == nullptr || history == nullptr || root == nullptr
            || history->index >= history->entries.size()) return;
        auto state = capture_form_state(*root);
        if (persist) persist_form_state(
            navigation_address_for_root(root), state);
        store_nested_form_state(*history, history->index, std::move(state));
    }

    void restore_after_document_parse(
        dom_node& root,
        std::string_view address,
        std::optional<uint32_t> frame_id = std::nullopt)
    {
        if (frame_id.has_value()) {
            const auto history = nested_history_states.find(*frame_id);
            if (history != nested_history_states.end()
                && history->second.index < history->second.entries.size()
                && !history->second.entries[history->second.index]
                    .form_state.controls.empty()) {
                restore_form_state(
                    root,
                    history->second.entries[history->second.index].form_state,
                    false);
                return;
            }
        }
        if (profile_storage == nullptr) return;
        const auto identity = form_document_identity(address);
        if (identity.empty()) return;
        const auto profile = profile_storage->snapshot();
        const auto known = profile.form_states.find(identity);
        if (known != profile.form_states.end())
            restore_form_state(root, known->second, true);
    }
