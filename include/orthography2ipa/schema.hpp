// Direct port of orthography2ipa/schema.py — the pydantic v2 validation models
// for language specs.
//
// This module is the reference's validation layer: it mirrors the JSON spec
// schema (``data/SCHEMA.md``) as C++ structs and checks a document against
// them WITHOUT building a runtime spec. The runtime loader stays in
// src/orthography2ipa.cpp; ``validate`` / the CLI use this surface to assert
// that every ``data/*.json`` file is structurally sound and free of unknown
// keys.
//
// Fidelity notes, because pydantic is the specification here:
//   * ``extra='forbid'`` on every model: an unknown key is an error, never a
//     silent ignore.
//   * ``use_enum_values=True``: an enum field holds the enum VALUE, so the
//     ported fields are plain strings validated against the reference's enum
//     members (see the ``*_VALUES`` tables in schema.cpp).
//   * Lax-mode coercion is reproduced, not approximated: a string "2" fills an
//     int field, "yes" fills a bool, 2.0 fills an int ONLY when it has no
//     fractional part, while a number never fills a str field.
//   * Error ORDER is part of the contract. pydantic-core reports the declared
//     fields of a model in declaration order (input order is irrelevant), then
//     the unknown keys in document order, then the model-level validators.
//     Nested items report in list-index / dict-key order, which is why the
//     ported maps keep the document's key order instead of sorting.
//   * Every error carries pydantic's ``loc``, ``msg`` and ``type``;
//     format_failure() renders them exactly as the reference does.
//
// Python's ``int`` is unbounded; the ported fields are ``long long``. A JSON
// integer literal beyond that range keeps its exact digits for messages and
// range checks, and saturates in the struct (no spec or documented shape
// depends on it).
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <stdexcept>
#include <utility>
#include <variant>
#include <vector>

namespace orthography2ipa {

struct LanguageSpec;                  // orthography2ipa.hpp — the runtime spec

namespace schema {

/// A JSON object field, in the order the document wrote its keys. pydantic
/// keeps a dict's insertion order and its validators iterate in that order, so
/// ``std::map`` (sorted) would reorder both the dump and the error list.
template <typename V>
class Dict {
public:
    using Entry = std::pair<std::string, V>;
    using const_iterator = typename std::vector<Entry>::const_iterator;

    Dict() = default;
    Dict(std::initializer_list<Entry> init) : entries_(init) {}

    const_iterator begin() const noexcept { return entries_.begin(); }
    const_iterator end() const noexcept { return entries_.end(); }
    const std::vector<Entry>& entries() const noexcept { return entries_; }

    std::size_t size() const noexcept { return entries_.size(); }
    bool empty() const noexcept { return entries_.empty(); }

    const V* find(const std::string& key) const noexcept {
        for (const auto& entry : entries_)
            if (entry.first == key) return &entry.second;
        return nullptr;
    }
    bool contains(const std::string& key) const noexcept { return find(key) != nullptr; }

    /// Python's ``d[key] = value``: a repeat key updates in place and keeps the
    /// position it was first written at.
    V& insert(const std::string& key) {
        for (auto& entry : entries_)
            if (entry.first == key) return entry.second;
        entries_.emplace_back(key, V());
        return entries_.back().second;
    }
    void reserve(std::size_t n) { entries_.reserve(n); }

private:
    std::vector<Entry> entries_;
};

// ─── schema.py type aliases ────────────────────────────────────────────────
/// schema.py IPACandidates = Optional[List[str]] — ``None`` is the inheritance
/// deletion marker; individual candidates may be empty strings.
using IPACandidates = std::optional<std::vector<std::string>>;

struct WeightedGraphemeModel;

/// schema.py GraphemeValue = Optional[Union[List[str], WeightedGraphemeModel]]
using GraphemeValue =
    std::optional<std::variant<std::vector<std::string>, WeightedGraphemeModel>>;

/// schema.py grammatical_endings value: Union[str, List[Optional[str]]] — a
/// string is one realisation, a list is the ranked candidate list whose
/// element 0 may be ``null`` to defer rank 1 to the grapheme tables.
using EndingValue = std::variant<std::string, std::vector<std::optional<std::string>>>;

// ─── models (field order == schema.py declaration order) ───────────────────
/// schema.py WeightedGraphemeModel
struct WeightedGraphemeModel {
    std::vector<std::string> ipa;
    std::vector<double> weights;
};

/// schema.py SourceModel — a bibliographic reference (``sources[]``).
struct SourceModel {
    std::string id;
    std::string author;
    long long year = 0;
    std::string title;
    std::optional<std::string> publisher;
    std::optional<std::string> url;
    std::optional<std::string> doi;
    std::optional<std::string> wikipedia_url;
    std::optional<std::string> pages;
    std::optional<std::string> notes;
};

/// schema.py AncestorModel — one ancestry link (``ancestors[]``).
struct AncestorModel {
    std::string code;
    std::string role;                                   // AncestorRole value
    double weight = 0.5;
    std::string notes;
};

/// schema.py SandhiRuleModel — a cross-word-boundary rule (``sandhi_rules[]``).
struct SandhiRuleModel {
    std::string id;
    std::string name;
    std::string left_context;
    std::string right_context;
    std::optional<std::string> transform;
    std::optional<std::string> right_transform;
    bool obligatory = true;
    std::string notes;
};

/// schema.py AllophoneRuleModel — a post-lexical rewrite (``allophone_rules[]``).
struct AllophoneRuleModel {
    std::string id;
    std::variant<std::string, std::vector<std::string>> phonemes;
    std::string surface;
    std::string append;
    std::optional<bool> word_initial;
    std::optional<bool> word_final;
    std::optional<std::string> stress;
    std::optional<std::string> syllable_position;
    std::optional<std::string> preceded_by;
    std::optional<std::string> followed_by;
    std::optional<std::string> preceded_by_2;
    std::optional<std::string> followed_by_2;
    std::optional<std::string> preceded_by_3;
    std::optional<std::vector<std::string>> preceded_by_phoneme_2;
    std::optional<std::vector<std::string>> followed_by_phoneme_2;
    std::optional<std::vector<std::string>> preceded_by_surface_phoneme_2;
    std::optional<bool> requires_other_nucleus;
    std::optional<bool> followed_by_nucleus;
    std::optional<std::vector<std::string>> preceded_by_phoneme;
    std::optional<std::vector<std::string>> followed_by_phoneme;
    std::optional<std::vector<std::string>> preceded_by_grapheme;
    std::optional<std::vector<std::string>> followed_by_grapheme;
    std::optional<std::vector<std::string>> followed_by_grapheme_not;
    std::optional<std::vector<std::string>> word_contains_grapheme;
    std::optional<std::vector<std::string>> word_contains_grapheme_not;
    std::optional<std::vector<std::string>> grapheme;
    std::optional<std::vector<std::string>> word;
    std::string notes;
    std::optional<std::string> mutates_neighbor;
    std::optional<std::string> mutates_neighbor_side;
};

/// schema.py OrthographyStandardModel
struct OrthographyStandardModel {
    std::string name;
    std::optional<std::string> authority;
    std::optional<long long> year;
    std::optional<std::string> url;
    std::optional<std::string> notes;
};

/// schema.py ValidCeilingModel — one entry of the dataset-keyed mapping.
struct ValidCeilingModel {
    double per = 0.0;
    std::string folded;
    std::string citation;
};

/// schema.py AuditRecordModel — one entry of ``audit``'s dataset-keyed mapping.
struct AuditRecordModel {
    std::string conclusion;                             // AuditConclusion value
    std::string measured;
    std::string reference;
};

/// schema.py LocationModel
struct LocationModel {
    double latitude = 0.0;
    double longitude = 0.0;
    std::optional<std::string> source;
    std::optional<std::string> notes;
};

/// schema.py TimeSpanModel
struct TimeSpanModel {
    long long start_year = 0;
    std::optional<long long> end_year;
};

/// schema.py ToneRulesModel — the computed-tone system.
struct ToneRulesModel {
    Dict<std::string> classes;
    Dict<std::string> marks;
    Dict<std::string> tones;
    Dict<Dict<Dict<std::string>>> table;
    std::vector<std::string> dead_codas;
    std::string no_mark = "none";
    std::optional<std::string> notes;
};

/// schema.py StressRulesModel — declarative stress placement.
struct StressRulesModel {
    long long default_position = -2;
    std::optional<std::vector<std::string>> final_stress_endings;
    std::optional<std::vector<std::string>> penult_stress_endings;
    std::optional<std::vector<std::string>> antepenult_stress_endings;
    std::optional<std::vector<std::string>> marked_vowels;
    std::string stress_mark = "\xCB\x88";               // U+02C8 ˈ
    std::string accent2_mark;
    std::optional<std::vector<std::string>> accent2_final_letters;
    std::optional<std::vector<std::string>> diphthongs;
    std::optional<std::vector<std::string>> vowel_letters;
    bool quantity_sensitive = false;
    bool superheavy_final_attracts = true;
    long long max_onset = 1;
    std::optional<std::vector<std::string>> onset_clusters;
    bool constrain_mark_onsets = true;
    std::optional<std::vector<std::string>> cliticless_words;
    bool coda_liquid_capture = false;
    std::string secondary_stress;                       // "" | "alternating"
    std::string compound_stress;                        // "" | "last"
    bool iambic_length = false;
    std::string source = "rules";                       // "rules" | "plugin"
    std::optional<std::string> notes;
};

/// schema.py LanguageSpecModel — one ``data/{code}.json`` file.
struct LanguageSpecModel {
    // ─── required ───────────────────────────────────────────────────
    std::string code;
    std::string name;
    std::string script;

    // ─── classification ─────────────────────────────────────────────
    std::optional<std::string> family;
    bool clade = false;

    // ─── core mappings (optional when a ``*_base`` key supplies them) ─
    std::optional<Dict<GraphemeValue>> graphemes;
    std::optional<Dict<IPACandidates>> allophones;
    std::optional<std::vector<std::string>> phonemes;
    std::string orthography_kind = "native";

    // ─── positional overrides ───────────────────────────────────────
    std::optional<Dict<std::optional<Dict<std::vector<std::string>>>>> positional_graphemes;

    // ─── inheritance keys ───────────────────────────────────────────
    std::optional<std::string> graphemes_base;
    std::optional<std::string> allophones_base;
    std::optional<std::string> positional_graphemes_base;
    std::optional<std::string> word_exceptions_base;
    std::optional<std::string> grammatical_endings_base;

    // ─── ancestry ───────────────────────────────────────────────────
    std::optional<std::string> parent;
    std::optional<std::vector<AncestorModel>> ancestors;

    // ─── metadata ───────────────────────────────────────────────────
    std::optional<std::string> notes;
    std::string quality = "research";
    std::string script_type = "alphabet";
    std::optional<std::string> inherent_vowel;
    std::optional<std::vector<std::string>> optional_marks;
    std::optional<std::vector<std::string>> fold_diacritics;
    std::optional<std::vector<std::string>> vowel_graphemes;
    std::optional<std::vector<std::string>> dependent_vowels;
    std::optional<std::vector<std::string>> preposed_vowels;
    std::optional<std::vector<std::string>> trailing_vowel_axis_digraphs;
    std::optional<bool> coda_no_inherent_vowel;
    std::optional<std::string> inherent_vowel_final;
    std::optional<std::string> virama_final_vowel;
    std::optional<bool> collapse_geminates;
    std::optional<bool> doubled_letters_geminate;
    std::optional<bool> constrain_onsets;
    std::optional<bool> word_internal_apostrophe;
    std::optional<std::string> iso639_3;
    std::optional<std::string> glottolog_code;
    std::optional<std::string> wikidata_qid;
    std::optional<std::string> phoible_id;
    std::optional<std::string> wals_code;

    // ─── extended structures ────────────────────────────────────────
    std::optional<std::vector<SandhiRuleModel>> sandhi_rules;
    std::optional<std::vector<AllophoneRuleModel>> allophone_rules;
    long long allophone_passes = 1;
    std::optional<StressRulesModel> stress;
    std::optional<ToneRulesModel> tone_rules;
    std::optional<Dict<std::string>> tone_inventory;
    std::optional<bool> tone_marks_syllable_final;
    std::optional<std::vector<SourceModel>> sources;
    std::optional<std::vector<std::string>> wikipedia;
    std::optional<std::vector<std::string>> urls;
    std::optional<TimeSpanModel> timespan;
    std::optional<OrthographyStandardModel> orthography_standard;
    std::optional<LocationModel> location;
    std::optional<Dict<ValidCeilingModel>> valid_ceiling;
    std::optional<Dict<AuditRecordModel>> audit;

    // ─── whole-word overrides for a closed irregular set ────────────
    std::optional<Dict<std::string>> word_exceptions;

    // ─── suffix morphology: ending → IPA at the effective word end ──
    std::optional<Dict<EndingValue>> grammatical_endings;
};

// ─── the pydantic error surface ────────────────────────────────────────────
/// One element of a pydantic ``loc`` path: a field name, a dict key, a list
/// index, a union-member tag (``list[str]``) or the ``[key]`` marker.
struct LocPart {
    std::string key;                                    // rendered form
    long long index = 0;
    bool is_index = false;

    LocPart() = default;
    LocPart(std::string k) : key(std::move(k)) {}       // NOLINT: implicit on purpose
    static LocPart indexed(long long i) { LocPart p; p.index = i; p.is_index = true; return p; }

    std::string text() const { return is_index ? std::to_string(index) : key; }
};

/// One ``pydantic.ValidationError.errors()`` entry.
struct ValidationMessage {
    std::vector<LocPart> loc;
    std::string msg;
    std::string type;
};

/// pydantic's ``ValidationError``: the collected messages, in pydantic-core's
/// report order. ``what()`` is the reference's own multi-line rendering.
class ValidationError : public std::runtime_error {
public:
    explicit ValidationError(std::vector<ValidationMessage> messages);

    /// pydantic's `ValidationError.errors()`.
    const std::vector<ValidationMessage>& messages() const noexcept { return messages_; }
    std::size_t error_count() const noexcept { return messages_.size(); }

    /// The reference's multi-line rendering, minus the input repr pydantic adds.
    static std::string render(const std::vector<ValidationMessage>& messages);
    std::size_t size() const noexcept { return messages_.size(); }

private:
    std::vector<ValidationMessage> messages_;
};

/// ``json.JSONDecodeError``: the message and the (line, column, char) position
/// of the fault, counted in CODE POINTS of the decoded text.
class JsonDecodeError : public std::runtime_error {
public:
    JsonDecodeError(std::string message, std::size_t pos, std::size_t line, std::size_t column);

    std::size_t pos() const noexcept { return pos_; }
    std::size_t line() const noexcept { return line_; }
    std::size_t column() const noexcept { return column_; }

private:
    std::size_t pos_;
    std::size_t line_;
    std::size_t column_;
};

/// ``UnicodeDecodeError`` — the reference does NOT catch it in validate_all(),
/// so neither does this port.
class UnicodeDecodeError : public std::runtime_error {
public:
    explicit UnicodeDecodeError(const std::string& message) : std::runtime_error(message) {}
};

/// ``OSError`` from opening the spec file (missing file, directory, no
/// permission). ``what()`` reads like Python's ``str(exc)``.
class SpecIoError : public std::runtime_error {
public:
    explicit SpecIoError(const std::string& message) : std::runtime_error(message) {}
};

/// ``json_loader``'s ``KeyError``.
class LanguageKeyError : public std::runtime_error {
public:
    explicit LanguageKeyError(const std::string& message) : std::runtime_error(message) {}
};

// ─── schema.py validation helpers ──────────────────────────────────────────
/// schema.py DATA_DIR — the directory the specs are read from. It follows the
/// registry's own search order (``ORTHOGRAPHY2IPA_DATA_DIR``, then
/// ``O2I_DEFAULT_DATA_DIR``, then ``O2I_INSTALL_DATA_DIR``) and can be pointed
/// elsewhere with set_data_directory().
const std::string& data_directory();
void set_data_directory(const std::string& path);

/// schema.py validate_spec_file(path) -> LanguageSpecModel.
/// Throws JsonDecodeError on malformed JSON, ValidationError on a schema
/// violation, SpecIoError on an unreadable file and UnicodeDecodeError when the
/// bytes are not UTF-8.
LanguageSpecModel validate_spec_file(const std::string& path);

/// The same two steps with the document in hand: ``json.load`` and
/// ``LanguageSpecModel.model_validate``. validate_spec_file is this function
/// behind a file read, and it is the entry the fuzz harness uses.
LanguageSpecModel validate_spec_json(const std::string& text);

/// schema.py iter_spec_files() — every ``data/*.json``, sorted by code.
std::vector<std::string> iter_spec_files();

/// schema.py validate_all() — ``(ok, failures)``. C++ cannot return a Python
/// ``Exception``, so a failure carries an ``std::exception_ptr``: pass it to
/// format_failure(const std::string&, const std::exception_ptr&).
struct ValidationResults {
    std::vector<std::string> ok;
    std::vector<std::pair<std::string, std::exception_ptr>> failures;
};
ValidationResults validate_all();
ValidationResults validate_all(const std::string& code);

/// schema.py format_failure(code, exc) — ``code.json: field.path — message
/// [type]`` lines, or the single ``code.json: <error>`` line for anything that
/// is not a validation error.
std::string format_failure(const std::string& code, const std::exception& exc);
std::string format_failure(const std::string& code, const std::exception_ptr& exc);

/// The mirror of ``LanguageSpecModel.model_dump_json()``: the validated model
/// written back out with its defaults, coercions and ``null`` markers, in
/// declaration order — the form the parity harness diffs against the
/// reference. Non-finite floats become ``null``, as pydantic's JSON serializer
/// does.
std::string dump_json(const LanguageSpecModel& model);

} // namespace schema

// ─── json_loader.py, the public surface ────────────────────────────────────
namespace json_loader {

/// json_loader.py available_json_codes() — sorted codes with a JSON data file,
/// clades included (the index is every file on disk, not the resolvable set).
std::vector<std::string> available_json_codes();

/// json_loader.py load_json_spec(code) — the fully resolved, cached spec.
/// Throws LanguageKeyError when no ``{code}.json`` is indexed. The returned
/// reference is the registry's cached spec and is invalidated by a later
/// orthography2ipa::set_data_directory().
const LanguageSpec& load_json_spec(const std::string& code);

/// json_loader.py load_all_json_specs() — the cache the reference returns. It
/// lives in the registry, so this hands back its entries by pointer rather than
/// copying the dict; a code that fails to load is skipped (the reference only
/// warns) and reported through ``failed`` when one is given.
std::vector<std::pair<std::string, const LanguageSpec*>> load_all_json_specs(
    std::vector<std::pair<std::string, std::string>>* failed = nullptr);

} // namespace json_loader

} // namespace orthography2ipa
