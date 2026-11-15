#pragma once

// Direct port of orthography2ipa/underspecification.py — what the writing does
// not say, and admitting it.
//
// An abjad does not write its short vowels, so the engine's reading of a bare
// skeleton is a guess. This module reports that honestly: which letters the
// reading invented a vowel for, and how much of the word the writing actually
// determines. It changes no transcription and restores no mark.
//
// It consumes LanguageSpec.script_type and LanguageSpec.optional_marks, which
// the C++ spec already carries. The one script type the reference lists is
// ScriptType.ABJAD, whose value is the string "abjad".
//
// Positions are Unicode CODE POINT indices, as Python's enumerate() over a str
// produces them.
#include <cstddef>
#include <string>
#include <vector>

namespace orthography2ipa {
struct LanguageSpec;                 // orthography2ipa/orthography2ipa.hpp
} // namespace orthography2ipa

namespace orthography2ipa::underspecification {

/// underdetermined_positions: the indices of the letters in *word* whose
/// reading the writing does not fix. Empty for an orthography that writes its
/// vowels, and for a fully-marked word.
std::vector<std::size_t> underdetermined_positions(const std::string& word,
                                                   const LanguageSpec& spec);

/// is_underdetermined: True when *word*'s reading is, in part, a guess.
bool is_underdetermined(const std::string& word, const LanguageSpec& spec);

/// mark_density: the fraction of *word*'s letters the writing actually
/// determines, in [0, 1]. 1.0 for a fully-marked word and for any orthography
/// that writes its vowels; 0.0 for a bare skeleton.
double mark_density(const std::string& word, const LanguageSpec& spec);

} // namespace orthography2ipa::underspecification
