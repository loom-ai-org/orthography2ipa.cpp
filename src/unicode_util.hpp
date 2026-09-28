// Unicode utilities shared by the direct ports of the reference Python
// modules (vowels.py, phonetok.py). Python strings are sequences of Unicode
// code points, so the ports work on std::u32string and convert at UTF-8
// boundaries. All semantics mirror the Python stdlib exactly:
// unicodedata.normalize/category/combining/name and str.lower()/isalpha().
#pragma once

#include <string>
#include <string_view>

namespace orthography2ipa::uni {

enum class Form { NFC, NFD, NFKC };

std::u32string to_utf32(std::string_view utf8);
std::string to_utf8(std::u32string_view utf32);

std::u32string normalize(std::u32string_view text, Form form);
std::u32string nfc(std::u32string_view text);
std::u32string nfd(std::u32string_view text);
std::u32string nfkc(std::u32string_view text);

// Full Unicode lowercase (root locale), equivalent to Python str.lower().
// May expand one code point into several (U+0130 -> i + U+0307).
std::u32string lower(std::u32string_view text);
std::u32string lower_one(char32_t cp);

// Two-letter general category, e.g. "Lu", "Mn", "Lo" (Python
// unicodedata.category; "Cn" for unassigned/unknown).
std::string category(char32_t cp);

// Canonical combining class (Python unicodedata.combining).
int combining(char32_t cp);

// Python str.isalpha(): general category starts with "L".
bool is_alpha(char32_t cp);

// Modern Unicode character name, or "" when the character is unnamed
// (Python unicodedata.name(ch, "")).
std::string char_name(char32_t cp);

} // namespace orthography2ipa::uni
