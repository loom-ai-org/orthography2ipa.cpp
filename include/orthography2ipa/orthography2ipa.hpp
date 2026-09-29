#pragma once

#include "orthography2ipa/phonetok.hpp"

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <utility>
#include <array>

namespace orthography2ipa {

struct Candidate {
    std::string ipa;
    double score{};
    bool operator==(const Candidate& other) const { return ipa == other.ipa && score == other.score; }
};
// IPAPath is defined in orthography2ipa/phonetok.hpp (the shared beam
// path type both the tokenizer and the engine produce).

struct AllophoneRule {
    std::string id, surface, append, syllable_position, stress, preceded_by, followed_by;
    std::string preceded_by_2, followed_by_2, preceded_by_3;
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
    std::string virama_final_vowel;
    bool coda_no_inherent_vowel = false;
    bool collapse_geminates = false, doubled_letters_geminate = true, constrain_onsets = false;
    std::vector<std::string> fold_diacritics, vowel_graphemes, dependent_vowels, preposed_vowels, trailing_vowel_axis_digraphs;
    // Per-candidate weights for the weighted-object grapheme form
    // ({"ipa": [...], "weights": [...]}); own-only, sparse.
    std::map<std::string, std::vector<double>> grapheme_weights;
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
struct GraphemeFeature {
    std::string grapheme, position, previous, next;
    bool vowel = false, consonant = false;
    std::vector<std::string> candidates;
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
    explicit Tokenizer(const LanguageSpec& spec);
    std::vector<Token> tokenize(const std::string& text) const;
    std::vector<Token> grapheme_tokens(const std::string& text) const;
    TokenSequence tokenize_with_context(const std::string& text) const;
    std::vector<std::string> tokenize_word(const std::string& word,
                                            std::vector<std::string>* unmapped = nullptr) const;
    std::vector<IPAPath> beam(const std::string& word, std::size_t width = 8) const;
private:
    const LanguageSpec& spec_;
    PhonetokTokenizer tokenizer_;
    // g2p.py engine-side derivations: whether any positional entry keys on
    // syllable aperture (the engine's _uses_aperture), and the graphemes
    // the spec declares as stress marks that emit nothing (the engine's
    // _silent_stress_marks).
    bool uses_aperture_ = false;
    std::string silent_stress_marks_;
    // g2p.py G2P._rescorers: the spec's compiled allophone rescorer
    // (allophony.py compile_allophone_rescorer), repeated allophone_passes
    // times so a rule that only fires on another rule's output can feed off
    // it. Null when the spec declares no rules — the chain is empty and the
    // default path is byte-identical.
    std::unique_ptr<rescorer::LatticeRescorer> allophone_rescorer_;
    std::vector<const rescorer::LatticeRescorer*> rescorers_;
};

class G2P {
public:
    explicit G2P(std::string language, std::map<std::string, std::vector<std::string>> plugin_overrides = {},
                 std::string dialect_profile = "");
    const LanguageSpec& spec() const;
    const std::map<std::string, std::vector<std::string>>& plugin_overrides() const;
    std::string transcribe(const std::string& text, const std::string& search = "greedy",
                           std::size_t beam_width = 8) const;
    TranscriptionResult transcribe_detailed(const std::string& text,
                                            const std::string& search = "greedy",
                                            std::size_t beam_width = 8) const;
    /// g2p.py transcribe_word: transcribe a single *word* through the
    /// per-word pipeline (beam, grammatical endings, computed tone,
    /// geminate collapse, word-final virama, tone-mark docking, stress).
    std::string transcribe_word(const std::string& word,
                                const std::string& search = "greedy",
                                std::size_t beam_width = 8) const;
    std::vector<IPAPath> candidates(const std::string& word, std::size_t beam_width = 8) const;
    std::vector<IPAPath> lattice(const std::string& word, std::size_t beam_width = 8) const;
    std::vector<GraphemeFeature> features(const std::string& word) const;
    double word_confidence(const std::string& word, std::size_t beam_width = 8) const;
private:
    std::string language_;
    const LanguageSpec* spec_;
    std::map<std::string, std::vector<std::string>> plugin_overrides_;
    std::string dialect_profile_;
    // g2p.py _transcribe_word: the per-word stage of the pipeline (word
    // overrides, grammatical endings, rescorer plugins, the word-final
    // ordering), shared by transcribe_detailed and transcribe_word.
    WordTranscription transcribe_one(const std::string& word, std::size_t width,
                                     bool forced, const std::string& forced_ipa) const;
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
class RescorerPlugin {
public:
    virtual ~RescorerPlugin() = default;
    virtual std::vector<std::string> language_codes() const = 0;
    virtual std::vector<IPAPath> rescore(const std::string& word, const std::string& lang,
                                         const std::vector<IPAPath>& paths) const = 0;
    virtual int priority() const { return 50; }
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
const StressPlugin* get_stress_plugin(const std::string& code);
const SyllabifierPlugin* get_syllabifier(const std::string& code);

void register_normalize_plugin(const std::string& name, std::shared_ptr<NormalizePlugin> plugin);
void register_syllabifier_plugin(const std::string& name, std::shared_ptr<SyllabifierPlugin> plugin);
void register_stress_plugin(const std::string& name, std::shared_ptr<StressPlugin> plugin);
void register_rescorer_plugin(const std::string& name, std::shared_ptr<RescorerPlugin> plugin);
void register_sandhi_plugin(const std::string& name, std::shared_ptr<SandhiPlugin> plugin);
void discover_plugins(const std::string& directory = "");

std::string resolve(const std::string& code);
const LanguageSpec& get(const std::string& code);
std::vector<std::string> available_codes(bool include_clades = false);
std::map<std::string, std::vector<std::string>> available_families();
std::map<std::string, std::vector<PluginAnswer>> who_answers(const std::string& code);
std::vector<std::string> validate(const std::string& code);
void set_data_directory(const std::string& path);

void register_lexicon(const std::string& code, const std::string& source);
void set_lexicon_directory(const std::string& path);
void clear_lexicons();
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
