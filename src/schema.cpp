// Direct port of orthography2ipa/schema.py — the pydantic v2 validation models;
// see the header for the contract and for the fidelity notes (report order, lax
// coercion, `extra='forbid'`).
#include "orthography2ipa/schema.hpp"

#include "orthography2ipa/exceptions.hpp"
#include "orthography2ipa/orthography2ipa.hpp"
#include "unicode_util.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>

namespace orthography2ipa {
namespace schema {
namespace {

// Direct port of orthography2ipa/schema.py — see the header for the contract.
#include "orthography2ipa/schema.hpp"

#include "orthography2ipa/exceptions.hpp"
#include "unicode_util.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>


namespace fs = std::filesystem;
using uni::category;
using uni::to_utf32;
using uni::to_utf8;

// ═══════════════════════════════════════════════════════════════════════════
// The parsed document — json.load()'s result, with its types intact
// ═══════════════════════════════════════════════════════════════════════════
//
// The runtime loader in src/orthography2ipa.cpp reads specs through
// boost::property_tree, which erases the difference between `null` and `""`,
// between `2` and `"2"`, and between an object and an array — every one of
// which pydantic's lax mode keys a coercion off, and which `extra='forbid'`
// needs to know the declared keys from. schema.py therefore gets its own
// reader: it keeps each object's keys in DOCUMENT order, because that is the
// order pydantic reports a model's unknown keys and a dict field's entries in.

class Json {
public:
    enum class Kind { Null, Bool, Int, Float, Str, List, Object };

    Kind kind = Kind::Null;
    bool boolean = false;
    long long integer = 0;
    double real = 0.0;
    std::string str;
    std::vector<Json> list;
    std::vector<std::pair<std::string, Json>> object;   // document order, keys unique

    /// Exact decimal text of an integer literal too large for `long long`.
    /// Python's int has no bound and pydantic keeps it, so the digits survive
    /// into the message and the comparisons; `integer` saturates.
    std::string big_digits;

    /// A \uXXXX escape whose surrogate half never paired. pydantic-core hands
    /// such a string back as raw, and the FIELD reports string_unicode; the
    /// flag is the port's record of that, set at parse time.
    bool surrogate_pair_broken = false;

    bool is_null() const { return kind == Kind::Null; }
    bool is_int() const { return kind == Kind::Int; }

    const Json* member(const std::string& key) const {
        for (const auto& entry : object)
            if (entry.first == key) return &entry.second;
        return nullptr;
    }

    std::size_t code_points() const { return to_utf32(str).size(); }

    std::string repr() const;    // Python's repr(), for the {v!r} in messages
};

/// Python's repr() of a float: the shortest decimal that round-trips, fixed
/// notation while the value has at most 16 digits before the point, and
/// `1e+16` / `5e-324` style outside it.
std::string float_repr(double value) {
    if (std::isnan(value)) return "nan";
    if (std::isinf(value)) return value < 0 ? "-inf" : "inf";
    char buffer[40];
    auto written = std::to_chars(buffer, buffer + sizeof(buffer), value);
    if (written.ec != std::errc{}) return std::to_string(value);
    const std::string shortest(buffer, static_cast<std::size_t>(written.ptr - buffer));
    // %.*g over increasing precision gives the same shortest digits and tells
    // us where the point sits, which is what the fixed/scientific choice needs.
    for (int precision = 1; precision <= 17; ++precision) {
        char candidate[64];
        std::snprintf(candidate, sizeof(candidate), "%.*g", precision, value);
        if (std::strtod(candidate, nullptr) != value) continue;
        std::string text(candidate);
        const auto e = text.find('e');
        std::string mantissa = e == std::string::npos ? text : text.substr(0, e);
        int exponent = 0;
        if (e != std::string::npos) exponent = std::stoi(text.substr(e + 1));
        bool negative = false;
        if (!mantissa.empty() && (mantissa.front() == '-' || mantissa.front() == '+')) {
            negative = mantissa.front() == '-';
            mantissa.erase(mantissa.begin());
        }
        const auto dot = mantissa.find('.');
        std::string digits = dot == std::string::npos ? mantissa
                                                 : mantissa.substr(0, dot) + mantissa.substr(dot + 1);
        const auto first = digits.find_first_not_of('0');
        std::string out = negative ? "-" : "";
        if (first == std::string::npos) return out + "0.0";
        const auto last = digits.find_last_not_of('0');
        digits = digits.substr(first, last - first + 1);
        // Dropping a leading zero moves the decimal point one place left, and
        // every dropped zero sits at or before the point (they ARE the integer
        // part's zeros, for a mantissa of the shape 0.xxx / 0.0xxx). Without
        // this, 0.92 rendered as 9.2 — measured on AncestorModel.weight
        // (0.92 -> 9.2), ValidCeilingModel.per (0.2295 -> 2.295) and the
        // WeightedGraphemeModel weights.
        const int point = (dot == std::string::npos ? static_cast<int>(mantissa.size())
                                                   : static_cast<int>(dot)) + exponent
                          - static_cast<int>(first);
        if (point <= -4 || point > 16) {                    // scientific
            out += digits.front();
            if (digits.size() > 1) out += "." + digits.substr(1);
            char exponent_text[16];
            std::snprintf(exponent_text, sizeof(exponent_text), "e%+03d", point - 1);
            out += exponent_text;
        } else if (point <= 0) {                            // 0.000ddd
            out += "0." + std::string(static_cast<std::size_t>(-point), '0') + digits;
        } else if (static_cast<std::size_t>(point) >= digits.size()) {
            out += digits + std::string(static_cast<std::size_t>(point) - digits.size(), '0') + ".0";
        } else {
            out += digits.substr(0, static_cast<std::size_t>(point)) + "." +
                   digits.substr(static_cast<std::size_t>(point));
        }
        return out;
    }
    return std::to_string(value);
}

std::string Json::repr() const {
    switch (kind) {
        case Kind::Null: return "None";
        case Kind::Bool: return boolean ? "True" : "False";
        case Kind::Int: return big_digits.empty() ? std::to_string(integer) : big_digits;
        case Kind::Float: return float_repr(real);
        case Kind::Str: return detail::py_repr(str);
        case Kind::List: {
            std::string out = "[";
            for (std::size_t i = 0; i < list.size(); ++i) {
                if (i) out += ", ";
                out += list[i].repr();
            }
            return out + "]";
        }
        case Kind::Object: {
            std::string out = "{";
            for (std::size_t i = 0; i < object.size(); ++i) {
                if (i) out += ", ";
                out += detail::py_repr(object[i].first) + ": " + object[i].second.repr();
            }
            return out + "}";
        }
    }
    return {};
}



/// The reader, over CODE POINTS: json.decoder's JSONDecodeError reports a
/// position in characters, not bytes.
class Reader {
public:
    using Kind = Json::Kind;

    explicit Reader(const std::u32string& text) : text_(text) {}

    Json document() {
        skip_space();
        Json root = value();
        skip_space();
        if (pos_ != text_.size()) fail("Extra data");
        return root;
    }

private:
    const std::u32string& text_;
    std::size_t pos_ = 0;
    /// Set by the escape reader when the \uXXXX surrogate never paired; read
    /// back by string_value() when it finalizes the parsed string.
    bool string_has_surrogate_ = false;

    [[noreturn]] void fail(const std::string& message) const {
        std::size_t line = 1, start = 0;
        for (std::size_t i = 0; i < pos_ && i < text_.size(); ++i)
            if (text_[i] == U'\n') { ++line; start = i + 1; }
        throw JsonDecodeError(message + ": line " + std::to_string(line) + " column " +
                                  std::to_string(pos_ - start + 1) + " (char " +
                                  std::to_string(pos_) + ")",
                              pos_, line, pos_ - start + 1);
    }

    bool done() const { return pos_ >= text_.size(); }
    char32_t at(std::size_t offset = 0) const {
        return pos_ + offset < text_.size() ? text_[pos_ + offset] : U'\0';
    }

    void skip_space() {
        while (!done()) {
            const char32_t cp = at();
            if (cp != U' ' && cp != U'\t' && cp != U'\n' && cp != U'\r') break;  // json's " \t\n\r"
            ++pos_;
        }
    }

    bool word(const char* name) {
        const std::u32string needle = to_utf32(name);
        if (text_.compare(pos_, needle.size(), needle) != 0) return false;
        pos_ += needle.size();
        return true;
    }

    static Json scalar(Kind kind) {
        Json out;
        out.kind = kind;
        return out;
    }

    Json value() {
        if (done()) fail("Expecting value");
        if (at() == U'{') return object_value();
        if (at() == U'[') return array_value();
        if (at() == U'"') return string_value();
        if (word("true")) { Json out = scalar(Kind::Bool); out.boolean = true; return out; }
        if (word("false")) return scalar(Kind::Bool);
        if (word("null")) return Json{};
        // Python's json accepts the JavaScript constants unless told not to.
        if (word("NaN")) { Json out = scalar(Kind::Float); out.real = NAN; return out; }
        if (word("Infinity")) { Json out = scalar(Kind::Float); out.real = INFINITY; return out; }
        if (at() == U'-') {
            if (word("-Infinity")) { Json out = scalar(Kind::Float); out.real = -INFINITY; return out; }
            if (word("-NaN")) { Json out = scalar(Kind::Float); out.real = NAN; return out; }
        }
        if (at() == U'-' || (at() >= U'0' && at() <= U'9')) return number_value();
        fail("Expecting value");
    }

    Json string_value() {
        const std::size_t opening = pos_;
        string_has_surrogate_ = false;
        ++pos_;
        std::u32string out;
        for (;;) {
            if (done()) { pos_ = opening; fail("Unterminated string starting at"); }
            const char32_t cp = at();
            if (cp == U'"') { ++pos_; break; }
            // CPython's py_scanstring raises AT the control character's own
            // index (strict mode); this loop used to fail one past it.
            if (cp < 0x20) fail("Invalid control character at");
            if (cp != U'\\') { out += cp; ++pos_; continue; }
            ++pos_;
            if (done()) { pos_ = opening; fail("Unterminated string starting at"); }
            const char32_t escape = at();
            ++pos_;
            switch (escape) {
                case U'"': out += U'"'; break;
                case U'\\': out += U'\\'; break;
                case U'/': out += U'/'; break;
                case U'b': out += U'\b'; break;
                case U'f': out += U'\f'; break;
                case U'n': out += U'\n'; break;
                case U'r': out += U'\r'; break;
                case U't': out += U'\t'; break;
                case U'u': out += unescape_u(); break;
                // CPython raises AT the backslash (end - 1 from its own
                // post-escape cursor); the first -- only reached the escape
                // character.
                default: pos_ -= 2; fail("Invalid \\escape");
            }
        }
        Json out_value = scalar(Kind::Str);
        out_value.str = to_utf8(out);
        out_value.surrogate_pair_broken = string_has_surrogate_;
        return out_value;
    }

    char32_t hex4(std::size_t skip = 0) {
        // CPython matches \\uXXXX with _utf16_re and reports the failure
        // position at the 'u' — the index AFTER the backslash — never at the
        // digit that broke the escape.
        const std::size_t u_index = skip > 0 ? pos_ : pos_ - 1;
        unsigned value = 0;
        for (int i = 0; i < 4; ++i) {
            const char32_t cp = at();
            unsigned digit;
            if (cp >= U'0' && cp <= U'9') digit = static_cast<unsigned>(cp - U'0');
            else if (cp >= U'a' && cp <= U'f') digit = static_cast<unsigned>(cp - U'a' + 10);
            else if (cp >= U'A' && cp <= U'F') digit = static_cast<unsigned>(cp - U'A' + 10);
            else {
                pos_ = u_index;
                fail("Invalid \\uXXXX escape");
            }
            value = value * 16 + digit;
            ++pos_;
        }
        return static_cast<char32_t>(value);
    }

    /// CPython's decoder pairs a high surrogate with the following
    /// `\uXXXX` low surrogate. An UNPAIRED one is a code point with no UTF-8
    /// form at all: the reference keeps it as a lone surrogate (and its own
    /// JSON dump then fails), so the port maps it to U+FFFD, which the parity
    /// harness never feeds it. See build/notes/worker_schema.md.
    char32_t unescape_u() {
        const char32_t first = hex4();
        if (first >= 0xD800 && first <= 0xDBFF && at() == U'\\' && at(1) == U'u') {
            const std::size_t saved = pos_;
            pos_ += 2;
            const char32_t second = hex4();
            if (second >= 0xDC00 && second <= 0xDFFF)
                return static_cast<char32_t>(0x10000 + ((first - 0xD800) << 10) + (second - 0xDC00));
            pos_ = saved;
        }
        if (first >= 0xD800 && first <= 0xDFFF) {
            // Unpaired on the string side: pydantic-core hands the raw bytes
            // back and the FIELD reports string_unicode, so the port marks
            // the parsed string instead of inventing an error here.
            string_has_surrogate_ = true;
            return 0xFFFD;
        }
        return first;
    }

    /// CPython's NUMBER_RE: `-?(0|[1-9]\d*)(\.\d+)?([eE][-+]?\d+)?`, int() when
    /// the text has no `.` and no exponent, float() otherwise.
    Json number_value() {
        // CPython's NUMBER_RE is `(-?(?:0|[1-9]\d*))(.\d+)?([eE][-+]?\d+)?\b`
        // — the fraction and exponent are OPTIONAL, and a subpiece only joins
        // the number when it COMPLETES (a bare `e` or `.` with nothing
        // acceptable after it backtracks to the last matched prefix). This
        // parser used to consume the `e` and then fail mid-number, which made
        // `{"a":1e}` read as an error inside the value instead of the
        // object parser's "Expecting ',' delimiter" at the `e`.
        const std::size_t begin = pos_;
        bool integral = true;
        if (at() == U'-') ++pos_;
        if (at() == U'0') {
            ++pos_;
        } else if (at() >= U'1' && at() <= U'9') {
            while (at() >= U'0' && at() <= U'9') ++pos_;
        } else {
            pos_ = begin;
            fail("Expecting value");
        }

        // Fraction: only when a digit follows the dot; the dot otherwise
        // belongs to whatever follows the number.
        std::size_t probe = pos_;
        if (probe < text_.size() && text_[probe] == U'.') {
            std::size_t cursor = probe + 1;
            if (cursor < text_.size() && text_[cursor] >= U'0' && text_[cursor] <= U'9') {
                integral = false;
                pos_ = probe;
                ++pos_;
                while (at() >= U'0' && at() <= U'9') ++pos_;
            }
        }
        // Exponent: only when digits follow the (optional) sign.
        probe = pos_;
        if (probe < text_.size() && (text_[probe] == U'e' || text_[probe] == U'E')) {
            std::size_t cursor = probe + 1;
            if (cursor < text_.size() && (text_[cursor] == U'+' || text_[cursor] == U'-')) ++cursor;
            if (cursor < text_.size() && text_[cursor] >= U'0' && text_[cursor] <= U'9') {
                integral = false;
                pos_ = probe;
                ++pos_;
                if (at() == U'+' || at() == U'-') ++pos_;
                while (at() >= U'0' && at() <= U'9') ++pos_;
            }
        }
        const std::string text = to_utf8(text_.substr(begin, pos_ - begin));
        Json out;
        if (!integral) {
            out.kind = Kind::Float;
            out.real = std::strtod(text.c_str(), nullptr);
            return out;
        }
        out.kind = Kind::Int;
        const std::string digits = text == "-0" ? "0" : text;
        const char* first = digits.data();
        const char* last = first + digits.size();
        if (std::from_chars(first, last, out.integer).ec != std::errc{}) {
            out.big_digits = digits;
            out.integer = !digits.empty() && digits.front() == '-' ? (-9223372036854775807LL - 1)
                                                                   : 9223372036854775807LL;
        }
        return out;
    }


    Json object_value() {
        ++pos_;
        Json out = scalar(Kind::Object);
        skip_space();
        if (at() == U'}') { ++pos_; return out; }
        for (;;) {
            skip_space();
            if (at() != U'"') fail("Expecting property name enclosed in double quotes");
            const Json key = string_value();
            skip_space();
            if (at() != U':') fail("Expecting ':' delimiter");
            ++pos_;
            skip_space();
            Json item = value();
            // Python builds a dict: a repeated key overwrites, in place.
            bool replaced = false;
            for (auto& entry : out.object)
                if (entry.first == key.str) { entry.second = std::move(item); replaced = true; break; }
            if (!replaced) out.object.emplace_back(std::move(key.str), std::move(item));
            skip_space();
            if (at() == U'}') { ++pos_; break; }
            if (at() != U',') fail("Expecting ',' delimiter");
            ++pos_;
        }
        return out;
    }

    Json array_value() {
        ++pos_;
        Json out = scalar(Kind::List);
        skip_space();
        if (at() == U']') { ++pos_; return out; }
        for (;;) {
            skip_space();
            out.list.push_back(value());
            skip_space();
            if (at() == U']') { ++pos_; break; }
            if (at() != U',') fail("Expecting ',' delimiter");
            ++pos_;
        }
        return out;
    }
};

std::string hex_byte(unsigned char value) {
    static const char* const digits = "0123456789abcdef";
    std::string out = "0x";
    out += digits[(value >> 4) & 0xF];
    out += digits[value & 0xF];
    return out;
}

/// Path.open(encoding="utf-8") decodes before the parser runs, and
/// UnicodeDecodeError is NOT one of the exceptions validate_all() catches: a
/// non-UTF-8 file takes the whole run down in the reference, so it does here.
std::u32string decode_utf8(const std::string& bytes) {
    std::u32string out;
    const auto* data = reinterpret_cast<const unsigned char*>(bytes.data());
    const std::size_t size = bytes.size();
    for (std::size_t i = 0; i < size;) {
        const unsigned char lead = data[i];
        std::size_t extra = 0;
        char32_t base = 0;
        if (lead < 0x80) { out += static_cast<char32_t>(lead); ++i; continue; }
        if ((lead & 0xE0) == 0xC0) { extra = 1; base = lead & 0x1Fu; }
        else if ((lead & 0xF0) == 0xE0) { extra = 2; base = lead & 0x0Fu; }
        else if ((lead & 0xF8) == 0xF0) { extra = 3; base = lead & 0x07u; }
        else throw UnicodeDecodeError("'utf-8' codec can't decode byte " + hex_byte(lead) +
                                      " in position " + std::to_string(i) + ": invalid start byte");
        if (i + extra >= size)
            throw UnicodeDecodeError("'utf-8' codec can't decode bytes in position " +
                                     std::to_string(i) + "-" + std::to_string(size - 1) +
                                     ": unexpected end of data");
        for (std::size_t k = 1; k <= extra; ++k) {
            if ((data[i + k] & 0xC0) != 0x80)
                throw UnicodeDecodeError("'utf-8' codec can't decode byte " +
                                         hex_byte(data[i + k]) + " in position " +
                                         std::to_string(i + k) + ": invalid continuation byte");
            base = (base << 6) | (data[i + k] & 0x3Fu);
        }
        if (base > 0x10FFFF || (base >= 0xD800 && base <= 0xDFFF))
            throw UnicodeDecodeError("'utf-8' codec can't decode byte " + hex_byte(lead) +
                                     " in position " + std::to_string(i) + ": illegal UTF-16 surrogate");
        i += extra + 1;
        out += base;
    }
    return out;
}


// ═══════════════════════════════════════════════════════════════════════════
// The report, and the coercions every model is built from
// ═══════════════════════════════════════════════════════════════════════════

/// pydantic collects EVERY failing field before raising, in a fixed order: the
/// model's declared fields in declaration order, then the unknown keys in
/// document order, then the model-level validators. `format_failure` prints
/// these in that order, so the order is part of the ported behaviour.
class Report {
public:
    std::vector<ValidationMessage> messages;

    void push(LocPart part) { path_.push_back(std::move(part)); }
    void pop() { path_.pop_back(); }
    std::vector<LocPart> path() const { return path_; }

    void add(const std::string& type, const std::string& message) {
        messages.push_back({path_, message, type});
    }

    /// A `raise ValueError(...)` inside a validator: pydantic prefixes the text
    /// and files it under `value_error`.
    void value_error(const std::string& message) { add("value_error", "Value error, " + message); }

    std::size_t mark() const { return messages.size(); }
    bool clean_since(std::size_t mark) const { return messages.size() == mark; }
    void truncate(std::size_t mark) { messages.resize(mark); }

private:
    std::vector<LocPart> path_;
};

/// Pushes one path element for the duration of a scope.
class Guard {
public:
    Guard(Report& report, LocPart part) : report_(report) { report_.push(std::move(part)); }
    explicit Guard(Report& report, const std::string& key) : report_(report) {
        report_.push(LocPart(key));
    }
    Guard(Report& report, const char* key) : report_(report) { report_.push(LocPart(std::string(key))); }
    Guard(const Guard&) = delete;
    Guard& operator=(const Guard&) = delete;
    ~Guard() { report_.pop(); }

private:
    Report& report_;
};

/// Python's str.strip() set: the Unicode White_Space property.
bool py_space(char32_t cp) {
    static const char32_t controls[] = {0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x1C, 0x1D, 0x1E, 0x1F, 0x85};
    for (const char32_t c : controls)
        if (cp == c) return true;
    const std::string cat = category(cp);
    return cat == "Zs" || cat == "Zl" || cat == "Zp";
}

std::u32string py_strip(const std::u32string& text) {
    std::size_t begin = 0, end = text.size();
    while (begin < end && py_space(text[begin])) ++begin;
    while (end > begin && py_space(text[end - 1])) --end;
    return text.substr(begin, end - begin);
}

/// pydantic's `int` from a str: an optional sign, ASCII digits with single
/// underscores BETWEEN them, and an optional fractional part that must be all
/// zeros — "3.0" is 3, while "3.", ".3", "1e3", "3.5" and "3__0" are not ints.
bool parse_int_text(const std::u32string& raw, std::u32string& digits) {
    const std::u32string text = py_strip(raw);
    std::size_t i = 0;
    std::u32string out;
    if (i < text.size() && (text[i] == U'+' || text[i] == U'-')) {
        if (text[i] == U'-') out += U'-';
        ++i;
    }
    std::size_t run = 0;
    while (i < text.size() && (text[i] == U'_' || (text[i] >= U'0' && text[i] <= U'9'))) {
        if (text[i] == U'_') {
            if (run == 0 || out.empty() || out.back() == U'_') return false;
            out += U'_';
            ++i;
            continue;
        }
        out += text[i];
        ++i;
        run = 1;
    }
    if (run == 0 || (!out.empty() && out.back() == U'_')) return false;
    if (i < text.size() && text[i] == U'.') {
        ++i;
        std::size_t zeros = 0;
        while (i < text.size() && (text[i] == U'0' || text[i] == U'_')) {
            if (text[i] == U'_') {
                if (zeros == 0 || (i > 0 && text[i - 1] == U'_')) return false;
            } else {
                ++zeros;
            }
            ++i;
        }
        if (zeros == 0) return false;
    }
    digits = out;
    return i == text.size();
}

/// pydantic's `float` from a str: Python's float() over ASCII digits with the
/// same underscore rule, plus the inf / nan spellings. Hex floats, Unicode
/// digits and anything else are `float_parsing` failures.
bool parse_float_text(const std::u32string& raw, double& value) {
    const std::u32string text = py_strip(raw);
    if (text.empty()) return false;
    std::size_t i = 0;
    std::string out;
    if (text[i] == U'+' || text[i] == U'-') { out += static_cast<char>(text[i]); ++i; }
    const std::u32string body = text.substr(i);
    std::u32string lowered;
    for (const char32_t cp : body) lowered += static_cast<char32_t>(std::tolower(static_cast<int>(cp)));
    if (lowered == U"inf" || lowered == U"infinity") {
        out += "inf";
        value = std::strtod(out.c_str(), nullptr);
        return true;
    }
    if (lowered == U"nan") { value = NAN; return true; }
    bool seen_digit = false, seen_dot = false, seen_exp = false, after_exp_digit = false;
    for (std::size_t k = 0; k < body.size(); ++k) {
        const char32_t cp = body[k];
        if (cp == U'_') {
            const bool before = k > 0 && body[k - 1] >= U'0' && body[k - 1] <= U'9';
            const bool after = k + 1 < body.size() && body[k + 1] >= U'0' && body[k + 1] <= U'9';
            if (!before || !after) return false;
            continue;
        }
        if (cp >= U'0' && cp <= U'9') {
            out += static_cast<char>(cp);
            seen_digit = true;
            if (seen_exp) after_exp_digit = true;
            continue;
        }
        if (cp == U'.' && !seen_dot && !seen_exp) { out += '.'; seen_dot = true; continue; }
        if ((cp == U'e' || cp == U'E') && seen_digit && !seen_exp) {
            out += 'e';
            seen_exp = true;
            continue;
        }
        if ((cp == U'+' || cp == U'-') && seen_exp && !after_exp_digit &&
            (body[k - 1] == U'e' || body[k - 1] == U'E')) {
            out += static_cast<char>(cp);
            continue;
        }
        return false;
    }
    if (!seen_digit || (seen_exp && !after_exp_digit)) return false;
    value = std::strtod(out.c_str(), nullptr);
    return true;
}

/// pydantic's `bool` from a str: an exact case-insensitive word from the
/// accepted set, with no whitespace tolerance at all (" true " is not a bool).
bool bool_word(const std::string& utf8, bool& value) {
    static const char* const truthy[] = {"true", "t", "yes", "y", "on", "1"};
    static const char* const falsy[] = {"false", "f", "no", "n", "off", "0"};
    std::string lowered;
    for (const char c : utf8) lowered += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (const char* word : truthy)
        if (lowered == word) { value = true; return true; }
    for (const char* word : falsy)
        if (lowered == word) { value = false; return true; }
    return false;
}

// ─── the scalar coercions, one pydantic error each ─────────────────────────

bool scalar_str(const Json& v, std::string& out) {
    if (v.kind == Json::Kind::Str) { out = v.str; return true; }
    return false;
}

/// `Field(min_length=1)` — Python counts CODE POINTS, so `é` alone is long
/// enough for a non-empty field.
bool check_min_length(Report& report, const std::string& value, std::size_t limit) {
    if (limit == 0 || to_utf32(value).size() >= limit) return true;
    report.add("string_too_short", "String should have at least " + std::to_string(limit) +
                                       (limit == 1 ? " character" : " characters"));
    return false;
}

bool to_str(Report& report, const Json& v, std::string& out) {
    if (!scalar_str(v, out)) {
        report.add("string_type", "Input should be a valid string");
        return false;
    }
    // pydantic-core materializes every string through UTF-8: the JSON
    // surrogate escape \ud800 by itself parses on the str side and then fails
    // to encode, which surfaces as string_unicode at the FIELD, after the
    // other fields of the same object have been checked. (Measured:
    // {"code":"\ud800"} is the reference's ["code": string_unicode] + the two
    // missing-field rows.)
    if (v.surrogate_pair_broken && !v.str.empty()) {
        report.add("string_unicode",
                   "Input should be a valid string, unable to parse raw data as a unicode string");
        return false;
    }
    if (v.str.empty() && v.kind == Json::Kind::Str && v.surrogate_pair_broken) {
        report.add("string_unicode",
                   "Input should be a valid string, unable to parse raw data as a unicode string");
        return false;
    }
    return true;
}

bool to_int(Report& report, const Json& v, long long& out, std::string& digits) {
    switch (v.kind) {
        case Json::Kind::Int:
            out = v.integer;
            digits = v.big_digits.empty() ? std::to_string(v.integer) : v.big_digits;
            return true;
        case Json::Kind::Bool:
            out = v.boolean ? 1 : 0;
            digits = std::to_string(out);
            return true;
        case Json::Kind::Float:
            if (!std::isfinite(v.real)) {
                report.add("finite_number", "Input should be a finite number");
                return false;
            }
            if (v.real == std::floor(v.real) && std::fabs(v.real) < 9.0e18) {
                out = static_cast<long long>(v.real);
                digits = std::to_string(out);
                return true;
            }
            if (std::fabs(v.real) >= 9.0e18) {
                // pydantic-core: the JSON float parses to an integral Python
                // int that no C int64 can hold, so the FIELD parse fails
                // before any constraint (measured: allophone_passes = 1e+20
                // reported int_parsing_size, not the le=4 bound).
                report.add("int_parsing_size",
                           "Unable to parse input string as an integer, exceeded maximum size");
                return false;
            }
            report.add("int_from_float",
                       "Input should be a valid integer, got a number with a fractional part");
            return false;
        case Json::Kind::Str: {
            std::u32string text;
            if (!parse_int_text(to_utf32(v.str), text)) {
                report.add("int_parsing",
                           "Input should be a valid integer, unable to parse string as an integer");
                return false;
            }
            const std::string ascii = to_utf8(text);
            if (std::from_chars(ascii.data(), ascii.data() + ascii.size(), out).ec != std::errc{}) {
                out = !ascii.empty() && ascii.front() == '-' ? (-9223372036854775807LL - 1)
                                                              : 9223372036854775807LL;
            }
            digits = std::to_string(out);
            return true;
        }
        default:
            report.add("int_type", "Input should be a valid integer");
            return false;
    }
}

bool to_float(Report& report, const Json& v, double& out) {
    switch (v.kind) {
        case Json::Kind::Int:
            if (!v.big_digits.empty() && v.big_digits.size() > 310) {
                report.add("float_type", "Input should be a valid number");
                return false;
            }
            out = static_cast<double>(v.integer);
            return true;
        case Json::Kind::Bool:
            out = v.boolean ? 1.0 : 0.0;
            return true;
        case Json::Kind::Float:
            out = v.real;
            return true;
        case Json::Kind::Str: {
            double value = 0.0;
            if (!parse_float_text(to_utf32(v.str), value)) {
                report.add("float_parsing",
                           "Input should be a valid number, unable to parse string as a number");
                return false;
            }
            out = value;
            return true;
        }
        default:
            report.add("float_type", "Input should be a valid number");
            return false;
    }
}

bool to_bool(Report& report, const Json& v, bool& out) {
    switch (v.kind) {
        case Json::Kind::Bool:
            out = v.boolean;
            return true;
        case Json::Kind::Int:
            if (v.big_digits.empty() && (v.integer == 0 || v.integer == 1)) {
                out = v.integer != 0;
                return true;
            }
            if (!v.big_digits.empty()) {
                report.add("bool_type", "Input should be a valid boolean");
                return false;
            }
            report.add("bool_parsing", "Input should be a valid boolean, unable to interpret input");
            return false;
        case Json::Kind::Float:
            if (std::isfinite(v.real) && v.real == std::floor(v.real)) {
                if (v.real == 0.0 || v.real == 1.0) {
                    out = v.real == 1.0;
                    return true;
                }
                report.add("bool_parsing", "Input should be a valid boolean, unable to interpret input");
                return false;
            }
            report.add("bool_type", "Input should be a valid boolean");
            return false;
        case Json::Kind::Str: {
            bool value = false;
            if (bool_word(v.str, value)) {
                out = value;
                return true;
            }
            report.add("bool_parsing", "Input should be a valid boolean, unable to interpret input");
            return false;
        }
        default:
            report.add("bool_type", "Input should be a valid boolean");
            return false;
    }
}

/// The `Input should be 'a', 'b' or 'c'` list pydantic builds for an enum and
/// for a Literal, in the order the reference declares the members.
std::string expected_values(const std::vector<const char*>& values) {
    std::string out;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i) out += (i + 1 == values.size() ? " or " : ", ");
        out += std::string("'") + values[i] + "'";
    }
    return out;
}

bool to_enum(Report& report, const Json& v, const std::vector<const char*>& allowed, std::string& out) {
    if (v.kind == Json::Kind::Str)
        for (const char* candidate : allowed)
            if (v.str == candidate) { out = v.str; return true; }
    report.add("enum", "Input should be " + expected_values(allowed));
    return false;
}

bool to_literal(Report& report, const Json& v, const std::vector<const char*>& allowed,
                std::string& out) {
    if (v.kind == Json::Kind::Str)
        for (const char* candidate : allowed)
            if (v.str == candidate) { out = v.str; return true; }
    report.add("literal_error", "Input should be " + expected_values(allowed));
    return false;
}


// ═══════════════════════════════════════════════════════════════════════════
// The model validators
// ═══════════════════════════════════════════════════════════════════════════

/// How a field is declared, which decides what a MISSING key and a `null` do:
///  * kRequired  — no default: a missing key is pydantic's `missing`.
///  * kDefaulted — `X = <default>`: a missing key keeps the default, and a
///                 `null` is still a type error, because X is not Optional.
///  * kOptional  — `Optional[X] = None`: missing and `null` both store None.
enum Presence { kRequired = 0, kDefaulted = 1, kOptional = 2 };

/// The enum and Literal member lists, in the order the reference declares them:
/// that is the order pydantic lists them in its `Input should be …` message.
namespace values {

const std::vector<const char*> kAncestorRole = {
    "parent", "parent_dialect", "proto_language", "ancestor", "substrate", "superstrate",
    "adstrate", "lexifier", "creole_base", "related"};
const std::vector<const char*> kQualityTier = {"stub", "skeleton", "research", "production"};
const std::vector<const char*> kScriptType = {
    "alphabet", "abjad", "abugida", "syllabary", "logographic", "featural", "mixed",
    "reconstruction"};
const std::vector<const char*> kOrthographyKind = {"native", "romanization", "transliteration"};
const std::vector<const char*> kAuditConclusion = {
    "input_limited", "mislabeled_gold", "sample_too_small", "at_ceiling_documented",
    "change_refused_uncited", "logographic"};
const std::vector<const char*> kGraphemePosition = {
    "word_initial", "word_final", "intervocalic", "intervocalic_cross_word", "onset",
    "nucleus_stressed", "nucleus_unstressed", "nucleus_secondary", "coda", "first_pretonic",
    "pretonic", "posttonic", "before_vowel", "before_final_vowel", "before_mute_e",
    "after_vowel", "before_consonant", "after_consonant", "before_a", "before_e", "before_i",
    "before_o", "before_u", "before_y", "before_front_vowel", "before_back_vowel",
    "after_front_vowel", "after_back_vowel", "before_palatal", "after_palatal", "after_a",
    "after_e", "after_i", "after_o", "after_u", "consonantal", "vocalic", "default", "nucleus",
    "open_syllable", "closed_syllable", "nucleus_stressed_open", "nucleus_stressed_closed",
    "nucleus_unstressed_open", "nucleus_unstressed_closed"};
const std::vector<const char*> kStressContext = {
    "vowel", "consonant", "consonant_cluster", "coda", "coda_nasal", "front_vowel", "back_vowel",
    "palatal", "emphatic", "word_boundary", "any"};

}  // namespace values

const std::string kIso6393Pattern = "^[a-z]{3}$";
const std::string kGlottologPattern = "^[a-z0-9]{4}\\d{4}$";
const std::string kWikidataPattern = "^Q[1-9]\\d*$";

/// pydantic evaluates `pattern` with the Rust regex crate, whose `$` is the
/// absolute end of the text — a trailing newline does NOT match, unlike
/// Python's `re`. The three patterns this schema declares are literals, ASCII
/// classes and `\d`, so the port matches them itself over CODE POINTS: the same
/// whole-text rule, with no backtracking engine behind it.
namespace pattern {

struct Item {
    enum class Kind { Char, Class, Digit };
    Kind kind = Kind::Char;
    char32_t character = 0;
    std::vector<std::pair<char32_t, char32_t>> ranges;
    std::size_t count = 1;
    bool star = false;
};

bool accepts(const Item& item, char32_t cp) {
    switch (item.kind) {
        case Item::Kind::Char: return cp == item.character;
        case Item::Kind::Digit: return category(cp) == "Nd";
        case Item::Kind::Class:
            for (const auto& range : item.ranges)
                if (cp >= range.first && cp <= range.second) return true;
            return false;
    }
    return false;
}

std::vector<Item> compile(const std::u32string& source) {
    std::vector<Item> items;
    std::size_t i = 0;
    if (i < source.size() && source[i] == U'^') ++i;
    while (i < source.size() && source[i] != U'$') {
        Item item;
        if (source[i] == U'[') {
            item.kind = Item::Kind::Class;
            ++i;
            while (i < source.size() && source[i] != U']') {
                const char32_t low = source[i++];
                if (i + 1 < source.size() && source[i] == U'-' && source[i + 1] != U']') {
                    item.ranges.emplace_back(low, source[i + 1]);
                    i += 2;
                } else {
                    item.ranges.emplace_back(low, low);
                }
            }
            if (i < source.size()) ++i;                                   // the ']'
        } else if (source[i] == U'\\') {
            ++i;
            item.kind = source[i] == U'd' ? Item::Kind::Digit : Item::Kind::Char;
            item.character = source[i];
            ++i;
        } else {
            item.kind = Item::Kind::Char;
            item.character = source[i++];
        }
        if (i < source.size() && source[i] == U'{') {
            ++i;
            std::size_t count = 0;
            while (i < source.size() && source[i] >= U'0' && source[i] <= U'9')
                count = count * 10 + static_cast<std::size_t>(source[i++] - U'0');
            item.count = count;
            if (i < source.size() && source[i] == U'}') ++i;
        } else if (i < source.size() && source[i] == U'*') {
            item.star = true;
            ++i;
        }
        items.push_back(item);
    }
    return items;
}

bool walk(const std::vector<Item>& items, std::size_t index, const std::u32string& text,
          std::size_t position) {
    if (index == items.size()) return position == text.size();
    const Item& item = items[index];
    if (!item.star) {
        std::size_t moved = position;
        for (std::size_t k = 0; k < item.count; ++k) {
            if (moved >= text.size() || !accepts(item, text[moved])) return false;
            ++moved;
        }
        return walk(items, index + 1, text, moved);
    }
    for (std::size_t taken = 0;; ++taken) {
        if (walk(items, index + 1, text, position + taken)) return true;
        if (position + taken >= text.size() || !accepts(item, text[position + taken])) return false;
    }
}

}  // namespace pattern

bool pattern_matches(const std::string& source, const std::string& utf8) {
    const std::vector<pattern::Item> items = pattern::compile(to_utf32(source));
    return pattern::walk(items, 0, to_utf32(utf8), 0);
}

bool check_pattern(Report& report, const std::string& source, const std::string& value) {
    if (pattern_matches(source, value)) return true;
    report.add("string_pattern_mismatch", "String should match pattern '" + source + "'");
    return false;
}

/// Python's repr() of a list of strings, which is how the tone-table validator
/// prints `sorted(names)`.
std::string py_repr_list(const std::vector<std::string>& items) {
    std::string out = "[";
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i) out += ", ";
        out += detail::py_repr(items[i]);
    }
    return out + "]";
}

// ─── value-level checks, reporting at the CURRENT path ─────────────────────

bool value_str(Report& report, const Json& v, std::string& out) {
    return to_str(report, v, out);
}

bool value_string_list(Report& report, const Json& v, std::vector<std::string>& out) {
    if (v.kind != Json::Kind::List) {
        report.add("list_type", "Input should be a valid list");
        return false;
    }
    bool ok = true;
    for (std::size_t i = 0; i < v.list.size(); ++i) {
        Guard item(report, LocPart::indexed(static_cast<long long>(i)));
        std::string value;
        // pydantic validates EVERY list item and keeps one error row per bad
        // element; the loop used to stop at the first failure, so
        // graphemes.a = [1, 2] lost the element-1 row.
        if (!to_str(report, v.list[i], value)) { ok = false; continue; }
        out.push_back(std::move(value));
    }
    return ok;
}

/// One member of a pydantic smart union: its errors vanish when the member
/// matched and stay on the record, under its own tag, when it did not.
template <typename Run>
bool union_member(Report& report, const std::string& tag, Run run) {
    const std::size_t mark = report.messages.size();
    Guard guard(report, tag);
    if (run()) {
        report.truncate(mark);
        return true;
    }
    return false;
}

bool weighted_grapheme_model(Report& report, const Json& json, WeightedGraphemeModel& out);

/// schema.py GraphemeValue = Optional[Union[List[str], WeightedGraphemeModel]]
bool value_grapheme(Report& report, const Json& json, GraphemeValue& out) {
    if (json.is_null()) { out.reset(); return true; }
    const std::size_t base = report.messages.size();
    std::vector<std::string> plain;
    if (union_member(report, "list[str]", [&]() { return value_string_list(report, json, plain); })) {
        out = std::move(plain);
        return true;
    }
    WeightedGraphemeModel weighted;
    if (union_member(report, "function-after[_lengths_match(), WeightedGraphemeModel]",
                     [&]() { return weighted_grapheme_model(report, json, weighted); })) {
        report.truncate(base);           // the member that matched leaves no trace
        out = weighted;
        return true;
    }
    out.reset();
    return false;
}

/// schema.py IPACandidates = Optional[List[str]] — `None` is the inheritance
/// deletion marker and an empty list is the silent-grapheme tombstone, so both
/// are values, not errors.
bool value_ipa_candidates(Report& report, const Json& json, IPACandidates& out) {
    if (json.is_null()) { out.reset(); return true; }
    std::vector<std::string> items;
    if (!value_string_list(report, json, items)) return false;
    out = std::move(items);
    return true;
}

/// schema.py grammatical_endings value: Union[str, List[Optional[str]]]
bool value_ending(Report& report, const Json& json, EndingValue& out) {
    if (json.kind == Json::Kind::Str) { out = json.str; return true; }
    std::size_t base = report.messages.size();
    {
        Guard tag(report, std::string("str"));
        report.add("string_type", "Input should be a valid string");
    }
    std::vector<std::optional<std::string>> items;
    if (union_member(report, "list[nullable[str]]", [&]() {
            if (json.kind != Json::Kind::List) {
                report.add("list_type", "Input should be a valid list");
                return false;
            }
            for (std::size_t i = 0; i < json.list.size(); ++i) {
                Guard item(report, LocPart::indexed(static_cast<long long>(i)));
                const Json& element = json.list[i];
                if (element.is_null()) { items.emplace_back(); continue; }
                if (element.kind != Json::Kind::Str) {
                    report.add("string_type", "Input should be a valid string");
                    return false;
                }
                items.emplace_back(element.str);
            }
            return true;
        })) {
        report.truncate(base);
        out = std::move(items);
        return true;
    }
    return false;
}


// ─── the field binder ──────────────────────────────────────────────────────

/// One model instance under validation. It holds the object the model was given,
/// records which keys were bound (everything else is an `extra_forbidden`) and
/// remembers where the report stood at entry, because a
/// `model_validator(mode="after")` runs only when nothing below it failed.
class Binding {
public:
    Binding(Report& report, const Json& json, const char* model)
        : report_(report), json_(json), model_(model), mark_(report.messages.size()),
          taken_(json.object.size(), false) {}

    /// pydantic accepts a mapping (or an instance) and nothing else.
    bool begin() {
        if (json_.kind == Json::Kind::Object) return true;
        report_.add("model_type",
                    "Input should be a valid dictionary or instance of " + std::string(model_));
        return false;
    }

    /// The keys the model never heard of, in document order, after every field.
    void extras() {
        for (std::size_t i = 0; i < json_.object.size(); ++i) {
            if (taken_[i]) continue;
            Guard guard(report_, json_.object[i].first);
            report_.add("extra_forbidden", "Extra inputs are not permitted");
        }
    }

    bool built() const { return report_.messages.size() == mark_; }
    bool clean_since(std::size_t mark) const { return report_.messages.size() == mark; }
    std::size_t mark() const { return mark_; }
    Report& report() { return report_; }
    bool present(const std::string& name) const { return json_.member(name) != nullptr; }

    /// The shared shape: `missing` when a required key is absent, then the
    /// declared type's check with the field's own path pushed.
    template <typename Check>
    bool field(const std::string& name, Presence presence, Check check) {
        take(name);
        const Json* value = json_.member(name);
        if (!value) {
            if (presence == kRequired) {
                Guard guard(report_, name);
                report_.add("missing", "Field required");
                return false;
            }
            return true;
        }
        if (value->is_null() && presence == kOptional) {
            Guard guard(report_, name);
            return check(*value);
        }
        Guard guard(report_, name);
        return check(*value);
    }

    // ── scalars ─────────────────────────────────────────────────────────────
    bool str(const std::string& name, std::string& out, Presence presence,
             std::size_t min_length = 0, const std::string* pattern_source = nullptr) {
        return field(name, presence, [&](const Json& v) {
            std::string value;
            if (!to_str(report_, v, value)) return false;
            // A pattern is a CONSTRAINT, not the destination: the validated
            // value still lands in `out`. This branch used to return the
            // check's boolean directly, so every pattern-validated optional
            // string stayed unset (measured: iso639_3 / glottolog_code /
            // wikidata_qid all serialized as null).
            if (pattern_source && !check_pattern(report_, *pattern_source, value)) return false;
            if (!check_min_length(report_, value, min_length)) return false;
            out = std::move(value);
            return true;
        });
    }

    bool opt_str(const std::string& name, std::optional<std::string>& out, std::size_t min_length = 0,
                 const std::string* pattern_source = nullptr) {
        return field(name, kOptional, [&](const Json& v) {
            if (v.is_null()) { out.reset(); return true; }
            std::string value;
            if (!to_str(report_, v, value)) return false;
            // Same constraint-not-destination rule as str(): this branch used
            // to return the pattern check's boolean directly and left `out`
            // unset for every pattern-validated optional string (measured:
            // iso639_3 / glottolog_code / wikidata_qid all serialized null).
            if (pattern_source && !check_pattern(report_, *pattern_source, value)) return false;
            if (!check_min_length(report_, value, min_length)) return false;
            out = std::move(value);
            return true;
        });
    }

    /// `int` with the `ge` / `le` bounds pydantic enforces before any validator.
    bool integer(const std::string& name, long long& out, Presence presence, long long ge = 0,
                 bool has_ge = false, long long le = 0, bool has_le = false) {
        return field(name, presence, [&](const Json& v) {
            long long value = 0;
            std::string digits;
            if (!to_int(report_, v, value, digits)) return false;
            digits_ = std::move(digits);
            // pydantic-core reports a JSON integer that overflows the C int64
            // backing field BEFORE any constraint: the parse fails, not the
            // bound. (Measured: allophone_passes = 1e20 came back
            // less_than_equal instead of int_parsing_size.)
            if (v.is_int() && !v.big_digits.empty()) {
                report_.add("int_parsing_size",
                            "Unable to parse input string as an integer, exceeded maximum size");
                return false;
            }
            const int against_ge = has_ge ? compare(value, v, ge) : 0;
            if (has_ge && against_ge < 0) {
                report_.add("greater_than_equal",
                            "Input should be greater than or equal to " + std::to_string(ge));
                return false;
            }
            const int against_le = has_le ? compare(value, v, le) : 0;
            if (has_le && against_le > 0) {
                report_.add("less_than_equal",
                            "Input should be less than or equal to " + std::to_string(le));
                return false;
            }
            out = value;
            return true;
        });
    }

    bool opt_int(const std::string& name, std::optional<long long>& out) {
        return field(name, kOptional, [&](const Json& v) {
            if (v.is_null()) { out.reset(); return true; }
            long long value = 0;
            std::string digits;
            if (!to_int(report_, v, value, digits)) return false;
            digits_ = std::move(digits);
            out = value;
            return true;
        });
    }

    bool number(const std::string& name, double& out, Presence presence, double ge = 0.0,
                bool has_ge = false, double le = 0.0, bool has_le = false) {
        return field(name, presence, [&](const Json& v) {
            double value = 0.0;
            if (!to_float(report_, v, value)) return false;
            // pydantic checks the upper bound first, which is what a NaN
            // exposes: both comparisons fail and `less_than_equal` is the one
            // it reports.
            if (has_le && !(value <= le)) {
                report_.add("less_than_equal", "Input should be less than or equal to " + limit(le));
                return false;
            }
            if (has_ge && !(value >= ge)) {
                report_.add("greater_than_equal", "Input should be greater than or equal to " + limit(ge));
                return false;
            }
            out = value;
            return true;
        });
    }

    bool boolean(const std::string& name, bool& out, Presence presence) {
        return field(name, presence, [&](const Json& v) { return to_bool(report_, v, out); });
    }

    bool opt_boolean(const std::string& name, std::optional<bool>& out) {
        return field(name, kOptional, [&](const Json& v) {
            if (v.is_null()) { out.reset(); return true; }
            bool value = false;
            if (!to_bool(report_, v, value)) return false;
            out = value;
            return true;
        });
    }

    /// An enum field. `use_enum_values=True` means the model stores the VALUE,
    /// so the ported field is a plain string that has to name a member.
    bool enumeration(const std::string& name, std::string& out,
                     const std::vector<const char*>& allowed, Presence presence) {
        return field(name, presence,
                     [&](const Json& v) { return to_enum(report_, v, allowed, out); });
    }

    bool literal(const std::string& name, std::string& out,
                 const std::vector<const char*>& allowed, Presence presence) {
        return field(name, presence,
                     [&](const Json& v) { return to_literal(report_, v, allowed, out); });
    }

    bool opt_literal(const std::string& name, std::optional<std::string>& out,
                     const std::vector<const char*>& allowed) {
        return field(name, kOptional, [&](const Json& v) {
            if (v.is_null()) { out.reset(); return true; }
            std::string value;
            if (!to_literal(report_, v, allowed, value)) return false;
            out = std::move(value);
            return true;
        });
    }

    // ── containers ──────────────────────────────────────────────────────────
    /// `List[str]` in any of the three presences. Empty entries are legal (a
    /// silent grapheme), so only the element TYPE is checked here.
    bool strings(const std::string& name, std::optional<std::vector<std::string>>& out,
                 Presence presence) {
        return field(name, presence, [&](const Json& v) {
            if (v.is_null()) {
                if (presence == kOptional) { out.reset(); return true; }
                report_.add("list_type", "Input should be a valid list");
                return false;
            }
            std::vector<std::string> items;
            if (!value_string_list(report_, v, items)) return false;
            out = std::move(items);
            return true;
        });
    }

    /// `Dict[str, str]` / `Optional[Dict[str, str]]`.
    bool string_map(const std::string& name, Dict<std::string>& out, Presence presence) {
        return map(name, out, presence, [](Report& report, const Json& v, std::string& slot) {
            return value_str(report, v, slot);
        });
    }

    /// `Dict[GraphemePosition, List[str]]`, the inner half of positional
    /// overrides: a bad key reports at `…<key>.[key]`.
    bool position_map(const std::string& name,
                      std::optional<Dict<std::optional<Dict<std::vector<std::string>>>>>& out) {
        return field(name, kOptional, [&](const Json& v) {
            if (v.is_null()) { out.reset(); return true; }
            if (v.kind != Json::Kind::Object) {
                report_.add("dict_type", "Input should be a valid dictionary");
                return false;
            }
            Dict<std::optional<Dict<std::vector<std::string>>>> entries;
            bool ok = true;
            for (const auto& grapheme : v.object) {
                Guard key(report_, grapheme.first);
                std::optional<Dict<std::vector<std::string>>> positions;
                if (grapheme.second.is_null()) {
                    entries.insert(grapheme.first);
                    continue;
                }
                if (grapheme.second.kind != Json::Kind::Object) {
                    report_.add("dict_type", "Input should be a valid dictionary");
                    ok = false;
                    continue;
                }
                Dict<std::vector<std::string>> built;
                for (const auto& entry : grapheme.second.object) {
                    // pydantic validates the mapping under EVERY entry: an
                    // unknown position key reports the enum error at [key] and
                    // STILL validates the value at the key itself (measured:
                    // {"x": {"word_initial": ["a"], "y": 1}} carries the enum
                    // row AND a list_type row for the int value).
                    if (!one_of(values::kGraphemePosition, entry.first)) {
                        {
                            Guard name(report_, entry.first);
                            Guard marker(report_, std::string("[key]"));
                            report_.add("enum", "Input should be " + expected_values(values::kGraphemePosition));
                        }
                        ok = false;
                        std::vector<std::string> items;
                        Guard value_guard(report_, entry.first);
                        if (!value_string_list(report_, entry.second, items)) ok = false;
                        continue;
                    }
                    Guard name(report_, entry.first);
                    std::vector<std::string> items;
                    if (!value_string_list(report_, entry.second, items)) { ok = false; continue; }
                    built.insert(entry.first) = std::move(items);
                }
                positions = std::move(built);
                entries.insert(grapheme.first) = std::move(positions);
            }
            if (ok) out = std::move(entries);
            return ok;
        });
    }

    template <typename Item, typename Check>
    bool map(const std::string& name, Dict<Item>& out, Presence presence, Check check) {
        return field(name, presence, [&](const Json& v) {
            if (v.is_null()) {
                if (presence == kOptional) return true;
                report_.add("dict_type", "Input should be a valid dictionary");
                return false;
            }
            if (v.kind != Json::Kind::Object) {
                report_.add("dict_type", "Input should be a valid dictionary");
                return false;
            }
            Dict<Item> entries;
            bool ok = true;
            for (const auto& entry : v.object) {
                Guard key(report_, entry.first);
                if (!check(report_, entry.second, entries.insert(entry.first))) ok = false;
            }
            if (ok) out = std::move(entries);
            return ok;
        });
    }

    /// `Optional[Dict[str, X]] = None`: a null and an absent key both store None.
    template <typename Item, typename Check>
    bool opt_map(const std::string& name, std::optional<Dict<Item>>& out, Check check) {
        bool seen = false;
        Dict<Item> entries;
        const bool ok = map(name, entries, kOptional, [&](Report& r, const Json& v, Item& slot) {
            seen = true;
            return check(r, v, slot);
        });
        const Json* value = json_.member(name);
        if (ok && value && !value->is_null()) out = std::move(entries);
        else if (ok) out.reset();
        (void)seen;
        return ok;
    }

    template <typename Item, typename Check>
    bool list(const std::string& name, std::optional<std::vector<Item>>& out, Presence presence,
              Check check) {
        return field(name, presence, [&](const Json& v) {
            if (v.is_null()) {
                if (presence == kOptional) { out.reset(); return true; }
                report_.add("list_type", "Input should be a valid list");
                return false;
            }
            if (v.kind != Json::Kind::List) {
                report_.add("list_type", "Input should be a valid list");
                return false;
            }
            std::vector<Item> items;
            bool ok = true;
            for (std::size_t i = 0; i < v.list.size(); ++i) {
                Guard index(report_, LocPart::indexed(static_cast<long long>(i)));
                items.emplace_back();
                if (!check(report_, v.list[i], items.back())) ok = false;
            }
            if (ok) out = std::move(items);
            return ok;
        });
    }

    /// `Optional[<Model>] = None`.
    template <typename Item, typename Check>
    bool submodel(const std::string& name, std::optional<Item>& out, Check check) {
        return field(name, kOptional, [&](const Json& v) {
            if (v.is_null()) { out.reset(); return true; }
            Item built;
            if (!check(report_, v, built)) return false;
            out = std::move(built);
            return true;
        });
    }

    /// The digits behind the last integer the binding read, for a validator
    /// that reports the value the way the reference's f-string does.
    const std::string& digits() const { return digits_; }

private:
    Report& report_;
    const Json& json_;
    const char* model_;
    std::size_t mark_;
    std::vector<bool> taken_;
    std::string digits_;

    void take(const std::string& name) {
        for (std::size_t i = 0; i < json_.object.size(); ++i)
            if (json_.object[i].first == name) taken_[i] = true;
    }

    static bool one_of(const std::vector<const char*>& allowed, const std::string& key) {
        for (const char* candidate : allowed)
            if (key == candidate) return true;
        return false;
    }

    /// `<` / `>` against a bound, keeping an out-of-int64 Python int honest.
    static int compare(long long value, const Json& v, long long bound) {
        if (!v.big_digits.empty()) return v.big_digits.front() == '-' ? -1 : 1;
        return value < bound ? -1 : (value > bound ? 1 : 0);
    }

    static std::string limit(double value) {
        if (value == std::floor(value) && std::fabs(value) < 1e15)
            return std::to_string(static_cast<long long>(value));
        return float_repr(value);
    }
};

// ═══════════════════════════════════════════════════════════════════════════
// schema.py WeightedGraphemeModel
// ═══════════════════════════════════════════════════════════════════════════

bool weighted_grapheme_model(Report& report, const Json& json, WeightedGraphemeModel& out) {
    Binding b(report, json, "WeightedGraphemeModel");
    if (!b.begin()) return false;
    std::optional<std::vector<std::string>> ipa;
    bool ok = b.strings("ipa", ipa, kRequired);
    if (ok && ipa) out.ipa = std::move(*ipa);

    // weights: List[float]  +  @field_validator("weights") _weights_valid
    ok = b.field("weights", kRequired, [&](const Json& v) {
        if (v.kind != Json::Kind::List) {
            report.add("list_type", "Input should be a valid list");
            return false;
        }
        std::vector<double> values;
        for (std::size_t i = 0; i < v.list.size(); ++i) {
            Guard item(report, LocPart::indexed(static_cast<long long>(i)));
            double number = 0.0;
            if (!to_float(report, v.list[i], number)) return false;
            values.push_back(number);
        }
        bool negative = false;
        double total = 0.0;
        for (const double weight : values) {
            if (weight < 0) negative = true;
            total += weight;
        }
        if (negative) {
            report.value_error("candidate weights must be non-negative");
            return false;
        }
        // Python's `if total <= 0: raise` — NaN is unordered, so a weights
        // list of [NaN] PASSES in the reference (measured: the reference
        // accepts {"ipa": ["x"], "weights": [NaN]}). `!(total > 0)` would
        // have rejected it.
        if (total <= 0) {
            report.value_error("candidate weights must sum to a positive value");
            return false;
        }
        out.weights = std::move(values);
        return true;
    }) && ok;

    b.extras();
    // @model_validator(mode="after") _lengths_match
    if (b.built() && out.ipa.size() != out.weights.size())
        report.value_error("weights length (" + std::to_string(out.weights.size()) +
                           ") must equal ipa length (" + std::to_string(out.ipa.size()) + ")");
    return b.built() && ok;
}


// ═══════════════════════════════════════════════════════════════════════════
// The rest of schema.py's models
// ═══════════════════════════════════════════════════════════════════════════

bool source_model(Report& report, const Json& json, SourceModel& out) {
    Binding b(report, json, "SourceModel");
    if (!b.begin()) return false;
    bool ok = b.str("id", out.id, kRequired, 1);
    ok = b.str("author", out.author, kRequired, 1) && ok;
    // year: int  +  @field_validator("year") _plausible_year
    ok = b.field("year", kRequired, [&](const Json& v) {
        long long value = 0;
        std::string digits;
        if (!to_int(report, v, value, digits)) return false;
        const bool plausible = v.big_digits.empty() && 1500 <= value && value <= 2100;
        if (!plausible) {
            report.value_error("implausible publication year: " +
                               (v.big_digits.empty() ? digits : v.big_digits));
            return false;
        }
        out.year = value;
        return true;
    }) && ok;
    ok = b.str("title", out.title, kRequired, 1) && ok;
    ok = b.opt_str("publisher", out.publisher) && ok;
    ok = b.opt_str("url", out.url) && ok;
    // doi: Optional[str]  +  @field_validator("doi") _doi_shape
    ok = b.field("doi", kOptional, [&](const Json& v) {
        if (v.is_null()) { out.doi.reset(); return true; }
        std::string value;
        if (!to_str(report, v, value)) return false;
        if (value.rfind("10.", 0) != 0 || value.find('/') == std::string::npos) {
            report.value_error("doi does not look like a DOI: " + detail::py_repr(value));
            return false;
        }
        out.doi = std::move(value);
        return true;
    }) && ok;
    ok = b.opt_str("wikipedia_url", out.wikipedia_url) && ok;
    ok = b.opt_str("pages", out.pages) && ok;
    ok = b.opt_str("notes", out.notes) && ok;
    b.extras();
    return b.built() && ok;
}

bool ancestor_model(Report& report, const Json& json, AncestorModel& out) {
    Binding b(report, json, "AncestorModel");
    if (!b.begin()) return false;
    bool ok = b.str("code", out.code, kRequired, 1);
    ok = b.enumeration("role", out.role, values::kAncestorRole, kRequired) && ok;
    // weight: float = 0.5  +  @field_validator("weight") _weight_in_range
    ok = b.field("weight", kDefaulted, [&](const Json& v) {
        double value = 0.0;
        if (!to_float(report, v, value)) return false;
        if (!(0.0 <= value && value <= 1.0)) {
            Json number;
            number.kind = Json::Kind::Float;
            number.real = value;
            report.value_error("ancestor weight must be in [0.0, 1.0], got " + number.repr());
            return false;
        }
        out.weight = value;
        return true;
    }) && ok;
    ok = b.str("notes", out.notes, kDefaulted) && ok;
    b.extras();
    return b.built() && ok;
}

bool sandhi_rule_model(Report& report, const Json& json, SandhiRuleModel& out) {
    Binding b(report, json, "SandhiRuleModel");
    if (!b.begin()) return false;
    bool ok = b.str("id", out.id, kRequired, 1);
    ok = b.str("name", out.name, kRequired, 1) && ok;
    ok = b.str("left_context", out.left_context, kRequired) && ok;
    ok = b.str("right_context", out.right_context, kRequired) && ok;
    ok = b.opt_str("transform", out.transform) && ok;
    ok = b.opt_str("right_transform", out.right_transform) && ok;
    ok = b.boolean("obligatory", out.obligatory, kDefaulted) && ok;
    ok = b.str("notes", out.notes, kDefaulted) && ok;
    b.extras();
    // @model_validator(mode="after") _at_least_one_transform
    if (b.built() && !out.transform && !out.right_transform)
        report.value_error("sandhi rule '" + out.id +
                           "' declares neither 'transform' nor 'right_transform', so it can "
                           "never change anything; give it at least one");
    return b.built() && ok;
}

bool allophone_rule_model(Report& report, const Json& json, AllophoneRuleModel& out) {
    Binding b(report, json, "AllophoneRuleModel");
    if (!b.begin()) return false;
    bool ok = b.str("id", out.id, kRequired, 1);
    // phonemes: Union[str, List[str]] — a bare string is legal and stays one.
    ok = b.field("phonemes", kRequired, [&](const Json& v) {
        if (v.kind == Json::Kind::Str) { out.phonemes = v.str; return true; }
        const std::size_t base = report.messages.size();
        {
            Guard tag(report, std::string("str"));
            report.add("string_type", "Input should be a valid string");
        }
        std::vector<std::string> items;
        if (!union_member(report, "list[str]", [&]() { return value_string_list(report, v, items); }))
            return false;
        report.truncate(base);            // the member that matched leaves no trace
        out.phonemes = std::move(items);
        return true;
    }) && ok;
    ok = b.str("surface", out.surface, kDefaulted) && ok;
    ok = b.str("append", out.append, kDefaulted) && ok;
    ok = b.opt_boolean("word_initial", out.word_initial) && ok;
    ok = b.opt_boolean("word_final", out.word_final) && ok;
    ok = b.opt_literal("stress", out.stress, {"stressed", "unstressed", "pretonic", "posttonic"}) && ok;
    ok = b.opt_literal("syllable_position", out.syllable_position, {"onset", "coda", "nucleus"}) && ok;
    ok = b.opt_literal("preceded_by", out.preceded_by, values::kStressContext) && ok;
    ok = b.opt_literal("followed_by", out.followed_by, values::kStressContext) && ok;
    ok = b.opt_literal("preceded_by_2", out.preceded_by_2, values::kStressContext) && ok;
    ok = b.opt_literal("followed_by_2", out.followed_by_2, values::kStressContext) && ok;
    ok = b.opt_literal("preceded_by_3", out.preceded_by_3, values::kStressContext) && ok;
    ok = b.strings("preceded_by_phoneme_2", out.preceded_by_phoneme_2, kOptional) && ok;
    ok = b.strings("followed_by_phoneme_2", out.followed_by_phoneme_2, kOptional) && ok;
    ok = b.strings("preceded_by_surface_phoneme_2", out.preceded_by_surface_phoneme_2, kOptional) && ok;
    ok = b.opt_boolean("requires_other_nucleus", out.requires_other_nucleus) && ok;
    ok = b.opt_boolean("followed_by_nucleus", out.followed_by_nucleus) && ok;
    ok = b.strings("preceded_by_phoneme", out.preceded_by_phoneme, kOptional) && ok;
    ok = b.strings("followed_by_phoneme", out.followed_by_phoneme, kOptional) && ok;
    ok = b.strings("preceded_by_grapheme", out.preceded_by_grapheme, kOptional) && ok;
    ok = b.strings("followed_by_grapheme", out.followed_by_grapheme, kOptional) && ok;
    ok = b.strings("followed_by_grapheme_not", out.followed_by_grapheme_not, kOptional) && ok;
    ok = b.strings("word_contains_grapheme", out.word_contains_grapheme, kOptional) && ok;
    ok = b.strings("word_contains_grapheme_not", out.word_contains_grapheme_not, kOptional) && ok;
    ok = b.strings("grapheme", out.grapheme, kOptional) && ok;
    ok = b.strings("word", out.word, kOptional) && ok;
    ok = b.str("notes", out.notes, kDefaulted) && ok;
    ok = b.opt_str("mutates_neighbor", out.mutates_neighbor) && ok;
    ok = b.opt_literal("mutates_neighbor_side", out.mutates_neighbor_side, {"preceding", "following"}) && ok;
    b.extras();
    // @model_validator(mode="after") _surface_or_append — and, as in pydantic,
    // the first whole-instance validator that raises ends the chain, so
    // _mutation_pair below stays quiet once this one has spoken.
    if (b.built() && !out.surface.empty() && !out.append.empty()) {
        report.value_error("allophone rule '" + out.id + "': surface and append are mutually exclusive");
        return false;
    }
    // @model_validator(mode="after") _mutation_pair
    if (b.built() && out.mutates_neighbor.has_value() != out.mutates_neighbor_side.has_value()) {
        report.value_error("allophone rule '" + out.id +
                           "': mutates_neighbor and mutates_neighbor_side must be set together "
                           "(both or neither)");
        return false;
    }
    return b.built() && ok;
}

bool orthography_standard_model(Report& report, const Json& json, OrthographyStandardModel& out) {
    Binding b(report, json, "OrthographyStandardModel");
    if (!b.begin()) return false;
    bool ok = b.str("name", out.name, kRequired, 1);
    ok = b.opt_str("authority", out.authority) && ok;
    ok = b.opt_int("year", out.year) && ok;
    ok = b.opt_str("url", out.url) && ok;
    ok = b.opt_str("notes", out.notes) && ok;
    b.extras();
    return b.built() && ok;
}

bool valid_ceiling_model(Report& report, const Json& json, ValidCeilingModel& out) {
    Binding b(report, json, "ValidCeilingModel");
    if (!b.begin()) return false;
    bool ok = b.number("per", out.per, kRequired, 0.0, true, 1.0, true);
    ok = b.str("folded", out.folded, kRequired, 1) && ok;
    ok = b.str("citation", out.citation, kRequired, 1) && ok;
    b.extras();
    return b.built() && ok;
}

bool audit_record_model(Report& report, const Json& json, AuditRecordModel& out) {
    Binding b(report, json, "AuditRecordModel");
    if (!b.begin()) return false;
    bool ok = b.enumeration("conclusion", out.conclusion, values::kAuditConclusion, kRequired);
    ok = b.str("measured", out.measured, kRequired, 1) && ok;
    ok = b.str("reference", out.reference, kRequired, 1) && ok;
    b.extras();
    return b.built() && ok;
}

bool location_model(Report& report, const Json& json, LocationModel& out) {
    Binding b(report, json, "LocationModel");
    if (!b.begin()) return false;
    bool ok = b.number("latitude", out.latitude, kRequired, -90.0, true, 90.0, true);
    ok = b.number("longitude", out.longitude, kRequired, -180.0, true, 180.0, true) && ok;
    ok = b.opt_str("source", out.source) && ok;
    ok = b.opt_str("notes", out.notes) && ok;
    b.extras();
    return b.built() && ok;
}

bool timespan_model(Report& report, const Json& json, TimeSpanModel& out) {
    Binding b(report, json, "TimeSpanModel");
    if (!b.begin()) return false;
    bool ok = b.integer("start_year", out.start_year, kRequired);
    const std::string start_digits = b.digits();
    const bool start_known = b.present("start_year") && report.clean_since(b.mark());
    // end_year: Optional[int]  +  @field_validator("end_year") _end_after_start,
    // which reads info.data["start_year"] — absent when that field failed.
    ok = b.field("end_year", kOptional, [&](const Json& v) {
        if (v.is_null()) { out.end_year.reset(); return true; }
        long long value = 0;
        std::string digits;
        if (!to_int(report, v, value, digits)) return false;
        const bool precedes =
            start_known && (v.big_digits.empty() ? value < out.start_year
                                                 : v.big_digits.front() == '-');
        if (precedes) {
            report.value_error("end_year " + digits + " precedes start_year " + start_digits);
            return false;
        }
        out.end_year = value;
        return true;
    }) && ok;
    b.extras();
    return b.built() && ok;
}

/// `Dict[str, str]` as a VALUE check (the tone rules' classes / marks / tones).
bool map_str_str(Report& report, const Json& json, Dict<std::string>& out) {
    if (json.kind != Json::Kind::Object) {
        report.add("dict_type", "Input should be a valid dictionary");
        return false;
    }
    bool ok = true;
    for (const auto& entry : json.object) {
        Guard key(report, entry.first);
        std::string value;
        if (!to_str(report, entry.second, value)) { ok = false; continue; }
        out.insert(entry.first) = std::move(value);
    }
    return ok;
}

bool tone_rules_model(Report& report, const Json& json, ToneRulesModel& out) {
    Binding b(report, json, "ToneRulesModel");
    if (!b.begin()) return false;
    bool ok = b.map("classes", out.classes, kDefaulted,
                    [](Report& r, const Json& v, std::string& slot) { return value_str(r, v, slot); });
    ok = b.map("marks", out.marks, kDefaulted,
               [](Report& r, const Json& v, std::string& slot) { return value_str(r, v, slot); }) && ok;
    const std::size_t before_tones = report.messages.size();
    ok = b.map("tones", out.tones, kDefaulted,
               [](Report& r, const Json& v, std::string& slot) { return value_str(r, v, slot); }) && ok;
    const bool tones_known = report.messages.size() == before_tones;

    // table: Dict[str, Dict[str, Dict[str, str]]]. pydantic's field validator
    // on `table` runs whenever THAT field parsed — even when a sibling failed
    // (measured: `tones: "x"` still produced the table's value_error row).
    bool table_parsed = false;
    ok = b.map("table", out.table, kDefaulted,
               [&](Report& r, const Json& v, Dict<Dict<std::string>>& shapes) {
        if (v.kind != Json::Kind::Object) {
            r.add("dict_type", "Input should be a valid dictionary");
            return false;
        }
        bool nested = true;
        table_parsed = true;
        for (const auto& shape : v.object) {
            Guard name(r, shape.first);
            nested = map_str_str(r, shape.second, shapes.insert(shape.first)) && nested;
        }
        return nested;
    }) && ok;

    std::optional<std::vector<std::string>> dead_codas;
    ok = b.strings("dead_codas", dead_codas, kDefaulted) && ok;
    if (dead_codas) out.dead_codas = std::move(*dead_codas);
    ok = b.str("no_mark", out.no_mark, kDefaulted) && ok;
    ok = b.opt_str("notes", out.notes) && ok;
    b.extras();

    // @field_validator("table") _tones_are_named. `names` is info.data["tones"],
    // so a failed or absent `tones` leaves the declared set empty; the table is
    // walked in document order and the FIRST failure is the message.
    if (table_parsed) {
        std::vector<std::string> names;
        if (tones_known)
            for (const auto& tone : out.tones) names.push_back(tone.first);
        std::sort(names.begin(), names.end());
        std::string problem;
        for (const auto& klass : out.table.entries()) {
            for (const auto& shape : klass.second.entries()) {
                const std::string& key = shape.first;
                if (key != "live" && key != "dead_short" && key != "dead_long" && key != "any") {
                    problem = "table[" + detail::py_repr(klass.first) + "]: " + detail::py_repr(key) +
                              " is not a syllable shape (live, dead_short, dead_long, any)";
                    break;
                }
                for (const auto& mark : shape.second.entries()) {
                    if (std::find(names.begin(), names.end(), mark.second) == names.end()) {
                        problem = "table[" + detail::py_repr(klass.first) + "][" + detail::py_repr(key) +
                                  "][" + detail::py_repr(mark.first) + "]: " + detail::py_repr(mark.second) +
                                  " is not one of the declared tones " + py_repr_list(names);
                        break;
                    }
                }
                if (!problem.empty()) break;
            }
            if (!problem.empty()) break;
        }
        if (!problem.empty()) {
            Guard table(report, std::string("table"));
            report.value_error(problem);
            ok = false;
        }
    }
    return ok && b.built();
}


// ═══════════════════════════════════════════════════════════════════════════
// schema.py StressRulesModel
// ═══════════════════════════════════════════════════════════════════════════

/// @field_validator("final_stress_endings", "penult_stress_endings",
///                  "antepenult_stress_endings", "marked_vowels", "cliticless_words")
bool stress_entries(Report& report, Binding& b, const std::string& name,
                    std::optional<std::vector<std::string>>& out) {
    return b.field(name, kOptional, [&](const Json& v) {
        if (v.is_null()) { out.reset(); return true; }
        std::vector<std::string> items;
        if (!value_string_list(report, v, items)) return false;
        for (const auto& entry : items) {
            if (!entry.empty()) continue;
            report.value_error("stress rule entries must be non-empty strings");
            return false;
        }
        out = std::move(items);
        return true;
    });
}

bool stress_rules_model(Report& report, const Json& json, StressRulesModel& out) {
    Binding b(report, json, "StressRulesModel");
    if (!b.begin()) return false;
    // default_position: int = -2  +  @field_validator("default_position")
    bool ok = b.field("default_position", kDefaulted, [&](const Json& v) {
        long long value = 0;
        std::string digits;
        if (!to_int(report, v, value, digits)) return false;
        const bool outside = v.big_digits.empty() ? (value == 0 || value < -4 || value > 2) : true;
        if (outside)
            report.value_error("default_position: negative values -1..-4 count from the end "
                               "(oxytone..4th-from-last); positive values 1..2 count from the "
                               "start (1=first syllable, 2=second); 0 is not valid; got " +
                               (v.big_digits.empty() ? digits : v.big_digits));
        else
            out.default_position = value;
        return !outside;
    });
    ok = stress_entries(report, b, "final_stress_endings", out.final_stress_endings) && ok;
    ok = stress_entries(report, b, "penult_stress_endings", out.penult_stress_endings) && ok;
    ok = stress_entries(report, b, "antepenult_stress_endings", out.antepenult_stress_endings) && ok;
    ok = stress_entries(report, b, "marked_vowels", out.marked_vowels) && ok;
    ok = b.str("stress_mark", out.stress_mark, kDefaulted) && ok;
    ok = b.str("accent2_mark", out.accent2_mark, kDefaulted) && ok;
    ok = b.strings("accent2_final_letters", out.accent2_final_letters, kOptional) && ok;
    ok = b.strings("diphthongs", out.diphthongs, kOptional) && ok;
    ok = b.strings("vowel_letters", out.vowel_letters, kOptional) && ok;
    ok = b.boolean("quantity_sensitive", out.quantity_sensitive, kDefaulted) && ok;
    ok = b.boolean("superheavy_final_attracts", out.superheavy_final_attracts, kDefaulted) && ok;
    ok = b.integer("max_onset", out.max_onset, kDefaulted, 1, true, 3, true) && ok;
    ok = b.strings("onset_clusters", out.onset_clusters, kOptional) && ok;
    ok = b.boolean("constrain_mark_onsets", out.constrain_mark_onsets, kDefaulted) && ok;
    ok = stress_entries(report, b, "cliticless_words", out.cliticless_words) && ok;
    ok = b.boolean("coda_liquid_capture", out.coda_liquid_capture, kDefaulted) && ok;
    ok = b.literal("secondary_stress", out.secondary_stress, {"", "alternating"}, kDefaulted) && ok;
    ok = b.literal("compound_stress", out.compound_stress, {"", "last"}, kDefaulted) && ok;
    ok = b.boolean("iambic_length", out.iambic_length, kDefaulted) && ok;
    ok = b.literal("source", out.source, {"rules", "plugin"}, kDefaulted) && ok;
    ok = b.opt_str("notes", out.notes) && ok;
    b.extras();
    return ok && b.built();
}

// ═══════════════════════════════════════════════════════════════════════════
// schema.py LanguageSpecModel
// ═══════════════════════════════════════════════════════════════════════════

/// @field_validator("graphemes", "allophones") _non_empty_keys — empty keys
/// mean a grapheme with no spelling at all, which the engine cannot have.
bool grapheme_keys_ok(Report& report, const std::string& field_name, const std::vector<std::string>& keys) {
    for (const auto& key : keys) {
        if (!key.empty()) continue;
        Guard guard(report, field_name);
        report.value_error("grapheme/allophone keys must be non-empty");
        return false;
    }
    return true;
}

/// @field_validator("grammatical_endings") _ending_values, which delegates the
/// value to positional.py normalize_ending_value and prefixes the failing
/// ending's own repr.
bool ending_values_ok(Report& report, const Dict<EndingValue>& endings) {
    for (const auto& entry : endings.entries()) {
        if (entry.first.empty()) {
            Guard guard(report, std::string("grammatical_endings"));
            report.value_error("grammatical_endings keys must be non-empty");
            return false;
        }
        const auto* items = std::get_if<std::vector<std::optional<std::string>>>(&entry.second);
        std::string problem;
        if (items && items->empty())
            problem = "grammatical_endings value must not be an empty list";
        if (items) {
            for (std::size_t i = 1; i < items->size(); ++i) {
                if (items->at(i).has_value()) continue;
                problem = "grammatical_endings alternative must be a string (use \"\" for a mute "
                          "reading), got None";
                break;
            }
        }
        if (problem.empty()) continue;
        Guard guard(report, std::string("grammatical_endings"));
        report.value_error(detail::py_repr(entry.first) + ": " + problem);
        return false;
    }
    return true;
}

bool language_spec_model(Report& report, const Json& json, LanguageSpecModel& out) {
    Binding b(report, json, "LanguageSpecModel");
    if (!b.begin()) return false;
    bool ok = true;

    // ─── required ───────────────────────────────────────────────────────────
    ok = b.str("code", out.code, kRequired, 1) && ok;
    ok = b.str("name", out.name, kRequired, 1) && ok;
    ok = b.str("script", out.script, kRequired, 1) && ok;

    // ─── classification ─────────────────────────────────────────────────────
    ok = b.opt_str("family", out.family, 1) && ok;
    ok = b.boolean("clade", out.clade, kDefaulted) && ok;

    // ─── core mappings ──────────────────────────────────────────────────────
    ok = b.opt_map("graphemes", out.graphemes,
                   [](Report& r, const Json& v, GraphemeValue& slot) { return value_grapheme(r, v, slot); }) && ok;
    if (ok && out.graphemes) {
        std::vector<std::string> keys;
        for (const auto& entry : *out.graphemes) keys.push_back(entry.first);
        ok = grapheme_keys_ok(report, "graphemes", keys) && ok;
    }
    ok = b.opt_map("allophones", out.allophones,
                   [](Report& r, const Json& v, IPACandidates& slot) {
                       return value_ipa_candidates(r, v, slot);
                   }) && ok;
    if (ok && out.allophones) {
        std::vector<std::string> keys;
        for (const auto& entry : *out.allophones) keys.push_back(entry.first);
        ok = grapheme_keys_ok(report, "allophones", keys) && ok;
    }
    ok = b.strings("phonemes", out.phonemes, kOptional) && ok;
    ok = b.enumeration("orthography_kind", out.orthography_kind, values::kOrthographyKind,
                       kDefaulted) && ok;

    // ─── positional overrides ───────────────────────────────────────────────
    ok = b.position_map("positional_graphemes", out.positional_graphemes) && ok;

    // ─── inheritance keys ───────────────────────────────────────────────────
    ok = b.opt_str("graphemes_base", out.graphemes_base) && ok;
    ok = b.opt_str("allophones_base", out.allophones_base) && ok;
    ok = b.opt_str("positional_graphemes_base", out.positional_graphemes_base) && ok;
    ok = b.opt_str("word_exceptions_base", out.word_exceptions_base) && ok;
    ok = b.opt_str("grammatical_endings_base", out.grammatical_endings_base) && ok;

    // ─── ancestry ───────────────────────────────────────────────────────────
    ok = b.opt_str("parent", out.parent) && ok;
    ok = b.list("ancestors", out.ancestors, kOptional,
                [](Report& r, const Json& v, AncestorModel& slot) { return ancestor_model(r, v, slot); }) && ok;

    // ─── metadata ───────────────────────────────────────────────────────────
    ok = b.opt_str("notes", out.notes) && ok;
    ok = b.enumeration("quality", out.quality, values::kQualityTier, kDefaulted) && ok;
    ok = b.enumeration("script_type", out.script_type, values::kScriptType, kDefaulted) && ok;
    ok = b.opt_str("inherent_vowel", out.inherent_vowel) && ok;
    ok = b.strings("optional_marks", out.optional_marks, kOptional) && ok;
    ok = b.strings("fold_diacritics", out.fold_diacritics, kOptional) && ok;
    ok = b.strings("vowel_graphemes", out.vowel_graphemes, kOptional) && ok;
    ok = b.strings("dependent_vowels", out.dependent_vowels, kOptional) && ok;
    ok = b.strings("preposed_vowels", out.preposed_vowels, kOptional) && ok;
    ok = b.strings("trailing_vowel_axis_digraphs", out.trailing_vowel_axis_digraphs, kOptional) && ok;
    ok = b.opt_boolean("coda_no_inherent_vowel", out.coda_no_inherent_vowel) && ok;
    ok = b.opt_str("inherent_vowel_final", out.inherent_vowel_final) && ok;
    ok = b.opt_str("virama_final_vowel", out.virama_final_vowel) && ok;
    ok = b.opt_boolean("collapse_geminates", out.collapse_geminates) && ok;
    ok = b.opt_boolean("doubled_letters_geminate", out.doubled_letters_geminate) && ok;
    ok = b.opt_boolean("constrain_onsets", out.constrain_onsets) && ok;
    ok = b.opt_boolean("word_internal_apostrophe", out.word_internal_apostrophe) && ok;
    ok = b.opt_str("iso639_3", out.iso639_3, 0, &kIso6393Pattern) && ok;
    ok = b.opt_str("glottolog_code", out.glottolog_code, 0, &kGlottologPattern) && ok;
    ok = b.opt_str("wikidata_qid", out.wikidata_qid, 0, &kWikidataPattern) && ok;
    ok = b.opt_str("phoible_id", out.phoible_id, 1) && ok;
    ok = b.opt_str("wals_code", out.wals_code, 1) && ok;

    // ─── extended structures ────────────────────────────────────────────────
    ok = b.list("sandhi_rules", out.sandhi_rules, kOptional,
                [](Report& r, const Json& v, SandhiRuleModel& slot) { return sandhi_rule_model(r, v, slot); }) && ok;
    ok = b.list("allophone_rules", out.allophone_rules, kOptional,
                [](Report& r, const Json& v, AllophoneRuleModel& slot) {
                    return allophone_rule_model(r, v, slot);
                }) && ok;
    ok = b.integer("allophone_passes", out.allophone_passes, kDefaulted, 1, true, 4, true) && ok;
    ok = b.submodel("stress", out.stress,
                    [](Report& r, const Json& v, StressRulesModel& slot) {
                        return stress_rules_model(r, v, slot);
                    }) && ok;
    ok = b.submodel("tone_rules", out.tone_rules,
                    [](Report& r, const Json& v, ToneRulesModel& slot) {
                        return tone_rules_model(r, v, slot);
                    }) && ok;
    ok = b.opt_map("tone_inventory", out.tone_inventory,
                   [](Report& r, const Json& v, std::string& slot) { return value_str(r, v, slot); }) && ok;
    ok = b.opt_boolean("tone_marks_syllable_final", out.tone_marks_syllable_final) && ok;
    ok = b.list("sources", out.sources, kOptional,
                [](Report& r, const Json& v, SourceModel& slot) { return source_model(r, v, slot); }) && ok;
    ok = b.strings("wikipedia", out.wikipedia, kOptional) && ok;
    ok = b.strings("urls", out.urls, kOptional) && ok;
    ok = b.submodel("timespan", out.timespan,
                    [](Report& r, const Json& v, TimeSpanModel& slot) { return timespan_model(r, v, slot); }) && ok;
    ok = b.submodel("orthography_standard", out.orthography_standard,
                    [](Report& r, const Json& v, OrthographyStandardModel& slot) {
                        return orthography_standard_model(r, v, slot);
                    }) && ok;
    ok = b.submodel("location", out.location,
                    [](Report& r, const Json& v, LocationModel& slot) { return location_model(r, v, slot); }) && ok;
    ok = b.opt_map("valid_ceiling", out.valid_ceiling,
                   [](Report& r, const Json& v, ValidCeilingModel& slot) {
                       return valid_ceiling_model(r, v, slot);
                   }) && ok;
    ok = b.opt_map("audit", out.audit,
                   [](Report& r, const Json& v, AuditRecordModel& slot) {
                       return audit_record_model(r, v, slot);
                   }) && ok;

    // ─── whole-word overrides, then suffix morphology ───────────────────────
    ok = b.opt_map("word_exceptions", out.word_exceptions,
                   [](Report& r, const Json& v, std::string& slot) { return value_str(r, v, slot); }) && ok;
    ok = b.opt_map("grammatical_endings", out.grammatical_endings,
                   [](Report& r, const Json& v, EndingValue& slot) { return value_ending(r, v, slot); }) && ok;
    if (ok && out.grammatical_endings) ok = ending_values_ok(report, *out.grammatical_endings) && ok;

    b.extras();
    return ok && b.built();
}

// ═══════════════════════════════════════════════════════════════════════════
// schema.py DATA_DIR
// ═══════════════════════════════════════════════════════════════════════════

std::string& data_dir_slot() {
    static std::string dir = [] {
        if (const char* override_dir = std::getenv("ORTHOGRAPHY2IPA_DATA_DIR")) return std::string(override_dir);
#ifdef O2I_DEFAULT_DATA_DIR
        if (fs::is_directory(O2I_DEFAULT_DATA_DIR)) return std::string(O2I_DEFAULT_DATA_DIR);
#endif
#ifdef O2I_INSTALL_DATA_DIR
        return std::string(O2I_INSTALL_DATA_DIR);
#else
        return std::string();
#endif
    }();
    return dir;
}


// ═══════════════════════════════════════════════════════════════════════════
// model_dump_json() — the validated model written back out
// ═══════════════════════════════════════════════════════════════════════════
//
// pydantic's serializer emits every declared field, in declaration order, with
// the defaults and the `null` markers filled in; that is the shape the parity
// harness diffs against the reference. Non-finite floats become `null`, as
// pydantic's own JSON writer does, and a `Dict` keeps the document's key order.


class Writer {
public:
    void raw(const std::string& text) { out_ += text; }

    static std::string json_string(const std::string& utf8) {
        std::string out = "\"";
        for (const char c : utf8) {
            switch (c) {
                case '"': out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default:
                    if (static_cast<unsigned char>(c) < 0x20) {
                        static const char* const hex = "0123456789abcdef";
                        out += "\\u00";
                        out += hex[(static_cast<unsigned char>(c) >> 4) & 0xF];
                        out += hex[static_cast<unsigned char>(c) & 0xF];
                    } else {
                        out += c;
                    }
            }
        }
        return out + "\"";
    }

    std::string take() { return std::move(out_); }

private:
    std::string out_;
};

void dump(Writer& w, bool value) { w.raw(value ? "true" : "false"); }
void dump(Writer& w, const std::string& value) { w.raw(Writer::json_string(value)); }
void dump(Writer& w, long long value) { w.raw(std::to_string(value)); }

void dump(Writer& w, double value) {
    if (!std::isfinite(value)) { w.raw("null"); return; }
    Json number;
    number.kind = Json::Kind::Float;
    number.real = value;
    std::string text = number.repr();
    if (text.find_first_of(".e") == std::string::npos) text += ".0";
    w.raw(text);
}

template <typename T>
void dump(Writer& w, const std::optional<T>& value) {
    if (!value) { w.raw("null"); return; }
    dump(w, *value);
}

template <typename T>
void dump(Writer& w, const std::vector<T>& items) {
    w.raw("[");
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i) w.raw(",");
        dump(w, items[i]);
    }
    w.raw("]");
}

template <typename T>
void dump(Writer& w, const Dict<T>& entries) {
    w.raw("{");
    bool first = true;
    for (const auto& entry : entries.entries()) {
        if (!first) w.raw(",");
        first = false;
        w.raw(Writer::json_string(entry.first) + ":");
        dump(w, entry.second);
    }
    w.raw("}");
}

template <typename... Ts>
void dump(Writer& w, const std::variant<Ts...>& value) {
    std::visit([&w](const auto& held) { dump(w, held); }, value);
}

/// One model: each field written in the reference's declaration order.
template <typename Body>
void model_object(Writer& w, Body&& body) {
    w.raw("{");
    bool first = true;
    body(w, first);
    w.raw("}");
}

template <typename T>
void member(Writer& w, bool& first, const std::string& name, const T& value) {
    if (!first) w.raw(",");
    first = false;
    w.raw(Writer::json_string(name) + ":");
    dump(w, value);
}

void dump(Writer& w, const WeightedGraphemeModel& m) {
    model_object(w, [&](Writer& out, bool& first) {
        member(out, first, "ipa", m.ipa);
        member(out, first, "weights", m.weights);
    });
}

void dump(Writer& w, const SourceModel& m) {
    model_object(w, [&](Writer& out, bool& first) {
        member(out, first, "id", m.id);
        member(out, first, "author", m.author);
        member(out, first, "year", m.year);
        member(out, first, "title", m.title);
        member(out, first, "publisher", m.publisher);
        member(out, first, "url", m.url);
        member(out, first, "doi", m.doi);
        member(out, first, "wikipedia_url", m.wikipedia_url);
        member(out, first, "pages", m.pages);
        member(out, first, "notes", m.notes);
    });
}

void dump(Writer& w, const AncestorModel& m) {
    model_object(w, [&](Writer& out, bool& first) {
        member(out, first, "code", m.code);
        member(out, first, "role", m.role);
        member(out, first, "weight", m.weight);
        member(out, first, "notes", m.notes);
    });
}

void dump(Writer& w, const SandhiRuleModel& m) {
    model_object(w, [&](Writer& out, bool& first) {
        member(out, first, "id", m.id);
        member(out, first, "name", m.name);
        member(out, first, "left_context", m.left_context);
        member(out, first, "right_context", m.right_context);
        member(out, first, "transform", m.transform);
        member(out, first, "right_transform", m.right_transform);
        member(out, first, "obligatory", m.obligatory);
        member(out, first, "notes", m.notes);
    });
}

void dump(Writer& w, const AllophoneRuleModel& m) {
    model_object(w, [&](Writer& out, bool& first) {
        member(out, first, "id", m.id);
        member(out, first, "phonemes", m.phonemes);
        member(out, first, "surface", m.surface);
        member(out, first, "append", m.append);
        member(out, first, "word_initial", m.word_initial);
        member(out, first, "word_final", m.word_final);
        member(out, first, "stress", m.stress);
        member(out, first, "syllable_position", m.syllable_position);
        member(out, first, "preceded_by", m.preceded_by);
        member(out, first, "followed_by", m.followed_by);
        member(out, first, "preceded_by_2", m.preceded_by_2);
        member(out, first, "followed_by_2", m.followed_by_2);
        member(out, first, "preceded_by_3", m.preceded_by_3);
        member(out, first, "preceded_by_phoneme_2", m.preceded_by_phoneme_2);
        member(out, first, "followed_by_phoneme_2", m.followed_by_phoneme_2);
        member(out, first, "preceded_by_surface_phoneme_2", m.preceded_by_surface_phoneme_2);
        member(out, first, "requires_other_nucleus", m.requires_other_nucleus);
        member(out, first, "followed_by_nucleus", m.followed_by_nucleus);
        member(out, first, "preceded_by_phoneme", m.preceded_by_phoneme);
        member(out, first, "followed_by_phoneme", m.followed_by_phoneme);
        member(out, first, "preceded_by_grapheme", m.preceded_by_grapheme);
        member(out, first, "followed_by_grapheme", m.followed_by_grapheme);
        member(out, first, "followed_by_grapheme_not", m.followed_by_grapheme_not);
        member(out, first, "word_contains_grapheme", m.word_contains_grapheme);
        member(out, first, "word_contains_grapheme_not", m.word_contains_grapheme_not);
        member(out, first, "grapheme", m.grapheme);
        member(out, first, "word", m.word);
        member(out, first, "notes", m.notes);
        member(out, first, "mutates_neighbor", m.mutates_neighbor);
        member(out, first, "mutates_neighbor_side", m.mutates_neighbor_side);
    });
}

void dump(Writer& w, const OrthographyStandardModel& m) {
    model_object(w, [&](Writer& out, bool& first) {
        member(out, first, "name", m.name);
        member(out, first, "authority", m.authority);
        member(out, first, "year", m.year);
        member(out, first, "url", m.url);
        member(out, first, "notes", m.notes);
    });
}

void dump(Writer& w, const ValidCeilingModel& m) {
    model_object(w, [&](Writer& out, bool& first) {
        member(out, first, "per", m.per);
        member(out, first, "folded", m.folded);
        member(out, first, "citation", m.citation);
    });
}

void dump(Writer& w, const AuditRecordModel& m) {
    model_object(w, [&](Writer& out, bool& first) {
        member(out, first, "conclusion", m.conclusion);
        member(out, first, "measured", m.measured);
        member(out, first, "reference", m.reference);
    });
}

void dump(Writer& w, const LocationModel& m) {
    model_object(w, [&](Writer& out, bool& first) {
        member(out, first, "latitude", m.latitude);
        member(out, first, "longitude", m.longitude);
        member(out, first, "source", m.source);
        member(out, first, "notes", m.notes);
    });
}

void dump(Writer& w, const TimeSpanModel& m) {
    model_object(w, [&](Writer& out, bool& first) {
        member(out, first, "start_year", m.start_year);
        member(out, first, "end_year", m.end_year);
    });
}

void dump(Writer& w, const ToneRulesModel& m) {
    model_object(w, [&](Writer& out, bool& first) {
        member(out, first, "classes", m.classes);
        member(out, first, "marks", m.marks);
        member(out, first, "tones", m.tones);
        member(out, first, "table", m.table);
        member(out, first, "dead_codas", m.dead_codas);
        member(out, first, "no_mark", m.no_mark);
        member(out, first, "notes", m.notes);
    });
}

void dump(Writer& w, const StressRulesModel& m) {
    model_object(w, [&](Writer& out, bool& first) {
        member(out, first, "default_position", m.default_position);
        member(out, first, "final_stress_endings", m.final_stress_endings);
        member(out, first, "penult_stress_endings", m.penult_stress_endings);
        member(out, first, "antepenult_stress_endings", m.antepenult_stress_endings);
        member(out, first, "marked_vowels", m.marked_vowels);
        member(out, first, "stress_mark", m.stress_mark);
        member(out, first, "accent2_mark", m.accent2_mark);
        member(out, first, "accent2_final_letters", m.accent2_final_letters);
        member(out, first, "diphthongs", m.diphthongs);
        member(out, first, "vowel_letters", m.vowel_letters);
        member(out, first, "quantity_sensitive", m.quantity_sensitive);
        member(out, first, "superheavy_final_attracts", m.superheavy_final_attracts);
        member(out, first, "max_onset", m.max_onset);
        member(out, first, "onset_clusters", m.onset_clusters);
        member(out, first, "constrain_mark_onsets", m.constrain_mark_onsets);
        member(out, first, "cliticless_words", m.cliticless_words);
        member(out, first, "coda_liquid_capture", m.coda_liquid_capture);
        member(out, first, "secondary_stress", m.secondary_stress);
        member(out, first, "compound_stress", m.compound_stress);
        member(out, first, "iambic_length", m.iambic_length);
        member(out, first, "source", m.source);
        member(out, first, "notes", m.notes);
    });
}
}  // namespace

// ═══════════════════════════════════════════════════════════════════════════
// The exception types
// ═══════════════════════════════════════════════════════════════════════════

/// pydantic renders `str(ValidationError)` with the offending input value
/// re-printed and a documentation URL per line. format_failure() never uses
/// that text — it walks errors() — so what() here is the same shape (count,
/// title, one indented line per message) without the input repr.
ValidationError::ValidationError(std::vector<ValidationMessage> messages)
    : std::runtime_error(render(messages)), messages_(std::move(messages)) {}

std::string ValidationError::render(const std::vector<ValidationMessage>& taken) {
    std::string out = std::to_string(taken.size()) +
                      (taken.size() == 1 ? " validation error for LanguageSpecModel\n"
                                         : " validation errors for LanguageSpecModel\n");
    for (const auto& message : taken) {
        std::string loc;
        for (const auto& part : message.loc) {
            if (!loc.empty()) loc += ".";
            loc += part.text();
        }
        out += (loc.empty() ? std::string("<root>") : loc) + "\n  " + message.msg + " [type=" +
               message.type + "]\n";
    }
    return out;
}

JsonDecodeError::JsonDecodeError(std::string message, std::size_t pos, std::size_t line,
                                 std::size_t column)
    : std::runtime_error(message), pos_(pos), line_(line), column_(column) {}


// ═══════════════════════════════════════════════════════════════════════════
// schema.py's public API
// ═══════════════════════════════════════════════════════════════════════════

const std::string& data_directory() { return data_dir_slot(); }

void set_data_directory(const std::string& path) { data_dir_slot() = path; }

LanguageSpecModel validate_spec_json(const std::string& text) {
    Report report;
    LanguageSpecModel model;
    const Json json = Reader(decode_utf8(text)).document();
    if (!language_spec_model(report, json, model))
        throw ValidationError(std::move(report.messages));
    return model;
}

LanguageSpecModel validate_spec_file(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        const std::error_code code = std::make_error_code(std::errc::no_such_file_or_directory);
        throw SpecIoError("[Errno " + std::to_string(code.value()) + "] " + code.message() + ": " +
                          detail::py_repr(path));
    }
    std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (input.bad()) throw SpecIoError("read of " + detail::py_repr(path) + " failed");
    if (!text.empty() && text.front() == '\xEF' && (text.size() < 2 || text[1] != '\xBB')) {
        // A lone 0xEF is just a bad byte and decode_utf8 says so.
    }
    return validate_spec_json(text);
}

std::vector<std::string> iter_spec_files() {
    std::vector<std::string> files;
    const std::string dir = data_directory();
    if (dir.empty() || !fs::is_directory(dir)) return files;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.path().filename().string().front() == '.') continue;    // fnmatch skips dotfiles
        if (entry.path().extension() != ".json") continue;
        files.push_back(entry.path().string());
    }
    std::sort(files.begin(), files.end());
    return files;
}

ValidationResults validate_all(const std::string& code) {
    ValidationResults results;
    std::vector<std::string> files{fs::path(data_directory()) / (code + ".json")};
    for (const auto& path : files) {
        const std::string name = fs::path(path).filename().string();
        const std::string spec_code = name.size() > 5 ? name.substr(0, name.size() - 5) : name;
        try {
            validate_spec_file(path);
            results.ok.push_back(spec_code);
        } catch (const ValidationError& error) {
            results.failures.emplace_back(spec_code, std::make_exception_ptr(error));
        } catch (const JsonDecodeError& error) {
            results.failures.emplace_back(spec_code, std::make_exception_ptr(error));
        } catch (const SpecIoError& error) {
            results.failures.emplace_back(spec_code, std::make_exception_ptr(error));
        }
    }
    return results;
}

ValidationResults validate_all() {
    ValidationResults results;
    for (const auto& path : iter_spec_files()) {
        const std::string name = fs::path(path).filename().string();
        const std::string spec_code = name.size() > 5 ? name.substr(0, name.size() - 5) : name;
        try {
            validate_spec_file(path);
            results.ok.push_back(spec_code);
        } catch (const ValidationError& error) {
            results.failures.emplace_back(spec_code, std::make_exception_ptr(error));
        } catch (const JsonDecodeError& error) {
            results.failures.emplace_back(spec_code, std::make_exception_ptr(error));
        } catch (const SpecIoError& error) {
            results.failures.emplace_back(spec_code, std::make_exception_ptr(error));
        }
    }
    return results;
}

std::string format_failure(const std::string& code, const std::exception& error) {
    std::string lines;
    if (const auto* invalid = dynamic_cast<const ValidationError*>(&error)) {
        for (const auto& message : invalid->messages()) {
            std::string loc;
            for (const auto& part : message.loc) {
                if (!loc.empty()) loc += ".";
                loc += part.text();
            }
            if (loc.empty()) loc = "<root>";
            if (!lines.empty()) lines += "\n";
            lines += code + ".json: " + loc + " — " + message.msg + " [" + message.type + "]";
        }
        return lines;
    }
    // json.JSONDecodeError is a VALUE error whose str() already says what and
    // where; the reference's format_failure prints it bare (str(exc)), without
    // the `<code>.json` prefix.
    if (dynamic_cast<const schema::JsonDecodeError*>(&error))
        return std::string("JSONDecodeError: ") + error.what();
    return code + ".json: " + error.what();
}

std::string format_failure(const std::string& code, const std::exception_ptr& error) {
    try {
        std::rethrow_exception(error);
    } catch (const std::exception& raised) {
        return format_failure(code, raised);
    }
}


std::string dump_json(const LanguageSpecModel& model) {
    Writer w;
    bool first = true;
    w.raw("{");
    auto m = [&](const char* name, const auto& value) { member(w, first, name, value); };

    m("code", model.code);
    m("name", model.name);
    m("script", model.script);
    m("family", model.family);
    m("clade", model.clade);
    m("graphemes", model.graphemes);
    m("allophones", model.allophones);
    m("phonemes", model.phonemes);
    m("orthography_kind", model.orthography_kind);
    m("positional_graphemes", model.positional_graphemes);
    m("graphemes_base", model.graphemes_base);
    m("allophones_base", model.allophones_base);
    m("positional_graphemes_base", model.positional_graphemes_base);
    m("word_exceptions_base", model.word_exceptions_base);
    m("grammatical_endings_base", model.grammatical_endings_base);
    m("parent", model.parent);
    m("ancestors", model.ancestors);
    m("notes", model.notes);
    m("quality", model.quality);
    m("script_type", model.script_type);
    m("inherent_vowel", model.inherent_vowel);
    m("optional_marks", model.optional_marks);
    m("fold_diacritics", model.fold_diacritics);
    m("vowel_graphemes", model.vowel_graphemes);
    m("dependent_vowels", model.dependent_vowels);
    m("preposed_vowels", model.preposed_vowels);
    m("trailing_vowel_axis_digraphs", model.trailing_vowel_axis_digraphs);
    m("coda_no_inherent_vowel", model.coda_no_inherent_vowel);
    m("inherent_vowel_final", model.inherent_vowel_final);
    m("virama_final_vowel", model.virama_final_vowel);
    m("collapse_geminates", model.collapse_geminates);
    m("doubled_letters_geminate", model.doubled_letters_geminate);
    m("constrain_onsets", model.constrain_onsets);
    m("word_internal_apostrophe", model.word_internal_apostrophe);
    m("iso639_3", model.iso639_3);
    m("glottolog_code", model.glottolog_code);
    m("wikidata_qid", model.wikidata_qid);
    m("phoible_id", model.phoible_id);
    m("wals_code", model.wals_code);
    m("sandhi_rules", model.sandhi_rules);
    m("allophone_rules", model.allophone_rules);
    m("allophone_passes", model.allophone_passes);
    m("stress", model.stress);
    m("tone_rules", model.tone_rules);
    m("tone_inventory", model.tone_inventory);
    m("tone_marks_syllable_final", model.tone_marks_syllable_final);
    m("sources", model.sources);
    m("wikipedia", model.wikipedia);
    m("urls", model.urls);
    m("timespan", model.timespan);
    m("orthography_standard", model.orthography_standard);
    m("location", model.location);
    m("valid_ceiling", model.valid_ceiling);
    m("audit", model.audit);
    m("word_exceptions", model.word_exceptions);
    m("grammatical_endings", model.grammatical_endings);
    w.raw("}");
    return w.take();
}
/// json_loader._index: every spec file on disk, clades included, as a sorted
/// list of codes — the index json_loader.py builds once at import.
std::vector<std::string> indexed_codes() {
    std::vector<std::string> codes;
    for (const auto& path : iter_spec_files()) {
        const std::string name = fs::path(path).filename().string();
        const auto cut = name.find(".json");
        codes.push_back(cut == std::string::npos ? name : name.substr(0, cut));
    }
    std::sort(codes.begin(), codes.end());
    return codes;
}

}  // namespace schema

// ═══════════════════════════════════════════════════════════════════════════
// json_loader.py, the public surface
// ═══════════════════════════════════════════════════════════════════════════

namespace json_loader {

std::vector<std::string> available_json_codes() {
    return schema::indexed_codes();
}

const LanguageSpec& load_json_spec(const std::string& code) {
    const std::vector<std::string> index = schema::indexed_codes();
    if (std::find(index.begin(), index.end(), code) == index.end()) {
        std::string available = "{";
        for (std::size_t i = 0; i < index.size(); ++i) {
            if (i) available += ", ";
            available += detail::py_repr(index[i]);
        }
        throw schema::LanguageKeyError("unsupported language: '" + code +
                                       ".json' not found. Available: " + available + "}");
    }
    return orthography2ipa::get(code);
}

std::vector<std::pair<std::string, const LanguageSpec*>> load_all_json_specs(
    std::vector<std::pair<std::string, std::string>>* failed) {
    std::vector<std::pair<std::string, const LanguageSpec*>> loaded;
    for (const auto& code : schema::indexed_codes()) {
        try {
            loaded.emplace_back(code, &orthography2ipa::get(code));
        } catch (const std::exception& error) {
            // json_loader only warns here and keeps going, so the port does too.
            if (failed) failed->emplace_back(code, error.what());
        }
    }
    return loaded;
}

}  // namespace json_loader

}  // namespace orthography2ipa
