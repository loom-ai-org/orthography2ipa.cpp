// Direct port of orthography2ipa/vowels.py — see the reference module for
// the documentation and citations behind every set and predicate. All
// internals operate on std::u32string (Python code points); the public
// entry points convert at UTF-8 boundaries.
#include "orthography2ipa/vowels.hpp"

#include "unicode_util.hpp"

#include <algorithm>
#include <array>
#include <optional>
#include <string_view>

namespace orthography2ipa::vowels {
namespace {

using uni::to_utf32;
using uni::to_utf8;

bool contains_codepoint(std::u32string_view set, char32_t cp) {
    return set.find(cp) != std::u32string_view::npos;
}

// ── Orthographic vowels: Latin (incl. accented/diacritic forms) + Greek ──
const std::u32string ORTHOGRAPHIC_VOWELS = to_utf32(
    "aeiou"
    "áéíóúàèìòùâêîôûãõäëïöüåæø"
    "ẽĩũ"
    "ąęėįųūīāēőűýěůŏŭıå"
    "αεηιουωάέήίόύώΐΰ"
    "ًٌٍَُِٰ"
    "аеёиоуыэюяіїєѐѝ"
    "әөүұӕ");

// Cyrillic letters that decompose to a vowel base but are glides, not
// vowels (й /j/, ў /w/) — refused by name in is_orthographic_vowel.
const std::u32string GLIDES_WITH_VOWEL_BASE = to_utf32("йў");

// ── IPA vowels (vocoids) ──
const std::u32string IPA_VOWELS = to_utf32(
    "aeiou"
    "ɛɔəɨʉɯæɐʌɒœøɪʊɤɵɞɑ"
    "ɘɚɜɝɶy"
    "ãẽĩõũ"
    "̯̃");

// ── Front / back orthographic vowel classes ──
const std::u32string FRONT_BASE = to_utf32("eiyεηιυ");
const std::u32string BACK_BASE = to_utf32("aouαοω");

// Combining diacritics that keep the base vowel's front/back axis.
const std::u32string AXIS_PRESERVING_MARKS = to_utf32(
    "̨̣́̀̂̌̄̆̇̃͂");

// Characters whose class does NOT come from stripping an axis-preserving
// mark: diaeresis/umlaut fronts, dotless ı, non-decomposing front letters,
// Greek dialytika forms.
const std::u32string FRONT_EXPLICIT = to_utf32(
    "äöüëïÿ"
    "ı"
    "øœæ"
    "ϊϋΐΰῗῧ");

// Ring vowels (å, ẙ) straddle the axis — in NEITHER class.
const std::u32string AXIS_AMBIGUOUS = to_utf32("åẙ");

// IPA on-glides: glide + nucleus is still a vowel letter; a bare glide is not.
const std::u32string ONGLIDES = to_utf32("jwɥ");

// IPA vowel symbols by front/back axis (central vowels on neither).
const std::u32string IPA_FRONT = to_utf32("iyɪʏeøɛœæaɶ");
const std::u32string IPA_BACK = to_utf32("uʊɯoɤɔɑɒʌ");

// Single palatal / palato-alveolar / alveolo-palatal IPA symbols.
const std::u32string PALATAL_SINGLE = to_utf32("ʎɲʃʒjcɟçʝɕʑɥ");

// Affricates whose first segment is a coronal stop — matched as a prefix.
const std::array<std::u32string, 4> PALATAL_AFFRICATES = {
    to_utf32("tʃ"), to_utf32("dʒ"), to_utf32("tɕ"), to_utf32("dʑ")};

const char32_t TIE_BAR = U'\u0361';
const char32_t PHARYNGEALIZATION = U'\u02E4';

// Tokens in a Unicode character NAME that mark the character a written vowel.
constexpr const char* UNICODE_VOWEL_NAME_TOKENS[] = {"VOWEL", "VOCALIC"};

// ── Closed inventories, derived from the letter sets themselves ──
// A (script, is-combining-mark) key, from the first word of the Unicode
// name plus the combining class — vowels.py _script_key. nullopt for
// unnamed characters.
struct ScriptKey {
    std::string script;
    bool is_mark;
    bool operator<(const ScriptKey& other) const {
        return script != other.script ? script < other.script : is_mark < other.is_mark;
    }
};

std::optional<ScriptKey> script_key(char32_t ch) {
    const std::string name = uni::char_name(ch);
    if (name.empty()) return std::nullopt;
    const auto space = name.find(' ');
    ScriptKey key;
    key.script = space == std::string::npos ? name : name.substr(0, space);
    key.is_mark = uni::combining(ch) != 0;
    return key;
}

const std::set<ScriptKey>& closed_inventories() {
    static const std::set<ScriptKey> inventories = [] {
        std::set<ScriptKey> out;
        for (char32_t ch : ORTHOGRAPHIC_VOWELS)
            if (auto key = script_key(ch)) out.insert(*key);
        return out;
    }();
    return inventories;
}

bool closed(char32_t ch) {
    const auto key = script_key(ch);
    return key && closed_inventories().count(*key) != 0;
}

bool is_letter_or_mark(char32_t ch) {
    const std::string cat = uni::category(ch);
    return !cat.empty() && (cat[0] == 'L' || cat[0] == 'M');
}

bool unicode_says_vowel(char32_t ch) {
    const std::string name = uni::char_name(ch);
    if (name.empty()) return false;
    for (const char* token : UNICODE_VOWEL_NAME_TOKENS)
        if (name.find(token) != std::string::npos) return true;
    return false;
}

// ── Core (u32 domain) predicates ──────────────────────────────────────

bool is_orthographic_vowel_u32(char32_t ch) {
    const std::u32string lowered = uni::lower_one(ch);
    if (lowered.size() == 1 && contains_codepoint(ORTHOGRAPHIC_VOWELS, lowered[0])) return true;
    if (lowered.size() == 1 && contains_codepoint(GLIDES_WITH_VOWEL_BASE, lowered[0])) return false;
    const std::u32string decomposed = uni::nfd(lowered);
    if (decomposed == lowered) return false;
    return contains_codepoint(ORTHOGRAPHIC_VOWELS, decomposed[0]) &&
           std::all_of(decomposed.begin() + 1, decomposed.end(),
                       [](char32_t m) { return uni::combining(m) != 0; });
}

bool is_ipa_vowel_u32(char32_t ch) {
    const std::u32string lowered = uni::lower_one(ch);
    if (lowered.size() == 1 && contains_codepoint(IPA_VOWELS, lowered[0])) return true;
    const std::u32string base = uni::nfd(std::u32string(1, ch));
    if (base == std::u32string(1, ch)) return false;
    const std::u32string lowered_base = uni::lower_one(base[0]);
    return lowered_base.size() == 1 && contains_codepoint(IPA_VOWELS, lowered_base[0]);
}

bool axis_preserving(char32_t mark) { return contains_codepoint(AXIS_PRESERVING_MARKS, mark); }

// vowels.py _vowel_axis: "front", "back" or none. Input is normally one
// character but may expand under full lowercasing (U+0130), exactly as
// the Python single-character str can.
std::optional<std::string_view> vowel_axis(const std::u32string& ch) {
    const std::u32string c = uni::lower(ch);
    if (c.size() == 1) {
        if (contains_codepoint(AXIS_AMBIGUOUS, c[0])) return std::nullopt;
        if (contains_codepoint(FRONT_EXPLICIT, c[0])) return std::string_view("front");
    }
    const std::u32string decomposed = uni::nfd(c);
    const char32_t base = decomposed.empty() ? 0 : decomposed[0];
    const bool all_preserving = std::all_of(decomposed.begin() + 1, decomposed.end(), axis_preserving);
    if (decomposed.size() > 1 && !all_preserving) return std::nullopt;
    if (contains_codepoint(FRONT_BASE, base)) return std::string_view("front");
    if (contains_codepoint(BACK_BASE, base)) return std::string_view("back");
    return std::nullopt;
}

// vowels.py _ipa_axis: front/back read off the IPA vowel chart.
std::optional<std::string_view> ipa_axis(const std::u32string& ipa) {
    for (char32_t c : ipa) {
        if (contains_codepoint(IPA_FRONT, c)) return std::string_view("front");
        if (contains_codepoint(IPA_BACK, c)) return std::string_view("back");
        if (is_ipa_vowel_u32(c)) return std::nullopt;  // a central vowel: neither axis
    }
    return std::nullopt;
}

bool syllabic_mark(char32_t ch) { return ch == U'\u0329' || ch == U'\u030D'; }

// vowels.py is_nucleus_only over u32.
bool is_nucleus_only_u32(const std::u32string& ipa) {
    if (ipa.empty()) return false;
    bool found = false;
    std::size_t i = 0;
    const std::size_t n = ipa.size();
    if (n > 1 && contains_codepoint(ONGLIDES, ipa[0])) i = 1;
    while (i < n) {
        const char32_t ch = ipa[i];
        const bool next_is_syllabic = i + 1 < n && syllabic_mark(ipa[i + 1]);
        if (next_is_syllabic) {
            found = true;
            i += 2;
            continue;
        }
        const std::string cat = uni::category(ch);
        if (uni::combining(ch) != 0 || cat == "Lm" || cat == "Sk") {
            ++i;
            continue;
        }
        if (is_ipa_vowel_u32(ch)) {
            found = true;
            ++i;
            continue;
        }
        return false;  // a consonant: this grapheme is not a written vowel
    }
    return found;
}

bool grapheme_is_vowel_u32(const std::u32string& grapheme, const std::vector<std::u32string>& ipa,
                           const std::set<std::u32string>& vowel_overrides) {
    if (grapheme.empty()) return false;
    if (vowel_overrides.count(grapheme)) return true;
    const char32_t ch = grapheme[0];
    if (is_orthographic_vowel_u32(ch)) return true;
    if (!is_letter_or_mark(ch)) return false;
    if (closed(ch)) return false;
    const std::u32string primary = ipa.empty() ? std::u32string{} : ipa[0];
    if (is_nucleus_only_u32(primary)) return true;
    return unicode_says_vowel(ch);
}

std::optional<std::string_view> grapheme_vowel_axis_u32(const std::u32string& grapheme,
                                                        const std::vector<std::u32string>& ipa,
                                                        const std::set<std::u32string>& vowel_overrides) {
    if (grapheme.empty()) return std::nullopt;
    const char32_t ch = grapheme[0];
    if (!vowel_overrides.count(grapheme)) {
        if (const auto axis = vowel_axis(std::u32string(1, ch))) return axis;
        if (closed(ch)) return std::nullopt;
    }
    if (!is_letter_or_mark(ch)) return std::nullopt;
    if (!grapheme_is_vowel_u32(grapheme, ipa, vowel_overrides)) return std::nullopt;
    return ipa_axis(ipa.empty() ? std::u32string{} : ipa[0]);
}

} // namespace

const std::string SYLLABIC_MARKS = "̩̍";

bool is_orthographic_vowel(char32_t ch) { return is_orthographic_vowel_u32(ch); }
bool is_ipa_vowel(char32_t ch) { return is_ipa_vowel_u32(ch); }
bool is_front_vowel(const std::u32string& ch) { return vowel_axis(ch) == std::string_view("front"); }
bool is_back_vowel(const std::u32string& ch) { return vowel_axis(ch) == std::string_view("back"); }
bool is_nucleus_only(const std::u32string& ipa) { return is_nucleus_only_u32(ipa); }

bool is_orthographic_vowel(const std::string& ch) {
    const std::u32string units = to_utf32(ch);
    if (units.size() != 1) return false;
    return is_orthographic_vowel_u32(units[0]);
}

bool is_ipa_vowel(const std::string& ch) {
    const std::u32string units = to_utf32(ch);
    if (units.size() != 1) return false;
    return is_ipa_vowel_u32(units[0]);
}

bool is_front_vowel(const std::string& ch) { return is_front_vowel(to_utf32(ch)); }

bool is_back_vowel(const std::string& ch) { return is_back_vowel(to_utf32(ch)); }

std::string base_vowel_letter(const std::string& ch) {
    const std::u32string units = to_utf32(ch);
    if (units.empty()) return ch;
    const std::u32string c = uni::lower_one(units[0]);
    const std::u32string decomposed = uni::nfd(c);
    if (decomposed.size() > 1 &&
        std::all_of(decomposed.begin() + 1, decomposed.end(), axis_preserving))
        return to_utf8(std::u32string(1, decomposed[0]));
    return to_utf8(c);
}

bool is_palatal_consonant(const std::string& ipa) {
    std::u32string s = to_utf32(ipa);
    s.erase(std::remove(s.begin(), s.end(), TIE_BAR), s.end());
    if (s.empty()) return false;
    for (const auto& aff : PALATAL_AFFRICATES)
        if (s.compare(0, aff.size(), aff) == 0) return true;
    return contains_codepoint(PALATAL_SINGLE, s[0]);
}

bool is_pharyngealized_consonant(const std::string& ipa) {
    return to_utf32(ipa).find(PHARYNGEALIZATION) != std::u32string::npos;
}

bool is_nucleus_only(const std::string& ipa) { return is_nucleus_only_u32(to_utf32(ipa)); }

bool grapheme_is_vowel(const std::string& grapheme, const std::vector<std::string>& ipa,
                       const std::set<std::string>& vowel_overrides) {
    std::vector<std::u32string> ipa_u32;
    ipa_u32.reserve(ipa.size());
    for (const auto& value : ipa) ipa_u32.push_back(to_utf32(value));
    std::set<std::u32string> overrides;
    for (const auto& value : vowel_overrides) overrides.insert(to_utf32(value));
    return grapheme_is_vowel_u32(to_utf32(grapheme), ipa_u32, overrides);
}

std::string grapheme_vowel_axis(const std::string& grapheme, const std::vector<std::string>& ipa,
                                const std::set<std::string>& vowel_overrides) {
    std::vector<std::u32string> ipa_u32;
    ipa_u32.reserve(ipa.size());
    for (const auto& value : ipa) ipa_u32.push_back(to_utf32(value));
    std::set<std::u32string> overrides;
    for (const auto& value : vowel_overrides) overrides.insert(to_utf32(value));
    const auto axis = grapheme_vowel_axis_u32(to_utf32(grapheme), ipa_u32, overrides);
    return axis ? std::string(*axis) : std::string();
}

} // namespace orthography2ipa::vowels
