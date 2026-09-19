#pragma once
#include "webscene_css_box_values.h"
#include <cctype>
#include <cstdlib>
#include <charconv>
#include <cmath>

namespace webscene_native::css {
inline std::optional<float> css_time_ms(std::string_view text) {
    text=trim_css_view(text);
    float multiplier=1;
    if(text.ends_with("ms")) text.remove_suffix(2);
    else if(text.ends_with('s')) { text.remove_suffix(1); multiplier=1000; }
    else return std::nullopt;
    if(text.empty()) return std::nullopt;
    if(text.front()=='+') text.remove_prefix(1);
    if(text.empty() || (text.front()!='-' && text.front()!='.' &&
        (text.front()<'0' || text.front()>'9')) || text.back()<'0' || text.back()>'9') return std::nullopt;
    const std::string number{text};
    char* parsed_end = nullptr;
    const auto value = std::strtof(number.c_str(), &parsed_end);
    if(parsed_end != number.c_str()+number.size() ||
        !std::isfinite(value*multiplier)) return std::nullopt;
    return value*multiplier;
}
inline bool is_css_time(std::string_view value) { return css_time_ms(value).has_value(); }
inline float parse_css_time_ms(std::string value) { return css_time_ms(value).value_or(0); }

struct parsed_keyframe_transform final {
    css_length translate_x{0, length_unit::pixels};
    css_length translate_y{0, length_unit::pixels};
    float scale_x{1};
    float scale_y{1};
    float rotate_degrees{0};
};

inline std::optional<float> finite_css_number(std::string_view text)
{
    text = trim_css_view(text);
    if (text.empty() || text.size() > 64U) return std::nullopt;
    const std::string value{text};
    char* end = nullptr;
    const auto number = std::strtof(value.c_str(), &end);
    if (end != value.c_str() + value.size() || !std::isfinite(number)) {
        return std::nullopt;
    }
    return number;
}

inline std::optional<css_length> keyframe_translation_length(std::string_view text)
{
    text = trim_css_view(text);
    if (text.empty() || text.size() > 64U) return std::nullopt;
    auto lower = ascii_lower(std::string(text));
    if (lower.ends_with('%')) {
        const auto number = finite_css_number(
            std::string_view(lower).substr(0U, lower.size() - 1U));
        return number.has_value()
            ? std::optional<css_length>{css_length{*number, length_unit::percent}}
            : std::nullopt;
    }
    if (lower.ends_with("px")) {
        const auto number = finite_css_number(
            std::string_view(lower).substr(0U, lower.size() - 2U));
        return number.has_value()
            ? std::optional<css_length>{css_length{*number, length_unit::pixels}}
            : std::nullopt;
    }
    const auto zero = finite_css_number(lower);
    return zero.has_value() && *zero == 0.0F
        ? std::optional<css_length>{css_length{0, length_unit::pixels}}
        : std::nullopt;
}

inline std::optional<float> keyframe_rotation_angle(std::string_view text)
{
    text = trim_css_view(text);
    if (text.empty() || text.size() > 64U) return std::nullopt;
    auto lower = ascii_lower(std::string(text));
    auto multiplier = 1.0F;
    if (lower.ends_with("deg")) lower.resize(lower.size() - 3U);
    else if (lower.ends_with("turn")) {
        lower.resize(lower.size() - 4U);
        multiplier = 360.0F;
    } else if (lower.ends_with("rad")) {
        lower.resize(lower.size() - 3U);
        multiplier = 57.29577951308232F;
    } else {
        const auto zero = finite_css_number(lower);
        return zero.has_value() && *zero == 0.0F ? zero : std::nullopt;
    }
    const auto number = finite_css_number(lower);
    if (!number.has_value() || !std::isfinite(*number * multiplier)) {
        return std::nullopt;
    }
    return *number * multiplier;
}

inline std::optional<parsed_keyframe_transform> parse_keyframe_transform(
    std::string value)
{
    value = ascii_lower(trim_value(std::move(value)));
    if (value == "none") return parsed_keyframe_transform{};
    if (value.empty() || value.size() > 1024U) return std::nullopt;
    parsed_keyframe_transform result;
    bool translated_x = false;
    bool translated_y = false;
    bool scaled_x = false;
    bool scaled_y = false;
    bool rotated = false;
    size_t cursor = 0U;
    size_t function_count = 0U;
    while (cursor < value.size()) {
        while (cursor < value.size()
            && std::isspace(static_cast<unsigned char>(value[cursor]))) ++cursor;
        if (cursor == value.size()) break;
        const auto name_begin = cursor;
        while (cursor < value.size()
            && std::isalpha(static_cast<unsigned char>(value[cursor]))) ++cursor;
        if (cursor == name_begin || cursor >= value.size() || value[cursor] != '(') {
            return std::nullopt;
        }
        const auto name = value.substr(name_begin, cursor - name_begin);
        const auto argument_begin = ++cursor;
        auto depth = 1U;
        while (cursor < value.size() && depth != 0U) {
            if (value[cursor] == '(') ++depth;
            else if (value[cursor] == ')') --depth;
            ++cursor;
        }
        if (depth != 0U || ++function_count > 5U) return std::nullopt;
        const auto argument_end = cursor - 1U;
        if (value.find('(', argument_begin) < argument_end) return std::nullopt;
        auto arguments = split_css_component_list(
            std::string_view(value).substr(
                argument_begin, argument_end - argument_begin), ',');
        if (arguments.size() == 1U) {
            auto first = trim_value(arguments.front());
            const auto separator = first.find_first_of(" \t\r\n");
            if (separator != std::string::npos) {
                auto second_begin = first.find_first_not_of(" \t\r\n", separator);
                if (second_begin == std::string::npos
                    || first.find_first_of(" \t\r\n", second_begin)
                        != std::string::npos) return std::nullopt;
                arguments = {
                    first.substr(0U, separator), first.substr(second_begin)};
            }
        }
        const auto translation = [&](size_t index) {
            return index < arguments.size()
                ? keyframe_translation_length(arguments[index]) : std::nullopt;
        };
        const auto scale = [&](size_t index) {
            return index < arguments.size()
                ? finite_css_number(arguments[index]) : std::nullopt;
        };
        if (name == "translate" && (arguments.size() == 1U || arguments.size() == 2U)
            && !translated_x && !translated_y) {
            const auto x = translation(0U);
            const auto y = arguments.size() == 2U
                ? translation(1U)
                : std::optional<css_length>{css_length{0, length_unit::pixels}};
            if (!x.has_value() || !y.has_value()) return std::nullopt;
            result.translate_x = *x; result.translate_y = *y;
            translated_x = translated_y = true;
        } else if (name == "translatex" && arguments.size() == 1U && !translated_x) {
            const auto x = translation(0U);
            if (!x.has_value()) return std::nullopt;
            result.translate_x = *x; translated_x = true;
        } else if (name == "translatey" && arguments.size() == 1U && !translated_y) {
            const auto y = translation(0U);
            if (!y.has_value()) return std::nullopt;
            result.translate_y = *y; translated_y = true;
        } else if (name == "scale" && (arguments.size() == 1U || arguments.size() == 2U)
            && !scaled_x && !scaled_y) {
            const auto x = scale(0U);
            const auto y = arguments.size() == 2U ? scale(1U) : x;
            if (!x.has_value() || !y.has_value()) return std::nullopt;
            result.scale_x = *x; result.scale_y = *y;
            scaled_x = scaled_y = true;
        } else if (name == "scalex" && arguments.size() == 1U && !scaled_x) {
            const auto x = scale(0U);
            if (!x.has_value()) return std::nullopt;
            result.scale_x = *x; scaled_x = true;
        } else if (name == "scaley" && arguments.size() == 1U && !scaled_y) {
            const auto y = scale(0U);
            if (!y.has_value()) return std::nullopt;
            result.scale_y = *y; scaled_y = true;
        } else if (name == "rotate" && arguments.size() == 1U && !rotated) {
            const auto angle = keyframe_rotation_angle(arguments.front());
            if (!angle.has_value()) return std::nullopt;
            result.rotate_degrees = *angle; rotated = true;
        } else {
            return std::nullopt;
        }
    }
    return function_count == 0U
        ? std::nullopt : std::optional<parsed_keyframe_transform>{result};
}

inline void parse_transition_timing(
        const std::string& value,
        node_style::transition_timing& timing)
    {
        const auto lower = ascii_lower(value);
        if (lower == "ease") {
            timing.kind = node_style::transition_timing::function_kind::cubic_bezier;
            timing.x1 = 0.25F; timing.y1 = 0.1F; timing.x2 = 0.25F; timing.y2 = 1;
        } else if (lower == "linear") {
            timing.kind = node_style::transition_timing::function_kind::cubic_bezier;
            timing.x1 = 0; timing.y1 = 0; timing.x2 = 1; timing.y2 = 1;
        } else if (lower == "ease-in") {
            timing.kind = node_style::transition_timing::function_kind::cubic_bezier;
            timing.x1 = 0.42F; timing.y1 = 0; timing.x2 = 1; timing.y2 = 1;
        } else if (lower == "ease-out") {
            timing.kind = node_style::transition_timing::function_kind::cubic_bezier;
            timing.x1 = 0; timing.y1 = 0; timing.x2 = 0.58F; timing.y2 = 1;
        } else if (lower == "ease-in-out") {
            timing.kind = node_style::transition_timing::function_kind::cubic_bezier;
            timing.x1 = 0.42F; timing.y1 = 0; timing.x2 = 0.58F; timing.y2 = 1;
        } else if (lower.starts_with("cubic-bezier(") && lower.ends_with(')')) {
            auto points = lower.substr(13U, lower.size() - 14U);
            std::replace(points.begin(), points.end(), ',', ' ');
            std::istringstream stream(points);
            float x1 = 0.25F, y1 = 0.1F, x2 = 0.25F, y2 = 1;
            std::string trailing;
            if (stream >> x1 >> y1 >> x2 >> y2 && !(stream >> trailing)) {
                timing.kind = node_style::transition_timing::function_kind::cubic_bezier;
                timing.x1 = std::clamp(x1, 0.0F, 1.0F);
                timing.y1 = y1;
                timing.x2 = std::clamp(x2, 0.0F, 1.0F);
                timing.y2 = y2;
            }
        }
    }

inline bool parse_animation_timing(
        const std::string& value,
        node_style::transition_timing& timing)
    {
        const auto lower = ascii_lower(value);
        if (lower == "step-start" || lower == "step-end") {
            timing.kind = node_style::transition_timing::function_kind::steps;
            timing.step_count = 1U;
            timing.steps_position = lower == "step-start"
                ? node_style::transition_timing::step_position::jump_start
                : node_style::transition_timing::step_position::jump_end;
        } else if (lower.starts_with("steps(") && lower.ends_with(')')) {
            const auto components = split_css_component_list(
                std::string_view(lower).substr(6U, lower.size() - 7U), ',');
            if (components.empty() || components.size() > 2U) return false;
            const auto count_text = trim_css_view(components.front());
            uint32_t count = 0U;
            const auto parsed = std::from_chars(
                count_text.data(), count_text.data() + count_text.size(), count);
            if (parsed.ec != std::errc{}
                || parsed.ptr != count_text.data() + count_text.size()
                || count == 0U) return false;
            auto position = node_style::transition_timing::step_position::jump_end;
            if (components.size() == 2U) {
                const auto position_text = trim_css_view(components[1]);
                if (position_text == "start" || position_text == "jump-start") {
                    position = node_style::transition_timing::step_position::jump_start;
                } else if (position_text == "end" || position_text == "jump-end") {
                    position = node_style::transition_timing::step_position::jump_end;
                } else if (position_text == "jump-none") {
                    if (count == 1U) return false;
                    position = node_style::transition_timing::step_position::jump_none;
                } else if (position_text == "jump-both") {
                    position = node_style::transition_timing::step_position::jump_both;
                } else {
                    return false;
                }
            }
            timing.kind = node_style::transition_timing::function_kind::steps;
            timing.step_count = count;
            timing.steps_position = position;
        } else {
            const auto cubic = lower == "linear" || lower == "ease"
                || lower == "ease-in" || lower == "ease-out"
                || lower == "ease-in-out"
                || (lower.starts_with("cubic-bezier(") && lower.ends_with(')'));
            if (!cubic) return false;
            parse_transition_timing(lower, timing);
        }
        return true;
    }

inline bool is_animation_timing_function(const std::string& value)
    {
        node_style::transition_timing timing;
        return parse_animation_timing(value, timing);
    }

inline void configure_style_transitions(node_style& style)
    {
        if (!style.has_animation_data()) return;
        auto& animations = style.mutable_animations();
        const auto properties = split_css_component_list(animations.transition_property_value, ',');
        const auto durations = split_css_component_list(animations.transition_duration_value, ',');
        const auto delays = split_css_component_list(animations.transition_delay_value, ',');
        const auto timings = split_css_component_list(
            animations.transition_timing_function_value, ',');
        const auto resolve = [&](std::string_view property) {
            node_style::transition_timing result;
            for (size_t index = 0; index < properties.size(); ++index) {
                auto candidate = ascii_lower(trim_value(properties[index]));
                // Transition matching uses the canonical physical property.
                // A logical inset declaration and its physical alias address
                // the same computed value (CSS Logical Properties §4.1).
                if (candidate == "inset-inline-start") candidate = "left";
                else if (candidate == "inset-block-start") candidate = "top";
                else if (candidate == "background") candidate = "background-color";
                if (candidate != property && candidate != "all") continue;
                if (!durations.empty()) {
                    result.duration_ms = std::max(
                        0.0F, parse_css_time_ms(durations[index % durations.size()]));
                }
                if (!delays.empty()) {
                    result.delay_ms = parse_css_time_ms(delays[index % delays.size()]);
                }
                if (!timings.empty()) {
                    parse_transition_timing(timings[index % timings.size()], result);
                }
                return result;
            }
            return result;
        };
        animations.transform_transition = resolve("transform");
        animations.left_transition = resolve("left");
        animations.top_transition = resolve("top");
        animations.opacity_transition = resolve("opacity");
        animations.color_transition = resolve("color");
        animations.background_color_transition = resolve("background-color");
        animations.filter_transition = resolve("filter");
    }

inline void apply_transition_shorthand(
    node_style& style,
    const std::string& value,
    bool configure = true)
    {
        std::vector<std::string> properties;
        std::vector<std::string> durations;
        std::vector<std::string> delays;
        std::vector<std::string> timings;
        for (const auto& item : split_css_component_list(value, ',')) {
            auto property = std::string("all");
            auto duration = std::string("0s");
            auto delay = std::string("0s");
            auto timing = std::string("ease");
            auto saw_time = false;
            for (const auto& token : split_value_tokens(item)) {
                const auto lower = ascii_lower(token);
                if (is_css_time(lower)) {
                    if (!saw_time) duration = lower;
                    else delay = lower;
                    saw_time = true;
                } else if (lower == "linear" || lower == "ease" || lower == "ease-in"
                    || lower == "ease-out" || lower == "ease-in-out"
                    || lower.starts_with("cubic-bezier(")) {
                    timing = lower;
                } else if (lower != "normal") {
                    property = lower;
                }
            }
            properties.push_back(std::move(property));
            durations.push_back(std::move(duration));
            delays.push_back(std::move(delay));
            timings.push_back(std::move(timing));
        }
        const auto join = [](const std::vector<std::string>& values) {
            std::string result;
            for (const auto& value : values) {
                if (!result.empty()) result += ", ";
                result += value;
            }
            return result;
        };
        auto& animations = style.mutable_animations();
        animations.transition_property_value = join(properties);
        animations.transition_duration_value = join(durations);
        animations.transition_delay_value = join(delays);
        animations.transition_timing_function_value = join(timings);
        if (configure) configure_style_transitions(style);
    }

inline bool apply_compiled_single_transition_shorthand(
    node_style& style,
    const specified_css_value& specified,
    bool configure = true)
    {
        if (specified.kind != specified_css_kind::component_list
            || specified.components.empty()) return false;
        auto property = std::string("all");
        auto duration = std::string("0s");
        auto delay = std::string("0s");
        auto timing = std::string("ease");
        auto saw_time = false;
        for (const auto& component : specified.components) {
            const auto lower = ascii_lower(component.name);
            if (component.kind == css_typed_component_kind::time) {
                if (!saw_time) duration = lower;
                else delay = lower;
                saw_time = true;
            } else if (lower == "linear" || lower == "ease" || lower == "ease-in"
                || lower == "ease-out" || lower == "ease-in-out"
                || lower.starts_with("cubic-bezier(")) {
                timing = lower;
            } else if (lower != "normal") {
                property = lower;
            }
        }
        auto& animations = style.mutable_animations();
        animations.transition_property_value = std::move(property);
        animations.transition_duration_value = std::move(duration);
        animations.transition_delay_value = std::move(delay);
        animations.transition_timing_function_value = std::move(timing);
        if (configure) configure_style_transitions(style);
        return true;
    }

inline void apply_animation_shorthand(
    node_style::animation_data& animations,
    const std::string& value)
    {
        std::vector<std::string> names, durations, delays, timings, iterations;
        std::vector<std::string> directions, fill_modes, play_states;
        const auto items = split_css_component_list(value, ',');
        for (const auto& item : items) {
            auto name = std::string("none");
            auto duration = std::string("0s");
            auto delay = std::string("0s");
            auto timing = std::string("ease");
            auto iteration = std::string("1");
            auto direction = std::string("normal");
            auto fill_mode = std::string("none");
            auto play_state = std::string("running");
            auto saw_time = false;
            for (const auto& token : split_value_tokens(item)) {
                const auto lower = ascii_lower(token);
                if (is_css_time(lower)) {
                    if (!saw_time) duration = lower;
                    else delay = lower;
                    saw_time = true;
                } else if (is_animation_timing_function(lower)) {
                    timing = lower;
                } else if (lower == "infinite"
                    || std::all_of(lower.begin(), lower.end(), [](unsigned char character) {
                        return std::isdigit(character) || character == '.';
                    })) {
                    iteration = lower;
                } else if (lower == "forwards" || lower == "backwards"
                    || lower == "both") {
                    fill_mode = lower;
                } else if (lower == "normal" || lower == "reverse"
                    || lower == "alternate" || lower == "alternate-reverse") {
                    direction = lower;
                } else if (lower == "running" || lower == "paused") {
                    play_state = lower;
                } else if (lower != "none") {
                    name = token;
                }
            }
            names.push_back(std::move(name));
            durations.push_back(std::move(duration));
            delays.push_back(std::move(delay));
            timings.push_back(std::move(timing));
            iterations.push_back(std::move(iteration));
            directions.push_back(std::move(direction));
            fill_modes.push_back(std::move(fill_mode));
            play_states.push_back(std::move(play_state));
        }
        const auto join = [](const std::vector<std::string>& list) {
            auto result = std::string{};
            for (const auto& entry : list) {
                if (!result.empty()) result += ", ";
                result += entry;
            }
            return result;
        };
        animations.animation_name_value = names.empty() ? "none" : join(names);
        animations.animation_duration_value = durations.empty() ? "0s" : join(durations);
        animations.animation_delay_value = delays.empty() ? "0s" : join(delays);
        animations.animation_timing_function_value = timings.empty() ? "ease" : join(timings);
        animations.animation_iteration_count_value = iterations.empty() ? "1" : join(iterations);
        animations.animation_direction_value = directions.empty() ? "normal" : join(directions);
        animations.animation_fill_mode_value = fill_modes.empty() ? "none" : join(fill_modes);
        animations.animation_play_state_value = play_states.empty() ? "running" : join(play_states);
    }

inline void apply_animation_shorthand(node_style& style, const std::string& value)
{
    apply_animation_shorthand(style.mutable_animations(), value);
}

inline bool apply_animation_property(
    node_style::animation_data& animations,
    std::string_view name,
    const std::string& value)
{
    if (name == "animation") apply_animation_shorthand(animations, value);
    else if (name == "animation-name") animations.animation_name_value = value;
    else if (name == "animation-duration") animations.animation_duration_value = value;
    else if (name == "animation-delay") animations.animation_delay_value = value;
    else if (name == "animation-timing-function") {
        animations.animation_timing_function_value = value;
    } else if (name == "animation-iteration-count") {
        animations.animation_iteration_count_value = value;
    } else if (name == "animation-direction") animations.animation_direction_value = value;
    else if (name == "animation-fill-mode") animations.animation_fill_mode_value = value;
    else if (name == "animation-play-state") animations.animation_play_state_value = value;
    else return false;
    return true;
}

inline std::string serialize_animation_shorthand(
    const node_style::animation_data& animations)
    {
        const auto names = split_css_component_list(animations.animation_name_value, ',');
        if (names.empty()) return "none 0s ease 0s 1 normal none running";
        const auto durations = split_css_component_list(animations.animation_duration_value, ',');
        const auto timings = split_css_component_list(
            animations.animation_timing_function_value, ',');
        const auto delays = split_css_component_list(animations.animation_delay_value, ',');
        const auto iterations = split_css_component_list(
            animations.animation_iteration_count_value, ',');
        const auto directions = split_css_component_list(
            animations.animation_direction_value, ',');
        const auto fills = split_css_component_list(animations.animation_fill_mode_value, ',');
        const auto plays = split_css_component_list(animations.animation_play_state_value, ',');
        const auto coordinated = [](const auto& list, size_t index,
                                    std::string_view initial) {
            return list.empty() ? std::string(initial)
                : trim_value(list[index % list.size()]);
        };
        auto result = std::string{};
        for (size_t index = 0U; index < names.size(); ++index) {
            if (!result.empty()) result += ", ";
            result += trim_value(names[index]) + " "
                + coordinated(durations, index, "0s") + " "
                + coordinated(timings, index, "ease") + " "
                + coordinated(delays, index, "0s") + " "
                + coordinated(iterations, index, "1") + " "
                + coordinated(directions, index, "normal") + " "
                + coordinated(fills, index, "none") + " "
                + coordinated(plays, index, "running");
        }
        return result;
    }

inline std::optional<float> parse_registered_custom_number(
    std::string_view value,
    registered_property_syntax syntax)
{
    const auto source = ascii_lower(trim_value(value));
    const auto unit = syntax == registered_property_syntax::angle
        ? std::string_view{"deg"} : std::string_view{"%"};
    if (!source.ends_with(unit)) return std::nullopt;
    const auto number = source.substr(0U, source.size() - unit.size());
    if (number.empty()) return std::nullopt;
    char* end = nullptr;
    const auto parsed = std::strtof(number.c_str(), &end);
    if (end != number.c_str() + number.size() || !std::isfinite(parsed)) {
        return std::nullopt;
    }
    return parsed;
}

inline void configure_keyframes(node_style::animation_data& animations,
    const std::unordered_map<std::string,css_opacity_keyframes>& definitions,
    const std::unordered_map<std::string,registered_custom_property>& registrations = {})
    {
        animations.keyframe_animations.clear();
        const auto names = split_css_component_list(animations.animation_name_value, ',');
        if (names.empty()) return;
        const auto durations = split_css_component_list(animations.animation_duration_value, ',');
        const auto delays = split_css_component_list(animations.animation_delay_value, ',');
        const auto timings = split_css_component_list(
            animations.animation_timing_function_value, ',');
        const auto iteration_counts = split_css_component_list(
            animations.animation_iteration_count_value, ',');
        const auto directions = split_css_component_list(
            animations.animation_direction_value, ',');
        const auto fill_modes = split_css_component_list(
            animations.animation_fill_mode_value, ',');
        const auto play_states = split_css_component_list(
            animations.animation_play_state_value, ',');
        const auto coordinated = [](const auto& list, size_t index,
                                    std::string_view initial) {
            return list.empty() ? std::string(initial)
                : ascii_lower(trim_value(list[index % list.size()]));
        };
        const auto count = std::min(
            names.size(), node_style::animation_data::max_keyframe_animation_tracks);
        animations.keyframe_animations.reserve(count);
        for (size_t index = 0U; index < count; ++index) {
            node_style::animation_data::keyframe_animation track;
            track.name = trim_value(names[index]);
            const auto normalized_name = ascii_lower(track.name);
            track.duration_ms = std::max(0.0F, parse_css_time_ms(
                coordinated(durations, index, "0s")));
            track.delay_ms = parse_css_time_ms(coordinated(delays, index, "0s"));
            const auto iteration = coordinated(iteration_counts, index, "1");
            if (iteration == "infinite") {
                track.iterations = std::numeric_limits<float>::infinity();
            } else {
                char* parsed_end = nullptr;
                const auto parsed = std::strtof(iteration.c_str(), &parsed_end);
                track.iterations = parsed_end == iteration.c_str() + iteration.size()
                        && std::isfinite(parsed)
                    ? std::max(0.0F, parsed) : 1.0F;
            }
            const auto direction = coordinated(directions, index, "normal");
            track.direction = direction == "reverse"
                ? node_style::animation_data::direction_kind::reverse
                : direction == "alternate"
                    ? node_style::animation_data::direction_kind::alternate
                    : direction == "alternate-reverse"
                        ? node_style::animation_data::direction_kind::alternate_reverse
                        : node_style::animation_data::direction_kind::normal;
            const auto fill_mode = coordinated(fill_modes, index, "none");
            track.fill_mode = fill_mode == "forwards"
                ? node_style::animation_data::fill_kind::forwards
                : fill_mode == "backwards"
                    ? node_style::animation_data::fill_kind::backwards
                    : fill_mode == "both"
                        ? node_style::animation_data::fill_kind::both
                        : node_style::animation_data::fill_kind::none;
            track.play_state = coordinated(play_states, index, "running") == "paused"
                ? node_style::animation_data::play_kind::paused
                : node_style::animation_data::play_kind::running;
            node_style::transition_timing timing;
            parse_animation_timing(coordinated(timings, index, "ease"), timing);
            track.x1 = timing.x1; track.y1 = timing.y1;
            track.x2 = timing.x2; track.y2 = timing.y2;
            track.step_count = timing.step_count;
            track.timing_kind = timing.kind;
            track.step_position = timing.steps_position;
            const auto definition = definitions.find(normalized_name);
            if (normalized_name != "none" && definition != definitions.end()) {
                track.opacity_keyframes = definition->second.opacity_stops;
                track.translation_keyframes = definition->second.translation_stops;
                track.scale_keyframes = definition->second.scale_stops;
                track.rotation_keyframes = definition->second.rotation_stops;
                track.filter_keyframes = definition->second.filter_stops;
                for (const auto& [property_name, raw_stops]
                     : definition->second.custom_property_stops) {
                    const auto registration = registrations.find(property_name);
                    if (registration == registrations.end()) continue;
                    node_style::custom_property_animation custom;
                    custom.name = property_name;
                    custom.unit = registration->second.syntax
                            == registered_property_syntax::angle
                        ? "deg" : "%";
                    for (const auto& raw : raw_stops) {
                        const auto number = parse_registered_custom_number(
                            raw.value, registration->second.syntax);
                        if (number.has_value()) {
                            custom.keyframes.push_back({raw.offset, *number});
                        }
                    }
                    if (custom.keyframes.size() >= 2U) {
                        track.custom_property_animations.push_back(std::move(custom));
                    }
                }
            }
            const auto has_supported_effect =
                track.opacity_keyframes.size() >= 2U
                    || track.translation_keyframes.size() >= 2U
                    || track.scale_keyframes.size() >= 2U
                    || track.rotation_keyframes.size() >= 2U
                    || track.filter_keyframes.size() >= 2U
                    || !track.custom_property_animations.empty();
            if (has_supported_effect) {
                std::ostringstream signature;
                signature << normalized_name << '|' << track.duration_ms << '|'
                    << track.delay_ms << '|' << track.iterations << '|'
                    << static_cast<unsigned>(track.direction) << '|'
                    << static_cast<unsigned>(track.timing_kind) << ','
                    << track.step_count << ','
                    << static_cast<unsigned>(track.step_position) << '|'
                    << track.x1 << ',' << track.y1 << ',' << track.x2 << ',' << track.y2;
                for (const auto& stop : track.opacity_keyframes)
                    signature << "|o" << stop.offset << ':' << stop.opacity;
                for (const auto& stop : track.translation_keyframes) {
                    signature << "|t" << stop.offset << ':'
                        << stop.x.value << ',' << static_cast<unsigned>(stop.x.unit)
                        << ',' << stop.x.pixel_offset << ':'
                        << stop.y.value << ',' << static_cast<unsigned>(stop.y.unit)
                        << ',' << stop.y.pixel_offset;
                }
                for (const auto& stop : track.scale_keyframes)
                    signature << "|s" << stop.offset << ':' << stop.x << ',' << stop.y;
                for (const auto& stop : track.rotation_keyframes)
                    signature << "|r" << stop.offset << ':' << stop.degrees;
                for (const auto& stop : track.filter_keyframes)
                    signature << "|f" << stop.offset << ':' << stop.value;
                for (const auto& custom : track.custom_property_animations) {
                    signature << "|c" << custom.name << ':' << custom.unit;
                    for (const auto& stop : custom.keyframes) {
                        signature << ',' << stop.offset << ':' << stop.value;
                    }
                }
                track.signature = signature.str();
            }
            animations.keyframe_animations.push_back(std::move(track));
        }
    }

inline void configure_keyframes(node_style& style,
    const std::unordered_map<std::string,css_opacity_keyframes>& definitions,
    const std::unordered_map<std::string,registered_custom_property>& registrations = {})
{
    if (!style.has_animation_data()) return;
    configure_keyframes(style.mutable_animations(), definitions, registrations);
}

inline void append_keyframe(
        css_opacity_keyframes& definition,
        std::string selector,
        const std::vector<css_declaration>& declarations)
    {
        const auto opacity = std::find_if(
            declarations.begin(), declarations.end(), [](const auto& declaration) {
                return declaration.name == "opacity";
            });
        const auto transform = std::find_if(
            declarations.begin(), declarations.end(), [](const auto& declaration) {
                return declaration.name == "transform";
            });
        const auto filter = std::find_if(
            declarations.begin(), declarations.end(), [](const auto& declaration) {
                return declaration.name == "filter";
            });
        std::vector<const css_declaration*> custom_properties;
        for (const auto& declaration : declarations) {
            if (declaration.name.starts_with("--")) {
                custom_properties.push_back(&declaration);
            }
        }
        const auto transform_value = transform == declarations.end()
            ? std::optional<parsed_keyframe_transform>{}
            : parse_keyframe_transform(transform->value);
        for (auto component : split_css_component_list(selector, ',')) {
            component = ascii_lower(trim_value(std::move(component)));
            float offset = -1;
            if (component == "from") offset = 0;
            else if (component == "to") offset = 1;
            else if (component.ends_with('%')) {
                component.pop_back();
                offset = std::strtof(component.c_str(), nullptr) / 100.0F;
            }
            if (offset < 0 || offset > 1) continue;
            if (opacity != declarations.end()) {
                definition.opacity_stops.push_back({
                    offset,
                    std::clamp(std::strtof(opacity->value.c_str(), nullptr), 0.0F, 1.0F)});
            }
            if (transform_value.has_value()) {
                definition.translation_stops.push_back({
                    offset, transform_value->translate_x, transform_value->translate_y});
                definition.scale_stops.push_back({
                    offset, transform_value->scale_x, transform_value->scale_y});
                definition.rotation_stops.push_back({
                    offset, transform_value->rotate_degrees});
            }
            if (filter != declarations.end()) {
                definition.filter_stops.push_back({offset, filter->value});
            }
            for (const auto* custom : custom_properties) {
                if (custom->name.size() > 128U || custom->value.size() > 128U) {
                    continue;
                }
                auto found = definition.custom_property_stops.find(custom->name);
                if (found == definition.custom_property_stops.end()) {
                    if (definition.custom_property_stops.size() >= 8U) continue;
                    found = definition.custom_property_stops.emplace(
                        custom->name,
                        std::vector<css_opacity_keyframes::custom_property_stop>{})
                        .first;
                }
                if (found->second.size() < 64U) {
                    found->second.push_back({offset, custom->value});
                }
            }
        }
    }

inline void finish_keyframes(
        std::unordered_map<std::string,css_opacity_keyframes>& definitions,
        std::string name,
        css_opacity_keyframes definition)
    {
        const auto normalize = [](auto& stops) {
            std::stable_sort(
                stops.begin(), stops.end(),
                [](const auto& left, const auto& right) { return left.offset < right.offset; });
            using stop_type = typename std::decay_t<decltype(stops)>::value_type;
            std::vector<stop_type> unique;
            for (const auto& stop : stops) {
                if (!unique.empty()
                    && std::abs(unique.back().offset - stop.offset) < 0.0001F) {
                    unique.back() = stop;
                } else {
                    unique.push_back(stop);
                }
            }
            stops = std::move(unique);
        };
        normalize(definition.opacity_stops);
        normalize(definition.translation_stops);
        normalize(definition.scale_stops);
        normalize(definition.rotation_stops);
        normalize(definition.filter_stops);
        for (auto& [name, stops] : definition.custom_property_stops) {
            static_cast<void>(name);
            normalize(stops);
        }
        if (definition.rotation_stops.size() == 1U
            && definition.rotation_stops.front().offset > 0) {
            definition.rotation_stops.insert(definition.rotation_stops.begin(), {0, 0});
        }
        if (definition.translation_stops.size() == 1U
            && definition.translation_stops.front().offset > 0) {
            definition.translation_stops.insert(
                definition.translation_stops.begin(),
                {0, {0, length_unit::pixels}, {0, length_unit::pixels}});
        }
        if (definition.scale_stops.size() == 1U
            && definition.scale_stops.front().offset > 0) {
            definition.scale_stops.insert(
                definition.scale_stops.begin(), {0, 1, 1});
        }
        if (definition.opacity_stops.size() >= 2U
            || definition.translation_stops.size() >= 2U
            || definition.scale_stops.size() >= 2U
            || definition.rotation_stops.size() >= 2U
            || definition.filter_stops.size() >= 2U
            || std::any_of(
                definition.custom_property_stops.begin(),
                definition.custom_property_stops.end(),
                [](const auto& entry) { return entry.second.size() >= 2U; })) {
            definitions[ascii_lower(trim_value(std::move(name)))] =
                std::move(definition);
        }
    }

} // namespace webscene_native::css
