    static std::string semantic_normalize_text(std::string_view value)
    {
        std::string result;
        result.reserve(std::min(value.size(), maximum_semantic_text_bytes_v1));
        bool pending_space = false;
        for (const auto character : value) {
            const auto whitespace = character == ' ' || character == '\t'
                || character == '\r' || character == '\n' || character == '\f';
            if (whitespace) {
                pending_space = !result.empty();
                continue;
            }
            if (pending_space && result.size() < maximum_semantic_text_bytes_v1) {
                result.push_back(' ');
            }
            pending_space = false;
            if (result.size() == maximum_semantic_text_bytes_v1) break;
            result.push_back(character);
        }
        return result;
    }

    static std::string_view semantic_attribute(
        const dom_node& node,
        std::string_view name)
    {
        const auto found = node.attributes.find(std::string(name));
        return found == node.attributes.end()
            ? std::string_view{}
            : std::string_view(found->second);
    }

    static bool semantic_true(std::string_view value)
    {
        return lower_html_name(std::string(value)) == "true";
    }

    static bool semantic_hidden(const native_document& document, const dom_node& node)
    {
        if (!node.visible || node.attributes.find("hidden") != node.attributes.end()
            || node.style.display == display_mode::none
            || node.style.visibility_hidden || node.style.content_visibility_hidden
            || semantic_true(semantic_attribute(node, "aria-hidden"))) return true;
        return node.kind == dom_node_kind::element && document.is_inert(node);
    }

    static std::string semantic_role(const dom_node& node, bool document_root)
    {
        auto explicit_role = semantic_normalize_text(semantic_attribute(node, "role"));
        if (!explicit_role.empty()) {
            const auto separator = explicit_role.find(' ');
            if (separator != std::string::npos) explicit_role.resize(separator);
            return lower_html_name(std::move(explicit_role));
        }
        if (node.kind == dom_node_kind::text) return "staticText";
        if (document_root) return "document";
        if (node.tag == "button") return "button";
        if (node.tag == "textarea") return "textbox";
        if (node.tag == "select") return semantic_attribute(node, "multiple").empty()
            ? "combobox" : "listbox";
        if (node.tag == "option") return "option";
        if (node.tag == "a" && !semantic_attribute(node, "href").empty()) return "link";
        if (node.tag == "img") return "img";
        if (node.tag == "ul" || node.tag == "ol") return "list";
        if (node.tag == "li") return "listitem";
        if (node.tag == "table") return "table";
        if (node.tag == "thead" || node.tag == "tbody" || node.tag == "tfoot") return "rowgroup";
        if (node.tag == "tr") return "row";
        if (node.tag == "th") return "columnheader";
        if (node.tag == "td") return "cell";
        if (node.tag == "dialog") return "dialog";
        if (node.tag == "iframe") return "document";
        if (node.tag.size() == 2U && node.tag[0] == 'h'
            && node.tag[1] >= '1' && node.tag[1] <= '6') return "heading";
        if (node.tag == "input") {
            const auto type = lower_html_name(std::string(semantic_attribute(node, "type")));
            if (type == "checkbox") return "checkbox";
            if (type == "radio") return "radio";
            if (type == "button" || type == "submit" || type == "reset") return "button";
            if (type == "range") return "slider";
            if (type == "number") return "spinbutton";
            return "textbox";
        }
        return "generic";
    }

    static uint32_t semantic_action_support_bit(uint32_t action)
    {
        switch (action) {
            case WEBSCENE_SEMANTIC_ACTION_FOCUS_V1:
                return WEBSCENE_SEMANTIC_ACTION_SUPPORT_FOCUS_V1;
            case WEBSCENE_SEMANTIC_ACTION_PRESS_V1:
                return WEBSCENE_SEMANTIC_ACTION_SUPPORT_PRESS_V1;
            case WEBSCENE_SEMANTIC_ACTION_TOGGLE_V1:
                return WEBSCENE_SEMANTIC_ACTION_SUPPORT_TOGGLE_V1;
            case WEBSCENE_SEMANTIC_ACTION_INCREMENT_V1:
                return WEBSCENE_SEMANTIC_ACTION_SUPPORT_INCREMENT_V1;
            case WEBSCENE_SEMANTIC_ACTION_DECREMENT_V1:
                return WEBSCENE_SEMANTIC_ACTION_SUPPORT_DECREMENT_V1;
            case WEBSCENE_SEMANTIC_ACTION_SET_VALUE_V1:
                return WEBSCENE_SEMANTIC_ACTION_SUPPORT_SET_VALUE_V1;
            case WEBSCENE_SEMANTIC_ACTION_SET_SELECTION_V1:
                return WEBSCENE_SEMANTIC_ACTION_SUPPORT_SET_SELECTION_V1;
            default:
                return 0U;
        }
    }

    bool semantic_action_visible(const dom_node& node)
    {
        for (auto* current = &node;
            current != nullptr;
            current = document.composed_parent(*current)) {
            if (semantic_hidden(document, *current)) return false;
        }
        return true;
    }

    static bool semantic_text_selection_control(const dom_node& node)
    {
        if (node.tag == "textarea") return true;
        if (node.tag != "input") return false;
        const auto type = lower_html_name(
            std::string(semantic_attribute(node, "type")));
        return type.empty() || type == "text" || type == "search"
            || type == "tel" || type == "url" || type == "password";
    }

    uint32_t semantic_supported_actions(const dom_node& node)
    {
        if (!is_connected(node) || !semantic_action_visible(node)
            || document.is_inert(node)
            || node.attributes.contains("disabled")
            || semantic_true(semantic_attribute(node, "aria-disabled"))) {
            return 0U;
        }
        uint32_t result = 0U;
        if (is_programmatically_focusable(&node)) {
            result |= WEBSCENE_SEMANTIC_ACTION_SUPPORT_FOCUS_V1;
        }
        const auto role = semantic_role(node, false);
        const auto type = node.tag == "input"
            ? lower_html_name(std::string(semantic_attribute(node, "type")))
            : std::string{};
        if (node.tag == "button"
            || (node.tag == "a" && !semantic_attribute(node, "href").empty())
            || (node.tag == "input" && (type == "button" || type == "submit"
                || type == "reset" || type == "image" || type == "radio"))
            || role == "button") {
            result |= WEBSCENE_SEMANTIC_ACTION_SUPPORT_PRESS_V1;
        }
        if ((node.tag == "input" && type == "checkbox")
            || ((role == "checkbox" || role == "switch")
                && !semantic_attribute(node, "aria-checked").empty())) {
            result |= WEBSCENE_SEMANTIC_ACTION_SUPPORT_TOGGLE_V1;
        }
        if (node.tag == "input" && (type == "range" || type == "number")
            && !node.attributes.contains("readonly")
            && lower_html_name(std::string(semantic_attribute(node, "step")))
                != "any") {
            result |= WEBSCENE_SEMANTIC_ACTION_SUPPORT_INCREMENT_V1
                | WEBSCENE_SEMANTIC_ACTION_SUPPORT_DECREMENT_V1;
        }
        if (semantic_text_selection_control(node)) {
            result |= WEBSCENE_SEMANTIC_ACTION_SUPPORT_SET_SELECTION_V1;
            if (!node.attributes.contains("readonly")
                && !semantic_true(semantic_attribute(node, "aria-readonly"))) {
                result |= WEBSCENE_SEMANTIC_ACTION_SUPPORT_SET_VALUE_V1;
            }
        }
        return result;
    }

    static uint64_t semantic_identity(
        uint64_t document_generation,
        uint64_t frame_generation,
        uint32_t frame_owner_dom_node_id,
        uint32_t dom_node_id)
    {
        uint64_t value = UINT64_C(1469598103934665603);
        const auto mix = [&](uint64_t part) {
            for (unsigned byte = 0; byte < 8U; ++byte) {
                value ^= (part >> (byte * 8U)) & 0xffU;
                value *= UINT64_C(1099511628211);
            }
        };
        mix(document_generation);
        mix(frame_generation);
        mix(frame_owner_dom_node_id);
        mix(dom_node_id);
        return value == 0U ? UINT64_C(1) : value;
    }

    struct semantic_live_configuration_v1 final {
        uint32_t role{WEBSCENE_SEMANTIC_LIVE_REGION_GENERIC_V1};
        uint32_t politeness{};
        uint32_t relevant{};
        bool atomic{};
        bool enabled{};
    };

    static semantic_live_configuration_v1 semantic_live_configuration(
        const dom_node& node)
    {
        semantic_live_configuration_v1 result;
        auto role = semantic_normalize_text(semantic_attribute(node, "role"));
        const auto role_separator = role.find(' ');
        if (role_separator != std::string::npos) role.resize(role_separator);
        role = lower_html_name(std::move(role));
        if (role == "status") {
            result.role = WEBSCENE_SEMANTIC_LIVE_REGION_STATUS_V1;
            result.politeness = WEBSCENE_SEMANTIC_LIVE_POLITE_V1;
            result.atomic = true;
        } else if (role == "alert") {
            result.role = WEBSCENE_SEMANTIC_LIVE_REGION_ALERT_V1;
            result.politeness = WEBSCENE_SEMANTIC_LIVE_ASSERTIVE_V1;
            result.atomic = true;
        } else if (role == "log") {
            result.role = WEBSCENE_SEMANTIC_LIVE_REGION_LOG_V1;
            result.politeness = WEBSCENE_SEMANTIC_LIVE_POLITE_V1;
        }

        const auto live_attribute = node.attributes.find("aria-live");
        if (live_attribute != node.attributes.end()) {
            const auto live = lower_html_name(semantic_normalize_text(
                live_attribute->second));
            if (live == "polite") {
                result.politeness = WEBSCENE_SEMANTIC_LIVE_POLITE_V1;
            } else if (live == "assertive") {
                result.politeness = WEBSCENE_SEMANTIC_LIVE_ASSERTIVE_V1;
            } else {
                result.politeness = 0U;
            }
        }
        result.enabled = result.politeness != 0U;
        if (!result.enabled) return result;

        const auto atomic_attribute = node.attributes.find("aria-atomic");
        if (atomic_attribute != node.attributes.end()) {
            result.atomic = semantic_true(atomic_attribute->second);
        }
        const auto default_relevant = result.role
                == WEBSCENE_SEMANTIC_LIVE_REGION_LOG_V1
            ? WEBSCENE_SEMANTIC_LIVE_RELEVANT_ADDITIONS_V1
            : WEBSCENE_SEMANTIC_LIVE_RELEVANT_ADDITIONS_V1
                | WEBSCENE_SEMANTIC_LIVE_RELEVANT_TEXT_V1;
        result.relevant = default_relevant;
        const auto relevant_attribute = node.attributes.find("aria-relevant");
        if (relevant_attribute != node.attributes.end()) {
            result.relevant = 0U;
            auto tokens = lower_html_name(relevant_attribute->second);
            size_t cursor = 0U;
            while (cursor < tokens.size()) {
                while (cursor < tokens.size() && std::isspace(
                    static_cast<unsigned char>(tokens[cursor]))) ++cursor;
                const auto begin = cursor;
                while (cursor < tokens.size() && !std::isspace(
                    static_cast<unsigned char>(tokens[cursor]))) ++cursor;
                const auto token = std::string_view(tokens).substr(
                    begin, cursor - begin);
                if (token == "all") {
                    result.relevant = WEBSCENE_SEMANTIC_LIVE_RELEVANT_ADDITIONS_V1
                        | WEBSCENE_SEMANTIC_LIVE_RELEVANT_TEXT_V1
                        | WEBSCENE_SEMANTIC_LIVE_RELEVANT_REMOVALS_V1;
                } else if (token == "additions") {
                    result.relevant |= WEBSCENE_SEMANTIC_LIVE_RELEVANT_ADDITIONS_V1;
                } else if (token == "text") {
                    result.relevant |= WEBSCENE_SEMANTIC_LIVE_RELEVANT_TEXT_V1;
                } else if (token == "removals") {
                    result.relevant |= WEBSCENE_SEMANTIC_LIVE_RELEVANT_REMOVALS_V1;
                }
            }
            if (result.relevant == 0U) result.relevant = default_relevant;
        }
        return result;
    }

    static void semantic_live_append_text(
        std::string& destination,
        std::string_view value,
        bool& truncated)
    {
        if (value.empty()) return;
        auto available = static_cast<size_t>(
            WEBSCENE_SEMANTIC_LIVE_MAXIMUM_TEXT_BYTES_V1) - destination.size();
        if (!destination.empty()) {
            if (available == 0U) {
                truncated = true;
                return;
            }
            destination.push_back(' ');
            --available;
        }
        auto copied = std::min(available, value.size());
        if (copied < value.size()) {
            while (copied > 0U
                && (static_cast<unsigned char>(value[copied]) & 0xc0U) == 0x80U) {
                --copied;
            }
        }
        destination.append(value.substr(0U, copied));
        truncated = truncated || copied != value.size();
    }

    static void semantic_live_trim_incomplete_utf8(std::string& value)
    {
        if (value.empty()) return;
        auto lead = value.size() - 1U;
        while (lead > 0U
            && (static_cast<unsigned char>(value[lead]) & 0xc0U) == 0x80U) {
            --lead;
        }
        const auto first = static_cast<unsigned char>(value[lead]);
        const auto expected = (first & 0x80U) == 0U ? 1U
            : (first & 0xe0U) == 0xc0U ? 2U
            : (first & 0xf0U) == 0xe0U ? 3U
            : (first & 0xf8U) == 0xf0U ? 4U : 1U;
        if (value.size() - lead < expected) value.resize(lead);
    }

    semantic_live_capture_data_v1 take_semantic_live_events_v1()
    {
        constexpr size_t maximum_scan_nodes = 16U * 1024U;
        constexpr size_t maximum_regions = 256U;
        constexpr size_t maximum_fragments_per_region = 4096U;
        std::vector<semantic_live_region_state_v1> current;
        current.reserve(16U);
        size_t scanned_nodes = 0U;
        bool scan_truncated = false;
        const auto top_generation = document_generation.load(
            std::memory_order_acquire);

        std::function<void(const dom_node&, semantic_live_region_state_v1&, bool)>
            collect_fragments;
        collect_fragments = [&](const dom_node& node,
                                semantic_live_region_state_v1& region,
                                bool is_root) {
            if (++scanned_nodes > maximum_scan_nodes
                || region.fragments.size() >= maximum_fragments_per_region) {
                scan_truncated = true;
                return;
            }
            if (!is_root && semantic_hidden(document, node)) return;
            if (!is_root && node.kind == dom_node_kind::element
                && semantic_live_configuration(node).enabled) return;
            if (node.kind == dom_node_kind::text) {
                auto text = semantic_normalize_text(node.text_content);
                region.text_truncated = region.text_truncated
                    || node.text_content.size()
                        > WEBSCENE_SEMANTIC_LIVE_MAXIMUM_TEXT_BYTES_V1;
                semantic_live_trim_incomplete_utf8(text);
                if (!text.empty()) {
                    region.fragments.push_back({node.id, std::move(text)});
                }
            }
            for (const auto* child : document.composed_children(node)) {
                if (child == nullptr || scan_truncated) continue;
                collect_fragments(*child, region, false);
            }
        };

        std::function<void(const dom_node&, uint32_t, uint64_t, bool, bool)> visit;
        visit = [&](const dom_node& node, uint32_t frame_owner,
                    uint64_t frame_generation, bool ancestor_hidden,
                    bool ancestor_busy) {
            if (++scanned_nodes > maximum_scan_nodes) {
                scan_truncated = true;
                return;
            }
            const auto hidden = ancestor_hidden || semantic_hidden(document, node);
            if (hidden) return;
            auto active_owner = frame_owner;
            auto active_frame_generation = frame_generation;
            if (node.tag == "body" && node.parent != nullptr
                && node.parent->tag == "iframe") {
                active_owner = node.parent->id;
                const auto generation = frame_navigation_generations.find(active_owner);
                active_frame_generation = generation == frame_navigation_generations.end()
                    ? 0U : generation->second;
            }
            const auto busy = ancestor_busy
                || semantic_true(semantic_attribute(node, "aria-busy"));
            const auto configuration = semantic_live_configuration(node);
            if (configuration.enabled) {
                if (current.size() == maximum_regions) {
                    scan_truncated = true;
                    return;
                }
                semantic_live_region_state_v1 region;
                region.top_document_generation = top_generation;
                region.frame_generation = active_frame_generation;
                region.frame_owner_dom_node_id = active_owner;
                region.dom_node_id = node.id;
                region.semantic_id = semantic_identity(
                    top_generation, active_frame_generation, active_owner, node.id);
                region.role = configuration.role;
                region.politeness = configuration.politeness;
                region.relevant = configuration.relevant;
                region.atomic = configuration.atomic;
                region.busy = busy;
                collect_fragments(node, region, true);
                for (const auto& fragment : region.fragments) {
                    semantic_live_append_text(
                        region.text, fragment.text, region.text_truncated);
                }
                current.push_back(std::move(region));
            }
            for (const auto* child : document.composed_children(node)) {
                if (child == nullptr || scan_truncated) continue;
                visit(*child, active_owner, active_frame_generation, hidden, busy);
            }
        };
        visit(document.body(), 0U, 0U, false, false);
        if (scan_truncated) return {};

        std::vector<semantic_live_event_data_v1> events;
        events.reserve(std::min(current.size(), static_cast<size_t>(32U)));
        std::unordered_set<uint64_t> retained;
        retained.reserve(current.size());
        for (auto& region : current) {
            retained.insert(region.semantic_id);
            auto previous = semantic_live_regions.find(region.semantic_id);
            if (previous == semantic_live_regions.end()) {
                if (!region.busy
                    && !semantic_live_suppress_initial_alerts
                    && region.role == WEBSCENE_SEMANTIC_LIVE_REGION_ALERT_V1
                    && !region.text.empty()) {
                    semantic_live_event_data_v1 event;
                    event.top_document_generation = region.top_document_generation;
                    event.frame_generation = region.frame_generation;
                    event.semantic_id = region.semantic_id;
                    event.frame_owner_dom_node_id = region.frame_owner_dom_node_id;
                    event.dom_node_id = region.dom_node_id;
                    event.role = region.role;
                    event.politeness = region.politeness;
                    event.flags = WEBSCENE_SEMANTIC_LIVE_ATOMIC_V1
                        | WEBSCENE_SEMANTIC_LIVE_INITIAL_ALERT_V1
                        | WEBSCENE_SEMANTIC_LIVE_RELEVANT_ADDITIONS_V1
                        | (region.text_truncated
                            ? WEBSCENE_SEMANTIC_LIVE_TEXT_TRUNCATED_V1 : 0U);
                    event.text = region.text;
                    events.push_back(std::move(event));
                }
                semantic_live_regions.insert_or_assign(
                    region.semantic_id, std::move(region));
                continue;
            }
            if (region.busy) {
                previous->second.busy = true;
                continue;
            }

            std::unordered_map<uint32_t, std::string_view> old_fragments;
            std::unordered_map<uint32_t, std::string_view> new_fragments;
            old_fragments.reserve(previous->second.fragments.size());
            new_fragments.reserve(region.fragments.size());
            for (const auto& fragment : previous->second.fragments)
                old_fragments.emplace(fragment.dom_node_id, fragment.text);
            for (const auto& fragment : region.fragments)
                new_fragments.emplace(fragment.dom_node_id, fragment.text);

            uint32_t causes = 0U;
            std::string delta;
            bool text_truncated = false;
            std::unordered_set<uint32_t> replaced_old_fragments;
            uint64_t fingerprint = UINT64_C(1469598103934665603);
            const auto mix = [&](uint64_t value) {
                fingerprint ^= value;
                fingerprint *= UINT64_C(1099511628211);
            };
            for (size_t index = 0U; index < region.fragments.size(); ++index) {
                const auto& fragment = region.fragments[index];
                const auto old = old_fragments.find(fragment.dom_node_id);
                if (old == old_fragments.end()) {
                    const auto positional_replacement =
                        index < previous->second.fragments.size()
                        && !new_fragments.contains(
                            previous->second.fragments[index].dom_node_id);
                    if (positional_replacement) {
                        replaced_old_fragments.insert(
                            previous->second.fragments[index].dom_node_id);
                        if (previous->second.fragments[index].text
                            == fragment.text) continue;
                        if ((region.relevant
                                & WEBSCENE_SEMANTIC_LIVE_RELEVANT_TEXT_V1) == 0U)
                            continue;
                        causes |= WEBSCENE_SEMANTIC_LIVE_RELEVANT_TEXT_V1;
                        mix(previous->second.fragments[index].dom_node_id);
                    } else {
                        if ((region.relevant
                                & WEBSCENE_SEMANTIC_LIVE_RELEVANT_ADDITIONS_V1) == 0U)
                            continue;
                        causes |= WEBSCENE_SEMANTIC_LIVE_RELEVANT_ADDITIONS_V1;
                    }
                } else if (old->second != fragment.text) {
                    if ((region.relevant
                            & WEBSCENE_SEMANTIC_LIVE_RELEVANT_TEXT_V1) == 0U)
                        continue;
                    causes |= WEBSCENE_SEMANTIC_LIVE_RELEVANT_TEXT_V1;
                } else {
                    continue;
                }
                mix(fragment.dom_node_id);
                semantic_live_append_text(delta, fragment.text, text_truncated);
            }
            if ((region.relevant
                    & WEBSCENE_SEMANTIC_LIVE_RELEVANT_REMOVALS_V1) != 0U) {
                for (const auto& fragment : previous->second.fragments) {
                    if (new_fragments.contains(fragment.dom_node_id)
                        || replaced_old_fragments.contains(fragment.dom_node_id)) continue;
                    causes |= WEBSCENE_SEMANTIC_LIVE_RELEVANT_REMOVALS_V1;
                    mix(fragment.dom_node_id);
                    semantic_live_append_text(delta, fragment.text, text_truncated);
                }
            }
            if (causes != 0U) {
                auto event_text = region.atomic && !region.text.empty()
                    ? region.text : std::move(delta);
                for (const auto character : event_text) mix(
                    static_cast<unsigned char>(character));
                mix(causes);
                if (!event_text.empty()
                    && fingerprint != previous->second.last_event_fingerprint) {
                    semantic_live_event_data_v1 event;
                    event.top_document_generation = region.top_document_generation;
                    event.frame_generation = region.frame_generation;
                    event.semantic_id = region.semantic_id;
                    event.frame_owner_dom_node_id = region.frame_owner_dom_node_id;
                    event.dom_node_id = region.dom_node_id;
                    event.role = region.role;
                    event.politeness = region.politeness;
                    event.flags = causes
                        | (region.atomic ? WEBSCENE_SEMANTIC_LIVE_ATOMIC_V1 : 0U)
                        | (previous->second.busy
                            ? WEBSCENE_SEMANTIC_LIVE_BUSY_COALESCED_V1 : 0U)
                        | (text_truncated || region.text_truncated
                            ? WEBSCENE_SEMANTIC_LIVE_TEXT_TRUNCATED_V1 : 0U);
                    event.text = std::move(event_text);
                    events.push_back(std::move(event));
                    region.last_event_fingerprint = fingerprint;
                } else {
                    region.last_event_fingerprint =
                        previous->second.last_event_fingerprint;
                }
            } else {
                region.last_event_fingerprint = previous->second.last_event_fingerprint;
            }
            semantic_live_regions.insert_or_assign(
                region.semantic_id, std::move(region));
        }
        std::erase_if(semantic_live_regions, [&](const auto& entry) {
            return !retained.contains(entry.first);
        });
        semantic_live_suppress_initial_alerts = false;
        semantic_live_capture_data_v1 capture;
        capture.complete = true;
        capture.top_document_generation = top_generation;
        capture.live_region_semantic_ids.reserve(retained.size());
        for (const auto identity : retained) {
            capture.live_region_semantic_ids.push_back(identity);
        }
        std::sort(
            capture.live_region_semantic_ids.begin(),
            capture.live_region_semantic_ids.end());
        capture.events = std::move(events);
        return capture;
    }

    void retire_semantic_live_regions_v1(bool suppress_initial_alerts)
    {
        semantic_live_regions.clear();
        semantic_live_suppress_initial_alerts = suppress_initial_alerts;
    }

    std::shared_ptr<const semantic_snapshot_data_v1> build_semantic_snapshot_v1(
        uint64_t snapshot_generation)
    {
        ensure_layout();
        auto result = std::make_shared<semantic_snapshot_data_v1>();
        result->snapshot_generation = snapshot_generation;
        result->top_document_generation =
            document_generation.load(std::memory_order_acquire);
        result->layout_generation = document.scene_generation();
        result->documents.reserve(8U);
        result->nodes.reserve(std::min<size_t>(document.node_count(), 4096U));
        result->relationships.reserve(256U);
        result->action_targets.reserve(result->nodes.capacity());
        result->strings.reserve(16U * 1024U);

        struct captured_node final {
            const dom_node* node{};
            uint32_t document_index{};
        };
        std::vector<captured_node> captured;
        captured.reserve(result->nodes.capacity());
        std::vector<uint32_t> last_child_indices;
        last_child_indices.reserve(result->nodes.capacity());
        std::vector<std::unordered_map<std::string, uint32_t>> ids;
        std::vector<std::unordered_map<std::string, const dom_node*>> all_id_nodes;

        const auto append_document = [&](uint32_t frame_owner, uint64_t frame_generation,
                                         std::string_view origin) {
            if (result->documents.size() == maximum_semantic_documents_v1) {
                result->flags |= WEBSCENE_SEMANTIC_SNAPSHOT_TRUNCATED_DOCUMENTS_V1;
                return WEBSCENE_SEMANTIC_NONE_INDEX_V1;
            }
            webscene_semantic_document_v1 view{};
            view.struct_size = sizeof(view);
            view.version = 1U;
            view.document_generation = result->top_document_generation;
            view.frame_generation = frame_generation;
            view.frame_owner_dom_node_id = frame_owner;
            view.root_node_index = WEBSCENE_SEMANTIC_NONE_INDEX_V1;
            view.origin = result->append_string(origin);
            result->documents.push_back(view);
            ids.emplace_back();
            all_id_nodes.emplace_back();
            return static_cast<uint32_t>(result->documents.size() - 1U);
        };

        const auto top_document = append_document(0U, 0U, outer_storage_origin);
        if (top_document == WEBSCENE_SEMANTIC_NONE_INDEX_V1) return result;

        std::function<void(const dom_node&, uint32_t, uint32_t, bool)> visit;
        visit = [&](const dom_node& node, uint32_t document_index,
                    uint32_t parent_index, bool ancestor_hidden) {
            if (result->nodes.size() == maximum_semantic_nodes_v1) {
                result->flags |= WEBSCENE_SEMANTIC_SNAPSHOT_TRUNCATED_NODES_V1;
                return;
            }
            const auto hidden = ancestor_hidden || semantic_hidden(document, node);

            auto active_document = document_index;
            if (node.tag == "body" && node.parent != nullptr
                && node.parent->tag == "iframe") {
                if (hidden) return;
                const auto frame_id = node.parent->id;
                const auto generation = frame_navigation_generations.find(frame_id);
                auto origin = resource_origin(frame_base_addresses[&node]);
                if (!iframe_sandbox_allows(*node.parent, "allow-same-origin")) {
                    origin = "null";
                }
                active_document = append_document(
                    frame_id,
                    generation == frame_navigation_generations.end()
                        ? 0U : generation->second,
                    origin);
                if (active_document == WEBSCENE_SEMANTIC_NONE_INDEX_V1) return;
            }
            if (!node.id_attribute.empty()) {
                all_id_nodes[active_document].try_emplace(node.id_attribute, &node);
            }

            auto node_index = parent_index;
            const auto exposed = !hidden && (node.kind == dom_node_kind::element
                || node.kind == dom_node_kind::text);
            if (exposed) {
                webscene_semantic_node_v1 view{};
                view.struct_size = sizeof(view);
                view.version = 1U;
                view.dom_node_id = node.id;
                view.document_index = active_document;
                view.parent_index = parent_index;
                view.first_child_index = WEBSCENE_SEMANTIC_NONE_INDEX_V1;
                view.next_sibling_index = WEBSCENE_SEMANTIC_NONE_INDEX_V1;
                const auto& semantic_document = result->documents[active_document];
                view.semantic_id = semantic_identity(
                    semantic_document.document_generation,
                    semantic_document.frame_generation,
                    semantic_document.frame_owner_dom_node_id,
                    node.id);
                const auto bounds = transformed_client_bounds(document, node);
                view.x = bounds.x;
                view.y = bounds.y;
                view.width = bounds.width;
                view.height = bounds.height;
                view.role = result->append_string(semantic_role(
                    node,
                    result->documents[active_document].root_node_index
                        == WEBSCENE_SEMANTIC_NONE_INDEX_V1));
                view.supported_actions = semantic_supported_actions(node);
                node_index = static_cast<uint32_t>(result->nodes.size());
                result->nodes.push_back(view);
                result->action_targets.push_back({
                    view.semantic_id,
                    semantic_document.document_generation,
                    semantic_document.frame_generation,
                    semantic_document.frame_owner_dom_node_id,
                    node.id,
                    view.supported_actions});
                captured.push_back({&node, active_document});
                last_child_indices.push_back(WEBSCENE_SEMANTIC_NONE_INDEX_V1);
                if (!node.id_attribute.empty()) {
                    ids[active_document].try_emplace(node.id_attribute, node_index);
                }
                if (result->documents[active_document].root_node_index
                    == WEBSCENE_SEMANTIC_NONE_INDEX_V1) {
                    result->documents[active_document].root_node_index = node_index;
                }
                if (parent_index != WEBSCENE_SEMANTIC_NONE_INDEX_V1) {
                    auto& parent = result->nodes[parent_index];
                    if (parent.first_child_index == WEBSCENE_SEMANTIC_NONE_INDEX_V1) {
                        parent.first_child_index = node_index;
                    } else {
                        result->nodes[last_child_indices[parent_index]].next_sibling_index
                            = node_index;
                    }
                    last_child_indices[parent_index] = node_index;
                }
            }

            for (const auto* child : document.composed_children(node)) {
                if (child == nullptr) continue;
                visit(*child, active_document, node_index, hidden);
                if ((result->flags & WEBSCENE_SEMANTIC_SNAPSHOT_TRUNCATED_NODES_V1) != 0U)
                    break;
            }
        };
        visit(document.body(), top_document, WEBSCENE_SEMANTIC_NONE_INDEX_V1, false);

        const auto subtree_text = [&](const dom_node& root) {
            std::string text;
            size_t visited = 0U;
            std::function<void(const dom_node&)> collect = [&](const dom_node& current) {
                if (++visited > 4096U || text.size() >= maximum_semantic_text_bytes_v1) return;
                if (current.kind == dom_node_kind::text) {
                    if (!text.empty()) text.push_back(' ');
                    text.append(current.text_content.substr(
                        0U, maximum_semantic_text_bytes_v1 - text.size()));
                }
                for (const auto* child : document.composed_children(current)) {
                    if (child != nullptr) collect(*child);
                    if (visited > 4096U || text.size() >= maximum_semantic_text_bytes_v1) break;
                }
            };
            collect(root);
            return semantic_normalize_text(text);
        };

        const auto resolved_id_text = [&](std::string_view tokens, uint32_t document_index) {
            std::string text;
            size_t cursor = 0U;
            while (cursor < tokens.size()) {
                while (cursor < tokens.size() && std::isspace(
                    static_cast<unsigned char>(tokens[cursor]))) ++cursor;
                const auto begin = cursor;
                while (cursor < tokens.size() && !std::isspace(
                    static_cast<unsigned char>(tokens[cursor]))) ++cursor;
                if (begin == cursor) break;
                const auto found = all_id_nodes[document_index].find(
                    std::string(tokens.substr(begin, cursor - begin)));
                if (found == all_id_nodes[document_index].end()) continue;
                const auto alternative = subtree_text(*found->second);
                if (!alternative.empty()) {
                    if (!text.empty()) text.push_back(' ');
                    text.append(alternative);
                }
                if (text.size() >= maximum_semantic_text_bytes_v1) break;
            }
            return semantic_normalize_text(text);
        };

        std::vector<std::unordered_map<std::string, std::string>> label_texts(
            result->documents.size());
        for (const auto& capture : captured) {
            if (capture.node->tag != "label") continue;
            const auto target = semantic_attribute(*capture.node, "for");
            if (!target.empty()) {
                label_texts[capture.document_index].try_emplace(
                    target,
                    subtree_text(*capture.node));
            }
        }

        for (uint32_t index = 0U; index < captured.size(); ++index) {
            const auto& capture = captured[index];
            const auto& node = *capture.node;
            auto& view = result->nodes[index];
            auto name = semantic_normalize_text(semantic_attribute(node, "aria-label"));
            if (name.empty()) {
                name = resolved_id_text(
                    semantic_attribute(node, "aria-labelledby"), capture.document_index);
            }
            if (name.empty() && node.tag == "img") {
                name = semantic_normalize_text(semantic_attribute(node, "alt"));
            }
            if (name.empty() && node.kind == dom_node_kind::text) {
                name = semantic_normalize_text(node.text_content);
            }
            if (name.empty() && (node.tag == "button" || node.tag == "a"
                || node.tag == "option" || node.tag == "label"
                || (node.tag.size() == 2U && node.tag[0] == 'h'))) {
                name = subtree_text(node);
            }
            if (name.empty()) {
                name = semantic_normalize_text(semantic_attribute(node, "title"));
            }
            if (name.empty() && (node.tag == "input" || node.tag == "textarea"
                || node.tag == "select")) {
                const auto label = label_texts[capture.document_index].find(
                    node.id_attribute);
                if (label != label_texts[capture.document_index].end()) {
                    name = label->second;
                }
                for (auto* ancestor = node.parent;
                    name.empty() && ancestor != nullptr;
                    ancestor = ancestor->parent) {
                    if (ancestor->tag == "label") name = subtree_text(*ancestor);
                    if (ancestor->tag == "iframe") break;
                }
                if (name.empty()) name = semantic_normalize_text(
                    semantic_attribute(node, "placeholder"));
            }
            view.name = result->append_string(name);

            auto description = semantic_normalize_text(
                semantic_attribute(node, "aria-description"));
            if (description.empty()) {
                description = resolved_id_text(
                    semantic_attribute(node, "aria-describedby"), capture.document_index);
            }
            view.description = result->append_string(description);

            auto value = semantic_normalize_text(
                semantic_attribute(node, "aria-valuetext"));
            if (value.empty()) value = semantic_normalize_text(
                semantic_attribute(node, "aria-valuenow"));
            if (value.empty() && node.has_form_control()) value = node.form_control().value;
            if (value.empty() && (node.tag == "input" || node.tag == "textarea")) {
                value = semantic_normalize_text(semantic_attribute(node, "value"));
            }
            if (value.empty() && node.tag == "option") value = subtree_text(node);
            view.value = result->append_string(value);

            const auto has = [&](std::string_view attribute) {
                return node.attributes.find(std::string(attribute)) != node.attributes.end();
            };
            const auto state = [&](std::string_view attribute) {
                return lower_html_name(std::string(semantic_attribute(node, attribute)));
            };
            if (has("disabled") || state("aria-disabled") == "true")
                view.states |= WEBSCENE_SEMANTIC_NODE_DISABLED_V1;
            if (has("readonly") || state("aria-readonly") == "true")
                view.states |= WEBSCENE_SEMANTIC_NODE_READONLY_V1;
            if (has("required") || state("aria-required") == "true")
                view.states |= WEBSCENE_SEMANTIC_NODE_REQUIRED_V1;
            if (state("aria-invalid") != "" && state("aria-invalid") != "false")
                view.states |= WEBSCENE_SEMANTIC_NODE_INVALID_V1;
            if (state("aria-busy") == "true") view.states |= WEBSCENE_SEMANTIC_NODE_BUSY_V1;
            if (state("aria-modal") == "true") view.states |= WEBSCENE_SEMANTIC_NODE_MODAL_V1;
            if (state("aria-multiselectable") == "true")
                view.states |= WEBSCENE_SEMANTIC_NODE_MULTISELECTABLE_V1;
            if (state("aria-selected") == "true"
                || has("selected")
                || (node.has_form_control() && node.form_control().selectedness))
                view.states |= WEBSCENE_SEMANTIC_NODE_SELECTED_V1;
            const auto checked = state("aria-checked");
            const auto pressed = state("aria-pressed");
            if (checked == "mixed" || pressed == "mixed"
                || (node.has_form_control() && node.form_control().indeterminate))
                view.states |= WEBSCENE_SEMANTIC_NODE_MIXED_V1;
            if (checked == "true" || has("checked") || (node.has_form_control()
                && node.form_control().checkedness))
                view.states |= WEBSCENE_SEMANTIC_NODE_CHECKED_V1;
            if (pressed == "true") view.states |= WEBSCENE_SEMANTIC_NODE_PRESSED_V1;
            const auto expanded = state("aria-expanded");
            if (expanded == "true") view.states |= WEBSCENE_SEMANTIC_NODE_EXPANDED_V1;
            if (expanded == "false") view.states |= WEBSCENE_SEMANTIC_NODE_COLLAPSED_V1;
            if (&node == active_element && document_focused) {
                view.states |= WEBSCENE_SEMANTIC_NODE_FOCUSED_V1;
                result->focused_node_index = index;
            }
        }
        if (document_focused
            && result->focused_node_index == WEBSCENE_SEMANTIC_NONE_INDEX_V1) {
            for (const auto& semantic_document : result->documents) {
                if (semantic_document.frame_owner_dom_node_id
                    != focused_document_owner_id
                    || semantic_document.root_node_index
                        == WEBSCENE_SEMANTIC_NONE_INDEX_V1) continue;
                result->focused_node_index = semantic_document.root_node_index;
                result->nodes[result->focused_node_index].states |=
                    WEBSCENE_SEMANTIC_NODE_FOCUSED_V1;
                break;
            }
        }

        const std::array relationships{
            std::pair{"aria-labelledby", WEBSCENE_SEMANTIC_RELATION_LABELLED_BY_V1},
            std::pair{"aria-describedby", WEBSCENE_SEMANTIC_RELATION_DESCRIBED_BY_V1},
            std::pair{"aria-controls", WEBSCENE_SEMANTIC_RELATION_CONTROLS_V1},
            std::pair{"aria-owns", WEBSCENE_SEMANTIC_RELATION_OWNS_V1},
            std::pair{"aria-activedescendant", WEBSCENE_SEMANTIC_RELATION_ACTIVE_DESCENDANT_V1}};
        for (uint32_t source = 0U; source < captured.size(); ++source) {
            for (const auto& [attribute, kind] : relationships) {
                const auto tokens = semantic_attribute(*captured[source].node, attribute);
                size_t cursor = 0U;
                while (cursor < tokens.size()) {
                    while (cursor < tokens.size() && std::isspace(
                        static_cast<unsigned char>(tokens[cursor]))) ++cursor;
                    const auto begin = cursor;
                    while (cursor < tokens.size() && !std::isspace(
                        static_cast<unsigned char>(tokens[cursor]))) ++cursor;
                    if (begin == cursor) break;
                    const auto& local_ids = ids[captured[source].document_index];
                    const auto target = local_ids.find(
                        std::string(tokens.substr(begin, cursor - begin)));
                    if (target == local_ids.end()) continue;
                    if (result->relationships.size()
                        == maximum_semantic_relationships_v1) {
                        result->flags |=
                            WEBSCENE_SEMANTIC_SNAPSHOT_TRUNCATED_RELATIONSHIPS_V1;
                        break;
                    }
                    result->relationships.push_back({
                        sizeof(webscene_semantic_relationship_v1),
                        1U,
                        static_cast<uint32_t>(kind),
                        source,
                        target->second});
                }
            }
            if ((result->flags
                & WEBSCENE_SEMANTIC_SNAPSHOT_TRUNCATED_RELATIONSHIPS_V1) != 0U) break;
        }
        return result;
    }

    bool dispatch_semantic_activation(dom_node& node)
    {
        checkable_click_activation checkable_activation;
        auto form_control_changed = false;
        if (node.tag == "input") {
            if (!begin_checkable_click_activation(node, checkable_activation)) {
                form_control_changed = apply_non_checkable_form_control_click_default(node);
            }
        }
        webscene_input_event synthetic{};
        synthetic.kind = WEBSCENE_INPUT_POINTER_UP;
        synthetic.x = node.layout.x + node.layout.width / 2.0;
        synthetic.y = node.layout.y + node.layout.height / 2.0;
        bool default_prevented = false;
        if (!dispatch_input_event_type(
                synthetic, "click", node, &default_prevented, nullptr, false)) {
            return false;
        }
        if (checkable_activation.active) {
            if (default_prevented) {
                restore_checkable_click_activation(checkable_activation);
            } else {
                form_control_changed = checkable_activation.changed;
            }
        }
        if (!default_prevented && is_submit_button(&node)) {
            if(auto* form=form_owner(node);form!=nullptr
                && !dispatch_form_submit(synthetic,*form,&node)) return false;
        } else if (!default_prevented) {
            queue_external_navigation(node);
        }
        if (form_control_changed && is_connected(node)) {
            if (!dispatch_input_event_type(
                    synthetic, "input", node, nullptr, nullptr, false)
                || !dispatch_input_event_type(
                    synthetic, "change", node, nullptr, nullptr, false)) {
                return false;
            }
        }
        document.mark_dirty();
        return true;
    }

    bool dispatch_semantic_numeric_adjustment(dom_node& node, double direction)
    {
        const auto attribute_number = [&](std::string_view name) {
            return forms::finite_number(semantic_attribute(node, name));
        };
        const auto type = lower_html_name(
            std::string(semantic_attribute(node, "type")));
        auto minimum = attribute_number("min");
        auto maximum = attribute_number("max");
        if (type == "range") {
            if (!minimum) minimum = 0.0;
            if (!maximum) maximum = 100.0;
            if (*maximum < *minimum) maximum = minimum;
        }
        auto step = attribute_number("step").value_or(1.0);
        if (!(step > 0.0) || !std::isfinite(step)) step = 1.0;
        forms::ensure_text_value(node);
        auto current = forms::finite_number(node.form_control().value);
        if (!current) {
            current = type == "range"
                ? *minimum + (*maximum - *minimum) * 0.5
                : minimum.value_or(0.0);
        }
        auto next = *current + direction * step;
        if (minimum) next = std::max(next, *minimum);
        if (maximum) next = std::min(next, *maximum);
        std::array<char, 64U> buffer{};
        const auto formatted = std::to_chars(
            buffer.data(), buffer.data() + buffer.size(), next,
            std::chars_format::general);
        if (formatted.ec != std::errc{}) return false;
        const auto previous = node.form_control().value;
        node.mutable_form_control().value.assign(buffer.data(), formatted.ptr);
        node.mutable_form_control().value_initialized = true;
        node.mutable_form_control().dirty_value = true;
        recascade_form_value_selector_transition(
            node, previous, node.form_control().value, true, true);
        webscene_input_event synthetic{};
        if (!dispatch_input_event_type(synthetic, "input", node)
            || !dispatch_input_event_type(synthetic, "change", node)) {
            return false;
        }
        return true;
    }

    semantic_action_dispatch_result_v1 dispatch_semantic_action_v1(
        const semantic_action_request_data_v1& request)
    {
        if (document_generation.load(std::memory_order_acquire)
                != request.target.top_document_generation) {
            return semantic_action_dispatch_result_v1::discarded;
        }
        if (request.target.frame_owner_dom_node_id != 0U) {
            const auto frame = frame_navigation_generations.find(
                request.target.frame_owner_dom_node_id);
            if (frame == frame_navigation_generations.end()
                || frame->second != request.target.frame_generation) {
                return semantic_action_dispatch_result_v1::discarded;
            }
        }
        auto* node = document.find_by_native_id(request.target.dom_node_id);
        if (node == nullptr || !is_connected(*node)
            || focus_document_owner(node)
                != request.target.frame_owner_dom_node_id
            || semantic_identity(
                request.target.top_document_generation,
                request.target.frame_generation,
                request.target.frame_owner_dom_node_id,
                node->id) != request.target.semantic_id) {
            return semantic_action_dispatch_result_v1::discarded;
        }
        const auto action_bit = semantic_action_support_bit(request.action);
        if (action_bit == 0U) {
            return semantic_action_dispatch_result_v1::discarded;
        }

        auto isolate_locker = lock_shared_isolate();
        v8::Isolate::Scope isolate_scope(isolate);
        v8::HandleScope handle_scope(isolate);
        auto local_context = context_for_node(*node);
        if (local_context.IsEmpty()) {
            return semantic_action_dispatch_result_v1::discarded;
        }
        v8::Context::Scope context_scope(local_context);
        activate_css_cascade(local_context);
        ensure_layout();
        if ((semantic_supported_actions(*node) & action_bit) == 0U) {
            return semantic_action_dispatch_result_v1::discarded;
        }
        v8::TryCatch try_catch(isolate);
        webscene_input_event synthetic{};
        bool succeeded = true;
        switch (request.action) {
            case WEBSCENE_SEMANTIC_ACTION_FOCUS_V1:
                focus_visible = true;
                succeeded = set_active_element(node, synthetic);
                break;
            case WEBSCENE_SEMANTIC_ACTION_PRESS_V1:
            case WEBSCENE_SEMANTIC_ACTION_TOGGLE_V1:
                succeeded = dispatch_semantic_activation(*node);
                break;
            case WEBSCENE_SEMANTIC_ACTION_INCREMENT_V1:
                succeeded = dispatch_semantic_numeric_adjustment(*node, 1.0);
                break;
            case WEBSCENE_SEMANTIC_ACTION_DECREMENT_V1:
                succeeded = dispatch_semantic_numeric_adjustment(*node, -1.0);
                break;
            case WEBSCENE_SEMANTIC_ACTION_SET_VALUE_V1: {
                forms::ensure_text_value(*node);
                const auto previous = node->form_control().value;
                node->mutable_form_control().value = request.value;
                node->mutable_form_control().value_initialized = true;
                node->mutable_form_control().dirty_value = true;
                const auto caret = node->form_control().value.size();
                node->mutable_form_control().selection_start = caret;
                node->mutable_form_control().selection_end = caret;
                node->mutable_form_control().selection_start_utf16_suboffset = 0U;
                node->mutable_form_control().selection_end_utf16_suboffset = 0U;
                node->mutable_form_control().selection_direction =
                    text_selection_direction::none;
                node->mutable_form_control().selection_explicitly_set = true;
                recascade_form_value_selector_transition(
                    *node, previous, node->form_control().value, true, true);
                succeeded = dispatch_input_event_type(synthetic, "input", *node);
                break;
            }
            case WEBSCENE_SEMANTIC_ACTION_SET_SELECTION_V1: {
                forms::ensure_text_value(*node);
                const auto start = forms::utf8_selection_from_utf16_offset(
                    node->form_control().value, request.selection_start);
                const auto end = forms::utf8_selection_from_utf16_offset(
                    node->form_control().value, request.selection_end);
                node->mutable_form_control().selection_start = start.byte_offset;
                node->mutable_form_control().selection_start_utf16_suboffset =
                    start.utf16_suboffset;
                node->mutable_form_control().selection_end = end.byte_offset;
                node->mutable_form_control().selection_end_utf16_suboffset =
                    end.utf16_suboffset;
                node->mutable_form_control().selection_direction =
                    text_selection_direction::none;
                node->mutable_form_control().selection_explicitly_set = true;
                document.mark_dirty();
                break;
            }
            default:
                return semantic_action_dispatch_result_v1::discarded;
        }
        if (!succeeded) {
#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8_INSPECTOR)
            report_inspector_exception(try_catch, local_context);
#endif
            return semantic_action_dispatch_result_v1::failed;
        }
        perform_microtask_checkpoint();
        return semantic_action_dispatch_result_v1::performed;
    }
