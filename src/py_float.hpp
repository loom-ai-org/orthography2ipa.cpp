// Python repr(float), which json.dumps writes for a float.
#pragma once

#include <charconv>
#include <cmath>
#include <cstdlib>
#include <string>

namespace orthography2ipa::detail {

/// repr() of *value*: the shortest digits that read back as the same double,
/// and the nearest of those (std::to_chars), in fixed notation for exponents
/// -4 to 15 and in exponent notation outside that range, with ".0" on an
/// integral value.
inline std::string py_float(double value) {
    if (std::isnan(value)) return "NaN";
    if (std::isinf(value)) return value > 0 ? "Infinity" : "-Infinity";
    char buffer[64];
    const auto written = std::to_chars(buffer, buffer + sizeof buffer, value, std::chars_format::scientific);
    const std::string text(buffer, written.ptr);
    const auto mark = text.find('e');
    const int exponent = std::atoi(text.c_str() + mark + 1);
    const bool negative = text[0] == '-';
    std::string digits;
    for (std::size_t i = negative ? 1 : 0; i < mark; ++i)
        if (text[i] != '.') digits += text[i];
    std::string out;
    if (exponent < -4 || exponent >= 16) {
        out = digits.substr(0, 1);
        if (digits.size() > 1) out += "." + digits.substr(1);
        const std::string power = std::to_string(std::abs(exponent));
        out += std::string("e") + (exponent < 0 ? '-' : '+') + (power.size() < 2 ? "0" : "") + power;
    } else if (exponent < 0) {
        out = "0." + std::string(-exponent - 1, '0') + digits;
    } else if (static_cast<int>(digits.size()) <= exponent + 1) {
        out = digits + std::string(exponent + 1 - digits.size(), '0') + ".0";
    } else {
        out = digits.substr(0, exponent + 1) + "." + digits.substr(exponent + 1);
    }
    return negative ? "-" + out : out;
}

} // namespace orthography2ipa::detail
