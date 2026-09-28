// Direct port of orthography2ipa/vowels.py (the parts the Unicode token
// model depends on): the single shared owner of vowel-character
// classification. Every set and predicate mirrors the reference exactly;
// see the Python docstrings for the linguistic rationale.
#pragma once

#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace orthography2ipa::vowels {

// "̩̍" — U+0329 (syllabic, below) and U+030D (syllabic, above): the IPA
// syllabicity marks that make /r̩/ a nucleus rather than an onset.
extern const std::string SYLLABIC_MARKS;

// Code-point-domain cores, shared with the phonetok port. The str-based
// predicates below operate on one character but may expand under full
// lowercasing (U+0130 -> i + U+0307), hence the u32string overloads.
bool is_orthographic_vowel(char32_t ch);
bool is_orthographic_vowel(const std::u32string& ch);
bool is_ipa_vowel(char32_t ch);
bool is_front_vowel(const std::u32string& ch);
bool is_back_vowel(const std::u32string& ch);
bool is_nucleus_only(const std::u32string& ipa);

// True if *ch* (a single character, UTF-8) is a written (Latin or Greek)
// vowel letter — vowels.py is_orthographic_vowel.
bool is_orthographic_vowel(const std::string& ch);

// True if *ch* is an IPA vowel symbol (vocoid) — vowels.py is_ipa_vowel.
bool is_ipa_vowel(const std::string& ch);

// Front/back orthographic vowel classes — vowels.py is_front_vowel /
// is_back_vowel.
bool is_front_vowel(const std::string& ch);
bool is_back_vowel(const std::string& ch);

// The bare base vowel letter once axis-preserving diacritics are stripped
// — vowels.py base_vowel_letter. Returns the lowercased character
// unchanged when no such reduction applies.
std::string base_vowel_letter(const std::string& ch);

// Palatal / palato-alveolar consonant and pharyngealized ("emphatic")
// tests on an IPA string — vowels.py is_palatal_consonant /
// is_pharyngealized_consonant.
bool is_palatal_consonant(const std::string& ipa);
bool is_pharyngealized_consonant(const std::string& ipa);

// True if *ipa* is nothing but syllable nucleus — vowels.py is_nucleus_only.
bool is_nucleus_only(const std::string& ipa);

// Script-agnostic vowel-hood: spec data first, Unicode second —
// vowels.py grapheme_is_vowel. *ipa* is the grapheme's flat-table
// candidate list; *vowel_overrides* is the spec's vowel_graphemes set
// (whole grapheme strings).
bool grapheme_is_vowel(const std::string& grapheme, const std::vector<std::string>& ipa,
                       const std::set<std::string>& vowel_overrides);

// "front" / "back" / "" (for None) — vowels.py grapheme_vowel_axis.
std::string grapheme_vowel_axis(const std::string& grapheme, const std::vector<std::string>& ipa,
                                const std::set<std::string>& vowel_overrides);

} // namespace orthography2ipa::vowels
