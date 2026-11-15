// Direct port of orthography2ipa/inventory.py — the closed set of symbols a
// spec can emit, the TTS token vocabulary.
//
// A neural TTS frontend needs a fixed, enumerable symbol set: the model's
// embedding table is built from it before training, and a symbol that shows up
// at inference time but never at training time has no embedding — the word
// carrying it is mispronounced, permanently and silently. So a phonemizer that
// can emit a symbol nobody enumerated is not safe to train on.
//
// This module answers two questions a consumer must be able to ask —
// `emission_inventory` (every IPA string the spec's tables can put on a slot)
// and `phoneme_inventory` (those emissions cut into SEGMENTS, which is the
// token set proper) — and one a maintainer should: `dead_allophone_rules`
// (rules whose target phoneme no grapheme in this spec can produce).
//
// ## The closure invariant
//
// `emission_inventory` is derived from the same tables that do the emitting, so
// it is closed by construction: every place the engine can put IPA on a slot
// has to be a place this function reads. When someone adds a new source of IPA
// and forgets, the closure test is what catches it.
//
// Python type mapping: `FrozenSet[str]` -> `std::set<std::string>` (a set has
// no order to preserve; emission_inventory/phoneme_inventory/dead_allophone_rules
// are compared as sets, dead_allophone_rules' vector keeps the reference's rule
// declaration order), `Tuple[str, ...]` / `List[str]` ->
// `std::vector<std::string>` (order significant: `phoneme_atoms` is longest
// first and `tokenize` is the token sequence).
#pragma once

// The header is self-contained — it includes what it uses and forward declares
// `LanguageSpec` instead of pulling in orthography2ipa.hpp, so it does not
// depend on include order. It must still be #included at GLOBAL scope, exactly
// like features.hpp and sentence.hpp: nothing can survive inclusion inside
// orthography2ipa.hpp's reopened `namespace orthography2ipa { ... }` block,
// because a header included there is nested a second time
// (orthography2ipa::orthography2ipa::inventory) under either namespace form,
// and the API then does not exist at global scope.
#include <map>
#include <set>
#include <string>
#include <vector>

namespace orthography2ipa {
struct LanguageSpec;
} // namespace orthography2ipa

namespace orthography2ipa::inventory {

/// The `{word: ipa}` mapping an engine holds in Pipeline::lexicon. Each function
/// below that takes one reads that mapping, and reads the process-wide lexicon
/// when it is nullptr.
using Lexicon = std::map<std::string, std::string>;

/// inventory.py STRESS_MARKS: primary and secondary stress, inserted by
/// `stress.apply_stress_mark`. UTF-8, in the reference's declared order.
extern const std::vector<std::string> STRESS_MARKS;

/// inventory.py emission_inventory: every IPA string *spec* can place on a
/// slot. The union of every source the engine can take IPA from — the grapheme
/// table and the positional grapheme table; the allophone map's surfaces and
/// the `allophone_rules` surfaces (a rule is a NEW source of IPA, so an
/// inventory read off the graphemes alone would miss Najdi's `ts`); the
/// cross-word `sandhi_rules` surfaces; the word-level overrides
/// (`word_exceptions` and the sidecar *lexicon*); the grammatical
/// endings, whose whole ambiguous candidate list is an emission source; and the
/// stress marks when the spec declares stress rules. The empty string is
/// excluded: a deleted slot emits nothing, which is the absence of a symbol
/// rather than a symbol.
std::set<std::string> emission_inventory(const LanguageSpec& spec,
                                         const Lexicon* lexicon = nullptr);

/// inventory.py phoneme_inventory: the token set — `emission_inventory`, cut
/// into segments. An emission is what one slot carries, and it is not always
/// one phoneme (`ja` is a glide plus a vowel), so each emission is cut with
/// `segment_ipa` against the spec's own atoms; multi-character segments stay
/// whole when the data declares them.
std::set<std::string> phoneme_inventory(const LanguageSpec& spec,
                                        const Lexicon* lexicon = nullptr);

/// inventory.py phoneme_atoms: the multi-character segments *this spec
/// declares*, longest first. Public because segmentation is not only a
/// tokenizer concern: syllable weight has to know that a spec's `ts` is one
/// consonant. Derived from the spec's own tables, so what counts as a segment
/// is decided by the data.
std::vector<std::string> phoneme_atoms(const LanguageSpec& spec,
                                       const Lexicon* lexicon = nullptr);

/// inventory.py tokenize: cut a transcription into the tokens a TTS model
/// consumes. Every token returned is in `phoneme_inventory` — the closure
/// invariant. Stress marks are their own tokens (they are prosodic, not
/// segmental, so `ˈ` never rides on the segment before it), and whitespace is
/// dropped: a word boundary is the caller's concern.
std::vector<std::string> tokenize(const std::string& ipa, const LanguageSpec& spec,
                                  const Lexicon* lexicon = nullptr);

/// inventory.py dead_allophone_rules: ids of the `allophone_rules` whose
/// target phoneme this spec cannot produce, in rule declaration order. A rule
/// that cannot fire is not a subtle bug, it is a rule written against the wrong
/// symbol. Reachability is checked against BOTH inventories (the rescorer
/// matches a rule's target against a slot's whole IPA and against the segments
/// within it), and an abugida adds reading + inherent vowel to the producible
/// set. A rule with no phonemes targets nothing, so it is never dead.
std::vector<std::string> dead_allophone_rules(const LanguageSpec& spec);

} // namespace orthography2ipa::inventory
