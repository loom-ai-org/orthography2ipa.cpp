// Direct port of orthography2ipa/exceptions.py.
#include "orthography2ipa/exceptions.hpp"

#include "unicode_util.hpp"

#include <string>

namespace orthography2ipa {
namespace {

/// Python's `str.isprintable()` for one code point: everything except the
/// "Other" and "Separator" categories, space itself included.
bool is_printable(char32_t cp) {
    if (cp == U' ') return true;
    const std::string cat = uni::category(cp);
    if (cat.empty()) return false;
    switch (cat[0]) {
        case 'C': case 'Z': return false;
        default: return true;
    }
}

/// CPython's non-printable escape form for one code point.
void put_escape(std::string& out, char32_t cp) {
    static const char* const hex = "0123456789abcdef";
    if (cp < 0x100) {
        out += "\\x";
        out += hex[(cp >> 4) & 0xF]; out += hex[cp & 0xF];
    } else if (cp < 0x10000) {
        out += "\\u";
        for (int shift = 12; shift >= 0; shift -= 4) out += hex[(cp >> shift) & 0xF];
    } else {
        out += "\\U";
        for (int shift = 28; shift >= 0; shift -= 4) out += hex[(cp >> shift) & 0xF];
    }
}

/// exceptions.py builds its message with `{word!r}` / `{''.join(unmapped)!r}`:
/// the text is quoted the way CPython quotes it.
std::string message(const std::string& word, const std::vector<std::string>& unmapped,
                    const std::string& lang) {
    std::string joined;
    for (const auto& ch : unmapped) joined += ch;
    return lang + ": word " + detail::py_repr(word) + " has unmapped characters " +
           detail::py_repr(joined) + " not covered by the grapheme table";
}

} // namespace

namespace detail {

std::string py_repr(const std::string& utf8) {
    // CPython's repr() of a str: single quotes unless the text contains a
    // single quote and no double quote; the active quote, backslash, \n, \r,
    // \t and every non-printable code point are escaped.
    const std::u32string text = uni::to_utf32(utf8);
    const bool has_single = text.find(U'\'') != std::u32string::npos;
    const bool has_double = text.find(U'"') != std::u32string::npos;
    const char32_t quote = (has_single && !has_double) ? U'"' : U'\'';

    std::string out;
    out += static_cast<char>(quote);
    for (const char32_t cp : text) {
        if (cp == U'\\') { out += "\\\\"; continue; }
        if (cp == quote) { out += '\\'; out += uni::to_utf8(std::u32string(1, cp)); continue; }
        if (cp == U'\n') { out += "\\n"; continue; }
        if (cp == U'\r') { out += "\\r"; continue; }
        if (cp == U'\t') { out += "\\t"; continue; }
        if (cp < 0x80) { out += static_cast<char>(cp); continue; }
        if (is_printable(cp)) { out += uni::to_utf8(std::u32string(1, cp)); continue; }
        put_escape(out, cp);
    }
    out += static_cast<char>(quote);
    return out;
}

} // namespace detail

UnmappedScriptError::UnmappedScriptError(const std::string& word,
                                         const std::vector<std::string>& unmapped,
                                         const std::string& lang)
    : std::runtime_error(message(word, unmapped, lang)),
      word_(word), unmapped_(unmapped), lang_(lang) {}

} // namespace orthography2ipa
