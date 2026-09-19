namespace {
float directed_keyframe_progress(
    float progress,
    double iteration_index,
    node_style::animation_data::direction_kind direction) noexcept
{
    const auto odd_iteration = std::fmod(iteration_index, 2.0) >= 1.0;
    const auto reversed = direction == node_style::animation_data::direction_kind::reverse
        || (direction == node_style::animation_data::direction_kind::alternate
            && odd_iteration)
        || (direction == node_style::animation_data::direction_kind::alternate_reverse
            && !odd_iteration);
    return reversed ? 1.0F - progress : progress;
}

float active_keyframe_progress(
    float elapsed_ms,
    float duration_ms,
    node_style::animation_data::direction_kind direction) noexcept
{
    const auto iteration = std::floor(
        static_cast<double>(elapsed_ms) / static_cast<double>(duration_ms));
    auto progress = std::fmod(elapsed_ms, duration_ms) / duration_ms;
    if (progress < 0) progress += 1.0F;
    return directed_keyframe_progress(progress, iteration, direction);
}

float terminal_keyframe_progress(
    float iterations,
    node_style::animation_data::direction_kind direction) noexcept
{
    const auto count = std::max(0.0, static_cast<double>(iterations));
    if (count == 0.0) {
        // A zero-count effect never enters its first iteration. Both backwards
        // and forwards fill therefore use the start of that first iteration.
        return directed_keyframe_progress(0.0F, 0.0, direction);
    }
    if (!std::isfinite(count)) {
        // Zero-duration infinite effects have zero active duration. Web
        // Animations resolves their after-phase simple progress to one; an
        // infinite alternate iteration index is treated as forwards.
        return direction == node_style::animation_data::direction_kind::reverse
            ? 0.0F : 1.0F;
    }
    const auto whole = std::floor(count);
    const auto fractional = count - whole;
    const auto progress = fractional > 0.0
        ? static_cast<float>(fractional) : 1.0F;
    const auto iteration = fractional > 0.0 ? whole : std::max(0.0, whole - 1.0);
    return directed_keyframe_progress(progress, iteration, direction);
}

float keyframe_active_duration_ms(
    const node_style::animation_data::keyframe_animation& animation) noexcept
{
    if (animation.duration_ms <= 0 || animation.iterations <= 0) return 0;
    if (!std::isfinite(animation.iterations)) {
        return std::numeric_limits<float>::infinity();
    }
    return animation.duration_ms * animation.iterations;
}

bool fills_before(node_style::animation_data::fill_kind fill) noexcept
{
    return fill == node_style::animation_data::fill_kind::backwards
        || fill == node_style::animation_data::fill_kind::both;
}

bool fills_after(node_style::animation_data::fill_kind fill) noexcept
{
    return fill == node_style::animation_data::fill_kind::forwards
        || fill == node_style::animation_data::fill_kind::both;
}

void discard_web_animation_events(
    std::vector<native_document::web_animation_event_record>& events,
    uint32_t animation_id)
{
    std::erase_if(events, [animation_id](const auto& event) {
        return event.animation_id == animation_id;
    });
}
} // namespace

void native_document::signal_animation_frame(double timestamp_ms) noexcept
{
    if (std::isfinite(timestamp_ms) && timestamp_ms >= animation_frame_timestamp_ms_) {
        animation_frame_timestamp_ms_ = timestamp_ms;
    }
}

bool native_document::create_web_animation(
    dom_node& node,
    uint32_t id,
    node_style::animation_data::keyframe_animation effect)
{
    auto& animation = node.mutable_animation_runtime();
    const auto css_track_count = std::count_if(
        node.style.animations().keyframe_animations.begin(),
        node.style.animations().keyframe_animations.end(),
        [](const auto& track) { return !track.signature.empty(); });
    if (id == 0U
        || (web_animation_auxiliary_
            && web_animation_auxiliary_->nodes.size()
                >= maximum_live_web_animations)
        || (web_animation_auxiliary_
            && web_animation_auxiliary_->nodes.contains(id))
        || css_track_count + animation.web_animations.size()
            >= node_style::animation_data::max_keyframe_animation_tracks
        || std::any_of(
            animation.web_animations.begin(), animation.web_animations.end(),
            [id](const auto& candidate) { return candidate.id == id; })) {
        return false;
    }

    dom_node::animation_runtime_data::web_animation record;
    record.id = id;
    record.effect = std::move(effect);
    record.effect.name.clear();
    record.effect.signature = "waapi:" + std::to_string(id);
    record.runtime.name.clear();
    record.runtime.signature = record.effect.signature;
    record.runtime.started_ms = animation_frame_timestamp_ms_;
    record.runtime.fill_mode = record.effect.fill_mode;
    record.runtime.active = true;
    record.runtime.painted_opacity = node.style.opacity;
    record.runtime.painted_translation_x = node.style.transform_translate_x;
    record.runtime.painted_translation_y = node.style.transform_translate_y;
    record.runtime.painted_scale_x = node.style.transform_scale_x;
    record.runtime.painted_scale_y = node.style.transform_scale_y;
    record.runtime.painted_rotation_degrees = node.style.transform_rotate_degrees;
    animation.web_animations.push_back(std::move(record));
    if (!web_animation_auxiliary_) {
        web_animation_auxiliary_ =
            std::make_unique<web_animation_auxiliary_storage>();
    }
    web_animation_auxiliary_->nodes.emplace(id, node.id);
    active_animation_demand_cache_valid_ = false;
    last_animation_advance_timestamp_ms_ =
        std::numeric_limits<double>::quiet_NaN();
    static_cast<void>(advance_animations());
    mark_dirty();
    return true;
}

bool native_document::control_web_animation(
    uint32_t id,
    web_animation_command command,
    double seek_time_ms)
{
    if (!web_animation_auxiliary_) return false;
    const auto owner = web_animation_auxiliary_->nodes.find(id);
    if (owner == web_animation_auxiliary_->nodes.end()) return false;
    auto* node = find_by_native_id(owner->second);
    if (node == nullptr || node->animation_runtime() == nullptr) return false;
    auto* animation = node->animation_runtime();
    auto found = std::find_if(
        animation->web_animations.begin(), animation->web_animations.end(),
        [id](const auto& candidate) { return candidate.id == id; });
    if (found == animation->web_animations.end()) return false;
    auto& record = *found;
    auto& runtime = record.runtime;
    switch (command) {
        case web_animation_command::play:
            if (record.finished) {
                discard_web_animation_events(
                    web_animation_auxiliary_->events, id);
                runtime.started_ms = animation_frame_timestamp_ms_;
                runtime.active = true;
                runtime.filled = false;
                runtime.end_event_sent = false;
                record.finished = false;
            } else if (runtime.paused) {
                runtime.started_ms += std::max(
                    0.0, animation_frame_timestamp_ms_ - runtime.paused_at_ms);
            }
            runtime.paused = false;
            break;
        case web_animation_command::pause:
            if (!runtime.paused && !record.finished) {
                runtime.paused = true;
                runtime.paused_at_ms = animation_frame_timestamp_ms_;
            }
            break;
        case web_animation_command::cancel:
            discard_web_animation_events(
                web_animation_auxiliary_->events, id);
            web_animation_auxiliary_->events.push_back({node->id, id, true});
            animation->web_animations.erase(found);
            web_animation_auxiliary_->nodes.erase(id);
            break;
        case web_animation_command::finish: {
            const auto active_duration = keyframe_active_duration_ms(record.effect);
            if (!std::isfinite(active_duration)) return false;
            runtime.started_ms = animation_frame_timestamp_ms_
                - record.effect.delay_ms - active_duration;
            runtime.paused = false;
            runtime.active = true;
            record.finished = false;
            break;
        }
        case web_animation_command::seek:
            if (!std::isfinite(seek_time_ms)) return false;
            discard_web_animation_events(
                web_animation_auxiliary_->events, id);
            runtime.started_ms = (runtime.paused
                    ? runtime.paused_at_ms : animation_frame_timestamp_ms_)
                - seek_time_ms;
            runtime.active = true;
            runtime.filled = false;
            runtime.end_event_sent = false;
            record.finished = false;
            break;
    }
    active_animation_demand_cache_valid_ = false;
    last_animation_advance_timestamp_ms_ =
        std::numeric_limits<double>::quiet_NaN();
    static_cast<void>(advance_animations());
    mark_dirty();
    return true;
}

std::optional<native_document::web_animation_state>
native_document::web_animation_status(uint32_t id) const
{
    if (!web_animation_auxiliary_) return std::nullopt;
    const auto owner = web_animation_auxiliary_->nodes.find(id);
    if (owner == web_animation_auxiliary_->nodes.end()
        || owner->second == 0U || owner->second > native_id_index_.size()) {
        return std::nullopt;
    }
    const auto* node = native_id_index_[owner->second - 1U];
    if (node == nullptr || node->animation_runtime() == nullptr) {
        return std::nullopt;
    }
    const auto* animation = node->animation_runtime();
    const auto found = std::find_if(
        animation->web_animations.begin(), animation->web_animations.end(),
        [id](const auto& candidate) { return candidate.id == id; });
    if (found == animation->web_animations.end()) return std::nullopt;
    const auto timestamp = found->runtime.paused
        ? found->runtime.paused_at_ms : animation_frame_timestamp_ms_;
    const auto current_time = found->finished
        ? static_cast<double>(found->effect.delay_ms)
            + keyframe_active_duration_ms(found->effect)
        : timestamp - found->runtime.started_ms;
    return web_animation_state{
        current_time,
        found->runtime.paused,
        found->finished};
}

std::vector<uint32_t> native_document::web_animation_ids(
    const dom_node& node) const
{
    std::vector<uint32_t> result;
    const auto* animation = node.animation_runtime();
    if (animation == nullptr) return result;
    result.reserve(animation->web_animations.size());
    for (const auto& record : animation->web_animations) result.push_back(record.id);
    return result;
}

void native_document::cancel_web_animations(
    dom_node& root,
    bool include_descendants)
{
    auto canceled = false;
    const auto cancel_node = [&](dom_node& node) {
        auto* animation = node.animation_runtime();
        if (animation == nullptr || animation->web_animations.empty()) return;
        for (const auto& record : animation->web_animations) {
            discard_web_animation_events(
                web_animation_auxiliary_->events, record.id);
            web_animation_auxiliary_->events.push_back(
                {node.id, record.id, true});
            web_animation_auxiliary_->nodes.erase(record.id);
        }
        animation->web_animations.clear();
        canceled = true;
    };
    const auto visit = [&](const auto& self, dom_node& node) -> void {
        cancel_node(node);
        if (!include_descendants) return;
        for (auto* child : node.children) {
            if (child != nullptr) self(self, *child);
        }
    };
    visit(visit, root);
    if (!canceled) return;
    active_animation_demand_cache_valid_ = false;
    last_animation_advance_timestamp_ms_ =
        std::numeric_limits<double>::quiet_NaN();
    static_cast<void>(advance_animations());
    mark_dirty();
}

void native_document::update_style_animations(dom_node& node)
{
    const auto has_pseudo_animation = node.style.has_pseudo_elements()
        && ((node.style.before_pseudo().generated
                && node.style.before_pseudo().has_animation_data())
            || (node.style.after_pseudo().generated
                && node.style.after_pseudo().has_animation_data()));
    if (!node.has_animation_runtime() && !node.style.has_animation_data()
        && !has_pseudo_animation) {
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

    const auto& configured_keyframes = node.style.animations().keyframe_animations;
    const auto css_track_count = static_cast<size_t>(std::count_if(
        configured_keyframes.begin(), configured_keyframes.end(),
        [](const auto& track) { return !track.signature.empty(); }));
    const auto web_capacity =
        node_style::animation_data::max_keyframe_animation_tracks
        - std::min(css_track_count,
            node_style::animation_data::max_keyframe_animation_tracks);
    if (animation.web_animations.size() > web_capacity) {
        for (size_t index = web_capacity;
             index < animation.web_animations.size(); ++index) {
            discard_web_animation_events(
                web_animation_auxiliary_->events,
                animation.web_animations[index].id);
            web_animation_auxiliary_->events.push_back(
                {node.id, animation.web_animations[index].id, true});
            web_animation_auxiliary_->nodes.erase(
                animation.web_animations[index].id);
        }
        animation.web_animations.resize(web_capacity);
    }
    auto keyframe_set_changed = animation.keyframe_animations.size()
        != configured_keyframes.size();
    auto keyframe_phase_changed = keyframe_set_changed;
    for (size_t index = 0U;
         !keyframe_set_changed && index < configured_keyframes.size(); ++index) {
        keyframe_set_changed = animation.keyframe_animations[index].signature
            != configured_keyframes[index].signature;
    }
    for (size_t index = 0U;
         !keyframe_phase_changed && index < configured_keyframes.size(); ++index) {
        const auto desired_pause = !configured_keyframes[index].signature.empty()
            && configured_keyframes[index].play_state
                == node_style::animation_data::play_kind::paused;
        keyframe_phase_changed =
            animation.keyframe_animations[index].fill_mode
                != configured_keyframes[index].fill_mode
            || animation.keyframe_animations[index].paused != desired_pause;
    }
    if (keyframe_phase_changed) {
        // Permit the host to settle a zero-length phase change again at the
        // current timestamp; the ordinary duplicate-frame guard still applies
        // once the updated state has been sampled.
        last_animation_advance_timestamp_ms_ =
            std::numeric_limits<double>::quiet_NaN();
    }
    if (keyframe_set_changed && animation_events_) {
        std::erase_if(*animation_events_, [&](const auto& event) {
            return event.node_id == node.id && event.pseudo_element.empty();
        });
    }
    if (animation.keyframe_animations.size() > configured_keyframes.size()) {
        animation.keyframe_animations.resize(configured_keyframes.size());
    }
    while (animation.keyframe_animations.size() < configured_keyframes.size()) {
        animation.keyframe_animations.emplace_back();
    }
    for (size_t track_index = 0U;
         track_index < configured_keyframes.size(); ++track_index) {
        const auto& configured = configured_keyframes[track_index];
        auto& runtime = animation.keyframe_animations[track_index];
        const auto identity_changed = runtime.signature != configured.signature;
        const auto fill_changed = runtime.fill_mode != configured.fill_mode;
        const auto was_paused = runtime.paused;
        const auto previous_pause_timestamp = runtime.paused_at_ms;
        const auto desired_pause = !configured.signature.empty()
            && configured.play_state
                == node_style::animation_data::play_kind::paused;
        runtime.name = configured.name;
        runtime.fill_mode = configured.fill_mode;
        if (identity_changed) {
            runtime.signature = configured.signature;
            runtime.started_ms = animation_frame_timestamp_ms_;
            runtime.active = !configured.signature.empty();
            runtime.filled = false;
            runtime.end_event_sent = false;
            runtime.filter_valid = false;
            runtime.filter_keyframes.clear();
            runtime.filter_underlying.clear();
            runtime.painted_filter_functions.clear();
            runtime.painted_custom_properties.clear();
            runtime.underlying_custom_properties.clear();
            runtime.painted_opacity = node.style.opacity;
            runtime.painted_translation_x = node.style.transform_translate_x;
            runtime.painted_translation_y = node.style.transform_translate_y;
            runtime.painted_scale_x = node.style.transform_scale_x;
            runtime.painted_scale_y = node.style.transform_scale_y;
            runtime.painted_rotation_degrees = node.style.transform_rotate_degrees;
            for (const auto& custom : configured.custom_property_animations) {
                const auto found = node.style.custom_properties().values.find(
                    custom.name);
                const auto underlying = found == node.style.custom_properties().values.end()
                    ? std::string{} : found->second;
                runtime.underlying_custom_properties.emplace(
                    custom.name, underlying);
                runtime.painted_custom_properties.emplace(
                    custom.name, underlying);
            }
            if (!configured.filter_keyframes.empty()) {
                auto valid = configured.filter_keyframes.size() >= 2U;
                size_t longest = 0U;
                for (const auto& source : configured.filter_keyframes) {
                    dom_node::animation_runtime_data::retained_filter_keyframe stop;
                    stop.offset = source.offset;
                    valid = valid
                        && parse_foreground_filters(node, source.value, stop.functions);
                    stop.authored_none = stop.functions.empty();
                    runtime.filter_keyframes.push_back(std::move(stop));
                    if (runtime.filter_keyframes.back().functions.size()
                        > runtime.filter_keyframes[longest].functions.size()) {
                        longest = runtime.filter_keyframes.size() - 1U;
                    }
                }
                if (valid) {
                    const auto canonical =
                        runtime.filter_keyframes[longest].functions;
                    for (auto& stop : runtime.filter_keyframes) {
                        auto expected = canonical;
                        if (!make_retained_filter_lists_compatible(
                                stop.functions, expected)) {
                            valid = false;
                            break;
                        }
                    }
                }
                runtime.filter_valid = valid;
                if (configured.opacity_keyframes.size() < 2U
                    && configured.translation_keyframes.size() < 2U
                    && configured.scale_keyframes.size() < 2U
                    && configured.rotation_keyframes.size() < 2U
                    && configured.custom_property_animations.empty()
                    && !valid) {
                    runtime.active = false;
                }
            }
        } else if (fill_changed && !runtime.signature.empty()) {
            runtime.active = true;
            runtime.filled = false;
        }
        if (!configured.filter_keyframes.empty()) {
            const auto& effects = node.style.textual().effect_values;
            const auto authored = effects.find("filter");
            const auto value = authored == effects.end()
                ? std::string_view{"none"}
                : std::string_view{authored->second};
            if (!parse_foreground_filters(
                    node, value, runtime.filter_underlying)) {
                runtime.filter_underlying.clear();
            }
            if (identity_changed) {
                runtime.painted_filter_functions = runtime.filter_underlying;
            }
        }
        if (was_paused && (!desired_pause || identity_changed)) {
            const auto held_duration = std::max(
                0.0, animation_frame_timestamp_ms_ - previous_pause_timestamp);
            if (!identity_changed && runtime.active) {
                runtime.started_ms += held_duration;
            }
        }
        runtime.paused = desired_pause;
        if (desired_pause && (!was_paused || identity_changed)) {
            runtime.paused_at_ms = animation_frame_timestamp_ms_;
        } else if (configured.signature.empty()) {
            runtime.paused_at_ms = 0;
        }
    }

    const auto update_pseudo_tracks = [&] (
        const node_style::pseudo_element& pseudo,
        dom_node::animation_runtime_data::pseudo_animation_runtime& pseudo_runtime,
        std::string_view pseudo_name) {
        static const std::vector<node_style::animation_data::keyframe_animation> empty;
        const auto& configured = pseudo.generated && !pseudo.display_none
                && pseudo.has_animation_data()
            ? pseudo.animations().keyframe_animations : empty;
        auto set_changed = pseudo_runtime.keyframe_animations.size() != configured.size();
        auto phase_changed = set_changed;
        for (size_t index = 0U; !set_changed && index < configured.size(); ++index) {
            set_changed = pseudo_runtime.keyframe_animations[index].signature
                != configured[index].signature;
        }
        for (size_t index = 0U; !phase_changed && index < configured.size(); ++index) {
            const auto desired_pause = !configured[index].signature.empty()
                && configured[index].play_state
                    == node_style::animation_data::play_kind::paused;
            phase_changed = pseudo_runtime.keyframe_animations[index].fill_mode
                    != configured[index].fill_mode
                || pseudo_runtime.keyframe_animations[index].paused != desired_pause;
        }
        if (phase_changed) {
            last_animation_advance_timestamp_ms_ =
                std::numeric_limits<double>::quiet_NaN();
        }
        if (set_changed && animation_events_) {
            std::erase_if(*animation_events_, [&](const auto& event) {
                return event.node_id == node.id
                    && event.pseudo_element == pseudo_name;
            });
        }
        pseudo_runtime.keyframe_animations.resize(configured.size());
        if (configured.empty()) {
            pseudo_runtime.opacity_override = false;
            pseudo_runtime.transform_override = false;
            pseudo_runtime.filter_override = false;
            pseudo_runtime.painted_filter_functions.clear();
            return;
        }
        for (size_t index = 0U; index < configured.size(); ++index) {
            const auto& source = configured[index];
            auto& runtime = pseudo_runtime.keyframe_animations[index];
            const auto identity_changed = runtime.signature != source.signature;
            const auto fill_changed = runtime.fill_mode != source.fill_mode;
            const auto was_paused = runtime.paused;
            const auto previous_pause_timestamp = runtime.paused_at_ms;
            const auto desired_pause = !source.signature.empty()
                && source.play_state == node_style::animation_data::play_kind::paused;
            runtime.name = source.name;
            runtime.fill_mode = source.fill_mode;
            if (identity_changed) {
                runtime.signature = source.signature;
                runtime.started_ms = animation_frame_timestamp_ms_;
                runtime.active = !source.signature.empty();
                runtime.filled = false;
                runtime.end_event_sent = false;
                runtime.filter_valid = false;
                runtime.filter_keyframes.clear();
                runtime.filter_underlying.clear();
                runtime.painted_filter_functions.clear();
                runtime.painted_custom_properties.clear();
                runtime.underlying_custom_properties.clear();
                runtime.painted_opacity = pseudo.opacity;
                runtime.painted_translation_x = {0, length_unit::pixels};
                runtime.painted_translation_y = {0, length_unit::pixels};
                runtime.painted_scale_x = 1;
                runtime.painted_scale_y = 1;
                runtime.painted_rotation_degrees = 0;
                for (const auto& custom : source.custom_property_animations) {
                    auto underlying = std::string{};
                    const auto registration_value =
                        node.style.custom_properties().values.find(custom.name);
                    if (registration_value
                        != node.style.custom_properties().values.end()) {
                        underlying = registration_value->second;
                    }
                    runtime.underlying_custom_properties.emplace(
                        custom.name, underlying);
                    runtime.painted_custom_properties.emplace(
                        custom.name, underlying);
                }
                if (!source.filter_keyframes.empty()) {
                    auto valid = source.filter_keyframes.size() >= 2U;
                    size_t longest = 0U;
                    for (const auto& filter_source : source.filter_keyframes) {
                        dom_node::animation_runtime_data::retained_filter_keyframe stop;
                        stop.offset = filter_source.offset;
                        valid = valid && parse_foreground_filters(
                            node, filter_source.value, stop.functions);
                        stop.authored_none = stop.functions.empty();
                        runtime.filter_keyframes.push_back(std::move(stop));
                        if (runtime.filter_keyframes.back().functions.size()
                            > runtime.filter_keyframes[longest].functions.size()) {
                            longest = runtime.filter_keyframes.size() - 1U;
                        }
                    }
                    if (valid) {
                        const auto canonical =
                            runtime.filter_keyframes[longest].functions;
                        for (auto& stop : runtime.filter_keyframes) {
                            auto expected = canonical;
                            if (!make_retained_filter_lists_compatible(
                                    stop.functions, expected)) {
                                valid = false;
                                break;
                            }
                        }
                    }
                    runtime.filter_valid = valid;
                    if (source.opacity_keyframes.size() < 2U
                        && source.translation_keyframes.size() < 2U
                        && source.scale_keyframes.size() < 2U
                        && source.rotation_keyframes.size() < 2U
                        && source.custom_property_animations.empty()
                        && !valid) {
                        runtime.active = false;
                    }
                }
            } else if (fill_changed && !runtime.signature.empty()) {
                runtime.active = true;
                runtime.filled = false;
            }
            if (was_paused && (!desired_pause || identity_changed)) {
                const auto held_duration = std::max(
                    0.0, animation_frame_timestamp_ms_ - previous_pause_timestamp);
                if (!identity_changed && runtime.active) {
                    runtime.started_ms += held_duration;
                }
            }
            runtime.paused = desired_pause;
            if (desired_pause && (!was_paused || identity_changed)) {
                runtime.paused_at_ms = animation_frame_timestamp_ms_;
            } else if (source.signature.empty()) {
                runtime.paused_at_ms = 0;
            }
        }
    };
    update_pseudo_tracks(
        node.style.before_pseudo(), animation.before_pseudo, "::before");
    update_pseudo_tracks(
        node.style.after_pseudo(), animation.after_pseudo, "::after");

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
            const auto interpolable = previous_target_valid && filter_target_valid
                && make_retained_filter_lists_compatible(from, target);
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
    const auto timing_value = [&] (
        float progress,
        float x1,
        float y1,
        float x2,
        float y2,
        node_style::transition_timing::function_kind kind =
            node_style::transition_timing::function_kind::cubic_bezier,
        uint32_t step_count = 1U,
        node_style::transition_timing::step_position step_position =
            node_style::transition_timing::step_position::jump_end) {
        if (kind == node_style::transition_timing::function_kind::steps) {
            const auto count = static_cast<float>(std::max(1U, step_count));
            auto current_step = std::floor(progress * count);
            auto jumps = count;
            if (step_position
                == node_style::transition_timing::step_position::jump_start) {
                current_step += 1.0F;
            } else if (step_position
                == node_style::transition_timing::step_position::jump_none) {
                jumps -= 1.0F;
            } else if (step_position
                == node_style::transition_timing::step_position::jump_both) {
                current_step += 1.0F;
                jumps += 1.0F;
            }
            return std::clamp(current_step / std::max(1.0F, jumps), 0.0F, 1.0F);
        }
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
    const auto serialize_custom_number = [](float value, std::string_view unit) {
        std::ostringstream stream;
        stream << value << unit;
        return stream.str();
    };
    const auto replace_token = [](std::string& value,
                                  const std::string& previous,
                                  const std::string& current) {
        if (previous.empty() || previous == current) return false;
        auto changed = false;
        size_t offset = 0U;
        while ((offset = value.find(previous, offset)) != std::string::npos) {
            value.replace(offset, previous.size(), current);
            offset += current.size();
            changed = true;
        }
        return changed;
    };
    const auto replace_node_dependent_tokens = [&](dom_node& node,
                                                   const std::string& previous,
                                                   const std::string& current) {
        auto changed = false;
        if (node.style.has_background_image_data()) {
            changed = replace_token(
                node.style.mutable_background_image().image_value,
                previous, current) || changed;
        }
        if (auto* textual = node.style.mutable_textual_if_present();
            textual != nullptr) {
            for (auto& [name, value] : textual->effect_values) {
                static_cast<void>(name);
                changed = replace_token(value, previous, current) || changed;
            }
        }
        return changed;
    };
    const auto replace_pseudo_dependent_tokens = [&](node_style::pseudo_element& pseudo,
                                                     const std::string& previous,
                                                     const std::string& current) {
        return replace_token(
            pseudo.background_image.image_value, previous, current);
    };
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

        const auto previous_opacity_override =
            animation.keyframe_opacity_override;
        const auto previous_translation_override =
            animation.keyframe_translation_override;
        const auto previous_scale_override =
            animation.keyframe_scale_override;
        const auto previous_rotation_override =
            animation.keyframe_rotation_override;
        const auto previous_filter_override =
            animation.keyframe_filter_override;
        animation.keyframe_opacity_override = false;
        animation.keyframe_translation_override = false;
        animation.keyframe_scale_override = false;
        animation.keyframe_rotation_override = false;
        animation.keyframe_filter_override = false;
        auto winning_opacity = node.style.opacity;
        auto winning_translation_x = node.style.transform_translate_x;
        auto winning_translation_y = node.style.transform_translate_y;
        auto winning_scale_x = node.style.transform_scale_x;
        auto winning_scale_y = node.style.transform_scale_y;
        auto winning_rotation = node.style.transform_rotate_degrees;
        const std::vector<retained_filter_function>* winning_filter = nullptr;
        const std::vector<retained_filter_function>* filter_underlying = nullptr;
        for (size_t track_index = 0U;
             track_index < animation.keyframe_animations.size()
                && track_index < node.style.animations().keyframe_animations.size();
             ++track_index) {
            auto& runtime = animation.keyframe_animations[track_index];
            const auto& configured =
                node.style.animations().keyframe_animations[track_index];
            if (runtime.signature.empty()) continue;
            if (!runtime.filter_underlying.empty()) {
                filter_underlying = &runtime.filter_underlying;
            }
            auto contributes = false;
            auto progress = 0.0F;
            auto completed = false;
            if (!runtime.active) {
                contributes = runtime.filled;
            } else {
                const auto timestamp_ms = runtime.paused
                    ? runtime.paused_at_ms : animation_frame_timestamp_ms_;
                const auto elapsed_ms = static_cast<float>(
                    timestamp_ms - runtime.started_ms - configured.delay_ms);
                if (elapsed_ms < 0) {
                    contributes = fills_before(configured.fill_mode);
                    if (contributes) {
                        progress = directed_keyframe_progress(
                            0.0F, 0.0, configured.direction);
                    }
                } else {
                    const auto active_duration =
                        keyframe_active_duration_ms(configured);
                    const auto instantaneous = active_duration == 0;
                    if (instantaneous || elapsed_ms >= active_duration) {
                        contributes = fills_after(configured.fill_mode);
                        if (contributes) {
                            progress = terminal_keyframe_progress(
                                configured.iterations, configured.direction);
                        }
                        // A paused positive delay remains in its before phase.
                        // Once the zero-length active interval is reached,
                        // however, there is no interval left to hold.
                        if (!runtime.paused || instantaneous) {
                            runtime.active = false;
                            runtime.filled = contributes;
                            completed = true;
                        }
                    } else {
                        contributes = true;
                        runtime.filled = false;
                        progress = active_keyframe_progress(
                            elapsed_ms, configured.duration_ms,
                            configured.direction);
                    }
                }
            }
            const auto eased_segment = [&](float segment_progress) {
                return timing_value(
                    segment_progress,
                    configured.x1, configured.y1,
                    configured.x2, configured.y2,
                    configured.timing_kind,
                    configured.step_count,
                    configured.step_position);
            };
            if (contributes && configured.opacity_keyframes.size() >= 2U) {
                const auto& stops = configured.opacity_keyframes;
                auto right = std::lower_bound(
                    stops.begin(), stops.end(), progress,
                    [](const node_style::opacity_keyframe& stop, float value) {
                        return stop.offset < value;
                    });
                if (right == stops.begin()) {
                    runtime.painted_opacity = right->opacity;
                } else if (right == stops.end()) {
                    runtime.painted_opacity = stops.back().opacity;
                } else {
                    const auto& from = *(right - 1);
                    const auto span = std::max(0.0001F, right->offset - from.offset);
                    const auto local = std::clamp(
                        (progress - from.offset) / span, 0.0F, 1.0F);
                    const auto eased = eased_segment(local);
                    runtime.painted_opacity = from.opacity
                        + (right->opacity - from.opacity) * eased;
                }
                winning_opacity = runtime.painted_opacity;
                animation.keyframe_opacity_override = true;
            }
            if (contributes && configured.translation_keyframes.size() >= 2U) {
                const auto& stops = configured.translation_keyframes;
                auto right = std::lower_bound(
                    stops.begin(), stops.end(), progress,
                    [](const node_style::translation_keyframe& stop, float value) {
                        return stop.offset < value;
                    });
                if (right == stops.begin()) {
                    runtime.painted_translation_x = right->x;
                    runtime.painted_translation_y = right->y;
                } else if (right == stops.end()) {
                    runtime.painted_translation_x = stops.back().x;
                    runtime.painted_translation_y = stops.back().y;
                } else {
                    const auto& from = *(right - 1);
                    const auto span = std::max(0.0001F, right->offset - from.offset);
                    const auto local = std::clamp(
                        (progress - from.offset) / span, 0.0F, 1.0F);
                    const auto eased = eased_segment(local);
                    runtime.painted_translation_x = interpolate_length(
                        from.x, right->x, eased);
                    runtime.painted_translation_y = interpolate_length(
                        from.y, right->y, eased);
                }
                winning_translation_x = runtime.painted_translation_x;
                winning_translation_y = runtime.painted_translation_y;
                animation.keyframe_translation_override = true;
            }
            if (contributes && configured.scale_keyframes.size() >= 2U) {
                const auto& stops = configured.scale_keyframes;
                auto right = std::lower_bound(
                    stops.begin(), stops.end(), progress,
                    [](const node_style::scale_keyframe& stop, float value) {
                        return stop.offset < value;
                    });
                if (right == stops.begin()) {
                    runtime.painted_scale_x = right->x;
                    runtime.painted_scale_y = right->y;
                } else if (right == stops.end()) {
                    runtime.painted_scale_x = stops.back().x;
                    runtime.painted_scale_y = stops.back().y;
                } else {
                    const auto& from = *(right - 1);
                    const auto span = std::max(0.0001F, right->offset - from.offset);
                    const auto local = std::clamp(
                        (progress - from.offset) / span, 0.0F, 1.0F);
                    const auto eased = eased_segment(local);
                    runtime.painted_scale_x = from.x + (right->x - from.x) * eased;
                    runtime.painted_scale_y = from.y + (right->y - from.y) * eased;
                }
                winning_scale_x = runtime.painted_scale_x;
                winning_scale_y = runtime.painted_scale_y;
                animation.keyframe_scale_override = true;
            }
            if (contributes && configured.rotation_keyframes.size() >= 2U) {
                const auto& stops = configured.rotation_keyframes;
                auto right = std::lower_bound(
                    stops.begin(), stops.end(), progress,
                    [](const node_style::rotation_keyframe& stop, float value) {
                        return stop.offset < value;
                    });
                if (right == stops.begin()) {
                    runtime.painted_rotation_degrees = right->degrees;
                } else if (right == stops.end()) {
                    runtime.painted_rotation_degrees = stops.back().degrees;
                } else {
                    const auto& from = *(right - 1);
                    const auto span = std::max(0.0001F, right->offset - from.offset);
                    const auto local = std::clamp(
                        (progress - from.offset) / span, 0.0F, 1.0F);
                    const auto eased = eased_segment(local);
                    runtime.painted_rotation_degrees = from.degrees
                        + (right->degrees - from.degrees) * eased;
                }
                winning_rotation = runtime.painted_rotation_degrees;
                animation.keyframe_rotation_override = true;
            }
            if (contributes && runtime.filter_valid
                && runtime.filter_keyframes.size() >= 2U) {
                const auto& stops = runtime.filter_keyframes;
                const auto assign_stop = [&](const auto& stop) {
                    if (stop.authored_none) {
                        runtime.painted_filter_functions.clear();
                    } else {
                        runtime.painted_filter_functions = stop.functions;
                    }
                };
                auto right = std::lower_bound(
                    stops.begin(), stops.end(), progress,
                    [](const auto& stop, float value) {
                        return stop.offset < value;
                    });
                if (right == stops.begin()) {
                    assign_stop(*right);
                } else if (right == stops.end()) {
                    assign_stop(stops.back());
                } else if (std::abs(progress - right->offset) < 0.000001F) {
                    assign_stop(*right);
                } else {
                    const auto& from = *(right - 1);
                    const auto span = std::max(0.0001F, right->offset - from.offset);
                    const auto local = std::clamp(
                        (progress - from.offset) / span, 0.0F, 1.0F);
                    const auto eased = eased_segment(local);
                    runtime.painted_filter_functions = from.functions;
                    for (size_t index = 0U;
                         index < runtime.painted_filter_functions.size(); ++index) {
                        const auto initial = from.functions[index].amount;
                        runtime.painted_filter_functions[index].amount = initial
                            + (right->functions[index].amount - initial) * eased;
                    }
                }
                winning_filter = &runtime.painted_filter_functions;
                animation.keyframe_filter_override = true;
            }
            for (const auto& custom : configured.custom_property_animations) {
                if (custom.keyframes.size() < 2U) continue;
                auto sampled = runtime.underlying_custom_properties[custom.name];
                if (contributes) {
                    auto right = std::lower_bound(
                        custom.keyframes.begin(), custom.keyframes.end(), progress,
                        [](const auto& stop, float value) {
                            return stop.offset < value;
                        });
                    auto number = 0.0F;
                    if (right == custom.keyframes.begin()) {
                        number = right->value;
                    } else if (right == custom.keyframes.end()) {
                        number = custom.keyframes.back().value;
                    } else {
                        const auto& from = *(right - 1);
                        const auto local = std::clamp(
                            (progress - from.offset)
                                / std::max(0.0001F, right->offset - from.offset),
                            0.0F, 1.0F);
                        const auto eased = eased_segment(local);
                        number = from.value + (right->value - from.value) * eased;
                    }
                    sampled = serialize_custom_number(number, custom.unit);
                }
                auto& previous = runtime.painted_custom_properties[custom.name];
                if (sampled != previous) {
                    const auto token_changed = replace_node_dependent_tokens(
                        node, previous, sampled);
                    node.style.mutable_custom_properties().values[custom.name] = sampled;
                    previous = sampled;
                    advanced = advanced || token_changed || contributes;
                }
            }
            advanced = advanced || (contributes && keyframe_paint_visible);
            if (completed && !runtime.end_event_sent) {
                if (!animation_events_) {
                    animation_events_ =
                        std::make_unique<std::vector<animation_event_record>>();
                }
                animation_events_->push_back({
                    node.id,
                    "animationend",
                    runtime.name,
                    keyframe_active_duration_ms(configured) / 1000.0F});
                runtime.end_event_sent = true;
            }
        }
        // Script-created animations share the host timestamp and retained
        // property sampler. They compose after CSS animations in creation
        // order, matching replace composition without another scheduler.
        for (auto& record : animation.web_animations) {
            auto& runtime = record.runtime;
            const auto& configured = record.effect;
            auto contributes = false;
            auto progress = 0.0F;
            auto completed = false;
            if (!runtime.active) {
                contributes = runtime.filled;
            } else {
                const auto timestamp_ms = runtime.paused
                    ? runtime.paused_at_ms : animation_frame_timestamp_ms_;
                const auto elapsed_ms = static_cast<float>(
                    timestamp_ms - runtime.started_ms - configured.delay_ms);
                if (elapsed_ms < 0) {
                    contributes = fills_before(configured.fill_mode);
                    if (contributes) {
                        progress = directed_keyframe_progress(
                            0.0F, 0.0, configured.direction);
                    }
                } else {
                    const auto active_duration =
                        keyframe_active_duration_ms(configured);
                    const auto instantaneous = active_duration == 0;
                    if (instantaneous || elapsed_ms >= active_duration) {
                        contributes = fills_after(configured.fill_mode);
                        if (contributes) {
                            progress = terminal_keyframe_progress(
                                configured.iterations, configured.direction);
                        }
                        if (!runtime.paused || instantaneous) {
                            runtime.active = false;
                            runtime.filled = contributes;
                            record.finished = true;
                            completed = true;
                        }
                    } else {
                        contributes = true;
                        runtime.filled = false;
                        progress = active_keyframe_progress(
                            elapsed_ms, configured.duration_ms,
                            configured.direction);
                    }
                }
            }
            const auto eased_segment = [&](float segment_progress) {
                return timing_value(
                    segment_progress,
                    configured.x1, configured.y1,
                    configured.x2, configured.y2,
                    configured.timing_kind,
                    configured.step_count,
                    configured.step_position);
            };
            if (contributes && configured.opacity_keyframes.size() >= 2U) {
                const auto& stops = configured.opacity_keyframes;
                auto right = std::lower_bound(
                    stops.begin(), stops.end(), progress,
                    [](const auto& stop, float value) {
                        return stop.offset < value;
                    });
                if (right == stops.begin()) runtime.painted_opacity = right->opacity;
                else if (right == stops.end()) {
                    runtime.painted_opacity = stops.back().opacity;
                } else {
                    const auto& from = *(right - 1);
                    const auto local = std::clamp(
                        (progress - from.offset)
                            / std::max(0.0001F, right->offset - from.offset),
                        0.0F, 1.0F);
                    const auto eased = eased_segment(local);
                    runtime.painted_opacity = from.opacity
                        + (right->opacity - from.opacity) * eased;
                }
                winning_opacity = runtime.painted_opacity;
                animation.keyframe_opacity_override = true;
            }
            if (contributes && configured.translation_keyframes.size() >= 2U) {
                const auto& stops = configured.translation_keyframes;
                auto right = std::lower_bound(
                    stops.begin(), stops.end(), progress,
                    [](const auto& stop, float value) {
                        return stop.offset < value;
                    });
                if (right == stops.begin()) {
                    runtime.painted_translation_x = right->x;
                    runtime.painted_translation_y = right->y;
                } else if (right == stops.end()) {
                    runtime.painted_translation_x = stops.back().x;
                    runtime.painted_translation_y = stops.back().y;
                } else {
                    const auto& from = *(right - 1);
                    const auto eased = eased_segment(std::clamp(
                        (progress - from.offset)
                            / std::max(0.0001F, right->offset - from.offset),
                        0.0F, 1.0F));
                    runtime.painted_translation_x = interpolate_length(
                        from.x, right->x, eased);
                    runtime.painted_translation_y = interpolate_length(
                        from.y, right->y, eased);
                }
                winning_translation_x = runtime.painted_translation_x;
                winning_translation_y = runtime.painted_translation_y;
                animation.keyframe_translation_override = true;
            }
            if (contributes && configured.scale_keyframes.size() >= 2U) {
                const auto& stops = configured.scale_keyframes;
                auto right = std::lower_bound(
                    stops.begin(), stops.end(), progress,
                    [](const auto& stop, float value) {
                        return stop.offset < value;
                    });
                if (right == stops.begin()) {
                    runtime.painted_scale_x = right->x;
                    runtime.painted_scale_y = right->y;
                } else if (right == stops.end()) {
                    runtime.painted_scale_x = stops.back().x;
                    runtime.painted_scale_y = stops.back().y;
                } else {
                    const auto& from = *(right - 1);
                    const auto eased = eased_segment(std::clamp(
                        (progress - from.offset)
                            / std::max(0.0001F, right->offset - from.offset),
                        0.0F, 1.0F));
                    runtime.painted_scale_x = from.x
                        + (right->x - from.x) * eased;
                    runtime.painted_scale_y = from.y
                        + (right->y - from.y) * eased;
                }
                winning_scale_x = runtime.painted_scale_x;
                winning_scale_y = runtime.painted_scale_y;
                animation.keyframe_scale_override = true;
            }
            if (contributes && configured.rotation_keyframes.size() >= 2U) {
                const auto& stops = configured.rotation_keyframes;
                auto right = std::lower_bound(
                    stops.begin(), stops.end(), progress,
                    [](const auto& stop, float value) {
                        return stop.offset < value;
                    });
                if (right == stops.begin()) {
                    runtime.painted_rotation_degrees = right->degrees;
                } else if (right == stops.end()) {
                    runtime.painted_rotation_degrees = stops.back().degrees;
                } else {
                    const auto& from = *(right - 1);
                    const auto eased = eased_segment(std::clamp(
                        (progress - from.offset)
                            / std::max(0.0001F, right->offset - from.offset),
                        0.0F, 1.0F));
                    runtime.painted_rotation_degrees = from.degrees
                        + (right->degrees - from.degrees) * eased;
                }
                winning_rotation = runtime.painted_rotation_degrees;
                animation.keyframe_rotation_override = true;
            }
            advanced = advanced || (contributes && keyframe_paint_visible);
            if (completed && !runtime.end_event_sent) {
                web_animation_auxiliary_->events.push_back(
                    {node.id, record.id, false});
                runtime.end_event_sent = true;
            }
        }
        if (animation.keyframe_opacity_override) {
            animation.painted_opacity = winning_opacity;
        } else if (previous_opacity_override) {
            animation.painted_opacity = node.style.opacity;
        }
        const auto previous_translation_x = animation.painted_transform_translate_x;
        const auto previous_translation_y = animation.painted_transform_translate_y;
        if (animation.keyframe_translation_override) {
            animation.painted_transform_translate_x = winning_translation_x;
            animation.painted_transform_translate_y = winning_translation_y;
        } else if (previous_translation_override) {
            animation.painted_transform_translate_x = node.style.transform_translate_x;
            animation.painted_transform_translate_y = node.style.transform_translate_y;
        }
        if (animation.keyframe_translation_override || previous_translation_override) {
            layout_changed = layout_changed
                || !length_equal(
                    previous_translation_x, animation.painted_transform_translate_x)
                || !length_equal(
                    previous_translation_y, animation.painted_transform_translate_y);
        }
        if (animation.keyframe_scale_override) {
            animation.painted_transform_scale_x = winning_scale_x;
            animation.painted_transform_scale_y = winning_scale_y;
        } else if (previous_scale_override) {
            animation.painted_transform_scale_x = node.style.transform_scale_x;
            animation.painted_transform_scale_y = node.style.transform_scale_y;
        }
        if (animation.keyframe_rotation_override) {
            animation.painted_transform_rotate_degrees = winning_rotation;
        } else if (previous_rotation_override) {
            animation.painted_transform_rotate_degrees =
                node.style.transform_rotate_degrees;
        }
        if (animation.keyframe_filter_override && winning_filter != nullptr) {
            animation.painted_filter_functions = *winning_filter;
        } else if (previous_filter_override && !animation.filter_animation_active) {
            if (filter_underlying != nullptr) {
                animation.painted_filter_functions = *filter_underlying;
            } else {
                animation.painted_filter_functions.clear();
            }
        }

        const auto advance_pseudo = [&] (
            node_style::pseudo_element& pseudo,
            dom_node::animation_runtime_data::pseudo_animation_runtime& pseudo_runtime,
            std::string_view pseudo_name) {
            if (!pseudo.generated || pseudo.display_none) {
                pseudo_runtime.opacity_override = false;
                pseudo_runtime.transform_override = false;
                pseudo_runtime.filter_override = false;
                pseudo_runtime.painted_filter_functions.clear();
                return;
            }
            const auto& configured_tracks = pseudo.animations().keyframe_animations;
            auto opacity_override = false;
            auto transform_override = false;
            auto filter_override = false;
            auto winning_opacity = pseudo.opacity;
            auto winning_translation_x = css_length{0, length_unit::pixels};
            auto winning_translation_y = css_length{0, length_unit::pixels};
            auto winning_scale_x = 1.0F;
            auto winning_scale_y = 1.0F;
            auto winning_rotation = 0.0F;
            const std::vector<retained_filter_function>* winning_filter = nullptr;
            for (size_t index = 0U;
                 index < pseudo_runtime.keyframe_animations.size()
                    && index < configured_tracks.size(); ++index) {
                auto& runtime = pseudo_runtime.keyframe_animations[index];
                const auto& configured = configured_tracks[index];
                if (runtime.signature.empty()) continue;
                auto contributes = false;
                auto progress = 0.0F;
                auto completed = false;
                if (!runtime.active) {
                    contributes = runtime.filled;
                } else {
                    const auto timestamp_ms = runtime.paused
                        ? runtime.paused_at_ms : animation_frame_timestamp_ms_;
                    const auto elapsed_ms = static_cast<float>(
                        timestamp_ms - runtime.started_ms - configured.delay_ms);
                    if (elapsed_ms < 0) {
                        contributes = fills_before(configured.fill_mode);
                        if (contributes) {
                            progress = directed_keyframe_progress(
                                0.0F, 0.0, configured.direction);
                        }
                    } else {
                        const auto active_duration =
                            keyframe_active_duration_ms(configured);
                        const auto instantaneous = active_duration == 0;
                        if (instantaneous || elapsed_ms >= active_duration) {
                            contributes = fills_after(configured.fill_mode);
                            if (contributes) {
                                progress = terminal_keyframe_progress(
                                    configured.iterations, configured.direction);
                            }
                            if (!runtime.paused || instantaneous) {
                                runtime.active = false;
                                runtime.filled = contributes;
                                completed = true;
                            }
                        } else {
                            contributes = true;
                            runtime.filled = false;
                            progress = active_keyframe_progress(
                                elapsed_ms, configured.duration_ms,
                                configured.direction);
                        }
                    }
                }
                const auto eased_segment = [&](float segment_progress) {
                    return timing_value(
                        segment_progress,
                        configured.x1, configured.y1,
                        configured.x2, configured.y2,
                        configured.timing_kind,
                        configured.step_count,
                        configured.step_position);
                };
                if (contributes && configured.opacity_keyframes.size() >= 2U) {
                    const auto& stops = configured.opacity_keyframes;
                    auto right = std::lower_bound(
                        stops.begin(), stops.end(), progress,
                        [](const node_style::opacity_keyframe& stop, float value) {
                            return stop.offset < value;
                        });
                    if (right == stops.begin()) runtime.painted_opacity = right->opacity;
                    else if (right == stops.end()) {
                        runtime.painted_opacity = stops.back().opacity;
                    } else {
                        const auto& from = *(right - 1);
                        const auto span = std::max(0.0001F, right->offset - from.offset);
                        const auto eased = eased_segment(std::clamp(
                            (progress - from.offset) / span, 0.0F, 1.0F));
                        runtime.painted_opacity = from.opacity
                            + (right->opacity - from.opacity) * eased;
                    }
                    winning_opacity = runtime.painted_opacity;
                    opacity_override = true;
                }
                if (contributes && configured.translation_keyframes.size() >= 2U) {
                    const auto& stops = configured.translation_keyframes;
                    auto right = std::lower_bound(
                        stops.begin(), stops.end(), progress,
                        [](const node_style::translation_keyframe& stop, float value) {
                            return stop.offset < value;
                        });
                    if (right == stops.begin()) {
                        runtime.painted_translation_x = right->x;
                        runtime.painted_translation_y = right->y;
                    } else if (right == stops.end()) {
                        runtime.painted_translation_x = stops.back().x;
                        runtime.painted_translation_y = stops.back().y;
                    } else {
                        const auto& from = *(right - 1);
                        const auto span = std::max(0.0001F, right->offset - from.offset);
                        const auto eased = eased_segment(std::clamp(
                            (progress - from.offset) / span, 0.0F, 1.0F));
                        runtime.painted_translation_x = interpolate_length(
                            from.x, right->x, eased);
                        runtime.painted_translation_y = interpolate_length(
                            from.y, right->y, eased);
                    }
                    winning_translation_x = runtime.painted_translation_x;
                    winning_translation_y = runtime.painted_translation_y;
                    transform_override = true;
                }
                if (contributes && configured.scale_keyframes.size() >= 2U) {
                    const auto& stops = configured.scale_keyframes;
                    auto right = std::lower_bound(
                        stops.begin(), stops.end(), progress,
                        [](const node_style::scale_keyframe& stop, float value) {
                            return stop.offset < value;
                        });
                    if (right == stops.begin()) {
                        runtime.painted_scale_x = right->x;
                        runtime.painted_scale_y = right->y;
                    } else if (right == stops.end()) {
                        runtime.painted_scale_x = stops.back().x;
                        runtime.painted_scale_y = stops.back().y;
                    } else {
                        const auto& from = *(right - 1);
                        const auto span = std::max(0.0001F, right->offset - from.offset);
                        const auto eased = eased_segment(std::clamp(
                            (progress - from.offset) / span, 0.0F, 1.0F));
                        runtime.painted_scale_x = from.x + (right->x - from.x) * eased;
                        runtime.painted_scale_y = from.y + (right->y - from.y) * eased;
                    }
                    winning_scale_x = runtime.painted_scale_x;
                    winning_scale_y = runtime.painted_scale_y;
                    transform_override = true;
                }
                if (contributes && configured.rotation_keyframes.size() >= 2U) {
                    const auto& stops = configured.rotation_keyframes;
                    auto right = std::lower_bound(
                        stops.begin(), stops.end(), progress,
                        [](const node_style::rotation_keyframe& stop, float value) {
                            return stop.offset < value;
                        });
                    if (right == stops.begin()) {
                        runtime.painted_rotation_degrees = right->degrees;
                    } else if (right == stops.end()) {
                        runtime.painted_rotation_degrees = stops.back().degrees;
                    } else {
                        const auto& from = *(right - 1);
                        const auto span = std::max(0.0001F, right->offset - from.offset);
                        const auto eased = eased_segment(std::clamp(
                            (progress - from.offset) / span, 0.0F, 1.0F));
                        runtime.painted_rotation_degrees = from.degrees
                            + (right->degrees - from.degrees) * eased;
                    }
                    winning_rotation = runtime.painted_rotation_degrees;
                    transform_override = true;
                }
                if (contributes && runtime.filter_valid
                    && runtime.filter_keyframes.size() >= 2U) {
                    const auto& stops = runtime.filter_keyframes;
                    const auto assign_stop = [&](const auto& stop) {
                        if (stop.authored_none) {
                            runtime.painted_filter_functions.clear();
                        } else {
                            runtime.painted_filter_functions = stop.functions;
                        }
                    };
                    auto right = std::lower_bound(
                        stops.begin(), stops.end(), progress,
                        [](const auto& stop, float value) {
                            return stop.offset < value;
                        });
                    if (right == stops.begin()) assign_stop(*right);
                    else if (right == stops.end()) assign_stop(stops.back());
                    else if (std::abs(progress - right->offset) < 0.000001F) {
                        assign_stop(*right);
                    } else {
                        const auto& from = *(right - 1);
                        const auto span = std::max(0.0001F, right->offset - from.offset);
                        const auto eased = eased_segment(std::clamp(
                            (progress - from.offset) / span, 0.0F, 1.0F));
                        runtime.painted_filter_functions = from.functions;
                        for (size_t filter_index = 0U;
                             filter_index < runtime.painted_filter_functions.size();
                             ++filter_index) {
                            const auto initial = from.functions[filter_index].amount;
                            runtime.painted_filter_functions[filter_index].amount = initial
                                + (right->functions[filter_index].amount - initial) * eased;
                        }
                    }
                    winning_filter = &runtime.painted_filter_functions;
                    filter_override = true;
                }
                for (const auto& custom : configured.custom_property_animations) {
                    if (custom.keyframes.size() < 2U) continue;
                    auto sampled = runtime.underlying_custom_properties[custom.name];
                    if (contributes) {
                        auto right = std::lower_bound(
                            custom.keyframes.begin(), custom.keyframes.end(), progress,
                            [](const auto& stop, float value) {
                                return stop.offset < value;
                            });
                        auto number = 0.0F;
                        if (right == custom.keyframes.begin()) {
                            number = right->value;
                        } else if (right == custom.keyframes.end()) {
                            number = custom.keyframes.back().value;
                        } else {
                            const auto& from = *(right - 1);
                            const auto eased = eased_segment(std::clamp(
                                (progress - from.offset)
                                    / std::max(0.0001F, right->offset - from.offset),
                                0.0F, 1.0F));
                            number = from.value
                                + (right->value - from.value) * eased;
                        }
                        sampled = serialize_custom_number(number, custom.unit);
                    }
                    auto& previous = runtime.painted_custom_properties[custom.name];
                    if (sampled != previous) {
                        const auto token_changed = replace_pseudo_dependent_tokens(
                            pseudo, previous, sampled);
                        previous = sampled;
                        advanced = advanced || token_changed || contributes;
                    }
                }
                advanced = advanced || (contributes && keyframe_paint_visible);
                if (completed && !runtime.end_event_sent) {
                    if (!animation_events_) {
                        animation_events_ =
                            std::make_unique<std::vector<animation_event_record>>();
                    }
                    animation_events_->push_back({
                        node.id,
                        "animationend",
                        runtime.name,
                        keyframe_active_duration_ms(configured) / 1000.0F,
                        std::string(pseudo_name)});
                    runtime.end_event_sent = true;
                }
            }
            pseudo_runtime.opacity_override = opacity_override;
            pseudo_runtime.transform_override = transform_override;
            pseudo_runtime.filter_override = filter_override;
            pseudo_runtime.painted_opacity = winning_opacity;
            pseudo_runtime.painted_translation_x = winning_translation_x;
            pseudo_runtime.painted_translation_y = winning_translation_y;
            pseudo_runtime.painted_scale_x = winning_scale_x;
            pseudo_runtime.painted_scale_y = winning_scale_y;
            pseudo_runtime.painted_rotation_degrees = winning_rotation;
            if (filter_override && winning_filter != nullptr) {
                pseudo_runtime.painted_filter_functions = *winning_filter;
            } else {
                pseudo_runtime.painted_filter_functions.clear();
            }
        };
        advance_pseudo(
            node.style.mutable_before_pseudo(), animation.before_pseudo, "::before");
        advance_pseudo(
            node.style.mutable_after_pseudo(), animation.after_pseudo, "::after");
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
        const auto keyframe_active = std::any_of(
            animation->keyframe_animations.begin(),
            animation->keyframe_animations.end(), [](const auto& track) {
                return track.active && !track.paused;
            });
        const auto web_animation_active = std::any_of(
            animation->web_animations.begin(),
            animation->web_animations.end(), [](const auto& track) {
                return track.runtime.active && !track.runtime.paused;
            });
        const auto pseudo_active = [](const auto& pseudo) {
            return std::any_of(
                pseudo.keyframe_animations.begin(),
                pseudo.keyframe_animations.end(), [](const auto& track) {
                    return track.active && !track.paused;
                });
        };
        const auto generated_pseudo_active =
            (node->style.before_pseudo().generated
                && !node->style.before_pseudo().display_none
                && pseudo_active(animation->before_pseudo))
            || (node->style.after_pseudo().generated
                && !node->style.after_pseudo().display_none
                && pseudo_active(animation->after_pseudo));
        return transition_active
            || ((keyframe_active || web_animation_active
                    || generated_pseudo_active)
                && intersects_visible_paint_area(*node));
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

std::vector<native_document::web_animation_event_record>
native_document::take_web_animation_events()
{
    if (!web_animation_auxiliary_) return {};
    auto events = std::move(web_animation_auxiliary_->events);
    web_animation_auxiliary_->events.clear();
    return events;
}
