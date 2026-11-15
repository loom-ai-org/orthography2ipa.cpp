// Direct port of orthography2ipa/lattice_per.py — pronunciation-fair phoneme
// error rate against the pronunciation lattice.
//
// Scoring a hypothesis against a SINGLE reference transcription punishes the
// model for every legitimate pronunciation variant the reference happens not to
// use (optional sandhi fired or not, dialectal allophone choices, cliticization
// variants). The candidate lattice already encodes the set of pronunciations a
// speaker of the lect could validly have produced for the reference text, so
// the fair score is the ORACLE edit distance: the minimum, over all lattice
// paths, of the segment-level edit distance to the hypothesis. A hypothesis is
// only charged for segments that are wrong on EVERY valid reading.
//
// Two readings are always admissible, and the oracle distance is the minimum
// over both, computed without path enumeration: a Levenshtein frontier is
// pushed through the lattice slot by slot (each slot contributes the
// elementwise-minimum frontier over its candidates), which is the edit-distance
// transducer composed with the acyclic lattice; and the frontier is pushed
// through the engine's final reading (G2P::transcribe, i.e. with cross-word
// sandhi and the sentence rescorers that the raw lattice deliberately does not
// apply).
//
// Costs are unit by default. With weighted=true a substitution costs the
// articulatory feature distance of the two segments
// (orthography2ipa::feats::phonetic_distance, in [0, 1]) instead of 1, so a
// laminal/apical sibilant confusion costs a fraction of a stop/vowel confusion;
// insertions and deletions stay at 1.
//
// Normalization: per = oracle distance / segment count of the engine's FINAL
// reading of the reference. The oracle PATH length is not tracked (different
// paths have different lengths); the final reading is the single deterministic
// denominator. top_per is what a naive single-reference PER would have said and
// variant_credit() is the difference — the error mass some valid variant
// explains.
//
// Honesty caveat inherited from the reference: the metric is only as good as
// the lect spec it scores against. Report the lect and the engine version next
// to any number.
//
// NAMING. The reference module is lattice_per.py and its one public function is
// ALSO called lattice_per. The namespace here is orthography2ipa::per, and the
// function keeps the reference name and spelling:
// orthography2ipa::per::lattice_per(hyp_ipa, ref_text, lang). A namespace named
// lattice_per would work only in its fully doubled form
// (orthography2ipa::lattice_per::lattice_per): the moment the name resolves to
// the namespace — `using namespace orthography2ipa;` and a plain call, or the
// orthography2ipa::lattice_per(...) shape every other module of this library
// calls itself with — g++ rejects it ("expected primary-expression before '('
// token": a namespace is not a callable). `per` keeps the reference name on the
// function and the call site natural. The dataclass keeps its name as
// orthography2ipa::per::LatticePER. Everything else the module defines is
// underscore-private in the reference (_STRIPPED, _segments, _sub_cost_unit,
// _DIACRITIC_FLIP, _strip_marks, _sub_cost_featural, _advance, _oracle_frontier)
// and stays file-local in src/lattice_per.cpp.
//
// Segmentation and mark stripping run over Unicode CODE POINTS (the reference
// indexes Python strs); the IPA splitting itself is the already-ported
// allophony.py segment_ipa. All strings are UTF-8 at this boundary.
//
// As with every other header in this project, include this one at GLOBAL scope:
// a `#include` from inside a reopened `namespace orthography2ipa { ... }` nests
// these declarations one level deeper, at
// orthography2ipa::orthography2ipa::per (measured: the same happens to
// sandhi.hpp, and for the standard-library headers it is a hard error).
#pragma once

#include <cstddef>
#include <string>

namespace orthography2ipa::per {

/// lattice_per.py LatticePER: the frozen dataclass returned by lattice_per().
/// Field names, field order and meaning are the reference's; the reference's
/// `variant_credit` is a property over the stored fields, so it is a method
/// here and is NOT one of the fields Python's dataclass compares.
struct LatticePER {
    /// Oracle edit distance: min over the admissible readings (all lattice
    /// paths, plus the engine's final sandhi-applied reading).
    double distance = 0.0;
    /// `distance / ref_segments` — the headline phoneme error rate.
    double per = 0.0;
    /// Edit distance against the engine's final reading only — what a
    /// single-reference PER would have scored.
    double top_distance = 0.0;
    /// `top_distance / ref_segments`.
    double top_per = 0.0;
    /// Segment count of the engine's final reading of the reference text (the
    /// denominator; the rate itself divides by max(ref_segments, 1)).
    std::size_t ref_segments = 0;
    /// Segment count of the hypothesis after mark stripping.
    std::size_t hyp_segments = 0;
    /// The lect the reference was scored under — g2p.lang, i.e. the RESOLVED
    /// canonical code, not the caller's spelling.
    std::string lang;

    /// LatticePER.variant_credit: `top_per - per` — error mass the
    /// single-reference score would have charged that some valid pronunciation
    /// variant explains.
    double variant_credit() const { return top_per - per; }

    /// dataclass(frozen=True) equality over the stored fields.
    bool operator==(const LatticePER& other) const;
};

/// lattice_per.py lattice_per: the pronunciation-fair phoneme error rate of
/// *hyp_ipa* against the pronunciations the *lang* lect admits for *ref_text*.
///
/// *hyp_ipa* is a hypothesis phone string (IPA; whitespace and stress marks are
/// ignored) and *ref_text* is reference ORTHOGRAPHIC text, whose lattice is
/// built with the *lang* lect spec. `weighted` mirrors the reference keyword:
/// substitutions then cost articulatory feature distances in [0, 1] instead of
/// 1 (insertions and deletions stay 1). `beam_width` is the reference's keyword
/// of the same name and is passed straight to G2P::sentence_lattice, which the
/// reference calls with its default `search="greedy"` — so `G2P._width()`
/// collapses the width to 1 and the number is inert. It is threaded through
/// exactly as the Python does it, not "fixed"; a caller who wants a real beam
/// has to reach G2P::sentence_lattice with search="beam" directly.
///
/// Throws what the engine throws for an unusable lect or an unmappable
/// reference (the reference lets the same exceptions escape).
LatticePER lattice_per(const std::string& hyp_ipa, const std::string& ref_text,
                       const std::string& lang, bool weighted = false,
                       std::size_t beam_width = 8);

} // namespace orthography2ipa::per
