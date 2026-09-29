#include "orthography2ipa/orthography2ipa.hpp"
#include "orthography2ipa/vowels.hpp"
#include "unicode_util.hpp"

#include <algorithm>
#include <numeric>

namespace orthography2ipa::tone {
namespace {

using uni::to_utf32;
using uni::to_utf8;

// The IPA tone symbols: Chao pitch numerals and Chao tone letters.
bool is_tone_mark(char32_t ch) {
    static const std::u32string marks = U"\u00b9\u00b2\u00b3\u2074\u2075"
                                        U"\u02e5\u02e6\u02e7\u02e8\u02e9";
    return marks.find(ch) != std::u32string::npos;
}

bool is_tone_mark_in(const std::string& seg) {
    for (const char32_t ch : to_utf32(seg))
        if (is_tone_mark(ch)) return true;
    return false;
}

// tone.py _is_nucleus: whether *seg* can continue a nucleus — a vocoid,
// or a mark on one (anything non-alphabetic).
bool is_nucleus(const std::string& seg) {
    const std::u32string units = to_utf32(seg);
    if (units.empty()) return false;
    return vowels::is_ipa_vowel(units[0]) || !uni::is_alpha(units[0]);
}

// tone.py _rime: (nucleus_end_offset, long_vowel, dead) for one syllable's
// IPA. The nucleus is the first maximal run of vocoids; everything after
// it is the coda. A syllable is dead when its coda opens with one of the
// spec's dead_codas (the obstruents that check a syllable), or when it has
// no coda and its nucleus is short. Length is read off the IPA length
// mark, so it is a property of the transcription rather than of the
// spelling. std::nullopt when the syllable has no nucleus at all.
struct Rime {
    std::size_t nucleus_end = 0;
    bool long_vowel = false;
    bool dead = false;
};
std::optional<Rime> rime(const std::string& ipa, const std::vector<std::string>& atoms,
                         const std::vector<std::string>& dead_codas) {
    const std::vector<std::string> segs = segment_ipa(ipa, atoms);
    const auto vowel_start = [](const std::string& seg) {
        const std::u32string units = to_utf32(seg);
        return !units.empty() && vowels::is_ipa_vowel(units[0]);
    };
    std::size_t i = 0;
    while (i < segs.size() && !vowel_start(segs[i])) ++i;
    if (i == segs.size()) return std::nullopt;
    std::size_t j = i;
    while (j < segs.size() && vowel_start(segs[j])) ++j;
    std::string nucleus, coda, before;
    for (std::size_t k = 0; k < j; ++k) nucleus += segs[k];
    for (std::size_t k = j; k < segs.size(); ++k) coda += segs[k];
    for (std::size_t k = 0; k < i; ++k) before += segs[k];
    Rime out;
    out.nucleus_end = to_utf32(before).size() + to_utf32(nucleus).size();
    out.long_vowel = nucleus.find("\xcb\x90") != std::string::npos;  // ː
    if (!coda.empty()) {
        out.dead = false;
        for (const auto& c : dead_codas)
            if (coda.rfind(c, 0) == 0) { out.dead = true; break; }
    } else {
        out.dead = !out.long_vowel;
    }
    return out;
}

// tone.py _syllable_tone: the tone letter one syllable's spelling calls
// for, or "".
std::string syllable_tone(const std::vector<std::string>& graphemes,
                          const std::string& ipa, const ToneData& rules,
                          const std::vector<std::string>& atoms) {
    std::string cls;
    for (const auto& grapheme : graphemes) {
        for (const char32_t ch : to_utf32(grapheme)) {
            const auto it = rules.classes.find(to_utf8(std::u32string(1, ch)));
            if (it != rules.classes.end()) { cls = it->second; break; }
        }
        if (!cls.empty()) break;
    }
    if (cls.empty()) return "";
    const auto by_shape = rules.table.find(cls);
    if (by_shape == rules.table.end()) return "";
    std::string mark = rules.no_mark;
    for (const auto& grapheme : graphemes) {
        for (const char32_t ch : to_utf32(grapheme)) {
            const auto it = rules.marks.find(to_utf8(std::u32string(1, ch)));
            if (it != rules.marks.end()) mark = it->second;
        }
    }
    const std::optional<Rime> shape = rime(ipa, atoms, rules.dead_codas);
    if (!shape.has_value()) return "";
    const std::string shape_name = shape->dead
        ? (shape->long_vowel ? "dead_long" : "dead_short") : "live";
    const auto lookup = [&by_shape, &mark](const char* shape_key)
        -> std::optional<std::string> {
        const auto shape_it = by_shape->second.find(shape_key);
        if (shape_it == by_shape->second.end()) return std::nullopt;
        const auto mark_it = shape_it->second.find(mark);
        if (mark_it == shape_it->second.end()) return std::nullopt;
        return mark_it->second;
    };
    std::optional<std::string> tone_name = lookup(shape_name.c_str());
    if (!tone_name.has_value()) tone_name = lookup("any");
    if (!tone_name.has_value() || tone_name->empty()) return "";
    const auto tone = rules.tones.find(*tone_name);
    return tone == rules.tones.end() ? std::string{} : tone->second;
}

} // namespace

// tone.py _syllable_slots: group slot indices into syllables by maximal
// onset over SLOTS. A slot whose IPA holds a vocoid is a nucleus. Of the
// consonant slots between two nuclei the last one opens the following
// syllable and the rest close the preceding one — maximal onset counted in
// slots, so a slot that already spells a cluster (⟨กร⟩ kr) stays one
// onset. Slots that spell nothing (a tone mark, a cancellation sign) ride
// the syllable of the slot they are written on, which is the nearest one
// to their left.
std::vector<std::vector<std::size_t>> syllable_slots(
        const std::vector<std::string>& segments) {
    std::vector<std::size_t> nuclei;
    for (std::size_t i = 0; i < segments.size(); ++i) {
        for (const char32_t ch : to_utf32(segments[i]))
            if (vowels::is_ipa_vowel(ch)) { nuclei.push_back(i); break; }
    }
    std::vector<std::vector<std::size_t>> sylls(nuclei.size());
    if (nuclei.empty()) return sylls;
    std::size_t prev = 0;
    for (std::size_t n = 0; n < nuclei.size(); ++n) {
        const std::size_t idx = nuclei[n];
        std::vector<std::size_t> between;
        for (std::size_t k = prev; k < idx; ++k) between.push_back(k);
        if (n == 0) {
            sylls[0] = between;
            sylls[0].push_back(idx);
        } else if (!segments[idx].empty() &&
                   !vowels::is_ipa_vowel(to_utf32(segments[idx]).front())) {
            // This slot spells its own onset — a consonant read with its
            // inherent vowel, or one that absorbed a preposed vowel sign.
            // It takes nothing from the consonants before it, and they all
            // close the syllable before.
            for (const std::size_t k : between) sylls[n - 1].push_back(k);
            sylls[n] = {idx};
        } else {
            std::vector<std::size_t> spelling;
            for (const std::size_t k : between)
                if (!segments[k].empty()) spelling.push_back(k);
            // The onset is the last slot before the nucleus that spells
            // something. Everything from the onset onward opens this
            // syllable, INCLUDING the slots after it that spell nothing: a
            // tone mark is written on the initial consonant, so it sits
            // between that consonant and the vowel sign that follows it,
            // and it names the tone of the syllable that consonant opens.
            const std::size_t cut = spelling.empty() ? idx : spelling.back();
            for (const std::size_t k : between)
                if (k < cut) sylls[n - 1].push_back(k);
            for (const std::size_t k : between)
                if (k >= cut) sylls[n].push_back(k);
            sylls[n].push_back(idx);
        }
        prev = idx + 1;
    }
    for (std::size_t k = prev; k < segments.size(); ++k)
        sylls.back().push_back(k);
    return sylls;
}

// tone.py dock_tone_marks: move every tone mark in *ipa* to the end of the
// syllable it belongs to. The syllable a mark belongs to is the one whose
// nucleus it follows; its end is found by maximal onset. Idempotent: a
// mark already at a syllable end is left where it is.
std::string dock_tone_marks(const std::string& ipa,
                            const std::vector<std::string>& atoms) {
    if (ipa.empty() || !is_tone_mark_in(ipa)) return ipa;
    std::vector<std::string> sorted_atoms = atoms;
    std::sort(sorted_atoms.begin(), sorted_atoms.end(),
              [](const std::string& a, const std::string& b) {
                  return to_utf32(a).size() > to_utf32(b).size();
              });
    const std::u32string text = to_utf32(ipa);
    const std::size_t n = text.size();
    std::u32string out;
    std::size_t i = 0;
    while (i < n) {
        if (!is_tone_mark(text[i])) {
            out.push_back(text[i]);
            ++i;
            continue;
        }
        std::size_t j = i;
        while (j < n && is_tone_mark(text[j])) ++j;
        const std::u32string tone = text.substr(i, j - i);
        // Window up to the next tone mark: everything the current syllable
        // could still claim.
        std::size_t t = j;
        while (t < n && !is_tone_mark(text[t])) ++t;
        const std::vector<std::string> segs =
            segment_ipa(to_utf8(text.substr(j, t - j)), sorted_atoms);
        bool all_nuclei = !segs.empty() && t < n;
        if (all_nuclei)
            for (const auto& seg : segs)
                if (!is_nucleus(seg)) { all_nuclei = false; break; }
        if (all_nuclei) {
            // Nothing but another tone-bearing nucleus follows: this
            // syllable ended at the mark (hiatus).
            out += tone;
            i = j;
            continue;
        }
        std::size_t k = 0;
        while (k < segs.size() && is_nucleus(segs[k]))
            ++k;  // offglides finish the nucleus
        std::size_t c = k;
        while (c < segs.size() && !is_nucleus(segs[c]))
            ++c;  // the consonants after the rime
        if (c < segs.size() && c > k)
            --c;  // the last one is the next onset
        std::u32string coda;
        for (std::size_t s = 0; s < c; ++s) coda += to_utf32(segs[s]);
        out += coda;
        out += tone;
        i = j + coda.size();
    }
    return to_utf8(out);
}

// tone.py assign_computed_tones: write each syllable's tone into
// *segments*, per the spec's tone rules. *graphemes* and *segments* are
// the parallel per-slot arrays of one reading: the grapheme key each slot
// matched, and the IPA it produced ("" for a slot that spells no segment).
// The tone letter is appended at the end of its syllable, where IPA writes
// it; a spec whose transcription convention puts it on the nucleus instead
// reads it back with dock_tone_marks.
std::string assign_computed_tones(const std::vector<std::string>& graphemes,
                                  const std::vector<std::string>& segments,
                                  const ToneData& rules,
                                  const std::vector<std::string>& atoms) {
    const std::vector<std::vector<std::size_t>> sylls = syllable_slots(segments);
    if (sylls.empty())
        // Nothing with a nucleus to carry a tone — a bare consonant
        // letter, a spelling the tables read as silent. The reading is
        // returned untouched rather than rebuilt from no syllables.
        return std::accumulate(segments.begin(), segments.end(), std::string{});
    std::string out;
    for (const auto& syl : sylls) {
        std::string ipa;
        std::vector<std::string> syl_graphemes;
        for (const std::size_t i : syl) {
            ipa += segments[i];
            if (i < graphemes.size()) syl_graphemes.push_back(graphemes[i]);
        }
        const std::string tone =
            syllable_tone(syl_graphemes, ipa, rules, atoms);
        out += ipa;
        if (!tone.empty()) out += tone;
    }
    return out;
}

} // namespace orthography2ipa::tone
