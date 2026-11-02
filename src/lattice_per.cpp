// Direct port of orthography2ipa/lattice_per.py — the oracle (pronunciation-
// fair) edit distance of a hypothesis phone string against the pronunciation
// lattice of a reference text, plus its normalizations. See
// include/orthography2ipa/lattice_per.hpp for the naming choice: the function
// keeps the reference name inside namespace `per`, because a `lattice_per`
// namespace would only be callable through its doubled form
// (orthography2ipa::lattice_per::lattice_per) — see the header.
//
// Positions, segmentation and the stripped-mark table are Unicode CODE POINTS:
// Python indexes a str by code point, and segment_ipa groups a base character
// with its combining marks. Nothing here touches a byte-level regex.
#include "orthography2ipa/lattice_per.hpp"

#include "orthography2ipa/feats.hpp"
#include "orthography2ipa/orthography2ipa.hpp"
#include "unicode_util.hpp"

#include <algorithm>
#include <cstddef>
#include <numeric>
#include <string>
#include <utility>
#include <vector>

namespace orthography2ipa::per {
namespace {

using uni::to_utf32;
using uni::to_utf8;

/// lattice_per.py _STRIPPED: marks stripped before scoring, because stress and
/// syllable boundaries are suprasegmental and ASR phone output rarely carries
/// them consistently. ˈ ˌ . ‿ | ‖ as code points. The reference loops over the
/// set and does a `str.replace(mark, "")` per mark; each of the six is one
/// distinct code point, so deleting every occurrence of each is the same
/// operation in the same order.
const std::u32string STRIPPED = {0x02C8, 0x02CC, 0x002E, 0x203F, 0x007C, 0x2016};

/// lattice_per.py _DIACRITIC_FLIP: the featural cost of a diacritic-only
/// difference between two segments whose base characters match — priced as a
/// single binary feature flip.
constexpr double DIACRITIC_FLIP = 1.0 / 16.0;

/// Python `str.isspace()` as `str.split()` (no separator) applies it: the ASCII
/// blanks and controls, NEL, the file/group separators, and the Unicode
/// separator classes Zs / Zl / Zp.
bool is_space(char32_t cp) {
    if (cp == 0x0020 || cp == 0x0085) return true;
    if (cp >= 0x0009 && cp <= 0x000D) return true;      // \t \n \v \f \r
    if (cp >= 0x001C && cp <= 0x001F) return true;      // FS GS RS US
    const std::string cat = uni::category(cp);
    return cat == "Zs" || cat == "Zl" || cat == "Zp";
}

/// lattice_per.py _segments: the phoneme-sized segments of *ipa*, NFC, with the
/// stress/boundary marks and whitespace removed. `[s for word in ipa.split()
/// for s in segment_ipa(word)]`, so each whitespace-delimited run is segmented
/// on its own and the word boundaries never produce a segment of their own.
std::vector<std::string> segments(const std::string& ipa) {
    std::u32string text = uni::nfc(to_utf32(ipa));
    for (const char32_t mark : STRIPPED)
        text.erase(std::remove(text.begin(), text.end(), mark), text.end());

    std::vector<std::string> out;
    std::u32string word;
    const auto flush = [&word, &out] {
        if (word.empty()) return;                        // split() drops empty runs
        for (const auto& segment : segment_ipa(to_utf8(word))) out.push_back(segment);
        word.clear();
    };
    for (const char32_t cp : text) {
        if (is_space(cp)) { flush(); continue; }
        word.push_back(cp);
    }
    flush();
    return out;
}

/// The cost of substituting segment *a* for segment *b* — the reference passes
/// one of its two cost functions down the frontier, unbound.
using SubCost = double (*)(const std::string& a, const std::string& b);

/// lattice_per.py _sub_cost_unit.
double sub_cost_unit(const std::string& a, const std::string& b) {
    return a == b ? 0.0 : 1.0;
}

/// lattice_per.py _strip_marks: the segment's base characters — NFD, with every
/// canonical-combining-class-nonzero code point dropped (modifier letters such
/// as ʰ or ː have class 0 and stay).
std::string strip_marks(const std::string& segment) {
    const std::u32string decomposed = uni::nfd(to_utf32(segment));
    std::u32string base;
    for (const char32_t cp : decomposed)
        if (uni::combining(cp) == 0) base.push_back(cp);
    return to_utf8(base);
}

/// lattice_per.py _sub_cost_featural.
double sub_cost_featural(const std::string& a, const std::string& b) {
    if (a == b) return 0.0;
    double d = feats::phonetic_distance(a, b);
    if (d > 1.0) {
        // phonetic_distance exceeds 1 when a segment is outside its feature
        // table (e.g. the laminal/apical sibilant diacritics). Fall back to the
        // base characters and charge the diacritic difference as one binary
        // feature flip — s̻/s is then a near-match, not a full substitution. A
        // substitution must never cost more than a deletion plus an insertion,
        // so the result is clamped to unit either way.
        const std::string base_a = strip_marks(a);
        const std::string base_b = strip_marks(b);
        d = base_a == base_b ? DIACRITIC_FLIP
                             : feats::phonetic_distance(base_a, base_b) + DIACRITIC_FLIP;
    }
    return std::min(1.0, d);
}

/// One row of the Levenshtein DP over the hypothesis, as the reference spells
/// it: `frontier[j]` is the cost of aligning the segments read so far against
/// `hyp[:j]`. Every row has exactly len(hyp) + 1 states.
using Frontier = std::vector<double>;

/// `[float(j) for j in range(len(hyp) + 1)]` — the row before any segment.
Frontier initial_frontier(std::size_t hyp_size) {
    Frontier frontier(hyp_size + 1);
    std::iota(frontier.begin(), frontier.end(), 0.0);
    return frontier;
}

/// lattice_per.py _advance: one Levenshtein row, extending every frontier state
/// by *segment*.
Frontier advance(const Frontier& frontier, const std::string& segment,
                 const std::vector<std::string>& hyp, SubCost sub_cost) {
    Frontier out;
    out.reserve(hyp.size() + 1);
    out.push_back(frontier[0] + 1.0);
    for (std::size_t j = 1; j <= hyp.size(); ++j) {
        out.push_back(std::min({frontier[j] + 1.0,                     // delete segment
                                out[j - 1] + 1.0,                      // insert hyp[j-1]
                                frontier[j - 1] + sub_cost(segment, hyp[j - 1])}));
    }
    return out;
}

/// One lattice slot's candidates, each candidate pre-segmented.
using SlotCandidates = std::vector<std::vector<std::string>>;

/// lattice_per.py _oracle_frontier: the min edit distance from any concatenation
/// of one candidate per slot (each candidate pre-segmented) to *hyp*. A slot
/// with no candidates contributes nothing — the reference's `if best is not
/// None` leaves the frontier as it was, which is what a rescorer-deleted (or
/// otherwise empty) slot means.
double oracle_frontier(const std::vector<SlotCandidates>& slot_candidates,
                       const std::vector<std::string>& hyp, SubCost sub_cost) {
    Frontier frontier = initial_frontier(hyp.size());
    for (const auto& candidates : slot_candidates) {
        Frontier best;
        for (const auto& candidate_segments : candidates) {
            Frontier row = frontier;
            for (const auto& segment : candidate_segments)
                row = advance(row, segment, hyp, sub_cost);
            if (best.empty()) {
                best = std::move(row);
                continue;
            }
            for (std::size_t k = 0; k < best.size(); ++k)
                best[k] = std::min(best[k], row[k]);
        }
        if (!best.empty()) frontier = std::move(best);
    }
    return frontier.back();
}

} // namespace

bool LatticePER::operator==(const LatticePER& other) const {
    return distance == other.distance && per == other.per &&
           top_distance == other.top_distance && top_per == other.top_per &&
           ref_segments == other.ref_segments && hyp_segments == other.hyp_segments &&
           lang == other.lang;
}

LatticePER lattice_per(const std::string& hyp_ipa, const std::string& ref_text,
                       const std::string& lang, bool weighted, std::size_t beam_width) {
    G2P g2p(lang);
    const std::string top = g2p.transcribe(ref_text);
    const std::vector<std::string> top_segments = segments(top);
    const std::vector<std::string> hyp = segments(hyp_ipa);
    const SubCost sub_cost = weighted ? &sub_cost_featural : &sub_cost_unit;

    // Reading 2: the final engine output (sandhi, sentence rescorers).
    Frontier f = initial_frontier(hyp.size());
    for (const auto& segment : top_segments) f = advance(f, segment, hyp, sub_cost);
    const double top_distance = f.back();

    // Reading 1: every path through the pre-sandhi sentence lattice.
    const sentence::SentenceLattice lattice =
        g2p.sentence_lattice(ref_text, "greedy", beam_width);
    std::vector<SlotCandidates> slot_candidates;
    for (const auto& word : lattice) {
        if (word.slots.empty()) {
            // Lexicon override / unmapped word: its single reading is the
            // word's flattened IPA.
            slot_candidates.push_back(SlotCandidates{segments(word.ipa)});
            continue;
        }
        for (const auto& slot : word.slots) {
            SlotCandidates candidates;
            candidates.reserve(slot.candidates.size());
            for (const auto& candidate : slot.candidates)
                candidates.push_back(segments(candidate.ipa));
            slot_candidates.push_back(std::move(candidates));
        }
    }
    const double lattice_distance = oracle_frontier(slot_candidates, hyp, sub_cost);

    const double distance = std::min(lattice_distance, top_distance);
    const double denom = static_cast<double>(std::max<std::size_t>(top_segments.size(), 1));
    LatticePER result;
    result.distance = distance;
    result.per = distance / denom;
    result.top_distance = top_distance;
    result.top_per = top_distance / denom;
    result.ref_segments = top_segments.size();
    result.hyp_segments = hyp.size();
    result.lang = resolve(lang);        // g2p.py `self.lang = resolve(lang)`
    return result;
}

} // namespace orthography2ipa::per
