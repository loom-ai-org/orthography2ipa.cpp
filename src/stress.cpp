// Direct port of orthography2ipa/stress.py — see stress.hpp and the
// reference module for the documentation and citations behind every
// function. One C++ function per Python function, same name, same
// precedence rules, same corner cases. All per-character work runs on
// std::u32string (Python code points); conversion happens at UTF-8
// boundaries.
#include "orthography2ipa/stress.hpp"

#include "orthography2ipa/feats.hpp"
#include "unicode_util.hpp"
#include "orthography2ipa/vowels.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <map>
#include <stdexcept>
#include <functional>
#include <unordered_set>

namespace orthography2ipa::stress {

namespace {

using uni::to_utf32;
using uni::to_utf8;

// Size in bytes of the UTF-8 character at *pos*.
std::size_t char_size(const std::string& text, std::size_t pos) {
    const unsigned char c = static_cast<unsigned char>(text[pos]);
    return c < 0x80 ? 1 : (c & 0xe0) == 0xc0 ? 2 : (c & 0xf0) == 0xe0 ? 3 : 4;
}

// The characters of *text*, one UTF-8 string per code point.
std::vector<std::string> chars(const std::string& text) {
    std::vector<std::string> out;
    for (std::size_t i = 0; i < text.size();) {
        const std::size_t n = char_size(text, i);
        out.push_back(text.substr(i, n));
        i += n;
    }
    return out;
}

std::string plain_lower(const std::string& text) {
    return to_utf8(uni::lower(to_utf32(text)));
}

bool combining(const std::string& ch) {
    const auto u32 = to_utf32(ch);
    return u32.size() == 1 && uni::combining(u32[0]) != 0;
}

// stress.py _is_vowel_char: orthographic vowels of Latin/Greek-script
// languages (with accented forms) plus IPA vocoids. A combining mark is
// never a nucleus: it modifies the character it sits on.
bool is_vowel_char(const std::string& ch) {
    if (combining(ch)) return false;
    return vowels::is_orthographic_vowel(ch) || vowels::is_ipa_vowel(ch);
}

// stress.py _ends_in_vowel: whether *ipa*'s final phonetic segment is a
// vowel (ignoring trailing combining marks such as nasality/length).
bool ends_in_vowel(const std::string& ipa) {
    const auto list = chars(ipa);
    for (auto it = list.rbegin(); it != list.rend(); ++it) {
        if (combining(*it)) continue;
        return is_vowel_char(*it);
    }
    return false;
}

// stress.py _GLIDES = set("jw" "ʲʷ").
bool is_glide_cp(char32_t cp) {
    return cp == U'j' || cp == U'w' || cp == U'ʲ' || cp == U'ʷ';
}

// stress.py _LIQUIDS: laterals + rhotics.
const std::u32string& liquids() {
    static const std::u32string set = to_utf32("lɫʎɬrɾʁʀɽɺ");
    return set;
}

bool is_liquid_cp(char32_t cp) { return liquids().find(cp) != std::u32string::npos; }

// stress.py _capture_coda_liquids: move a syllable-initial liquid that
// heads a consonant cluster back onto the preceding syllable as its coda.
std::vector<std::string> capture_coda_liquids(
    const std::vector<std::string>& syllables) {
    if (syllables.size() < 2) return syllables;
    std::vector<std::string> out{syllables[0]};
    for (std::size_t s = 1; s < syllables.size(); ++s) {
        const std::string& syll = syllables[s];
        // onset = the leading consonant segments before the first nucleus char
        std::size_t j = 0;
        while (j < syll.size() && !is_vowel_char(syll.substr(j, char_size(syll, j))))
            j += char_size(syll, j);
        const std::string onset = syll.substr(0, j);
        // the base consonant characters of the onset (combining marks ride along)
        std::vector<std::string> cons;
        for (const auto& c : chars(onset))
            if (!combining(c)) cons.push_back(c);
        // Capture only a FALLING cluster: liquid followed by an
        // equal-or-lower sonority consonant. A liquid + glide RISES in
        // sonority and is a legal complex onset, so it is left intact.
        bool peel = false;
        if (cons.size() >= 2 && !out.back().empty()) {
            const auto first = to_utf32(cons[0]);
            const auto second = to_utf32(cons[1]);
            peel = first.size() == 1 && is_liquid_cp(first[0]) &&
                   !(second.size() == 1 && is_glide_cp(second[0]));
        }
        if (peel) {
            // peel the leading liquid (plus any combining marks riding on it)
            std::size_t k = char_size(syll, 0);
            while (k < syll.size() && combining(syll.substr(k, char_size(syll, k))))
                k += char_size(syll, k);
            out.back() += syll.substr(0, k);
            out.push_back(syll.substr(k));
        } else {
            out.push_back(syll);
        }
    }
    return out;
}

// stress.py _AFFRICATES: tie-bar spellings first, then the bare
// two-symbol forms.
const std::vector<std::string>& affricate_atoms() {
    static const std::vector<std::string> atoms = [] {
        const std::vector<std::string> bare = {
            "tʃ", "dʒ", "tɕ", "dʑ", "ts", "dz", "ʈʂ", "ɖʐ", "pf", "tɬ", "dɮ", "kx"};
        std::vector<std::string> out;
        for (const auto& a : bare)
            out.push_back(a.substr(0, char_size(a, 0)) + "\u0361" +
                          a.substr(char_size(a, 0)));  // a[0] + _TIE + a[1:]
        for (const auto& a : bare) out.push_back(a);
        return out;
    }();
    return atoms;
}

// stress.py _MARK_ATOMS: affricates as they reach the stress marker,
// where a boundary drawn inside one would cut a phoneme in half —
// lengthened on the stop and carrying ejective/aspirated/palatalized/
// labialized releases; sorted longest first.
const std::vector<std::string>& mark_atoms() {
    static const std::vector<std::string> atoms = [] {
        const std::vector<std::string> bases = {
            "tʃ", "dʒ", "tɕ", "dʑ", "ts", "dz", "ʈʂ", "ɖʐ", "pf",
            "tɬ", "dɮ", "kx", "dʐ", "tʂ"};
        const std::vector<std::string> lengths = {"", "ː"};
        const std::vector<std::string> releases = {"", "ʼ", "ʰ", "ʲ", "ʷ"};
        std::unordered_set<std::string> set;
        for (const auto& base : bases)
            for (const auto& length : lengths)
                for (const char* tie : {"", "\u0361"})
                    for (const auto& release : releases)
                        set.insert(base.substr(0, char_size(base, 0)) + length + tie +
                                   base.substr(char_size(base, 0)) + release);
        std::vector<std::string> out(set.begin(), set.end());
        std::stable_sort(out.begin(), out.end(),
                         [](const std::string& a, const std::string& b) {
                             return to_utf32(a).size() > to_utf32(b).size();
                         });
        return out;
    }();
    return atoms;
}

// stress.py _is_vowel_segment: decided by a segment's BASE character.
bool is_vowel_segment(const std::string& seg) {
    return !seg.empty() && vowels::is_ipa_vowel(to_utf32(seg)[0]);
}

// stress.py _LENGTH ("ː"); _LENGTH_MARKS ("ːˑ") for _prefix_mark.
constexpr const char* LENGTH = "ː";
bool is_length_mark_cp(char32_t cp) { return cp == U'ː' || cp == U'ˑ'; }

std::vector<std::string> segment_with_affricates(
    const std::string& ipa, const std::vector<std::string>& atoms = {}) {
    std::vector<std::string> all_atoms(atoms.begin(), atoms.end());
    for (const auto& a : affricate_atoms()) all_atoms.push_back(a);
    return segment_ipa(ipa, all_atoms);
}

// ── the onset judges (stress.py _OnsetJudge / _IpaOnsetJudge) ──

/// Decides whether a consonant run is a licit onset. Built once per spec.
class OnsetJudge {
public:
    OnsetJudge(const LanguageSpec& spec, std::optional<int> max_onset)
        : spec_(spec), code_(spec.code), max_onset_(max_onset) {
        for (const auto& [g, values] : spec.graphemes) {
            if (g.empty()) continue;
            ordered_graphemes_.push_back(to_utf32(g));
            ipa_of_.emplace(to_utf32(g),
                            values.empty() ? std::u32string() : to_utf32(values[0]));
        }
        // key=len, reverse=True — a stable sort keeps equal lengths in
        // declaration order, exactly like Python's sorted().
        std::stable_sort(ordered_graphemes_.begin(), ordered_graphemes_.end(),
                         [](const std::u32string& a, const std::u32string& b) {
                             return a.size() > b.size();
                         });
    }

    virtual ~OnsetJudge() = default;

    // graphemes_of: split an orthographic consonant *run* into the spec's
    // graphemes. Longest match first. Case folding goes through
    // lower_str, the same language-aware fold the rest of the engine
    // uses, done per candidate grapheme (it can change a string's length).
    virtual std::vector<std::u32string> graphemes_of(const std::u32string& run) const {
        std::vector<std::u32string> out;
        std::size_t i = 0;
        while (i < run.size()) {
            bool matched = false;
            for (const auto& g : ordered_graphemes_) {
                const std::size_t size = g.size();
                if (size <= run.size() - i &&
                        to_utf32(lower_str(to_utf8(run.substr(i, size)), code_)) == g) {
                    out.push_back(run.substr(i, size));
                    i += size;
                    matched = true;
                    break;
                }
            }
            if (!matched) {
                out.push_back(run.substr(i, 1));
                i += 1;
            }
            while (i < run.size() && uni::combining(run[i]) != 0) {
                out.back() += run[i];
                i += 1;
            }
        }
        return out;
    }

    // ipa_of: one grapheme's primary IPA.
    virtual std::u32string ipa_of(const std::u32string& grapheme) const {
        if (auto it = ipa_of_.find(grapheme); it != ipa_of_.end()) return it->second;
        const std::u32string lowered =
            to_utf32(lower_str(to_utf8(grapheme), code_));
        if (auto it = ipa_of_.find(lowered); it != ipa_of_.end()) return it->second;
        return {};
    }

    // _segment: the single IPA segment *grapheme* spells, or nullopt.
    virtual std::optional<std::u32string> segment(const std::u32string& grapheme) const {
        const std::u32string ipa = ipa_of(grapheme);
        if (ipa.empty()) return std::nullopt;
        const std::string ipa8 = to_utf8(ipa);
        if (vowels::is_affricate(ipa8))
            return ipa;  // one segment however the spec writes the tie bar
        const auto segs = segment_ipa(ipa8, affricate_atoms());
        if (segs.size() == 1) return to_utf32(segs[0]);
        return std::nullopt;
    }

    // _is_geminate: a doubled letter — ⟨ss⟩ ⟨tt⟩ ⟨nn⟩. Heterosyllabic.
    static bool is_geminate(const std::u32string& grapheme) {
        std::u32string letters;
        for (char32_t ch : grapheme)
            if (uni::combining(ch) == 0) letters.push_back(ch);
        return letters.size() == 2 &&
               uni::lower_one(letters[0]) == uni::lower_one(letters[1]);
    }

    // _opens_with_velar_nasal: coda-only in the inventories this judge
    // serves, so no onset may start with one. Read off the first segment
    // rather than the whole IPA so a cluster-spelling grapheme is judged too.
    bool opens_with_velar_nasal(const std::u32string& grapheme) const {
        const std::u32string ipa = ipa_of(grapheme);
        if (ipa.empty()) return false;
        const auto segs = segment_ipa(to_utf8(ipa), affricate_atoms());
        return !segs.empty() && !segs[0].empty() && to_utf32(segs[0])[0] == U'ŋ';
    }

    // _tier: the sonority tier of the grapheme's segment.
    int tier(const std::u32string& grapheme) const {
        const auto seg = segment(grapheme);
        return seg.has_value() ? vowels::sonority_class(to_utf8(*seg))
                               : vowels::SONORITY_UNKNOWN;
    }

    // licit: whether the orthographic consonant *run* may open a syllable.
    bool licit(const std::u32string& run) {
        const std::string key = to_utf8(run);
        if (auto it = cache_.find(key); it != cache_.end()) return it->second;
        if (cache_.size() >= CACHE_MAX) cache_.clear();
        const bool verdict = licit_uncached(run);
        cache_.emplace(key, verdict);
        return verdict;
    }

protected:
    static constexpr std::size_t MAX_MEMBERS = 3;
    static constexpr std::size_t CACHE_MAX = 8192;

    // _licit — virtual so _IpaOnsetJudge can replace it.
    virtual bool licit_uncached(const std::u32string& run) {
        if (run.empty()) return true;  // an onsetless syllable is fine
        for (char32_t ch : run)
            if (!uni::is_alpha(ch) && uni::combining(ch) == 0)
                // A hyphen, an apostrophe, a space or a digit is not a
                // segment. It cannot be an onset and cannot be part of
                // one, so it always ends up in the coda of whatever
                // precedes it — and is TRANSPARENT to weight.
                return false;
        const auto units = graphemes_of(run);
        // (a) the spec's own cap, before any reasoning of ours
        if (max_onset_.has_value() &&
                units.size() > static_cast<std::size_t>(*max_onset_))
            return false;
        if (opens_with_velar_nasal(units[0])) return false;  // coda-only segment
        if (units.size() == 1) return true;  // every language licenses a simple onset
        if (units.size() > MAX_MEMBERS) return false;
        for (const auto& u : units)
            if (is_geminate(u)) return false;  // a geminate never joins a complex onset
        if (units.size() == MAX_MEMBERS) {
            if (two_member(units[0], units[1]) && is_palatal_glide_unit(units[2]))
                return true;  // ⟨brj⟩ ⟨glj⟩
            return appendix_on(units[0], {units[1], units[2]});
        }
        return two_member(units[0], units[1]);
    }

    // _two_member: the four licit two-member cluster shapes.
    virtual bool two_member(const std::u32string& first, const std::u32string& second) const {
        const auto s1 = segment(first), s2 = segment(second);
        if (!s1.has_value() || !s2.has_value()) return false;
        const std::string s1_8 = to_utf8(*s1), s2_8 = to_utf8(*s2);
        const int t1 = vowels::sonority_class(s1_8);
        const int t2 = vowels::sonority_class(s2_8);
        if (t1 == vowels::SONORITY_UNKNOWN || t2 == vowels::SONORITY_UNKNOWN) return false;
        const bool obstruent = t1 <= vowels::SONORITY_FRICATIVE;
        if (vowels::is_glottal(s1_8)) {
            // A glottal is placeless and cannot head a rising onset.
            return false;
        }
        // RISE — obstruent + liquid (a palatal glide is judged below).
        if (obstruent && t2 >= vowels::SONORITY_LIQUID &&
                !vowels::is_palatal_glide(s2_8)) {
            // …except a homorganic coronal stop + lateral: */tl dl/ is the
            // systematic gap in the Germanic and Romance onset inventories.
            if (t1 == vowels::SONORITY_STOP && vowels::is_lateral(s2_8) &&
                    vowels::place_class(s1_8) == "coronal" &&
                    vowels::place_class(s2_8) == "coronal")
                return false;
            return true;
        }
        // CJ — a SONORANT + /j/. Icelandic ⟨mj lj nj rj⟩; an OBSTRUENT head
        // is excluded: continental Germanic cuts kat·je and dag·je.
        if (t2 == vowels::SONORITY_GLIDE && vowels::is_palatal_glide(s2_8) && !obstruent)
            return true;
        // CW — obstruent + labial approximant: ⟨kv⟩ ⟨dv⟩ ⟨tv⟩ ⟨kw⟩ ⟨zw⟩.
        if (obstruent && vowels::is_labial_approximant(s2_8)) return true;
        // APPENDIX — VOICELESS sibilant + voiceless obstruent (⟨st⟩ ⟨sch⟩
        // ⟨szcz⟩). Polish ⟨zc⟩ is no onset.
        {
            const auto voiced1 = vowels::is_voiced(s1_8);
            const auto voiced2 = vowels::is_voiced(s2_8);
            if (vowels::is_sibilant(s1_8) && voiced1.has_value() && !*voiced1 &&
                    t2 <= vowels::SONORITY_FRICATIVE && voiced2.has_value() &&
                    !*voiced2 && !vowels::is_sibilant(s2_8))
                return true;
        }
        // STOP_N — stop + non-homorganic coronal nasal (⟨kn⟩ ⟨gn⟩ ⟨pn⟩).
        if (t1 == vowels::SONORITY_STOP && t2 == vowels::SONORITY_NASAL &&
                vowels::place_class(s2_8) == "coronal" &&
                vowels::place_class(s1_8) != vowels::place_class(s2_8))
            return true;
        return false;
    }

    // _appendix_on: a sibilant appendix on a licit two-member core.
    bool appendix_on(const std::u32string& first,
                     const std::vector<std::u32string>& rest) const {
        const auto s1 = segment(first);
        const auto core_head = segment(rest[0]);
        if (!s1.has_value() || !core_head.has_value() ||
                !vowels::is_sibilant(to_utf8(*s1)))
            return false;
        const auto voiced1 = vowels::is_voiced(to_utf8(*s1));
        if (!voiced1.has_value() || *voiced1) return false;  // the appendix is /s/, voiceless
        if (vowels::is_sibilant(to_utf8(*core_head))) return false;  // ⟨ssch⟩: mis·schien
        if (vowels::sonority_class(to_utf8(*core_head)) > vowels::SONORITY_FRICATIVE)
            return false;  // the appendix adjoins to an OBSTRUENT core
        const auto voiced_head = vowels::is_voiced(to_utf8(*core_head));
        if (!voiced_head.has_value() || *voiced_head) return false;  // ⟨str⟩ yes, ⟨sdr⟩ no
        return two_member(rest[0], rest[1]);
    }

    bool is_palatal_glide_unit(const std::u32string& grapheme) const {
        const auto seg = segment(grapheme);
        return seg.has_value() && vowels::is_palatal_glide(to_utf8(*seg));
    }

private:
    const LanguageSpec& spec_;
    std::string code_;
    std::optional<int> max_onset_;
    std::vector<std::u32string> ordered_graphemes_;
    std::map<std::u32string, std::u32string> ipa_of_;
    std::map<std::string, bool> cache_;
};

// stress.py _IpaOnsetJudge: the same onset judgement, read off IPA
// segments instead of graphemes. Nothing here comes from a grapheme
// table, so this judge is safe for a spec that does not set
// constrain_onsets.
class IpaOnsetJudge : public OnsetJudge {
public:
    IpaOnsetJudge(std::optional<int> max_onset,
                  const std::vector<std::string>& onset_clusters)
        : OnsetJudge(empty_spec(), std::nullopt), max_onset_(max_onset),
          onset_clusters_(onset_clusters.begin(), onset_clusters.end()) {
        for (const auto& atom : mark_atoms()) atoms_.push_back(atom);
    }

    // graphemes_of: segmentation is segment_ipa; a unit's segment is itself.
    std::vector<std::u32string> graphemes_of(const std::u32string& run) const override {
        const auto segs = segment_ipa(to_utf8(run), atoms_);
        std::vector<std::u32string> out;
        out.reserve(segs.size());
        for (const auto& s : segs) out.push_back(to_utf32(s));
        return out;
    }

    std::u32string ipa_of(const std::u32string& grapheme) const override { return grapheme; }

    std::optional<std::u32string> segment(const std::u32string& grapheme) const override {
        return grapheme.empty() ? std::nullopt : std::optional<std::u32string>(grapheme);
    }

protected:
    // As the parent, plus every rise onto a GLIDE: a transcription shows
    // no morpheme boundary, and by sonority alone a glide is the strongest
    // possible second member of a rise. Romance /bje tja fja/, ⟨mulher⟩'s
    // lh+j, and the glottal ⟨Alajuela⟩ case are onsets the parent refuses.
    bool two_member(const std::u32string& first, const std::u32string& second) const override {
        if (!first.empty() && !second.empty()) {
            const int t1 = vowels::sonority_class(to_utf8(first));
            const int t2 = vowels::sonority_class(to_utf8(second));
            if (t2 == vowels::SONORITY_GLIDE && vowels::SONORITY_UNKNOWN < t1 && t1 < t2)
                return true;
        }
        return OnsetJudge::two_member(first, second);
    }

    // _licit: the parent's judgement, or the spec's own onset inventory.
    bool licit_uncached(const std::u32string& run) override {
        if (onset_clusters_.empty()) return OnsetJudge::licit_uncached(run);
        const auto units = graphemes_of(run);
        if (units.size() < 2) return OnsetJudge::licit_uncached(run);
        if (max_onset_.has_value() && units.size() > static_cast<std::size_t>(*max_onset_))
            return false;
        if (opens_with_velar_nasal(units[0])) return false;
        const auto last = segment(units.back());
        if (last.has_value() &&
                vowels::sonority_class(to_utf8(*last)) == vowels::SONORITY_GLIDE) {
            // A glide rides on whatever onset precedes it — Spanish
            // ⟨Adrián⟩ is a-drián, not ad-rián — so it is stripped and the
            // rest of the cluster judged on its own. One member left is a
            // simple onset.
            std::u32string head;
            for (std::size_t k = 0; k + 1 < units.size(); ++k) head += units[k];
            return units.size() == 2 || licit(head);
        }
        return onset_clusters_.count(to_utf8(run)) != 0;
    }

private:
    static const LanguageSpec& empty_spec() {
        // The IPA judge has no spec to read; its overrides replace every
        // grapheme-table access, so the empty spec is never consulted.
        static const LanguageSpec spec;
        return spec;
    }

    std::optional<int> max_onset_;
    std::unordered_set<std::string> onset_clusters_;
    std::vector<std::string> atoms_;
};

// stress.py _rebalance_onsets: move whatever the language cannot license
// as an onset into the coda. The first syllable's onset is never re-judged.
std::vector<std::string> rebalance_onsets(const std::vector<std::string>& syllables,
                                          OnsetJudge& judge) {
    if (syllables.size() < 2) return syllables;
    std::vector<std::string> out{syllables[0]};
    for (std::size_t s = 1; s < syllables.size(); ++s) {
        const std::string& syll = syllables[s];
        std::size_t j = 0;
        while (j < syll.size()) {
            const std::size_t n = char_size(syll, j);
            if (!combining(syll.substr(j, n)) && is_vowel_char(syll.substr(j, n))) break;
            j += n;
        }
        const std::string onset = syll.substr(0, j);
        if (onset.empty() || out.back().empty()) {
            out.push_back(syll);
            continue;
        }
        const auto units = judge.graphemes_of(to_utf32(onset));
        std::size_t keep = 0;
        for (std::size_t k = units.size(); k >= 1 && k <= units.size(); --k) {
            std::u32string tail;
            for (std::size_t u = units.size() - k; u < units.size(); ++u) tail += units[u];
            if (judge.licit(tail)) {
                keep = to_utf8(tail).size();
                break;
            }
            if (k == 1) break;
        }
        if (keep >= onset.size()) {
            out.push_back(syll);
            continue;
        }
        out.back() += onset.substr(0, onset.size() - keep);
        out.push_back(syll.substr(onset.size() - keep));
    }
    return out;
}

} // namespace


// ── public API (stress.py one-to-one) ──

std::set<std::string> cliticless_keys(const LanguageSpec& spec) {
    const auto& forms = spec.cliticless_words;
    std::set<std::string> keys;
    for (const auto& form : forms)
        keys.insert(to_utf8(uni::nfc(to_utf32(lower_str(form, spec.code)))));
    return keys;
}

bool is_cliticless(const std::string& word, const LanguageSpec& spec) {
    const auto keys = cliticless_keys(spec);
    if (keys.empty()) return false;
    return keys.count(to_utf8(uni::nfc(to_utf32(lower_str(word, spec.code))))) != 0;
}

// ── the bundled vowel-group syllabifier ──

// stress.py _split_nuclei: split a vowel *run* into nuclei using
// *diphthongs*, greedy longest-first.
std::vector<std::string> split_nuclei(const std::string& run,
                                      const std::vector<std::string>& diphthongs) {
    if (diphthongs.empty() || to_utf32(run).size() < 2) return {run};
    std::vector<std::u32string> ordered;
    for (const auto& d : diphthongs) ordered.push_back(to_utf32(d));
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const std::u32string& a, const std::u32string& b) {
                         return a.size() > b.size();
                     });
    const std::u32string lowered = uni::lower(to_utf32(run));
    std::vector<std::string> nuclei;
    std::size_t i = 0;
    while (i < lowered.size()) {
        bool matched = false;
        for (const auto& diph : ordered) {
            if (diph.size() <= lowered.size() - i &&
                    lowered.compare(i, diph.size(), diph) == 0) {
                // run[i:i + len(diph)] — the ORIGINAL text's slice.
                std::string nucleus;
                for (std::size_t k = i; k < i + diph.size(); ++k)
                    nucleus += to_utf8(std::u32string(1, to_utf32(run)[k]));
                nuclei.push_back(nucleus);
                i += diph.size();
                matched = true;
                break;
            }
        }
        if (!matched) {
            nuclei.push_back(to_utf8(std::u32string(1, to_utf32(run)[i])));
            i += 1;
        }
        // a combining mark, or the length mark, is not a nucleus of its own
        while (i < lowered.size() &&
               (uni::combining(lowered[i]) != 0 || is_length_mark_cp(lowered[i]))) {
            if (!nuclei.empty()) nuclei.back() += to_utf8(std::u32string(1, to_utf32(run)[i]));
            else nuclei.push_back(to_utf8(std::u32string(1, to_utf32(run)[i])));
            i += 1;
        }
    }
    return nuclei;
}

// stress.py _syllabify_with_diphthongs: syllabify with a vowel run split
// into nuclei by *diphthongs*, onset-maximising like the plain splitter.
std::vector<std::string> syllabify_with_diphthongs(
    const std::string& word,
    const std::function<bool(const std::string&)>& is_vowel_char_fn,
    const std::vector<std::string>& diphthongs) {
    std::vector<std::string> syllables;
    std::string onset;
    std::size_t i = 0;
    while (i < word.size()) {
        const std::string c = word.substr(i, char_size(word, i));
        if (!is_vowel_char_fn(c)) {
            onset += c;
            i += char_size(word, i);
            while (i < word.size() && combining(word.substr(i, char_size(word, i)))) {
                onset += word.substr(i, char_size(word, i));
                i += char_size(word, i);
            }
            continue;
        }
        std::size_t j = i;
        while (j < word.size()) {
            const std::string d = word.substr(j, char_size(word, j));
            if (is_vowel_char_fn(d) || combining(d) || to_utf32(d)[0] == U'ː') {
                j += char_size(word, j);
                continue;
            }
            break;
        }
        const auto nuclei = split_nuclei(word.substr(i, j - i), diphthongs);
        for (std::size_t k = 0; k < nuclei.size(); ++k)
            syllables.push_back((k == 0 ? onset : std::string()) + nuclei[k]);
        onset.clear();
        i = j;
    }
    if (!onset.empty()) {
        if (!syllables.empty()) syllables.back() += onset;
        else syllables.push_back(onset);
    }
    return syllables;
}

std::vector<std::string> syllabify(const std::string& word,
                                   const std::set<std::string>* vowels_set,
                                   const std::vector<std::string>& diphthongs,
                                   bool coda_liquid_capture,
                                   const LanguageSpec* spec,
                                   std::optional<int> max_onset) {
    auto is_vowel_char_fn = [&](const std::string& c) {
        if (vowels_set != nullptr)
            return vowels_set->count(plain_lower(c)) != 0;  // c.lower() in vowels
        return is_vowel_char(c);
    };
    if (word.empty()) return {};
    if (!diphthongs.empty()) {
        auto sylls = syllabify_with_diphthongs(word, is_vowel_char_fn, diphthongs);
        // Onset maximisation is a preference, and it stops at what the
        // language licenses (the spec's _OnsetJudge, when a spec is given).
        if (spec != nullptr) {
            OnsetJudge judge(*spec, max_onset);
            sylls = rebalance_onsets(sylls, judge);
        }
        if (coda_liquid_capture) sylls = capture_coda_liquids(sylls);
        return sylls;
    }
    // indices of nucleus starts
    std::vector<std::string> syllables;
    std::string current;
    bool in_nucleus = false;
    for (std::size_t i = 0; i < word.size();) {
        const std::string c = word.substr(i, char_size(word, i));
        const auto cp = to_utf32(c)[0];
        if (uni::combining(cp) != 0 || cp == U'ː') {
            // A combining mark, or the IPA length mark ː, belongs to the
            // character it sits on: neither opens or closes a syllable.
            current += c;
            i += char_size(word, i);
            continue;
        }
        const bool vowel = is_vowel_char_fn(c);
        bool current_has_vowel = false;
        for (const auto& d : chars(current))
            if (is_vowel_char_fn(d)) { current_has_vowel = true; break; }
        if (vowel && !in_nucleus && !current.empty() && current_has_vowel) {
            // a new nucleus after the previous syllable already has one
            syllables.push_back(current);
            current = c;
        } else if (!vowel && in_nucleus) {
            // first consonant after a nucleus: close the syllable here so
            // the consonant opens the next one (onset-maximising)
            syllables.push_back(current);
            current = c;
        } else {
            current += c;
        }
        in_nucleus = vowel;
        i += char_size(word, i);
    }
    if (!current.empty()) {
        bool current_has_vowel = false;
        for (const auto& d : chars(current))
            if (is_vowel_char_fn(d)) { current_has_vowel = true; break; }
        if (current_has_vowel || syllables.empty()) syllables.push_back(current);
        else syllables.back() += current;  // trailing consonant cluster joins the last syllable
    }
    if (spec != nullptr) {
        OnsetJudge judge(*spec, max_onset);
        syllables = rebalance_onsets(syllables, judge);
    }
    if (coda_liquid_capture) syllables = capture_coda_liquids(syllables);
    return syllables;
}

std::vector<std::string> syllabify_for_mark(const std::string& ipa,
                                            const LanguageSpec& spec) {
    std::vector<std::string> sylls =
        syllabify(ipa, nullptr, spec.diphthongs);
    if (!spec.constrain_mark_onsets) {
        if (spec.coda_liquid_capture) sylls = capture_coda_liquids(sylls);
        return sylls;
    }
    const std::optional<int> cap = spec.max_onset_declared
                                       ? std::optional<int>(spec.max_onset)
                                       : std::nullopt;
    IpaOnsetJudge judge(cap, spec.onset_clusters);
    sylls = rebalance_onsets(sylls, judge);
    if (spec.coda_liquid_capture) sylls = capture_coda_liquids(sylls);
    return sylls;
}

// stress.py _prefix_mark: *syll* with *mark* written before its first real
// segment (a length mark or a combining diacritic at the head of a
// syllable always belongs to the preceding vowel).
std::string prefix_mark(const std::string& syll, const std::string& mark) {
    std::size_t i = 0;
    while (i < syll.size()) {
        const auto cp = to_utf32(syll.substr(i, char_size(syll, i)))[0];
        if (is_length_mark_cp(cp) || uni::combining(cp) != 0) {
            i += char_size(syll, i);
            continue;
        }
        break;
    }
    return syll.substr(0, i) + mark + syll.substr(i);
}

std::set<int> secondary_stress_positions(int n_syllables,
                                         std::optional<int> stress_index,
                                         const LanguageSpec& spec) {
    if (spec.secondary_stress != SECONDARY_ALTERNATING) return {};
    if (!stress_index.has_value() || *stress_index < 0 || n_syllables < 3) return {};
    const int si = *stress_index;
    std::set<int> out;
    for (int i = si % 2; i < si - 1; i += 2) out.insert(i);
    return out;
}

std::string apply_stress_mark(const std::string& ipa, const LanguageSpec& spec,
                              int stress_index,
                              const std::vector<std::string>* syllables,
                              const std::vector<std::string>* ipa_syllables,
                              const std::string& mark,
                              const std::vector<int>& secondary_indices) {
    const std::string stress_mark = !mark.empty() ? mark : spec.stress_mark;
    if (ipa.find(stress_mark) != std::string::npos ||
            ipa.find(spec.stress_mark) != std::string::npos)
        return ipa;
    // The spec's diphthongs split the IPA too, not just the spelling.
    std::vector<std::string> ipa_sylls = ipa_syllables != nullptr
                                             ? *ipa_syllables
                                             : syllabify_for_mark(ipa, spec);
    if (ipa_sylls.empty()) return ipa;

    int offset_from_end;
    if (stress_index < 0) {
        offset_from_end = -stress_index;
    } else {
        const std::size_t n_orth =
            syllables != nullptr ? syllables->size() : ipa_sylls.size();
        long long overflow =
            static_cast<long long>(ipa_sylls.size()) - static_cast<long long>(n_orth);
        if (overflow > 0) {
            // The IPA has MORE syllables than the orthography: either the
            // orthographic syllabifier UNDERCOUNTED a vowel sequence, or an
            // allophone rule EPENTHESIZED a nucleus. The discriminator is
            // the nucleus itself: anaptyctic vowels are reduced (ə), while
            // an undercounted written sequence splits into FULL vowels.
            // Fold excess non-initial ə-nucleus syllables back into the
            // preceding syllable for COUNTING, then end-anchor as before.
            auto epenthetic = [&](const std::string& syll, bool final) {
                std::vector<std::string> vowel_chars;
                for (const auto& c : chars(syll))
                    if (is_vowel_char(c)) vowel_chars.push_back(c);
                if (vowel_chars != std::vector<std::string>{"ə"}) return false;
                if (!final) return true;
                const std::string schwa = "\u0259";
                const std::size_t n = char_size(syll, syll.size() - schwa.size());
                return !(n == schwa.size() &&
                         syll.compare(syll.size() - n, n, schwa) == 0);
            };
            std::vector<std::string> merged{ipa_sylls[0]};
            for (std::size_t i = 1; i < ipa_sylls.size(); ++i) {
                if (overflow > 0 &&
                        epenthetic(ipa_sylls[i], i + 1 == ipa_sylls.size())) {
                    merged.back() += ipa_sylls[i];
                    --overflow;
                } else {
                    merged.push_back(ipa_sylls[i]);
                }
            }
            ipa_sylls = merged;
        } else if (overflow < 0 &&
                   stress_index <= static_cast<long long>(ipa_sylls.size()) - 1 &&
                   !ends_in_vowel(ipa)) {
            // The IPA has FEWER syllables than the orthography, the
            // start-anchored stress index still points inside it, AND the
            // transcription ends in a consonant while the spelling ended in
            // a vowel — word-final vowel APOCOPE. The stressed nucleus
            // keeps its start-anchored index; end-anchoring would drag the
            // mark forward onto an earlier syllable.
            for (const int sec : secondary_indices)
                if (sec >= 0 && static_cast<std::size_t>(sec) < ipa_sylls.size() &&
                        sec != stress_index)
                    ipa_sylls[static_cast<std::size_t>(sec)] =
                        prefix_mark(ipa_sylls[static_cast<std::size_t>(sec)], SECONDARY_MARK);
            ipa_sylls[static_cast<std::size_t>(stress_index)] =
                prefix_mark(ipa_sylls[static_cast<std::size_t>(stress_index)], stress_mark);
            std::string out;
            for (const auto& s : ipa_sylls) out += s;
            return out;
        }
        offset_from_end = static_cast<int>(
            std::max<long long>(1, static_cast<long long>(n_orth) - stress_index));
    }

    // _target: the IPA-syllable slot *index* lands on, by the same
    // anchoring the main stress used.
    auto target_of = [&](int index) {
        if (index < 0)
            return static_cast<int>(ipa_sylls.size()) + index < 0
                       ? 0
                       : static_cast<int>(ipa_sylls.size()) + index;
        const std::size_t n = syllables != nullptr ? syllables->size() : ipa_sylls.size();
        return static_cast<int>(std::max<long long>(
            0, static_cast<long long>(ipa_sylls.size()) -
                   std::max<long long>(1, static_cast<long long>(n) - index)));
    };

    const int target = static_cast<int>(std::max<long long>(
        0, static_cast<long long>(ipa_sylls.size()) - offset_from_end));
    std::set<int> marked{target};
    for (const int sec : secondary_indices) {
        const int sec_target = target_of(sec);
        if (marked.count(sec_target)) continue;
        marked.insert(sec_target);
        ipa_sylls[static_cast<std::size_t>(sec_target)] =
            prefix_mark(ipa_sylls[static_cast<std::size_t>(sec_target)], SECONDARY_MARK);
    }
    ipa_sylls[static_cast<std::size_t>(target)] =
        prefix_mark(ipa_sylls[static_cast<std::size_t>(target)], stress_mark);
    std::string out;
    for (const auto& s : ipa_sylls) out += s;
    return out;
}

// ── quantity-sensitive stress ──

std::vector<std::string> syllabify_ipa(const std::string& ipa, int max_onset,
                                       const std::vector<std::string>& atoms) {
    if (ipa.empty()) return {};
    const auto segs = segment_with_affricates(ipa, atoms);
    // Nucleus spans (in SEGMENTS): maximal runs of vocoids.
    std::vector<std::pair<std::size_t, std::size_t>> nuclei;
    std::size_t i = 0;
    while (i < segs.size()) {
        if (is_vowel_segment(segs[i])) {
            const std::size_t start = i;
            while (i < segs.size() && is_vowel_segment(segs[i])) ++i;
            nuclei.emplace_back(start, i);
        } else {
            ++i;
        }
    }
    if (nuclei.empty()) return {ipa};
    std::vector<std::string> syllables;
    for (std::size_t n = 0; n < nuclei.size(); ++n) {
        const auto [start, end] = nuclei[n];
        std::size_t onset_start;
        if (n == 0) {
            onset_start = 0;  // everything before the first nucleus is its onset
        } else {
            const std::size_t prev_end = nuclei[n - 1].second;
            const std::size_t cluster = start - prev_end;
            onset_start =
                start - std::min<std::size_t>(static_cast<std::size_t>(max_onset), cluster);
        }
        std::size_t coda_end;
        if (n + 1 < nuclei.size()) {
            const std::size_t nxt_start = nuclei[n + 1].first;
            const std::size_t cluster = nxt_start - end;
            coda_end = nxt_start - std::min<std::size_t>(static_cast<std::size_t>(max_onset), cluster);
        } else {
            coda_end = segs.size();  // trailing consonants close the last syllable
        }
        std::string syllable;
        for (std::size_t k = onset_start; k < coda_end; ++k) syllable += segs[k];
        syllables.push_back(syllable);
    }
    return syllables;
}

std::string syllable_weight(const std::string& syllable,
                            const std::vector<std::string>& atoms) {
    const auto segs = segment_with_affricates(syllable, atoms);
    auto first_vowel = std::find_if(segs.begin(), segs.end(), is_vowel_segment);
    if (first_vowel == segs.end()) return LIGHT;  // no nucleus — nothing to weigh
    const std::size_t start = static_cast<std::size_t>(first_vowel - segs.begin());
    std::size_t end = start;
    while (end < segs.size() && is_vowel_segment(segs[end])) ++end;
    // A long vowel or a diphthong is a branching (heavy) nucleus.
    bool long_vowel = end - start > 1;
    for (std::size_t k = start; k < end && !long_vowel; ++k)
        if (segs[k].find(LENGTH) != std::string::npos) long_vowel = true;
    const std::size_t coda = segs.size() - end;
    if (long_vowel && coda > 0) return SUPERHEAVY;
    if (coda >= 2) return SUPERHEAVY;
    if (long_vowel || coda > 0) return HEAVY;
    return LIGHT;
}

int detect_stress_by_weight(const std::string& ipa, const LanguageSpec& spec,
                            const std::vector<std::string>& atoms) {
    const auto syllables = syllabify_ipa(ipa, spec.max_onset, atoms);
    const std::size_t n = syllables.size();
    if (n <= 1) return -1;
    if (spec.superheavy_final_attracts &&
            syllable_weight(syllables.back(), atoms) == SUPERHEAVY)
        return -1;
    if (n >= 2 && (syllable_weight(syllables[n - 2], atoms) == HEAVY ||
                   syllable_weight(syllables[n - 2], atoms) == SUPERHEAVY))
        return -2;
    const int default_position = spec.default_stress_position;
    if (default_position < 0)
        return n >= static_cast<std::size_t>(-default_position)
                   ? default_position
                   : -static_cast<int>(n);
    return -static_cast<int>(n);  // a positive default counts from the start
}

// ── iambic length ──

std::set<int> iambic_length_positions(const std::vector<std::string>& ipa_syllables,
                                      const std::vector<std::string>& atoms) {
    const std::size_t n = ipa_syllables.size();
    if (n < 3) return {};
    const bool first_heavy = syllable_weight(ipa_syllables[0], atoms) != LIGHT;
    std::set<int> out;
    for (std::size_t i = first_heavy ? 0 : 1; i < n - 1; i += 2)
        if (syllable_weight(ipa_syllables[i], atoms) == LIGHT) out.insert(static_cast<int>(i));
    return out;
}

// _lengthen_nucleus: *syllable* with a length mark written after its
// (short) nucleus.
std::string lengthen_nucleus(const std::string& syllable,
                             const std::vector<std::string>& atoms) {
    auto segs = segment_with_affricates(syllable, atoms);
    for (auto& seg : segs) {
        if (is_vowel_segment(seg)) {
            seg += LENGTH;
            break;
        }
    }
    std::string out;
    for (const auto& seg : segs) out += seg;
    return out;
}

std::string apply_iambic_length(const std::string& ipa, const LanguageSpec& spec,
                                const std::vector<std::string>& atoms) {
    if (!spec.iambic_length || ipa.empty() || ipa.find(LENGTH) != std::string::npos)
        return ipa;
    auto ipa_sylls = syllabify_ipa(ipa, spec.max_onset, atoms);
    const auto idxs = iambic_length_positions(ipa_sylls, atoms);
    if (idxs.empty()) return ipa;
    for (const int i : idxs)
        ipa_sylls[static_cast<std::size_t>(i)] =
            lengthen_nucleus(ipa_sylls[static_cast<std::size_t>(i)], atoms);
    std::string out;
    for (const auto& s : ipa_sylls) out += s;
    return out;
}

// ── stress detection ──

std::optional<int> plugin_stress(const std::string& word,
                                 const std::vector<std::string>& syllables,
                                 const std::string& lang) {
    const StressPlugin* plugin = lang.empty() ? nullptr : get_stress_plugin(lang);
    if (plugin == nullptr)
        throw std::runtime_error(
            "the " + lang + " spec sets stress.source = 'plugin', but no stress "
            "plugin is registered for it. This is fatal on purpose.");
    const auto index = plugin->stressed_index(word, syllables, lang);
    if (!index.has_value()) return std::nullopt;
    return static_cast<int>(*index);
}

int detect_stress(const std::string& word, const LanguageSpec& spec,
                  const std::vector<std::string>* syllables,
                  const std::string& lang) {
    std::vector<std::string> sylls;
    if (syllables != nullptr) {
        sylls = *syllables;
    } else {
        sylls = syllables_for(word, lang, spec.diphthongs);
    }
    const std::size_t n = sylls.size();
    if (n <= 1) return 0;

    // 0. The spec may say its stress is not expressible here at all, and
    // name a plugin instead.
    if (spec.stress_source == "plugin") {
        const auto index = plugin_stress(word, sylls, lang);
        if (index.has_value())
            return std::max(0, std::min(*index, static_cast<int>(n) - 1));
    }

    // 1. written accent overrides everything
    if (!spec.marked_vowels.empty()) {
        const std::set<std::string> marked(spec.marked_vowels.begin(),
                                           spec.marked_vowels.end());
        for (std::size_t idx = 0; idx < sylls.size(); ++idx) {
            for (const auto& ch : chars(sylls[idx])) {
                if (marked.count(ch)) return static_cast<int>(idx);
            }
        }
    }

    const std::u32string lowered = uni::lower(to_utf32(word));
    const std::string lowered8 = to_utf8(lowered);

    auto ends_with = [&](const std::vector<std::string>& endings) {
        // sorted(endings, key=len, reverse=True) — stable, so equal lengths
        // keep declaration order.
        std::vector<std::u32string> ordered;
        for (const auto& e : endings) ordered.push_back(to_utf32(e));
        std::stable_sort(ordered.begin(), ordered.end(),
                         [](const std::u32string& a, const std::u32string& b) {
                             return a.size() > b.size();
                         });
        for (const auto& ending : ordered) {
            const std::string e = to_utf8(ending);
            if (lowered8.size() >= e.size() &&
                    lowered8.compare(lowered8.size() - e.size(), e.size(), e) == 0)
                return true;
        }
        return false;
    };

    // 2. oxytone endings — longest first so '-im' wins over '-m'
    if (ends_with(spec.final_stress_endings)) return static_cast<int>(n) - 1;
    // 3. forced paroxytone endings
    if (ends_with(spec.penult_stress_endings)) return static_cast<int>(n) - 2;
    // 4. forced proparoxytone endings — clamped into the word.
    if (ends_with(spec.antepenult_stress_endings))
        return std::max(0, static_cast<int>(n) - 3);

    // 5. default position, clamped into the word.
    const int pos = spec.default_stress_position;
    if (pos >= 1) return std::min(pos - 1, static_cast<int>(n) - 1);
    return std::max(0, static_cast<int>(n) + pos);
}


// stress.py _spec_for: the loaded spec for *lang*, or nullptr.
const LanguageSpec* spec_for(const std::string& lang) {
    try {
        return &get(lang);
    } catch (const std::exception&) {
        return nullptr;
    }
}

std::vector<std::string> syllables_for(const std::string& word, const std::string& lang,
                                       const std::vector<std::string>& diphthongs,
                                       const LanguageSpec* spec) {
    // stress.py _syllables_for: registered plugin for *lang* first, naive
    // fallback second.
    if (!lang.empty()) {
        if (const SyllabifierPlugin* plugin = get_syllabifier(lang)) {
            try {
                auto sylls = plugin->syllabify(word, lang);
                std::string joined;
                for (const auto& s : sylls) joined += s;
                if (!sylls.empty() && joined == word) return sylls;
            } catch (const std::exception&) {
                // logging.getLogger(__name__).warning(
                //     "syllabifier plugin %r failed on word %r", ...)
            }
        }
    }
    const LanguageSpec* used_spec = spec;
    if (used_spec == nullptr && !lang.empty()) used_spec = spec_for(lang);
    // Read before the phonotactics guard below can drop the spec: which
    // letters are nuclei is the language's own statement, independent of
    // whether its onset inventory has been checked.
    std::set<std::string> vowel_set;
    const std::set<std::string>* vowels_set = nullptr;
    if (used_spec != nullptr && used_spec->stress_defined &&
            !used_spec->vowel_letters.empty()) {
        for (const auto& v : used_spec->vowel_letters)
            vowel_set.insert(plain_lower(v));
        vowels_set = &vowel_set;
    }
    std::optional<int> max_onset;
    if (used_spec != nullptr && !used_spec->constrain_onsets) {
        // The spec has not opted in: its onset inventory has not been
        // checked against the shapes the judge knows. Maximise the onset
        // unconstrained, exactly as before.
        used_spec = nullptr;
    }
    if (used_spec != nullptr && used_spec->stress_defined && used_spec->max_onset_declared)
        max_onset = used_spec->max_onset;
    return syllabify(word, vowels_set, diphthongs, /*coda_liquid_capture=*/false,
                     used_spec, max_onset);
}

} // namespace orthography2ipa::stress
