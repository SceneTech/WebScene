#include "webscene_css_ascii_number.h"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {
[[noreturn]] void fail(std::string_view message)
{
    std::cerr << "webscene_css_ascii_number_tests: " << message << '\n';
    std::exit(1);
}

void require(bool condition, std::string_view message)
{
    if (!condition) fail(message);
}

void require_length(std::string_view source, float expected, bool percent)
{
    const auto parsed = webscene_native::css::parse_ascii_inset_length(source);
    require(parsed.has_value(), "portable inset length was rejected");
    require(std::abs(parsed->value - expected) < 0.0001F,
        "portable inset length value differed");
    require(parsed->percent == percent, "portable inset length unit differed");
}
} // namespace

int main()
{
    require_length("0", 0.0F, false);
    require_length("+0PX", 0.0F, false);
    require_length("0e999999px", 0.0F, false);
    require_length("-0e-999999%", -0.0F, true);
    require_length("-.5px", -0.5F, false);
    require_length("1.25e2%", 125.0F, true);
    require_length("1e-2px", 0.01F, false);
    for (const auto invalid : {
             std::string_view{}, std::string_view{"+"}, std::string_view{"."},
             std::string_view{"1"}, std::string_view{"1e+px"}, std::string_view{"nanpx"},
             std::string_view{"infpx"}, std::string_view{"1e9999px"},
             std::string_view{"1px "}, std::string_view{"1em"},
             std::string_view{"1\0px", 4U}}) {
        require(!webscene_native::css::parse_ascii_inset_length(invalid).has_value(),
            "invalid portable inset length was accepted");
    }

    const auto started = std::chrono::steady_clock::now();
    auto checksum = 0.0F;
    for (auto iteration = 0U; iteration < 100000U; ++iteration) {
        const auto parsed = webscene_native::css::parse_ascii_inset_length("-12.5e-1px");
        require(parsed.has_value(), "repeated portable parse failed");
        checksum += parsed->value;
    }
    const auto elapsed = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - started).count();
    require(std::abs(checksum + 125000.0F) < 0.01F, "portable parse checksum differed");
    require(elapsed < 500.0, "100000 portable parses exceeded 500 ms");
    std::cout << "css-ascii-number parses=100000 elapsed-ms=" << elapsed << '\n';
}
