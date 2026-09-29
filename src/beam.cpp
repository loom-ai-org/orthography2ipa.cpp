#include "beam.hpp"

#include "orthography2ipa/orthography2ipa.hpp"
#include "unicode_util.hpp"
#include "orthography2ipa/vowels.hpp"

#include <algorithm>
#include <cmath>

namespace orthography2ipa::beam {

namespace {

using uni::to_utf32;
using uni::to_utf8;

// phonetok.py _NASAL_TILDE: the bare combining tilde a coda nasal slot
// emits for a nasalised-vowel reading.
constexpr char32_t NASAL_TILDE = U'\u0303';

// phonetok.py _NASAL_CARRIERS: oral IPA vowels (deliberately excluding
// the precomposed nasal vowels ã ẽ ĩ õ ũ and every combining mark, so a
// second tilde never stacks) plus the glides that legitimately carry a
// nasal offglide (nasal diphthongs from ⟨ão ãe õe⟩).
const std::u32string& nasal_carriers() {
    static const std::u32string set =
        U"\u0061\u0065\u0069\u006f\u0075"                       // aeiou
        U"\u025b\u0254\u0259\u0268\u0289\u026f\u00e6\u0250\u028c\u0252\u0153\u00f8\u026a\u028a\u0264\u0275\u025e\u0251\u0258\u025a\u025c\u025d\u0276" // IPA vowels
        U"\u0079"                                               // y
        U"\u0077\u006a\u0265\u0270";                            // wjɥɰ
    return set;
}

// phonetok.py _LENGTH_MARKS
bool is_length_mark(char32_t cp) { return cp == U'\u02d0' || cp == U'\u02d1'; }

// phonetok.py _carrier_split_index: index right after seg's base
// vowel/glide character, walking back over trailing length marks and any
// other non-tilde combining diacritic.
std::size_t carrier_split_index(const std::u32string& seg) {
    std::size_t j = seg.size();
    while (j > 0 &&
           (is_length_mark(seg[j - 1]) ||
            (seg[j - 1] != NASAL_TILDE && uni::combining(seg[j - 1]))))
        --j;
    return j;
}

// phonetok.py _carrier_tail: the character a following nasal tilde would
// actually attach to ("" for an empty or all-diacritic segment).
std::u32string carrier_tail(const std::u32string& seg) {
    const std::size_t j = carrier_split_index(seg);
    return j > 0 ? std::u32string(1, seg[j - 1]) : std::u32string{};
}

// phonetok.py _splice_nasal_tilde: insert the tilde into the last
// non-empty segment right after its base vowel/glide (IPA normal form:
// ũː, never uː̃) and append an empty placeholder so segments stay one
// entry per grapheme token.
std::vector<std::string> splice_nasal_tilde(std::vector<std::string> segs) {
    for (std::size_t i = segs.size(); i-- > 0;) {
        if (segs[i].empty()) continue;
        const std::u32string seg = uni::to_utf32(segs[i]);
        const std::size_t j = carrier_split_index(seg);
        std::u32string out = seg;
        out.insert(j, 1, NASAL_TILDE);
        segs[i] = uni::to_utf8(out);
        segs.push_back("");
        return segs;
    }
    // No prior non-empty segment to attach to (defensive path; the guard
    // in expand_beam means this is not reachable via a validated carrier).
    segs.push_back(uni::to_utf8(std::u32string(1, NASAL_TILDE)));
    return segs;
}

// positional.py _BEFORE_EXACT: exact next vowel letter → position key.
const char* before_exact(const std::string& letter) {
    static const std::map<std::string, const char*> map = {
        {"a", "before_a"}, {"e", "before_e"}, {"i", "before_i"},
        {"o", "before_o"}, {"u", "before_u"}, {"y", "before_y"},
        // Cyrillic plain vowel letters share the same exact-letter axes;
        // the iotated letters (е ё ю я) are deliberately absent.
        {"\xd0\xb0", "before_a"}, {"\xd1\x8d", "before_e"},
        {"\xd0\xb8", "before_i"}, {"\xd1\x96", "before_i"},
        {"\xd0\xbe", "before_o"}, {"\xd1\x83", "before_u"},
    };
    const auto it = map.find(letter);
    return it != map.end() ? it->second : nullptr;
}

// positional.py _AFTER_EXACT
const char* after_exact(const std::string& letter) {
    static const std::map<std::string, const char*> map = {
        {"a", "after_a"}, {"e", "after_e"}, {"i", "after_i"},
        {"o", "after_o"}, {"u", "after_u"},
    };
    const auto it = map.find(letter);
    return it != map.end() ? it->second : nullptr;
}

// positional.py _TRANSPARENT_SUFFIX_GRAPHEMES: word-final grammatical
// markers whose spec silencing leaves the preceding slot audibly final.
bool is_transparent_suffix(const std::string& grapheme) {
    return grapheme == "s" || grapheme == "x";
}

// positional.py _MUTE_E_INFLECTIONS / _MUTE_E_CONSONANT_DIGRAPHS
bool is_mute_e_inflection(const std::string& grapheme) {
    return grapheme == "s" || grapheme == "d";
}
bool is_mute_e_consonant_digraph(const std::string& grapheme) {
    return grapheme == "th";
}

struct WordEnd {
    bool final_slot = false;
    bool last_audible_slot = false;
    bool silent_final_vowel = false;
};

// positional.py effective_word_end: is this slot effectively word-final,
// under the spec's declared word_final overrides only? Templated because
// the ending matcher works on grapheme KEYS, not on the tokenizer's
// context objects (positional.py _EndSlot) — any slot exposing
// next()/grapheme()/is_vowel() answers the same question.
template <class Slot>
WordEnd effective_word_end(const Slot* ctx, const LanguageSpec* spec) {
    if (ctx == nullptr || spec == nullptr) return {};
    const auto silenced_word_finally = [&](const std::string& grapheme) {
        const auto entry = spec->positional_graphemes.find(grapheme);
        if (entry == spec->positional_graphemes.end()) return false;
        const auto final_candidates = entry->second.find("word_final");
        if (final_candidates == entry->second.end()) return false;
        return std::find(final_candidates->second.begin(),
                         final_candidates->second.end(),
                         std::string{}) != final_candidates->second.end();
    };
    const auto* tail = ctx->next();
    WordEnd out;
    out.final_slot = tail == nullptr;
    out.last_audible_slot =
        tail != nullptr && tail->next() == nullptr &&
        is_transparent_suffix(tail->grapheme()) &&
        silenced_word_finally(tail->grapheme());
    out.silent_final_vowel =
        (out.final_slot || out.last_audible_slot) && ctx->is_vowel() &&
        silenced_word_finally(ctx->grapheme());
    return out;
}

// positional.py _took_a_preposed_vowel: True when this grapheme absorbed
// a preposed vowel written before it (the Tai vowels), so a word_final
// coda entry must not reach it.
bool took_a_preposed_vowel(const GraphemeContext& ctx, const LanguageSpec* spec) {
    const GraphemeContext* prev = ctx.prev();
    if (prev == nullptr || spec == nullptr || spec->preposed_vowels.empty())
        return false;
    bool found = false;
    for (const auto& v : spec->preposed_vowels)
        if (uni::lower(uni::to_utf32(prev->grapheme())) ==
            uni::lower(uni::to_utf32(v)))
            found = true;
    if (!found) return false;
    // The absorbed vowel's own token spells nothing.
    return prev->token().ipa.empty();
}

// positional.py _is_mute_e_slot: True when *ctx* holds the word-final
// mute ⟨e⟩ of a ⟨VCe⟩ pattern (a single inflectional suffix counts as
// transparent; so does the ⟨th⟩ digraph).
bool is_mute_e_slot(const GraphemeContext* ctx) {
    if (ctx == nullptr || ctx->grapheme() != "e") return false;
    const GraphemeContext* tail = ctx->next();
    if (tail != nullptr &&
        (tail->next() != nullptr || !is_mute_e_inflection(tail->grapheme())))
        return false;
    const GraphemeContext* cons = ctx->prev();
    if (cons == nullptr || cons->is_vowel()) return false;
    if (cons->grapheme().size() > 1 && !is_mute_e_consonant_digraph(cons->grapheme()))
        return false;
    const GraphemeContext* nucleus = cons->prev();
    return nucleus != nullptr && nucleus->is_vowel();
}

// positional.py _carries_nucleus: a non-vowel grapheme whose primary IPA
// candidate contains a vowel (a CV unit) is stress-conditioned like a
// plain vowel letter. Reads the flat-table candidates, never a
// positional result, so the answer cannot be circular.
bool carries_nucleus(const GraphemeContext& ctx) {
    if (ctx.ipa().empty()) return false;
    for (const char32_t cp : uni::to_utf32(ctx.ipa().front()))
        if (vowels::is_ipa_vowel(cp)) return true;
    return false;
}

} // namespace

// ── weights.py ───────────────────────────────────────────────────────────

std::vector<double> candidate_base_costs(
        const std::vector<std::string>& ipa,
        const std::optional<std::vector<double>>& weights,
        const std::string& grapheme) {
    (void)grapheme; // only names the malformed-weights warning in Python
    const std::size_t n = ipa.size();
    bool valid = weights.has_value() && weights->size() == n;
    double total = 0.0;
    if (valid)
        for (const double w : *weights) {
            if (w < 0.0) { valid = false; break; }
            total += w;
        }
    if (valid && !(total > 0.0)) valid = false;
    if (!valid) {
        // Malformed (or absent) weights: uniform-descending rank cost —
        // byte-identical to the behaviour that predates weights.
        std::vector<double> costs(n);
        for (std::size_t rank = 0; rank < n; ++rank)
            costs[rank] = static_cast<double>(rank);
        return costs;
    }
    std::vector<double> costs;
    costs.reserve(n);
    for (const double w : *weights) {
        const double p = std::max(w / total, WEIGHT_FLOOR);
        costs.push_back(-std::log(p));
    }
    return costs;
}

// ── positional.py ────────────────────────────────────────────────────────

std::vector<Branch> build_branches(
        const std::vector<std::string>& candidates,
        const std::optional<std::vector<double>>& weights,
        const std::map<std::string, std::vector<std::string>>* allophone_map,
        const std::string& grapheme) {
    const std::vector<double> costs = candidate_base_costs(candidates, weights, grapheme);
    std::vector<Branch> branches;
    for (std::size_t rank = 0; rank < candidates.size(); ++rank) {
        const std::string& phoneme = candidates[rank];
        const double base_cost = costs[rank];
        if (allophone_map != nullptr) {
            const auto it = allophone_map->find(phoneme);
            if (it != allophone_map->end()) {
                for (std::size_t a_rank = 0; a_rank < it->second.size(); ++a_rank)
                    branches.push_back(
                        {it->second[a_rank], base_cost + 0.5 * static_cast<double>(a_rank)});
                continue;
            }
        }
        branches.push_back({phoneme, base_cost});
    }
    // Duplicate IPA strings collapse to their lowest cost; sort (cost, ipa).
    std::map<std::string, double> seen;
    for (const auto& b : branches) {
        auto it = seen.find(b.ipa);
        if (it == seen.end() || b.cost < it->second) seen[b.ipa] = b.cost;
    }
    std::vector<Branch> out;
    out.reserve(seen.size());
    for (const auto& [ipa, cost] : seen) out.push_back({ipa, cost});
    std::sort(out.begin(), out.end(), [](const Branch& a, const Branch& b) {
        if (a.cost != b.cost) return a.cost < b.cost;
        return a.ipa < b.ipa;
    });
    return out;
}

// positional.py _silent_word_finally: does *spec* emit nothing for the
// grapheme *letters* at a word end? Two ways a spec can say "mute here":
// a positional word_final entry whose FIRST candidate is empty, and a flat
// graphemes entry whose first candidate is empty (mute unconditionally).
bool silent_word_finally(const LanguageSpec* spec, const std::string& letters) {
    if (spec == nullptr) return false;
    const auto entry = spec->positional_graphemes.find(letters);
    if (entry != spec->positional_graphemes.end() && !entry->second.empty()) {
        if (auto candidates = entry->second.find("word_final");
            candidates != entry->second.end() && !candidates->second.empty())
            return candidates->second.front().empty();
        if (auto default_candidates = entry->second.find("default");
            default_candidates != entry->second.end() && !default_candidates->second.empty())
            return default_candidates->second.front().empty();
    }
    const auto flat = spec->graphemes.find(letters);
    return flat != spec->graphemes.end() && !flat->second.empty() &&
           flat->second.front().empty();
}

// positional.py _MAX_SILENT_TAIL: longest silent-tail grapheme considered.
constexpr std::size_t MAX_SILENT_TAIL = 3;

// positional.py _strip_silent_tail: remove the trailing graphemes *spec*
// emits nothing for. Longest grapheme first, repeated so a stack of mute
// finals is fully removed.
std::u32string strip_silent_tail(std::u32string syllable, const LanguageSpec* spec) {
    bool changed = true;
    while (changed && !syllable.empty()) {
        changed = false;
        const std::size_t max_size = std::min(MAX_SILENT_TAIL, syllable.size());
        for (std::size_t size = max_size; size >= 1; --size) {
            const std::u32string tail = syllable.substr(syllable.size() - size);
            if (silent_word_finally(spec, to_utf8(uni::lower(tail)))) {
                syllable.erase(syllable.size() - size);
                changed = true;
                break;
            }
        }
    }
    return syllable;
}

// positional.py _is_open_syllable: is *syllable* open (no coda)? Decided
// orthographically, on the syllable the spec's own syllabifier produced.
// nullopt when it cannot be decided.
std::optional<bool> is_open_syllable(std::u32string syllable,
                                     const LanguageSpec* spec,
                                     bool word_final = false) {
    while (!syllable.empty()) {
        const char32_t last = syllable.back();
        if (uni::is_alpha(last) || uni::combining(last) != 0) break;
        // A hyphen, an apostrophe or a space is NOT a segment, so it is no
        // coda: it cannot make an open syllable closed. It is also an
        // orthographic word boundary, so what stands before it is
        // word-final and its silent tail comes off with the same rule a
        // true word-final syllable gets.
        syllable.pop_back();
        word_final = true;
    }
    if (word_final) syllable = strip_silent_tail(std::move(syllable), spec);
    bool has_vowel = false;
    for (char32_t ch : syllable)
        if (vowels::is_orthographic_vowel(ch)) { has_vowel = true; break; }
    if (syllable.empty() || !has_vowel) return std::nullopt;
    return vowels::is_orthographic_vowel(syllable.back());
}

std::vector<const char*> grapheme_positions(
        const GraphemeContext& ctx, const LanguageSpec* spec,
        std::optional<std::size_t> syll_idx,
        std::optional<std::size_t> stressed_syll_idx,
        const std::set<std::size_t>& secondary_syll_idxs,
        const std::optional<std::string>& syllable,
        std::optional<bool> syllable_final) {
    std::vector<const char*> pos;
    const bool is_vowel = ctx.is_vowel();
    const GraphemeContext* prev = ctx.prev();
    const GraphemeContext* next = ctx.next();
    const bool prev_is_v = prev != nullptr && prev->is_vowel();
    const bool next_is_v = next != nullptr && next->is_vowel();

    // 0. before a vowel that is itself the word's last audible slot and is
    // silenced there (the e-caduc case) — most specific, checked before
    // the exact per-letter/class positions below.
    const WordEnd word_end = effective_word_end(&ctx, spec);
    const WordEnd next_word_end = effective_word_end(next, spec);
    if (next_word_end.silent_final_vowel) pos.push_back("before_final_vowel");

    // 1. before_X (exact letter), then the front/back vowel class, then
    // the palatal consonant class (same class tier).
    if (next != nullptr) {
        const std::string nc =
            vowels::base_vowel_letter(next->grapheme().substr(0, 1));
        if (const char* exact = before_exact(nc)) pos.push_back(exact);
        if (next->is_front()) pos.push_back("before_front_vowel");
        else if (next->is_back()) pos.push_back("before_back_vowel");
        if (next->is_palatal()) pos.push_back("before_palatal");
    }

    // 2. word boundary
    if (prev == nullptr) pos.push_back("word_initial");
    if (word_end.final_slot && !took_a_preposed_vowel(ctx, spec))
        pos.push_back("word_final");

    // 3. intervocalic (consonants between two vowels)
    if (prev_is_v && next_is_v) pos.push_back("intervocalic");

    // 4. nucleus_stressed / nucleus_unstressed for graphemes carrying a
    // nucleus. Besides plain vowel letters this covers CV units whose IPA
    // contains a vowel, whose reduction is conditioned on the same stress
    // geometry.
    // 4a. syllable aperture (open/closed), on its own and crossed with
    // stress. The crossed positions are emitted first (most specific),
    // then the aperture-only pair, and only then the stress-only
    // positions kept below. Aperture is unknown without a syllabification,
    // so nothing is emitted when syllable is absent.
    const std::optional<bool> open_syllable =
        syllable.has_value() ? is_open_syllable(to_utf32(*syllable), spec,
                                                 syllable_final.value_or(false))
                             : std::nullopt;
    if (open_syllable.has_value() && (is_vowel || carries_nucleus(ctx))) {
        if (syll_idx.has_value() && stressed_syll_idx.has_value()) {
            if (*syll_idx == *stressed_syll_idx) {
                pos.push_back(*open_syllable ? "nucleus_stressed_open"
                                             : "nucleus_stressed_closed");
            // A secondary foot head takes NEITHER crossed pair: it is not
            // the main stress, and it is not weak either, so the UNSTRESSED
            // pair (the reduction entry) must not reach it.
            } else if (secondary_syll_idxs.count(*syll_idx) == 0) {
                pos.push_back(*open_syllable ? "nucleus_unstressed_open"
                                             : "nucleus_unstressed_closed");
            }
        }
        pos.push_back(*open_syllable ? "open_syllable" : "closed_syllable");
    }

    if (syll_idx.has_value() && stressed_syll_idx.has_value() &&
            (is_vowel || carries_nucleus(ctx))) {
        if (*syll_idx == *stressed_syll_idx) {
            pos.push_back("nucleus_stressed");
        } else {
            // Prominence LEVEL: a secondary foot head is stressed, just
            // not the main accent, so it gets its own position and never
            // nucleus_unstressed. PRETONIC/POSTTONIC are about POSITION
            // relative to the main stress, a different fact, and are
            // emitted either way.
            if (secondary_syll_idxs.count(*syll_idx) != 0) {
                pos.push_back("nucleus_secondary");
            } else {
                pos.push_back("nucleus_unstressed");
            }
            if (*syll_idx < *stressed_syll_idx) {
                if (*syll_idx + 1 == *stressed_syll_idx) pos.push_back("first_pretonic");
                pos.push_back("pretonic");
            } else {
                pos.push_back("posttonic");
            }
        }
    }

    // 4b. nucleus of the ⟨VCe⟩ split digraph.
    if (is_vowel && next != nullptr && is_mute_e_slot(next->next()))
        pos.push_back("before_mute_e");

    // The "effectively word-final" case (a grapheme followed only by a
    // transparent silenced suffix) ranks BELOW the stress positions on
    // purpose — a true word-final vowel outranks stress, this heuristic
    // proxy does not.
    if (word_end.last_audible_slot && prev != nullptr)
        pos.push_back("word_final");

    // 5. after/before vowel / consonant context
    if (prev_is_v) {
        const bool trailing_axis =
            spec != nullptr && prev->grapheme().size() > 1 && [&] {
                const std::u32string lower =
                    uni::lower(uni::to_utf32(prev->grapheme()));
                for (const auto& d : spec->trailing_vowel_axis_digraphs)
                    if (lower == uni::lower(uni::to_utf32(d))) return true;
                return false;
            }();
        const std::string prev_trailing = trailing_axis
            ? prev->grapheme().substr(prev->grapheme().size() - 1)
            : prev->grapheme().substr(0, 1);
        const std::string pc = vowels::base_vowel_letter(prev_trailing);
        if (const char* exact = after_exact(pc)) pos.push_back(exact);
        if (trailing_axis && vowels::is_front_vowel(prev_trailing))
            pos.push_back("after_front_vowel");
        else if (trailing_axis && vowels::is_back_vowel(prev_trailing))
            pos.push_back("after_back_vowel");
        else if (prev->is_front()) pos.push_back("after_front_vowel");
        else if (prev->is_back()) pos.push_back("after_back_vowel");
        if (prev->is_palatal()) pos.push_back("after_palatal");
        pos.push_back("after_vowel");
    } else if (prev != nullptr && spec != nullptr &&
               !spec->inherent_vowel.empty() && carries_nucleus(*prev)) {
        // Abugida: a consonant letter whose inherent vowel was not
        // suppressed leaves an unwritten vowel between it and this
        // grapheme. BOTH positions are emitted, most specific first.
        pos.push_back("after_vowel");
        if (prev->is_palatal()) pos.push_back("after_palatal");
        pos.push_back("after_consonant");
    } else if (prev != nullptr) {
        if (prev->is_palatal()) pos.push_back("after_palatal");
        pos.push_back("after_consonant");
    }
    if (next_is_v) pos.push_back("before_vowel");
    else if (next != nullptr) pos.push_back("before_consonant");

    // 6. nucleus fallback for vowels
    if (is_vowel) pos.push_back("nucleus");

    pos.push_back("default");
    return pos;
}

std::optional<std::vector<std::string>> positional_candidates(
        const LanguageSpec& spec, const std::string& grapheme,
        const std::vector<const char*>& positions) {
    const auto entry = spec.positional_graphemes.find(grapheme);
    if (entry == spec.positional_graphemes.end() || entry->second.empty())
        return std::nullopt;
    for (const char* position : positions) {
        const auto it = entry->second.find(position);
        if (it != entry->second.end()) return it->second;
    }
    return std::nullopt;
}

std::vector<Branch> resolve_branches(
        const LanguageSpec& spec, const GraphemeContext& ctx,
        const PhonetokTokenizer& tokenizer,
        const std::map<std::string, std::vector<std::string>>* allophone_map,
        std::optional<std::size_t> syll_idx,
        std::optional<std::size_t> stressed_syll_idx,
        const std::set<std::size_t>& secondary_syll_idxs,
        const std::optional<std::string>& syllable,
        std::optional<bool> syllable_final) {
    const std::string& grapheme = ctx.grapheme();
    const std::vector<std::string> base_candidates = ctx.ipa();

    const auto positions =
        grapheme_positions(ctx, &spec, syll_idx, stressed_syll_idx,
                           secondary_syll_idxs, syllable, syllable_final);
    const auto pos_candidates = positional_candidates(spec, grapheme, positions);

    std::vector<std::string> candidates;
    std::optional<std::vector<double>> weights;
    if (!pos_candidates.has_value()) {
        // Flat table: per-candidate weights (if any) apply.
        candidates = base_candidates;
        weights = tokenizer.weights_for(grapheme);
    } else {
        // Positional winner first, then flat alternatives not already
        // covered. Positional overrides carry their own ordering; flat
        // weights do not apply to them.
        std::set<std::string> seen(pos_candidates->begin(), pos_candidates->end());
        candidates = *pos_candidates;
        for (const auto& c : base_candidates)
            if (!seen.count(c)) candidates.push_back(c);
    }
    return build_branches(candidates, weights, allophone_map, grapheme);
}

// ── phonetok.py nasal machinery ──────────────────────────────────────────

void constrain_nasal_carriers(std::vector<std::vector<Branch>>& slot_branches) {
    const std::u32string tilde(1, NASAL_TILDE);
    for (std::size_t idx = 1; idx < slot_branches.size(); ++idx) {
        std::vector<Branch>& branches = slot_branches[idx];
        if (branches.empty()) continue;
        bool only_tilde = true;
        for (const auto& b : branches)
            if (uni::to_utf32(b.ipa) != tilde) { only_tilde = false; break; }
        if (!only_tilde) continue;
        std::size_t j = idx - 1;
        while (true) {
            std::vector<Branch>& prev = slot_branches[j];
            if (prev.empty()) break;
            bool has_empty = false;
            for (const auto& b : prev)
                if (b.ipa.empty()) { has_empty = true; break; }
            std::vector<Branch> kept;
            for (const auto& b : prev) {
                if (b.ipa.empty()) { kept.push_back(b); continue; }
                const std::u32string tail = carrier_tail(uni::to_utf32(b.ipa));
                if (!tail.empty() &&
                    nasal_carriers().find(tail[0]) != std::u32string::npos)
                    kept.push_back(b);
            }
            if (!kept.empty() && kept.size() != prev.size()) prev = kept;
            if (!has_empty || j == 0) break;
            --j;
        }
    }
}

std::vector<Hypothesis> expand_beam(std::vector<Hypothesis> beam,
                                    const std::vector<Branch>& branches,
                                    std::size_t width) {
    const std::u32string tilde(1, NASAL_TILDE);
    const auto expand_all = [&](bool guarded) {
        std::vector<Hypothesis> out;
        for (const auto& hyp : beam) {
            for (const auto& branch : branches) {
                if (uni::to_utf32(branch.ipa) == tilde) {
                    std::u32string tail;
                    for (auto it = hyp.segments.rbegin(); it != hyp.segments.rend(); ++it)
                        if (!it->empty()) {
                            tail = carrier_tail(uni::to_utf32(*it));
                            break;
                        }
                    const bool legal =
                        !tail.empty() &&
                        nasal_carriers().find(tail[0]) != std::u32string::npos;
                    if (guarded && !legal) continue;
                    out.push_back({splice_nasal_tilde(hyp.segments), hyp.score + branch.cost});
                    continue;
                }
                Hypothesis next{hyp.segments, hyp.score + branch.cost};
                next.segments.push_back(branch.ipa);
                out.push_back(std::move(next));
            }
        }
        return out;
    };
    std::vector<Hypothesis> new_beam = expand_all(true);
    if (new_beam.empty()) {
        // Every branch was a guarded tilde with no valid carrier and no
        // oral alternative in this slot — keep them rather than drop the
        // slot entirely (defensive).
        new_beam = expand_all(false);
    }
    std::stable_sort(new_beam.begin(), new_beam.end(),
                     [](const Hypothesis& a, const Hypothesis& b) {
                         return a.score < b.score;
                     });
    if (new_beam.size() > width) new_beam.resize(width);
    return new_beam;
}

// ── positional.py normalize_ending_value / match_grammatical_ending ──

// positional.py normalize_ending_value: one grammatical_endings value →
// (rank1, alts). A string is one realisation; a list whose FIRST element
// is null defers rank 1 to the grapheme tables and adds only the remaining
// elements as lower-ranked candidates.
void normalize_ending_value(const std::vector<std::optional<std::string>>& value,
                            std::optional<std::string>& rank1,
                            std::vector<std::string>& alternatives) {
    if (value.empty()) return;
    rank1 = value.front();
    for (std::size_t i = 1; i < value.size(); ++i)
        if (value[i].has_value()) alternatives.push_back(*value[i]);
}

std::optional<GrammaticalEnding> match_grammatical_ending(
        const std::vector<std::string>& graphemes, const LanguageSpec* spec) {
    if (spec == nullptr || spec->grammatical_endings.empty() || graphemes.size() < 2)
        return std::nullopt;

    // Trailing transparent grammatical suffix, if the spec declares one.
    std::size_t suffix_tokens = 0;
    {
        // positional.py _EndSlot: a minimal duck-typed grapheme context —
        // the two fields effective_word_end reads.
        struct EndSlot {
            std::string g;
            const EndSlot* next_slot = nullptr;
            const EndSlot* next() const { return next_slot; }
            const std::string& grapheme() const { return g; }
            bool is_vowel() const { return false; }
        };
        EndSlot last{to_utf8(uni::lower(to_utf32(graphemes.back()))), nullptr};
        EndSlot penult{to_utf8(uni::lower(to_utf32(graphemes[graphemes.size() - 2]))), &last};
        if (effective_word_end(&penult, spec).last_audible_slot) suffix_tokens = 1;
    }

    const std::size_t end = graphemes.size() - suffix_tokens;
    // Letter offset at which each candidate token span starts, so a
    // surface-letter ending can be rounded outward to whole tokens.
    std::vector<std::string> stem;
    for (std::size_t i = 0; i < end; ++i) stem.push_back(to_utf8(uni::lower(to_utf32(graphemes[i]))));
    std::string surface;
    std::vector<std::size_t> starts;
    std::size_t offset = 0;
    for (const auto& token : stem) {
        starts.push_back(offset);
        offset += to_utf32(token).size();
        surface += token;
    }
    if (surface.empty()) return std::nullopt;
    // Ascending letter cut => the first hit is the longest ending. A cut
    // inside a token rounds OUTWARD to that token's start, and the span is
    // admissible only while at least one whole token still precedes it.
    for (std::size_t cut = 1; cut < to_utf32(surface).size(); ++cut) {
        // surface[cut:] — a code-point suffix of the joined stem.
        const std::u32string surface32 = to_utf32(surface);
        const std::string tail = to_utf8(surface32.substr(cut));
        if (spec->grammatical_endings.find(tail) == spec->grammatical_endings.end())
            continue;
        std::optional<std::string> rank1;
        std::vector<std::string> alternatives;
        normalize_ending_value(spec->grammatical_endings.at(tail), rank1, alternatives);
        // first = bisect_right(starts, cut) - 1: the last token start <= cut.
        std::size_t first = 0;
        bool found = false;
        for (std::size_t i = 0; i < starts.size(); ++i)
            if (starts[i] <= cut) { first = i; found = true; }
        if (!found || first < 1)
            // This ending's outward-rounded span leaves no head token; a
            // SHORTER ending later in the scan may still fit.
            continue;
        GrammaticalEnding out;
        out.ending = tail;
        out.ipa = rank1;
        out.tokens = end - first + suffix_tokens;
        out.alternatives = alternatives;
        return out;
    }
    return std::nullopt;
}

// ── positional.py merge_nucleusless_final_syllable / g2p.py _ApertureView ──

std::vector<std::string> merge_nucleusless_final_syllable(
        const std::vector<std::string>& syllables, const LanguageSpec* spec) {
    std::vector<std::string> result = syllables;
    if (result.size() < 2) return result;
    std::u32string core = to_utf32(result.back());
    while (!core.empty()) {
        const char32_t last = core.back();
        if (uni::is_alpha(last) || uni::combining(last) != 0) break;
        core.pop_back();
    }
    core = strip_silent_tail(std::move(core), spec);
    bool has_vowel = false;
    for (char32_t ch : core)
        if (vowels::is_orthographic_vowel(ch)) { has_vowel = true; break; }
    if (!core.empty() && has_vowel) return result;
    result[result.size() - 2] += result[result.size() - 1];
    result.pop_back();
    return result;
}

ApertureView::ApertureView(const std::vector<std::string>& syllables,
                           const LanguageSpec* spec, bool enabled)
    : enabled_(enabled),
      syllables_(enabled ? merge_nucleusless_final_syllable(syllables, spec)
                         : syllables),
      merged_(syllables_.size() < syllables.size()) {}

std::optional<std::size_t> ApertureView::index(std::optional<std::size_t> idx) const {
    if (!idx.has_value()) return std::nullopt;
    std::size_t i = *idx;
    if (merged_) i = std::min(i, syllables_.size() - 1);
    if (i >= syllables_.size()) return std::nullopt;
    return i;
}

std::optional<std::string> ApertureView::syllable(std::optional<std::size_t> idx) const {
    if (!enabled_) return std::nullopt;
    const auto i = index(idx);
    if (!i.has_value()) return std::nullopt;
    return syllables_[*i];
}

std::optional<bool> ApertureView::is_final(std::optional<std::size_t> idx) const {
    const auto i = index(idx);
    if (!i.has_value()) return std::nullopt;
    return *i == syllables_.size() - 1;
}

} // namespace orthography2ipa::beam
