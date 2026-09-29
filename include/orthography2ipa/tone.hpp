#pragma once

// Direct port of orthography2ipa/tone.py: tone-mark placement over the
// universal IPA tone symbols and a maximal-onset syllable division, plus
// computed tone — tone spelled by the syllable's SHAPE (initial-consonant
// class × rime shape × tone mark) rather than by a diacritic.
#include <cstddef>
#include <string>
#include <vector>

namespace orthography2ipa::tone {

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
