#include "webscene_css_specified_ir.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string_view>

int main()
{
    using namespace webscene_native::css;
    struct sample final { css_property_id property; std::string_view value; };
    constexpr std::array<sample, 24> samples{{
        {css_property_id::display, "flex"},
        {css_property_id::position, "absolute"},
        {css_property_id::visibility, "hidden"},
        {css_property_id::text_align, "center"},
        {css_property_id::width, "120px"},
        {css_property_id::height, "80%"},
        {css_property_id::margin_left, "auto"},
        {css_property_id::font_size, "16px"},
        {css_property_id::line_height, "1.5"},
        {css_property_id::stroke_width, "2px"},
        {css_property_id::padding, "1px 2px 3px 4px"},
        {css_property_id::border_width, "1px 2px 3px 4px"},
        {css_property_id::gap, "4px 8px"},
        {css_property_id::border_spacing, "2px 3px"},
        {css_property_id::color, "rebeccapurple"},
        {css_property_id::background_color, "#123456"},
        {css_property_id::border_top_color, "currentColor"},
        {css_property_id::container_name, "main"},
        {css_property_id::background_position, "center 20%"},
        {css_property_id::transition, "width 200ms ease"},
        {css_property_id::transition_property, "width, opacity"},
        {css_property_id::animation, "fade 1s linear"},
        {css_property_id::animation_name, "fade"},
        {css_property_id::scrollbar_color, "#777 transparent"},
    }};
    constexpr size_t rounds = 100000U;
    uint64_t checksum = 0U;
    const auto started = std::chrono::steady_clock::now();
    for (size_t round = 0; round < rounds; ++round) {
        for (const auto& sample : samples) {
            const auto value = compile_specified_value(sample.property, sample.value);
            checksum += static_cast<uint8_t>(value.kind) + (value.valid ? 257U : 0U);
            checksum = (checksum << 1U) | (checksum >> 63U);
        }
    }
    const auto elapsed = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - started).count();
    std::cout << "compilations=" << rounds * samples.size()
              << " elapsed_ms=" << elapsed
              << " checksum=" << checksum << '\n';
}
