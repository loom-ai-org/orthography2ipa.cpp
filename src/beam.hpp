// Direct port of the shared candidate/beam machinery: weights.py
// (candidate_base_costs), positional.py (grapheme_positions — the
// syllable/stress subset available without the syllabifier port,
// positional_candidates, build_branches, resolve_branches) and the
// phonetok.py nasal-carrier guard (constrain_nasal_carriers,
// _expand_beam and its carrier helpers). Both the standalone tokenizer
// beam (PhonetokTokenizer::ipa_beam) and the engine beam share exactly
// this code, as the reference does.
#pragma once

#include "orthography2ipa/phonetok.hpp"

#include <cstddef>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace orthography2ipa::beam {

// weights.py WEIGHT_FLOOR: probability floor for a zero weight so
// -log(p) stays finite.
constexpr double WEIGHT_FLOOR = 1e-6;

/// One (ipa, cost) beam branch.
struct Branch {
    std::string ipa;
    double cost = 0.0;
};

/// One beam entry: segments so far (one per consumed slot) and the
/// cumulative additive cost.
struct Hypothesis {
    std::vector<std::string> segments;
    double score = 0.0;
};

// weights.py candidate_base_costs: rank cost [0.0, 1.0, 2.0, ...] when
// weights are absent or malformed (length mismatch, negative value, or
// zero sum — any of which falls back to rank, never crashes); else
// -log(weight / total) with the floor applied so a zero weight stays
// finite but strongly disfavoured.
std::vector<double> candidate_base_costs(
    const std::vector<std::string>& ipa,
    const std::optional<std::vector<double>>& weights,
    const std::string& grapheme = {});

// positional.py build_branches: each candidate becomes a branch at its
// base cost; a candidate with declared allophones further branches into
// its variants at +0.5 per rank beyond the first. Duplicate IPA strings
// collapse to their lowest cost; the result is sorted (cost, ipa).
std::vector<Branch> build_branches(
    const std::vector<std::string>& candidates,
    const std::optional<std::vector<double>>& weights,
    const std::map<std::string, std::vector<std::string>>* allophone_map,
    const std::string& grapheme);

// positional.py grapheme_positions: the ordered positions to try for
// the grapheme wrapped by *ctx*, most specific first. syll_idx /
// stressed_syll_idx add the stress-conditioned nucleus positions
// (nullopt = no stress context, the standalone tokenizer); the aperture
// positions arrive with the syllabification port.
std::vector<const char*> grapheme_positions(
    const GraphemeContext& ctx, const LanguageSpec* spec,
    std::optional<std::size_t> syll_idx = std::nullopt,
    std::optional<std::size_t> stressed_syll_idx = std::nullopt);

// positional.py positional_candidates: the first positional override
// matching *grapheme* over *positions*, or nullopt when the grapheme has
// no entry at all or none of *positions* is declared for it.
std::optional<std::vector<std::string>> positional_candidates(
    const LanguageSpec& spec, const std::string& grapheme,
    const std::vector<const char*>& positions);

// positional.py resolve_branches: the full per-grapheme branch
// resolution both beams share. A positional override fires its
// candidates first with the flat table's remaining candidates appended;
// when no override fires the flat table is used with its per-candidate
// weights (tokenizer.weights_for).
std::vector<Branch> resolve_branches(
    const LanguageSpec& spec, const GraphemeContext& ctx,
    const PhonetokTokenizer& tokenizer,
    const std::map<std::string, std::vector<std::string>>* allophone_map,
    std::optional<std::size_t> syll_idx = std::nullopt,
    std::optional<std::size_t> stressed_syll_idx = std::nullopt);

// phonetok.py constrain_nasal_carriers: a slot whose ONLY reading is the
// bare combining tilde needs a preceding oral vowel/glide carrier; the
// constraint is pushed back onto the preceding slots (walking past
// deletable slots) before any pruning, making it search-width
// independent. Mutates and returns *slot_branches*.
void constrain_nasal_carriers(
    std::vector<std::vector<Branch>>& slot_branches);

// phonetok.py PhonetokTokenizer._expand_beam: expand every beam entry
// with every branch, guard the lone nasal tilde against non-carriers,
// splice an accepted tilde into its carrier in IPA normal form, then
// stable-sort by score and prune to *width*.
std::vector<Hypothesis> expand_beam(std::vector<Hypothesis> beam,
                                    const std::vector<Branch>& branches,
                                    std::size_t width);

} // namespace orthography2ipa::beam
