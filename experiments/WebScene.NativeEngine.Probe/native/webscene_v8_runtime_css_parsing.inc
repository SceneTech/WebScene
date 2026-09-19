    static std::string_view trim_css_view(std::string_view value)
    {
        while (!value.empty() && is_css_whitespace(static_cast<unsigned char>(value.front()))) {
            value.remove_prefix(1U);
        }
        while (!value.empty() && is_css_whitespace(static_cast<unsigned char>(value.back()))) {
            value.remove_suffix(1U);
        }
        return value;
    }

    static std::string trim_css(std::string_view value)
    {
        return std::string(trim_css_view(value));
    }

    static size_t matching_css_brace(const std::string& css, size_t open, size_t end)
    {
        size_t depth = 1U;
        char quote = 0;
        for (size_t index = open + 1U; index < end; ++index) {
            const auto character = css[index];
            if (quote != 0) {
                if (character == quote && (index == 0 || css[index - 1U] != '\\')) quote = 0;
                continue;
            }
            if (character == '\'' || character == '"') quote = character;
            else if (character == '{') ++depth;
            else if (character == '}' && --depth == 0U) return index;
        }
        return std::string::npos;
    }

    static std::string copy_ascii_lower(std::string_view value)
    {
        auto result = std::string(value);
        for (auto& character : result) {
            if (character >= 'A' && character <= 'Z') {
                character = static_cast<char>(character + ('a' - 'A'));
            }
        }
        return result;
    }

    static void append_css_syntax_declaration(
        std::vector<css_declaration>& result, std::string_view name,
        std::string_view value, bool important,
        bool preserve_empty_custom_properties = false)
    {
        css::append_declaration(result,name,value,important,preserve_empty_custom_properties);
    }

    static std::vector<css_declaration> parse_css_declarations(
        std::string_view body,
        bool preserve_empty_custom_properties = false)
    {
        std::vector<css_declaration> result;
#if defined(WEBSCENE_NATIVE_ENGINE_CSSPARSER)
        result = css::parse_declarations(body, preserve_empty_custom_properties);
#else
        size_t cursor = 0;
        while (cursor < body.size()) {
            const auto separator = body.find(':', cursor);
            if (separator == std::string_view::npos) break;
            auto end = body.find(';', separator + 1U);
            if (end == std::string_view::npos) end = body.size();
            auto name = trim_css(body.substr(cursor, separator - cursor));
            auto value = trim_css(body.substr(separator + 1U, end - separator - 1U));
            auto lower_value = lower_html_name(value);
            const auto important_marker = lower_value.ends_with("!important")
                ? value.size() - std::string_view("!important").size()
                : std::string::npos;
            const auto important = important_marker != std::string::npos;
            if (important) {
                value = trim_css(std::string_view(value).substr(0, important_marker));
            }
            const auto custom = name.starts_with("--");
            if (custom && !valid_custom_property_name(name)) {
                cursor = end + 1U;
                continue;
            }
            if (!custom && name.starts_with("-")
                && name != "-moz-transform"
                && name != "-webkit-transform"
                && name != "-webkit-font-smoothing") {
                cursor = end + 1U;
                continue;
            }
            if (custom && value.empty() && preserve_empty_custom_properties) {
                value = " ";
            }
            if (!name.empty() && !value.empty()) {
                result.push_back({std::move(name), std::move(value), important});
            }
            cursor = end + 1U;
        }
#endif
        for (auto& declaration : result) {
            if (declaration.name.starts_with("--")) continue;
            const auto normalized = css::serialize_css_string_and_url_tokens(
                declaration.value);
            if (normalized.has_value()) declaration.value = *normalized;
        }
        return result;
    }

    static bool store_inline_declaration(
        dom_node& node,
        const css_declaration& declaration)
    {
        const auto existing_important =
            node.authored_style().important_declarations.contains(declaration.name);
        if (existing_important && !declaration.important) {
            return false;
        }

        auto& authored = node.mutable_authored_style();
        authored.declarations[declaration.name] = serialize_cssom_specified_value(
            declaration.name,
            declaration.value);
        authored.record_declaration_order(declaration.name);
        if (declaration.important) {
            authored.important_declarations.insert(declaration.name);
        } else {
            authored.important_declarations.erase(declaration.name);
        }
        return true;
    }

    static void strip_interaction_state_from_trigger(
        std::string& trigger,
        std::string_view pseudo)
    {
        const auto needle = std::string(":") + std::string(pseudo);
        // A functional pseudo containing the state changes truth with it,
        // but its other arguments are not prerequisites for invalidation.
        // Removing the complete function deliberately broadens the structural
        // trigger while retaining class/id/tag/attribute filtering.
        for (;;) {
            auto removed_function = false;
            for (size_t colon = 0; colon < trigger.size(); ++colon) {
                if (trigger[colon] != ':' || colon + 1U >= trigger.size()
                    || trigger[colon + 1U] == ':') {
                    continue;
                }
                auto open = colon + 1U;
                while (open < trigger.size()
                    && (std::isalnum(static_cast<unsigned char>(trigger[open]))
                        || trigger[open] == '-' || trigger[open] == '_')) {
                    ++open;
                }
                if (open >= trigger.size() || trigger[open] != '(') continue;
                auto depth = 1;
                char quote = 0;
                auto close = open + 1U;
                for (; close < trigger.size() && depth > 0; ++close) {
                    const auto character = trigger[close];
                    if (quote != 0) {
                        if (character == quote
                            && trigger[close - 1U] != '\\') {
                            quote = 0;
                        }
                        continue;
                    }
                    if (character == '\'' || character == '"') quote = character;
                    else if (character == '(') ++depth;
                    else if (character == ')') --depth;
                }
                if (depth != 0) break;
                if (trigger.find(needle, open + 1U) < close - 1U) {
                    trigger.erase(colon, close - colon);
                    removed_function = true;
                    break;
                }
                colon = close - 1U;
            }
            if (!removed_function) break;
        }
        for (size_t position = 0;
            (position = trigger.find(needle, position)) != std::string::npos;) {
            trigger.erase(position, needle.size());
        }
    }

    void index_interaction_selector_dependencies(
        const css_rule& rule,
        std::string_view pseudo,
        std::vector<hover_selector_dependency>& dependencies)
    {
        const auto& selector = rule.selector();
        const auto needle = std::string(":") + std::string(pseudo);
        size_t search = 0U;
        while ((search = selector.find(needle, search)) != std::string::npos) {
            const auto token_end = search + needle.size();
            if (token_end < selector.size()) {
                const auto next = selector[token_end];
                if (std::isalnum(static_cast<unsigned char>(next))
                    || next == '-' || next == '_') {
                    search = token_end;
                    continue;
                }
            }

            auto compound_begin = size_t{0};
            auto bracket_depth = 0;
            auto parenthesis_depth = 0;
            char quote = 0;
            for (size_t index = 0; index < search; ++index) {
                const auto character = selector[index];
                if (quote != 0) {
                    if (character == quote
                        && (index == 0U || selector[index - 1U] != '\\')) {
                        quote = 0;
                    }
                    continue;
                }
                if (character == '\\') {
                    index = skip_css_escape_sequence(selector, index) - 1U;
                    continue;
                }
                if (character == '\'' || character == '"') {
                    quote = character;
                    continue;
                }
                if (character == '[') ++bracket_depth;
                else if (character == ']') --bracket_depth;
                else if (character == '(') ++parenthesis_depth;
                else if (character == ')') --parenthesis_depth;
                else if (bracket_depth == 0 && parenthesis_depth == 0
                    && (std::isspace(static_cast<unsigned char>(character))
                        || character == '>' || character == '+'
                        || character == '~' || character == ',')) {
                    compound_begin = index + 1U;
                }
            }
            while (compound_begin < search
                && std::isspace(
                    static_cast<unsigned char>(selector[compound_begin]))) {
                ++compound_begin;
            }
            auto compound_end = compound_begin;
            bracket_depth = 0;
            parenthesis_depth = 0;
            quote = 0;
            for (; compound_end < selector.size(); ++compound_end) {
                const auto character = selector[compound_end];
                if (quote != 0) {
                    if (character == quote
                        && (compound_end == 0U
                            || selector[compound_end - 1U] != '\\')) {
                        quote = 0;
                    }
                    continue;
                }
                if (character == '\\') {
                    compound_end =
                        skip_css_escape_sequence(selector, compound_end) - 1U;
                    continue;
                }
                if (character == '\'' || character == '"') {
                    quote = character;
                    continue;
                }
                if (character == '[') ++bracket_depth;
                else if (character == ']') --bracket_depth;
                else if (character == '(') ++parenthesis_depth;
                else if (character == ')') --parenthesis_depth;
                else if (bracket_depth == 0 && parenthesis_depth == 0
                    && (std::isspace(static_cast<unsigned char>(character))
                        || character == '>' || character == '+'
                        || character == '~' || character == ',')) {
                    break;
                }
            }

            auto trigger = selector.substr(
                compound_begin,
                compound_end - compound_begin);
            strip_interaction_state_from_trigger(trigger, pseudo);
            if (const auto pseudo_element = trigger.find("::");
                pseudo_element != std::string::npos) {
                trigger.resize(pseudo_element);
            }
            if (trigger.ends_with(":before")) trigger.resize(trigger.size() - 7U);
            else if (trigger.ends_with(":after")) trigger.resize(trigger.size() - 6U);
            if (trigger.empty()) trigger = "*";

            auto scope = hover_invalidation_scope::subject;
            auto selector_arm_end = selector.size();
            bracket_depth = 0;
            parenthesis_depth = 0;
            quote = 0;
            for (auto position = compound_end; position < selector.size(); ++position) {
                const auto character = selector[position];
                if (quote != 0) {
                    if (character == quote
                        && (position == 0U || selector[position - 1U] != '\\')) {
                        quote = 0;
                    }
                    continue;
                }
                if (character == '\\') {
                    position = skip_css_escape_sequence(selector, position) - 1U;
                    continue;
                }
                if (character == '\'' || character == '"') {
                    quote = character;
                    continue;
                }
                if (character == '[') ++bracket_depth;
                else if (character == ']') --bracket_depth;
                else if (character == '(') ++parenthesis_depth;
                else if (character == ')') --parenthesis_depth;
                else if (character == ',' && bracket_depth == 0
                    && parenthesis_depth == 0) {
                    selector_arm_end = position;
                    break;
                }
                if (bracket_depth != 0 || parenthesis_depth != 0) continue;
                if (character == '+' || character == '~') {
                    scope = hover_invalidation_scope::siblings;
                } else if (character == '>'
                    || std::isspace(static_cast<unsigned char>(character))) {
                    if (scope != hover_invalidation_scope::siblings) {
                        scope = hover_invalidation_scope::subtree;
                    }
                }
            }

            auto affected_begin = compound_begin;
            auto affected_end = compound_end;
            auto affected_direct_children = false;
            if (scope != hover_invalidation_scope::subject) {
                bracket_depth = 0;
                parenthesis_depth = 0;
                quote = 0;
                affected_begin = compound_end;
                for (auto position = compound_end; position < selector_arm_end; ++position) {
                    const auto character = selector[position];
                    if (quote != 0) {
                        if (character == quote
                            && (position == 0U || selector[position - 1U] != '\\')) {
                            quote = 0;
                        }
                        continue;
                    }
                    if (character == '\\') {
                        position = skip_css_escape_sequence(selector, position) - 1U;
                        continue;
                    }
                    if (character == '\'' || character == '"') {
                        quote = character;
                        continue;
                    }
                    if (character == '[') ++bracket_depth;
                    else if (character == ']') --bracket_depth;
                    else if (character == '(') ++parenthesis_depth;
                    else if (character == ')') --parenthesis_depth;
                    else if (bracket_depth == 0 && parenthesis_depth == 0
                        && (std::isspace(static_cast<unsigned char>(character))
                            || character == '>' || character == '+'
                            || character == '~')) {
                        affected_begin = position + 1U;
                    }
                }
                while (affected_begin < selector_arm_end
                    && std::isspace(
                        static_cast<unsigned char>(selector[affected_begin]))) {
                    ++affected_begin;
                }
                auto combinator = affected_begin;
                while (combinator > compound_end
                    && std::isspace(static_cast<unsigned char>(
                        selector[combinator - 1U]))) {
                    --combinator;
                }
                affected_direct_children = combinator > compound_end
                    && selector[combinator - 1U] == '>';
                affected_end = selector_arm_end;
                while (affected_end > affected_begin
                    && std::isspace(
                        static_cast<unsigned char>(selector[affected_end - 1U]))) {
                    --affected_end;
                }
            }
            auto affected = selector.substr(
                affected_begin,
                affected_end - affected_begin);
            strip_interaction_state_from_trigger(affected, pseudo);
            if (const auto pseudo_element = affected.find("::");
                pseudo_element != std::string::npos) {
                affected.resize(pseudo_element);
            }
            if (affected.ends_with(":before")) affected.resize(affected.size() - 7U);
            else if (affected.ends_with(":after")) affected.resize(affected.size() - 6U);
            if (affected.empty()) affected = "*";
            const auto propagate_to_descendants = std::any_of(
                rule.declarations().begin(),
                rule.declarations().end(),
                [](const css_declaration& declaration) {
                    return declaration.name.starts_with("--");
                });
            if (scope == hover_invalidation_scope::subject
                && propagate_to_descendants) {
                scope = hover_invalidation_scope::subtree;
                affected = "*";
            }
            auto compiled_trigger = compile_css_compound_selector(trigger);
            dependencies.push_back({
                std::move(trigger),
                std::move(affected),
                std::move(compiled_trigger),
                scope,
                affected_direct_children,
                propagate_to_descendants});
            search = token_end;
        }
    }

    std::unordered_set<std::string> css_variable_references(
        std::string_view value) const
    {
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        ++css_variable_runtime_token_scans;
#endif
        const auto parsed = css::referenced_custom_properties(value);
        return {parsed.begin(), parsed.end()};
    }

    void index_css_rule(size_t index)
    {
#if defined(WEBSCENE_NATIVE_ENGINE_MEDIA_REFRESH_BENCHMARK_COUNTERS)
        media_refresh_index_rule_calls.fetch_add(1U, std::memory_order_relaxed);
#endif
        const auto& rule = css_rules[index];
        const auto& selector = rule.selector();
        if (selector.find("::selection") != std::string::npos) {
            return;
        }
        if (rule.payload->declaration_variable_references.size()
            == rule.declarations().size()) {
            for (const auto& reference : rule.payload->variable_references) {
                css_rules_by_variable_reference[reference].push_back(index);
            }
        } else {
            // Hand-authored/legacy prepared payloads may predate immutable
            // dependency metadata. Preserve correctness while generated and
            // parser-owned payloads stay on the allocation-free indexed path.
            std::unordered_set<std::string> references;
            for (const auto& declaration : rule.declarations()) {
                const auto parsed = css_variable_references(declaration.value);
                references.insert(parsed.begin(), parsed.end());
            }
            for (const auto& reference : references) {
                css_rules_by_variable_reference[reference].push_back(index);
            }
        }
        uint64_t inheritance_candidate_mask =
            rule.payload->inheritance_candidate_mask;
        if (rule.payload->declaration_variable_references.size()
            != rule.declarations().size()) {
            for (const auto& declaration : rule.declarations()) {
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
                ++css_inheritance_candidate_runtime_declaration_scans;
#endif
                if (declaration.name.starts_with("--")) continue;
                const auto mask = css::property_mask(declaration.name);
                if (mask != 0U
                    && (css::cascade_keyword_is(
                            declaration.value, "inherit")
                        || declaration.value.find("var(")
                            != std::string::npos)) {
                    inheritance_candidate_mask |= mask;
                }
            }
        }
        if (inheritance_candidate_mask != 0U) {
            css_inheritance_candidate_rules.push_back(
                {index, inheritance_candidate_mask});
        }
        index_interaction_selector_dependencies(
            rule, "hover", hover_selector_dependencies);
        index_interaction_selector_dependencies(
            rule, "active", active_selector_dependencies);
        const auto index_invalidation_rule = [&](auto& feature_index,
                                                  std::string_view feature) {
            auto& rules = feature_index[std::string(feature)];
            if (rules.empty() || rules.back() != index) rules.push_back(index);
        };
        css::index_child_list_rule(index,
            rule.payload->compiled_pseudo_origin.compounds.empty()
                ? rule.compiled_selector() : rule.payload->compiled_pseudo_origin,
            rule.payload->invalidation, css_child_list_index);
        for (const auto& component : rule.payload->invalidation) {
            for (const auto& [name, scope] : component.attributes) {
                static_cast<void>(scope);
                css_attribute_dependencies.emplace(name);
                index_invalidation_rule(css_invalidation_rules_by_attribute, name);
            }
            for (const auto& [name, scope] : component.classes) {
                static_cast<void>(scope);
                index_invalidation_rule(css_invalidation_rules_by_class, name);
            }
        }
        css::index_selector(index, selector,
            rule.payload->compiled_pseudo_origin.compounds.empty()
                ? rule.compiled_selector() : rule.payload->compiled_pseudo_origin,
            css_rules_by_id, css_rules_by_class,
            css_rules_by_tag, css_rules_by_attribute, css_focus_rules,
            unindexed_css_rules, css_descendant_attribute_dependencies);
    }

    bool attribute_requires_recascade(std::string_view name) const
    {
        if (name == "id" || name == "class" || name == "style"
            || name == "type" || name == "hidden" || name == "open") return true;
        return css_attribute_dependencies.contains(std::string(name));
    }

    bool attribute_may_affect_descendants(std::string_view name) const
    {
        return css_descendant_attribute_dependencies.contains(std::string(name));
    }

    bool media_query_matches(std::string query) const
    {
        const auto [width,height,scale,scheme]=viewport_provider();
        static_cast<void>(scale);
        return css::media_matches(std::move(query),
            {width,height,scheme==WEBSCENE_PREFERRED_COLOR_SCHEME_DARK});
    }

    bool css_rule_media_matches(const css_rule& rule) const
    {
        return std::all_of(
            rule.media_queries().begin(),
            rule.media_queries().end(),
            [this](const std::string& query) { return media_query_matches(query); });
    }

    static std::shared_ptr<const css_rule_payload> intern_css_rule_payload(
        std::string selector,
        const std::vector<css_declaration>& declarations,
        const std::vector<std::string>& media_queries,
        const selector_namespace_context* namespaces
#if defined(WEBSCENE_NATIVE_ENGINE_SERVO_SELECTORS)
        , const selector_syntax_selector* parsed_selector = nullptr
#endif
        )
    {
#if defined(WEBSCENE_NATIVE_ENGINE_SERVO_SELECTORS)
        static_cast<void>(parsed_selector);
#endif
        return css::intern_rule_payload(
            shared_css_rule_payload_mutex,shared_css_rule_payloads,
            [namespaces](std::string_view value) {
                return compile_css_selector(value, namespaces);
            },
            std::move(selector), declarations, media_queries, 0U,
            namespaces == nullptr ? std::string{} : namespaces->cache_key());
    }

    uint32_t register_cascade_layer(const std::string& name)
    {
        if (!name.empty() && active_stylesheet_layer_names != nullptr
            && std::find(
                active_stylesheet_layer_names->begin(),
                active_stylesheet_layer_names->end(),
                name) == active_stylesheet_layer_names->end()) {
            active_stylesheet_layer_names->push_back(name);
        }
        if (!name.empty()) {
            const auto key = cascade_layer_registration_key(
                name, active_stylesheet_owner_id);
            const auto known = css_cascade_layer_orders.find(key);
            if (known != css_cascade_layer_orders.end()) return known->second;
            const auto order = next_css_cascade_layer_order++;
            css_cascade_layer_orders.emplace(key, order);
            return order;
        }
        const auto order = next_css_cascade_layer_order++;
        return order;
    }

    void append_css_rule(
        std::string selector,
        const std::vector<css_declaration>& declarations,
        const std::vector<std::string>& media_queries,
        uint32_t cascade_layer_order,
        const selector_namespace_context* namespaces
#if defined(WEBSCENE_NATIVE_ENGINE_SERVO_SELECTORS)
        , const selector_syntax_selector* parsed_selector = nullptr
#endif
        )
    {
        const auto index = css_rules.size();
        css_rule rule;
        rule.stylesheet_owner_id = active_stylesheet_owner_id;
        rule.shadow_scope_root_id = active_stylesheet_shadow_scope_root_id;
        rule.payload = intern_css_rule_payload(
            std::move(selector),
            declarations,
            media_queries,
            namespaces
#if defined(WEBSCENE_NATIVE_ENGINE_SERVO_SELECTORS)
            , parsed_selector
#endif
            );
        rule.media_matches = css_rule_media_matches(rule);
        rule.cascade_layer_order = cascade_layer_order;
        css_rules.push_back(std::move(rule));
        index_css_rule(index);
    }

    void inventory_css_length_units(
        std::string_view value,
        std::string_view source)
    {
        char quote = 0;
        for (size_t index = 0; index < value.size();) {
            if (quote != 0) {
                if (value[index] == quote
                    && (index == 0U || value[index - 1U] != '\\')) quote = 0;
                ++index;
                continue;
            }
            if (value[index] == '\'' || value[index] == '"') {
                quote = value[index++];
                continue;
            }
            if (!std::isdigit(static_cast<unsigned char>(value[index]))
                && !(value[index] == '.' && index + 1U < value.size()
                    && std::isdigit(static_cast<unsigned char>(value[index + 1U])))) {
                ++index;
                continue;
            }
            if (index > 0U) {
                const auto previous = value[index - 1U];
                if (std::isalnum(static_cast<unsigned char>(previous))
                    || previous == '_' || previous == '#') {
                    ++index;
                    continue;
                }
            }
            while (index < value.size()
                && (std::isdigit(static_cast<unsigned char>(value[index]))
                    || value[index] == '.')) ++index;
            const auto unit_start = index;
            while (index < value.size()
                && std::isalpha(static_cast<unsigned char>(value[index]))) ++index;
            if (unit_start == index) continue;
            const auto unit = lower_html_name(
                std::string(value.substr(unit_start, index - unit_start)));
            if (unit != "em" && unit != "rem"
                && unit != "vw" && unit != "vh"
                && unit != "vmin" && unit != "vmax"
                && unit != "ch" && unit != "ex"
                && unit != "cm" && unit != "mm"
                && unit != "in" && unit != "pt"
                && unit != "pc" && unit != "q") {
                continue;
            }
            record_feature(
                "css",
                "length-unit:" + unit,
                unit == "em" || unit == "rem" || unit == "vw" || unit == "vh"
                    ? "partially-supported" : "unsupported",
                unit == "em" || unit == "rem"
                    ? "default 16px root metric"
                    : unit == "vw" || unit == "vh"
                        ? "top-level logical viewport dimensions"
                        : "the native length parser treats this numeric dimension as px",
                std::string(source));
        }
    }

    void inventory_css_value_functions(
        std::string_view value,
        std::string_view source = "stylesheet-parser")
    {
        inventory_css_length_units(value, source);
        char quote = 0;
        for (size_t index = 0; index < value.size();) {
            const auto character = value[index];
            if (quote != 0) {
                if (character == quote && (index == 0U || value[index - 1U] != '\\')) {
                    quote = 0;
                }
                ++index;
                continue;
            }
            if (character == '\'' || character == '"') {
                quote = character;
                ++index;
                continue;
            }
            if (!std::isalpha(static_cast<unsigned char>(character))
                && character != '-') {
                ++index;
                continue;
            }
            const auto name_start = index++;
            while (index < value.size()
                && (std::isalnum(static_cast<unsigned char>(value[index]))
                    || value[index] == '-')) ++index;
            auto after_name = index;
            while (after_name < value.size()
                && std::isspace(static_cast<unsigned char>(value[after_name]))) ++after_name;
            if (after_name >= value.size() || value[after_name] != '(') continue;

            auto name = lower_html_name(std::string(value.substr(name_start, index - name_start)));
            if (std::none_of(name.begin(), name.end(), [](unsigned char character) {
                    return std::isalpha(character);
                })) {
                continue;
            }
            auto classification = std::string("unsupported");
            auto semantic_slice = std::string{};
            if (name == "var") {
                classification = "partially-supported";
                semantic_slice = "inherited custom-property substitution with fallback; no registered typed properties";
            } else if (name == "calc" || name == "min" || name == "max") {
                classification = "partially-supported";
                semantic_slice = "length arithmetic over px, percent, and unitless zero";
            } else if (name == "rgb" || name == "rgba") {
                classification = "partially-supported";
                semantic_slice = "legacy comma-separated integer RGB channels and numeric alpha";
            } else if (name == "translate" || name == "translatex" || name == "translatey"
                || name == "scale" || name == "scalex" || name == "scaley"
                || name == "rotate") {
                classification = "partially-supported";
                semantic_slice = "one occurrence per 2D transform function in the native fixed transform projection";
            } else if (name == "cubic-bezier") {
                classification = "partially-supported";
                semantic_slice = "transition timing functions with four numeric arguments";
            } else if (name == "steps") {
                classification = "supported";
                semantic_slice = "keyframe animation timing with positive integer count and jump positions";
            } else if (name == "url") {
                classification = "partially-supported";
                semantic_slice = "first URL-backed SVG CSS background layer";
            }
            record_feature(
                "css",
                "function:" + name,
                std::move(classification),
                std::move(semantic_slice),
                std::string(source));
        }
    }

    void inventory_css_selector(std::string_view selector)
    {
        auto record_selector_feature = [this](
                                           std::string feature,
                                           std::string classification = "supported",
                                           std::string semantic_slice = {}) {
            record_feature(
                "css",
                std::move(feature),
                std::move(classification),
                std::move(semantic_slice),
                "selector-parser");
        };

        bool saw_type = false;
        bool saw_class = false;
        bool saw_id = false;
        bool saw_universal = false;
        int bracket_depth = 0;
        int parenthesis_depth = 0;
        char quote = 0;
        for (size_t index = 0; index < selector.size(); ++index) {
            const auto character = selector[index];
            if (quote != 0) {
                if (character == quote && (index == 0U || selector[index - 1U] != '\\')) {
                    quote = 0;
                }
                continue;
            }
            if (character == '\'' || character == '"') {
                quote = character;
                continue;
            }
            if (character == '[') {
                ++bracket_depth;
                const auto close = selector.find(']', index + 1U);
                if (close == std::string_view::npos) {
                    record_selector_feature(
                        "selector:attribute",
                        "invalid-authoring",
                        "unterminated attribute selector");
                    break;
                }
                const auto condition = selector.substr(index + 1U, close - index - 1U);
                const auto operator_index = condition.find_first_of("~|^$*=");
                if (operator_index == std::string_view::npos) {
                    record_selector_feature("selector:attribute-presence");
                } else {
                    auto operator_name = std::string(1U, condition[operator_index]);
                    if (condition[operator_index] != '='
                        && operator_index + 1U < condition.size()
                        && condition[operator_index + 1U] == '=') {
                        operator_name.push_back('=');
                    }
                    if (operator_name == "=") {
                        record_selector_feature("selector:attribute-equals");
                    } else if (operator_name == "~=" || operator_name == "|="
                        || operator_name == "^=" || operator_name == "$="
                        || operator_name == "*=") {
                        record_selector_feature(
                            "selector:attribute-operator:" + operator_name,
                            "partially-supported",
                            "case-sensitive matching without selector flags");
                    } else {
                        record_selector_feature(
                            "selector:attribute-operator:" + operator_name,
                            "unsupported");
                    }
                }
                index = close;
                --bracket_depth;
                continue;
            }
            if (character == '(') {
                ++parenthesis_depth;
                continue;
            }
            if (character == ')') {
                if (parenthesis_depth > 0) --parenthesis_depth;
                continue;
            }
            if (bracket_depth != 0) continue;
            if (character == ':') {
                const auto pseudo_element_marker = index + 1U < selector.size()
                    && selector[index + 1U] == ':';
                if (pseudo_element_marker) ++index;
                const auto name_start = index + 1U;
                auto name_end = name_start;
                while (name_end < selector.size()
                    && (std::isalnum(static_cast<unsigned char>(selector[name_end]))
                        || selector[name_end] == '-')) ++name_end;
                auto name = lower_html_name(std::string(
                    selector.substr(name_start, name_end - name_start)));
                const auto pseudo_element = pseudo_element_marker
                    || name == "before" || name == "after"
                    || name == "first-letter" || name == "first-line";
                if (pseudo_element) {
                    record_selector_feature(
                        "selector:pseudo-element:" + name,
                        name == "before" || name == "after" ? "supported" : "unsupported");
                } else {
                    static const std::unordered_set<std::string> supported_pseudo_classes{
                        "root", "first-child", "last-child", "only-child",
                        "first-of-type", "last-of-type", "only-of-type", "empty",
                        "enabled", "disabled", "checked", "hover", "focus", "focus-visible",
                        "link", "any-link"};
                    if (name == "not" || name == "is" || name == "where") {
                        record_selector_feature(
                            "selector:pseudo-class:" + name,
                            "partially-supported",
                            "comma-separated compound-selector arguments");
                    } else if (name == "nth-child") {
                        record_selector_feature(
                            "selector:pseudo-class:nth-child",
                            "partially-supported",
                            "integer, odd, even, and An+B forms without an of-selector");
                    } else if (name == "state") {
                        record_selector_feature("selector:pseudo-class:state");
                    } else {
                        record_selector_feature(
                            "selector:pseudo-class:" + name,
                            supported_pseudo_classes.contains(name) ? "supported" : "unsupported");
                    }
                }
                index = name_end == 0U ? index : name_end - 1U;
                continue;
            }
            if (parenthesis_depth != 0) continue;
            if (character == '>') {
                record_selector_feature("selector:combinator:child");
            } else if (character == '+') {
                record_selector_feature("selector:combinator:adjacent-sibling");
            } else if (character == '~') {
                record_selector_feature("selector:combinator:general-sibling");
            } else if (character == '#') {
                saw_id = true;
            } else if (character == '.') {
                saw_class = true;
            } else if (character == '*') {
                saw_universal = true;
            } else if (character == '|' || character == '\\') {
                record_selector_feature(
                    character == '|' ? "selector:namespace" : "selector:escaped-identifier",
                    "unsupported");
            } else if (std::isalpha(static_cast<unsigned char>(character))) {
                const auto previous = index == 0U ? ' ' : selector[index - 1U];
                if (index == 0U || std::isspace(static_cast<unsigned char>(previous))
                    || previous == '>' || previous == '+' || previous == '~' || previous == ',') {
                    saw_type = true;
                }
            } else if (std::isspace(static_cast<unsigned char>(character))) {
                auto next = index + 1U;
                while (next < selector.size()
                    && std::isspace(static_cast<unsigned char>(selector[next]))) ++next;
                auto previous = index;
                while (previous > 0U
                    && std::isspace(static_cast<unsigned char>(selector[previous - 1U]))) --previous;
                if (previous > 0U && next < selector.size()
                    && selector[previous - 1U] != '>' && selector[previous - 1U] != '+'
                    && selector[previous - 1U] != '~' && selector[next] != '>'
                    && selector[next] != '+' && selector[next] != '~') {
                    record_selector_feature("selector:combinator:descendant");
                }
            }
        }
        if (saw_type) record_selector_feature("selector:type");
        if (saw_class) record_selector_feature("selector:class");
        if (saw_id) record_selector_feature("selector:id");
        if (saw_universal) record_selector_feature("selector:universal");
    }

    bool inventory_media_query(std::string query)
    {
        return css::inventory_media(std::move(query),
            [this](const auto& category,const auto& feature,const auto& classification,
                const std::string& detail,const auto& source) {
                record_feature(category,feature,classification,detail,source);
            });
    }

    static uint32_t css_selector_specificity(std::string_view selector)
    {
        // Pack the CSS (ID, class/attribute/pseudo-class, type/pseudo-element)
        // tuple so ordinary integer ordering retains the cascade's lexicographic order.
        uint32_t ids = 0;
        uint32_t classes = 0;
        uint32_t elements = 0;
        bool compound_start = true;
        for (size_t index = 0; index < selector.size();) {
            const auto character = selector[index];
            if (std::isspace(static_cast<unsigned char>(character))
                || character == '>' || character == '+' || character == '~') {
                compound_start = true;
                ++index;
                continue;
            }
            if (character == '#') {
                ++ids;
                ++index;
                while (index < selector.size()
                    && (std::isalnum(static_cast<unsigned char>(selector[index]))
                        || selector[index] == '-' || selector[index] == '_')) ++index;
                compound_start = false;
                continue;
            }
            if (character == '.') {
                ++classes;
                ++index;
                while (index < selector.size()
                    && (std::isalnum(static_cast<unsigned char>(selector[index]))
                        || selector[index] == '-' || selector[index] == '_')) ++index;
                compound_start = false;
                continue;
            }
            if (character == '[') {
                ++classes;
                char quote = 0;
                for (++index; index < selector.size(); ++index) {
                    if (quote != 0) {
                        if (selector[index] == quote) quote = 0;
                    } else if (selector[index] == '\'' || selector[index] == '"') {
                        quote = selector[index];
                    } else if (selector[index] == ']') {
                        ++index;
                        break;
                    }
                }
                compound_start = false;
                continue;
            }
            if (character == ':') {
                auto pseudo_element = index + 1U < selector.size()
                    && selector[index + 1U] == ':';
                if (pseudo_element) {
                    index += 2U;
                } else {
                    ++index;
                }
                const auto name_start = index;
                while (index < selector.size()
                    && (std::isalnum(static_cast<unsigned char>(selector[index]))
                        || selector[index] == '-')) ++index;
                std::string name(selector.substr(name_start, index - name_start));
                std::transform(name.begin(), name.end(), name.begin(), [](unsigned char value) {
                    return static_cast<char>(std::tolower(value));
                });
                pseudo_element = pseudo_element || name == "before" || name == "after";
                if (pseudo_element) ++elements;
                else if (name != "where") ++classes;
                if (index < selector.size() && selector[index] == '(') {
                    int depth = 1;
                    for (++index; index < selector.size() && depth > 0; ++index) {
                        if (selector[index] == '(') ++depth;
                        else if (selector[index] == ')') --depth;
                    }
                }
                compound_start = false;
                continue;
            }
            if (compound_start
                && std::isalpha(static_cast<unsigned char>(character))) {
                ++elements;
                while (index < selector.size()
                    && (std::isalnum(static_cast<unsigned char>(selector[index]))
                        || selector[index] == '-')) ++index;
                compound_start = false;
                continue;
            }
            compound_start = false;
            ++index;
        }
        return std::min(ids, 0xffU) << 16U
            | std::min(classes, 0xffU) << 8U
            | std::min(elements, 0xffU);
    }

    void sort_css_candidates(std::vector<size_t>& candidates) const
    { css::sort_candidates(css_rules,candidates); }

    bool refresh_css_media_matches(std::vector<size_t>* changed_rules = nullptr)
    {
        auto changed = false;
        for (size_t index = 0; index < css_rules.size(); ++index) {
            auto& rule = css_rules[index];
            const auto matches = css_rule_media_matches(rule);
            if (matches != rule.media_matches) {
                changed = true;
                if (changed_rules != nullptr) changed_rules->push_back(index);
            }
            rule.media_matches = matches;
        }
        return changed;
    }

    bool refresh_media_environment()
    {
        auto isolate_locker = lock_shared_isolate();
        v8::Isolate::Scope isolate_scope(isolate);
        v8::HandleScope handle_scope(isolate);
        auto changed = false;
        const auto refresh_context = [&](v8::Local<v8::Context> local_context) {
            if (local_context.IsEmpty()) return;
            activate_css_cascade(local_context);
            v8::Context::Scope context_scope(local_context);
            std::vector<size_t> changed_rules;
            if (!refresh_css_media_matches(&changed_rules)) return;
            changed = true;
            rebuild_css_rule_indexes_and_root_variables();
            recascade_media_query_changes(active_root(), changed_rules);
        };
        refresh_context(context.Get(isolate));
        for (auto& [id, persistent_context] : actual_frame_contexts) {
            static_cast<void>(id);
            if (!persistent_context.IsEmpty()) {
                refresh_context(persistent_context.Get(isolate));
            }
        }
        activate_css_cascade(context.Get(isolate));
        if (changed) document.mark_dirty();
        return true;
    }

    static std::string resolve_css_resource_urls(std::string value,const std::string& address)
    { return css::resolve_resource_urls(std::move(value),address); }

    void parse_opacity_keyframes(
        const std::string& css,
        std::string name,
        size_t begin,
        size_t end)
    {
        css_opacity_keyframes definition;
        size_t cursor = begin;
        while (cursor < end) {
            const auto open = css.find('{', cursor);
            if (open == std::string::npos || open >= end) break;
            const auto close = matching_css_brace(css, open, end);
            if (close == std::string::npos) break;
            const auto selector = trim_css(
                std::string_view(css).substr(cursor, open - cursor));
            const auto declarations = parse_css_declarations(
                std::string_view(css).substr(open + 1U, close - open - 1U));
            css::append_keyframe(definition, selector, declarations);
            cursor = close + 1U;
        }
        css::finish_keyframes(
            opacity_keyframes, trim_css(std::move(name)), std::move(definition));
    }

    void append_parsed_css_style_rule(
        std::string prelude,
        std::vector<css_declaration> declarations,
        const std::vector<std::string>& inherited_media,
        const std::string& stylesheet_address,
        uint32_t cascade_layer_order = 0U,
        const selector_namespace_context& namespaces = {})
    {
#if defined(WEBSCENE_NATIVE_ENGINE_SERVO_SELECTORS)
        css::prepare_style_rule(prelude,std::move(declarations),inherited_media,stylesheet_address,
            [this](const css_declaration& declaration) {
                inventory_css_value_functions(declaration.value);
                if(declaration.name == "background-image"
                    || declaration.name == "mask-image") {
                    if(const auto url=first_css_url(declaration.value);url.has_value())
                        if (!has_claimed_service_worker_for_url(
                                current_navigation_address()))
                            start_resource_prefetch(*url,{},WEBSCENE_RESOURCE_IMAGE);
                }
            },
            [this, cascade_layer_order, &namespaces](const std::string& selector,const std::vector<css_declaration>& values,
                   const std::vector<std::string>& media) {
                inventory_css_selector(selector);
                append_css_rule(selector, values, media, cascade_layer_order, &namespaces);
            }, &namespaces);
#else
        for (auto& declaration : declarations) {
            declaration.value = resolve_css_resource_urls(
                std::move(declaration.value),
                stylesheet_address);
            inventory_css_value_functions(declaration.value);
            if (declaration.name == "background-image"
                || declaration.name == "mask-image") {
                if (const auto url = first_css_url(declaration.value); url.has_value()) {
                    if (!has_claimed_service_worker_for_url(
                            current_navigation_address())) {
                        start_resource_prefetch(*url, {}, WEBSCENE_RESOURCE_IMAGE);
                    }
                }
            }
        }
        // The shared selector parser intentionally implements document-tree
        // selectors and rejects Shadow DOM-only pseudo classes.  Preserve the
        // production milestone's exact `:host` form here; cascade scope checks
        // match it only against the owning host, so it never enters ordinary
        // selector matching or leaks into the document tree.
        if (trim_css_view(prelude) == ":host") {
            inventory_css_selector(":host");
            append_css_rule(
                ":host", declarations, inherited_media, cascade_layer_order, &namespaces);
            return;
        }
        size_t selector_cursor = 0U;
        int bracket_depth = 0;
        int parenthesis_depth = 0;
        char quote = 0;
        for (size_t index = 0U; index <= prelude.size(); ++index) {
            const auto at_end = index == prelude.size();
            const auto character = at_end ? ',' : prelude[index];
            if (!at_end && quote != 0) {
                if (character == quote
                    && (index == 0U || prelude[index - 1U] != '\\')) quote = 0;
                continue;
            }
            if (!at_end && (character == '\'' || character == '"')) {
                quote = character;
                continue;
            }
            if (!at_end && character == '[') ++bracket_depth;
            else if (!at_end && character == ']') --bracket_depth;
            else if (!at_end && character == '(') ++parenthesis_depth;
            else if (!at_end && character == ')') --parenthesis_depth;
            else if (character == ',' && bracket_depth == 0 && parenthesis_depth == 0) {
                auto selector = trim_css(std::string_view(prelude).substr(
                    selector_cursor, index - selector_cursor));
                if (!selector.empty()) {
                    inventory_css_selector(selector);
                    append_css_rule(
                        std::move(selector), declarations, inherited_media,
                        cascade_layer_order, &namespaces);
                }
                selector_cursor = index + 1U;
            }
        }

#endif
    }

    void parse_css_rules(
        const std::string& css,
        size_t begin,
        size_t end,
        const std::vector<std::string>& inherited_media = {},
        const std::string& stylesheet_address = {},
        const std::string& inherited_layer_path = {},
        uint32_t inherited_layer_order = 0U)
    {
        const auto qualify_layer = [&](std::string_view local_name) {
            const auto local = trim_css(local_name);
            if (local.empty() || inherited_layer_path.empty()) return local;
            return inherited_layer_path + "." + local;
        };
        const auto register_layer_statement = [&](std::string_view prelude) {
            size_t start = 0U;
            while (start <= prelude.size()) {
                auto comma = prelude.find(',', start);
                if (comma == std::string_view::npos) comma = prelude.size();
                const auto name = qualify_layer(
                    prelude.substr(start, comma - start));
                if (!name.empty()) register_cascade_layer(name);
                if (comma == prelude.size()) break;
                start = comma + 1U;
            }
            record_feature(
                "css", "at-rule:@layer", "supported",
                "named, anonymous, statement, nested, normal and important author layers",
                "stylesheet-parser");
        };
        const auto next_boundary = [&](size_t start) {
            char quote = 0;
            int bracket_depth = 0;
            int parenthesis_depth = 0;
            for (size_t index = start; index < end; ++index) {
                const auto character = css[index];
                if (quote != 0) {
                    if (character == quote
                        && (index == start || css[index - 1U] != '\\')) quote = 0;
                    continue;
                }
                if (character == '\'' || character == '"') {
                    quote = character;
                    continue;
                }
                if (character == '[') ++bracket_depth;
                else if (character == ']') --bracket_depth;
                else if (character == '(') ++parenthesis_depth;
                else if (character == ')') --parenthesis_depth;
                else if (bracket_depth == 0 && parenthesis_depth == 0
                    && (character == ';' || character == '{')) return index;
            }
            return std::string::npos;
        };
        size_t cursor = begin;
        while (cursor < end) {
            const auto boundary = next_boundary(cursor);
            if (boundary == std::string::npos || boundary >= end) break;
            if (css[boundary] == ';') {
                const auto statement = trim_css(
                    std::string_view(css).substr(cursor, boundary - cursor));
                if (statement.starts_with("@layer")) {
                    register_layer_statement(
                        std::string_view(statement).substr(6U));
                } else if (!statement.empty() && statement.front() == '@') {
                    auto name_end = statement.find_first_of(" (\t\r\n");
                    if (name_end == std::string::npos) name_end = statement.size();
                    record_feature(
                        "css",
                        "at-rule:" + lower_html_name(statement.substr(0U, name_end)),
                        "unsupported", {}, "stylesheet-parser");
                }
                cursor = boundary + 1U;
                continue;
            }
            const auto open = boundary;
            const auto close = matching_css_brace(css, open, end);
            if (close == std::string::npos) break;
            const auto prelude = trim_css(std::string_view(css).substr(cursor, open - cursor));
            if (prelude.starts_with("@keyframes")
                || prelude.starts_with("@-webkit-keyframes")) {
                const auto prefix = prelude.starts_with("@keyframes")
                    ? std::string_view("@keyframes")
                    : std::string_view("@-webkit-keyframes");
                auto name = trim_css(std::string_view(prelude).substr(prefix.size()));
                parse_opacity_keyframes(css, name, open + 1U, close);
                record_feature(
                    "css",
                    "at-rule:@keyframes",
                    "partially-supported",
                    "opacity, rotate(), and bounded filter keyframes with host-clock timing",
                    "stylesheet-parser");
            } else if (prelude.starts_with("@font-face")) {
                record_feature(
                    "css",
                    "at-rule:@font-face",
                    "supported",
                    "font-family and first src url are registered by the native host",
                    "stylesheet-parser");
            } else if (prelude.starts_with("@media")) {
                auto nested_media = inherited_media;
                auto query = trim_css(std::string_view(prelude).substr(6U));
                const auto supported = inventory_media_query(query);
                record_feature(
                    "css",
                    "at-rule:@media",
                    supported ? "supported" : "unsupported",
                    supported ? std::string{} : "unsupported media type or condition",
                    "stylesheet-parser");
                nested_media.push_back(std::move(query));
                parse_css_rules(css, open + 1U, close, nested_media,
                    stylesheet_address, inherited_layer_path, inherited_layer_order);
            } else if (prelude.starts_with("@supports")) {
                auto condition = trim_css(std::string_view(prelude).substr(9U));
                auto negated = false;
                if (condition.starts_with("not ")) {
                    negated = true;
                    condition = trim_css(std::string_view(condition).substr(4U));
                }
                // This bounded runtime only advertises selector() forms whose
                // state is implemented by its selector engine. Unknown support
                // conditions remain false instead of accidentally enabling a
                // fallback block.
                const auto supported = condition == "selector(:focus-visible)";
                record_feature(
                    "css",
                    "at-rule:@supports",
                    supported ? "supported" : "unsupported",
                    supported ? "selector(:focus-visible)" : "condition is not evaluated",
                    "stylesheet-parser");
                if (supported != negated) {
                    parse_css_rules(css, open + 1U, close, inherited_media,
                        stylesheet_address, inherited_layer_path, inherited_layer_order);
                }
            } else if (prelude.starts_with("@layer")) {
                const auto local_name = trim_css(
                    std::string_view(prelude).substr(6U));
                auto layer_path = qualify_layer(local_name);
                const auto layer_order = register_cascade_layer(layer_path);
                if (layer_path.empty()) {
                    // Preserve a unique internal scope for named sublayers of
                    // an anonymous layer without turning that anonymous layer
                    // into a globally reusable name.
                    layer_path = inherited_layer_path;
                    if (!layer_path.empty()) layer_path += ".";
                    layer_path += "\x1f" "anonymous-" + std::to_string(layer_order);
                }
                record_feature(
                    "css",
                    "at-rule:@layer",
                    "supported",
                    "named, anonymous, statement, nested, normal and important author layers",
                    "stylesheet-parser");
                parse_css_rules(css, open + 1U, close, inherited_media,
                    stylesheet_address, layer_path, layer_order);
            } else if (prelude.starts_with("@container")) {
                record_feature(
                    "css",
                    "at-rule:@container",
                    "unsupported",
                    "container conditions are not evaluated",
                    "stylesheet-parser");
                // Unsupported conditional rules must be ignored, not flattened
                // into the unconditional cascade (which enables every breakpoint).
            } else if (!prelude.empty() && prelude.front() != '@') {
                auto declarations = parse_css_declarations(
                    std::string_view(css).substr(open + 1U, close - open - 1U));
                append_parsed_css_style_rule(
                    prelude,
                    std::move(declarations),
                    inherited_media,
                    stylesheet_address,
                    inherited_layer_order);
            } else if (!prelude.empty() && prelude.front() == '@') {
                auto name_end = prelude.find_first_of(" (\t\r\n");
                if (name_end == std::string::npos) name_end = prelude.size();
                record_feature(
                    "css",
                    "at-rule:" + lower_html_name(prelude.substr(0U, name_end)),
                    "unsupported",
                    {},
                    "stylesheet-parser");
            }
            cursor = close + 1U;
        }
    }

#if defined(WEBSCENE_NATIVE_ENGINE_CSSPARSER)
    void finish_css_syntax_keyframes(std::string name, css_opacity_keyframes definition)
    { css::finish_keyframes(opacity_keyframes,std::move(name),std::move(definition)); }

    void parse_css_rules_with_cssparser(
        std::string_view css,
        const std::string& stylesheet_address)
    {

        const auto first_new_rule = css_rules.size();
        css::stylesheet_sink sink(*this, stylesheet_address, css.size());
        const auto parsed = stream_css_syntax_stylesheet(css, sink);
        if (!parsed || !sink.complete()) {
            css_rules.resize(first_new_rule);
            return;
        }
        for (auto& [name, definition] : sink.keyframes()) {
            finish_css_syntax_keyframes(std::move(name), std::move(definition));
            record_feature(
                "css", "at-rule:@keyframes", "partially-supported",
                "opacity, rotate(), and bounded filter keyframes with host-clock timing",
                "stylesheet-parser");
        }
    }
#endif

    void add_prepared_stylesheet(const css::prepared_stylesheet& sheet)
    {
        std::vector<uint32_t> layer_orders(sheet.cascade_layers.size() + 1U, 0U);
        for (size_t index = 0; index < sheet.cascade_layers.size(); ++index) {
            layer_orders[index + 1U] = register_cascade_layer(
                sheet.cascade_layers[index]);
        }
        for (const auto& payload : sheet.rules) {
            css_rule rule;
            rule.payload = payload;
            rule.media_matches = css_rule_media_matches(rule);
            if (payload->cascade_layer_index < layer_orders.size()) {
                rule.cascade_layer_order =
                    layer_orders[payload->cascade_layer_index];
            }
            const auto index = css_rules.size();
            css_rules.push_back(std::move(rule));
            index_css_rule(index);
        }
        // A prepared sheet can introduce an earlier named layer after later
        // sheets are already connected. Rebuild from the complete rule set so
        // document-global layer order, importance and rollback all participate.
        css::rebuild_root_variables(
            css_rules, css_variables, important_css_variables);
        for (const auto& [name, frames] : sheet.keyframes) opacity_keyframes[name] = frames;
        for (const auto& diagnostic : sheet.diagnostics)
            record_feature("css", diagnostic.feature, diagnostic.classification,
                diagnostic.detail, "compiled-stylesheet");
    }

    std::string expand_stylesheet_imports(
        std::string source,
        const std::string& stylesheet_address)
    {
        constexpr auto maximum_depth = size_t{16U};
        constexpr auto maximum_imports = size_t{1024U};
        constexpr auto maximum_bytes = size_t{32U * 1024U * 1024U};
        size_t import_count = 0U;
        size_t loaded_bytes = source.size();
        std::unordered_set<std::string> ancestors;
        if (!stylesheet_address.empty()) ancestors.insert(stylesheet_address);

        const auto trim = [](std::string_view value) {
            const auto first = value.find_first_not_of(" \t\n\f\r");
            if (first == std::string_view::npos) return std::string{};
            const auto last = value.find_last_not_of(" \t\n\f\r");
            return std::string(value.substr(first, last - first + 1U));
        };
        const auto keyword_at = [](std::string_view value, size_t offset,
                                   std::string_view keyword) {
            if (offset + keyword.size() > value.size()) return false;
            for (size_t index = 0U; index < keyword.size(); ++index) {
                if (std::tolower(static_cast<unsigned char>(value[offset + index]))
                    != std::tolower(static_cast<unsigned char>(keyword[index]))) {
                    return false;
                }
            }
            const auto end = offset + keyword.size();
            return end == value.size()
                || !std::isalnum(static_cast<unsigned char>(value[end]));
        };
        const auto statement_end = [](std::string_view value, size_t start) {
            auto parentheses = 0;
            char quote = 0;
            auto comment = false;
            for (auto index = start; index < value.size(); ++index) {
                const auto character = value[index];
                const auto next = index + 1U < value.size() ? value[index + 1U] : 0;
                if (comment) {
                    if (character == '*' && next == '/') {
                        comment = false;
                        ++index;
                    }
                    continue;
                }
                if (quote != 0) {
                    if (character == '\\') ++index;
                    else if (character == quote) quote = 0;
                    continue;
                }
                if (character == '/' && next == '*') {
                    comment = true;
                    ++index;
                } else if (character == '\'' || character == '"') {
                    quote = character;
                } else if (character == '(') {
                    ++parentheses;
                } else if (character == ')' && parentheses > 0) {
                    --parentheses;
                } else if (character == ';' && parentheses == 0) {
                    return index;
                } else if (character == '{' && parentheses == 0) {
                    return std::string_view::npos;
                }
            }
            return std::string_view::npos;
        };
        const auto absolutize_urls = [&](std::string_view value, const std::string& base) {
            std::string result;
            result.reserve(value.size());
            char quote = 0;
            auto comment = false;
            for (size_t index = 0U; index < value.size();) {
                const auto character = value[index];
                const auto next = index + 1U < value.size() ? value[index + 1U] : 0;
                if (comment) {
                    result.push_back(character);
                    ++index;
                    if (character == '*' && next == '/') {
                        result.push_back(next);
                        ++index;
                        comment = false;
                    }
                    continue;
                }
                if (quote != 0) {
                    result.push_back(character);
                    ++index;
                    if (character == '\\' && index < value.size()) {
                        result.push_back(value[index++]);
                    } else if (character == quote) {
                        quote = 0;
                    }
                    continue;
                }
                if (character == '/' && next == '*') {
                    result.append("/*");
                    index += 2U;
                    comment = true;
                    continue;
                }
                if (character == '\'' || character == '"') {
                    quote = character;
                    result.push_back(character);
                    ++index;
                    continue;
                }
                auto open = index;
                if (keyword_at(value, index, "url")) {
                    open += 3U;
                    while (open < value.size()
                        && std::isspace(static_cast<unsigned char>(value[open]))) ++open;
                }
                if (open >= value.size() || value[open] != '(') {
                    result.push_back(character);
                    ++index;
                    continue;
                }
                auto close = open + 1U;
                char url_quote = 0;
                for (; close < value.size(); ++close) {
                    if (url_quote != 0) {
                        if (value[close] == '\\') ++close;
                        else if (value[close] == url_quote) url_quote = 0;
                    } else if (value[close] == '\'' || value[close] == '"') {
                        url_quote = value[close];
                    } else if (value[close] == ')') {
                        break;
                    }
                }
                if (close >= value.size()) {
                    result.append(value.substr(index));
                    break;
                }
                auto raw = trim(value.substr(open + 1U, close - open - 1U));
                if (raw.size() >= 2U
                    && (raw.front() == '\'' || raw.front() == '"')
                    && raw.back() == raw.front()) {
                    raw = raw.substr(1U, raw.size() - 2U);
                }
                const auto resolved = resolve_resource_url(raw, base);
                result += "url(\"";
                for (const auto byte : resolved) {
                    if (byte == '\\' || byte == '"') result.push_back('\\');
                    result.push_back(byte);
                }
                result += "\")";
                index = close + 1U;
            }
            return result;
        };

        std::function<std::string(std::string, const std::string&, size_t)> expand;
        expand = [&](std::string css, const std::string& base, size_t depth) {
            if (depth >= maximum_depth || import_count >= maximum_imports
                || loaded_bytes >= maximum_bytes) {
                return css;
            }
            std::string output;
            output.reserve(css.size());
            size_t cursor = 0U;
            auto imports_allowed = true;
            while (cursor < css.size()) {
                const auto trivia_begin = cursor;
                for (;;) {
                    while (cursor < css.size()
                        && std::isspace(static_cast<unsigned char>(css[cursor]))) {
                        ++cursor;
                    }
                    if (cursor + 1U < css.size() && css[cursor] == '/'
                        && css[cursor + 1U] == '*') {
                        const auto close = css.find("*/", cursor + 2U);
                        cursor = close == std::string::npos ? css.size() : close + 2U;
                        continue;
                    }
                    break;
                }
                output.append(css, trivia_begin, cursor - trivia_begin);
                if (cursor >= css.size()) break;

                const auto end = statement_end(css, cursor);
                if (end == std::string::npos) {
                    output.append(css, cursor, std::string::npos);
                    break;
                }
                if (keyword_at(css, cursor, "@charset")) {
                    output.append(css, cursor, end - cursor + 1U);
                    cursor = end + 1U;
                    continue;
                }
                if (keyword_at(css, cursor, "@layer") && imports_allowed) {
                    output.append(css, cursor, end - cursor + 1U);
                    cursor = end + 1U;
                    continue;
                }
                if (!keyword_at(css, cursor, "@import") || !imports_allowed) {
                    output.append(css, cursor, std::string::npos);
                    break;
                }

                ++import_count;
                auto position = cursor + 7U;
                while (position < end
                    && std::isspace(static_cast<unsigned char>(css[position]))) {
                    ++position;
                }
                std::string href;
                if (position < end && (css[position] == '\'' || css[position] == '"')) {
                    const auto quote = css[position++];
                    while (position < end && css[position] != quote) {
                        if (css[position] == '\\' && position + 1U < end) ++position;
                        href.push_back(css[position++]);
                    }
                    if (position < end) ++position;
                } else if (keyword_at(css, position, "url")
                    && position + 3U < end && css[position + 3U] == '(') {
                    position += 4U;
                    while (position < end
                        && std::isspace(static_cast<unsigned char>(css[position]))) ++position;
                    char quote = 0;
                    if (position < end && (css[position] == '\'' || css[position] == '"'))
                        quote = css[position++];
                    while (position < end
                        && ((quote != 0 && css[position] != quote)
                            || (quote == 0 && css[position] != ')'))) {
                        if (css[position] == '\\' && position + 1U < end) ++position;
                        href.push_back(css[position++]);
                    }
                    if (quote != 0 && position < end) ++position;
                    while (position < end && css[position] != ')') ++position;
                    if (position < end) ++position;
                    href = trim(href);
                }
                auto qualifiers = trim(std::string_view(css).substr(position, end - position));
                if (href.empty()) {
                    cursor = end + 1U;
                    continue;
                }

                std::string imported;
                std::string resolved;
                if (!load_parser_text_resource(
                        href,
                        base,
                        WEBSCENE_RESOURCE_STYLESHEET,
                        WEBSCENE_REQUEST_DESTINATION_STYLE,
                        imported,
                        resolved)
                    || imported.size() > maximum_bytes - std::min(loaded_bytes, maximum_bytes)
                    || ancestors.contains(resolved)) {
                    cursor = end + 1U;
                    continue;
                }
                loaded_bytes += imported.size();
                ancestors.insert(resolved);
                imported = absolutize_urls(imported, resolved);
                auto expanded = expand(std::move(imported), resolved, depth + 1U);
                ancestors.erase(resolved);

                auto lower = copy_ascii_lower(qualifiers);
                std::string layer;
                auto anonymous_layer = false;
                if (lower.starts_with("layer")) {
                    auto consumed = size_t{5U};
                    if (consumed < qualifiers.size() && qualifiers[consumed] == '(') {
                        const auto close = qualifiers.find(')', consumed + 1U);
                        if (close != std::string::npos) {
                            layer = trim(std::string_view(qualifiers).substr(
                                consumed + 1U, close - consumed - 1U));
                            consumed = close + 1U;
                        }
                    } else {
                        anonymous_layer = true;
                    }
                    qualifiers = trim(std::string_view(qualifiers).substr(consumed));
                    lower = copy_ascii_lower(qualifiers);
                }
                std::string supports;
                if (lower.starts_with("supports(")) {
                    auto depth_count = 1;
                    auto close = size_t{9U};
                    for (; close < qualifiers.size() && depth_count > 0; ++close) {
                        if (qualifiers[close] == '(') ++depth_count;
                        else if (qualifiers[close] == ')') --depth_count;
                    }
                    if (depth_count == 0) {
                        supports = trim(std::string_view(qualifiers).substr(9U, close - 10U));
                        qualifiers = trim(std::string_view(qualifiers).substr(close));
                    }
                }
                if (!qualifiers.empty())
                    expanded = "@media " + qualifiers + " {\n" + expanded + "\n}";
                if (!supports.empty())
                    expanded = "@supports (" + supports + ") {\n" + expanded + "\n}";
                if (!layer.empty() || anonymous_layer)
                    expanded = "@layer" + (layer.empty() ? std::string{} : " " + layer)
                        + " {\n" + expanded + "\n}";
                output += expanded;
                cursor = end + 1U;
            }
            return output;
        };
        return expand(std::move(source), stylesheet_address, 0U);
    }

    size_t add_stylesheet(
        std::string css,
        const std::string& stylesheet_address = {},
        uint32_t stylesheet_owner_id = 0,
        uint32_t shadow_scope_root_id = 0)
    {
        // Font registration belongs to document consumption, not transport:
        // cached, imported, inline and dynamically added sheets all come here.
        if (stylesheet_consumer) stylesheet_consumer(stylesheet_address, css);
        if (copy_ascii_lower(css).find("@import") != std::string::npos) {
            css = expand_stylesheet_imports(std::move(css), stylesheet_address);
        }
        // A newly registered face can change metrics without changing the CSS
        // family/weight cache key (including dynamically inserted font rules).
        if (copy_ascii_lower(css).find("@font-face") != std::string::npos)
            document.invalidate_font_measurements();
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        binding_callback_timer timer(profile_startup ? &startup_css_parse : nullptr);
#endif
        const auto first_new_rule = css_rules.size();
        std::vector<std::string> stylesheet_layer_names;
        auto* previous_layer_names = active_stylesheet_layer_names;
        active_stylesheet_layer_names = &stylesheet_layer_names;
#if defined(WEBSCENE_NATIVE_ENGINE_CSSPARSER)
        const auto previous_owner_id = active_stylesheet_owner_id;
        const auto previous_shadow_scope_root_id =
            active_stylesheet_shadow_scope_root_id;
        active_stylesheet_owner_id = stylesheet_owner_id;
        active_stylesheet_shadow_scope_root_id = shadow_scope_root_id;
        parse_css_rules_with_cssparser(css, stylesheet_address);
        active_stylesheet_owner_id = previous_owner_id;
        active_stylesheet_shadow_scope_root_id = previous_shadow_scope_root_id;
#else
        size_t comment = 0;
        while ((comment = css.find("/*", comment)) != std::string::npos) {
            const auto end = css.find("*/", comment + 2U);
            if (end == std::string::npos) {
                css.erase(comment);
                break;
            }
            css.erase(comment, end - comment + 2U);
        }
        const auto previous_owner_id = active_stylesheet_owner_id;
        const auto previous_shadow_scope_root_id =
            active_stylesheet_shadow_scope_root_id;
        active_stylesheet_owner_id = stylesheet_owner_id;
        active_stylesheet_shadow_scope_root_id = shadow_scope_root_id;
        parse_css_rules(css, 0, css.size(), {}, stylesheet_address);
        active_stylesheet_owner_id = previous_owner_id;
        active_stylesheet_shadow_scope_root_id = previous_shadow_scope_root_id;
#endif
        active_stylesheet_layer_names = previous_layer_names;
        if (stylesheet_owner_id == 0U) {
            auto& known = css_stylesheet_layer_names[0U];
            for (auto& name : stylesheet_layer_names) {
                if (std::find(known.begin(), known.end(), name) == known.end()) {
                    known.push_back(std::move(name));
                }
            }
        } else {
            css_stylesheet_layer_names.insert_or_assign(
                stylesheet_owner_id, std::move(stylesheet_layer_names));
        }
        css::rebuild_root_variables(
            css_rules, css_variables, important_css_variables);
        return first_new_rule;
    }

    std::string resolve_css_value(const dom_node& node, std::string value) const
    { return css::resolve_value(node,std::move(value),css_variables); }

    static bool is_css_hex_digit(unsigned char c) { return css::is_css_hex_digit(c); }
    static size_t skip_css_escape_sequence(std::string_view text,size_t slash)
    { return css::skip_css_escape_sequence(text,slash); }
    static std::string read_css_identifier(std::string_view text,size_t& cursor)
    { return css::read_css_identifier(text,cursor); }

    static bool css_attribute_selector_matches(const dom_node& node,std::string_view condition)
    { return css::attribute_matches(node,condition); }

    static size_t find_css_attribute_close(std::string_view selector,size_t cursor)
    { return css::find_css_attribute_close(selector,cursor); }

    static bool css_nth_expression_matches(std::string_view argument,int position)
    { return css::nth_matches(argument,position); }

    bool is_actually_disabled(const dom_node& node) const
    { return css::is_actually_disabled(document,node); }

    static compiled_css_compound compile_css_compound_selector(std::string_view selector)
    { return css::compile_css_compound_selector(selector); }

    css::interaction_state selector_interaction_state() const
    { return {hover_target,active_element,focus_visible,active_pointer_target}; }
    std::optional<std::string> selector_target_hash() const
    {
                const auto local_context = isolate->GetCurrentContext();
                v8::Local<v8::Value> location_value;
                v8::Local<v8::Value> hash_value;
                if (!local_context->Global()->Get(
                        local_context, js_string(isolate, "location")).ToLocal(&location_value)
                    || !location_value->IsObject()
                    || !location_value.As<v8::Object>()->Get(
                        local_context, js_string(isolate, "hash")).ToLocal(&hash_value)) {
                    return std::nullopt;
                }
                auto hash = to_utf8(isolate, hash_value);
                return hash;
    }
    bool custom_state_matches(const dom_node& node, std::string_view state) const
    {
        const auto known = custom_states.find(node.id);
        return known != custom_states.end()
            && std::find(known->second.begin(), known->second.end(), state)
                != known->second.end();
    }
    bool css_compound_selector_matches(const dom_node& node,const compiled_css_compound& selector,
        const dom_node* scope_root = nullptr, bool stable_features_only = false) const
    {
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        ++css_compound_match_checks;
#endif
        return css::compound_matches(*this,node,selector,scope_root,stable_features_only);
    }

    void record_css_positional_sibling_visit() const
    {
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        ++css_positional_sibling_visits;
#endif
    }

    std::optional<css::positional_sibling_summary> css_positional_summary(
        const dom_node& node, bool of_type) const
    {
        if (active_selector_match_context == nullptr) return std::nullopt;
        return active_selector_match_context->position(node, of_type,
            [this] { record_css_positional_sibling_visit(); });
    }

    bool css_compound_selector_matches(
        const dom_node& node,
        std::string_view selector,
        const dom_node* scope_root = nullptr) const
    {
        return css_compound_selector_matches(
            node,
            compile_css_compound_selector(selector),
            scope_root);
    }

    struct css_selector_split final {
        std::string_view left;
        std::string_view right;
        char combinator{' '};
        bool found{false};
    };

    static css_selector_split split_css_selector(std::string_view selector)
    {
        selector = trim_css_view(selector);
        int bracket_depth = 0;
        int parenthesis_depth = 0;
        char quote = 0;
        css_selector_split last{{}, selector, ' ', false};
        for (size_t index = 0; index < selector.size(); ++index) {
            const auto value = selector[index];
            if (quote != 0) {
                if (value == quote && (index == 0U || selector[index - 1U] != '\\')) quote = 0;
                continue;
            }
            if (value == '\\') {
                index = skip_css_escape_sequence(selector, index) - 1U;
                continue;
            }
            if (value == '\'' || value == '"') {
                quote = value;
                continue;
            }
            if (value == '[') ++bracket_depth;
            else if (value == ']') --bracket_depth;
            else if (value == '(') ++parenthesis_depth;
            else if (value == ')') --parenthesis_depth;
            if (bracket_depth != 0 || parenthesis_depth != 0) continue;
            if (value == '>' || value == '+' || value == '~') {
                auto right = index + 1U;
                while (right < selector.size()
                    && std::isspace(static_cast<unsigned char>(selector[right]))) ++right;
                last = {
                    trim_css_view(selector.substr(0, index)),
                    trim_css_view(selector.substr(right)),
                    value,
                    true};
                continue;
            }
            if (!std::isspace(static_cast<unsigned char>(value))) continue;
            const auto whitespace_start = index;
            while (index + 1U < selector.size()
                && std::isspace(static_cast<unsigned char>(selector[index + 1U]))) ++index;
            const auto right = index + 1U;
            if (right >= selector.size()) continue;
            const auto previous = whitespace_start == 0U ? 0 : selector[whitespace_start - 1U];
            const auto next = selector[right];
            if (previous == '>' || previous == '+' || previous == '~'
                || next == '>' || next == '+' || next == '~') continue;
            last = {
                trim_css_view(selector.substr(0, whitespace_start)),
                trim_css_view(selector.substr(right)),
                ' ',
                true};
        }
        return last;
    }

    static compiled_css_selector compile_css_selector(
        std::string_view selector,
        const selector_namespace_context* namespaces = nullptr)
    {
#if defined(WEBSCENE_NATIVE_ENGINE_SERVO_SELECTORS)
        return css::compile_selector(selector, namespaces);
#else
        std::vector<std::string> reversed_compounds;
        std::vector<char> reversed_combinators;
        auto remaining = trim_css_view(selector);
        while (!remaining.empty()) {
            const auto split = split_css_selector(remaining);
            reversed_compounds.emplace_back(trim_css_view(split.right));
            if (!split.found || split.left.empty()) break;
            reversed_combinators.push_back(split.combinator);
            remaining = split.left;
        }

        compiled_css_selector result;
        result.compounds.assign(
            reversed_compounds.rbegin(),
            reversed_compounds.rend());
        result.combinators.assign(
            reversed_combinators.rbegin(),
            reversed_combinators.rend());
        result.specificity = css_selector_specificity(selector);
        result.compiled_compounds.reserve(result.compounds.size());
        for (const auto& compound : result.compounds) {
            result.compiled_compounds.push_back(
                compile_css_compound_selector(compound, namespaces));
        }
        return result;
#endif
    }

    static compiled_css_selector_list compile_css_selector_list(
        std::string_view selector,
        const selector_namespace_context* namespaces = nullptr)
    {
        compiled_css_selector_list result;
#if defined(WEBSCENE_NATIVE_ENGINE_SERVO_SELECTORS)
        return css::compile_selector_list(selector, namespaces);
#else
        size_t start = 0;
        int bracket_depth = 0;
        int parenthesis_depth = 0;
        char quote = 0;
        for (size_t index = 0; index <= selector.size(); ++index) {
            const auto at_end = index == selector.size();
            const auto character = at_end ? ',' : selector[index];
            if (!at_end && quote != 0) {
                if (character == quote
                    && (index == 0U || selector[index - 1U] != '\\')) {
                    quote = 0;
                }
                continue;
            }
            if (!at_end && character == '\\') {
                index = skip_css_escape_sequence(selector, index) - 1U;
                continue;
            }
            if (!at_end && (character == '\'' || character == '"')) {
                quote = character;
                continue;
            }
            if (!at_end && character == '[') ++bracket_depth;
            else if (!at_end && character == ']') --bracket_depth;
            else if (!at_end && character == '(') ++parenthesis_depth;
            else if (!at_end && character == ')') --parenthesis_depth;
            else if (character == ',' && bracket_depth == 0 && parenthesis_depth == 0) {
                result.selectors.push_back(compile_css_selector(
                    selector.substr(start, index - start), namespaces));
                start = index + 1U;
            }
        }
#endif
        return result;
    }

    std::shared_ptr<const compiled_css_selector_list> cached_css_selector_list(
        std::string_view selector) const
    {
        if (const auto known = compiled_css_selector_lists.find(selector);
            known != compiled_css_selector_lists.end()) {
            if (active_selector_match_context != nullptr)
                active_selector_match_context->retained_lists.try_emplace(known->second.get(), known->second);
            return known->second;
        }
        if (compiled_css_selector_lists.size()
            >= maximum_compiled_css_selector_lists) {
            compiled_css_selector_lists.clear();
        }
        auto key = std::string(selector);
        auto prepared = std::make_shared<const compiled_css_selector_list>(
            compile_css_selector_list(key));
        if (active_selector_match_context != nullptr)
            active_selector_match_context->retained_lists.try_emplace(prepared.get(), prepared);
        return compiled_css_selector_lists.emplace(
            key,
            std::move(prepared)).first->second;
    }

    static const dom_node* previous_element_sibling(const dom_node& node)
    { return css::previous_element_sibling(node); }

    bool css_selector_matches(
        const dom_node& node,
        std::string_view selector,
        const dom_node* scope_root = nullptr) const
    {
        // Functional pseudo-classes recurse through this cache. Keep the
        // selected entry alive if a nested selector reaches the cache bound
        // and clears the map while the outer matcher is still using it.
        const auto prepared = cached_css_selector_list(selector);
        return compiled_css_selector_list_matches(
            node,
            *prepared,
            scope_root);
    }

    bool css_compiled_selector_list_matches(
        const dom_node& node,
        const compiled_css_selector_list& selectors,
        const dom_node* scope_root = nullptr) const
    {
        return compiled_css_selector_list_matches(node, selectors, scope_root);
    }

    bool css_relative_selector_matches(
        const dom_node& scope,
        std::string_view relative) const
    {
        auto anchored = std::string(":scope ");
        anchored.append(relative);
        const auto prepared = cached_css_selector_list(anchored);
        return css::relative_selector_list_matches(
            scope, *prepared,
            [this](const dom_node& candidate,
                   const compiled_css_selector& selector,
                   const dom_node* root) {
                return !selector.compounds.empty()
                    && compiled_css_selector_matches(
                        candidate, selector,
                        selector.compounds.size() - 1U, root);
            });
    }

    bool css_compiled_relative_selector_list_matches(
        const dom_node& scope,
        const compiled_css_selector_list& selectors) const
    {
        return css::relative_selector_list_matches(
            scope, selectors,
            [this](const dom_node& candidate,
                   const compiled_css_selector& selector,
                   const dom_node* root) {
                return !selector.compounds.empty()
                    && compiled_css_selector_matches(
                        candidate, selector,
                        selector.compounds.size() - 1U, root);
            });
    }

    bool compiled_css_selector_matches(const dom_node& node,const compiled_css_selector& selector,
        size_t component,const dom_node* scope_root = nullptr) const
    {
        return css::selector_matches(document,node,selector,component,scope_root,
            [&](const dom_node& candidate,const compiled_css_compound& compound,const dom_node* scope) {
                return css_compound_selector_matches(candidate,compound,scope);
            }, active_selector_match_context);
    }

    bool css_rule_matches(
        const dom_node& node,
        const css_rule& rule,
        const dom_node* scope_root = nullptr) const
    {
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        ++css_rule_match_checks;
#endif
        return !rule.compiled_selector().compounds.empty()
            && compiled_css_selector_matches(
                node,
                rule.compiled_selector(),
                rule.compiled_selector().compounds.size() - 1U,
                scope_root);
    }

    bool css_selector_list_matches(
        const dom_node& node,
        std::string_view selector,
        const dom_node* scope_root = nullptr) const
    {
        const auto prepared = cached_css_selector_list(selector);
        return compiled_css_selector_list_matches(
            node,
            *prepared,
            scope_root);
    }

    static int split_pseudo_element_selector(const std::string& selector,std::string& origin)
    { return css::split_pseudo_element_selector(selector,origin); }
