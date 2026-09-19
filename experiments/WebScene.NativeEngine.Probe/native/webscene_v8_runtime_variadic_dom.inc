    enum class variadic_insertion { append, prepend, before, after, replace_children, replace_with };

    static void insert_variadic_nodes(const v8::FunctionCallbackInfo<v8::Value>& info,
        variadic_insertion placement)
    {
        auto* self = current(info.GetIsolate());
        custom_element_reaction_scope reactions(*self);
        const auto* operation = placement == variadic_insertion::append ? "ParentNode.append"
            : placement == variadic_insertion::prepend ? "ParentNode.prepend"
            : placement == variadic_insertion::before ? "ChildNode.before"
            : placement == variadic_insertion::after ? "ChildNode.after"
            : placement == variadic_insertion::replace_children ? "ParentNode.replaceChildren" : "ChildNode.replaceWith";
        binding_callback_timer timer(self->profile(binding_category::dom_mutation),
            self->profile_named(operation));
        auto* receiver = unwrap_node(info.This());
        if (receiver == nullptr) return;
        const auto context = info.GetIsolate()->GetCurrentContext();

        // WebIDL converts every non-Node argument before the DOM algorithm
        // reads parent/sibling state. A failed conversion must stop immediately.
        std::vector<dom_node*> arguments;
        arguments.reserve(static_cast<size_t>(info.Length()));
        for (int i = 0; i < info.Length(); ++i) {
            auto* node = info[i]->IsObject() ? unwrap_node(info[i].As<v8::Object>()) : nullptr;
            if (node == nullptr) {
                v8::Local<v8::String> converted;
                if (!info[i]->ToString(context).ToLocal(&converted)) return;
                node = &self->document.create_element("#text");
                node->text_content = to_wtf8(info.GetIsolate(), converted);
            }
            arguments.push_back(node);
        }
        const bool replace_all = placement == variadic_insertion::replace_children;
        const bool replace_self = placement == variadic_insertion::replace_with;
        const bool sibling = placement == variadic_insertion::before || placement == variadic_insertion::after || replace_self;
        auto* parent = sibling ? receiver->parent : receiver;
        if (parent == nullptr || (arguments.empty() && !replace_all && !replace_self)) return;
        // Clearing an already empty ParentNode has no removal steps, mutation
        // record, focus change, or custom-element reactions. Avoid routing
        // that exact state through child-list recascade and scene publication.
        if (replace_all && arguments.empty()
            && parent->children.empty()
            && parent->text_content.empty()) {
            return;
        }

        // Find a sibling which will survive conversion, even if the receiver
        // itself or surrounding siblings occur in the argument list.
        dom_node* viable = nullptr;
        if (sibling) {
            const std::unordered_set<dom_node*> incoming(arguments.begin(), arguments.end());
            const auto found = std::find(parent->children.begin(), parent->children.end(), receiver);
            if (found == parent->children.end()) return;
            if (placement == variadic_insertion::before) {
                for (auto it = found; it != parent->children.begin();) {
                    auto* candidate = *--it;
                    if (!incoming.contains(candidate)) { viable = candidate; break; }
                }
            } else {
                for (auto it = std::next(found); it != parent->children.end(); ++it) {
                    if (!incoming.contains(*it)) { viable = *it; break; }
                }
            }
        }

        std::vector<dom_node*> previous_parents;
        std::unordered_set<dom_node*> previous_seen;
        const auto remember_parent = [&](dom_node* old_parent) {
            if (old_parent != nullptr && previous_seen.insert(old_parent).second)
                previous_parents.push_back(old_parent);
        };
        const auto clear_replacement_focus = [&](dom_node& root) {
            self->document.unregister_modal_subtree(root);
            auto *focused_owner = self->focused_document_owner_id == 0U
                                      ? nullptr
                                      : self->document.find_by_native_id(
                                            self->focused_document_owner_id);
            const auto removes_focused_document =
                focused_owner != nullptr &&
                self->subtree_contains(root, focused_owner);
            if (!self->subtree_contains(root, self->active_element) &&
                !removes_focused_document)
              return;
            // Removal clears focus without dispatching blur/focusout (also
            // Blink's RemoveFocusedElementOfSubtree with omitted blur events).
            // Do this before detachment so :focus-within ancestors are updated.
            auto* focused = self->active_element;
            if (self->subtree_contains(root, focused)) {
              focused->mutable_form_control().input_focused = false;
              focused->mutable_form_control().caret_visible = false;
              self->active_element = nullptr;
              self->recascade_focus_subject(*focused,
                                            "focus-replacement-removal");
            }
            if (removes_focused_document) {
              self->focused_document_owner_id = 0U;
            }
            self->document.mark_dirty();
        };
        const auto detach = [&](dom_node* node) {
            if ((replace_all || replace_self) && node->parent != nullptr) clear_replacement_focus(*node);
            auto* old_parent = node->parent;
            if (old_parent == nullptr) return;
            if (self->is_connected(*node)) self->notify_custom_elements_subtree(*node, "disconnected");
            remember_parent(old_parent);
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
            self->dom_variadic_detach_child_visits += old_parent->children.size();
#endif
            std::erase(old_parent->children, node);
            node->parent = nullptr;
            synchronize_textarea_value_from_children(*old_parent);
        };
        // Like Blink's ConvertNodeUnionsIntoNodes, use a flat worklist instead
        // of inserting/removing everything through a temporary DOM fragment.
        // Source removals still happen before multi-node pre-insertion checks.
        std::vector<dom_node*> nodes;
        for (auto* argument : arguments) {
            if (argument->kind == dom_node_kind::document_fragment) {
                remember_parent(argument);
                auto children = std::move(argument->children);
                argument->children.clear();
                for (auto* child : children) {
                    if (child == nullptr) continue;
                    child->parent = nullptr;
                    nodes.push_back(child);
                }
            } else nodes.push_back(argument);
        }
        if (nodes.size() > 1U) {
            std::unordered_map<dom_node*, size_t> last_occurrence;
            std::vector<dom_node*> first_occurrences;
            const bool can_batch_detachment = !self->custom_elements_active
                && (!(replace_all || replace_self) || self->active_element == nullptr);
            if (can_batch_detachment) first_occurrences.reserve(nodes.size());
            for (size_t i = 0; i < nodes.size(); ++i) {
                const auto [entry, first] = last_occurrence.try_emplace(nodes[i], i);
                if (first) {
                    if (can_batch_detachment) first_occurrences.push_back(nodes[i]);
                    else detach(nodes[i]);
                }
                else entry->second = i;
            }
            // No author callback or focus recascade can observe this interval.
            // Preserve first-removal order in parent links (including nested
            // inputs), then compact each affected parent once. Restricting this
            // to adjacent same-parent runs makes alternating parents quadratic.
            for (auto* node : first_occurrences) {
                auto* old_parent = node->parent;
                if (old_parent == nullptr) continue;
                if (replace_all || replace_self) clear_replacement_focus(*node);
                remember_parent(old_parent);
                node->parent = nullptr;
            }
            if (can_batch_detachment) {
                for (auto* old_parent : previous_parents) {
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
                    self->dom_variadic_detach_child_visits += old_parent->children.size();
#endif
                    // Only removed entries have null parents. Other children
                    // retain their parent even when that parent is also moved.
                    std::erase_if(old_parent->children, [](auto* node) {
                        return node != nullptr && node->parent == nullptr;
                    });
                }
                // All child lists must be final before reading source text.
                for (auto* old_parent : previous_parents)
                    synchronize_textarea_value_from_children(*old_parent);
            }
            size_t output = 0;
            for (size_t i = 0; i < nodes.size(); ++i)
                if (last_occurrence.at(nodes[i]) == i) nodes[output++] = nodes[i];
            nodes.resize(output);
        }
        const auto finish_sources = [&] {
            bool stylesheets_changed = false;
            for (auto* node : nodes)
                if (!self->is_connected(*node)) stylesheets_changed |= self->deactivate_connected_stylesheets(*node, false);
            if (stylesheets_changed) self->recascade_after_stylesheet_change();
            for (auto* old_parent : previous_parents) self->recascade_changed_child_list(*old_parent);
            if (!previous_parents.empty()) self->document.mark_dirty();
        };
        const auto invalid_parent = parent->kind != dom_node_kind::element
            && parent->kind != dom_node_kind::document_fragment && parent->tag != "#document-root";
        const bool document_parent = parent->tag == "#document-root"
            || (!sibling && self->is_document_object(info.This()));
        if (invalid_parent || std::any_of(nodes.begin(), nodes.end(), [&](const auto* node) {
                return insertion_would_create_cycle(parent, node)
                    || (node->kind == dom_node_kind::document_type && !document_parent)
                    || node->tag == "#document-root";
            })) {
            finish_sources();
            throw_dom_exception(info, "The requested insertion violates the node hierarchy", "HierarchyRequestError");
            return;
        }
        if (nodes.empty() && !replace_all && !replace_self) return;

        dom_node* reference = nullptr;
        if (placement == variadic_insertion::prepend
            || (placement == variadic_insertion::before && viable == nullptr)) {
            if (!parent->children.empty()) reference = parent->children.front();
        } else if (placement == variadic_insertion::before) {
            const auto found = std::find(parent->children.begin(), parent->children.end(), viable);
            if (found != parent->children.end() && std::next(found) != parent->children.end()) reference = *std::next(found);
        } else if (placement == variadic_insertion::after || replace_self) reference = viable;
        if (nodes.size() == 1U) {
            if (reference == nodes.front()) {
                const auto found = std::find(parent->children.begin(), parent->children.end(), reference);
                reference = found != parent->children.end() && std::next(found) != parent->children.end()
                    ? *std::next(found) : nullptr;
            }
            detach(nodes.front());
        }
        if (replace_all) {
            for (auto* child : parent->children) if (child != nullptr) clear_replacement_focus(*child);
            self->detach_all_children(*parent);
            parent->text_content.clear();
        } else if (replace_self && receiver->parent == parent) {
            detach(receiver);
            self->deactivate_connected_stylesheets(*receiver);
            receiver->visible = false;
            self->weaken_detached_subtree_wrappers(*receiver);
        }
        const auto position = reference == nullptr ? parent->children.end()
            : std::find(parent->children.begin(), parent->children.end(), reference);
        if (reference != nullptr && position == parent->children.end()) {
            finish_sources();
            throw_dom_exception(info, "The insertion reference is no longer a child", "NotFoundError");
            return;
        }
        for (auto* node : nodes) { node->parent = parent; node->visible = true; }
        parent->children.insert(position, nodes.begin(), nodes.end());
        self->document.mark_dirty();
        self->recascade_completed_child_list(*parent, nodes, previous_parents);
        for (auto* node : nodes) {
            self->enqueue_connected_resources_subtree(*node, self->wrap_node(*node), context);
            self->enqueue_iframe_hydration_if_needed(*node);
            self->notify_custom_elements_subtree(*node, "connected");
        }
        self->activate_connected_stylesheet(*parent);
        synchronize_textarea_value_from_children(*parent);
    }
