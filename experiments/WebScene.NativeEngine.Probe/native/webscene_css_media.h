#pragma once
#include "webscene_css_container_queries.h"
#include "webscene_css_matching.h"
#include "webscene_accessibility_preferences.h"
#include <cstdlib>

namespace webscene_native::css {
struct media_environment {
    float width{};
    float height{};
    bool dark{false};
    bool reduced_motion{false};
    bool forced_colors{false};
    bool contrast_more{false};

    media_environment() = default;
    media_environment(float width_value, float height_value)
        : media_environment(width_value, height_value, false) {}
    media_environment(float width_value, float height_value, bool dark_value)
        : width(width_value), height(height_value), dark(dark_value)
    {
        const auto preferences = get_active_accessibility_preferences();
        reduced_motion = preferences.reduced_motion;
        forced_colors = preferences.forced_colors;
        contrast_more = preferences.contrast_more;
    }
    media_environment(float width_value, float height_value, bool dark_value,
        bool reduced_motion_value, bool forced_colors_value = false,
        bool contrast_more_value = false)
        : width(width_value), height(height_value), dark(dark_value),
          reduced_motion(reduced_motion_value), forced_colors(forced_colors_value),
          contrast_more(contrast_more_value) {}
};
inline bool media_matches(std::string query, const media_environment& environment)
{
    if (is_container_query(query)) return true;
        std::transform(query.begin(), query.end(), query.begin(), [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
        size_t alternative_start = 0;
        while (alternative_start <= query.size()) {
            auto alternative_end = query.find(',', alternative_start);
            if (alternative_end == std::string::npos) alternative_end = query.size();
            auto alternative = trim_value(std::string_view(query).substr(
                alternative_start,
                alternative_end - alternative_start));
            const auto negated = alternative.starts_with("not ");
            if (negated) alternative = trim_value(std::string_view(alternative).substr(4U));
            auto matches = true;
            if (alternative.starts_with("all and")) {
                alternative = trim_value(std::string_view(alternative).substr(7U));
            } else if (alternative.starts_with("screen and")) {
                alternative = trim_value(std::string_view(alternative).substr(10U));
            } else if (alternative == "all" || alternative == "screen"
                || alternative.starts_with("(")) {
            } else if (alternative == "print" || alternative.starts_with("print and")) {
                matches = false;
            } else {
                matches = false;
            }

            size_t condition_start = 0;
            while (matches && (condition_start = alternative.find('(', condition_start))
                != std::string::npos) {
                const auto condition_end = alternative.find(')', condition_start + 1U);
                if (condition_end == std::string::npos) break;
                const auto condition = trim_value(std::string_view(alternative).substr(
                    condition_start + 1U,
                    condition_end - condition_start - 1U));
                const auto separator = condition.find(':');
                auto feature = trim_value(std::string_view(condition).substr(0, separator));
                auto value = separator == std::string::npos
                    ? std::string{} : trim_value(std::string_view(condition).substr(separator + 1U));
                auto range_operator = std::string{};
                if (separator == std::string::npos) {
                    for (const auto candidate : {std::string_view{"<="}, std::string_view{">="},
                         std::string_view{"<"}, std::string_view{">"}, std::string_view{"="}}) {
                        const auto position = condition.find(candidate);
                        if (position == std::string::npos) continue;
                        feature = trim_value(std::string_view(condition).substr(0, position));
                        value = trim_value(std::string_view(condition).substr(
                            position + candidate.size()));
                        range_operator = std::string(candidate);
                        break;
                    }
                }
                const auto number = std::strtof(value.c_str(), nullptr);
                if ((feature == "width" || feature == "height") && !range_operator.empty()) {
                    const auto actual = feature == "width" ? environment.width : environment.height;
                    if (range_operator == "<=") matches = actual <= number;
                    else if (range_operator == ">=") matches = actual >= number;
                    else if (range_operator == "<") matches = actual < number;
                    else if (range_operator == ">") matches = actual > number;
                    else matches = actual == number;
                } else if (feature == "max-width") matches = environment.width <= number;
                else if (feature == "min-width") matches = environment.width >= number;
                else if (feature == "max-height") matches = environment.height <= number;
                else if (feature == "min-height") matches = environment.height >= number;
                else if (feature == "orientation") {
                    matches = value == "landscape"
                        ? environment.width >= environment.height
                        : value == "portrait" && environment.height > environment.width;
                } else if (feature == "hover" || feature == "any-hover") {
                    matches = value == "hover";
                } else if (feature == "pointer" || feature == "any-pointer") {
                    matches = value == "fine";
                } else if (feature == "prefers-reduced-motion") {
                    matches = value == (environment.reduced_motion ? "reduce" : "no-preference");
                } else if (feature == "prefers-color-scheme") {
                    matches = value == (
                        environment.dark
                            ? "dark"
                            : "light");
                } else if (feature == "forced-colors") {
                    matches = value == (environment.forced_colors ? "active" : "none");
                } else if (feature == "prefers-contrast") {
                    matches = value == (environment.contrast_more ? "more" : "no-preference");
                } else {
                    matches = false;
                }
                condition_start = condition_end + 1U;
            }
            if (negated) matches = !matches;
            if (matches) return true;
            if (alternative_end == query.size()) break;
            alternative_start = alternative_end + 1U;
        }
        return false;
    }

template<typename Record>
bool inventory_media(std::string query, Record&& record_feature)
    {
        std::transform(query.begin(), query.end(), query.begin(), [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
        auto supported = true;
        size_t alternative_start = 0U;
        while (alternative_start <= query.size()) {
            auto alternative_end = query.find(',', alternative_start);
            if (alternative_end == std::string::npos) alternative_end = query.size();
            auto alternative = trim_value(std::string_view(query).substr(
                alternative_start, alternative_end - alternative_start));
            if (alternative.starts_with("not ")) {
                alternative = trim_value(std::string_view(alternative).substr(4U));
            }
            const auto first_condition = alternative.find('(');
            auto media_type = trim_value(std::string_view(alternative).substr(0U, first_condition));
            if (media_type.ends_with(" and")) media_type.resize(media_type.size() - 4U);
            media_type = trim_value(media_type);
            if (!media_type.empty()) {
                const auto known = media_type == "all" || media_type == "screen" || media_type == "print";
                record_feature(
                    "css",
                    "media-type:" + media_type,
                    known ? "supported" : "unsupported",
                    {},
                    "media-query-parser");
                supported = supported && known;
            }
            size_t condition_start = 0U;
            while ((condition_start = alternative.find('(', condition_start))
                != std::string::npos) {
                const auto condition_end = alternative.find(')', condition_start + 1U);
                if (condition_end == std::string::npos) {
                    supported = false;
                    break;
                }
                const auto condition = trim_value(std::string_view(alternative).substr(
                    condition_start + 1U, condition_end - condition_start - 1U));
                const auto separator = condition.find(':');
                auto feature = trim_value(std::string_view(condition).substr(0U, separator));
                auto range_syntax = false;
                if (separator == std::string::npos) {
                    const auto operator_position = condition.find_first_of("<>=");
                    if (operator_position != std::string::npos) {
                        feature = trim_value(std::string_view(condition).substr(
                            0U, operator_position));
                        range_syntax = feature == "width" || feature == "height";
                    }
                }
                static const std::unordered_set<std::string> supported_features{
                    "max-width", "min-width", "max-height", "min-height", "orientation",
                    "hover", "any-hover", "pointer", "any-pointer",
                    "prefers-reduced-motion", "prefers-color-scheme",
                    "forced-colors", "prefers-contrast"};
                const auto known = range_syntax || (separator != std::string::npos
                    && supported_features.contains(feature));
                record_feature(
                    "css",
                    "media-feature:" + (feature.empty() ? std::string("<missing>") : feature),
                    known ? "supported" : "unsupported",
                    {},
                    "media-query-parser");
                supported = supported && known;
                condition_start = condition_end + 1U;
            }
            if (alternative_end == query.size()) break;
            alternative_start = alternative_end + 1U;
        }
        return supported;
    }

} // namespace webscene_native::css
