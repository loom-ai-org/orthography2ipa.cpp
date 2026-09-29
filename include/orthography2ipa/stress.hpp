// Direct port of orthography2ipa/stress.py: primary word-stress detection
// and IPA stress marking. Consumes the declarative stress block of a
// LanguageSpec (the C++ spec keeps the StressRules fields flat on
// LanguageSpec itself) to locate the stressed syllable of an orthographic
// word and to insert the IPA stress mark into a transcription.
//
// The bundled syllabifier is a vowel-group splitter, good enough for
// end-anchored stress systems. A spec that sets ``constrain_onsets`` has
// its onset maximisation constrained by the licit onsets of the language
// (the _OnsetJudge below). Languages with a real syllabifier ship it as a
// registered SyllabifierPlugin — every function also accepts a
// pre-computed syllable list.
//
// Every public function here mirrors the same-named Python function; see
// the reference module for the linguistic rationale and citations.
#pragma once

#include "orthography2ipa/orthography2ipa.hpp"

#include <optional>
#include <set>
#include <string>
#include <vector>

namespace orthography2ipa::stress {

// stress.py SECONDARY_ALTERNATING / SECONDARY_MARK / LIGHT / HEAVY /
// SUPERHEAVY. The marks are single code points but travel inside UTF-8
// strings.
constexpr const char* SECONDARY_ALTERNATING = "alternating";
constexpr const char* SECONDARY_MARK = "ˌ";  // U+02CC
constexpr const char* LIGHT = "light";
constexpr const char* HEAVY = "heavy";
constexpr const char* SUPERHEAVY = "superheavy";

// stress.py cliticless_keys: the spec's stress.cliticless_words as a
// normalized lookup set (language-aware lowercased + NFC).
std::set<std::string> cliticless_keys(const LanguageSpec& spec);

// stress.py is_cliticless: whether *word* is a declared prosodic clitic
// that takes no word stress.
bool is_cliticless(const std::string& word, const LanguageSpec& spec);

// stress.py syllabify: split *word* into syllables by vowel groups.
// Each maximal run of vowel characters becomes a nucleus; consonants
// attach to the following nucleus as far as the language licenses
// (onset-maximising, constrained by *spec* when it sets constrain_onsets),
// trailing consonants to the last syllable. *vowels* is the optional set
// of nucleus letters (stress.vowel_letters, lowercased); *diphthongs*
// splits a vowel run into several nuclei. *max_onset* caps the onset.
std::vector<std::string> syllabify(
    const std::string& word,
    const std::set<std::string>* vowels = nullptr,
    const std::vector<std::string>& diphthongs = {},
    bool coda_liquid_capture = false,
    const LanguageSpec* spec = nullptr,
    std::optional<int> max_onset = std::nullopt);

// stress.py syllabify_for_mark: divide a transcription into syllables for
// stress marking — maximal onset, constrained by what the language licenses.
std::vector<std::string> syllabify_for_mark(const std::string& ipa,
                                            const LanguageSpec& spec);

// stress.py syllabify_ipa: split an IPA word into syllables, dividing
// clusters by *max_onset*. Boundaries are drawn over SEGMENTS (a base plus
// its trailing modifiers is one consonant), because weight depends on them.
std::vector<std::string> syllabify_ipa(const std::string& ipa, int max_onset = 1,
                                       const std::vector<std::string>& atoms = {});

// stress.py syllable_weight: classify one IPA syllable as "light",
// "heavy" or "superheavy" (LIGHT/HEAVY/SUPERHEAVY).
std::string syllable_weight(const std::string& syllable,
                            const std::vector<std::string>& atoms = {});

// stress.py detect_stress: the 0-based index of the stressed syllable of
// *word*. Precedence: written accents → final_stress_endings →
// penult_stress_endings → antepenult_stress_endings → default_position.
// Monosyllables are inherently stressed. *syllables* takes precedence over
// plugin lookup; *lang* selects a registered syllabifier plugin (and a
// stress plugin when the spec declares stress source "plugin" — missing
// then is fatal, as in the reference).
int detect_stress(const std::string& word, const LanguageSpec& spec,
                  const std::vector<std::string>* syllables = nullptr,
                  const std::string& lang = "");

// stress.py detect_stress_by_weight: locate the stressed syllable of *ipa*
// by weight (quantity-sensitive systems). Returns an end-anchored index
// (-1 final, -2 penult, ...), ready for apply_stress_mark.
int detect_stress_by_weight(const std::string& ipa, const LanguageSpec& spec,
                            const std::vector<std::string>& atoms = {});

// stress.py secondary_stress_positions: the syllable indices that carry
// SECONDARY stress, as a 0-based set. Empty unless the spec declares
// stress.secondary_stress == "alternating". *stress_index* may be absent
// (no main stress could be placed — a clitic sentinel or no stress rules).
std::set<int> secondary_stress_positions(int n_syllables,
                                         std::optional<int> stress_index,
                                         const LanguageSpec& spec);

// stress.py apply_stress_mark: insert spec.stress_mark before the stressed
// syllable of *ipa* (and ˌ before every secondary-stress syllable in
// *secondary_indices*). *mark* replaces spec.stress_mark when given (the
// pitch-accent-2 caller passes accent2_mark). A non-negative
// *stress_index* is interpreted over the *orthographic* syllable count
// (*syllables*) and converted to an end-anchored offset; a negative one is
// the end-anchored offset directly. *ipa_syllables* is a pre-computed
// syllable division OF THE IPA (a quantity-sensitive caller's own
// division). Already-marked transcriptions are returned unchanged.
std::string apply_stress_mark(
    const std::string& ipa, const LanguageSpec& spec, int stress_index,
    const std::vector<std::string>* syllables = nullptr,
    const std::vector<std::string>* ipa_syllables = nullptr,
    const std::string& mark = "",
    const std::vector<int>& secondary_indices = {});

// stress.py iambic_length_positions: the 0-based indices of
// *ipa_syllables* whose nucleus should lengthen (weight-alternating
// footing; empty for a word of fewer than three syllables).
std::set<int> iambic_length_positions(
    const std::vector<std::string>& ipa_syllables,
    const std::vector<std::string>& atoms = {});

// stress.py apply_iambic_length: lengthen the foot-head nuclei of *ipa*
// per spec.iambic_length. No-op unless the spec opted in; a word already
// carrying a length mark is returned unchanged. Runs on the bare phoneme
// string, BEFORE the stress mark is written, so it never moves it.
std::string apply_iambic_length(const std::string& ipa, const LanguageSpec& spec,
                                const std::vector<std::string>& atoms = {});

// stress.py _syllables_for: syllabify *word* — registered syllabifier
// plugin for *lang* first (its result must round-trip to the word), the
// spec-driven bundled splitter second. *diphthongs* reaches the bundled
// splitter only. Without *spec* the spec is looked up for *lang*.
std::vector<std::string> syllables_for(
    const std::string& word, const std::string& lang,
    const std::vector<std::string>& diphthongs = {},
    const LanguageSpec* spec = nullptr);

// stress.py _plugin_stress: ask the registered stress plugin, or fail
// loudly if the spec expects one (stress source "plugin" with no
// registered plugin is fatal on purpose).
std::optional<int> plugin_stress(const std::string& word,
                                 const std::vector<std::string>& syllables,
                                 const std::string& lang);

} // namespace orthography2ipa::stress
