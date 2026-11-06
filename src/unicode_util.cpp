#include "unicode_util.hpp"

#include <unicode/normalizer2.h>
#include <unicode/unistr.h>
#include <unicode/uchar.h>

#include <mutex>
#include <stdexcept>

namespace orthography2ipa::uni {

std::u32string to_utf32(std::string_view utf8) {
    const icu::UnicodeString value = icu::UnicodeString::fromUTF8(utf8);
    std::u32string out;
    out.reserve(static_cast<std::size_t>(value.length()));
    int32_t i = 0;
    while (i < value.length()) {
        UChar32 cp = 0;
        U16_NEXT(value.getBuffer(), i, value.length(), cp);
        if (cp < 0) throw std::runtime_error("invalid UTF-16 in Unicode conversion");
        out.push_back(static_cast<char32_t>(cp));
    }
    return out;
}

std::string to_utf8(std::u32string_view utf32) {
    std::string out;
    icu::UnicodeString value;
    for (char32_t cp : utf32) value.append(static_cast<UChar32>(cp));
    value.toUTF8String(out);
    return out;
}

namespace {
const icu::Normalizer2* normalizer(Form form) {
    UErrorCode status = U_ZERO_ERROR;
    const icu::Normalizer2* instance = nullptr;
    switch (form) {
        case Form::NFC: instance = icu::Normalizer2::getNFCInstance(status); break;
        case Form::NFD: instance = icu::Normalizer2::getNFDInstance(status); break;
        case Form::NFKC: instance = icu::Normalizer2::getNFKCInstance(status); break;
    }
    if (U_FAILURE(status) || instance == nullptr)
        throw std::runtime_error("unable to initialize Unicode normalizer");
    return instance;
}

std::u32string normalize_impl(std::u32string_view text, Form form) {
    icu::UnicodeString value;
    for (char32_t cp : text) value.append(static_cast<UChar32>(cp));
    UErrorCode status = U_ZERO_ERROR;
    icu::UnicodeString out;
    normalizer(form)->normalize(value, out, status);
    if (U_FAILURE(status)) throw std::runtime_error("Unicode normalization failed");
    std::u32string result;
    result.reserve(static_cast<std::size_t>(out.length()));
    int32_t i = 0;
    while (i < out.length()) {
        UChar32 cp = 0;
        U16_NEXT(out.getBuffer(), i, out.length(), cp);
        if (cp < 0) throw std::runtime_error("invalid UTF-16 after normalization");
        result.push_back(static_cast<char32_t>(cp));
    }
    return result;
}
} // namespace

std::u32string normalize(std::u32string_view text, Form form) { return normalize_impl(text, form); }
std::u32string nfc(std::u32string_view text) { return normalize_impl(text, Form::NFC); }
std::u32string nfd(std::u32string_view text) { return normalize_impl(text, Form::NFD); }
std::u32string nfkc(std::u32string_view text) { return normalize_impl(text, Form::NFKC); }

std::u32string lower(std::u32string_view text) {
    icu::UnicodeString value;
    for (char32_t cp : text) value.append(static_cast<UChar32>(cp));
    value.toLower();  // full case mapping, root locale — Python str.lower()
    std::u32string out;
    out.reserve(static_cast<std::size_t>(value.length()));
    int32_t i = 0;
    while (i < value.length()) {
        UChar32 cp = 0;
        U16_NEXT(value.getBuffer(), i, value.length(), cp);
        if (cp < 0) throw std::runtime_error("invalid UTF-16 after lowercasing");
        out.push_back(static_cast<char32_t>(cp));
    }
    return out;
}

std::u32string lower_one(char32_t cp) { return lower(std::u32string(1, cp)); }

std::string category(char32_t cp) {
    switch (u_charType(static_cast<UChar32>(cp))) {
        case U_UPPERCASE_LETTER: return "Lu";
        case U_LOWERCASE_LETTER: return "Ll";
        case U_TITLECASE_LETTER: return "Lt";
        case U_MODIFIER_LETTER: return "Lm";
        case U_OTHER_LETTER: return "Lo";
        case U_NON_SPACING_MARK: return "Mn";
        case U_ENCLOSING_MARK: return "Me";
        case U_COMBINING_SPACING_MARK: return "Mc";
        case U_DECIMAL_DIGIT_NUMBER: return "Nd";
        case U_LETTER_NUMBER: return "Nl";
        case U_OTHER_NUMBER: return "No";
        case U_SPACE_SEPARATOR: return "Zs";
        case U_LINE_SEPARATOR: return "Zl";
        case U_PARAGRAPH_SEPARATOR: return "Zp";
        case U_CONTROL_CHAR: return "Cc";
        case U_FORMAT_CHAR: return "Cf";
        case U_PRIVATE_USE_CHAR: return "Co";
        case U_SURROGATE: return "Cs";
        case U_DASH_PUNCTUATION: return "Pd";
        case U_START_PUNCTUATION: return "Ps";
        case U_END_PUNCTUATION: return "Pe";
        case U_CONNECTOR_PUNCTUATION: return "Pc";
        case U_OTHER_PUNCTUATION: return "Po";
        case U_MATH_SYMBOL: return "Sm";
        case U_CURRENCY_SYMBOL: return "Sc";
        case U_MODIFIER_SYMBOL: return "Sk";
        case U_OTHER_SYMBOL: return "So";
        case U_INITIAL_PUNCTUATION: return "Pi";
        case U_FINAL_PUNCTUATION: return "Pf";
        default: return "Cn";
    }
}

int combining(char32_t cp) { return u_getCombiningClass(static_cast<UChar32>(cp)); }

bool is_alpha(char32_t cp) {
    const std::string cat = category(cp);
    return !cat.empty() && cat[0] == 'L';
}

std::string char_name(char32_t cp) {
    char buffer[128];
    UErrorCode status = U_ZERO_ERROR;
    const int32_t length =
        u_charName(static_cast<UChar32>(cp), U_UNICODE_CHAR_NAME, buffer, sizeof(buffer), &status);
    if (U_FAILURE(status) || length <= 0) return "";
    return std::string(buffer, static_cast<std::size_t>(length));
}

} // namespace orthography2ipa::uni
