#include "webscene_css_text_values.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {
void require(bool condition, std::string_view message)
{
    if (condition) return;
    std::cerr << "CSS pseudo font-size test failed: " << message << '\n';
    std::exit(1);
}
}

int main()
{
    webscene_native::dom_node originating;
    originating.tag = "span";
    originating.style.font_size = 13.0F;
    const auto resolved = [&](const std::string& value) {
        return webscene_native::css::resolved_pseudo_font_size(originating, value);
    };
    require(std::abs(resolved("150%") - 19.5F) < 0.001F,
        "150% did not resolve from the originating 13px size");
    require(std::abs(resolved("1.5em") - 19.5F) < 0.001F,
        "1.5em did not resolve from the originating 13px size");
    require(std::abs(resolved("19.5px") - 19.5F) < 0.001F,
        "absolute pixels changed during pseudo resolution");
    require(resolved("inherit") == -1.0F && resolved("unset") == -1.0F,
        "inherit and unset did not preserve pseudo inheritance");
    require(resolved("initial") == 14.0F,
        "initial did not select the engine's medium font size");
    std::cout << "CSS pseudo font-size tests passed\n";
}
