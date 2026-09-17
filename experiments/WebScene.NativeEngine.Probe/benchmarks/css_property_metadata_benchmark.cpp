#include "webscene_css_property_mask.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string_view>

int main()
{
    // Keep the mix stable: fifteen high-frequency direct fields and nine
    // effective-property registry entries. This measures dispatch overhead,
    // not parser or layout work.
    constexpr std::array<std::string_view, 24> names{
        "width", "height", "display", "position", "color", "background",
        "background-color", "font-size", "font-family", "font-weight",
        "line-height", "text-align", "visibility", "opacity", "transform",
        "margin", "margin-left", "padding", "padding-inline-start", "gap",
        "border-width", "border-top-color", "inset-inline-start", "overflow"};
    constexpr size_t rounds = 1000000U;
    uint64_t checksum = 0U;
    const auto started = std::chrono::steady_clock::now();
    for (size_t round = 0; round < rounds; ++round) {
        for (const auto name : names) {
            checksum ^= webscene_native::css::property_mask(name);
            checksum = (checksum << 1U) | (checksum >> 63U);
        }
    }
    const auto elapsed = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - started).count();
    std::cout << "lookups=" << rounds * names.size()
              << " elapsed_ms=" << elapsed
              << " checksum=" << checksum << '\n';
}
