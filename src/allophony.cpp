#include "orthography2ipa/orthography2ipa.hpp"
#include "orthography2ipa/rescorer.hpp"

#include "orthography2ipa/vowels.hpp"
#include "unicode_util.hpp"

#include <algorithm>
#include <set>

namespace orthography2ipa::rescorer {
namespace {

using uni::to_utf32;
using uni::to_utf8;

// allophony.py _is_a_mark is phonetok.py's: True if *ch* is a combining
// mark, which rides the letter it is on.
bool is_a_mark(char32_t ch) {
    return uni::combining(ch) != 0 || uni::category(ch) == "Mn";
}

// First character of a UTF-8 string as a code point ("" -> 0).
char32_t head_char(const std::string& s) {
    const std::u32string units = to_utf32(s);
    return units.empty() ? U'\0' : units.front();
}

// allophony.py _NASALS: nasal consonants, by IPA. A vowel nasalises
// before one of these in coda; the class is decided by the neighbour's
// IPA, never by the language.
bool is_nasal_char(char32_t ch) {
    static const std::u32string nasals = U"mn\u0272\u014b\u0273\u0274";
    return nasals.find(ch) != std::u32string::npos;
}

// allophony.py _spells_nothing: True when *gctx* is a combining mark the
// spec maps to nothing. Only an unconditional single empty candidate
// counts, and the grapheme must be a combining mark (a mark rides on the
// letter it attaches to and cannot itself divide a syllable). A silent
// BASE letter is opaque here: letting it stand as transparent would walk
// the look-ahead across a syllable it does not belong to.
bool spells_nothing(const GraphemeContext& gctx) {
    const auto& ipa_vals = gctx.ipa();
    if (ipa_vals.size() != 1 || !ipa_vals[0].empty()) return false;
    const std::u32string grapheme = to_utf32(gctx.grapheme());
    if (grapheme.size() == 1) return is_a_mark(grapheme[0]);
    if (grapheme.empty()) return false;
    const bool head_ok = uni::is_alpha(grapheme[0]) || is_a_mark(grapheme[0]);
    if (!head_ok) return false;
    for (std::size_t i = 1; i < grapheme.size(); ++i)
        if (!is_a_mark(grapheme[i])) return false;
    return true;
}

// allophony.py _syllable_position: classify the grapheme's syllable
// position by a maximal-onset rule. A vowel grapheme is a "nucleus"; a
// consonant grapheme followed (word-locally) by a vowel is an "onset",
// otherwise a "coda". Only a combining MARK the spec reads as nothing is
// transparent here; a silent base letter is a real grapheme occupying a
// real slot and must not be skipped.
std::string syllable_position(const GraphemeContext& ctx) {
    if (ctx.is_vowel()) return "nucleus";
    const GraphemeContext* nxt = ctx.next();
    while (nxt != nullptr && spells_nothing(*nxt)) nxt = nxt->next();
    if (nxt != nullptr && nxt->is_vowel()) return "onset";
    return "coda";
}

// allophony.py _begins_consonant_cluster: whether *gctx* starts a
// consonant cluster, reading in *step* direction (+1 = following, -1 =
// preceding). Three ways to qualify: the neighbour realises a long
// consonant, it is one grapheme spelling several consonants, or the
// grapheme beyond it is also a consonant. Stated over phonological
// classes only — no language, script or code is consulted.
bool begins_consonant_cluster(const GraphemeContext& gctx, int step) {
    if (!gctx.is_consonant()) return false;
    const std::string ipa = gctx.ipa().empty() ? std::string{} : gctx.ipa().front();
    if (ipa.find("\xcb\x90") != std::string::npos) return true;  // ː
    std::size_t consonant_segments = 0;
    for (const auto& seg : segment_ipa(ipa))
        if (!vowels::is_ipa_vowel(head_char(seg))) ++consonant_segments;
    if (consonant_segments >= 2) return true;
    const GraphemeContext* beyond = gctx.at(step);
    return beyond != nullptr && beyond->is_consonant();
}

// allophony.py _neighbor_is: whether neighbour grapheme *gctx* matches the
// neighbour class *cls*. *step* is the direction the neighbour lies in
// (+1 = the grapheme after the anchor, -1 = the one before); only the
// direction-sensitive classes read it.
bool neighbor_is(const std::string& cls, const GraphemeContext* gctx, int step) {
    if (cls == "word_boundary") return gctx == nullptr;
    if (gctx == nullptr) return false;
    if (cls == "any") return true;
    if (cls == "vowel") return gctx->is_vowel();
    if (cls == "consonant") return gctx->is_consonant();
    if (cls == "consonant_cluster") return begins_consonant_cluster(*gctx, step);
    if (cls == "coda") return syllable_position(*gctx) == "coda";
    if (cls == "coda_nasal") {
        if (syllable_position(*gctx) != "coda") return false;
        const std::string ipa = gctx->ipa().empty() ? std::string{} : gctx->ipa().front();
        return !ipa.empty() && is_nasal_char(head_char(ipa));
    }
    if (cls == "front_vowel") return gctx->is_front();
    if (cls == "back_vowel") return gctx->is_back();
    if (cls == "palatal") return gctx->is_palatal();
    if (cls == "emphatic") return gctx->is_emphatic();
    return false;  // unreachable — AllophoneRule validates the vocabulary
}

// allophony.py _is_nucleus_segment: whether one segmented IPA symbol is a
// syllable nucleus. The base letter is read from the DECOMPOSED form,
// because a segment can arrive precomposed: a tone language writes its
// vowels with the accent baked in, so í is the single codepoint U+00ED
// and its first character is not the vowel letter i at all.
bool is_nucleus_segment(const std::string& seg) {
    const std::u32string base = uni::nfd(to_utf32(seg));
    if (base.empty()) return false;
    if (vowels::is_ipa_vowel(base[0])) return true;
    for (const char32_t m : to_utf32(vowels::SYLLABIC_MARKS))
        if (seg.find(to_utf8(std::u32string(1, m))) != std::string::npos) return true;
    return false;
}

// allophony.py _has_other_nucleus: whether some slot OTHER than this one
// carries a syllable nucleus — a vowel OR a syllabic consonant (Czech
// ⟨vlk⟩ [vl̩k] has no vowel at all). Read from the lattice slots' top
// candidates, like every other neighbour lookup.
bool has_other_nucleus(const RescoreContext& ctx) {
    for (std::size_t j = 0; j < ctx.slots->size(); ++j) {
        if (j == ctx.index) continue;
        const SegmentSlot& slot = (*ctx.slots)[j];
        if (slot.candidates.empty()) continue;
        const std::string& ipa = slot.top().ipa;
        if (ipa.empty()) continue;
        for (const auto& seg : segment_ipa(ipa)) {
            if (vowels::is_ipa_vowel(head_char(seg))) return true;
            for (const char32_t m : to_utf32(vowels::SYLLABIC_MARKS))
                if (seg.find(to_utf8(std::u32string(1, m))) != std::string::npos) return true;
        }
    }
    return false;
}

// allophony.py _has_later_nucleus: whether a syllable nucleus occurs LATER
// in the word than this slot — the final-syllable predicate.
bool has_later_nucleus(const RescoreContext& ctx) {
    for (std::size_t j = ctx.index + 1; j < ctx.slots->size(); ++j) {
        const SegmentSlot& slot = (*ctx.slots)[j];
        if (slot.candidates.empty()) continue;
        const std::string& ipa = slot.top().ipa;
        if (ipa.empty()) continue;
        for (const auto& seg : segment_ipa(ipa))
            if (is_nucleus_segment(seg)) return true;
    }
    return false;
}

// allophony.py _applied: what *ipa* becomes when *rule* fires — a rewrite
// or an insertion.
std::string applied(const AllophoneRule& rule, const std::string& ipa) {
    return rule.append.empty() ? rule.surface : ipa + rule.append;
}

// allophony.py _source_word: reconstruct the current word's source
// spelling from its slots (word-local on both paths).
std::string source_word(const RescoreContext& ctx) {
    std::string word;
    for (const auto& slot : *ctx.slots) word += slot.grapheme;
    return word;
}

std::string lowered(const std::string& text) {
    return to_utf8(uni::lower(to_utf32(text)));
}

bool in_list(const std::string& value, const std::vector<std::string>& list) {
    return std::find(list.begin(), list.end(), value) != list.end();
}

// allophony.py _is_geminate_aware: whether *rule* is conditioned on a
// geminate twin of *ipa* — a gemination process references the doubled
// phoneme in its own phoneme neighbourhood (Tamil's paired TA_GEM1/TA_GEM2).
// The twin is matched either as the bare phoneme or as that phoneme
// carrying a following vowel (the shape its own inherent vowel gives it).
bool is_geminate_aware(const AllophoneRule& rule, const std::string& ipa) {
    for (const auto* neighbours :
         {&rule.preceded_by_phoneme, &rule.followed_by_phoneme}) {
        for (const auto& n : *neighbours)
            if (n == ipa || n.rfind(ipa, 0) == 0) return true;
    }
    return false;
}

// allophony.py _is_heterosyllabic: whether the doubled pair around *gctx*
// spans a syllable boundary. *step* points at the twin. A doubled
// consonant divides into a coda plus an onset only when a nucleus stands
// on each side of the pair; word-finally, or against another consonant,
// the pair is one long coda that nothing may divide.
bool is_heterosyllabic(const GraphemeContext& gctx, int step) {
    const GraphemeContext* before = gctx.at(-step);
    const GraphemeContext* after = gctx.at(2 * step);
    return before != nullptr && before->is_vowel() &&
           after != nullptr && after->is_vowel();
}

// allophony.py _triggers_on: whether *rule*'s trigger is the adjacent
// grapheme in direction *step*. Only POSITIVE, one-step conditions count.
bool triggers_on(const AllophoneRule& rule, int step) {
    if (step > 0)
        return !rule.followed_by.empty() || !rule.followed_by_grapheme.empty() ||
               !rule.followed_by_phoneme.empty();
    return !rule.preceded_by.empty() || !rule.preceded_by_grapheme.empty() ||
           !rule.preceded_by_phoneme.empty();
}

// allophony.py _is_segmental: whether *rule* may fire on a segment INSIDE
// a multi-phoneme slot — only rules that declare phoneme-neighbour context
// qualify. A rule without one was written under whole-slot semantics.
bool is_segmental(const AllophoneRule& rule) {
    return !rule.preceded_by_phoneme.empty() || !rule.followed_by_phoneme.empty();
}

// allophony.py _matches_segment: the conditions that describe ONE SEGMENT
// of a multi-phoneme slot: word-edge flags and phoneme neighbours, read at
// segment granularity.
bool matches_segment(const AllophoneRule& rule, const SegmentContext& seg_ctx) {
    if (rule.word_initial.has_value() &&
        *rule.word_initial != seg_ctx.is_word_initial)
        return false;
    if (rule.word_final.has_value() && *rule.word_final != seg_ctx.is_word_final)
        return false;
    if (!rule.preceded_by_phoneme.empty() &&
        (seg_ctx.prev == nullptr ||
         !in_list(*seg_ctx.prev, rule.preceded_by_phoneme)))
        return false;
    if (!rule.followed_by_phoneme.empty() &&
        (seg_ctx.next == nullptr ||
         !in_list(*seg_ctx.next, rule.followed_by_phoneme)))
        return false;
    return true;
}

// allophony.py _segment_context: build one segment's SegmentContext within
// its word. Neighbours fall back across slot boundaries to the ADJACENT
// SLOT's top candidate (its last segment on the left, first on the right).
// The cross-slot fallback segments are materialized in *storage*, which the
// caller owns for as long as the SegmentContext is read.
struct OwnedSegmentContext {
    SegmentContext ctx;
    std::string prev_storage, next_storage;
};
OwnedSegmentContext segment_context(const std::vector<std::string>& segments,
                                    std::size_t index, const RescoreContext& ctx,
                                    const std::vector<std::string>& atoms) {
    OwnedSegmentContext out;
    SegmentContext& built = out.ctx;
    if (index > 0) {
        built.prev = &segments[index - 1];
    } else if (ctx.prev_slot() != nullptr) {
        const auto prev_segs = segment_ipa(ctx.prev_slot()->top().ipa, atoms);
        if (!prev_segs.empty()) {
            out.prev_storage = prev_segs.back();
            built.prev = &out.prev_storage;
        }
    }
    if (index < segments.size() - 1) {
        built.next = &segments[index + 1];
    } else if (ctx.next_slot() != nullptr) {
        const auto next_segs = segment_ipa(ctx.next_slot()->top().ipa, atoms);
        if (!next_segs.empty()) {
            out.next_storage = next_segs.front();
            built.next = &out.next_storage;
        }
    }
    built.is_word_initial = ctx.is_word_initial() && index == 0;
    built.is_word_final = ctx.is_word_final() && index == segments.size() - 1;
    return out;
}

// allophony.py _rule_atoms: every multi-character phoneme the rules
// mention, longest first — a rule about tɕ must see tɕ as one segment,
// never t + ɕ.
std::vector<std::string> rule_atoms(const std::vector<AllophoneRule>& rules) {
    std::set<std::string> atom_set;
    for (const auto& rule : rules) {
        for (const auto* lists : {&rule.phonemes, &rule.preceded_by_phoneme,
                                  &rule.followed_by_phoneme,
                                  &rule.preceded_by_phoneme_2,
                                  &rule.followed_by_phoneme_2,
                                  &rule.preceded_by_surface_phoneme_2})
            for (const auto& atom : *lists) atom_set.insert(atom);
        if (!rule.surface.empty()) atom_set.insert(rule.surface);
        if (!rule.append.empty()) atom_set.insert(rule.append);
    }
    std::vector<std::string> atoms;
    for (const auto& atom : atom_set)
        if (to_utf32(atom).size() > 1) atoms.push_back(atom);
    // Longest first; stable for equal lengths (Python sorts a set, so the
    // order of equal-length atoms is arbitrary there too).
    std::stable_sort(atoms.begin(), atoms.end(),
                     [](const std::string& a, const std::string& b) {
                         return to_utf32(a).size() > to_utf32(b).size();
                     });
    return atoms;
}

} // namespace

// ── AllophoneRescorer (allophony.py) ─────────────────────────────────────

AllophoneRescorer::AllophoneRescorer(std::vector<AllophoneRule> rules,
                                     bool doubled_letters_geminate)
    : rules_(std::move(rules)), atoms_(rule_atoms(rules_)),
      doubling_is_geminate_(doubled_letters_geminate) {}

std::vector<Candidate> AllophoneRescorer::rescore(
        const SegmentSlot& slot, const RescoreContext& context) const {
    std::vector<Candidate> new_cands;
    bool changed = false;
    for (const Candidate& cand : slot.candidates) {
        const std::optional<std::string> surface = realize(cand.ipa, context);
        if (surface.has_value() && *surface != cand.ipa) {
            new_cands.push_back({*surface, cand.score});
            changed = true;
        } else {
            new_cands.push_back(cand);
        }
    }
    if (!changed) new_cands = slot.candidates;
    // The neighbour-mutation pass: a marker grapheme that deletes itself
    // while mutating its neighbour (the mutates_neighbor rule pair).
    const std::optional<std::string> feature = neighbor_mutation(context);
    if (feature.has_value()) {
        bool mutated = false;
        std::vector<Candidate> mutated_cands;
        for (const Candidate& cand : new_cands) {
            if (!cand.ipa.empty() && cand.ipa.find(*feature) == std::string::npos) {
                mutated_cands.push_back({cand.ipa + *feature, cand.score});
                mutated = true;
            } else {
                mutated_cands.push_back(cand);
            }
        }
        if (mutated) new_cands = std::move(mutated_cands);
    }
    return new_cands;
}

std::optional<std::string> AllophoneRescorer::neighbor_mutation(
        const RescoreContext& context) const {
    // The IPA feature THIS slot gains from an adjacent marker grapheme
    // that deletes itself while mutating its neighbour. Evaluated from the
    // AFFECTED slot's own rescore() call by re-running the MARKER's
    // rule-matching logic against a reconstruction of the marker's own
    // context — safe and order-independent, because every rescorer sees
    // pre-pass slot state.
    const int steps[] = {1, -1};
    const char* wanted_sides[] = {"preceding", "following"};
    for (int k = 0; k < 2; ++k) {
        const int step = steps[k];
        const long long j = static_cast<long long>(context.index) + step;
        if (j < 0 || static_cast<std::size_t>(j) >= context.slots->size()) continue;
        const SegmentSlot& marker_slot = (*context.slots)[static_cast<std::size_t>(j)];
        const GraphemeContext* marker_g = context.grapheme->at(step);
        if (marker_g == nullptr || marker_slot.candidates.empty()) continue;
        RescoreContext marker_ctx;
        marker_ctx.slot = &marker_slot;
        marker_ctx.index = static_cast<std::size_t>(j);
        marker_ctx.slots = context.slots;
        marker_ctx.grapheme = marker_g;
        marker_ctx.syll_idx = context.syll_idx;
        marker_ctx.stressed_syll_idx = context.stressed_syll_idx;
        const std::string ipa = marker_slot.top().ipa;
        for (const auto& rule : rules_) {
            if (!rule.mutates_neighbor.empty() &&
                rule.mutates_neighbor_side == wanted_sides[k] &&
                in_list(ipa, rule.phonemes) && matches(rule, marker_ctx))
                return rule.mutates_neighbor;
        }
    }
    return std::nullopt;
}

std::optional<int> AllophoneRescorer::geminate_twin_side(
        const std::string& ipa, const RescoreContext& context) const {
    // Which side this slot's written-geminate twin lies on, or nullopt. A
    // geminate is a single long consonant the input spells as two
    // identical, contiguous, same-grapheme slots. The doubled phoneme must
    // be a BARE consonant — two adjacent identical CV units of an abugida
    // are two syllables, not a geminate.
    if (!doubling_is_geminate_) return std::nullopt;
    if (ipa.empty()) return std::nullopt;
    for (const auto& seg : segment_ipa(ipa))
        if (vowels::is_ipa_vowel(head_char(seg))) return std::nullopt;
    const std::string own_g = lowered(context.grapheme->grapheme());
    const int steps[] = {1, -1};
    for (const int step : steps) {
        const long long j = static_cast<long long>(context.index) + step;
        const bool in_range = j >= 0 &&
            static_cast<std::size_t>(j) < context.slots->size();
        const SegmentSlot* neighbour =
            in_range ? &(*context.slots)[static_cast<std::size_t>(j)] : nullptr;
        const GraphemeContext* g_neighbour = context.grapheme->at(step);
        if (neighbour == nullptr || g_neighbour == nullptr) continue;
        if (neighbour->candidates.empty() || neighbour->top().ipa != ipa) continue;
        if (own_g != lowered(g_neighbour->grapheme())) continue;
        const auto near = context.slot->span;
        const auto far = neighbour->span;
        if (near.second == far.first || far.second == near.first) return step;
    }
    return std::nullopt;
}

std::optional<std::string> AllophoneRescorer::realize(
        const std::string& ipa, const RescoreContext& context) const {
    // Two passes. The whole-candidate pass is the original semantics — a
    // single-phoneme slot equal to a rule target. The segment pass then
    // serves slots whose one candidate carries SEVERAL phonemes (a Hangul
    // syllable block, an abugida consonant with its inherent vowel).
    const std::optional<int> twin_side = geminate_twin_side(ipa, context);
    for (const auto& rule : rules_) {
        if (!in_list(ipa, rule.phonemes) || !matches(rule, context)) continue;
        if (twin_side.has_value() && !is_geminate_aware(rule, ipa) &&
            !(triggers_on(rule, *twin_side) &&
              is_heterosyllabic(*context.grapheme, *twin_side)))
            // Geminate atomicity: a rule fired by material OUTSIDE the
            // geminate must not rewrite a single half and split the unit.
            continue;
        return applied(rule, ipa);
    }
    const std::vector<std::string> segments = segment_ipa(ipa, atoms_);
    if (segments.size() <= 1) return std::nullopt;
    return realize_segments(segments, context);
}

std::optional<std::string> AllophoneRescorer::realize_segments(
        const std::vector<std::string>& segments,
        const RescoreContext& context) const {
    // Apply the first matching rule to each segment; nullopt = no-op.
    bool changed = false;
    std::string out;
    for (std::size_t i = 0; i < segments.size(); ++i) {
        const OwnedSegmentContext owned = segment_context(segments, i, context, atoms_);
        const SegmentContext& seg_ctx = owned.ctx;
        const std::optional<std::string> surface =
            realize_one_segment(segments[i], seg_ctx, context);
        if (surface.has_value() && *surface != segments[i]) {
            changed = true;
            out += *surface;
        } else {
            out += segments[i];
        }
    }
    if (!changed) return std::nullopt;
    return out;
}

std::optional<std::string> AllophoneRescorer::realize_one_segment(
        const std::string& seg, const SegmentContext& seg_ctx,
        const RescoreContext& context) const {
    for (const auto& rule : rules_) {
        if (is_segmental(rule) && in_list(seg, rule.phonemes) &&
            matches_segment(rule, seg_ctx) && matches_slot(rule, context))
            return applied(rule, seg);
    }
    return std::nullopt;
}

bool AllophoneRescorer::matches(const AllophoneRule& rule,
                                const RescoreContext& ctx) const {
    // Whole-candidate match: word flags + slot neighbours + slot flags.
    // Slot neighbours match on the ADJACENT boundary segment — the last
    // segment of the previous slot's top candidate and the first segment
    // of the next slot's — never the whole candidate string.
    if (rule.word_initial.has_value() &&
        *rule.word_initial != ctx.is_word_initial())
        return false;
    if (rule.word_final.has_value() && *rule.word_final != ctx.is_word_final())
        return false;
    if (!rule.preceded_by_phoneme.empty()) {
        const SegmentSlot* prev = ctx.prev_slot();
        if (prev == nullptr || prev->top().ipa.empty()) return false;
        const auto prev_segs = segment_ipa(prev->top().ipa, atoms_);
        if (prev_segs.empty()) return false;
        if (!in_list(prev_segs.back(), rule.preceded_by_phoneme)) return false;
    }
    if (!rule.followed_by_phoneme.empty()) {
        const SegmentSlot* next = ctx.next_slot();
        if (next == nullptr || next->top().ipa.empty()) return false;
        const auto next_segs = segment_ipa(next->top().ipa, atoms_);
        if (next_segs.empty()) return false;
        if (!in_list(next_segs.front(), rule.followed_by_phoneme)) return false;
    }
    return matches_slot(rule, ctx);
}

bool AllophoneRescorer::matches_slot(const AllophoneRule& rule,
                                     const RescoreContext& ctx) const {
    // The conditions that describe the SLOT, not one of its segments:
    // stress, syllable position, and grapheme-class neighbours.
    const GraphemeContext& g = *ctx.grapheme;
    if (!rule.stress.empty()) {
        const std::optional<bool> stressed = ctx.is_stressed();
        if (!stressed.has_value()) return false;  // no stress context
        if (rule.stress == "pretonic" || rule.stress == "posttonic") {
            if (*stressed) return false;
            if (!ctx.syll_idx.has_value() || !ctx.stressed_syll_idx.has_value())
                return false;
            const long long syll = static_cast<long long>(*ctx.syll_idx);
            if (rule.stress == "pretonic") {
                if (!(syll < *ctx.stressed_syll_idx)) return false;
            } else if (!(syll > *ctx.stressed_syll_idx)) {
                return false;
            }
        } else if ((rule.stress == "stressed") != *stressed) {
            return false;
        }
    }
    if (!rule.syllable_position.empty() &&
        syllable_position(g) != rule.syllable_position)
        return false;
    if (!rule.preceded_by.empty() &&
        !neighbor_is(rule.preceded_by, g.prev(), -1))
        return false;
    if (!rule.followed_by.empty() &&
        !neighbor_is(rule.followed_by, g.next(), 1))
        return false;
    if (!rule.preceded_by_grapheme.empty()) {
        const GraphemeContext* prv = g.prev();
        if (prv == nullptr || prv->grapheme().empty() ||
            !in_list(lowered(prv->grapheme()), rule.preceded_by_grapheme))
            return false;
    }
    if (!rule.followed_by_grapheme.empty()) {
        const GraphemeContext* nxt = g.next();
        if (nxt == nullptr || nxt->grapheme().empty() ||
            !in_list(lowered(nxt->grapheme()), rule.followed_by_grapheme))
            return false;
    }
    if (!rule.followed_by_grapheme_not.empty()) {
        const GraphemeContext* nxt = g.next();
        if (nxt != nullptr && !nxt->grapheme().empty() &&
            in_list(lowered(nxt->grapheme()), rule.followed_by_grapheme_not))
            return false;
    }
    if (!rule.preceded_by_phoneme_2.empty() ||
        !rule.followed_by_phoneme_2.empty()) {
        // The grapheme TWO away, by its first IPA candidate — read from the
        // grapheme layer, so it is the underlying phoneme (⟨с⟩ of ⟨гости⟩
        // palatalises because the ⟨т⟩ after it stands before a soft vowel,
        // knowable before any rule has rewritten the ⟨т⟩).
        const auto phoneme_at = [&](int step) -> std::optional<std::string> {
            const GraphemeContext* gg = g.at(step);
            if (gg == nullptr || gg->ipa().empty() || gg->ipa().front().empty())
                return std::nullopt;
            const auto segs = segment_ipa(gg->ipa().front(), atoms_);
            if (segs.empty()) return std::nullopt;
            return segs.front();
        };
        if (!rule.preceded_by_phoneme_2.empty()) {
            const auto p2 = phoneme_at(-2);
            if (!p2.has_value() || !in_list(*p2, rule.preceded_by_phoneme_2))
                return false;
        }
        if (!rule.followed_by_phoneme_2.empty()) {
            const auto n2 = phoneme_at(2);
            if (!n2.has_value() || !in_list(*n2, rule.followed_by_phoneme_2))
                return false;
        }
    }
    if (!rule.preceded_by_2.empty() &&
        !neighbor_is(rule.preceded_by_2, g.at(-2), -1))
        return false;
    if (!rule.followed_by_2.empty() &&
        !neighbor_is(rule.followed_by_2, g.at(2), 1))
        return false;
    if (!rule.preceded_by_3.empty() &&
        !neighbor_is(rule.preceded_by_3, g.at(-3), -1))
        return false;
    if (!rule.preceded_by_surface_phoneme_2.empty()) {
        if (ctx.index < 2) return false;
        const SegmentSlot& two_back = (*ctx.slots)[ctx.index - 2];
        if (two_back.candidates.empty() || two_back.top().ipa.empty()) return false;
        const auto segs = segment_ipa(two_back.top().ipa, atoms_);
        if (segs.empty()) return false;
        if (!in_list(segs.back(), rule.preceded_by_surface_phoneme_2)) return false;
    }
    if (!rule.graphemes.empty()) {
        if (g.grapheme().empty() ||
            !in_list(lowered(g.grapheme()), rule.graphemes))
            return false;
    }
    if (!rule.word_contains_grapheme.empty() || !rule.word_contains_grapheme_not.empty()) {
        const std::string word = lowered(source_word(ctx));
        bool any_found = false;
        for (const auto& grapheme : rule.word_contains_grapheme)
            if (word.find(lowered(grapheme)) != std::string::npos) { any_found = true; break; }
        if (!rule.word_contains_grapheme.empty() && !any_found) return false;
        for (const auto& grapheme : rule.word_contains_grapheme_not)
            if (word.find(lowered(grapheme)) != std::string::npos) return false;
    }
    if (!rule.word.empty()) {
        const std::string word = lowered(source_word(ctx));
        if (!in_list(word, rule.word)) return false;
    }
    if (rule.requires_other_nucleus.has_value() &&
        *rule.requires_other_nucleus != has_other_nucleus(ctx))
        return false;
    if (rule.followed_by_nucleus.has_value() &&
        *rule.followed_by_nucleus != has_later_nucleus(ctx))
        return false;
    return true;
}

std::unique_ptr<LatticeRescorer> compile_allophone_rescorer(
        const std::vector<AllophoneRule>& rules, bool doubled_letters_geminate) {
    // "No rules" compiles to nullptr: the caller treats it as "no
    // post-lexical pass", keeping the default engine path byte-identical.
    if (rules.empty()) return nullptr;
    return std::make_unique<AllophoneRescorer>(rules, doubled_letters_geminate);
}

} // namespace orthography2ipa::rescorer
