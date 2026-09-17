#pragma once

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <utility>

namespace orthography2ipa {

struct Candidate { std::string ipa; double score{}; };
struct IPAPath { std::string ipa; double score{}; std::vector<std::string> graphemes; std::vector<std::string> segments; };

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

struct SandhiRule {
    std::string id, name, left_context, right_context;
    std::optional<std::string> transform, right_transform;
    bool obligatory = true;
    std::string notes;
};

struct LanguageSpec {
    std::string code, name, family, script, parent, quality;
    bool clade = false;
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
    std::string secondary_stress;
    int default_stress_position = -2;
    std::string stress_mark = "ˈ";
    std::string script_type = "alphabet";
    std::string inherent_vowel, inherent_vowel_final, virama_final_vowel;
    bool coda_no_inherent_vowel = false;
    std::optional<double> latitude, longitude;

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

struct InventoryDistance {
    double jaccard{}, feature_mean{};
    std::size_t size_a{}, size_b{}, shared{};
};
struct GraphemeDivergence {
    std::size_t shared_graphemes{}, total_graphemes{};
    double mean_ipa_distance{}, overlap_ratio{};
};
struct PhonologicalDistance {
    InventoryDistance inventory;
    GraphemeDivergence grapheme;
    double allophone_sim{}, combined{};
};

class Tokenizer {
public:
    explicit Tokenizer(const LanguageSpec& spec);
    std::vector<std::string> tokenize_word(const std::string& word,
                                            std::vector<std::string>* unmapped = nullptr) const;
    std::vector<IPAPath> beam(const std::string& word, std::size_t width = 8) const;
private:
    const LanguageSpec& spec_;
    std::vector<std::string> keys_;
};

class G2P {
public:
    explicit G2P(std::string language, std::map<std::string, std::vector<std::string>> plugin_overrides = {},
                 std::string dialect_profile = "");
    const LanguageSpec& spec() const;
    std::string transcribe(const std::string& text, const std::string& search = "greedy",
                           std::size_t beam_width = 8) const;
    TranscriptionResult transcribe_detailed(const std::string& text,
                                            const std::string& search = "greedy",
                                            std::size_t beam_width = 8) const;
    std::vector<IPAPath> candidates(const std::string& word, std::size_t beam_width = 8) const;
    double word_confidence(const std::string& word, std::size_t beam_width = 8) const;
private:
    std::string language_;
    const LanguageSpec* spec_;
    std::map<std::string, std::vector<std::string>> plugin_overrides_;
    std::string dialect_profile_;
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
void set_data_directory(const std::string& path);

void register_lexicon(const std::string& code, const std::string& source);
void set_lexicon_directory(const std::string& path);
void clear_lexicons();
std::vector<std::string> available_lexicon_codes();
std::map<std::string, std::string> get_lexicon(const std::string& code);
std::optional<std::string> lexicon_source(const std::string& code);
std::optional<std::string> lexicon_path(const std::string& code);
std::vector<std::pair<std::size_t, std::string>> validate_lexicon(const std::string& text);

std::string transcribe(const std::string& text, const std::string& language);
std::string apply_dialect_transform(const std::string& ipa, const std::string& profile,
                                    const std::string& orthography = "");
std::vector<std::string> available_dialect_profiles();
double segment_distance(const std::string& a, const std::string& b);
InventoryDistance inventory_distance(const LanguageSpec& a, const LanguageSpec& b);
GraphemeDivergence grapheme_divergence(const LanguageSpec& a, const LanguageSpec& b);
double allophone_overlap(const LanguageSpec& a, const LanguageSpec& b);
PhonologicalDistance phonological_distance(const LanguageSpec& a, const LanguageSpec& b);
double ancestry_similarity(const LanguageSpec& a, const LanguageSpec& b);
double geographic_distance(const LanguageSpec& a, const LanguageSpec& b, bool normalize = true);

} // namespace orthography2ipa
