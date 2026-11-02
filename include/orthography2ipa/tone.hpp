#pragma once

// Direct port of orthography2ipa/tone.py: tone-mark placement over the
// universal IPA tone symbols and a maximal-onset syllable division, plus
// computed tone — tone spelled by the syllable's SHAPE (initial-consonant
// class × rime shape × tone mark) rather than by a diacritic.
#include <cstddef>
#include <string>
#include <optional>
#include <string>
#include <vector>

namespace orthography2ipa {
struct ToneData;
}

namespace orthography2ipa::tone {

// tone.py _Rime: the three readings `_syllable_tone` branches on. nullopt
// (the reference returns None) when the syllable has no nucleus at all.
struct Rime {
    /// Code-point offset in the syllable's IPA just past the nucleus.
    std::size_t nucleus_end = 0;
    bool long_vowel = false;
    bool dead = false;
};

// tone.py _rime: the nucleus run, its length mark, and whether the syllable
// is DEAD (a coda opening with one of *dead_codas*, or no coda and a short
// nucleus) — the shape half of the class × shape × mark lookup.
std::optional<Rime> rime(const std::string& ipa, const std::vector<std::string>& atoms,
                         const std::vector<std::string>& dead_codas);

// tone.py _syllable_tone: the tone letter one syllable's spelling calls for,
// or "" when its initial class is undetermined, its shape has no nucleus, or
// the table has neither the (class, shape, mark) triple nor a "any"-mark
// fallback for that class. Exposed so a caller can see the three intermediate
// readings (class, mark, shape) that produced one tone letter.
std::string syllable_tone(const std::vector<std::string>& graphemes,
                          const std::string& ipa, const ToneData& rules,
                          const std::vector<std::string>& atoms);

// The two intermediate reads of `syllable_tone`, exposed for the same reason:
// the initial-consonant CLASS the syllable is filed under ("" when none), and
// the tone MARK its spelling carries (its `no_mark` when unmarked).
std::string syllable_class(const std::vector<std::string>& graphemes,
                           const ToneData& rules);
std::string syllable_mark(const std::vector<std::string>& graphemes,
                          const ToneData& rules);

// tone.py _syllable_slots: group slot indices into syllables by maximal
// onset over SLOTS; slots that spell nothing ride the syllable of the
// nearest slot to their left.
std::vector<std::vector<std::size_t>> syllable_slots(
    const std::vector<std::string>& segments);

// tone.py dock_tone_marks: move every tone mark in *ipa* to the end of the
// syllable it belongs to (maximal onset; offglides and codas; the last
// consonant before a following vowel is the next syllable's onset).
// Idempotent. *atoms* are the language's multi-character phonemes, so
// "ts" is one onset and not a coda t plus an onset s.
std::string dock_tone_marks(const std::string& ipa,
                            const std::vector<std::string>& atoms);

// tone.py assign_computed_tones: write each syllable's tone into
// *segments* per *rules* (the spec's tone_rules ToneData). *graphemes* and
// *segments* are the parallel per-slot arrays of one reading ("" for a
// slot that spells no segment). The tone letter is appended at the end of
// its syllable.
std::string assign_computed_tones(const std::vector<std::string>& graphemes,
                                  const std::vector<std::string>& segments,
                                  const ToneData& rules,
                                  const std::vector<std::string>& atoms);

} // namespace orthography2ipa::tone
