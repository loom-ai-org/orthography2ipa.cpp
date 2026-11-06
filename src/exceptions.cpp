// Direct port of orthography2ipa/exceptions.py.
#include "orthography2ipa/exceptions.hpp"

#include "unicode_util.hpp"

#include <string>

namespace orthography2ipa {
namespace {

// The str a Python caller passes as these bytes. A Python name can hold a lone
// surrogate, which encodes to ED A0..BF xx ("surrogatepass"), and a raw byte
// read from outside, which is U+DC80..DCFF ("surrogateescape"). Every other
// byte that is not valid UTF-8 decodes as a surrogateescape byte, one by one.
std::u32string python_str(const std::string& text) {
    std::u32string out;
    const auto byte = [&text](std::size_t i) { return static_cast<unsigned char>(text[i]); };
    const auto in = [&](std::size_t i, unsigned low, unsigned high) {
        return i < text.size() && byte(i) >= low && byte(i) <= high;
    };
    for (std::size_t i = 0; i < text.size();) {
        const unsigned lead = byte(i);
        std::size_t length = 0;
        if (lead < 0x80) length = 1;
        else if (lead >= 0xC2 && lead <= 0xDF) length = in(i + 1, 0x80, 0xBF) ? 2 : 0;
        else if (lead >= 0xE0 && lead <= 0xEF)
            length = in(i + 1, lead == 0xE0 ? 0xA0 : 0x80, 0xBF) && in(i + 2, 0x80, 0xBF) ? 3 : 0;
        else if (lead >= 0xF0 && lead <= 0xF4)
            length = in(i + 1, lead == 0xF0 ? 0x90 : 0x80, lead == 0xF4 ? 0x8F : 0xBF) &&
                     in(i + 2, 0x80, 0xBF) && in(i + 3, 0x80, 0xBF) ? 4 : 0;
        if (length == 0) {
            out.push_back(0xDC00 + lead);
            ++i;
            continue;
        }
        char32_t cp = length == 1 ? lead : lead & (0x7F >> length);
        for (std::size_t k = 1; k < length; ++k) cp = (cp << 6) | (byte(i + k) & 0x3F);
        out.push_back(cp);
        i += length;
    }
    return out;
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

// Python's repr() of a str (unicodeobject.c unicode_repr) — what the
// reference interpolates with `!r` into its messages. The quote is ' unless
// the text holds ' and no ", and a code point str.isprintable() rejects is
// written as a \x, \u or \U escape.
std::string py_repr(const std::string& text) {
    const std::u32string units = python_str(text);
    const bool double_quote = units.find(U'\'') != std::u32string::npos && units.find(U'"') == std::u32string::npos;
    const char32_t quote = double_quote ? U'"' : U'\'';
    const auto hex = [](char32_t cp, int digits) {
        static const char* const digit = "0123456789abcdef";
        std::string out;
        for (int shift = 4 * (digits - 1); shift >= 0; shift -= 4) out += digit[(cp >> shift) & 0xF];
        return out;
    };
    std::string out(1, static_cast<char>(quote));
    for (const char32_t cp : units) {
        if (cp == quote || cp == U'\\') out += '\\' + uni::to_utf8(std::u32string(1, cp));
        else if (cp == U'\t') out += "\\t";
        else if (cp == U'\n') out += "\\n";
        else if (cp == U'\r') out += "\\r";
        else if (cp < 0x20 || cp == 0x7F) out += "\\x" + hex(cp, 2);
        else if (cp < 0x7F) out += static_cast<char>(cp);
        else {
            // Py_UNICODE_ISPRINTABLE: false for the Other and Separator
            // categories, except the ASCII space handled above.
            const std::string category = uni::category(cp);
            const bool printable = category[0] != 'C' && category[0] != 'Z';
            if (printable) out += uni::to_utf8(std::u32string(1, cp));
            else if (cp <= 0xFF) out += "\\x" + hex(cp, 2);
            else if (cp <= 0xFFFF) out += "\\u" + hex(cp, 4);
            else out += "\\U" + hex(cp, 8);
        }
    }
    return out + static_cast<char>(quote);
}

} // namespace detail

UnmappedScriptError::UnmappedScriptError(const std::string& word,
                                         const std::vector<std::string>& unmapped,
                                         const std::string& lang)
    : std::runtime_error(message(word, unmapped, lang)),
      word_(word), unmapped_(unmapped), lang_(lang) {}

} // namespace orthography2ipa
