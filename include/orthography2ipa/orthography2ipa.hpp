#pragma once

#include "orthography2ipa/phonetok.hpp"

// features.hpp and sentence.hpp must be included at GLOBAL scope (the trap
// this header's own reopened `namespace orthography2ipa` sets for anything
// included inside it), and they are needed here because G2P's public surface
// returns their types by value.
#include "orthography2ipa/features.hpp"
#include "orthography2ipa/inventory.hpp"
#include "orthography2ipa/sentence.hpp"

#include <cstddef>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <utility>
#include <array>
#include <boost/property_tree/ptree.hpp>

namespace orthography2ipa {

namespace sandhi { class Engine; }

// Candidate and rescorer::SegmentSlot are declared in
// orthography2ipa/phonetok.hpp (the token model owns both dataclasses), and
// IPAPath likewise lives there — the shared beam path type the tokenizer and
// the engine both produce.

struct AllophoneRule {
    std::string id, surface, append, syllable_position, stress, preceded_by, followed_by;
    std::string preceded_by_2, followed_by_2, preceded_by_3, followed_by_3;
    std::string mutates_neighbor, mutates_neighbor_side;
    std::vector<std::string> phonemes, graphemes, word;
    std::vector<std::string> preceded_by_phoneme, followed_by_phoneme;
    std::vector<std::string> preceded_by_phoneme_2, followed_by_phoneme_2, preceded_by_surface_phoneme_2;
    std::vector<std::string> preceded_by_grapheme, followed_by_grapheme, followed_by_grapheme_not;
    std::vector<std::string> word_contains_grapheme, word_contains_grapheme_not;
    std::optional<bool> requires_other_nucleus, followed_by_nucleus;
    std::optional<bool> word_initial, word_final;
};

} // namespace orthography2ipa

// The lattice rescoring seam (rescorer.py) and the post-lexical allophone
// rule layer compiled into it (allophony.py). Included AFTER the
// Candidate/AllophoneRule declarations above: the rescorer API stores both
// by value.
#include "orthography2ipa/rescorer.hpp"

namespace orthography2ipa {

struct SandhiRule {
    std::string id, name, left_context, right_context;
    std::optional<std::string> transform, right_transform;
    bool obligatory = true;
    std::string notes;
};

struct LinguisticSource {
    std::string id, author, title, publisher, url, doi, wikipedia_url, pages, notes;
    int year = 0;
};
struct OrthographyStandard {
    std::string name, authority, url, notes;
    int year = 0;
};
struct TimeSpan { int start_year = 0; std::optional<int> end_year; };
struct Location { double latitude = 0, longitude = 0; std::string source, notes; };
struct ToneData {
    std::map<std::string, std::string> classes, marks, tones;
    std::map<std::string, std::map<std::string, std::map<std::string, std::string>>> table;
    std::vector<std::string> dead_codas;
    std::string no_mark, notes;
};
struct AncestorLink { std::string code, role, notes; double weight = 0.0; };

/// types.py ClosedHiatus: written vowel pairs that are two nuclei where a
/// consonant closes the syllable of the second letter (stress.closed_hiatus).
struct ClosedHiatus {
    std::vector<std::string> pairs, onsets, codas;
};

struct LanguageSpec {
    std::string code, name, family, script, parent, quality;
    std::string glottolog_code, iso639_3, wikidata_qid, phoible_id, wals_code, notes;
    bool clade = false;
    std::string orthography_kind = "native";
    std::vector<AncestorLink> ancestors;
    std::map<std::string, std::vector<std::string>> graphemes;
    std::map<std::string, std::vector<std::string>> allophones;
    std::map<std::string, std::map<std::string, std::vector<std::string>>> positional_graphemes;
    std::vector<std::string> phonemes;
    std::map<std::string, std::string> word_exceptions;
    std::map<std::string, std::vector<std::optional<std::string>>> grammatical_endings;
    std::vector<AllophoneRule> allophone_rules;
    int allophone_passes = 1;
    std::vector<SandhiRule> sandhi_rules;
    std::map<std::string, std::vector<std::string>> plugins;
    std::vector<std::string> marked_vowels, final_stress_endings, penult_stress_endings;
    std::vector<std::string> antepenult_stress_endings, diphthongs;
    // IPA vowel sequences that form one nucleus where the stress mark is
    // placed; unset, the transcription is split with `diphthongs`.
    std::optional<std::vector<std::string>> ipa_diphthongs;
    // stress.closed_hiatus; unset leaves every listed diphthong one nucleus.
    std::optional<ClosedHiatus> closed_hiatus;
    std::vector<std::string> vowel_letters, onset_clusters;
    bool quantity_sensitive = false, superheavy_final_attracts = false;
    int max_onset = 1;
    // Whether max_onset came from the spec or is merely the default — the
    // default 1 is a placeholder and must not cap the onsets (Python
    // StressRules.max_onset_declared, set by the loader).
    bool max_onset_declared = false;
    // StressRules.source: "rules" (the declarative stress system) or
    // "plugin" (a registered StressPlugin; missing then is fatal).
    std::string stress_source = "rules";
    std::string secondary_stress;
    // "last": a hyphen-joined group keeps the main stress on its last marked element.
    std::string compound_stress;
    std::string accent2_mark;
    std::vector<std::string> accent2_final_letters, cliticless_words;
    bool constrain_mark_onsets = true, coda_liquid_capture = false, iambic_length = false;
    int default_stress_position = -2;
    std::string stress_mark = "ˈ";
    bool stress_defined = false;
    std::string script_type = "alphabet";
    std::string inherent_vowel;
    // Optional with a meaningful empty-string state (as/bn declare "" =
    // suppress the final inherent vowel), hence the tri-state.
    std::optional<std::string> inherent_vowel_final;
    // The segments a syllable may close on; empty leaves codas unconstrained. Own-only.
    std::vector<std::string> codas;
    std::string virama_final_vowel;
    bool coda_no_inherent_vowel = false;
    // A consonant letter that spells the nucleus between an unvowelled onset
    // and a closing consonant (Thai ⟨ว⟩ /uːa/), in declaration order; own-only.
    std::vector<std::pair<std::string, std::string>> consonant_nuclei;
    bool collapse_geminates = false, doubled_letters_geminate = true, constrain_onsets = false;
    // An apostrophe between two letters belongs to the written word (doesn't).
    bool word_internal_apostrophe = false;
    std::vector<std::string> fold_diacritics, vowel_graphemes, dependent_vowels, preposed_vowels, trailing_vowel_axis_digraphs;
    // Per-candidate weights for the weighted-object grapheme form
    // ({"ipa": [...], "weights": [...]}); own-only, sparse.
    std::map<std::string, std::vector<CandidateWeight>> grapheme_weights;
    std::map<std::string, std::string> tone_inventory;
    bool tone_marks_syllable_final = false;
    std::optional<double> latitude, longitude;
    std::vector<LinguisticSource> sources;
    std::vector<OrthographyStandard> orthography_standards;
    std::optional<TimeSpan> timespan;
    std::optional<Location> location;
    std::optional<ToneData> tone;
    std::vector<std::string> optional_marks, wikipedia, urls, family_path_metadata;
    std::map<std::string, std::string> identifiers;

    std::vector<std::string> family_path() const;

    // ─── types.py LanguageSpec's own accessors ──────────────────────────
    /// types.py resolve_grapheme: positional override (exact, then the
    /// positional DEFAULT), else the base grapheme table, else KeyError —
    /// spelled here as an empty optional-vector at the grapheme level, and a
    /// thrown std::out_of_range (the C++ Key analogue) when no mapping has
    /// the grapheme at all.
    std::optional<std::vector<std::string>>
    resolve_grapheme(const std::string& grapheme,
                     const std::string& position = "") const;
    /// types.py has_positional_data: any positional override at all.
    bool has_positional_data() const { return !positional_graphemes.empty(); }
    /// types.py positional_grapheme_keys: the graphemes with overrides.
    std::vector<std::string> positional_grapheme_keys() const;
    /// types.py positions_for_grapheme: the positions declared for one
    /// grapheme (document order of the loader's map? — the reference's
    /// dict order is the document's, which the C++ loader preserves by
    /// key-sorted map; the set is what matters for callers).
    std::vector<std::string> positions_for_grapheme(const std::string& grapheme) const;
    /// types.py get_ancestors: ancestors filtered by ROLE (empty = all), and
    /// synthesized from `parent` as (parent, PARENT, 1.0) when the tuple is
    /// empty. AncestorRole values are the reference enum's strings.
    /// AncestorRole values are the reference enum's strings; a null *role*
    /// pointer means every role (the reference's role=None).
    std::vector<AncestorLink>
    get_ancestors(const std::string* role = nullptr) const;
};

/// g2p.py ConfidenceBreakdown: the richer, per-signal view behind
/// WordTranscription.confidence. Every field is in [0.0, 1.0] and derived
/// purely from the lattice; `value` is the headline number (lattice ×
/// coverage), `lattice` is the weakest-link slot confidence before coverage,
/// `per_slot` one confidence per grapheme slot in surface order, and
/// `unmapped` the OOV characters folded into `coverage`.
struct ConfidenceBreakdown {
    double value = 1.0;
    double lattice = 1.0;
    std::vector<double> per_slot;
    double coverage = 1.0;
    std::vector<std::string> unmapped;
};

struct WordTranscription {
    std::string word, ipa;
    std::vector<IPAPath> candidates;
    std::vector<std::string> unmapped;
    double coverage = 1.0;
    double confidence = 1.0;
};

struct TranscriptionResult {
    std::string ipa, lang;
    std::vector<WordTranscription> words;
};

struct InventoryDistance {
    double jaccard{}, feature_mean{};
    std::size_t size_a{}, size_b{}, shared{};
};
struct GraphemeDivergence {
    std::size_t shared_graphemes{}, total_graphemes{};
    double mean_ipa_distance{}, overlap_ratio{};
};
struct SpellingDivergence {
    std::size_t shared_phonemes{}, total_phonemes{}, identical_spellings{}, disjoint_spellings{};
    double mean_distance{};
};
struct WeightedDistance {
    double inventory{}, grapheme{}, allophone{}, ancestry{}, temporal{}, combined{};
    std::array<double, 5> weights{};
};
struct PhonologicalDistance {
    InventoryDistance inventory;
    GraphemeDivergence grapheme;
    double allophone_sim{}, combined{};
};

struct PluginAnswer {
    std::string name;
    int priority = 0;
    bool selected = false;
};

class Tokenizer {
public:
    /// `expand_allophones` is g2p.py G2P.expand_allophones: the positional
    /// beam folds the spec's declared allophones into each slot's branches
    /// when it is set (`allophone_map=spec.allophones`), and the beam is
    /// byte-identical to the plain one when it is not.
    /// `apply_allophony` is g2p.py G2P.apply_allophony: the spec's allophone
    /// rules compile to null when it is off, so the rescorer chain is empty.
    /// `extra_rescorers` is the reference's `G2P._rescorers` MINUS the compiled
    /// allophone rescorer: the caller-supplied `rescorer=` chain entries and
    /// the lattice rescorers the declared `rescore` plugins contribute
    /// (rescorer_plugin.py). They are composed BEFORE the allophone chain,
    /// exactly as g2p.py orders them:
    /// `self._rescorers = base + declared_rescorers + self._allophone_chain`.
    /// An empty list — the default — leaves the chain byte-identical to the
    /// spec-only pipeline.
    explicit Tokenizer(const LanguageSpec& spec, bool expand_allophones = false,
                       bool apply_allophony = true,
                       const std::vector<const rescorer::LatticeRescorer*>& extra_rescorers = {});
    std::vector<Token> tokenize(const std::string& text) const;
    std::vector<Token> grapheme_tokens(const std::string& text) const;
    TokenSequence tokenize_with_context(const std::string& text) const;
    std::vector<std::string> tokenize_word(const std::string& word,
                                            std::vector<std::string>* unmapped = nullptr) const;
    /// g2p.py G2P._positional_beam: the context-carrying beam (stress,
    /// aperture, syllable and allophone context, and the compiled allophone
    /// rescorer chain at the slot seam).
    std::vector<IPAPath> beam(const std::string& word, std::size_t width = 8) const;
    /// g2p.py `self._tokenizer.ipa_beam(word, ..., rescorer=self._rescorers)`:
    /// the plain tokenizer beam WITH the rescorer chain, which is the word
    /// path for a spec that needs no context. No stress context exists on it.
    std::vector<IPAPath> rescored_beam(const std::string& word, std::size_t width) const;
    /// g2p.py G2P.candidates: the tokenizer beam with NO rescorer chain —
    /// the raw per-word candidate paths.
    std::vector<IPAPath> plain_beam(const std::string& word, std::size_t width) const;
    /// g2p.py G2P.ipa_lattice: the engine's structured lattice for one word —
    /// the SAME slot block `beam` walks (positional resolution with
    /// stress/syllable/aperture context, then the rescorer chain), returned as
    /// slots instead of paths. `pre_rescore` returns the `resolve_branches`
    /// output BEFORE the rescorer chain, which is the only way to see what the
    /// rescorers rewrote. Slots with no candidates (rescorer-deleted or
    /// silenced) are omitted; a slot's candidates are truncated to `width`
    /// (SIZE_MAX keeps all).
    std::vector<rescorer::SegmentSlot> positional_slots(const std::string& word,
                                                        std::size_t width,
                                                        bool pre_rescore = false) const;
private:
    const LanguageSpec& spec_;
    PhonetokTokenizer tokenizer_;
    // g2p.py engine-side derivations: whether any positional entry keys on
    // syllable aperture (the engine's _uses_aperture), and the graphemes
    // the spec declares as stress marks that emit nothing (the engine's
    // _silent_stress_marks).
    bool uses_aperture_ = false;
    std::string silent_stress_marks_;
    bool expand_allophones_ = false;
    // g2p.py G2P._rescorers: the spec's compiled allophone rescorer
    // (allophony.py compile_allophone_rescorer), repeated allophone_passes
    // times so a rule that only fires on another rule's output can feed off
    // it. Null when the spec declares no rules — the chain is empty and the
    // default path is byte-identical.
    std::unique_ptr<rescorer::LatticeRescorer> allophone_rescorer_;
    std::vector<const rescorer::LatticeRescorer*> rescorers_;
};

class RescorerPlugin;

class G2P {
public:
    /// The stage gates mirror the reference constructor's keyword defaults:
    /// `expand_allophones=False`, `apply_sandhi=True`, `apply_stress=True`,
    /// `apply_allophony=True`. They decide which stages exist at all, so they
    /// belong to the pipeline port; the remaining constructor options are a
    /// separate public-API gap.
    /// `sentence_rescorer` is g2p.py's keyword of the same name: one
    /// sentence::SentenceRescorer or a list of them (nullptr = none). They run
    /// at the SENTENCE seam, before the spec's declarative sandhi pass, and
    /// when the list is empty the branch is skipped entirely — the default
    /// pipeline is byte-identical.
    explicit G2P(std::string language, std::map<std::string, std::vector<std::string>> plugin_overrides = {},
                 std::string dialect_profile = "", bool expand_allophones = false,
                 bool apply_sandhi = true, bool apply_stress = true,
                 bool apply_allophony = true,
                 const sentence::SentenceRescorerArg& sentence_rescorer = nullptr,
                 const rescorer::RescorerArg& rescorer = nullptr);
    const LanguageSpec& spec() const;
    /// g2p.py G2P.lang: the RESOLVED canonical code this engine transcribes for
    /// (registry.py resolve() of what the caller asked for).
    const std::string& language() const;
    const std::map<std::string, std::vector<std::string>>& plugin_overrides() const;
    std::string transcribe(const std::string& text, const std::string& search = "greedy",
                           std::size_t beam_width = 8) const;
    TranscriptionResult transcribe_detailed(const std::string& text,
                                            const std::string& search = "greedy",
                                            std::size_t beam_width = 8) const;
    /// g2p.py G2P.sentence_lattice: the whole utterance as an ordered
    /// SentenceLattice — every word's per-grapheme lattice in order, with word
    /// boundaries and each word's phrase/utterance Position. A READ: it never
    /// applies the sentence rescorers or the spec's sandhi pass.
    sentence::SentenceLattice sentence_lattice(const std::string& text,
                                               const std::string& search = "greedy",
                                               std::size_t beam_width = 8) const;
    /// g2p.py transcribe_word: transcribe a single *word* through the
    /// per-word pipeline (beam, grammatical endings, computed tone,
    /// geminate collapse, word-final virama, tone-mark docking, stress).
    std::string transcribe_word(const std::string& word,
                                const std::string& search = "greedy",
                                std::size_t beam_width = 8) const;
    std::vector<IPAPath> candidates(const std::string& word, std::size_t beam_width = 8) const;
    /// The beam path `transcribe_word` finalizes: the engine's own choice of
    /// the positional beam (when the spec needs the context) or the plain
    /// rescored beam, then the rescorer plugins and the grammatical-ending
    /// rewrite, WITHOUT the word-final stages. A read-only extraction for the
    /// debug surfaces that must show which slot produced which segment (the
    /// tone internals and the per-slot lattice); `transcribe_word` is the
    /// string this plus `_finalize_word_ipa` produces.
    IPAPath engine_path(const std::string& word, std::size_t width = 1) const;
    /// g2p.py G2P.ipa_lattice: one SegmentSlot per GRAPHEME token with ranked
    /// candidates AFTER the rescorer chain. `beam_width` caps the candidates
    /// per slot; SIZE_MAX (the CLI's `-1`) keeps every one. This replaces the
    /// old `lattice` stand-in, which returned whole-word paths and was NOT the
    /// reference's lattice.
    std::vector<rescorer::SegmentSlot> ipa_lattice(const std::string& word,
                                                   std::size_t beam_width = 8) const;
    /// The same slots BEFORE the rescorer chain (g2p.py reaches this state only
    /// through positional.resolve_branches): the view in which the allophony
    /// rescorer's rewrites are visible.
    std::vector<rescorer::SegmentSlot> lattice_pre_rescore(const std::string& word,
                                                           std::size_t beam_width = 8) const;
    /// g2p.py G2P.word_candidates: the top-*k* FULLY FINALIZED readings of
    /// *word* (same word pipeline as transcribe_word), duplicates collapsed,
    /// empty when every path finalizes to "". `beam_width` nullopt = max(k, 8).
    std::vector<std::string> word_candidates(const std::string& word, std::size_t k = 5,
                                             std::optional<std::size_t> beam_width = std::nullopt) const;
    /// g2p.py G2P.features: one WordFeatures per word of *text* (the same
    /// normalizer + word split as transcribe), each holding a GraphemeFeatures
    /// per grapheme over the shared pronunciation lattice. A PURE READ: it never
    /// affects transcribe.
    std::vector<features::WordFeatures> features(const std::string& text) const;
    /// g2p.py G2P.word_confidence: ConfidenceBreakdown.value — the lattice's
    /// weakest-link slot confidence folded with the word's grapheme coverage.
    double word_confidence(const std::string& word, std::size_t beam_width = 8) const;
    /// g2p.py G2P.confidence_breakdown: the per-signal view behind
    /// word_confidence. A whole-word override is a certain answer, so its
    /// lattice confidence is 1.0 and only coverage can lower it.
    ConfidenceBreakdown confidence_breakdown(const std::string& word,
                                             std::size_t beam_width = 8) const;
private:
    std::string language_;
    const LanguageSpec* spec_;
    std::map<std::string, std::vector<std::string>> plugin_overrides_;
    std::string dialect_profile_;
    // g2p.py G2P.__init__: `self._sandhi = SandhiEngine(spec.sandhi_rules) if
    // spec.sandhi_rules else None`. The rule contexts are compiled once per
    // engine, so a malformed context is an error at construction, exactly as
    // `re.compile` raises in the reference. Null for a spec without rules —
    // the sentence pipeline then skips the sandhi stage entirely.
    std::shared_ptr<sandhi::Engine> sandhi_;
    // g2p.py G2P._needs_context_beam: whether the word path takes the
    // context-carrying positional beam or the plain tokenizer beam.
    bool needs_context_beam_ = false;
    // g2p.py G2P.expand_allophones / apply_sandhi / apply_stress /
    // apply_allophony: the stage gates.
    bool expand_allophones_ = false;
    bool apply_sandhi_ = true;
    bool apply_stress_ = true;
    bool apply_allophony_ = true;
    // g2p.py G2P._sentence_rescorers = normalize_sentence_rescorers(sentence_rescorer).
    // Owned by value: normalize_sentence_rescorers takes ownership of the
    // argument's elements, matching the reference's list-normalization.
    sentence::SentenceRescorers sentence_rescorers_;
    // g2p.py G2P.__init__ `user_rescorers` — the caller's `rescorer=` argument,
    // normalized once. Composed with the declared plugins' lattice rescorers in
    // front of the spec's allophone chain by `chain_rescorers()`.
    std::vector<const rescorer::LatticeRescorer*> user_rescorers_;
    // g2p.py G2P.__init__ `declared_rescorers`: `plugin.rescorers(self.lang)`
    // for each declared `rescore` plugin, caller override over the spec's
    // list, resolved once at construction. The engine holds each plugin, so
    // the rescorers it owns live as long as the engine, as in the reference,
    // even when the registry replaces the plugin under the same name.
    std::vector<std::pair<std::string, std::shared_ptr<const RescorerPlugin>>> declared_rescore_plugins_;
    std::vector<const rescorer::LatticeRescorer*> declared_rescorers_;
    /// g2p.py _StagePlugins.get: the plugins declared for *stage*, the caller's
    /// override over the spec's list, resolved by name. Throws MissingPlugin
    /// for a name that nothing provides.
    std::vector<std::string> declared(const std::string& stage) const;
    /// g2p.py G2P._rescorers minus the allophone chain: what Tokenizer needs in
    /// front of the chain it compiles itself (user rescorers, then the
    /// declared `rescore` plugins' lattice rescorers for this language).
    std::vector<const rescorer::LatticeRescorer*> chain_rescorers() const;
    // g2p.py G2P._silent_stress_marks (g2p.py: the graphemes the spec
    // declares as stress marks that emit nothing) — `_unmarked` strips them
    // before every whole-word key lookup.
    std::string silent_marks_;
    // g2p.py _transcribe_word: the per-word stage of the pipeline (word
    // overrides, grammatical endings, rescorer plugins, the word-final
    // ordering), shared by transcribe_detailed and transcribe_word.
    WordTranscription transcribe_one(const std::string& word, std::size_t width,
                                     bool forced, const std::string& forced_ipa) const;
    /// g2p.py G2P._override_for: the whole-word reading for *word* —
    /// `word_exceptions` then the lexicon, each keyed on the language-aware
    /// lower-casing of the word with the spec's silent stress marks removed.
    std::optional<std::string> override_for(const std::string& word) const;
    /// g2p.py G2P._normalize: the input after the spec's normalize plugins.
    std::string normalized(std::string text) const;
    /// g2p.py G2P._compound_elements: the element surfaces of a hyphen-joined
    /// word the spec stresses as one compound, or nullopt.
    std::optional<std::vector<std::string>> compound_elements(const std::string& word) const;
    /// g2p.py G2P._unmapped_chars: the UNKNOWN tokens the grapheme table does
    /// not cover, and the coverage fraction they reduce.
    void unmapped_chars(const std::string& word, std::vector<std::string>* unmapped,
                        double* coverage) const;
    /// The declared (or caller-overridden) `rescore` plugins, in priority
    /// order, each applied to the whole-word path list — the C++ stand-in for
    /// the reference's per-slot rescorer chain (whose contract is a separate
    /// public-API gap). Missing, foreign and non-deterministic plugins all
    /// raise, and inventory conformance is checked on the output.
    std::vector<IPAPath> apply_rescorer_plugins(const std::string& word,
                                                 std::vector<IPAPath> paths) const;
};

class NormalizePlugin {
public:
    virtual ~NormalizePlugin() = default;
    virtual std::vector<std::string> language_codes() const = 0;
    virtual std::string normalize(const std::string& text, const std::string& lang) const = 0;
};
class SyllabifierPlugin {
public:
    virtual ~SyllabifierPlugin() = default;
    virtual std::vector<std::string> language_codes() const = 0;
    virtual std::vector<std::string> syllabify(const std::string& word, const std::string& lang) const = 0;
    virtual int priority() const { return 50; }
};
class StressPlugin {
public:
    virtual ~StressPlugin() = default;
    virtual std::vector<std::string> language_codes() const = 0;
    virtual std::optional<std::size_t> stressed_index(const std::string& word,
                                                       const std::vector<std::string>& syllables,
                                                       const std::string& lang) const = 0;
    virtual int priority() const { return 50; }
};
/// A G2P that declares a rescore plugin holds it through a shared_ptr for its
/// whole life. So a plugin must not own, through a shared_ptr, a G2P that
/// declares this plugin: the two then keep each other alive, and neither is
/// ever destroyed. A plugin that needs such an engine keeps a std::weak_ptr
/// to it.
class RescorerPlugin {
public:
    virtual ~RescorerPlugin() = default;
    virtual std::vector<std::string> language_codes() const = 0;
    virtual std::vector<IPAPath> rescore(const std::string& word, const std::string& lang,
                                         const std::vector<IPAPath>& paths) const = 0;
    virtual int priority() const { return 50; }
    /// rescorer_plugin.py RescorerPlugin.rescorers: the LATTICE rescorers this
    /// plugin contributes for *lang*, in application order — the seam the
    /// reference composes into `G2P._rescorers`. A G2P calls it once at
    /// construction for each declared plugin, whatever its language_codes();
    /// an empty result contributes nothing for that variety.
    /// The plugin OWNS the returned objects; the engine keeps raw pointers to
    /// them and holds the plugin itself for as long as it uses them, exactly
    /// as the reference holds the sequence.
    virtual std::vector<const rescorer::LatticeRescorer*> lattice_rescorers(
            const std::string& /*lang*/) const { return {}; }
};
class SandhiPlugin {
public:
    virtual ~SandhiPlugin() = default;
    virtual std::vector<std::string> language_codes() const = 0;
    virtual std::vector<std::string> apply(const std::vector<std::string>& words,
                                            const std::vector<std::string>& surfaces,
                                            const std::vector<bool>& pausal,
                                            const std::string& lang) const = 0;
};

// registry.py get_stress_plugin / get_syllabifier: the plugin registered
// for *code*, if any (registry lookups, distinct from the declared-stage
// name lookups the engine uses).
// orthography2ipa/__init__.py re-exports these at package level
// (`from orthography2ipa.inventory import ...`). phoneme_atoms is NOT
// re-exported there, so it stays module-scoped as inventory::phoneme_atoms.
using inventory::STRESS_MARKS;
using inventory::emission_inventory;
using inventory::phoneme_inventory;
using inventory::tokenize;
using inventory::dead_allophone_rules;

const StressPlugin* get_stress_plugin(const std::string& code);
const SyllabifierPlugin* get_syllabifier(const std::string& code);

/// registry.py MissingPlugin: a spec named a plugin and it is not installed.
/// Deliberately fatal — falling back to the built-in would be a DIFFERENT
/// TRANSCRIPTION, silently. The message is the reference's, verbatim.
class MissingPlugin : public std::runtime_error {
public:
    explicit MissingPlugin(const std::string& message);
};

/// registry.py MissingStressPlugin: a spec set stress.source = "plugin" and no
/// stress plugin is registered. Fatal on purpose, for the same reason.
class MissingStressPlugin : public std::runtime_error {
public:
    explicit MissingStressPlugin(const std::string& message);
};

/// registry.py ancestry_chain: the `parent` chain above *code*, nearest
/// ancestor first, including the classification-only clade nodes the chain
/// passes through.
std::vector<std::string> ancestry_chain(const std::string& code);

/// registry.py get_declared_plugins: the plugins *spec* names for *stage*, in
/// the order it names them, resolved against the registered plugins of that
/// stage. Raises MissingPlugin for a name nothing provides — the reference's
/// message names the wanted plugin and lists what IS installed.
/// The five stage interfaces share no common base in C++, so this returns the
/// resolved PLUGINS AS NAMES plus one typed accessor per stage; use
/// declared_normalize_plugin() & co. to reach the object.
std::vector<std::string> get_declared_plugins(const std::string& stage,
                                              const LanguageSpec& spec);

/// registry.py get_rescorers: the rescorers every registered plugin contributes
/// for *code*, in plugin-priority order (lowest priority first, so the
/// highest-priority plugin gets the last word). Empty when nothing is
/// registered for the resolved code.
std::vector<const rescorer::LatticeRescorer*> get_rescorers(const std::string& code);

/// Threads: the library keeps process-wide state with no lock. That state is
/// the plugin registry, the spec cache, the lexicon registry and its cache,
/// and the data directory. Many free functions read or write it, for example
/// get, resolve, validate, who_answers, get_lexicon, the free transcribe and
/// every register_* and set_* function, and the G2P constructor fills the
/// spec cache. This pattern is safe from more than one thread:
///   1. On one thread, register every plugin and lexicon, build every G2P,
///      and call transcribe once on each G2P. That call loads the lexicon of
///      the engine's language into the cache.
///   2. Then several threads can call transcribe and transcribe_detailed on
///      those G2P objects at the same time. This does not hold for a G2P
///      built with a dialect profile: the profile's patterns compile into a
///      shared cache on first use.
/// While step 2 runs, no thread calls a free function of this library or
/// builds a G2P. A program that needs other calls from more than one thread
/// puts its library calls behind a mutex of its own. Every lexicon setter
/// (register_lexicon, set_lexicon_directory, use_packaged_lexicons,
/// clear_lexicons) drops the whole lexicon cache, so call them all before
/// step 1, not between step 1 and step 2.
void register_normalize_plugin(const std::string& name, std::shared_ptr<NormalizePlugin> plugin);
void register_syllabifier_plugin(const std::string& name, std::shared_ptr<SyllabifierPlugin> plugin);
void register_stress_plugin(const std::string& name, std::shared_ptr<StressPlugin> plugin);
void register_rescorer_plugin(const std::string& name, std::shared_ptr<RescorerPlugin> plugin);
void register_sandhi_plugin(const std::string& name, std::shared_ptr<SandhiPlugin> plugin);
void discover_plugins(const std::string& directory = "");

std::string resolve(const std::string& code);
/// registry.py `resolves_exactly`: true when *code* names a spec through the
/// alias tables, case folding, BCP-47 standardization or a default region,
/// and false when only the nearest-language match finds one.
bool resolves_exactly(const std::string& code);
const LanguageSpec& get(const std::string& code);
std::vector<std::string> available_codes(bool include_clades = false);
std::map<std::string, std::vector<std::string>> available_families();
std::map<std::string, std::vector<PluginAnswer>> who_answers(const std::string& code);
std::vector<std::string> validate(const std::string& code);
void set_data_directory(const std::string& path);

/// ``FileNotFoundError``, which lexicon.py resolve_lexicon_source raises for a
/// local lexicon source that is not a file. ``what()`` reads like Python's
/// ``str(exc)``.
class FileNotFoundError : public std::runtime_error {
public:
    explicit FileNotFoundError(const std::string& message) : std::runtime_error(message) {}
};

/// ``LookupError``, which lexicon.py use_packaged_lexicons raises for a code
/// that no packaged lexicon provider serves.
class LookupError : public std::runtime_error {
public:
    explicit LookupError(const std::string& message) : std::runtime_error(message) {}
};

void register_lexicon(const std::string& code, const std::string& source);
/// lexicon.py is_ipa_string: True when *ipa* is NFC and every character is a
/// phonetic letter, combining mark, or one of the tolerated prosodic
/// modifiers. Empty is not a transcription.
bool is_ipa_string(const std::string& ipa);
/// lexicon.py parse_lexicon_text: TSV text -> {word: ipa}, NFC-normalised,
/// blank lines skipped, malformed lines skipped, the FIRST entry of a word
/// winning.
std::map<std::string, std::string> parse_lexicon_text(const std::string& text);
/// lexicon.py validate_lexicon_text: (line, reason) pairs for every malformed
/// line (the same checks the reference runs when registering a lexicon).
std::vector<std::pair<std::size_t, std::string>> validate_lexicon_text(const std::string& text);
/// lexicon.py resolve_lexicon_source: a local path with a leading ~ expanded
/// (std::runtime_error when the home directory cannot be found), and
/// http(s):// and hf:// fetched once into the cache and reused. Throws
/// FileNotFoundError when a local source is not a file. Returns the local FILE
/// path.
std::string resolve_lexicon_source(const std::string& source);
/// weights.py normalize_grapheme_value (over the loader's JSON node): one
/// grapheme's value -> (ipa, weights-or-nothing). The plain-list form keeps
/// weights empty; the weighted-object form {"ipa": [...], "weights": [...]}
/// carries them; a null (the inheritance tombstone) is passed through as an
/// empty ipa with weights-less, which the SPLIT preserves as nullopt.
std::pair<std::optional<std::vector<std::string>>, std::optional<std::vector<CandidateWeight>>>
normalize_grapheme_value(const boost::property_tree::ptree& value);
/// weights.py split_weighted_graphemes: the raw grapheme table -> the flat
/// (grapheme -> readings) map — keeping the inheritance nulls as nullopt
/// entries, exactly as the reference preserves them for the loader's shadow
/// semantics — plus the SPARSE weight table (an entry only for keys in the
/// weighted-object form).
std::pair<std::map<std::string, std::optional<std::vector<std::string>>>,
          std::map<std::string, std::vector<CandidateWeight>>>
split_weighted_graphemes(const boost::property_tree::ptree& raw_graphemes);
/// lexicon.py set_lexicon_dir: a leading ~ in *path* is expanded, as it is in
/// $ORTHOGRAPHY2IPA_LEXICON_DIR.
void set_lexicon_directory(const std::string& path);
/// lexicon.py clear_lexicons: forgets every registered lexicon, the lexicon
/// dir, the discovered packaged-lexicon providers and the enabled ones.
void clear_lexicons();
/// lexicon.py use_packaged_lexicons: enables the packaged lexicon of every
/// discovered provider, or of the providers that serve *codes* (each resolved
/// as get() resolves it), and returns every enabled resolved code, sorted. A
/// packaged lexicon ranks below register_lexicon and the lexicon dir, and above
/// the rules; clear_lexicons disables it. Throws LookupError when a code in
/// *codes* has no provider. Finding a provider enables nothing: only this call
/// does.
///
/// Python discovers providers through the ``orthography2ipa.lexicons`` entry
/// points. The port reads a search path instead, once, on the first call of
/// this function or of packaged_lexicon_sources (and again after
/// clear_lexicons):
///   - $ORTHOGRAPHY2IPA_PACKAGED_LEXICONS, a ':'-separated list of roots, in
///     order (empty entries are skipped, a leading ~ is expanded); set to the
///     empty string, it names no root;
///   - when that variable is unset, the o2i-lexicons directory beside the
///     default spec data directory: <prefix>/share/o2i-lexicons for an
///     install.
/// A root has the layout of the o2i-lexicons package's data directory,
/// ``<root>/<code>/<source>.tsv``. Each directory under a root that holds a
/// ``*.tsv`` file is one provider, named for the directory as an entry point
/// is named for its code. Its lexicon is the one ``*.tsv`` file in it, as
/// o2i_lexicons.tsv_path returns it; a provider with more than one fails and
/// serves nothing. A provider whose name does not resolve exactly (see
/// resolves_exactly) is skipped. When two providers serve one spec, the one
/// named for the exact resolved code wins, then the earlier root, then the
/// name. Skipped and failing providers are reported on stderr with the
/// reference's warning text.
///
/// A root that does not exist, or that is a file, is skipped with no message.
/// Discovery throws std::filesystem::filesystem_error when the process cannot
/// read the status of a root, for example under a directory it cannot search,
/// and std::runtime_error ("Could not determine home directory.") for a
/// ``~user`` entry whose user is unknown. Neither is a LookupError.
std::vector<std::string> use_packaged_lexicons();
std::vector<std::string> use_packaged_lexicons(const std::vector<std::string>& codes);
/// lexicon.py packaged_lexicon_sources: the discovered providers as
/// {resolved code: local file}. Enables nothing; a failing provider is left
/// out.
std::map<std::string, std::string> packaged_lexicon_sources();
std::vector<std::string> available_lexicon_codes();
std::map<std::string, std::string> get_lexicon(const std::string& code);
std::optional<std::string> lexicon_source(const std::string& code);
std::optional<std::string> lexicon_path(const std::string& code);
std::vector<std::pair<std::size_t, std::string>> validate_lexicon(const std::string& text);

// allophony.py segment_ipa: split an IPA candidate string into
// phoneme-sized segments. *atoms* are multi-character phonemes the caller
// cares about, tried longest-first; everything else groups as one base
// character plus its trailing modifiers.
std::vector<std::string> segment_ipa(const std::string& ipa,
                                     std::vector<std::string> atoms = {});

std::string transcribe(const std::string& text, const std::string& language);
std::string apply_dialect_transform(const std::string& ipa, const std::string& profile,
                                    const std::string& orthography = "");
std::vector<std::string> available_dialect_profiles();
double segment_distance(const std::string& a, const std::string& b);
std::vector<double> feature_vector(const std::string& segment);
std::vector<std::string> feature_names();
InventoryDistance inventory_distance(const LanguageSpec& a, const LanguageSpec& b);
GraphemeDivergence grapheme_divergence(const LanguageSpec& a, const LanguageSpec& b);
SpellingDivergence spelling_divergence(const LanguageSpec& a, const LanguageSpec& b);
double orthographic_distance(const LanguageSpec& a, const LanguageSpec& b);
double allophone_overlap(const LanguageSpec& a, const LanguageSpec& b);
/// distance.py tone_distance: Jaccard distance on the two specs'
/// tone_inventory KEY SETS. Two toneless languages are 0.0 apart; a toneless
/// and a tonal one are maximally 1.0 apart.
double tone_distance(const LanguageSpec& a, const LanguageSpec& b);
PhonologicalDistance phonological_distance(const LanguageSpec& a, const LanguageSpec& b);
double full_distance(const LanguageSpec& a, const LanguageSpec& b, double w_phonological = .6, double w_ancestry = .4);
WeightedDistance weighted_full_distance(const LanguageSpec& a, const LanguageSpec& b,
                                        double w_inventory = .25, double w_grapheme = .20,
                                        double w_allophone = .15, double w_ancestry = .40,
                                        double w_temporal = 0.0, int reference_year = 2025);
double ancestry_similarity(const LanguageSpec& a, const LanguageSpec& b, int max_depth = 10,
                           bool temporal_decay = false, double decay_halflife = 1000.0);
std::optional<double> temporal_distance(const LanguageSpec& a, const LanguageSpec& b, int reference_year = 2025);
double positional_divergence(const LanguageSpec& a, const LanguageSpec& b);
double phoneme_coverage(const LanguageSpec& native, const LanguageSpec& target);
double geographic_distance(const LanguageSpec& a, const LanguageSpec& b, bool normalize = true);
std::vector<std::vector<double>> pairwise_distances(const std::vector<LanguageSpec>& specs,
                                                    const std::string& metric = "combined");

} // namespace orthography2ipa
