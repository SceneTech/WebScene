    void apply_scrollbar_css_declaration(dom_node& node,int kind,const css_declaration& declaration)
    { css::apply_scrollbar_declaration(node,kind,declaration,css_variables); }

    static bool is_explicit_css_color_token(std::string_view value,uint32_t parsed) noexcept
    { return css::is_explicit_color_token(value,parsed); }

    static bool computed_layout_style_equal(const node_style& left,const node_style& right) noexcept
    { return css::computed_layout_style_equal(left,right); }

    static void append_utf8_codepoint(std::string& result, uint32_t codepoint)
    { css::append_utf8_codepoint(result,codepoint); }

    static std::string decode_css_content(std::string value)
    { return css::decode_content(std::move(value)); }

    template<typename Style>
    static bool apply_corner_radius_declaration(std::string_view name,const std::string& value,Style& style)
    { return css::apply_corner_radius_declaration(name,value,style); }

    static bool apply_box_shadow_value(node_style& style,const std::string& value,bool& complete)
    { return css::apply_box_shadow_value(style,value,complete); }

    void apply_pseudo_css_declaration(
        dom_node& node,
        node_style::pseudo_element& pseudo,
        const css_declaration& declaration)
    {
        feature_decision decision(
            *this,
            "css",
            "pseudo-property:" + declaration.name,
            "pseudo-style-application");
        css::apply_pseudo_declaration(node,pseudo,declaration,css_variables,decision,
            [&](bool contains_variable) {
                if(contains_variable && is_paint_property(declaration.name))
                    record_composition("css-custom-property-to-paint",node.id);
            });
    }

    static std::optional<std::string> first_css_url(const std::string& value)
    { return css::first_css_url(value); }

    static std::string svg_view_box(const std::string& markup)
    {
        const auto lower = lower_html_name(markup);
        const auto svg = lower.find("<svg");
        if (svg == std::string::npos) return {};
        const auto tag_end = lower.find('>', svg + 4U);
        if (tag_end == std::string::npos) return {};
        const auto view_box = lower.find("viewbox", svg + 4U);
        if (view_box != std::string::npos && view_box < tag_end) {
            const auto equals = markup.find('=', view_box + 7U);
            if (equals != std::string::npos && equals < tag_end) {
                auto value_start = equals + 1U;
                while (value_start < tag_end
                    && std::isspace(static_cast<unsigned char>(markup[value_start]))) {
                    ++value_start;
                }
                if (value_start < tag_end) {
                    const auto quote = markup[value_start];
                    if (quote == '\'' || quote == '"') {
                        const auto value_end = markup.find(quote, value_start + 1U);
                        if (value_end != std::string::npos && value_end <= tag_end) {
                            return markup.substr(
                                value_start + 1U,
                                value_end - value_start - 1U);
                        }
                    }
                }
            }
        }

        // SVG 2 permits an intrinsic viewport to be supplied by numeric width
        // and height attributes without a viewBox. Convert that bounded form
        // to the scene's canonical viewBox representation so ordinary bundled
        // CSS artwork (checker patterns, badges, and icons) retains its aspect
        // ratio instead of being discarded.
        const auto opening = markup.substr(svg, tag_end - svg + 1U);
        const auto parse_dimension = [](std::string value) -> std::optional<float> {
            value = trim_css(value);
            if (value.ends_with("px")) value.resize(value.size() - 2U);
            if (value.empty()) return std::nullopt;
            char* end = nullptr;
            const auto parsed = std::strtof(value.c_str(), &end);
            if (end == value.c_str() || *end != '\0' || !std::isfinite(parsed)
                || parsed <= 0.0F) {
                return std::nullopt;
            }
            return parsed;
        };
        const auto width = parse_dimension(extract_html_attribute(opening, "width"));
        const auto height = parse_dimension(extract_html_attribute(opening, "height"));
        if (!width.has_value() || !height.has_value()) return {};
        std::ostringstream synthesized;
        synthesized << "0 0 " << *width << ' ' << *height;
        return std::move(synthesized).str();
    }

    static void apply_background_position(node_style& style,const std::string& value)
    { css::apply_background_position(style,value); }

    static std::vector<std::string> split_css_component_list(std::string_view value,char separator)
    { return css::split_css_component_list(value,separator); }

    static std::vector<std::string> split_transition_tokens(std::string_view value)
    { return css::split_value_tokens(value); }
    static uint8_t margin_sides(std::string_view name) { return css::margin_sides(name); }
    static bool apply_margin_declaration(node_style& style,std::string_view name,
        const std::string& value,uint8_t allowed=15U)
    { return css::apply_margin_declaration(style,name,value,allowed); }
    static bool apply_padding_declaration(node_style& style,std::string_view name,const std::string& value)
    { return css::apply_padding_declaration(style,name,value); }

    static css_length parse_inset_length(const std::string& value) { return css::parse_inset_length(value); }
    static bool apply_inset_declaration(node_style& style,std::string_view name,const std::string& value)
    { return css::apply_inset_declaration(style,name,value); }
    static bool apply_border_declaration(node_style& style,std::string_view name,const std::string& value)
    { return css::apply_border_declaration(style,name,value); }

    static bool is_css_time(std::string_view value) { return css::is_css_time(value); }
    static float parse_css_time_ms(std::string value) { return css::parse_css_time_ms(std::move(value)); }
    static void parse_transition_timing(const std::string& value,node_style::transition_timing& timing)
    { css::parse_transition_timing(value,timing); }

    static std::string serialize_computed_time_list(const std::string& value)
    {
        const auto times = split_css_component_list(value, ',');
        std::string result;
        for (const auto& time : times) {
            if (!result.empty()) result += ", ";
            result += serialize_css_number(
                static_cast<double>(parse_css_time_ms(time)) / 1000.0) + "s";
        }
        return result.empty() ? std::string{"0s"} : result;
    }

    static void configure_style_transitions(node_style& style) { css::configure_style_transitions(style); }
    static void apply_transition_shorthand(node_style& style,const std::string& value)
    { css::apply_transition_shorthand(style,value); }

    static bool apply_grid_placement_declaration(node_style& style,std::string_view name,const std::string& value)
    { return css::apply_grid_placement_declaration(style,name,value); }

    static void apply_animation_shorthand(node_style& style,const std::string& value)
    { css::apply_animation_shorthand(style,value); }

    void configure_style_keyframe_animation(dom_node& node)
    {
        css::configure_keyframes(node.style,opacity_keyframes);
        for (const auto& track : node.style.animations().keyframe_animations) {
            if (track.opacity_keyframes.size() >= 2U)
                record_composition("css-opacity-keyframe-animation",node.id);
            if (track.translation_keyframes.size() >= 2U
                || track.scale_keyframes.size() >= 2U)
                record_composition("css-transform-keyframe-animation",node.id);
            if (track.rotation_keyframes.size() >= 2U)
                record_composition("css-rotation-keyframe-animation",node.id);
            if (track.filter_keyframes.size() >= 2U)
                record_composition("css-filter-keyframe-animation",node.id);
        }
        const auto configure_pseudo = [&](node_style::pseudo_element& pseudo) {
            if (!pseudo.has_animation_data()) return;
            css::configure_keyframes(pseudo.mutable_animations(), opacity_keyframes);
            for (const auto& track : pseudo.animations().keyframe_animations) {
                if (track.opacity_keyframes.size() >= 2U)
                    record_composition("css-pseudo-opacity-keyframe-animation",node.id);
                if (track.translation_keyframes.size() >= 2U
                    || track.scale_keyframes.size() >= 2U
                    || track.rotation_keyframes.size() >= 2U)
                    record_composition("css-pseudo-transform-keyframe-animation",node.id);
                if (track.filter_keyframes.size() >= 2U)
                    record_composition("css-pseudo-filter-keyframe-animation",node.id);
            }
        };
        if (node.style.has_pseudo_elements()) {
            configure_pseudo(node.style.mutable_before_pseudo());
            configure_pseudo(node.style.mutable_after_pseudo());
        }
    }

    void recompute_cascaded_line_height(dom_node& node,const std::vector<const css_rule*>& rules)
    { css::recompute_cascaded_line_height(node,rules,css_variables); }

    std::string current_css_base_address(dom_node& node)
    {
        // CSS may be applied while cooperative frame hydration is outside an
        // entered V8 context.  Resolve from the styled node's own cascade root
        // instead of the caller/current context so nested documents retain
        // their navigation and <base> URL semantics without dereferencing an
        // empty Local<Context>.
        auto* root = css_cascade_root_for_node(node);
        return effective_document_base(root);
    }

    void apply_css_declaration(
        dom_node& node,
        const css_declaration& declaration,
        bool inline_origin = false,
        bool defer_transition_configuration = false)
    {
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        if (profile_css) {
            ++css_profile_declaration_applications;
        }
#endif
        const auto& name = declaration.name;
        if (name == "-moz-transform" || name == "-webkit-transform"
            || name == "grid-gap" || name == "grid-row-gap" || name == "grid-column-gap") {
            feature_decision alias_decision(
                *this,
                "css",
                "property:" + name,
                "style-application");
            auto normalized = declaration;
            normalized.name = name.starts_with("grid-")
                ? canonical_css_property_name(name) : "transform";
            apply_css_declaration(
                node,
                normalized,
                inline_origin,
                defer_transition_configuration);
            return;
        }
        if (name.starts_with("--")) {
            css::apply_custom_property(node,declaration);
            record_feature("css","custom-property","supported",{},"style-application");
            return;
        }
        const auto* resolved_declaration = &declaration;
        std::optional<css_declaration> resolved_resource_declaration;
        if ((name == "background" || name == "background-image" || name == "mask-image")
            && first_css_url(declaration.value).has_value()) {
            resolved_resource_declaration = declaration;
            resolved_resource_declaration->value = resolve_css_resource_urls(
                declaration.value,
                current_css_base_address(node));
            resolved_declaration = &*resolved_resource_declaration;
        }
        feature_decision decision(
            *this,
            feature_decision::css_property_tag{},
            name);
        css::apply_declaration(document,node,*resolved_declaration,css_variables,inline_origin,decision,
            [this,&node,&name](const std::string& url,std::string& markup,std::string& resolved,std::string& view_box) {
                auto resource_context = isolate->GetCurrentContext();
                if (resource_context.IsEmpty()) resource_context = context_for_node(node);
                if (!resource_context.IsEmpty()
                    && enqueue_controlled_css_image_resource(
                        node, resource_context, name, url)) {
                    if (name == "mask-image") {
                        markup = "webscene-mask-pending-v1";
                        resolved = resolve_resource_url(
                            url, current_css_base_address(node));
                        view_box = "pending";
                        return true;
                    }
                    return false;
                }
                if(!load_text_resource(url,current_css_base_address(node),WEBSCENE_RESOURCE_IMAGE,markup,resolved))
                    return false;
                view_box=svg_view_box(markup);
                std::string raster;
                const auto raster_result =
                    css_raster_image_resource(markup, raster, view_box);
                if (raster_result == css_raster_image_result::ready) {
                    markup = std::move(raster);
                } else if (name == "mask-image"
                    && raster_result == css_raster_image_result::rejected) {
                    view_box.clear();
                }
                return true;
            },[&](bool contains_variable) {
                if(contains_variable && is_paint_property(name))
                    record_composition("css-custom-property-to-paint",node.id);
            },defer_transition_configuration);
    }

    void recompute_inline_font_relative_metrics(dom_node& node)
    { css::recompute_inline_font_relative_metrics(node); }

    void apply_css_rules(dom_node& node)
    {
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        binding_callback_timer timer(profile_startup ? &startup_css_apply : nullptr);
        const auto trace_css = profile_css;
        const auto css_apply_started = trace_css
            ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point{};
#endif
        // Only elements are CSS selector subjects. Text nodes generate anonymous
        // inline boxes; comments, doctypes, fragments, and other DOM-only nodes
        // never generate boxes. In particular, do not replace the display:none
        // assigned when a preserved comment was created with the default block
        // display for an unknown tag during a subtree recascade.
        if (node.kind != dom_node_kind::element) {
            const auto display_changed = node.style.display
                != (node.kind == dom_node_kind::text
                    ? display_mode::inline_flow
                    : display_mode::none);
            node.style.display = node.kind == dom_node_kind::text
                ? display_mode::inline_flow
                : display_mode::none;
            if (display_changed) document.mark_dirty();
            else document.mark_scene_changed();
            return;
        }
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        ++css_cascade_applications;
#endif
        const auto previous_style = node.style;
        css::reset_cascaded_style(node,css_variables);
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        const auto reset_completed = trace_css
            ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point{};
#endif
        auto candidates = css::collect_candidates(node, &node == active_element,
            css_rules_by_tag, css_rules_by_id, css_rules_by_attribute,
            css_focus_rules, unindexed_css_rules,
            [&](std::vector<size_t>& candidates, std::string_view class_name) {
                const auto append_candidates = [&](const auto& index, const auto& key) {
                    const auto match = index.find(key);
                    if (match != index.end())
                        candidates.insert(candidates.end(), match->second.begin(), match->second.end());
                };
#if defined(WEBSCENE_NATIVE_ENGINE_MEDIA_REFRESH_BENCHMARK_COUNTERS)
                media_refresh_class_lookups.fetch_add(1U, std::memory_order_relaxed);
#endif
#if defined(WEBSCENE_NATIVE_ENGINE_CSS_OWNED_CLASS_LOOKUP_CONTROL)
#if defined(WEBSCENE_NATIVE_ENGINE_MEDIA_REFRESH_BENCHMARK_COUNTERS)
                media_refresh_owned_class_lookup_keys.fetch_add(1U, std::memory_order_relaxed);
                media_refresh_owned_class_lookup_bytes.fetch_add(
                    class_name.size(), std::memory_order_relaxed);
#endif
                append_candidates(
                    css_rules_by_class,
                    std::string(class_name));
#else
                append_candidates(
                    css_rules_by_class,
                    class_name);
#endif
            });
        css::deduplicate_candidates(candidates);
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        css_cascade_candidate_checks += candidates.size();
        if (trace_css) css_profile_candidate_checks += candidates.size();
#endif
        auto matching=css::match_candidates(document,node,css_rules,candidates,
            [this](const dom_node& subject,const css_rule&,const compiled_css_selector& selector) {
                return compiled_css_selector_matches(subject,selector,selector.compounds.size()-1U);
            },[this](const dom_node& subject,const css_rule& rule) {
                return css_rule_matches(subject,rule);
            });
        auto& matched_rules=matching.ordinary;
        auto& matched_pseudo_rules=matching.pseudo;
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        if (trace_css) css_profile_matched_rules += matched_rules.size();
        const auto matching_completed = trace_css
            ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point{};
#endif
        const css::cascaded_rule_order cascade_order(matched_rules);
        css::apply_matched_declarations(node,cascade_order,
            [&](const css_declaration& declaration,bool inline_origin) {
                apply_css_declaration(node,declaration,inline_origin,true);
            });
        // Transition longhands share one computed transition table. During a
        // full cascade, configure it once after every winning declaration has
        // landed instead of reparsing its four lists after each longhand.
        css::configure_style_transitions(node.style);
        // Inline relative line-height is authored at a higher origin, but its
        // computed pixel value depends on the winning font-size. Re-resolve it
        // only after stylesheet declarations have completed their cascade.
        css::recompute_cascaded_line_height(node, cascade_order, css_variables);
        recompute_inline_font_relative_metrics(node);
        // Pseudo-elements inherit the originating element's completed
        // computed style. Apply them only after its custom and ordinary
        // declarations so `::before { background:var(--active-bg) }` resolves
        // the state variable authored on the active element.
        bool backdrop_important=false;
        if(node.dialog_state)node.dialog_state->backdrop_rgba=0;
        css::for_each_cascaded_pseudo_declaration(
            matched_pseudo_rules,
            [&](int pseudo_kind, const css_declaration& declaration) {
            if(pseudo_kind==7) {
                (void)css::apply_backdrop_declaration(
                    node,declaration,css_variables,backdrop_important);
                return;
            }
            if (pseudo_kind >= 3 && pseudo_kind <= 6) {
                apply_scrollbar_css_declaration(node, pseudo_kind, declaration);
                return;
            }
            auto& pseudo = pseudo_kind == 1
                ? node.style.mutable_before_pseudo()
                : node.style.mutable_after_pseudo();
            apply_pseudo_css_declaration(node, pseudo, declaration);
        });
        // The pseudo-element's used rectangle is layout state, not computed
        // style. A paint-only recascade rebuilds the computed pseudo style but
        // does not schedule layout, so retain the last used rectangle until a
        // layout-affecting change below marks the document dirty. The ensuing
        // layout pass will replace it with newly resolved geometry.
        if (node.style.before_pseudo().generated
            && previous_style.before_pseudo().generated) {
            node.style.mutable_before_pseudo().layout =
                previous_style.before_pseudo().layout;
        }
        if (node.style.after_pseudo().generated
            && previous_style.after_pseudo().generated) {
            node.style.mutable_after_pseudo().layout =
                previous_style.after_pseudo().layout;
        }
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        const auto declarations_completed = trace_css
            ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point{};
#endif
        configure_style_keyframe_animation(node);
        document.update_style_animations(node);
        detect_css_compositions(node);
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        const auto force_layout_invalidation =
            std::getenv("WEBSCENE_FORCE_RECASCADE_LAYOUT_INVALIDATION") != nullptr;
#else
        constexpr auto force_layout_invalidation = false;
#endif
        if (force_layout_invalidation
            || !computed_layout_style_equal(previous_style, node.style)) {
            document.mark_dirty();
        } else {
            document.mark_scene_changed();
        }
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        if (trace_css) {
            const auto completed = std::chrono::steady_clock::now();
            css_profile_reset_nanoseconds +=
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    reset_completed - css_apply_started).count();
            css_profile_matching_nanoseconds +=
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    matching_completed - reset_completed).count();
            css_profile_declarations_nanoseconds +=
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    declarations_completed - matching_completed).count();
            css_profile_finalize_nanoseconds +=
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    completed - declarations_completed).count();
        }
#endif
    }

    bool has_appended_css_rule_candidate(
        const dom_node& node,
        size_t first_new_rule) const
    {
        if (node.tag == "#text" || first_new_rule >= css_rules.size()) return false;
        const auto contains_new_rule = [first_new_rule](
            const std::vector<size_t>& indexes) {
            return !indexes.empty() && indexes.back() >= first_new_rule;
        };
        const auto index_contains_new_rule = [&contains_new_rule](
            const auto& index,
            const auto& key) {
            const auto match = index.find(key);
            return match != index.end() && contains_new_rule(match->second);
        };
        if (contains_new_rule(unindexed_css_rules)
            || index_contains_new_rule(css_rules_by_tag, node.tag)
            || (!node.id_attribute.empty()
                && index_contains_new_rule(css_rules_by_id, node.id_attribute))) {
            return true;
        }
        if (node.tag == "body"
            && (node.parent == nullptr || node.parent->tag == "iframe")
            && index_contains_new_rule(css_rules_by_tag, "html")) {
            return true;
        }
        for (const auto& [name, value] : node.attributes) {
            static_cast<void>(value);
            if (index_contains_new_rule(css_rules_by_attribute, name)) return true;
        }
        if (&node == active_element && contains_new_rule(css_focus_rules)) return true;
        const std::string_view classes(node.class_name);
        size_t cursor = 0U;
        while (cursor < classes.size()) {
            while (cursor < classes.size()
                && std::isspace(static_cast<unsigned char>(classes[cursor]))) ++cursor;
            const auto start = cursor;
            while (cursor < classes.size()
                && !std::isspace(static_cast<unsigned char>(classes[cursor]))) ++cursor;
            if (cursor > start
                && index_contains_new_rule(
                    css_rules_by_class,
#if defined(WEBSCENE_NATIVE_ENGINE_CSS_OWNED_CLASS_LOOKUP_CONTROL)
                    std::string(classes.substr(start, cursor - start)))) {
#else
                    classes.substr(start, cursor - start))) {
#endif
                return true;
            }
        }
        return false;
    }

    bool apply_appended_css_rules(dom_node& node, size_t first_new_rule)
    {
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        binding_callback_timer timer(
            profile_startup ? &startup_css_incremental_apply : nullptr);
#endif
        if (node.tag == "#text" || first_new_rule >= css_rules.size()) return false;

        std::vector<size_t> candidates;
        const auto append_new_candidates = [&](const std::vector<size_t>& source) {
            const auto first = std::lower_bound(
                source.begin(), source.end(), first_new_rule);
            candidates.insert(candidates.end(), first, source.end());
        };
        append_new_candidates(unindexed_css_rules);
        const auto append_candidates = [&](const auto& index, const auto& key) {
            const auto match = index.find(key);
            if (match != index.end()) {
                append_new_candidates(match->second);
            }
        };
        append_candidates(css_rules_by_tag, node.tag);
        if (node.tag == "body"
            && (node.parent == nullptr || node.parent->tag == "iframe")) {
            append_candidates(css_rules_by_tag, "html");
        }
        if (!node.id_attribute.empty()) append_candidates(css_rules_by_id, node.id_attribute);
        for (const auto& [name, value] : node.attributes) {
            static_cast<void>(value);
            append_candidates(css_rules_by_attribute, name);
        }
        if (&node == active_element) {
            append_new_candidates(css_focus_rules);
        }
        const std::string_view classes(node.class_name);
        size_t class_cursor = 0;
        while (class_cursor < classes.size()) {
            while (class_cursor < classes.size()
                && std::isspace(static_cast<unsigned char>(classes[class_cursor]))) ++class_cursor;
            const auto start = class_cursor;
            while (class_cursor < classes.size()
                && !std::isspace(static_cast<unsigned char>(classes[class_cursor]))) ++class_cursor;
            if (class_cursor > start) {
                append_candidates(
                    css_rules_by_class,
#if defined(WEBSCENE_NATIVE_ENGINE_CSS_OWNED_CLASS_LOOKUP_CONTROL)
                    std::string(classes.substr(start, class_cursor - start)));
#else
                    classes.substr(start, class_cursor - start));
#endif
            }
        }
        sort_css_candidates(candidates);
        candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());

        if (filter_stylesheet_candidates && candidates.empty()) return false;

        std::vector<const css_rule*> matched_rules;
        std::vector<std::pair<int, const css_rule*>> matched_pseudo_rules;
        for (const auto index : candidates) {
            const auto& rule = css_rules[index];
            if (!rule.media_matches) continue;
            std::string pseudo_origin;
            const auto pseudo_kind = split_pseudo_element_selector(rule.selector(), pseudo_origin);
            if (pseudo_kind != 0) {
                if (!pseudo_origin.empty() && css_selector_matches(node, pseudo_origin)) {
                    matched_pseudo_rules.emplace_back(pseudo_kind, &rule);
                }
                continue;
            }
            if (css_rule_matches(node, rule)) matched_rules.push_back(&rule);
        }
        if (filter_stylesheet_candidates
            && matched_rules.empty()
            && matched_pseudo_rules.empty()) {
            return false;
        }
        for (const auto* rule : matched_rules) {
            for (const auto& declaration : rule->declarations()) {
                if (declaration.name.starts_with("--")) apply_css_declaration(node, declaration);
            }
        }
        for (const auto* rule : matched_rules) {
            for (const auto& declaration : rule->declarations()) {
                if (!declaration.name.starts_with("--")) apply_css_declaration(node, declaration);
            }
        }
        recompute_cascaded_line_height(node, matched_rules);
        recompute_inline_font_relative_metrics(node);
        bool backdrop_important=false;
        if(node.dialog_state)node.dialog_state->backdrop_rgba=0;
        css::for_each_cascaded_pseudo_declaration(
            matched_pseudo_rules,
            [&](int pseudo_kind, const css_declaration& declaration) {
            if(pseudo_kind==7) {
                (void)css::apply_backdrop_declaration(
                    node,declaration,css_variables,backdrop_important);
                return;
            }
            if (pseudo_kind >= 3 && pseudo_kind <= 6) {
                apply_scrollbar_css_declaration(node, pseudo_kind, declaration);
                return;
            }
            auto& pseudo = pseudo_kind == 1
                ? node.style.mutable_before_pseudo()
                : node.style.mutable_after_pseudo();
            apply_pseudo_css_declaration(node, pseudo, declaration);
        });
        configure_style_keyframe_animation(node);
        document.update_style_animations(node);
        detect_css_compositions(node);
        document.mark_dirty();
        return true;
    }

    std::unordered_set<std::string> appended_css_custom_properties(
        size_t first_new_rule) const
    {
        std::unordered_set<std::string> properties;
        for (auto index = first_new_rule; index < css_rules.size(); ++index) {
            for (const auto& declaration : css_rules[index].declarations()) {
                if (declaration.name.starts_with("--")) {
                    properties.insert(declaration.name);
                }
            }
        }
        return properties;
    }

    bool declaration_references_variable(
        const css_rule& rule,
        size_t declaration_index,
        std::string_view variable) const
    {
        if (declaration_index
            < rule.payload->declaration_variable_references.size()) {
            const auto& references =
                rule.payload->declaration_variable_references[declaration_index];
            return std::find(
                references.begin(), references.end(), variable)
                != references.end();
        }
        return declaration_index < rule.declarations().size()
            && css_variable_references(
                rule.declarations()[declaration_index].value).contains(
                    std::string(variable));
    }

    template<typename Variables>
    bool declaration_references_any_variable(
        const css_rule& rule,
        size_t declaration_index,
        const Variables& variables) const
    {
        if (declaration_index
            < rule.payload->declaration_variable_references.size()) {
            const auto& references =
                rule.payload->declaration_variable_references[declaration_index];
            return std::any_of(
                references.begin(), references.end(),
                [&variables](const auto& reference) {
                    return css::variable_collection_contains(
                        variables,
                        reference);
                });
        }
        if (declaration_index >= rule.declarations().size()) return false;
        const auto references = css_variable_references(
            rule.declarations()[declaration_index].value);
        return std::any_of(
            references.begin(), references.end(),
            [&variables](const auto& reference) {
                return css::variable_collection_contains(
                    variables,
                    reference);
            });
    }

    void expand_css_variable_dependencies(
        std::unordered_set<std::string>& properties) const
    {
        std::vector<std::string> pending(properties.begin(), properties.end());
        for (size_t cursor = 0U; cursor < pending.size(); ++cursor) {
            const auto indexed = css_rules_by_variable_reference.find(pending[cursor]);
            if (indexed == css_rules_by_variable_reference.end()) continue;
            for (const auto rule_index : indexed->second) {
                if (rule_index >= css_rules.size()) continue;
                const auto& rule = css_rules[rule_index];
                for (size_t declaration_index = 0U;
                     declaration_index < rule.declarations().size();
                     ++declaration_index) {
                    const auto& declaration =
                        rule.declarations()[declaration_index];
                    if (!declaration.name.starts_with("--")) continue;
                    if (!declaration_references_variable(
                            rule, declaration_index, pending[cursor])) continue;
                    if (properties.insert(declaration.name).second) {
                        pending.push_back(declaration.name);
                    }
                }
            }
        }
    }

    struct css_variable_recascade_rule final {
        size_t index{0U};
        std::vector<std::string> properties;
    };

    struct css_variable_recascade_plan final {
        std::unordered_set<std::string> variables;
        std::vector<css_variable_recascade_rule> rules;
        std::unordered_map<std::string, std::vector<size_t>> rules_by_class;
        std::unordered_map<std::string, std::vector<size_t>> rules_by_id;
        std::unordered_map<std::string, std::vector<size_t>> rules_by_tag;
        std::unordered_map<std::string, std::vector<size_t>> rules_by_attribute;
        std::vector<size_t> focus_rules;
        std::vector<size_t> unindexed_rules;
    };

    css_variable_recascade_plan make_css_variable_recascade_plan(
        std::unordered_set<std::string> variables) const
    {
        css_variable_recascade_plan plan;
        plan.variables = std::move(variables);
        expand_css_variable_dependencies(plan.variables);
        if (plan.variables.empty()) return plan;

        std::vector<size_t> dependent_rules;
        for (const auto& variable : plan.variables) {
            const auto indexed = css_rules_by_variable_reference.find(variable);
            if (indexed != css_rules_by_variable_reference.end()) {
                dependent_rules.insert(
                    dependent_rules.end(),
                    indexed->second.begin(),
                    indexed->second.end());
            }
        }
        std::sort(dependent_rules.begin(), dependent_rules.end());
        dependent_rules.erase(
            std::unique(dependent_rules.begin(), dependent_rules.end()),
            dependent_rules.end());
        for (const auto index : dependent_rules) {
            if (index >= css_rules.size()) continue;
            css_variable_recascade_rule dependent{index, {}};
            const auto& rule = css_rules[index];
            for (size_t declaration_index = 0U;
                 declaration_index < rule.declarations().size();
                 ++declaration_index) {
                const auto& declaration =
                    rule.declarations()[declaration_index];
                if (declaration.name.starts_with("--")) continue;
                if (declaration_references_any_variable(
                        rule, declaration_index, plan.variables)) {
                    dependent.properties.push_back(declaration.name);
                }
            }
            std::sort(dependent.properties.begin(), dependent.properties.end());
            dependent.properties.erase(
                std::unique(
                    dependent.properties.begin(), dependent.properties.end()),
                dependent.properties.end());
            if (!dependent.properties.empty()) {
                plan.rules.push_back(std::move(dependent));
            }
        }
        std::unordered_map<size_t, size_t> plan_index_by_rule;
        for (size_t index = 0U; index < plan.rules.size(); ++index) {
            plan_index_by_rule.emplace(plan.rules[index].index, index);
        }
        const auto project_index = [&](const auto& source, auto& target) {
            for (const auto& [key, rule_indexes] : source) {
                for (const auto rule_index : rule_indexes) {
                    const auto found = plan_index_by_rule.find(rule_index);
                    if (found != plan_index_by_rule.end()) {
                        target[key].push_back(found->second);
                    }
                }
            }
        };
        project_index(css_rules_by_class, plan.rules_by_class);
        project_index(css_rules_by_id, plan.rules_by_id);
        project_index(css_rules_by_tag, plan.rules_by_tag);
        project_index(css_rules_by_attribute, plan.rules_by_attribute);
        for (const auto rule_index : css_focus_rules) {
            const auto found = plan_index_by_rule.find(rule_index);
            if (found != plan_index_by_rule.end()) {
                plan.focus_rules.push_back(found->second);
            }
        }
        for (const auto rule_index : unindexed_css_rules) {
            const auto found = plan_index_by_rule.find(rule_index);
            if (found != plan_index_by_rule.end()) {
                plan.unindexed_rules.push_back(found->second);
            }
        }
        return plan;
    }

    css_variable_recascade_plan make_css_variable_recascade_plan(
        size_t first_new_rule) const
    {
        return make_css_variable_recascade_plan(
            appended_css_custom_properties(first_new_rule));
    }

    bool reapply_css_variable_dependent_rules(
        dom_node& node,
        const css_variable_recascade_plan& plan)
    {
        if (node.tag == "#text" || plan.variables.empty()) return false;

        const auto references_affected_variable = [&](std::string_view value) {
            const auto references = css_variable_references(value);
            return std::any_of(
                references.begin(),
                references.end(),
                [&](const auto& reference) {
                    return plan.variables.contains(reference);
                });
        };

        auto filter_by_variable_dependency = true;
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        filter_by_variable_dependency =
            std::getenv("WEBSCENE_PROBE_DISABLE_CSS_VARIABLE_DEPENDENCY_FILTER")
                == nullptr;
#endif
        std::unordered_set<std::string> variable_dependent_properties;
        if (filter_by_variable_dependency) {
            auto plan_candidates = plan.unindexed_rules;
            const auto append_plan_candidates = [&](const auto& index, const std::string& key) {
                const auto match = index.find(key);
                if (match != index.end()) {
                    plan_candidates.insert(
                        plan_candidates.end(), match->second.begin(), match->second.end());
                }
            };
            append_plan_candidates(plan.rules_by_tag, node.tag);
            if (node.tag == "body"
                && (node.parent == nullptr || node.parent->tag == "iframe")) {
                append_plan_candidates(plan.rules_by_tag, "html");
            }
            if (!node.id_attribute.empty()) {
                append_plan_candidates(plan.rules_by_id, node.id_attribute);
            }
            for (const auto& [name, value] : node.attributes) {
                static_cast<void>(value);
                append_plan_candidates(plan.rules_by_attribute, name);
            }
            if (&node == active_element) {
                plan_candidates.insert(
                    plan_candidates.end(),
                    plan.focus_rules.begin(),
                    plan.focus_rules.end());
            }
            std::istringstream classes(node.class_name);
            for (std::string class_name; classes >> class_name;) {
                append_plan_candidates(plan.rules_by_class, class_name);
            }
            std::sort(plan_candidates.begin(), plan_candidates.end());
            plan_candidates.erase(
                std::unique(plan_candidates.begin(), plan_candidates.end()),
                plan_candidates.end());
            for (const auto candidate : plan_candidates) {
                const auto& dependent = plan.rules[candidate];
                const auto& rule = css_rules[dependent.index];
                if (!rule.media_matches || !css_rule_matches(node, rule)) continue;
                variable_dependent_properties.insert(
                    dependent.properties.begin(),
                    dependent.properties.end());
            }
            for (const auto& [name, value] : node.authored_style().declarations) {
                if (!name.starts_with("--") && references_affected_variable(value)) {
                    variable_dependent_properties.insert(name);
                }
            }
            if (variable_dependent_properties.empty()) return false;
        }
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        if (profile_startup) ++startup_stylesheet_variable_nodes;
#endif

        std::vector<size_t> candidates = unindexed_css_rules;
        const auto append_candidates = [&](const auto& index, const std::string& key) {
            const auto match = index.find(key);
            if (match != index.end()) {
                candidates.insert(candidates.end(), match->second.begin(), match->second.end());
            }
        };
        append_candidates(css_rules_by_tag, node.tag);
        if (node.tag == "body"
            && (node.parent == nullptr || node.parent->tag == "iframe")) {
            append_candidates(css_rules_by_tag, "html");
        }
        if (!node.id_attribute.empty()) append_candidates(css_rules_by_id, node.id_attribute);
        for (const auto& [name, value] : node.attributes) {
            static_cast<void>(value);
            append_candidates(css_rules_by_attribute, name);
        }
        if (&node == active_element) {
            candidates.insert(
                candidates.end(),
                css_focus_rules.begin(),
                css_focus_rules.end());
        }
        std::istringstream classes(node.class_name);
        for (std::string class_name; classes >> class_name;) {
            append_candidates(css_rules_by_class, class_name);
        }
        sort_css_candidates(candidates);
        candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());

        std::vector<const css_rule*> matched_rules;
        for (const auto index : candidates) {
            const auto& rule = css_rules[index];
            if (!rule.media_matches || !css_rule_matches(node, rule)) continue;
            matched_rules.push_back(&rule);
            if (!filter_by_variable_dependency) {
                for (const auto& declaration : rule.declarations()) {
                    if (!declaration.name.starts_with("--")
                        && declaration.value.find("var(") != std::string::npos) {
                        variable_dependent_properties.insert(declaration.name);
                    }
                }
            }
        }
        // Replay the complete cascade for every property whose computed value
        // depends on a newly introduced custom property. Replaying only the
        // var() declaration lets it jump past later declarations of equal or
        // higher precedence (for example a responsive `display:none`).
        for (const auto* rule : matched_rules) {
            for (const auto& declaration : rule->declarations()) {
                if (variable_dependent_properties.contains(declaration.name)) {
                    apply_css_declaration(node, declaration);
                }
            }
        }
        for (const auto& [name, value] : node.authored_style().declarations) {
            if (!variable_dependent_properties.contains(name)
                || !references_affected_variable(value)) {
                continue;
            }
            const auto property_mask = inline_style_mask(name);
            const auto retained_inline_mask = node.style.inline_property_mask;
            node.style.inline_property_mask &= ~property_mask;
            apply_css_declaration(
                node,
                {name, value,
                    node.authored_style().important_declarations.contains(name)}, true);
            node.style.inline_property_mask = retained_inline_mask;
        }
        document.mark_dirty();
        return true;
    }

    // Re-run the complete cascade only on dimension consumers when explicit
    // inheritance does not require descendant style propagation.
    // Keep the ordinary subtree path for aliases, inherited values and pseudo
    // elements; this is intentionally a conservative, uncached fast path.
    bool recascade_dimension_custom_property(dom_node& root, const std::string& name)
    {
        if (!is_connected(root) || document.containing_shadow_root(root) != nullptr) return false;
        auto* previous_root = const_cast<dom_node*>(active_css_cascade_root);
        auto* cascade_root = css_cascade_root_for_node(root);
        if (cascade_root != nullptr) activate_css_cascade(cascade_root);
        const auto restore = [this, previous_root, cascade_root](void*) {
            if (previous_root != nullptr && previous_root != cascade_root)
                activate_css_cascade(previous_root);
        };
        std::unique_ptr<void, decltype(restore)> scope(this, restore);

        if (css_attribute_dependencies.contains("style")) return false;
        // The lightweight attribute-dependency index does not decode escapes.
        for (const auto& rule : css_rules) {
            if (rule.selector().find(static_cast<char>(92)) != std::string::npos
                || copy_ascii_lower(rule.selector()).find("style") != std::string::npos) return false;
        }
        const auto is_geometry_property = [](std::string_view property) {
            return property == "width" || property == "height"
                || property == "min-width" || property == "max-width"
                || property == "min-height" || property == "max-height"
                || property == "grid-template-columns" || property == "grid-template-rows"
                || property == "grid-template-areas"
                || property == "left" || property == "right"
                || property == "top" || property == "bottom";
        };
        // Non-inherited-by-default dimensions can still explicitly inherit.
        // Include variable-resolved inheritance, not just literal tokens. If
        // present, the ordinary ancestor-first subtree cascade propagates it.
        std::vector<const css_rule*> inheritance_rules;
        for (const auto& rule : css_rules) {
            bool potential = false;
            for (const auto& declaration : rule.declarations()) {
                if (!is_geometry_property(declaration.name)) continue;
                if (trim_css_view(declaration.value) == "inherit") return false;
                potential = potential || declaration.value.find("var(") != std::string::npos;
            }
            if (potential) {
                std::string origin;
                if (split_pseudo_element_selector(rule.selector(), origin) != 0) return false;
                inheritance_rules.push_back(&rule);
            }
        }
        const auto references = [&](std::string_view value) {
            return css_variable_references(value).contains(name);
        };
        // An inherited inline alias may be authored above the mutation root.
        for (auto* parent = root.parent; parent != nullptr; parent = parent->parent) {
            for (const auto& [property, value] : parent->authored_style().declarations) {
                if (references(value) && !is_geometry_property(property)) return false;
            }
        }
        std::vector<const css_rule*> rules;
        if (const auto found = css_rules_by_variable_reference.find(name);
            found != css_rules_by_variable_reference.end()) {
            for (const auto index : found->second) {
                const auto& rule = css_rules[index];
                std::string origin;
                if (rule.shadow_scope_root_id != 0U
                    || split_pseudo_element_selector(rule.selector(), origin) != 0)
                    return false;
                for (size_t declaration_index = 0U;
                     declaration_index < rule.declarations().size();
                     ++declaration_index) {
                    const auto& declaration =
                        rule.declarations()[declaration_index];
                    if (declaration_references_variable(
                            rule, declaration_index, name)
                        && !is_geometry_property(declaration.name))
                        return false;
                }
                rules.push_back(&rule);
            }
        }
        std::vector<dom_node*> nodes{&root};
        std::vector<dom_node*> consumers;
        for (size_t i = 0; i < nodes.size(); ++i) {
            auto& node = *nodes[i];
            // Shadow inheritance and independent browsing contexts retain their
            // existing full-cascade handling.
            if (document.shadow_dom(node) != nullptr) return false;
            for (auto* child : node.children) {
                if (child != nullptr && !inactive_css_cascades.contains(child))
                    nodes.push_back(child);
            }
            bool affected = &node == &root;
            for (const auto& [property, value] : node.authored_style().declarations) {
                if (is_geometry_property(property)
                    && trim_css_view(resolve_css_value(node, value)) == "inherit") return false;
                if (!references(value)) continue;
                if (!is_geometry_property(property)) return false;
                affected = true;
            }
            if (node.kind == dom_node_kind::element) {
                for (const auto* rule : inheritance_rules) {
                    if (!rule->media_matches || !css_rule_matches(node, *rule)) continue;
                    for (const auto& declaration : rule->declarations()) {
                        if (is_geometry_property(declaration.name)
                            && trim_css_view(resolve_css_value(node, declaration.value)) == "inherit")
                            return false;
                    }
                }
            }
            if (!affected && node.kind == dom_node_kind::element) {
                affected = std::any_of(rules.begin(), rules.end(), [&](const auto* rule) {
                    return rule->media_matches && css_rule_matches(node, *rule);
                });
            }
            if (affected) consumers.push_back(&node);
        }
        // No partial updates before all conservative fallback checks succeed.
        for (auto* node : consumers) apply_css_rules(*node);
        return true;
    }

    void apply_css_rules_subtree(dom_node& node)
    {
        apply_css_rules(node);
        for (auto* child : node.children) {
            if (child == nullptr) continue;
            // An iframe owns an independent stylesheet cascade. A recascade
            // of an outer sibling or ancestor must never walk through that
            // browsing-context boundary with the caller's active rules.
            if (child != active_css_cascade_root
                && inactive_css_cascades.contains(child)) {
                continue;
            }
            apply_css_rules_subtree(*child);
        }
        const auto* shadow = document.shadow_dom(node);
        if (shadow != nullptr && shadow->root != nullptr) {
            apply_css_rules_subtree(*shadow->root);
        }
    }

    // A viewport change does not alter selector state. Re-evaluate only the
    // rules whose media-query truth changed. Non-inherited properties update
    // their matched subjects and explicit-inherit consumers. Custom-property
    // dependencies follow their stylesheet and inline alias chains. Modeled
    // inherited values propagate only while the computed value changes;
    // unknown/unmodeled inherited properties and shadow boundaries retain the
    // conservative complete-subtree path.
    void recascade_media_query_changes(
        dom_node& root,
        std::span<const size_t> changed_rules)
    {
        const auto trace_transition =
            std::getenv("WEBSCENE_TRACE_RECASCADE_ROOTS") != nullptr;
        const auto transition_started = trace_transition
            ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point{};
        webscene_frame_trace::scope invalidation_trace(
            frame_trace, "media-invalidation-start", "media-invalidation-end", 0);
        if (changed_rules.empty()) return;
        // Media truth changes, but this synchronous matching/cascade pass does
        // not mutate the DOM or dispatch authored callbacks. Reuse sibling
        // positions and selector prefixes across its subjects, then discard
        // them before matchMedia listeners can mutate selector state.
        selector_match_batch_scope matching_scope(*this);
        const css::rule_subject_index changed_index(css_rules,changed_rules);
        struct request final {
            dom_node* root;
            bool subtree;
            uint64_t changed_properties;
            uint64_t inherited_properties;
            bool full_cascade;
        };
        struct rule_effect final {
            bool subtree{};
            uint64_t changed_properties{};
            uint64_t inherited_properties{};
            std::vector<std::string> changed_variables;
        };
        const auto append_variable = [](auto& variables, std::string_view name) {
            if (std::find(variables.begin(), variables.end(), name)
                == variables.end()) {
                variables.emplace_back(name);
                return true;
            }
            return false;
        };
        const auto is_known_property = [](std::string_view name) {
            return css::generated_property_id_lowercase(name)
                    != css::css_property_id::unknown
                || std::binary_search(
                    css::native_storage_only_property_catalog.begin(),
                    css::native_storage_only_property_catalog.end(), name);
        };
        const auto classify_declaration = [
            &append_variable,
            &is_known_property](
            rule_effect& effect,
            const css_declaration& declaration) {
            if (declaration.name.starts_with("--")) {
                append_variable(effect.changed_variables, declaration.name);
                return;
            }
            const auto mask = css::property_mask(declaration.name);
            if (declaration.name == "all") {
                effect.subtree = true;
                return;
            }
            if (mask == 0U) {
                // A supported non-inherited property without a modeled
                // dependency mask still changes only its matched subject.
                // Unknown properties and unmodeled inherited values retain
                // the conservative subtree path.
                if (!is_known_property(declaration.name)
                    || css::generated_property_inherits_by_default(
                        declaration.name)) {
                    effect.subtree = true;
                }
                return;
            }
            if (css::generated_property_inherits_by_default(
                    declaration.name)) {
                effect.inherited_properties |= mask;
                return;
            }
            effect.changed_properties |= mask;
        };
        const auto classify_rule_effect = [&classify_declaration](
            const css_rule& rule) {
            rule_effect effect;
            for (const auto& declaration : rule.declarations()) {
                classify_declaration(effect, declaration);
            }
            return effect;
        };
        uint64_t exact_changed_properties = 0U;
        std::vector<std::string> changed_variables;
        for (const auto index : changed_rules) {
            if (index >= css_rules.size()) continue;
            const auto effect = classify_rule_effect(css_rules[index]);
            if (!effect.subtree) {
                exact_changed_properties |= effect.changed_properties;
                for (const auto& variable : effect.changed_variables) {
                    append_variable(changed_variables, variable);
                }
            }
        }
        struct inline_variable_declaration final {
            std::string_view name;
            std::string_view value;
            std::vector<std::string> references;
        };
        std::vector<inline_variable_declaration> inline_variable_declarations;
        if (!changed_variables.empty()) {
            const auto collect_inline = [&](const auto& self, dom_node& node) -> void {
                for (const auto& [name, value] :
                     node.authored_style().declarations) {
                    if (value.find("var(") == std::string::npos) continue;
                    const auto references = css_variable_references(value);
                    inline_variable_declarations.push_back(
                        {name, value,
                            {references.begin(), references.end()}});
                }
                for (auto* child : node.children) {
                    if (child == nullptr) continue;
                    if (child != active_css_cascade_root
                        && inactive_css_cascades.contains(child)) continue;
                    self(self, *child);
                }
                const auto* shadow = document.shadow_dom(node);
                if (shadow != nullptr && shadow->root != nullptr) {
                    self(self, *shadow->root);
                }
            };
            collect_inline(collect_inline, root);
        }
        std::unordered_set<std::string> affected_variable_set;
        std::vector<std::string> affected_variables;
        const auto queue_affected_variable = [&](std::string_view name) {
            if (!affected_variable_set.emplace(name).second) return;
            affected_variables.emplace_back(name);
        };
        for (const auto& variable : changed_variables) {
            queue_affected_variable(variable);
        }
        std::unordered_set<size_t> variable_rule_set;
        for (size_t cursor = 0; cursor < affected_variables.size(); ++cursor) {
            // Discovering another alias can grow affected_variables. Keep the
            // current name by value so that growth cannot invalidate the name
            // while stylesheet and inline dependency edges are inspected.
            const auto variable = affected_variables[cursor];
            if (const auto found = css_rules_by_variable_reference.find(variable);
                found != css_rules_by_variable_reference.end()) {
                for (const auto index : found->second) {
                    if (index >= css_rules.size()) continue;
                    variable_rule_set.insert(index);
                    const auto& rule = css_rules[index];
                    for (size_t declaration_index = 0U;
                         declaration_index < rule.declarations().size();
                         ++declaration_index) {
                        const auto& declaration =
                            rule.declarations()[declaration_index];
                        if (!declaration.name.starts_with("--")) continue;
                        if (declaration_references_variable(
                                rule, declaration_index, variable)) {
                            queue_affected_variable(declaration.name);
                        }
                    }
                }
            }
            for (const auto& declaration : inline_variable_declarations) {
                if (declaration.name.starts_with("--")
                    && std::find(
                        declaration.references.begin(),
                        declaration.references.end(),
                        variable) != declaration.references.end()) {
                    queue_affected_variable(declaration.name);
                }
            }
        }
        std::vector<size_t> variable_rule_indices(
            variable_rule_set.begin(), variable_rule_set.end());
        std::sort(variable_rule_indices.begin(), variable_rule_indices.end());
        const css::rule_subject_index variable_index(
            css_rules, variable_rule_indices);
        const auto variable_recascade_plan =
            make_css_variable_recascade_plan(affected_variable_set);
        const auto references_affected_variable = [&](std::string_view value) {
            if (value.find("var(") == std::string::npos) return false;
            const auto references = css_variable_references(value);
            return std::any_of(
                references.begin(), references.end(),
                [&affected_variable_set](const auto& name) {
                    return affected_variable_set.contains(name);
                });
        };
        for (const auto index : variable_rule_indices) {
            const auto& rule = css_rules[index];
            for (size_t declaration_index = 0U;
                 declaration_index < rule.declarations().size();
                 ++declaration_index) {
                const auto& declaration =
                    rule.declarations()[declaration_index];
                if (declaration.name.starts_with("--")
                    || !declaration_references_any_variable(
                        rule, declaration_index, affected_variable_set)) continue;
                const auto mask = css::property_mask(declaration.name);
                if (mask != 0U && declaration.name != "all"
                    && !css::generated_property_inherits_by_default(
                        declaration.name)) {
                    exact_changed_properties |= mask;
                }
            }
        }
        for (const auto& declaration : inline_variable_declarations) {
            if (declaration.name.starts_with("--")
                || !references_affected_variable(declaration.value)) continue;
            const auto mask = css::property_mask(declaration.name);
            if (mask != 0U && declaration.name != "all"
                && !css::generated_property_inherits_by_default(
                    declaration.name)) {
                exact_changed_properties |= mask;
            }
        }
        std::vector<size_t> inheritance_rule_indices;
        inheritance_rule_indices.reserve(
            css_inheritance_candidate_rules.size());
        for (const auto& candidate : css_inheritance_candidate_rules) {
            if (exact_changed_properties == 0U) break;
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
            ++css_media_inheritance_candidate_index_visits;
#endif
            if ((candidate.properties & exact_changed_properties) == 0U
                || candidate.index >= css_rules.size()) continue;
            const auto& rule = css_rules[candidate.index];
            if (!rule.media_matches) continue;
            inheritance_rule_indices.push_back(candidate.index);
        }
        const css::rule_subject_index inheritance_index(
            css_rules, inheritance_rule_indices);
        std::vector<request> requests;
        struct inheritance_consumer final {
            dom_node* node;
            uint64_t properties;
        };
        std::vector<inheritance_consumer> inheritance_consumers;
        std::vector<dom_node*> shadow_hosts;
        const auto inherited_properties = [&](
            dom_node& node,
            std::span<const css_rule* const> rules) {
            uint64_t result = 0U;
            const auto inspect = [&](const css_declaration& declaration) {
                if (declaration.name.starts_with("--")) return;
                const auto mask = css::property_mask(declaration.name);
                if ((mask & exact_changed_properties) == 0U) return;
                const auto inherits = css::cascade_keyword_is(
                        declaration.value, "inherit")
                    || (declaration.value.find("var(") != std::string::npos
                        && css::cascade_keyword_is(
                            resolve_css_value(node, declaration.value),
                            "inherit"));
                if (inherits) result |= mask;
            };
            for (const auto* rule : rules) {
                for (const auto& declaration : rule->declarations()) {
                    inspect(declaration);
                }
            }
            for (const auto& [name, value] :
                 node.authored_style().declarations) {
                inspect({name, value,
                    node.authored_style().important_declarations.contains(name)});
            }
            return result;
        };
        const auto references_active_variables = [this](
            std::string_view value,
            const std::vector<std::string>& active_variables) {
            if (active_variables.empty()
                || value.find("var(") == std::string::npos) return false;
            const auto references = css_variable_references(value);
            return std::any_of(
                references.begin(), references.end(),
                [&active_variables](const auto& name) {
                    return std::find(
                        active_variables.begin(),
                        active_variables.end(),
                        name) != active_variables.end();
                });
        };
        std::vector<std::string> active_variables;
        size_t changed_rule_subjects = 0U;
        size_t variable_consumer_subjects = 0U;
        size_t inline_variable_consumer_subjects = 0U;
        const auto visit = [&](const auto& self, dom_node& node) -> void {
            const auto inherited_variable_count = active_variables.size();
            rule_effect effect;
            bool has_request = false;
            bool full_cascade = false;
            if (node.kind == dom_node_kind::element) {
                const auto candidates=changed_index.candidates(node,&node==active_element);
                const auto matching = css::match_candidates(
                    document,
                    node,
                    css_rules,
                    candidates,
                    [this](const dom_node& subject, const css_rule&,
                           const compiled_css_selector& selector) {
                        return compiled_css_selector_matches(subject, selector, selector.compounds.size()-1U);
                    },
                    [this](const dom_node& subject, const css_rule& rule) {
                        return css_rule_matches(subject, rule);
                    },
                    false);
                for (const auto* rule : matching.ordinary) {
                    const auto changed_effect = classify_rule_effect(*rule);
                    if (trace_transition && changed_effect.subtree) {
                        std::cerr << "WebScene media subtree rule: selector="
                            << rule->selector() << ", declarations=";
                        for (const auto& declaration : rule->declarations()) {
                            const auto mask = css::property_mask(declaration.name);
                            if (declaration.name == "all") {
                                std::cerr << declaration.name << "(all) ";
                            } else if (mask == 0U) {
                                std::cerr << declaration.name << "(unknown) ";
                            } else if (css::generated_property_inherits_by_default(
                                    declaration.name)) {
                                std::cerr << declaration.name << "(inherited) ";
                            }
                        }
                        std::cerr << '\n';
                    }
                    effect.subtree = effect.subtree || changed_effect.subtree;
                    effect.changed_properties |=
                        changed_effect.changed_properties;
                    effect.inherited_properties |=
                        changed_effect.inherited_properties;
                    for (const auto& variable :
                         changed_effect.changed_variables) {
                        append_variable(effect.changed_variables, variable);
                    }
                }
                has_request = !matching.ordinary.empty()
                    || !matching.pseudo.empty();
                full_cascade = has_request;
                if (has_request) ++changed_rule_subjects;
                for (const auto& variable : effect.changed_variables) {
                    append_variable(active_variables, variable);
                }
                if (!effect.subtree && !active_variables.empty()
                    && !variable_rule_indices.empty()) {
                    const auto variable_candidates = variable_index.candidates(
                        node, &node == active_element);
                    const auto variable_matching = css::match_candidates(
                        document,
                        node,
                        css_rules,
                        variable_candidates,
                        [this](const dom_node& subject, const css_rule&,
                               const compiled_css_selector& selector) {
                            return compiled_css_selector_matches(
                                subject,
                                selector,
                                selector.compounds.size() - 1U);
                        },
                        [this](const dom_node& subject, const css_rule& rule) {
                            return css_rule_matches(subject, rule);
                        });
                    // Establish custom-property aliases first so declarations
                    // later in the same matched rule can consume the alias.
                    bool grew = false;
                    do {
                        grew = false;
                        for (const auto* rule : variable_matching.ordinary) {
                            for (size_t declaration_index = 0U;
                                 declaration_index < rule->declarations().size();
                                 ++declaration_index) {
                                const auto& declaration =
                                    rule->declarations()[declaration_index];
                                if (!declaration.name.starts_with("--")
                                    || !declaration_references_any_variable(
                                        *rule,
                                        declaration_index,
                                        active_variables)) continue;
                                grew = append_variable(
                                    active_variables,
                                    declaration.name) || grew;
                            }
                        }
                        for (const auto& [name, value] :
                             node.authored_style().declarations) {
                            if (!name.starts_with("--")
                                || !references_active_variables(
                                    value, active_variables)) continue;
                            grew = append_variable(
                                active_variables, name) || grew;
                        }
                    } while (grew);
                    for (const auto* rule : variable_matching.ordinary) {
                        for (size_t declaration_index = 0U;
                             declaration_index < rule->declarations().size();
                             ++declaration_index) {
                            const auto& declaration =
                                rule->declarations()[declaration_index];
                            if (!declaration_references_any_variable(
                                    *rule,
                                    declaration_index,
                                    active_variables)) continue;
                            has_request = true;
                            ++variable_consumer_subjects;
                            classify_declaration(effect, declaration);
                        }
                    }
                    if (std::any_of(
                        variable_matching.pseudo.begin(),
                        variable_matching.pseudo.end(),
                        [this, &active_variables](const auto& pseudo) {
                            const auto* rule = pseudo.second;
                            for (size_t declaration_index = 0U;
                                 declaration_index < rule->declarations().size();
                                 ++declaration_index) {
                                if (this->declaration_references_any_variable(
                                        *rule,
                                        declaration_index,
                                        active_variables)) return true;
                            }
                            return false;
                        })) {
                        has_request = true;
                        full_cascade = true;
                    }
                }
                if (!effect.subtree && !active_variables.empty()) {
                    for (const auto& [name, value] :
                         node.authored_style().declarations) {
                        if (!references_active_variables(
                            value, active_variables)) continue;
                        has_request = true;
                        ++inline_variable_consumer_subjects;
                        classify_declaration(
                            effect,
                            {name, value,
                                node.authored_style().important_declarations.contains(
                                    name)});
                    }
                }
                for (const auto& variable : effect.changed_variables) {
                    append_variable(active_variables, variable);
                }
                if (has_request) {
                    requests.push_back(
                        {&node, effect.subtree, effect.changed_properties,
                            effect.inherited_properties, full_cascade});
                }
                if (exact_changed_properties != 0U
                    && !inheritance_rule_indices.empty()) {
                    const auto inheritance_candidates =
                        inheritance_index.candidates(
                            node, &node == active_element);
                    const auto inheritance_matching = css::match_candidates(
                        document,
                        node,
                        css_rules,
                        inheritance_candidates,
                        [this](const dom_node& subject, const css_rule&,
                               const compiled_css_selector& selector) {
                            return compiled_css_selector_matches(
                                subject,
                                selector,
                                selector.compounds.size() - 1U);
                        },
                        [this](const dom_node& subject, const css_rule& rule) {
                            return css_rule_matches(subject, rule);
                        });
                    const auto properties = inherited_properties(
                        node, inheritance_matching.ordinary);
                    if (properties != 0U) {
                        inheritance_consumers.push_back({&node, properties});
                    }
                } else if (exact_changed_properties != 0U) {
                    const auto properties = inherited_properties(node, {});
                    if (properties != 0U) {
                        inheritance_consumers.push_back({&node, properties});
                    }
                }
            }
            if (effect.subtree) {
                active_variables.resize(inherited_variable_count);
                return;
            }
            for (auto* child : node.children) {
                if (child == nullptr) continue;
                if (child != active_css_cascade_root
                    && inactive_css_cascades.contains(child)) {
                    continue;
                }
                self(self, *child);
            }
            const auto* shadow = document.shadow_dom(node);
            if (shadow != nullptr && shadow->root != nullptr) {
                shadow_hosts.push_back(&node);
                self(self, *shadow->root);
            }
            active_variables.resize(inherited_variable_count);
        };
        visit(visit, root);
        // A shadow tree owns another rule index. Preserve the prior whole-tree
        // behavior whenever an exact request could flow into that boundary.
        for (auto& current : requests) {
            if (current.subtree) continue;
            current.subtree = std::any_of(
                shadow_hosts.begin(), shadow_hosts.end(),
                [this, &current](const dom_node* host) {
                    return subtree_contains(*current.root, host);
                });
        }
        std::vector<dom_node*> subtree_roots;
        for (const auto& current : requests) {
            if (current.subtree) subtree_roots.push_back(current.root);
        }
        std::erase_if(requests, [this, &subtree_roots](const request& current) {
            return std::any_of(
                subtree_roots.begin(), subtree_roots.end(),
                [this, &current](const dom_node* ancestor) {
                    return ancestor != current.root
                        && subtree_contains(*ancestor, current.root);
                });
        });
        std::unordered_map<dom_node*, size_t> request_by_node;
        for (size_t index = 0; index < requests.size(); ++index) {
            request_by_node.emplace(requests[index].root, index);
        }
        // Exact inheritance consumers form a sparse dependency chain. Add all
        // of them relative to the original changed subject; ancestor ordering
        // below makes multi-level `inherit` chains observe fresh parent values.
        for (const auto& consumer : inheritance_consumers) {
            uint64_t affected = 0U;
            bool covered = false;
            for (auto* ancestor = document.event_parent(*consumer.node);
                 ancestor != nullptr;
                 ancestor = document.event_parent(*ancestor)) {
                const auto found = request_by_node.find(ancestor);
                if (found == request_by_node.end()) continue;
                const auto& source = requests[found->second];
                if (source.subtree) {
                    covered = true;
                    break;
                }
                affected |= source.changed_properties & consumer.properties;
            }
            if (covered || affected == 0U) continue;
            if (const auto found = request_by_node.find(consumer.node);
                found != request_by_node.end()) {
                requests[found->second].changed_properties |= affected;
                requests[found->second].full_cascade = true;
            } else {
                request_by_node.emplace(consumer.node, requests.size());
                requests.push_back(
                    {consumer.node, false, affected, 0U, true});
            }
        }
        const auto inherited_style_changes = [](const node_style& left,
                                                 const node_style& right,
                                                 uint64_t properties) {
            const auto& left_text = left.textual();
            const auto& right_text = right.textual();
            uint64_t changed = 0U;
            const auto record = [&](uint64_t property, bool equal) {
                if ((properties & property) != 0U && !equal) {
                    changed |= property;
                }
            };
            record(css::inline_color,
                left.foreground_rgba == right.foreground_rgba);
            record(css::inline_cursor,
                left_text.cursor == right_text.cursor);
            record(css::inline_font_size,
                left.font_size == right.font_size);
            record(css::inline_font_family,
                left_text.font_family == right_text.font_family);
            record(css::inline_font_smoothing,
                left_text.font_smoothing == right_text.font_smoothing);
            record(css::inline_font_weight,
                left.font_weight == right.font_weight);
            record(css::inline_line_height,
                left.line_height == right.line_height);
            record(css::inline_letter_spacing,
                left.letter_spacing == right.letter_spacing
                    && left.letter_spacing_specified
                        == right.letter_spacing_specified);
            record(css::inline_word_spacing,
                left.word_spacing == right.word_spacing
                    && left.word_spacing_specified
                        == right.word_spacing_specified);
            record(css::inline_text_align,
                left_text.text_align == right_text.text_align);
            record(css::inline_visibility,
                left.visibility_hidden == right.visibility_hidden
                    && left.visibility_specified
                        == right.visibility_specified);
            record(css::inline_white_space,
                left_text.white_space == right_text.white_space);
            return changed;
        };
        order_recascade_roots(requests);
        const auto application_started = trace_transition
            ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point{};
        std::vector<dom_node*> applied_subtree_roots;
        std::unordered_set<dom_node*> applied_nodes;
        std::unordered_map<dom_node*, uint64_t> conditional_masks;
        for (const auto& current : requests) {
            if (!current.subtree && current.inherited_properties != 0U) {
                conditional_masks[current.root] |= current.inherited_properties;
            }
        }
        const auto propagate_inherited = [&](
            const auto& self,
            dom_node& parent,
            uint64_t inherited_mask) -> void {
            const auto apply_child = [&](dom_node& child) {
                auto mask = inherited_mask;
                if (const auto found = conditional_masks.find(&child);
                    found != conditional_masks.end()) {
                    mask |= found->second;
                }
                const auto previous_style = child.style;
                apply_css_rules(child);
                applied_nodes.insert(&child);
                const auto changed = inherited_style_changes(
                    previous_style, child.style, mask);
                if (changed != 0U) {
                    self(self, child, changed);
                }
            };
            for (auto* child : parent.children) {
                if (child == nullptr) continue;
                if (child != active_css_cascade_root
                    && inactive_css_cascades.contains(child)) continue;
                apply_child(*child);
            }
            const auto* shadow = document.shadow_dom(parent);
            if (shadow != nullptr && shadow->root != nullptr) {
                apply_child(*shadow->root);
            }
        };
        size_t triggered_inherited_roots = 0U;
        size_t targeted_variable_replays = 0U;
        for (const auto& current : requests) {
            if (std::any_of(
                applied_subtree_roots.begin(), applied_subtree_roots.end(),
                [this, &current](const dom_node* ancestor) {
                    return subtree_contains(*ancestor, current.root);
                })) continue;
            if (current.subtree) {
                apply_css_rules_subtree(*current.root);
                applied_subtree_roots.push_back(current.root);
                continue;
            }
            if (applied_nodes.contains(current.root)) continue;
            const auto previous_targeted_style = current.root->style;
            if (!current.full_cascade
                && reapply_css_variable_dependent_rules(
                    *current.root, variable_recascade_plan)) {
                ++targeted_variable_replays;
                applied_nodes.insert(current.root);
                if (current.inherited_properties != 0U) {
                    const auto changed = inherited_style_changes(
                        previous_targeted_style,
                        current.root->style,
                        current.inherited_properties);
                    if (changed != 0U) {
                        ++triggered_inherited_roots;
                        propagate_inherited(
                            propagate_inherited,
                            *current.root,
                            changed);
                    }
                }
                continue;
            }
            if (current.inherited_properties == 0U) {
                apply_css_rules(*current.root);
                applied_nodes.insert(current.root);
                continue;
            }
            const auto previous_style = current.root->style;
            apply_css_rules(*current.root);
            applied_nodes.insert(current.root);
            const auto changed = inherited_style_changes(
                    previous_style,
                    current.root->style,
                    current.inherited_properties);
            if (changed == 0U) continue;
            ++triggered_inherited_roots;
            propagate_inherited(
                propagate_inherited,
                *current.root,
                changed);
        }
        if (trace_transition) {
            const auto planned = std::chrono::duration<double, std::milli>(
                application_started - transition_started).count();
            const auto applied = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - application_started).count();
            const auto subtrees = std::count_if(
                requests.begin(), requests.end(), [](const request& current) {
                    return current.subtree;
                });
            const auto conditional_inherited = std::count_if(
                requests.begin(), requests.end(), [](const request& current) {
                    return !current.subtree
                        && current.inherited_properties != 0U;
                });
            std::cerr << "WebScene media transition: plan-ms=" << planned
                << ", apply-ms=" << applied
                << ", changed-rules=" << changed_rules.size()
                << ", inheritance-rules=" << inheritance_rule_indices.size()
                << ", variable-rules=" << variable_rule_indices.size()
                << ", changed-rule-subjects=" << changed_rule_subjects
                << ", variable-consumer-declarations="
                << variable_consumer_subjects
                << ", inline-variable-consumer-declarations="
                << inline_variable_consumer_subjects
                << ", requests=" << requests.size()
                << ", subtree-requests=" << subtrees
                << ", conditional-inherited=" << conditional_inherited
                << ", triggered-inherited=" << triggered_inherited_roots
                << ", targeted-variable-replays="
                << targeted_variable_replays
                << ", exact-property-mask=0x" << std::hex
                << exact_changed_properties << std::dec << '\n';
        }
    }

    static bool property_change_propagates_to_descendants(
        std::string_view property)
    {
        return css::generated_property_inherits_by_default(property);
    }

    static bool rule_change_propagates_to_descendants(const css_rule& rule)
    {
        return std::any_of(
            rule.declarations().begin(),
            rule.declarations().end(),
            [](const css_declaration& declaration) {
                return !declaration.name.starts_with("--")
                    && property_change_propagates_to_descendants(
                        declaration.name);
            });
    }

    template<typename Request>
    static void order_recascade_roots(std::vector<Request>& requests)
    {
        if (requests.size() < 2U) return;
        // Independent transition plans can discover a consumer before another
        // plan discovers its variable-defining ancestor. Apply ancestors first
        // so inherited values never come from a previous cascade generation.
        std::unordered_map<const dom_node*, size_t> depths;
        for (const auto& request : requests) {
            size_t depth = 0;
            for (auto* node = request.root; node != nullptr; node = node->parent) ++depth;
            depths.emplace(request.root, depth);
        }
        std::stable_sort(requests.begin(), requests.end(), [&](const auto& a, const auto& b) {
            return depths.at(a.root) < depths.at(b.root);
        });
    }

    std::vector<dom_node*> css_invalidation_route_targets(dom_node& subject,
        const dom_node* context_root, const css::css_invalidation_route& route)
    {
        using step = css::css_invalidation_step;
        std::vector<dom_node*> frontier{&subject};
        for (const auto direction : route) {
            std::vector<dom_node*> next;
            std::unordered_set<dom_node*> seen;
            const auto append = [&](dom_node* candidate) {
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
                ++selector_invalidation_candidate_visits;
#endif
                if (candidate != nullptr && candidate->kind == dom_node_kind::element
                    && (candidate == context_root || !inactive_css_cascades.contains(candidate))
                    && seen.insert(candidate).second) next.push_back(candidate);
            };
            std::unordered_set<dom_node*> walked;
            const auto descendants = [&](const auto& self, dom_node& root) -> void {
                for (auto* child : root.children) {
                    if (child == nullptr || inactive_css_cascades.contains(child)
                        || !walked.insert(child).second) continue;
                    append(child);
                    self(self, *child);
                }
            };
            for (auto* origin : frontier) {
                if (direction == step::children) {
                    for (auto* child : origin->children) append(child);
                } else if (direction == step::descendants
                    || direction == step::inclusive_descendants) {
                    if (direction == step::inclusive_descendants) append(origin);
                    descendants(descendants, *origin);
                } else if (direction == step::parent || direction == step::ancestors) {
                    if (origin == context_root) continue;
                    for (auto* parent = origin->parent; parent != nullptr; parent = parent->parent) {
                        if (seen.contains(parent)) break;
                        append(parent);
                        if (direction == step::parent || parent == context_root) break;
                    }
                } else {
                    if (origin == context_root || origin->parent == nullptr) continue;
                    const auto& siblings = origin->parent->children;
                    const auto found = std::find(siblings.begin(), siblings.end(), origin);
                    if (found == siblings.end()) continue;
                    const bool backwards = direction == step::previous_sibling
                        || direction == step::preceding_siblings;
                    const bool adjacent = direction == step::previous_sibling
                        || direction == step::next_sibling;
                    if (backwards) {
                        for (auto it = found; it != siblings.begin();) {
                            auto* candidate = *--it;
                            if (candidate == nullptr || candidate->kind != dom_node_kind::element) continue;
                            append(candidate);
                            if (adjacent) break;
                        }
                    } else {
                        for (auto it = std::next(found); it != siblings.end(); ++it) {
                            auto* candidate = *it;
                            if (candidate == nullptr || candidate->kind != dom_node_kind::element) continue;
                            append(candidate);
                            if (adjacent) break;
                        }
                    }
                }
            }
            frontier = std::move(next);
            if (frontier.empty()) break;
        }
        return frontier;
    }
    template<typename DependencyScope, typename UsePreviousState,
        typename RestoreCurrentState>
    void recascade_selector_transition(
        dom_node& subject,
        std::span<const size_t> changed_rules,
        DependencyScope&& dependency_scope,
        UsePreviousState&& use_previous_state,
        RestoreCurrentState&& restore_current_state,
        bool structural_change = false,
        bool force_transition = false)
    {
        selector_match_batch_scope matching_scope(*this);
        const auto trace_transition =
            std::getenv("WEBSCENE_TRACE_RECASCADE_ROOTS") != nullptr;
        const auto transition_started = trace_transition
            ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point{};
        struct request final { dom_node* root; bool subtree; };
        std::vector<request> requests;
        const auto add_request = [&](dom_node* root, bool subtree) {
            if (root == nullptr) return;
            for (auto& existing : requests) {
                if (existing.root != root) continue;
                existing.subtree = existing.subtree || subtree;
                return;
            }
            if (std::any_of(requests.begin(), requests.end(),
                    [this, root](const request& existing) {
                        return existing.subtree
                            && subtree_contains(*existing.root, root);
                    })) return;
            if (subtree) {
                std::erase_if(requests, [this, root](const request& existing) {
                    return subtree_contains(*root, existing.root);
                });
            }
            requests.push_back({root, subtree});
        };
        auto* previous_cascade_root = const_cast<dom_node*>(active_css_cascade_root);
        auto* context_root = css_cascade_root_for_node(subject);
        if (context_root != nullptr) activate_css_cascade(context_root);
        const auto visit_candidates = [&](dom_node& root,
                                          std::string affected_compound,
                                          compiled_css_compound compiled_compound,
                                          bool subtree) {
            std::string pseudo_origin;
            if (split_pseudo_element_selector(
                    affected_compound, pseudo_origin) != 0
                && !pseudo_origin.empty()) {
                affected_compound = std::move(pseudo_origin);
                compiled_compound = compile_css_compound_selector(affected_compound);
            }
            const auto visit = [&](const auto& self, dom_node& node) -> void {
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
                ++selector_invalidation_candidate_visits;
#endif
                if (node.kind == dom_node_kind::element
                    && css_compound_selector_matches(node, compiled_compound)) {
                    add_request(&node, subtree);
                }
                for (auto* child : node.children) {
                    if (child == nullptr) continue;
                    if (child != active_css_cascade_root
                        && inactive_css_cascades.contains(child)) continue;
                    self(self, *child);
                }
                const auto* shadow = document.shadow_dom(node);
                if (shadow != nullptr && shadow->root != nullptr) {
                    self(self, *shadow->root);
                }
            };
            visit(visit, root);
        };
        const auto add_custom_property_consumers = [&](
            dom_node& root,
            const css_rule& changed_rule) {
            std::vector<std::string> pending;
            for (const auto& declaration : changed_rule.declarations()) {
                if (declaration.name.starts_with("--")) {
                    pending.push_back(declaration.name);
                }
            }
            std::unordered_set<std::string> visited;
            for (size_t cursor = 0; cursor < pending.size(); ++cursor) {
                const auto variable = pending[cursor];
                if (!visited.insert(variable).second) continue;
                const auto found = css_rules_by_variable_reference.find(variable);
                if (found != css_rules_by_variable_reference.end()) {
                    for (const auto consumer_index : found->second) {
                        if (consumer_index >= css_rules.size()) continue;
                        const auto& consumer = css_rules[consumer_index];
                        if (!consumer.media_matches
                            || consumer.compiled_selector().compounds.empty()) continue;
                        visit_candidates(
                            root,
                            consumer.compiled_selector().compounds.back(),
                            consumer.compiled_selector().compiled_compounds.back(),
                            rule_change_propagates_to_descendants(consumer));
                        for (size_t declaration_index = 0U;
                             declaration_index < consumer.declarations().size();
                             ++declaration_index) {
                            const auto& declaration =
                                consumer.declarations()[declaration_index];
                            if (declaration.name.starts_with("--")
                                && declaration_references_variable(
                                    consumer, declaration_index, variable)) {
                                pending.push_back(declaration.name);
                            }
                        }
                    }
                }
                const auto visit_inline = [&](const auto& self, dom_node& node) -> void {
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
                    ++selector_invalidation_candidate_visits;
#endif
                    for (const auto& [property, value] :
                         node.authored_style().declarations) {
                        if (!css_variable_references(value).contains(variable)) continue;
                        if (property.starts_with("--")) pending.push_back(property);
                        else add_request(
                            &node,
                            property_change_propagates_to_descendants(property));
                    }
                    for (auto* child : node.children) {
                        if (child != nullptr) self(self, *child);
                    }
                };
                visit_inline(visit_inline, root);
            }
        };
        const auto visit_related_candidates = [&](
            dom_node& trigger,
            const compiled_css_selector& selector,
            size_t trigger_component,
            const css_rule& changed_rule) {
            std::vector<dom_node*> frontier{&trigger};
            for (auto component = trigger_component;
                 component + 1U < selector.compounds.size(); ++component) {
                std::vector<dom_node*> next;
                auto affected_compound = selector.compounds[component + 1U];
                auto compiled_affected =
                    selector.compiled_compounds[component + 1U];
                std::string pseudo_origin;
                if (split_pseudo_element_selector(
                        affected_compound, pseudo_origin) != 0
                    && !pseudo_origin.empty()) {
                    affected_compound = std::move(pseudo_origin);
                    compiled_affected =
                        compile_css_compound_selector(affected_compound);
                }
                const auto append_if_matching = [&](dom_node* candidate) {
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
                    ++selector_invalidation_candidate_visits;
#endif
                    if (candidate == nullptr
                        || candidate->kind != dom_node_kind::element
                        || !css_compound_selector_matches(
                            *candidate, compiled_affected, nullptr, structural_change)) return;
                    if (std::find(next.begin(), next.end(), candidate) == next.end()) {
                        next.push_back(candidate);
                    }
                };
                for (auto* origin : frontier) {
                    if (origin == nullptr) continue;
                    const auto combinator = selector.combinators[component];
                    if (combinator == '>') {
                        for (auto* child : origin->children) append_if_matching(child);
                        continue;
                    }
                    if (combinator == '+' || combinator == '~') {
                        if (origin->parent == nullptr) continue;
                        const auto& siblings = origin->parent->children;
                        const auto found = std::find(
                            siblings.begin(), siblings.end(), origin);
                        if (found == siblings.end()) continue;
                        for (auto current = std::next(found);
                             current != siblings.end(); ++current) {
                            if (*current == nullptr
                                || (*current)->kind != dom_node_kind::element) continue;
                            append_if_matching(*current);
                            if (combinator == '+') break;
                        }
                        continue;
                    }
                    const auto visit_descendants = [&](const auto& self,
                                                       dom_node& root) -> void {
                        for (auto* child : root.children) {
                            if (child == nullptr) continue;
                            if (child != active_css_cascade_root
                                && inactive_css_cascades.contains(child)) continue;
                            append_if_matching(child);
                            self(self, *child);
                        }
                        const auto* shadow = document.shadow_dom(root);
                        if (shadow != nullptr && shadow->root != nullptr) {
                            append_if_matching(shadow->root);
                            self(self, *shadow->root);
                        }
                    };
                    visit_descendants(visit_descendants, *origin);
                }
                frontier = std::move(next);
                if (frontier.empty()) break;
            }
            for (auto* candidate : frontier) {
                add_request(candidate, rule_change_propagates_to_descendants(changed_rule));
                add_custom_property_consumers(*candidate, changed_rule);
            }
        };
        const auto route_targets = [&](const css::css_invalidation_route& route) {
            return css_invalidation_route_targets(subject, context_root, route);
        };
        struct trigger_transition final {
            uint8_t scope{0};
            std::vector<dom_node*> anchors;
            std::vector<bool> current;
            std::vector<bool> previous;
        };
        std::unordered_map<const compiled_css_compound*, trigger_transition> transitions;
        transitions.reserve(changed_rules.size());
        for (const auto index : changed_rules) {
            if (index >= css_rules.size()) continue;
            const auto& rule = css_rules[index];
            const auto& compiled = rule.payload->compiled_pseudo_origin.compounds.empty()
                ? rule.compiled_selector() : rule.payload->compiled_pseudo_origin;
            if (compiled.compounds.empty()
                || compiled.compiled_compounds.size() != compiled.compounds.size()
                || rule.payload->invalidation.size() != compiled.compounds.size()) continue;
            for (size_t component = 0; component < compiled.compounds.size(); ++component) {
                const auto* selector = &compiled.compiled_compounds[component];
                if (transitions.contains(selector)) continue;
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
                ++selector_invalidation_plan_lookups;
#endif
                const auto targets = dependency_scope(rule.payload->invalidation[component]);
                if (targets.scope == 0U) continue;
                trigger_transition transition;
                transition.scope = targets.scope;
                std::unordered_set<dom_node*> seen;
                const auto append = [&](dom_node* anchor) {
                    if (anchor != nullptr && anchor->kind == dom_node_kind::element
                        && seen.insert(anchor).second) transition.anchors.push_back(anchor);
                };
                if ((targets.scope & css::invalidation_subject) != 0U) append(&subject);
                if ((targets.scope & css::invalidation_ancestors) != 0U) {
                    for (auto* anchor : route_targets({css::css_invalidation_step::ancestors})) append(anchor);
                }
                for (const auto* route : targets.routes)
                    for (auto* anchor : route_targets(*route)) append(anchor);
                for (auto* anchor : transition.anchors)
                    transition.current.push_back(css_compound_selector_matches(
                        *anchor, *selector, nullptr, structural_change));
                transition.previous.resize(transition.anchors.size());
                if(force_transition)
                    for(size_t i=0;i<transition.current.size();++i)
                        transition.previous[i]=!transition.current[i];
                transitions.emplace(selector, std::move(transition));
            }
        }
        if (!transitions.empty() && !structural_change && !force_transition) {
            use_previous_state();
            matching_scope.reset();
            for (auto& [selector, transition] : transitions) {
                for (size_t i = 0; i < transition.anchors.size(); ++i)
                    transition.previous[i] = css_compound_selector_matches(*transition.anchors[i], *selector);
            }
            restore_current_state();
            matching_scope.reset();
        }
        std::vector<size_t> fallback_rules;
        for (const auto index : changed_rules) {
            if (index >= css_rules.size()) continue;
            const auto& rule = css_rules[index];
            const auto& compiled = rule.payload->compiled_pseudo_origin.compounds.empty()
                ? rule.compiled_selector() : rule.payload->compiled_pseudo_origin;
            if (compiled.compounds.empty()
                || compiled.compiled_compounds.size() != compiled.compounds.size()
                || rule.payload->invalidation.size() != compiled.compounds.size()) {
                fallback_rules.push_back(index);
                continue;
            }
            auto found_trigger = false;
            for (size_t component = 0; component < compiled.compounds.size(); ++component) {
                const auto found = transitions.find(&compiled.compiled_compounds[component]);
                if (found == transitions.end()) continue;
                found_trigger = true;
                const auto& transition = found->second;
                if ((transition.scope & css::invalidation_fallback) != 0U) {
                    fallback_rules.push_back(index);
                    break;
                }
                for (size_t i = 0; i < transition.anchors.size(); ++i) {
                    if (transition.current[i] == transition.previous[i]) continue;
                    auto& anchor = *transition.anchors[i];
                    if (component + 1U == compiled.compounds.size()) {
                        add_request(&anchor, rule_change_propagates_to_descendants(rule));
                        add_custom_property_consumers(anchor, rule);
                    } else {
                        visit_related_candidates(anchor, compiled, component, rule);
                    }
                }
            }
            if (!found_trigger) fallback_rules.push_back(index);
        }
        const auto collect_fallback = [&] {
            if (fallback_rules.empty()) return;
            const auto visit = [&](const auto& self, dom_node& node) -> void {
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
                ++selector_invalidation_fallback_visits;
#endif
                if (node.kind == dom_node_kind::element) {
                    const auto matching = css::match_candidates(
                        document,
                        node,
                        css_rules,
                        fallback_rules,
                        [this](const dom_node& candidate, const css_rule&,
                               const compiled_css_selector& selector) {
                            return compiled_css_selector_matches(candidate, selector, selector.compounds.size()-1U);
                        },
                        [this](const dom_node& candidate, const css_rule& rule) {
                            return css_rule_matches(candidate, rule);
                        });
                    auto subtree = false;
                    for (const auto* rule : matching.ordinary) {
                        subtree = subtree
                            || rule_change_propagates_to_descendants(*rule);
                        add_custom_property_consumers(node, *rule);
                    }
                    if (!matching.ordinary.empty() || !matching.pseudo.empty()) {
                        add_request(&node, subtree);
                    }
                }
                for (auto* child : node.children) {
                    if (child == nullptr) continue;
                    if (child != active_css_cascade_root
                        && inactive_css_cascades.contains(child)) continue;
                    self(self, *child);
                }
                const auto* shadow = document.shadow_dom(node);
                if (shadow != nullptr && shadow->root != nullptr) {
                    self(self, *shadow->root);
                }
            };
            if (context_root != nullptr) visit(visit, *context_root);
        };
        if (!fallback_rules.empty()) {
            collect_fallback();
            use_previous_state();
            matching_scope.reset();
            collect_fallback();
            restore_current_state();
            matching_scope.reset();
        }
        add_request(&subject, false);
        const auto application_started = trace_transition
            ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point{};
        if (style_recascade_batch_depth == 0U) order_recascade_roots(requests);
        for (const auto& current : requests) {
            if (style_recascade_batch_depth != 0U) {
                schedule_style_recascade(
                    *current.root,
                    current.subtree,
                    "selector-transition");
            } else if (current.subtree) {
                apply_css_rules_subtree(*current.root);
            } else {
                apply_css_rules(*current.root);
            }
        }
        if (previous_cascade_root != nullptr
            && previous_cascade_root != context_root) {
            activate_css_cascade(previous_cascade_root);
        }
        if (trace_transition) {
            const auto planned = std::chrono::duration<double, std::milli>(
                application_started - transition_started).count();
            const auto applied = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - application_started).count();
            const auto subtree_requests = std::count_if(
                requests.begin(), requests.end(), [](const request& current) {
                    return current.subtree;
                });
            std::cerr << "WebScene selector transition: plan-ms=" << planned
                << ", apply-ms=" << applied << ", rules=" << changed_rules.size()
                << ", triggers=" << transitions.size()
                << ", fallback=" << fallback_rules.size()
                << ", requests=" << requests.size()
                << ", subtrees=" << subtree_requests << '\n';
        }
    }

    bool queue_attribute_selector_transition(
        dom_node& subject,
        std::string_view name,
        const std::optional<std::string>& previous_value,
        const std::optional<std::string>& current_value)
    {
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        if (style_recascade_batch_depth != 0U
            && !flushing_attribute_selector_transitions) {
            ++attribute_transition_schedule_requests;
        }
#endif
        if (style_recascade_batch_depth != 0U
            && !flushing_attribute_selector_transitions) {
            auto& by_name = pending_attribute_selector_transition_index[&subject];
            const auto [entry, inserted] = by_name.try_emplace(
                std::string(name), pending_attribute_selector_transitions.size());
            if (!inserted) {
                auto& pending = pending_attribute_selector_transitions[entry->second];
                pending.current_value = current_value;
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
                ++attribute_transition_coalesced_requests;
#endif
                return true;
            }
            pending_attribute_selector_transitions.push_back(
                pending_attribute_selector_transition{
                    &subject,
                    std::string(name),
                    previous_value,
                    current_value});
            return true;
        }
        return false;
    }

    void recascade_attribute_selector_transition(
        dom_node& subject,
        std::string_view name,
        const std::optional<std::string>& previous_value,
        const std::optional<std::string>& current_value)
    {
        if (queue_attribute_selector_transition(subject, name, previous_value, current_value)) return;
        webscene_frame_trace::scope invalidation_trace(
            frame_trace,
            "attribute-invalidation-start",
            "attribute-invalidation-end",
            0);
        const auto indexed = css_invalidation_rules_by_attribute.find(std::string(name));
        const std::span<const size_t> changed_rules =
            indexed == css_invalidation_rules_by_attribute.end()
            ? std::span<const size_t>{}
            : std::span<const size_t>{indexed->second};
        const auto trace_invalidation =
            std::getenv("WEBSCENE_TRACE_RECASCADE_ROOTS") != nullptr;
        const auto invalidation_started = trace_invalidation
            ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point{};
        recascade_selector_transition(
            subject,
            changed_rules,
            [&](const css::css_compound_dependencies& dependencies) {
                css::css_invalidation_targets result;
                const auto found = dependencies.attributes.find(std::string(name));
                if (found != dependencies.attributes.end()) result.include(found->second);
                return result;
            },
            [&] {
                if (previous_value.has_value()) {
                    subject.attributes[std::string(name)] = *previous_value;
                } else {
                    subject.attributes.erase(std::string(name));
                }
                if (name == "id") subject.id_attribute = previous_value.value_or(std::string{});
            },
            [&] {
                if (current_value.has_value()) {
                    subject.attributes[std::string(name)] = *current_value;
                } else {
                    subject.attributes.erase(std::string(name));
                }
                if (name == "id") subject.id_attribute = current_value.value_or(std::string{});
            });
        if (trace_invalidation) {
            const auto elapsed = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - invalidation_started).count();
            std::cerr << "WebScene attribute invalidation: ms=" << elapsed
                << ", attribute=" << name << ", rules=" << changed_rules.size()
                << ", node=" << subject.id << ", tag=" << subject.tag << '\n';
        }
    }

    void recascade_form_value_selector_transition(
        dom_node& subject,
        std::string previous_value,
        std::string current_value,
        std::optional<bool> user_validity_interacted = std::nullopt,
        std::optional<bool> value_changed_by_user = std::nullopt)
    {
        if(previous_value!=current_value)
            dismiss_validation_message_after_constraint_mutation(subject);
        if (previous_value != current_value)
            mark_primary_selection_publication_dirty(&subject);
        document.mark_dirty();
        const auto empty_state_changed=previous_value.empty()!=current_value.empty();
        const auto previous_range=forms::range_state(subject,previous_value);
        const auto current_range=forms::range_state(subject,current_value);
        const auto range_state_changed=previous_range!=current_range;
        const auto previous_value_changed_by_user=
            subject.form_control().value_changed_by_user;
        const auto current_value_changed_by_user=value_changed_by_user.value_or(
            previous_value_changed_by_user);
        const auto previous_text_validity=forms::text_validity_state(
            document,subject,previous_value,previous_value_changed_by_user);
        const auto current_text_validity=forms::text_validity_state(
            document,subject,current_value,current_value_changed_by_user);
        const auto text_validity_changed=previous_text_validity!=current_text_validity;
        const auto previous_numeric_validity=forms::numeric_validity_state(
            document,subject,previous_value);
        const auto current_numeric_validity=forms::numeric_validity_state(
            document,subject,current_value);
        const auto numeric_validity_changed=
            previous_numeric_validity!=current_numeric_validity;
        const auto previous_user_validity=subject.form_control().user_validity_interacted;
        const auto current_user_validity=user_validity_interacted.value_or(previous_user_validity);
        const auto user_validity_changed=previous_user_validity!=current_user_validity;
        auto& subject_control=subject.mutable_form_control();
        subject_control.user_validity_interacted=current_user_validity;
        subject_control.value_changed_by_user=current_value_changed_by_user;
        if(!empty_state_changed && !range_state_changed && !text_validity_changed
            && !numeric_validity_changed && !user_validity_changed) return;
        webscene_frame_trace::scope invalidation_trace(
            frame_trace,
            "form-state-invalidation-start",
            "form-state-invalidation-end",
            0);
        std::vector<size_t> changed_rule_storage;
        const auto append_rules=[&](std::string_view dependency) {
            const auto indexed=css_invalidation_rules_by_attribute.find(
                std::string(dependency));
            if(indexed!=css_invalidation_rules_by_attribute.end())
                changed_rule_storage.insert(changed_rule_storage.end(),
                    indexed->second.begin(),indexed->second.end());
        };
        if(empty_state_changed || text_validity_changed || numeric_validity_changed)
            append_rules("$live-form-value");
        if(range_state_changed) append_rules("$live-form-range");
        if(user_validity_changed) append_rules("$live-form-user-validity");
        std::ranges::sort(changed_rule_storage);
        changed_rule_storage.erase(
            std::unique(changed_rule_storage.begin(),changed_rule_storage.end()),
            changed_rule_storage.end());
        const auto changed_rules=std::span<const size_t>{
            changed_rule_storage.data(),changed_rule_storage.size()};
        recascade_selector_transition(
            subject,
            changed_rules,
            [&](const css::css_compound_dependencies& dependencies) {
                css::css_invalidation_targets result;
                const auto include=[&](std::string_view dependency) {
                    const auto found=dependencies.attributes.find(std::string(dependency));
                    if(found!=dependencies.attributes.end()) result.include(found->second);
                };
                if(empty_state_changed || text_validity_changed
                    || numeric_validity_changed)
                    include("$live-form-value");
                if(range_state_changed) include("$live-form-range");
                if(user_validity_changed) include("$live-form-user-validity");
                return result;
            },
            [&] {
                auto& control=subject.mutable_form_control();
                control.value=previous_value;
                control.value_changed_by_user=previous_value_changed_by_user;
                control.user_validity_interacted=previous_user_validity;
            },
            [&] {
                auto& control=subject.mutable_form_control();
                control.value=current_value;
                control.value_changed_by_user=current_value_changed_by_user;
                control.user_validity_interacted=current_user_validity;
            });
    }

    void recascade_form_checkedness_selector_transition(
        dom_node& subject,
        bool previous_checkedness,
        bool current_checkedness)
    {
        document.mark_dirty();
        if(previous_checkedness==current_checkedness) return;
        std::vector<size_t> changed_rule_storage;
        const auto append_rules=[&](std::string_view dependency) {
            const auto indexed=css_invalidation_rules_by_attribute.find(
                std::string(dependency));
            if(indexed!=css_invalidation_rules_by_attribute.end())
                changed_rule_storage.insert(changed_rule_storage.end(),
                    indexed->second.begin(),indexed->second.end());
        };
        append_rules("$live-form-checkedness");
        append_rules("$live-form-radio-group-checkedness");
        std::ranges::sort(changed_rule_storage);
        changed_rule_storage.erase(
            std::unique(changed_rule_storage.begin(),changed_rule_storage.end()),
            changed_rule_storage.end());
        const auto changed_rules=std::span<const size_t>{
            changed_rule_storage.data(),changed_rule_storage.size()};
        recascade_selector_transition(
            subject,
            changed_rules,
            [](const css::css_compound_dependencies& dependencies) {
                css::css_invalidation_targets result;
                for(const auto* dependency : {
                        "$live-form-checkedness",
                        "$live-form-radio-group-checkedness"}) {
                    const auto found=dependencies.attributes.find(dependency);
                    if(found!=dependencies.attributes.end()) result.include(found->second);
                }
                return result;
            },
            [&] {
                auto& control=subject.mutable_form_control();
                control.checkedness_initialized=true;
                control.checkedness=previous_checkedness;
            },
            [&] {
                auto& control=subject.mutable_form_control();
                control.checkedness_initialized=true;
                control.checkedness=current_checkedness;
            });
    }

    void recascade_radio_group_checkedness_selector_transition(
        dom_node& subject,
        dom_node& changed_radio,
        bool previous_checkedness,
        bool current_checkedness)
    {
        document.mark_dirty();
        if(previous_checkedness==current_checkedness) return;
        const auto indexed=css_invalidation_rules_by_attribute.find(
            "$live-form-radio-group-checkedness");
        const std::span<const size_t> changed_rules=indexed==
                css_invalidation_rules_by_attribute.end()
            ? std::span<const size_t>{}
            : std::span<const size_t>{indexed->second};
        recascade_selector_transition(
            subject,
            changed_rules,
            [](const css::css_compound_dependencies& dependencies) {
                css::css_invalidation_targets result;
                const auto found=dependencies.attributes.find(
                    "$live-form-radio-group-checkedness");
                if(found!=dependencies.attributes.end()) result.include(found->second);
                return result;
            },
            [&] {
                auto& control=changed_radio.mutable_form_control();
                control.checkedness_initialized=true;
                control.checkedness=previous_checkedness;
            },
            [&] {
                auto& control=changed_radio.mutable_form_control();
                control.checkedness_initialized=true;
                control.checkedness=current_checkedness;
            });
    }

    struct radio_group_selector_state final {
        dom_node* radio{nullptr};
        bool value_missing{false};
        bool indeterminate{false};
    };

    std::vector<radio_group_selector_state> capture_radio_group_selector_state()
    {
        std::vector<radio_group_selector_state> result;
        if(!css_invalidation_rules_by_attribute.contains(
                "$live-form-radio-group-checkedness")) return result;
        const auto visit=[&](const auto& recurse,dom_node& node)->void {
            if(forms::input_type_is(node,"radio") && is_connected(node)) {
                result.push_back({
                    &node,
                    forms::radio_group_value_missing(document,node),
                    css::indeterminate_matches(document,node)});
            }
            for(auto* child:node.children)
                if(child!=nullptr) recurse(recurse,*child);
            const auto* shadow=document.shadow_dom(node);
            if(shadow!=nullptr && shadow->root!=nullptr)
                recurse(recurse,*shadow->root);
        };
        visit(visit,document.body());
        return result;
    }

    bool radio_group_attribute_may_change_membership(
        const dom_node& node,std::string_view name) const
    {
        return (node.tag=="input"
                && (name=="name" || name=="type" || name=="form"
                    || name=="required"))
            || (node.tag=="form" && name=="id");
    }

    void recascade_radio_group_selector_state_changes(
        const std::vector<radio_group_selector_state>& previous)
    {
        if(previous.empty()) return;
        const auto indexed=css_invalidation_rules_by_attribute.find(
            "$live-form-radio-group-checkedness");
        if(indexed==css_invalidation_rules_by_attribute.end()) return;
        const std::span<const size_t> changed_rules{indexed->second};
        std::vector<dom_node*> changed;
        for(const auto& state:previous) {
            if(state.radio==nullptr || !is_connected(*state.radio)
                || !forms::input_type_is(*state.radio,"radio")) continue;
            const auto value_missing=forms::radio_group_value_missing(
                document,*state.radio);
            const auto indeterminate=css::indeterminate_matches(
                document,*state.radio);
            if(value_missing!=state.value_missing
                || indeterminate!=state.indeterminate)
                changed.push_back(state.radio);
        }
        std::ranges::sort(changed);
        changed.erase(std::unique(changed.begin(),changed.end()),changed.end());
        style_recascade_batch_scope style_batch(*this);
        for(auto* radio:changed) {
            recascade_selector_transition(
                *radio,
                changed_rules,
                [](const css::css_compound_dependencies& dependencies) {
                    css::css_invalidation_targets result;
                    const auto found=dependencies.attributes.find(
                        "$live-form-radio-group-checkedness");
                    if(found!=dependencies.attributes.end())
                        result.include(found->second);
                    return result;
                },
                [] {},
                [] {},
                false,
                true);
        }
    }

    void recascade_select_validity_selector_change(
        dom_node& select,std::optional<bool> previous_value_missing=std::nullopt)
    {
        dismiss_validation_message_after_constraint_mutation(select);
        document.mark_dirty();
        if(select.tag!="select" || !is_connected(select)) return;
        const auto current_value_missing=forms::select_value_missing(select);
        if(previous_value_missing && *previous_value_missing==current_value_missing)
            return;
        const auto indexed=css_invalidation_rules_by_attribute.find(
            "$live-form-select-selectedness");
        if(indexed==css_invalidation_rules_by_attribute.end()) return;
        const std::span<const size_t> changed_rules{indexed->second};
        recascade_selector_transition(
            select,
            changed_rules,
            [](const css::css_compound_dependencies& dependencies) {
                css::css_invalidation_targets result;
                const auto found=dependencies.attributes.find(
                    "$live-form-select-selectedness");
                if(found!=dependencies.attributes.end()) result.include(found->second);
                return result;
            },
            [] {},
            [] {},
            false,
            true);
    }

    void recascade_containing_select_validity(dom_node& node)
    {
        auto* select=node.tag=="select"?&node:forms::containing_select(node);
        if(select!=nullptr) recascade_select_validity_selector_change(*select);
    }

    void recascade_class_selector_transition(
        dom_node& subject,
        const std::optional<std::string>& previous_value,
        const std::optional<std::string>& current_value)
    {
        if (queue_attribute_selector_transition(subject, "class", previous_value, current_value)) return;
        webscene_frame_trace::scope invalidation_trace(
            frame_trace, "class-invalidation-start", "class-invalidation-end", 0);
        std::unordered_set<std::string> changed_classes;
        const auto tokens = [](std::string_view classes) {
            std::unordered_set<std::string> result;
            std::istringstream stream{std::string(classes)};
            for (std::string token; stream >> token;) result.insert(std::move(token));
            return result;
        };
        const auto previous_classes = tokens(previous_value.value_or(std::string{}));
        const auto current_classes = tokens(current_value.value_or(std::string{}));
        for (const auto& name : previous_classes)
            if (!current_classes.contains(name)) changed_classes.insert(name);
        for (const auto& name : current_classes)
            if (!previous_classes.contains(name)) changed_classes.insert(name);
        std::vector<size_t> changed_rules;
        if (const auto indexed = css_invalidation_rules_by_attribute.find("class");
            indexed != css_invalidation_rules_by_attribute.end()) {
            changed_rules = indexed->second;
        }
        for (const auto& class_name : changed_classes) {
            const auto indexed = css_invalidation_rules_by_class.find(class_name);
            if (indexed == css_invalidation_rules_by_class.end()) continue;
            changed_rules.insert(
                changed_rules.end(), indexed->second.begin(), indexed->second.end());
        }
        std::sort(changed_rules.begin(), changed_rules.end());
        changed_rules.erase(
            std::unique(changed_rules.begin(), changed_rules.end()), changed_rules.end());
        recascade_selector_transition(
            subject,
            changed_rules,
            [&](const css::css_compound_dependencies& dependencies) {
                css::css_invalidation_targets result;
                if (const auto found = dependencies.attributes.find("class");
                    found != dependencies.attributes.end()) result.include(found->second);
                for (const auto& name : changed_classes) {
                    const auto found = dependencies.classes.find(name);
                    if (found != dependencies.classes.end()) result.include(found->second);
                }
                return result;
            },
            [&] {
                subject.class_name = previous_value.value_or(std::string{});
                if (previous_value.has_value()) subject.attributes["class"] = *previous_value;
                else subject.attributes.erase("class");
            },
            [&] {
                subject.class_name = current_value.value_or(std::string{});
                if (current_value.has_value()) subject.attributes["class"] = *current_value;
                else subject.attributes.erase("class");
            });
    }

    bool is_connected(const dom_node& node)
    {
        auto* root = &node;
        for (auto* current = &node; current != nullptr; current = current->parent) {
            if (current == &document.body()) return true;
            root = current;
        }
        if (detached_document_roots.contains(root->id)) return true;
        // A provisional browsing-context body can only be the root of this
        // parent chain. Compare that root once instead of scanning every frame
        // body again for every ancestor queried by geometry-heavy libraries.
        for (const auto& [frame_id, provisional_body] : provisional_frame_bodies) {
            if (root != provisional_body) continue;
            const auto* frame = document.find_by_native_id(frame_id);
            for (auto* owner = frame; owner != nullptr; owner = owner->parent) {
                if (owner == &document.body()) return true;
            }
            return false;
        }
        return false;
    }

    void recascade_connected_subtree_now(
        dom_node& node,
        std::string_view reason = {})
    {
        selector_match_batch_scope matching_scope(*this);
        recascade_connected_subtree_with_matching_context(node, reason);
    }

    // Caller owns a matching scope over one immutable DOM/selector state.
    // Keeping sibling subtrees in that scope avoids rebuilding prefix and
    // positional indexes for every child of a completed insertion.
    void recascade_connected_subtree_with_matching_context(
        dom_node& node, std::string_view reason)
    {
        webscene_frame_trace::scope cascade_trace(frame_trace, "recascade-start", "recascade-end", 0);
        const auto trace_recascade_root =
            std::getenv("WEBSCENE_TRACE_RECASCADE_ROOTS") != nullptr;
        const auto trace_recascade_started = trace_recascade_root
            ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point{};
        if (!is_connected(node)) return;
        auto* previous_cascade_root = const_cast<dom_node*>(active_css_cascade_root);
        auto* node_cascade_root = css_cascade_root_for_node(node);
        if (node_cascade_root != nullptr) activate_css_cascade(node_cascade_root);
        retain_reconnected_subtree_wrappers(node);
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        const auto trace = profile_css;
        const auto candidate_checks_before = css_profile_candidate_checks;
        const auto matched_rules_before = css_profile_matched_rules;
        const auto declarations_before = css_profile_declaration_applications;
        const auto reset_nanoseconds_before = css_profile_reset_nanoseconds;
        const auto matching_nanoseconds_before = css_profile_matching_nanoseconds;
        const auto declaration_nanoseconds_before =
            css_profile_declarations_nanoseconds;
        const auto finalize_nanoseconds_before = css_profile_finalize_nanoseconds;
        if (trace
            && std::getenv("WEBSCENE_PROBE_TRACE_UNINDEXED_CSS") != nullptr
            && last_traced_unindexed_css_count != unindexed_css_rules.size()) {
            last_traced_unindexed_css_count = unindexed_css_rules.size();
            for (const auto index : unindexed_css_rules) {
                if (index < css_rules.size()) {
                    std::cerr << "[Native Engine Probe] CSS unindexed rule="
                        << index << ", selector=" << css_rules[index].selector() << '\n';
                }
            }
        }
        const auto started = trace ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point{};
        binding_callback_timer timer(profile_startup ? &startup_subtree_recascade : nullptr);
#else
        static_cast<void>(reason);
#endif
        apply_css_rules_subtree(node);
        if (trace_recascade_root) {
            const auto elapsed = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - trace_recascade_started).count();
            if (elapsed >= 5.0) {
                std::cerr << "WebScene recascade root: ms=" << elapsed
                    << ", node=" << node.id << ", tag=" << node.tag
                    << ", id=" << node.id_attribute << ", reason=" << reason
                    << ", class=" << node.class_name.substr(0, 180) << '\n';
            }
        }
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        if (trace) {
            const auto elapsed = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - started).count();
            if (elapsed >= 0.5) {
                size_t descendants = 0;
                std::vector<const dom_node*> pending{&node};
                while (!pending.empty()) {
                    const auto* current = pending.back();
                    pending.pop_back();
                    ++descendants;
                    pending.insert(pending.end(), current->children.begin(), current->children.end());
                }
                std::cerr << "[Native Engine Probe] CSS recascade "
                    << std::fixed << std::setprecision(3) << elapsed << " ms, nodes="
                    << descendants
                    << ", candidates="
                    << (css_profile_candidate_checks - candidate_checks_before)
                    << ", matched="
                    << (css_profile_matched_rules - matched_rules_before)
                    << ", declarations="
                    << (css_profile_declaration_applications - declarations_before)
                    << ", stages-ms="
                    << (css_profile_reset_nanoseconds - reset_nanoseconds_before)
                        / 1'000'000.0
                    << "/"
                    << (css_profile_matching_nanoseconds - matching_nanoseconds_before)
                        / 1'000'000.0
                    << "/"
                    << (css_profile_declarations_nanoseconds
                            - declaration_nanoseconds_before)
                        / 1'000'000.0
                    << "/"
                    << (css_profile_finalize_nanoseconds - finalize_nanoseconds_before)
                        / 1'000'000.0
                    << ", rules=" << css_rules.size()
                    << ", unindexed=" << unindexed_css_rules.size()
                    << ", tag=" << node.tag << ", id=" << node.id_attribute
                    << ", reason=" << reason
                    << ", class=" << node.class_name.substr(0, 180) << '\n';
            }
        }
#endif
        if (previous_cascade_root != nullptr
            && previous_cascade_root != node_cascade_root) {
            activate_css_cascade(previous_cascade_root);
        }
        reconcile_validation_message_eligibility(node);
    }

    void schedule_style_recascade(
        dom_node& root,
        bool subtree,
        std::string_view reason,
        std::string custom_property = {})
    {
        if (!is_connected(root)) return;
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        ++style_recascade_schedule_requests;
#endif
        auto* cascade_root = css_cascade_root_for_node(root);
        for (auto& pending : pending_style_recascades) {
            if (pending.root != &root || pending.cascade_root != cascade_root) continue;
            pending.subtree = pending.subtree || subtree;
            if (pending.custom_property != custom_property) pending.custom_property.clear();
            if (pending.reason.empty() && !reason.empty()) pending.reason = reason;
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
            ++style_recascade_coalesced_requests;
#endif
            return;
        }
        for (auto& pending : pending_style_recascades) {
            if (!pending.subtree || pending.cascade_root != cascade_root
                || pending.root == nullptr || !subtree_contains(*pending.root, &root))
                continue;
            // An ancestor request now covers another mutation as well.
            pending.custom_property.clear();
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
            ++style_recascade_coalesced_requests;
#endif
            return;
        }
        if (subtree) {
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
            const auto pending_before = pending_style_recascades.size();
#endif
            if (std::any_of(pending_style_recascades.begin(), pending_style_recascades.end(),
                    [this, &root, cascade_root](const auto& pending) {
                        return pending.root != nullptr && pending.cascade_root == cascade_root
                            && subtree_contains(root, pending.root);
                    })) custom_property.clear();
            std::erase_if(
                pending_style_recascades,
                [this, &root, cascade_root](const pending_style_recascade& pending) {
                    return pending.root != nullptr
                        && pending.cascade_root == cascade_root
                        && subtree_contains(root, pending.root);
                });
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
            style_recascade_coalesced_requests +=
                pending_before - pending_style_recascades.size();
#endif
        }
        pending_style_recascades.push_back(
            pending_style_recascade{
                &root,
                cascade_root,
                subtree,
                std::string(reason),
                std::move(custom_property)});
    }

    void erase_pending_stylesheet_replacement(uint32_t owner_id)
    {
        std::erase_if(
            pending_stylesheet_replacements,
            [&](const auto& pending) {
                if (pending.owner_id != owner_id) return false;
                pending_stylesheet_replacement_bytes -= std::min(
                    pending_stylesheet_replacement_bytes,
                    pending.source.size());
                return true;
            });
    }

    void rebuild_cascade_layer_order_for_stylesheet(
        uint32_t owner_id,
        const std::vector<std::string>& replacement_names)
    {
        const auto previous_orders = css_cascade_layer_orders;
        std::vector<std::pair<uint32_t, std::string>> ordered_pairs;
        ordered_pairs.reserve(previous_orders.size());
        for (const auto& [name, order] : previous_orders) {
            ordered_pairs.emplace_back(order, name);
        }
        std::sort(ordered_pairs.begin(), ordered_pairs.end());
        std::vector<std::string> ordered_names;
        ordered_names.reserve(ordered_pairs.size() + replacement_names.size());
        for (const auto& [order, name] : ordered_pairs) {
            static_cast<void>(order);
            ordered_names.push_back(name);
        }

        const auto known_owner = css_stylesheet_layer_names.find(owner_id);
        const auto previous_names = known_owner == css_stylesheet_layer_names.end()
            ? std::vector<std::string>{} : known_owner->second;
        const auto owner_scope = cascade_layer_scope_id(owner_id);
        std::vector<std::string> previous_keys;
        previous_keys.reserve(previous_names.size());
        for (const auto& name : previous_names) {
            previous_keys.push_back(
                std::to_string(owner_scope) + ':' + name);
        }
        std::unordered_set<std::string> declared_elsewhere;
        for (const auto& [candidate_owner, names] : css_stylesheet_layer_names) {
            if (candidate_owner == owner_id
                || cascade_layer_scope_id(candidate_owner) != owner_scope) {
                continue;
            }
            for (const auto& name : names) {
                declared_elsewhere.insert(
                    std::to_string(owner_scope) + ':' + name);
            }
        }

        auto insertion = std::numeric_limits<size_t>::max();
        std::vector<std::string> retained;
        retained.reserve(ordered_names.size() + replacement_names.size());
        for (const auto& name : ordered_names) {
            const auto owned = std::find(
                previous_keys.begin(), previous_keys.end(), name)
                != previous_keys.end();
            if (owned && insertion == std::numeric_limits<size_t>::max()) {
                insertion = retained.size();
            }
            if (owned && !declared_elsewhere.contains(name)) continue;
            retained.push_back(name);
        }
        if (insertion == std::numeric_limits<size_t>::max()) {
            insertion = retained.size();
        }
        insertion = std::min(insertion, retained.size());
        for (const auto& name : replacement_names) {
            const auto key = std::to_string(owner_scope) + ':' + name;
            if (name.empty()
                || std::find(retained.begin(), retained.end(), key)
                    != retained.end()) {
                continue;
            }
            retained.insert(retained.begin() + static_cast<std::ptrdiff_t>(insertion), key);
            ++insertion;
        }

        if (replacement_names.empty()) css_stylesheet_layer_names.erase(owner_id);
        else css_stylesheet_layer_names.insert_or_assign(owner_id, replacement_names);
        css_cascade_layer_orders.clear();
        uint32_t next_order = 1U;
        for (const auto& name : retained) {
            css_cascade_layer_orders.emplace(name, next_order++);
        }

        std::unordered_map<uint32_t, std::string> previous_names_by_order;
        for (const auto& [name, order] : previous_orders) {
            previous_names_by_order.emplace(order, name);
        }
        std::vector<uint32_t> anonymous_orders;
        for (const auto& rule : css_rules) {
            if (rule.cascade_layer_order != 0U
                && !previous_names_by_order.contains(rule.cascade_layer_order)
                && std::find(
                    anonymous_orders.begin(), anonymous_orders.end(),
                    rule.cascade_layer_order) == anonymous_orders.end()) {
                anonymous_orders.push_back(rule.cascade_layer_order);
            }
        }
        std::sort(anonymous_orders.begin(), anonymous_orders.end());
        std::unordered_map<uint32_t, uint32_t> remapped_anonymous_orders;
        for (const auto old_order : anonymous_orders) {
            remapped_anonymous_orders.emplace(old_order, next_order++);
        }
        for (auto& rule : css_rules) {
            if (rule.cascade_layer_order == 0U) continue;
            if (const auto named = previous_names_by_order.find(
                    rule.cascade_layer_order);
                named != previous_names_by_order.end()) {
                const auto remapped = css_cascade_layer_orders.find(named->second);
                rule.cascade_layer_order = remapped == css_cascade_layer_orders.end()
                    ? 0U : remapped->second;
            } else if (const auto anonymous = remapped_anonymous_orders.find(
                    rule.cascade_layer_order);
                anonymous != remapped_anonymous_orders.end()) {
                rule.cascade_layer_order = anonymous->second;
            }
        }
        next_css_cascade_layer_order = next_order;
    }

    void flush_pending_stylesheet_replacements()
    {
        if (pending_stylesheet_replacements.empty()) return;
        auto pending = std::move(pending_stylesheet_replacements);
        pending_stylesheet_replacements.clear();
        pending_stylesheet_replacement_bytes = 0U;
        auto* previous_root = const_cast<dom_node*>(active_css_cascade_root);
        std::vector<dom_node*> roots;
        for (const auto& replacement : pending) {
            auto* owner = document.find_by_native_id(replacement.owner_id);
            auto* root = document.find_by_native_id(replacement.cascade_root_id);
            if (owner == nullptr || root == nullptr || !is_connected(*owner)) {
                continue;
            }
            if (std::find(roots.begin(), roots.end(), root)
                == roots.end()) {
                roots.push_back(root);
            }
        }
        for (auto* root : roots) {
            activate_css_cascade(root);
            std::unordered_set<uint32_t> owners;
            for (const auto& replacement : pending) {
                auto* owner = document.find_by_native_id(replacement.owner_id);
                if (replacement.cascade_root_id == root->id
                    && owner != nullptr
                    && is_connected(*owner)) {
                    owners.insert(owner->id);
                }
            }
            std::erase_if(css_rules, [&](const auto& rule) {
                return owners.contains(rule.stylesheet_owner_id);
            });
            for (const auto& replacement : pending) {
                if (replacement.cascade_root_id == root->id
                    && owners.contains(replacement.owner_id)) {
                    rebuild_cascade_layer_order_for_stylesheet(
                        replacement.owner_id, replacement.layer_names);
                }
            }
            for (auto& replacement : pending) {
                auto* owner = document.find_by_native_id(replacement.owner_id);
                if (replacement.cascade_root_id != root->id
                    || owner == nullptr
                    || !is_connected(*owner)
                    || replacement.source.empty()) {
                    continue;
                }
                const auto* shadow_root =
                    document.containing_shadow_root(*owner);
                add_stylesheet(
                    std::move(replacement.source),
                    replacement.base_address,
                    owner->id,
                    shadow_root == nullptr ? 0U : shadow_root->id);
            }
            // Adopted sheets are the final author sheets for their document or
            // shadow root. Reappend them after any owner-backed replacement so
            // later <style> mutation cannot move normal author rules past them.
            reappend_adopted_stylesheets_for_cascade_root(root->id);
            rebuild_css_rule_indexes_and_root_variables();
            schedule_style_recascade(*root, true, "stylesheet-change");
        }
        if (previous_root != nullptr) activate_css_cascade(previous_root);
    }

    void flush_pending_style_recascades()
    {
        flush_pending_stylesheet_replacements();
        if (!pending_attribute_selector_transitions.empty()) {
            auto transitions = std::move(pending_attribute_selector_transitions);
            pending_attribute_selector_transitions.clear();
            pending_attribute_selector_transition_index.clear();
            // Plan the net first-to-final selector transition once per
            // node/attribute pair. Keep a temporary batch depth so those plans
            // still merge their recascade roots before any CSS is applied.
            ++style_recascade_batch_depth;
            flushing_attribute_selector_transitions = true;
            const auto set_state = [](const pending_attribute_selector_transition& transition,
                                       const std::optional<std::string>& value) {
                if (transition.subject == nullptr) return;
                if (value.has_value()) transition.subject->attributes[transition.name] = *value;
                else transition.subject->attributes.erase(transition.name);
                if (transition.name == "class")
                    transition.subject->class_name = value.value_or(std::string{});
                else if (transition.name == "id")
                    transition.subject->id_attribute = value.value_or(std::string{});
            };
            // Evaluate a sequence from the complete pre-batch state to the
            // final state. Testing each old attribute against all other final
            // attributes misses removals such as [a][b] when both disappear.
            // No style is applied and no author callback runs during planning.
            for (const auto& transition : transitions)
                set_state(transition, transition.previous_value);
            for (const auto& transition : transitions) {
                set_state(transition, transition.current_value);
                if (transition.subject == nullptr
                    || !is_connected(*transition.subject)
                    || transition.previous_value == transition.current_value) {
                    continue;
                }
                if (transition.name == "class") {
                    recascade_class_selector_transition(*transition.subject,
                        transition.previous_value, transition.current_value);
                } else {
                    recascade_attribute_selector_transition(
                        *transition.subject, transition.name,
                        transition.previous_value, transition.current_value);
                }
            }
            flushing_attribute_selector_transitions = false;
            --style_recascade_batch_depth;
        }
        if (pending_style_recascades.empty()) return;
        auto pending = std::move(pending_style_recascades);
        pending_style_recascades.clear();
        order_recascade_roots(pending);
        selector_match_batch_scope matching_scope(*this);
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        ++style_recascade_flush_batches;
        style_recascade_flush_roots += pending.size();
#endif
        for (const auto& recascade : pending) {
            if (recascade.root == nullptr || !is_connected(*recascade.root)) continue;
            if (recascade.subtree) {
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
                ++style_recascade_flush_subtree_roots;
#endif
                if (recascade.custom_property.empty()
                    || !recascade_dimension_custom_property(*recascade.root, recascade.custom_property))
                    recascade_connected_subtree_now(*recascade.root, recascade.reason);
            } else {
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
                ++style_recascade_flush_node_roots;
#endif
                // A flush is a synchronous CSSOM/layout barrier even when it
                // occurs inside a wider task batch. Apply the queued node now;
                // routing it back through the batching wrapper merely requeues
                // it and exposes stale computed style until the task returns.
                recascade_connected_node_now(*recascade.root);
            }
        }
    }

    void recascade_custom_property(dom_node& node, const std::string& name)
    {
        if (style_recascade_batch_depth != 0U) {
            schedule_style_recascade(node, true, "style.custom-property", name);
        } else if (!recascade_dimension_custom_property(node, name)) {
            recascade_connected_subtree_now(node, "style.custom-property");
        }
    }

    void recascade_connected_subtree(
        dom_node& node,
        std::string_view reason = {})
    {
        if (style_recascade_batch_depth != 0U) {
            binding_callback_timer timer(
                nullptr,
                profile_named("style-recascade/subtree-scheduled"));
            schedule_style_recascade(node, true, reason);
            return;
        }
        binding_callback_timer timer(
            nullptr,
            profile_named("style-recascade/subtree-immediate"));
        recascade_connected_subtree_now(node, reason);
    }

    void recascade_connected_node_now(dom_node& node)
    {
        if (!is_connected(node)) return;
        auto* previous_cascade_root = const_cast<dom_node*>(active_css_cascade_root);
        auto* node_cascade_root = css_cascade_root_for_node(node);
        if (node_cascade_root != nullptr) activate_css_cascade(node_cascade_root);
        apply_css_rules(node);
        if (previous_cascade_root != nullptr
            && previous_cascade_root != node_cascade_root) {
            activate_css_cascade(previous_cascade_root);
        }
    }

    void recascade_connected_node(dom_node& node)
    {
        if (!is_connected(node)) return;
        if (style_recascade_batch_depth != 0U) {
            binding_callback_timer timer(
                nullptr,
                profile_named("style-recascade/node-scheduled"));
            schedule_style_recascade(node, false, {});
            return;
        }
        binding_callback_timer timer(
            nullptr,
            profile_named("style-recascade/node-immediate"));
        recascade_connected_node_now(node);
    }

    std::vector<size_t> child_list_rule_candidates(dom_node& parent, const dom_node* context_root)
    {
        std::unordered_set<size_t> selected;
        const auto append = [&](const std::vector<size_t>& rules) {
            for (const auto rule : rules) {
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
                ++selector_invalidation_candidate_visits;
#endif
                selected.insert(rule);
            }
        };
        const auto lookup = [&](const auto& index, const auto& key) {
            if (index.empty()) return;
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
            ++selector_invalidation_plan_lookups;
#endif
            const auto found = index.find(key);
            if (found != index.end()) append(found->second);
        };
        // This phase neither exchanges cascades nor invokes author code. Do not
        // copy the entire index: only the selected rule IDs cross that boundary.
        for (const auto& bucket : css_child_list_index) {
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
            ++selector_invalidation_plan_lookups;
#endif
            for (const auto* node : css_invalidation_route_targets(parent, context_root, bucket.route)) {
                if (node == nullptr || node->kind != dom_node_kind::element) continue;
                append(bucket.universal);
                lookup(bucket.by_id, node->id_attribute);
                // Fold keys conservatively for HTML; the full matcher retains
                // XML case sensitivity and verifies all remaining constraints.
                if (!bucket.by_tag.empty()) lookup(bucket.by_tag, css::ascii_lower(node->tag));
                if (node->tag == "body" && (node->parent == nullptr || node->parent->tag == "iframe")
                    && std::none_of(node->children.begin(), node->children.end(),
                        [](const auto* child) { return child != nullptr && child->tag == "html"; }))
                    lookup(bucket.by_tag, std::string{"html"});
                if (!bucket.by_attribute.empty()) {
                    for (const auto& [name, value] : node->attributes) {
                        static_cast<void>(value);
                        lookup(bucket.by_attribute, css::ascii_lower(name));
                    }
                }
                if (!bucket.by_class.empty())
                    for (const auto& token : class_tokens(*node)) lookup(bucket.by_class, token);
            }
        }
        std::vector<size_t> result(selected.begin(), selected.end());
        std::sort(result.begin(), result.end());
        return result;
    }

    void recascade_changed_child_list(dom_node& parent, dom_node* inserted = nullptr)
    {
        if (!is_connected(parent)) return;
        auto* previous_root = const_cast<dom_node*>(active_css_cascade_root);
        auto* root = css_cascade_root_for_node(parent);
        if (root != nullptr) activate_css_cascade(root);
        const auto rules = child_list_rule_candidates(parent, root);
        if (!rules.empty()) {
            recascade_selector_transition(parent, rules,
                [](const css::css_compound_dependencies& component) {
                    css::css_invalidation_targets targets;
                    targets.include(component.child_list);
                    return targets;
                }, [] {}, [] {}, true);
        }
        if (previous_root != nullptr && previous_root != root) activate_css_cascade(previous_root);
        if (inserted != nullptr) recascade_connected_subtree(*inserted, "inserted-child");
        // The transition collector already includes its subject.
        if (rules.empty()) recascade_connected_node(parent);
        recascade_containing_select_validity(parent);
    }

    void recascade_completed_child_list(dom_node& parent,
        const std::vector<dom_node*>& inserted,
        const std::vector<dom_node*>& previous_parents = {})
    {
        std::unordered_set<dom_node*> seen{&parent};
        for (auto* previous : previous_parents)
            if (previous != nullptr && seen.insert(previous).second)
                recascade_changed_child_list(*previous);
        recascade_changed_child_list(parent);
        selector_match_batch_scope matching_scope(*this);
        for (auto* child : inserted)
            if (child != nullptr && child->parent == &parent) {
                if (style_recascade_batch_depth != 0U)
                    recascade_connected_subtree(*child, "inserted-child");
                else {
                    binding_callback_timer timer(nullptr, profile_named("style-recascade/subtree-immediate"));
                    recascade_connected_subtree_with_matching_context(*child, "inserted-child");
                }
            }
    }

    void recascade_interaction_transition(
        dom_node* previous,
        dom_node* next,
        const std::vector<hover_selector_dependency>& dependencies,
        std::string_view state_name,
        std::string_view reason)
    {
        if (previous == next || dependencies.empty()) return;

        std::vector<dom_node*> previous_path;
        std::vector<dom_node*> next_path;
        for (auto* node = previous; node != nullptr; node = node->parent) {
            previous_path.push_back(node);
        }
        for (auto* node = next; node != nullptr; node = node->parent) {
            next_path.push_back(node);
        }
        while (!previous_path.empty() && !next_path.empty()
            && previous_path.back() == next_path.back()) {
            previous_path.pop_back();
            next_path.pop_back();
        }
        std::vector<dom_node*> changed = std::move(previous_path);
        changed.insert(changed.end(), next_path.begin(), next_path.end());

        struct invalidation_request final
        {
            dom_node* root{nullptr};
            bool subtree{false};
        };
        std::vector<invalidation_request> requests;
        const auto add_request = [&](dom_node* root, bool subtree) {
            if (root == nullptr || !is_connected(*root)) return;
            for (auto& existing : requests) {
                if (existing.root != root) continue;
                existing.subtree = existing.subtree || subtree;
                return;
            }
            if (std::any_of(
                    requests.begin(),
                    requests.end(),
                    [this, root](const invalidation_request& existing) {
                        return existing.subtree
                            && subtree_contains(*existing.root, root);
                    })) {
                return;
            }
            if (subtree) {
                std::erase_if(
                    requests,
                    [this, root](const invalidation_request& existing) {
                        return subtree_contains(*root, existing.root);
                    });
            }
            requests.push_back({root, subtree});
        };

        for (auto* changed_node : changed) {
            if (changed_node == nullptr) continue;
            for (const auto& dependency : dependencies) {
                if (dependency.trigger_compound != "*"
                    && !css_compound_selector_matches(
                        *changed_node,
                        dependency.compiled_trigger)) {
                    continue;
                }
                switch (dependency.scope) {
                case hover_invalidation_scope::subject:
                    add_request(changed_node, false);
                    break;
                case hover_invalidation_scope::subtree: {
                    const auto add_affected = [&](dom_node& candidate) {
                        if (css_compound_selector_matches(
                                candidate,
                                dependency.affected_compound)) {
                            add_request(
                                &candidate,
                                dependency.propagate_to_descendants);
                        }
                    };
                    if (dependency.propagate_to_descendants
                        && dependency.affected_compound == "*"
                        && !dependency.affected_direct_children) {
                        add_request(changed_node, true);
                        break;
                    }
                    if (dependency.affected_direct_children) {
                        for (auto* child : changed_node->children) {
                            if (child != nullptr) add_affected(*child);
                        }
                    } else {
                        visit_subtree(*changed_node, [&](dom_node& candidate) {
                            if (&candidate != changed_node) add_affected(candidate);
                        });
                    }
                    break;
                }
                case hover_invalidation_scope::siblings: {
                    if (changed_node->parent == nullptr
                        || (dependency.propagate_to_descendants
                            && dependency.affected_compound == "*")) {
                        add_request(
                            changed_node->parent == nullptr
                                ? changed_node
                                : changed_node->parent,
                            true);
                        break;
                    }
                    for (auto* sibling : changed_node->parent->children) {
                        if (sibling != nullptr && sibling != changed_node
                            && css_compound_selector_matches(
                                *sibling,
                                dependency.affected_compound)) {
                            add_request(
                                sibling,
                                dependency.propagate_to_descendants);
                        }
                    }
                    break;
                }
                }
            }
        }

#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        const auto trace = profile_css;
        const auto started = trace
            ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point{};
        size_t recalculated_nodes = 0U;
#endif
        for (const auto& request : requests) {
            if (request.root == nullptr) continue;
            if (request.subtree) {
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
                if (trace) {
                    visit_subtree(*request.root, [&](const dom_node&) {
                        ++recalculated_nodes;
                    });
                }
#endif
                recascade_connected_subtree(*request.root, reason);
            } else {
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
                if (trace) ++recalculated_nodes;
#endif
                apply_css_rules(*request.root);
            }
        }
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        if (trace) {
            const auto elapsed = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - started).count();
            std::cerr << "[Native Engine Probe] CSS " << state_name
                << " invalidation "
                << std::fixed << std::setprecision(3) << elapsed
                << " ms, changed=" << changed.size()
                << ", dependencies=" << dependencies.size()
                << ", roots=" << requests.size()
                << ", nodes=" << recalculated_nodes
                << ", reason=" << reason << '\n';
        }
#endif
    }

    void recascade_hover_transition(
        dom_node* previous,
        dom_node* next,
        std::string_view reason)
    {
        recascade_interaction_transition(
            previous,
            next,
            hover_selector_dependencies,
            "hover",
            reason);
    }

    void recascade_active_transition(
        dom_node* previous,
        dom_node* next,
        std::string_view reason)
    {
        recascade_interaction_transition(
            previous,
            next,
            active_selector_dependencies,
            "active",
            reason);
    }

    void rebuild_root_variables()
    {
#if defined(WEBSCENE_NATIVE_ENGINE_MEDIA_REFRESH_BENCHMARK_COUNTERS)
        media_refresh_root_variable_refreshes.fetch_add(1U, std::memory_order_relaxed);
#endif
        css::rebuild_root_variables(css_rules,css_variables,important_css_variables);
    }

    void rebuild_css_rule_indexes_and_root_variables()
    {
        css_rules_by_class.clear();
        css_rules_by_id.clear();
        css_rules_by_tag.clear();
        css_rules_by_attribute.clear();
        css_rules_by_variable_reference.clear();
        css_inheritance_candidate_rules.clear();
        css_invalidation_rules_by_attribute.clear();
        css_invalidation_rules_by_class.clear();
        css_child_list_index.clear();
        css_focus_rules.clear();
        unindexed_css_rules.clear();
        hover_selector_dependencies.clear();
        active_selector_dependencies.clear();
        css_attribute_dependencies.clear();
        css_descendant_attribute_dependencies.clear();
        for (size_t index = 0; index < css_rules.size(); ++index) {
            index_css_rule(index);
        }
        rebuild_root_variables();
    }

    void recascade_after_stylesheet_change()
    {
        if (style_recascade_batch_depth != 0U) {
            schedule_style_recascade(
                active_root(),
                true,
                "stylesheet-change");
            document.mark_dirty();
            return;
        }
        apply_css_rules_subtree(active_root());
        document.mark_dirty();
    }

    bool deactivate_connected_stylesheets(dom_node& root, bool recascade = true)
    {
        std::unordered_set<uint32_t> owners;
        visit_subtree(root, [&](const dom_node& node) {
            if (node.tag == "style" || node.tag == "link") owners.insert(node.id);
        });
        if (owners.empty()) return false;
        const auto previous_size = css_rules.size();
        std::erase_if(css_rules, [&](const css_rule& rule) {
            return rule.stylesheet_owner_id != 0
                && owners.contains(rule.stylesheet_owner_id);
        });
        if (css_rules.size() == previous_size) return false;
        rebuild_css_rule_indexes_and_root_variables();
        if (recascade) recascade_after_stylesheet_change();
        return true;
    }

    void activate_connected_stylesheet(dom_node& node)
    {
        std::vector<dom_node*> stylesheet_owners;
        for (auto* ancestor = &node; ancestor != nullptr; ancestor = ancestor->parent) {
            if (ancestor->tag != "style" && ancestor->tag != "link") continue;
            stylesheet_owners.push_back(ancestor);
            break;
        }
        if (stylesheet_owners.empty()) {
            visit_subtree(node, [&](dom_node& candidate) {
                if (candidate.tag == "style" || candidate.tag == "link")
                    stylesheet_owners.push_back(&candidate);
            });
        }
        std::erase_if(stylesheet_owners, [&](const dom_node* owner) {
            return owner == nullptr || !is_connected(*owner);
        });
        if (stylesheet_owners.empty()) return;

        for (const auto* owner : stylesheet_owners) {
            if (owner != nullptr) erase_pending_stylesheet_replacement(owner->id);
        }
        for (auto* owner : stylesheet_owners)
            deactivate_connected_stylesheets(*owner, false);
        for (auto* owner : stylesheet_owners) {
            auto stylesheet = std::string{};
            if (const auto staged = stylesheet_cssom_sources.find(owner->id);
                staged != stylesheet_cssom_sources.end()) {
                stylesheet = staged->second;
            } else if (owner->tag == "style") {
                append_node_text(*owner, stylesheet);
            }
            if (!stylesheet.empty()) {
                const auto* shadow_root = document.containing_shadow_root(*owner);
                auto base = current_base_address();
                if (const auto known = stylesheet_cssom_addresses.find(owner->id);
                    known != stylesheet_cssom_addresses.end()) {
                    base = known->second;
                }
                add_stylesheet(
                    std::move(stylesheet),
                    base,
                    owner->id,
                    shadow_root == nullptr ? 0U : shadow_root->id);
            }
        }
        if (auto* root = css_cascade_root_for_node(node); root != nullptr) {
            reappend_adopted_stylesheets_for_cascade_root(root->id);
        }
        recascade_after_stylesheet_change();
    }

    static std::vector<std::string> parse_frame_stylesheets(const std::string& html)
    {
        std::vector<std::string> result;
        size_t cursor = 0;
        while (true) {
            const auto open = html.find("<link", cursor);
            if (open == std::string::npos) break;
            const auto close = html.find('>', open + 5U);
            if (close == std::string::npos) break;
            const auto tag = html.substr(open, close - open + 1U);
            const auto relation = extract_html_attribute(tag, "rel");
            const auto href = extract_html_attribute(tag, "href");
            if (relation == "stylesheet" && !href.empty()) result.push_back(href);
            cursor = close + 1U;
        }
        return result;
    }
