#include "webscene_native_form_state.h"

#include <chrono>
#include <clocale>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
void expect(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

int main() {
    using namespace webscene_native::forms;
    for (const auto* invalid : {"", "+1", "1.", " 1", "1x", "NaN",
             "Infinity", "1e309", "1e-9999"}) {
        expect(!finite_number(invalid).has_value(),
            "invalid finite form number was accepted");
    }
    expect(finite_number(".5") == 0.5, "fractional form number failed");
    expect(finite_number("-2.25e2") == -225.0, "exponent form number failed");
    expect(finite_number("0e-9999") == 0.0,
        "zero with extreme exponent was rejected");
    expect(finite_number("5e-324") == std::numeric_limits<double>::denorm_min(),
        "representable subnormal was rejected");
    const auto negative_zero = finite_number("-0");
    expect(negative_zero.has_value() && std::signbit(*negative_zero),
        "negative zero was not preserved");
    expect(format_finite_number(0.1) == "0.1", "short decimal changed");
    expect(format_finite_number(-0.0) == "-0", "negative zero changed");
    expect(format_finite_number(1.25) == "1.25", "decimal changed");
    expect(format_finite_number(100'000.0) == "100000",
        "fixed/scientific boundary changed");
    expect(format_finite_number(1'000'000.0) == "1e+06",
        "scientific notation boundary changed");
    expect(format_finite_number(3'128'091'567.555499) ==
            "3.128091567555499e+09",
        "long significant digits used the wrong notation");
    expect(format_finite_number(0.0001) == "0.0001",
        "small fixed notation boundary changed");
    expect(format_finite_number(0.00001) == "1e-05",
        "small scientific notation boundary changed");
    expect(format_finite_number(std::numeric_limits<double>::denorm_min())
            == "5e-324", "subnormal formatting changed");
    expect(!format_finite_number(INFINITY).has_value(),
        "non-finite value was formatted");
    expect(format_time_number(3723123.0) == "01:02:03.123",
        "millisecond time serialization changed");
    expect(format_time_number(0.0) == "00:00",
        "zero time serialization changed");

    const auto previous_locale = std::string(std::setlocale(LC_NUMERIC, nullptr));
    if (std::setlocale(LC_NUMERIC, "de_DE.UTF-8") != nullptr) {
        expect(finite_number("1.25") == 1.25,
            "parsing followed the process decimal locale");
        expect(!finite_number("1,25").has_value(),
            "a locale-specific decimal separator was accepted");
        expect(format_finite_number(1.25) == "1.25",
            "formatting followed the process decimal locale");
    }
    std::setlocale(LC_NUMERIC, previous_locale.c_str());

    const auto started = std::chrono::steady_clock::now();
    double checksum = 0.0;
    for (int index = 0; index < 10'000; ++index) {
        const auto value = 0.1 + static_cast<double>(index % 101) * 0.125;
        const auto formatted = format_finite_number(value);
        expect(formatted.has_value(), "finite value formatting failed");
        const auto parsed = finite_number(*formatted);
        expect(parsed.has_value() && *parsed == value,
            "formatted value did not round-trip");
        checksum += *parsed;
    }
    const auto elapsed_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - started).count();
    expect(checksum > 0.0, "conversion loop was not executed");
    expect(elapsed_ms < 500.0, "10,000 form conversions exceeded 500 ms");
    std::cout << "10,000 finite form round-trips: " << elapsed_ms << " ms\n";
}
