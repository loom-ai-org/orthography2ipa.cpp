// Python float behaviour the port must match: repr(float), which json.dumps
// writes for a float, and sum() of floats and ints.
#pragma once

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>

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

/// Python's sum() of floats (bltinmodule.c builtin_sum_impl), which since
/// Python 3.12 adds with Neumaier compensation. A plain running total differs
/// from it in the last bit. An int item is not a float to sum(): Python adds it
/// without compensation, so this class serves an all-float sequence only, and
/// PythonSum serves a sequence that holds ints.
class PythonFloatSum {
public:
    void add(double x) {
        const double t = sum_ + x;
        compensation_ += std::fabs(sum_) >= std::fabs(x) ? (sum_ - t) + x : (x - t) + sum_;
        sum_ = t;
    }
    double value() const {
        return compensation_ != 0.0 && std::isfinite(compensation_) ? sum_ + compensation_ : sum_;
    }

private:
    double sum_ = 0.0;
    double compensation_ = 0.0;
};

/// float() of the Python int written as *digits* (an optional '-' and decimal
/// digits, no leading zeros), correctly rounded. Python raises OverflowError
/// when the rounded value is out of range.
inline double py_int_to_float(const std::string& digits) {
    const double value = std::strtod(digits.c_str(), nullptr);
    if (std::isinf(value)) throw std::overflow_error("int too large to convert to float");
    return value;
}

/// The Python int a + b, both written as py_int_to_float takes them.
inline std::string py_int_add(const std::string& a, const std::string& b) {
    const bool a_negative = a[0] == '-', b_negative = b[0] == '-';
    std::string x = a.substr(a_negative), y = b.substr(b_negative);
    const auto magnitude_less = [](const std::string& l, const std::string& r) {
        return l.size() != r.size() ? l.size() < r.size() : l < r;
    };
    bool negative = a_negative;
    if (a_negative != b_negative && magnitude_less(x, y)) {
        std::swap(x, y);
        negative = b_negative;
    }
    // |x| >= |y| when the signs differ; the digits run least significant first.
    std::reverse(x.begin(), x.end());
    std::reverse(y.begin(), y.end());
    std::string out;
    int carry = 0;
    for (std::size_t i = 0; i < x.size() || i < y.size() || carry != 0; ++i) {
        const int dx = i < x.size() ? x[i] - '0' : 0, dy = i < y.size() ? y[i] - '0' : 0;
        int digit = a_negative == b_negative ? dx + dy + carry : dx - dy + carry;
        carry = digit < 0 ? -1 : digit / 10;
        digit -= carry * 10;
        out += static_cast<char>('0' + digit);
    }
    while (out.size() > 1 && out.back() == '0') out.pop_back();
    if (out == "0") return out;
    std::reverse(out.begin(), out.end());
    return negative ? "-" + out : out;
}

/// float(sum(items)) over Python ints and floats, step for step as
/// bltinmodule.c builtin_sum_impl in Python 3.12 and 3.13 adds them. sum()
/// starts on a C integer and adds ints exactly. The first float moves it to
/// the compensated float loop, where an int that fits a C long joins the high
/// part with no compensation. An int that overflows a C integer, in either
/// loop, leaves sum() on plain Python addition for the rest of the items:
/// exact while both sides are ints, rounded once per step after that. Python
/// 3.14 compensates the ints too. Py_ssize_t and long are 64 bits wide here,
/// as on Linux and macOS.
class PythonSum {
public:
    void add_float(double x) {
        switch (loop_) {
        case Loop::Int:
            float_sum_ = static_cast<double>(int_sum_) + x;
            loop_ = Loop::Float;
            return;
        case Loop::Float: {
            const double t = float_sum_ + x;
            compensation_ += std::fabs(float_sum_) >= std::fabs(x) ? (float_sum_ - t) + x : (x - t) + float_sum_;
            float_sum_ = t;
            return;
        }
        case Loop::Generic:
            if (exact_) float_sum_ = py_int_to_float(*exact_);
            exact_.reset();
            float_sum_ += x;
            return;
        }
    }

    /// *digits* as py_int_to_float takes them.
    void add_int(const std::string& digits) {
        std::int64_t b = 0;
        const bool fits = std::from_chars(digits.data(), digits.data() + digits.size(), b).ec == std::errc{};
        switch (loop_) {
        case Loop::Int:
            if (fits && (int_sum_ >= 0 ? b <= INT64_MAX - int_sum_ : b >= INT64_MIN - int_sum_)) {
                int_sum_ += b;
                return;
            }
            exact_ = py_int_add(std::to_string(int_sum_), digits);
            loop_ = Loop::Generic;
            return;
        case Loop::Float:
            if (fits) {
                float_sum_ += static_cast<double>(b);
                return;
            }
            float_sum_ = value() + py_int_to_float(digits);
            loop_ = Loop::Generic;
            return;
        case Loop::Generic:
            if (exact_) exact_ = py_int_add(*exact_, digits);
            else float_sum_ += py_int_to_float(digits);
            return;
        }
    }

    double value() const {
        switch (loop_) {
        case Loop::Int: return static_cast<double>(int_sum_);
        case Loop::Float:
            return compensation_ != 0.0 && std::isfinite(compensation_) ? float_sum_ + compensation_ : float_sum_;
        case Loop::Generic: break;
        }
        return exact_ ? py_int_to_float(*exact_) : float_sum_;
    }

private:
    enum class Loop { Int, Float, Generic };
    Loop loop_ = Loop::Int;
    std::int64_t int_sum_ = 0;
    double float_sum_ = 0.0;
    double compensation_ = 0.0;
    // The generic loop's result while it is still an int.
    std::optional<std::string> exact_;
};

} // namespace orthography2ipa::detail
