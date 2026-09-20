#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace webscene_native::css {

struct accessibility_preferences final {
    bool dark{false};
    bool forced_colors{false};
    bool reduced_motion{false};
    bool contrast_more{false};
    bool native_system_colors{false};
    uint32_t accent_rgba{0x0067c0ffU};
    uint32_t accent_text_rgba{0xffffffffU};
    uint32_t canvas_rgba{0xffffffffU};
    uint32_t canvas_text_rgba{0x000000ffU};
    uint32_t highlight_rgba{0x0067c0ffU};
    uint32_t highlight_text_rgba{0xffffffffU};
};

inline thread_local accessibility_preferences active_accessibility_preferences{};

inline void set_active_accessibility_preferences(
    accessibility_preferences preferences) noexcept
{
    active_accessibility_preferences = preferences;
}

inline accessibility_preferences get_active_accessibility_preferences() noexcept
{
    return active_accessibility_preferences;
}

inline std::optional<uint32_t> system_color_rgba(std::string_view input)
{
    auto name = std::string(input);
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    const auto preferences = get_active_accessibility_preferences();
    const auto dark = preferences.dark;
    const auto forced = preferences.forced_colors;

    if (name == "canvas") return preferences.native_system_colors
        ? preferences.canvas_rgba : (dark ? 0x1e1e1effU : 0xffffffffU);
    if (name == "canvastext") return preferences.native_system_colors
        ? preferences.canvas_text_rgba : (dark ? 0xffffffffU : 0x000000ffU);
    if (name == "buttonface" || name == "field") {
        return forced ? (dark ? 0x000000ffU : 0xffffffffU)
            : (dark ? 0x333333ffU : 0xefefefffU);
    }
    if (name == "buttontext" || name == "fieldtext") {
        return dark ? 0xffffffffU : 0x000000ffU;
    }
    if (name == "graytext") return dark ? 0xaaaaaaffU : 0x6d6d6dffU;
    if (name == "highlight" || name == "selecteditem") return preferences.native_system_colors
        ? preferences.highlight_rgba : (dark ? 0x6aa9ffffU : 0x0067c0ffU);
    if (name == "accentcolor") return preferences.native_system_colors
        ? preferences.accent_rgba : (dark ? 0x6aa9ffffU : 0x0067c0ffU);
    if (name == "highlighttext" || name == "selecteditemtext"
        ) return preferences.native_system_colors
            ? preferences.highlight_text_rgba : 0xffffffffU;
    if (name == "accentcolortext") return preferences.native_system_colors
        ? preferences.accent_text_rgba : 0xffffffffU;
    if (name == "linktext") return dark ? 0x8ab4f8ffU : 0x0000eeffU;
    if (name == "visitedtext") return dark ? 0xc58af9ffU : 0x551a8bffU;
    if (name == "activetext") return 0xff0000ffU;
    if (name == "mark") return 0xffff00ffU;
    if (name == "marktext") return 0x000000ffU;
    return std::nullopt;
}

} // namespace webscene_native::css
