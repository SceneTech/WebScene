void native_document::signal_animation_frame(double timestamp_ms) noexcept
{
    if (std::isfinite(timestamp_ms) && timestamp_ms >= animation_frame_timestamp_ms_) {
        animation_frame_timestamp_ms_ = timestamp_ms;
    }
}

void native_document::update_style_animations(dom_node& node)
{
    if (!node.has_animation_runtime() && !node.style.has_animation_data()) {
        return;
    }
    active_animation_demand_cache_valid_ = false;
    auto& animation = node.mutable_animation_runtime();
    const auto length_equal = [](css_length left, css_length right) {
        return left.unit == right.unit
            && std::abs(left.value - right.value) < 0.001F
            && std::abs(left.pixel_offset - right.pixel_offset) < 0.001F;
    };
    const auto transform_target_changed = !length_equal(
            node.style.transform_translate_x,
            animation.transform_animation_target_translate_x)
        || !length_equal(
            node.style.transform_translate_y,
            animation.transform_animation_target_translate_y)
        || std::abs(node.style.transform_scale_x
            - animation.transform_animation_target_scale_x) >= 0.001F
        || std::abs(node.style.transform_scale_y
            - animation.transform_animation_target_scale_y) >= 0.001F
        || std::abs(node.style.transform_rotate_degrees
            - animation.transform_animation_target_degrees) >= 0.001F;
    if (!animation.transform_animation_initialized) {
        animation.transform_animation_initialized = true;
        animation.painted_transform_translate_x = node.style.transform_translate_x;
        animation.painted_transform_translate_y = node.style.transform_translate_y;
        animation.painted_transform_scale_x = node.style.transform_scale_x;
        animation.painted_transform_scale_y = node.style.transform_scale_y;
        animation.painted_transform_rotate_degrees = node.style.transform_rotate_degrees;
        animation.transform_animation_target_translate_x = node.style.transform_translate_x;
        animation.transform_animation_target_translate_y = node.style.transform_translate_y;
        animation.transform_animation_target_scale_x = node.style.transform_scale_x;
        animation.transform_animation_target_scale_y = node.style.transform_scale_y;
        animation.transform_animation_target_degrees = node.style.transform_rotate_degrees;
    } else if (transform_target_changed) {
        if (animation.transform_animation_active) {
            transition_events_.push_back({
                node.id,
                "transitioncancel",
                "transform",
                std::clamp(
                    static_cast<float>(animation_frame_timestamp_ms_
                        - animation.transform_animation_started_ms
                        - animation.transform_animation_delay_ms),
                    0.0F,
                    animation.transform_animation_duration_ms) / 1000.0F});
        }
        if (node.style.animations().transform_transition.duration_ms <= 0) {
        animation.painted_transform_translate_x = node.style.transform_translate_x;
        animation.painted_transform_translate_y = node.style.transform_translate_y;
        animation.painted_transform_scale_x = node.style.transform_scale_x;
        animation.painted_transform_scale_y = node.style.transform_scale_y;
        animation.painted_transform_rotate_degrees = node.style.transform_rotate_degrees;
        animation.transform_animation_target_translate_x = node.style.transform_translate_x;
        animation.transform_animation_target_translate_y = node.style.transform_translate_y;
        animation.transform_animation_target_scale_x = node.style.transform_scale_x;
        animation.transform_animation_target_scale_y = node.style.transform_scale_y;
        animation.transform_animation_target_degrees = node.style.transform_rotate_degrees;
        animation.transform_animation_active = false;
        } else {
            animation.transform_animation_from_translate_x = animation.painted_transform_translate_x;
            animation.transform_animation_from_translate_y = animation.painted_transform_translate_y;
            animation.transform_animation_target_translate_x = node.style.transform_translate_x;
            animation.transform_animation_target_translate_y = node.style.transform_translate_y;
            animation.transform_animation_from_scale_x = animation.painted_transform_scale_x;
            animation.transform_animation_from_scale_y = animation.painted_transform_scale_y;
            animation.transform_animation_target_scale_x = node.style.transform_scale_x;
            animation.transform_animation_target_scale_y = node.style.transform_scale_y;
            animation.transform_animation_from_degrees = animation.painted_transform_rotate_degrees;
            animation.transform_animation_target_degrees = node.style.transform_rotate_degrees;
            animation.transform_animation_duration_ms = node.style.animations().transform_transition.duration_ms;
            animation.transform_animation_delay_ms = node.style.animations().transform_transition.delay_ms;
            animation.transform_animation_x1 = node.style.animations().transform_transition.x1;
            animation.transform_animation_y1 = node.style.animations().transform_transition.y1;
            animation.transform_animation_x2 = node.style.animations().transform_transition.x2;
            animation.transform_animation_y2 = node.style.animations().transform_transition.y2;
            animation.transform_animation_started_ms = animation_frame_timestamp_ms_;
            animation.transform_animation_active = true;
            animation.transform_animation_start_event_sent = animation.transform_animation_delay_ms <= 0;
            transition_events_.push_back({node.id, "transitionrun", "transform", 0});
            if (animation.transform_animation_start_event_sent) {
                transition_events_.push_back({
                    node.id,
                    "transitionstart",
                    "transform",
                    std::min(-animation.transform_animation_delay_ms,
                        animation.transform_animation_duration_ms) / 1000.0F});
            }
        }
    }

    const auto update_inset_transition = [&] (
        const char* property_name,
        css_length target,
        const node_style::transition_timing& timing,
        css_length& painted,
        css_length& from,
        css_length& animation_target,
        float& duration_ms,
        float& delay_ms,
        float& x1,
        float& y1,
        float& x2,
        float& y2,
        double& started_ms,
        bool& initialized,
        bool& active,
        bool& start_event_sent) {
        if (!initialized) {
            initialized = true;
            painted = target;
            animation_target = target;
            return;
        }
        if (length_equal(target, animation_target)) return;
        if (active) {
            transition_events_.push_back({
                node.id,
                "transitioncancel",
                property_name,
                std::clamp(
                    static_cast<float>(animation_frame_timestamp_ms_
                        - started_ms - delay_ms),
                    0.0F,
                    duration_ms) / 1000.0F});
        }
        animation_target = target;
        const auto interpolable = painted.unit != length_unit::automatic
            && target.unit != length_unit::automatic;
        if (timing.duration_ms <= 0 || !interpolable) {
            painted = target;
            active = false;
            return;
        }
        from = painted;
        duration_ms = timing.duration_ms;
        delay_ms = timing.delay_ms;
        x1 = timing.x1;
        y1 = timing.y1;
        x2 = timing.x2;
        y2 = timing.y2;
        started_ms = animation_frame_timestamp_ms_;
        active = true;
        start_event_sent = delay_ms <= 0;
        transition_events_.push_back({node.id, "transitionrun", property_name, 0});
        if (start_event_sent) {
            transition_events_.push_back({
                node.id,
                "transitionstart",
                property_name,
                std::min(-delay_ms, duration_ms) / 1000.0F});
        }
    };
    update_inset_transition(
        "left",
        node.style.left,
        node.style.animations().left_transition,
        animation.painted_left,
        animation.left_animation_from,
        animation.left_animation_target,
        animation.left_animation_duration_ms,
        animation.left_animation_delay_ms,
        animation.left_animation_x1,
        animation.left_animation_y1,
        animation.left_animation_x2,
        animation.left_animation_y2,
        animation.left_animation_started_ms,
        animation.left_animation_initialized,
        animation.left_animation_active,
        animation.left_animation_start_event_sent);
    update_inset_transition(
        "top",
        node.style.top,
        node.style.animations().top_transition,
        animation.painted_top,
        animation.top_animation_from,
        animation.top_animation_target,
        animation.top_animation_duration_ms,
        animation.top_animation_delay_ms,
        animation.top_animation_x1,
        animation.top_animation_y1,
        animation.top_animation_x2,
        animation.top_animation_y2,
        animation.top_animation_started_ms,
        animation.top_animation_initialized,
        animation.top_animation_active,
        animation.top_animation_start_event_sent);

    const auto opacity_keyframe_changed =
        animation.opacity_keyframe_animation_signature
            != node.style.animations().opacity_keyframe_animation_signature;
    const auto rotation_keyframe_changed =
        animation.rotation_keyframe_animation_signature
            != node.style.animations().rotation_keyframe_animation_signature;
    if (opacity_keyframe_changed || rotation_keyframe_changed) {
        animation.keyframe_animation_end_event_sent = false;
    }
    if (opacity_keyframe_changed) {
        animation.opacity_keyframe_animation_signature =
            node.style.animations().opacity_keyframe_animation_signature;
        animation.opacity_keyframe_animation_started_ms = animation_frame_timestamp_ms_;
        animation.opacity_keyframe_animation_active =
            !animation.opacity_keyframe_animation_signature.empty()
            && node.style.animations().opacity_keyframes.size() >= 2U
            && node.style.animations().opacity_keyframe_duration_ms > 0
            && node.style.animations().opacity_keyframe_iterations != 0;
        if (!animation.opacity_keyframe_animation_active) {
            animation.painted_opacity = node.style.opacity;
        }
    }
    if (rotation_keyframe_changed) {
        animation.rotation_keyframe_animation_signature =
            node.style.animations().rotation_keyframe_animation_signature;
        animation.rotation_keyframe_animation_started_ms = animation_frame_timestamp_ms_;
        animation.rotation_keyframe_animation_active =
            !animation.rotation_keyframe_animation_signature.empty()
            && node.style.animations().rotation_keyframes.size() >= 2U
            && node.style.animations().opacity_keyframe_duration_ms > 0
            && node.style.animations().opacity_keyframe_iterations != 0;
        if (!animation.rotation_keyframe_animation_active) {
            animation.painted_transform_rotate_degrees = node.style.transform_rotate_degrees;
        }
    }

    if (animation.opacity_animation_active && node.style.animations().transition_property_value == "none") {
        transition_events_.push_back({node.id,"transitioncancel","opacity",
            std::clamp(static_cast<float>(animation_frame_timestamp_ms_-
                animation.opacity_animation_started_ms-animation.opacity_animation_delay_ms),
                0.0F,animation.opacity_animation_duration_ms)/1000.0F});
        animation.opacity_animation_active=false;
        animation.painted_opacity=node.style.opacity;
    }
    if (!animation.opacity_animation_initialized) {
        animation.opacity_animation_initialized = true;
        animation.painted_opacity = node.style.opacity;
        animation.opacity_animation_target = node.style.opacity;
    } else if (std::abs(node.style.opacity - animation.opacity_animation_target) >= 0.0001F) {
        if (animation.opacity_animation_active) {
            transition_events_.push_back({
                node.id,
                "transitioncancel",
                "opacity",
                std::clamp(
                    static_cast<float>(animation_frame_timestamp_ms_
                        - animation.opacity_animation_started_ms - animation.opacity_animation_delay_ms),
                    0.0F,
                    animation.opacity_animation_duration_ms) / 1000.0F});
        }
        animation.opacity_animation_target = node.style.opacity;
        if (node.style.animations().opacity_transition.duration_ms <= 0) {
            animation.painted_opacity = node.style.opacity;
            animation.opacity_animation_active = false;
        } else {
            animation.opacity_animation_from = animation.painted_opacity;
            animation.opacity_animation_duration_ms = node.style.animations().opacity_transition.duration_ms;
            animation.opacity_animation_delay_ms = node.style.animations().opacity_transition.delay_ms;
            animation.opacity_animation_x1 = node.style.animations().opacity_transition.x1;
            animation.opacity_animation_y1 = node.style.animations().opacity_transition.y1;
            animation.opacity_animation_x2 = node.style.animations().opacity_transition.x2;
            animation.opacity_animation_y2 = node.style.animations().opacity_transition.y2;
            animation.opacity_animation_started_ms = animation_frame_timestamp_ms_;
            animation.opacity_animation_active = true;
            animation.opacity_animation_start_event_sent = animation.opacity_animation_delay_ms <= 0;
            transition_events_.push_back({node.id, "transitionrun", "opacity", 0});
            if (animation.opacity_animation_start_event_sent) {
                transition_events_.push_back({
                    node.id,
                    "transitionstart",
                    "opacity",
                    std::min(-animation.opacity_animation_delay_ms,
                        animation.opacity_animation_duration_ms) / 1000.0F});
            }
        }
    }

    const auto resolved_color = [&]() {
        for (auto* current = &node; current != nullptr; current = current->parent) {
            if ((current->style.foreground_rgba & 0xFFU) != 0U) {
                return current->style.foreground_rgba;
            }
        }
        return 0xD1D4DCFFU;
    }();
    if (!animation.color_animation_initialized) {
        animation.color_animation_initialized = true;
        animation.painted_foreground_rgba = resolved_color;
        animation.color_animation_target_rgba = resolved_color;
    } else if (resolved_color != animation.color_animation_target_rgba) {
        if (animation.color_animation_active) {
            transition_events_.push_back({
                node.id,
                "transitioncancel",
                "color",
                std::clamp(
                    static_cast<float>(animation_frame_timestamp_ms_
                        - animation.color_animation_started_ms - animation.color_animation_delay_ms),
                    0.0F,
                    animation.color_animation_duration_ms) / 1000.0F});
        }
        animation.color_animation_target_rgba = resolved_color;
        if (node.style.animations().color_transition.duration_ms <= 0) {
            animation.painted_foreground_rgba = resolved_color;
            animation.color_animation_active = false;
        } else {
            animation.color_animation_from_rgba = animation.painted_foreground_rgba;
            animation.color_animation_duration_ms = node.style.animations().color_transition.duration_ms;
            animation.color_animation_delay_ms = node.style.animations().color_transition.delay_ms;
            animation.color_animation_x1 = node.style.animations().color_transition.x1;
            animation.color_animation_y1 = node.style.animations().color_transition.y1;
            animation.color_animation_x2 = node.style.animations().color_transition.x2;
            animation.color_animation_y2 = node.style.animations().color_transition.y2;
            animation.color_animation_started_ms = animation_frame_timestamp_ms_;
            animation.color_animation_active = true;
            animation.color_animation_start_event_sent = animation.color_animation_delay_ms <= 0;
            transition_events_.push_back({node.id, "transitionrun", "color", 0});
            if (animation.color_animation_start_event_sent) {
                transition_events_.push_back({
                    node.id,
                    "transitionstart",
                    "color",
                    std::min(-animation.color_animation_delay_ms,
                        animation.color_animation_duration_ms) / 1000.0F});
            }
        }

    }

    const auto resolved_background = node.style.background_current_color
        ? resolved_color
        : node.style.background_rgba;
    const auto& background_timing =
        node.style.animations().background_color_transition;
    if (animation.background_color_animation_active
        && background_timing.duration_ms <= 0) {
        transition_events_.push_back({
            node.id,
            "transitioncancel",
            "background-color",
            std::clamp(
                static_cast<float>(animation_frame_timestamp_ms_
                    - animation.background_color_animation_started_ms
                    - animation.background_color_animation_delay_ms),
                0.0F,
                animation.background_color_animation_duration_ms) / 1000.0F});
        animation.painted_background_rgba = resolved_background;
        animation.background_color_animation_target_rgba = resolved_background;
        animation.background_color_animation_active = false;
    }
    if (!animation.background_color_animation_initialized) {
        animation.background_color_animation_initialized = true;
        animation.painted_background_rgba = resolved_background;
        animation.background_color_animation_target_rgba = resolved_background;
    } else if (resolved_background
        != animation.background_color_animation_target_rgba) {
        if (animation.background_color_animation_active) {
            transition_events_.push_back({
                node.id,
                "transitioncancel",
                "background-color",
                std::clamp(
                    static_cast<float>(animation_frame_timestamp_ms_
                        - animation.background_color_animation_started_ms
                        - animation.background_color_animation_delay_ms),
                    0.0F,
                    animation.background_color_animation_duration_ms) / 1000.0F});
        }
        animation.background_color_animation_target_rgba = resolved_background;
        if (background_timing.duration_ms <= 0) {
            animation.painted_background_rgba = resolved_background;
            animation.background_color_animation_active = false;
        } else {
            animation.background_color_animation_from_rgba =
                animation.painted_background_rgba;
            animation.background_color_animation_duration_ms =
                background_timing.duration_ms;
            animation.background_color_animation_delay_ms = background_timing.delay_ms;
            animation.background_color_animation_x1 = background_timing.x1;
            animation.background_color_animation_y1 = background_timing.y1;
            animation.background_color_animation_x2 = background_timing.x2;
            animation.background_color_animation_y2 = background_timing.y2;
            animation.background_color_animation_started_ms = animation_frame_timestamp_ms_;
            animation.background_color_animation_active = true;
            animation.background_color_animation_start_event_sent =
                background_timing.delay_ms <= 0;
            transition_events_.push_back({
                node.id, "transitionrun", "background-color", 0});
            if (animation.background_color_animation_start_event_sent) {
                transition_events_.push_back({
                    node.id,
                    "transitionstart",
                    "background-color",
                    std::min(-background_timing.delay_ms,
                        background_timing.duration_ms) / 1000.0F});
            }
        }
    }

    const auto& filter_timing = node.style.animations().filter_transition;
    const auto& effects = node.style.textual().effect_values;
    const auto filter_entry = effects.find("filter");
    const auto filter_value = filter_entry == effects.end()
        ? std::string{"none"}
        : filter_entry->second;
    std::vector<retained_filter_function> filter_target;
    const auto filter_target_valid = parse_foreground_filters(
        node, filter_value, filter_target);
    if (animation.filter_animation_active && filter_timing.duration_ms <= 0) {
        transition_events_.push_back({
            node.id,
            "transitioncancel",
            "filter",
            std::clamp(
                static_cast<float>(animation_frame_timestamp_ms_
                    - animation.filter_animation_started_ms
                    - animation.filter_animation_delay_ms),
                0.0F,
                animation.filter_animation_duration_ms) / 1000.0F});
        animation.painted_filter_functions = filter_target_valid
            ? filter_target : std::vector<retained_filter_function>{};
        animation.filter_animation_active = false;
    }
    if (!animation.filter_animation_initialized) {
        animation.filter_animation_initialized = true;
        animation.filter_animation_target_value = filter_value;
        animation.painted_filter_functions = filter_target_valid
            ? filter_target : std::vector<retained_filter_function>{};
    } else {
        std::vector<retained_filter_function> previous_target;
        const auto previous_target_valid = parse_foreground_filters(
            node, animation.filter_animation_target_value, previous_target);
        const auto target_changed = filter_target_valid != previous_target_valid
            || (filter_target_valid && filter_target != previous_target);
        animation.filter_animation_target_value = filter_value;
        if (target_changed) {
            if (animation.filter_animation_active) {
                transition_events_.push_back({
                    node.id,
                    "transitioncancel",
                    "filter",
                    std::clamp(
                        static_cast<float>(animation_frame_timestamp_ms_
                            - animation.filter_animation_started_ms
                            - animation.filter_animation_delay_ms),
                        0.0F,
                        animation.filter_animation_duration_ms) / 1000.0F});
            }
            auto from = animation.filter_animation_active
                ? animation.painted_filter_functions
                : previous_target;
            auto target = filter_target;
            const auto identity_amount = [](uint32_t flags) {
                return flags == (1U << 31U) || flags == (1U << 29U)
                    || flags == (1U << 27U) ? 1.0F : 0.0F;
            };
            const auto make_compatible = [&](auto& left, auto& right) {
                if (left.size() > right.size()) {
                    for (size_t index = 0U; index < right.size(); ++index) {
                        if (left[index].flags != right[index].flags) return false;
                    }
                    for (size_t index = right.size(); index < left.size(); ++index) {
                        right.push_back({
                            left[index].flags, identity_amount(left[index].flags)});
                    }
                } else if (right.size() > left.size()) {
                    for (size_t index = 0U; index < left.size(); ++index) {
                        if (left[index].flags != right[index].flags) return false;
                    }
                    for (size_t index = left.size(); index < right.size(); ++index) {
                        left.push_back({
                            right[index].flags, identity_amount(right[index].flags)});
                    }
                }
                for (size_t index = 0U; index < left.size(); ++index) {
                    if (left[index].flags != right[index].flags) return false;
                }
                return true;
            };
            const auto interpolable = previous_target_valid && filter_target_valid
                && make_compatible(from, target);
            if (filter_timing.duration_ms <= 0 || !interpolable) {
                animation.painted_filter_functions = filter_target_valid
                    ? filter_target : std::vector<retained_filter_function>{};
                animation.filter_animation_active = false;
            } else {
                animation.filter_animation_from = std::move(from);
                animation.filter_animation_target = std::move(target);
                animation.filter_animation_target_is_none = filter_target.empty();
                animation.painted_filter_functions = animation.filter_animation_from;
                animation.filter_animation_duration_ms = filter_timing.duration_ms;
                animation.filter_animation_delay_ms = filter_timing.delay_ms;
                animation.filter_animation_x1 = filter_timing.x1;
                animation.filter_animation_y1 = filter_timing.y1;
                animation.filter_animation_x2 = filter_timing.x2;
                animation.filter_animation_y2 = filter_timing.y2;
                animation.filter_animation_started_ms = animation_frame_timestamp_ms_;
                animation.filter_animation_active = true;
                animation.filter_animation_start_event_sent =
                    filter_timing.delay_ms <= 0;
                transition_events_.push_back({node.id, "transitionrun", "filter", 0});
                if (animation.filter_animation_start_event_sent) {
                    transition_events_.push_back({
                        node.id,
                        "transitionstart",
                        "filter",
                        std::min(-filter_timing.delay_ms,
                            filter_timing.duration_ms) / 1000.0F});
                }
            }
        }
    }
}

bool native_document::advance_animations() noexcept
{
    // The worker can revisit active animations several times between host
    // requestAnimationFrame ticks. Re-evaluating an unchanged timestamp creates
    // redundant scene work and, on large component trees, can starve discrete
    // pointer input behind identical animation publications.
    if (animation_frame_timestamp_ms_ == last_animation_advance_timestamp_ms_) {
        return false;
    }
    last_animation_advance_timestamp_ms_ = animation_frame_timestamp_ms_;

    const auto cubic = [](float t, float first, float second) {
        const auto inverse = 1.0F - t;
        return 3.0F * inverse * inverse * t * first
            + 3.0F * inverse * t * t * second
            + t * t * t;
    };
    const auto cubic_derivative = [](float t, float first, float second) {
        const auto inverse = 1.0F - t;
        return 3.0F * inverse * inverse * first
            + 6.0F * inverse * t * (second - first)
            + 3.0F * t * t * (1.0F - second);
    };
    const auto timing_value = [&](float progress, float x1, float y1, float x2, float y2) {
        // CSS cubic-bezier timing is parameterized by x, so solve x(t) for the
        // elapsed-time progress before sampling y(t). Newton converges quickly
        // for ordinary curves; bisection keeps unusual but valid curves stable.
        auto parameter = progress;
        for (auto iteration = 0; iteration < 5; ++iteration) {
            const auto error = cubic(
                parameter,
                x1, x2) - progress;
            const auto derivative = cubic_derivative(
                parameter,
                x1, x2);
            if (std::abs(error) < 0.0001F || std::abs(derivative) < 0.0001F) break;
            parameter = std::clamp(parameter - error / derivative, 0.0F, 1.0F);
        }
        auto lower = 0.0F;
        auto upper = 1.0F;
        for (auto iteration = 0; iteration < 8; ++iteration) {
            const auto sampled = cubic(
                parameter,
                x1, x2);
            if (std::abs(sampled - progress) < 0.0001F) break;
            if (sampled < progress) lower = parameter;
            else upper = parameter;
            parameter = (lower + upper) * 0.5F;
        }
        return cubic(
            parameter,
            y1, y2);
    };
    const auto interpolate_length = [](css_length from, css_length target, float progress) {
        const auto normalized = [](css_length value) {
            if (value.unit == length_unit::automatic) value.unit = length_unit::pixels;
            return value;
        };
        from = normalized(from);
        target = normalized(target);
        if (from.unit == target.unit) {
            return css_length{
                from.value + (target.value - from.value) * progress,
                from.unit,
                from.pixel_offset + (target.pixel_offset - from.pixel_offset) * progress};
        }
        const auto is_linear_length = [](css_length value) {
            return value.unit == length_unit::pixels || value.unit == length_unit::percent;
        };
        if (is_linear_length(from) && is_linear_length(target)) {
            const auto percent = [](css_length value) {
                return value.unit == length_unit::percent ? value.value : 0.0F;
            };
            const auto pixels = [](css_length value) {
                return (value.unit == length_unit::pixels ? value.value : 0.0F)
                    + value.pixel_offset;
            };
            return css_length{
                percent(from) + (percent(target) - percent(from)) * progress,
                length_unit::percent,
                pixels(from) + (pixels(target) - pixels(from)) * progress};
        }
        return progress >= 1.0F ? target : from;
    };
    const auto length_equal = [](css_length left, css_length right) {
        return left.unit == right.unit
            && std::abs(left.value - right.value) < 0.001F
            && std::abs(left.pixel_offset - right.pixel_offset) < 0.001F;
    };
    const auto interpolate_color = [](uint32_t from, uint32_t target, float amount) {
        uint32_t result = 0;
        for (auto shift : {24U, 16U, 8U, 0U}) {
            const auto first = static_cast<float>((from >> shift) & 0xFFU);
            const auto last = static_cast<float>((target >> shift) & 0xFFU);
            const auto channel = static_cast<uint32_t>(std::clamp(
                std::lround(first + (last - first) * amount), 0L, 255L));
            result |= channel << shift;
        }
        return result;
    };
    auto advanced = false;
    auto layout_changed = false;
    for (auto& owner : nodes_) {
        auto& node = *owner;
        auto* animation_pointer = node.animation_runtime();
        // Retained JS wrappers can keep detached nodes alive after a component
        // replaces a subtree. Their CSS animation state must not keep the host
        // frame clock alive, and display:none descendants have no painted
        // timeline to advance. Reconnection/display restoration re-enters this
        // path with the existing authored animation state.
        if (animation_pointer == nullptr
            || !participates_in_animation_frame(node)) {
            continue;
        }
        auto& animation = *animation_pointer;
        const auto keyframe_paint_visible = intersects_visible_paint_area(node);
        const auto advance_inset_transition = [&] (
            const char* property_name,
            css_length& painted,
            const css_length& from,
            const css_length& target,
            float duration_ms,
            float delay_ms,
            float x1,
            float y1,
            float x2,
            float y2,
            double started_ms,
            bool& active,
            bool& start_event_sent) {
            if (!active) return false;
            const auto previous = painted;
            const auto elapsed_ms = static_cast<float>(
                animation_frame_timestamp_ms_ - started_ms - delay_ms);
            if (elapsed_ms >= 0 && !start_event_sent) {
                start_event_sent = true;
                transition_events_.push_back({
                    node.id, "transitionstart", property_name, 0});
            }
            const auto progress = std::clamp(
                elapsed_ms / std::max(0.001F, duration_ms),
                0.0F,
                1.0F);
            const auto eased = timing_value(progress, x1, y1, x2, y2);
            painted = interpolate_length(from, target, eased);
            if (progress >= 1.0F) {
                painted = target;
                active = false;
                transition_events_.push_back({
                    node.id,
                    "transitionend",
                    property_name,
                    duration_ms / 1000.0F});
            }
            return !length_equal(previous, painted);
        };
        const auto left_changed = advance_inset_transition(
            "left",
            animation.painted_left,
            animation.left_animation_from,
            animation.left_animation_target,
            animation.left_animation_duration_ms,
            animation.left_animation_delay_ms,
            animation.left_animation_x1,
            animation.left_animation_y1,
            animation.left_animation_x2,
            animation.left_animation_y2,
            animation.left_animation_started_ms,
            animation.left_animation_active,
            animation.left_animation_start_event_sent);
        const auto top_changed = advance_inset_transition(
            "top",
            animation.painted_top,
            animation.top_animation_from,
            animation.top_animation_target,
            animation.top_animation_duration_ms,
            animation.top_animation_delay_ms,
            animation.top_animation_x1,
            animation.top_animation_y1,
            animation.top_animation_x2,
            animation.top_animation_y2,
            animation.top_animation_started_ms,
            animation.top_animation_active,
            animation.top_animation_start_event_sent);
        layout_changed = layout_changed || left_changed || top_changed;
        advanced = advanced || left_changed || top_changed;
        if (animation.transform_animation_active) {
            const auto previous_translate_x = animation.painted_transform_translate_x;
            const auto previous_translate_y = animation.painted_transform_translate_y;
            const auto elapsed_ms = static_cast<float>(
                animation_frame_timestamp_ms_ - animation.transform_animation_started_ms
                - animation.transform_animation_delay_ms);
        if (elapsed_ms >= 0 && !animation.transform_animation_start_event_sent) {
            animation.transform_animation_start_event_sent = true;
            transition_events_.push_back({node.id, "transitionstart", "transform", 0});
        }
        const auto progress = std::clamp(
            elapsed_ms / std::max(0.001F, animation.transform_animation_duration_ms),
            0.0F,
            1.0F);
        const auto eased = timing_value(
            progress,
            animation.transform_animation_x1,
            animation.transform_animation_y1,
            animation.transform_animation_x2,
            animation.transform_animation_y2);
        animation.painted_transform_translate_x = interpolate_length(
            animation.transform_animation_from_translate_x,
            animation.transform_animation_target_translate_x,
            eased);
        animation.painted_transform_translate_y = interpolate_length(
            animation.transform_animation_from_translate_y,
            animation.transform_animation_target_translate_y,
            eased);
            const auto translated = [](css_length left, css_length right) {
                return left.unit != right.unit
                    || std::abs(left.value - right.value) >= 0.001F
                    || std::abs(left.pixel_offset - right.pixel_offset) >= 0.001F;
            };
            layout_changed = layout_changed
                || translated(previous_translate_x, animation.painted_transform_translate_x)
                || translated(previous_translate_y, animation.painted_transform_translate_y);
        animation.painted_transform_scale_x = animation.transform_animation_from_scale_x
            + (animation.transform_animation_target_scale_x
                - animation.transform_animation_from_scale_x) * eased;
        animation.painted_transform_scale_y = animation.transform_animation_from_scale_y
            + (animation.transform_animation_target_scale_y
                - animation.transform_animation_from_scale_y) * eased;
        animation.painted_transform_rotate_degrees =
            animation.transform_animation_from_degrees
            + (animation.transform_animation_target_degrees
                - animation.transform_animation_from_degrees) * eased;
        advanced = true;
        if (progress >= 1.0F) {
            animation.painted_transform_translate_x = animation.transform_animation_target_translate_x;
            animation.painted_transform_translate_y = animation.transform_animation_target_translate_y;
            animation.painted_transform_scale_x = animation.transform_animation_target_scale_x;
            animation.painted_transform_scale_y = animation.transform_animation_target_scale_y;
            animation.painted_transform_rotate_degrees = animation.transform_animation_target_degrees;
            animation.transform_animation_active = false;
            transition_events_.push_back({
                node.id,
                "transitionend",
                "transform",
                animation.transform_animation_duration_ms / 1000.0F});
        }
        }

        if (animation.opacity_animation_active) {
            const auto elapsed_ms = static_cast<float>(
                animation_frame_timestamp_ms_ - animation.opacity_animation_started_ms
                - animation.opacity_animation_delay_ms);
            if (elapsed_ms >= 0 && !animation.opacity_animation_start_event_sent) {
                animation.opacity_animation_start_event_sent = true;
                transition_events_.push_back({node.id, "transitionstart", "opacity", 0});
            }
            const auto progress = std::clamp(
                elapsed_ms / std::max(0.001F, animation.opacity_animation_duration_ms),
                0.0F,
                1.0F);
            const auto eased = timing_value(
                progress,
                animation.opacity_animation_x1,
                animation.opacity_animation_y1,
                animation.opacity_animation_x2,
                animation.opacity_animation_y2);
            animation.painted_opacity = animation.opacity_animation_from
                + (animation.opacity_animation_target - animation.opacity_animation_from) * eased;
            advanced = true;
            if (progress >= 1) {
                animation.painted_opacity = animation.opacity_animation_target;
                animation.opacity_animation_active = false;
                transition_events_.push_back({
                    node.id,
                    "transitionend",
                    "opacity",
                    animation.opacity_animation_duration_ms / 1000.0F});
            }
        }

        if (animation.color_animation_active) {
            const auto elapsed_ms = static_cast<float>(
                animation_frame_timestamp_ms_ - animation.color_animation_started_ms
                - animation.color_animation_delay_ms);
            if (elapsed_ms >= 0 && !animation.color_animation_start_event_sent) {
                animation.color_animation_start_event_sent = true;
                transition_events_.push_back({node.id, "transitionstart", "color", 0});
            }
            const auto progress = std::clamp(
                elapsed_ms / std::max(0.001F, animation.color_animation_duration_ms),
                0.0F,
                1.0F);
            const auto eased = timing_value(
                progress,
                animation.color_animation_x1,
                animation.color_animation_y1,
                animation.color_animation_x2,
                animation.color_animation_y2);
            animation.painted_foreground_rgba = interpolate_color(
                animation.color_animation_from_rgba,
                animation.color_animation_target_rgba,
                eased);
            advanced = true;
            if (progress >= 1) {
                animation.painted_foreground_rgba = animation.color_animation_target_rgba;
                animation.color_animation_active = false;
                transition_events_.push_back({
                    node.id,
                    "transitionend",
                    "color",
                    animation.color_animation_duration_ms / 1000.0F});
            }
        }

        if (animation.background_color_animation_active) {
            const auto elapsed_ms = static_cast<float>(
                animation_frame_timestamp_ms_
                - animation.background_color_animation_started_ms
                - animation.background_color_animation_delay_ms);
            if (elapsed_ms >= 0
                && !animation.background_color_animation_start_event_sent) {
                animation.background_color_animation_start_event_sent = true;
                transition_events_.push_back({
                    node.id, "transitionstart", "background-color", 0});
            }
            const auto progress = std::clamp(
                elapsed_ms
                    / std::max(0.001F,
                        animation.background_color_animation_duration_ms),
                0.0F,
                1.0F);
            const auto eased = timing_value(
                progress,
                animation.background_color_animation_x1,
                animation.background_color_animation_y1,
                animation.background_color_animation_x2,
                animation.background_color_animation_y2);
            animation.painted_background_rgba = interpolate_color(
                animation.background_color_animation_from_rgba,
                animation.background_color_animation_target_rgba,
                eased);
            advanced = true;
            if (progress >= 1) {
                animation.painted_background_rgba =
                    animation.background_color_animation_target_rgba;
                animation.background_color_animation_active = false;
                transition_events_.push_back({
                    node.id,
                    "transitionend",
                    "background-color",
                    animation.background_color_animation_duration_ms / 1000.0F});
            }
        }

        if (animation.filter_animation_active) {
            const auto elapsed_ms = static_cast<float>(
                animation_frame_timestamp_ms_ - animation.filter_animation_started_ms
                - animation.filter_animation_delay_ms);
            if (elapsed_ms >= 0 && !animation.filter_animation_start_event_sent) {
                animation.filter_animation_start_event_sent = true;
                transition_events_.push_back({node.id, "transitionstart", "filter", 0});
            }
            const auto progress = std::clamp(
                elapsed_ms / std::max(0.001F, animation.filter_animation_duration_ms),
                0.0F,
                1.0F);
            const auto eased = timing_value(
                progress,
                animation.filter_animation_x1,
                animation.filter_animation_y1,
                animation.filter_animation_x2,
                animation.filter_animation_y2);
            for (size_t index = 0U;
                 index < animation.painted_filter_functions.size(); ++index) {
                const auto from = animation.filter_animation_from[index].amount;
                const auto target = animation.filter_animation_target[index].amount;
                animation.painted_filter_functions[index].amount =
                    from + (target - from) * eased;
            }
            advanced = true;
            if (progress >= 1.0F) {
                if (animation.filter_animation_target_is_none) {
                    animation.painted_filter_functions.clear();
                } else {
                    animation.painted_filter_functions =
                        animation.filter_animation_target;
                }
                animation.filter_animation_active = false;
                transition_events_.push_back({
                    node.id,
                    "transitionend",
                    "filter",
                    animation.filter_animation_duration_ms / 1000.0F});
            }
        }

        auto keyframe_animation_completed = false;
        if (animation.opacity_keyframe_animation_active) {
            const auto elapsed_ms = static_cast<float>(
                animation_frame_timestamp_ms_
                - animation.opacity_keyframe_animation_started_ms
                - node.style.animations().opacity_keyframe_delay_ms);
            if (elapsed_ms < 0) {
                animation.painted_opacity = node.style.opacity;
                advanced = advanced || keyframe_paint_visible;
            } else {
                const auto duration = std::max(
                    0.001F, node.style.animations().opacity_keyframe_duration_ms);
                const auto iterations = node.style.animations().opacity_keyframe_iterations;
                const auto finite = std::isfinite(iterations);
                const auto total_duration = finite
                    ? duration * std::max(0.0F, iterations)
                    : 0.0F;
                if (finite && elapsed_ms >= total_duration) {
                    animation.painted_opacity =
                        node.style.animations().opacity_keyframe_fill_forwards
                        && !node.style.animations().opacity_keyframes.empty()
                        ? node.style.animations().opacity_keyframes.back().opacity
                        : node.style.opacity;
                    animation.opacity_keyframe_animation_active = false;
                    keyframe_animation_completed = true;
                    advanced = advanced || keyframe_paint_visible;
                } else {
                    auto cycle_progress = std::fmod(elapsed_ms, duration) / duration;
                    if (cycle_progress < 0) cycle_progress += 1.0F;
                    const auto& stops = node.style.animations().opacity_keyframes;
                    auto right = std::lower_bound(
                        stops.begin(), stops.end(), cycle_progress,
                        [](const node_style::opacity_keyframe& stop, float progress) {
                            return stop.offset < progress;
                        });
                    if (right == stops.begin()) {
                        animation.painted_opacity = right->opacity;
                    } else if (right == stops.end()) {
                        animation.painted_opacity = stops.back().opacity;
                    } else {
                        const auto& from = *(right - 1);
                        const auto span = std::max(
                            0.0001F, right->offset - from.offset);
                        const auto segment_progress = std::clamp(
                            (cycle_progress - from.offset) / span, 0.0F, 1.0F);
                        const auto eased = timing_value(
                            segment_progress,
                            node.style.animations().opacity_keyframe_x1,
                            node.style.animations().opacity_keyframe_y1,
                            node.style.animations().opacity_keyframe_x2,
                            node.style.animations().opacity_keyframe_y2);
                        animation.painted_opacity = from.opacity
                            + (right->opacity - from.opacity) * eased;
                    }
                    advanced = advanced || keyframe_paint_visible;
                }
            }
        }

        if (animation.rotation_keyframe_animation_active) {
            const auto elapsed_ms = static_cast<float>(
                animation_frame_timestamp_ms_
                - animation.rotation_keyframe_animation_started_ms
                - node.style.animations().opacity_keyframe_delay_ms);
            if (elapsed_ms < 0) {
                animation.painted_transform_rotate_degrees =
                    node.style.transform_rotate_degrees;
                advanced = advanced || keyframe_paint_visible;
            } else {
                const auto duration = std::max(
                    0.001F, node.style.animations().opacity_keyframe_duration_ms);
                const auto iterations = node.style.animations().opacity_keyframe_iterations;
                const auto finite = std::isfinite(iterations);
                const auto total_duration = finite
                    ? duration * std::max(0.0F, iterations)
                    : 0.0F;
                if (finite && elapsed_ms >= total_duration) {
                    animation.painted_transform_rotate_degrees =
                        node.style.transform_rotate_degrees;
                    animation.rotation_keyframe_animation_active = false;
                    keyframe_animation_completed = true;
                    advanced = advanced || keyframe_paint_visible;
                } else {
                    auto cycle_progress = std::fmod(elapsed_ms, duration) / duration;
                    if (cycle_progress < 0) cycle_progress += 1.0F;
                    const auto& stops = node.style.animations().rotation_keyframes;
                    auto right = std::lower_bound(
                        stops.begin(), stops.end(), cycle_progress,
                        [](const node_style::rotation_keyframe& stop, float progress) {
                            return stop.offset < progress;
                        });
                    if (right == stops.begin()) {
                        animation.painted_transform_rotate_degrees = right->degrees;
                    } else if (right == stops.end()) {
                        animation.painted_transform_rotate_degrees = stops.back().degrees;
                    } else {
                        const auto& from = *(right - 1);
                        const auto span = std::max(
                            0.0001F, right->offset - from.offset);
                        const auto segment_progress = std::clamp(
                            (cycle_progress - from.offset) / span, 0.0F, 1.0F);
                        const auto eased = timing_value(
                            segment_progress,
                            node.style.animations().opacity_keyframe_x1,
                            node.style.animations().opacity_keyframe_y1,
                            node.style.animations().opacity_keyframe_x2,
                            node.style.animations().opacity_keyframe_y2);
                        animation.painted_transform_rotate_degrees = from.degrees
                            + (right->degrees - from.degrees) * eased;
                    }
                    advanced = advanced || keyframe_paint_visible;
                }
            }
        }
        if (keyframe_animation_completed
            && !animation.opacity_keyframe_animation_active
            && !animation.rotation_keyframe_animation_active
            && !animation.keyframe_animation_end_event_sent) {
            auto animation_name = node.style.animations().animation_name_value;
            if (const auto comma = animation_name.find(',');
                comma != std::string::npos) {
                animation_name.erase(comma);
            }
            const auto first = animation_name.find_first_not_of(" \t\r\n");
            const auto last = animation_name.find_last_not_of(" \t\r\n");
            if (first != std::string::npos) {
                animation_name = animation_name.substr(first, last - first + 1U);
            }
            if (!animation_events_) {
                animation_events_ =
                    std::make_unique<std::vector<animation_event_record>>();
            }
            animation_events_->push_back({
                node.id,
                "animationend",
                std::move(animation_name),
                node.style.animations().opacity_keyframe_duration_ms
                    * node.style.animations().opacity_keyframe_iterations / 1000.0F});
            animation.keyframe_animation_end_event_sent = true;
        }
    }
    // Opacity, colors, filters, scale, and rotation are paint-only.
    // Their interpolated values are consumed directly by build_scene(), so they
    // must not repeat the
    // entire document's intrinsic sizing, shaping, and layout. Translation is
    // still reflected in the retained layout coordinates and therefore remains
    // a geometry invalidation until translated boxes are represented separately.
    if (layout_changed) mark_dirty();
    if (advanced) active_animation_demand_cache_valid_ = false;
    return advanced;
}

bool native_document::has_active_animations() const noexcept
{
    if (active_animation_demand_cache_valid_) {
        return active_animation_demand_cache_;
    }
    active_animation_demand_cache_ =
        std::any_of(nodes_.begin(), nodes_.end(), [this](const auto& node) {
        const auto* animation = node->animation_runtime();
        if (!participates_in_animation_frame(*node) || animation == nullptr) {
            return false;
        }
        const auto transition_active = animation->transform_animation_active
            || animation->left_animation_active
            || animation->top_animation_active
            || animation->opacity_animation_active
            || animation->color_animation_active
            || animation->background_color_animation_active
            || animation->filter_animation_active;
        const auto keyframe_active = animation->opacity_keyframe_animation_active
            || animation->rotation_keyframe_animation_active;
        return transition_active
            || (keyframe_active && intersects_visible_paint_area(*node));
    });
    active_animation_demand_cache_valid_ = true;
    return active_animation_demand_cache_;
}

std::vector<native_document::transition_event_record>
native_document::take_transition_events()
{
    auto events = std::move(transition_events_);
    transition_events_.clear();
    return events;
}

std::vector<native_document::animation_event_record>
native_document::take_animation_events()
{
    if (!animation_events_) return {};
    auto events = std::move(*animation_events_);
    animation_events_.reset();
    return events;
}
