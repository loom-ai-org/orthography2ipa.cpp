// Direct port of orthography2ipa/features.py — the pure-data linguistic
// feature view for downstream ML / CRF G2P.
//
// `orthography2ipa` ships no trained weights, which makes it an ideal feature
// provider for a downstream sequence model: it already computes, per grapheme,
// the phonological-class predicates, the word-local neighbours and the ranked
// candidate lattice with `-log P` costs, plus a per-word confidence signal.
// This module packages that structure as a flat, JSON-able feature view.
// Everything here is a PURE READ — it never affects `G2P.transcribe`; the
// records are built from a word's lattice slots and grapheme contexts, both of
// which the caller already has.
//
// `WordFeatures`/`GraphemeFeatures` are pure value records: they hold no
// pointers into the lattice and no engine state. The reference's field list and
// defaults are reproduced exactly (see features.py GraphemeFeatures), including
// the `None` of `margin` (kept `None`, not `+inf`, so `as_dict` stays
// strict-JSON safe) and the zero-candidate slot shape.
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace orthography2ipa {
struct GraphemeContext;
namespace rescorer { struct SegmentSlot; }
} // namespace orthography2ipa

namespace orthography2ipa::features {

/// features.py `FeatureValue = object` — the scalar types `as_dict` produces.
/// Every value is directly `json.dumps`-safe and accepted as a
/// python-crfsuite / sklearn-crfsuite feature value, so the C++ record keeps
/// the five JSON types distinct (an `int` index is not a `float` one).
class FeatureValue {
public:
    enum class Kind { Null, Bool, Int, Number, String };

    FeatureValue() = default;

    static FeatureValue boolean(bool value);
    static FeatureValue integer(std::size_t value);
    static FeatureValue number(double value);
    static FeatureValue text(std::string value);

    Kind kind() const { return kind_; }
    bool is_null() const { return kind_ == Kind::Null; }
    /// Valid only for the matching kind; the record types guarantee that.
    bool as_bool() const { return bool_; }
    std::size_t as_int() const { return int_; }
    double as_number() const { return number_; }
    const std::string& as_string() const { return string_; }

    /// The `json.dumps` rendering of this one scalar (ensure_ascii=False).
    std::string to_json() const;

private:
    Kind kind_ = Kind::Null;
    bool bool_ = false;
    std::size_t int_ = 0;
    double number_ = 0.0;
    std::string string_;
};

/// features.py GraphemeFeatures: the feature record for a single grapheme —
/// one item of a CRF sequence.
struct GraphemeFeatures {
    /// The source grapheme (lower-cased, as tokenised).
    std::string grapheme;
    /// `(start, end)` NFC code-point offsets of the grapheme (same contract as
    /// SegmentSlot.span / GraphemeContext.span).
    std::pair<std::size_t, std::size_t> span{0, 0};
    /// Zero-based position of this grapheme within its word.
    std::size_t index = 0;
    /// Word-relative position: "initial", "medial" or "final".
    std::string position;
    /// The immediately preceding grapheme within the word; nullopt at the
    /// word's left edge (or when the slot matched no grapheme context).
    std::optional<std::string> prev;
    /// The immediately following grapheme within the word; nullopt at the
    /// word's right edge.
    std::optional<std::string> next;
    /// The grapheme two positions back within the word; nullopt likewise.
    std::optional<std::string> prev2;
    /// The grapheme two positions ahead within the word; nullopt likewise.
    std::optional<std::string> next2;
    /// Phonological-class predicates, delegated to the matched
    /// GraphemeContext (the single source of truth for vowels), and False when
    /// the slot matched no context at all.
    bool is_vowel = false;
    bool is_consonant = false;
    bool is_front = false;
    bool is_back = false;
    /// Ranked (ipa, cost) options for this slot, best (lowest cost) first — the
    /// same lattice a LatticeRescorer re-costs. `Candidate.score` is the
    /// reference's `Candidate.cost`.
    std::vector<std::pair<std::string, double>> candidates;
    /// IPA of the best candidate; "" for a slot with no candidates.
    std::string top1_ipa;
    /// Cost of the best candidate; 0.0 for a canonical slot or a slot with no
    /// candidates.
    double top1_cost = 0.0;
    /// Cost gap between top-1 and top-2 (`cost2 - cost1`); nullopt when the
    /// slot has fewer than two candidates — no rival means no ambiguity to
    /// measure, and `+inf` is not strict-JSON.
    std::optional<double> margin;
    /// Number of ranked candidates for this slot.
    std::size_t n_candidates = 0;
    /// Per-WORD confidence in [0, 1], denormalised onto every grapheme.
    double confidence = 0.0;
    /// The language's primary script (e.g. "Latin").
    std::string script;
    /// The resolved language code (e.g. "en-GB").
    std::string code;

    /// features.py GraphemeFeatures.as_dict: the flat, JSON-able,
    /// CRF-consumable feature dict for this grapheme. The nested ranked
    /// `candidates` lattice is intentionally absent (not a scalar feature);
    /// read it off the record. The span is emitted as `span_start`/`span_end`.
    /// Key order is the reference dict's insertion order.
    std::vector<std::pair<std::string, FeatureValue>> as_dict() const;
};

/// features.py WordFeatures: the feature sequence for one word — the unit a
/// CRF trains/predicts on.
struct WordFeatures {
    /// Orthographic surface form (as it appeared after normalization).
    std::string word;
    /// The resolved language code.
    std::string code;
    /// The language's primary script.
    std::string script;
    /// The word's per-word confidence in [0, 1].
    double confidence = 0.0;
    /// Per-grapheme feature records, in surface order.
    std::vector<GraphemeFeatures> graphemes;

    /// features.py WordFeatures.as_dicts: the CRF feature SEQUENCE for this
    /// word — `as_dict` per grapheme, in order.
    std::vector<std::vector<std::pair<std::string, FeatureValue>>> as_dicts() const;
};

/// features.py build_word_features: assemble a WordFeatures from a word's
/// lattice and contexts.
///
/// `slots` (the engine's `ipa_lattice`) and `contexts` (the reference's
/// `flat_contexts` over the same grapheme tokens) are aligned by character
/// span: both are built from the identical `grapheme_tokens(word)` in surface
/// order and share each grapheme's span. Looking each slot up by its start
/// offset stays correct even if a rescorer drops a slot. All phonological
/// predicates and neighbours are read off the matched GraphemeContext — no
/// vowel logic is recomputed.
WordFeatures build_word_features(const std::string& word,
                                 const std::vector<rescorer::SegmentSlot>& slots,
                                 const std::vector<GraphemeContext>& contexts,
                                 double confidence,
                                 const std::string& code,
                                 const std::string& script);

} // namespace orthography2ipa::features
