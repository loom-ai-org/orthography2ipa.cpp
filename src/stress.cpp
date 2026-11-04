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

// stress.py _nucleus_mask: which code points of *word* belong to a syllable
// nucleus. *semivowels* maps ⟨y⟩/⟨w⟩ to its semivowel_role in the language,
// and its answer replaces *is_vowel_fn* for that letter. A contextual letter
// is a nucleus only when neither neighbour is a nucleus on its own account.
// Combining marks are never nuclei, and they are skipped when the
// neighbours are read.
std::vector<bool> nucleus_mask(const std::u32string& word,
                               const std::function<bool(const std::string&)>& is_vowel_fn,
                               const std::map<std::string, std::string>* semivowels = nullptr) {
    // 1 nucleus, 0 not, -1 contextual (the reference's None).
    std::vector<int> fixed;
    fixed.reserve(word.size());
    for (char32_t ch : word) {
        const std::string* role = nullptr;
        if (semivowels != nullptr) {
            const auto hit = semivowels->find(to_utf8(uni::lower_one(ch)));
            if (hit != semivowels->end()) role = &hit->second;
        }
        if (uni::combining(ch) != 0) fixed.push_back(0);
        else if (role == nullptr) fixed.push_back(is_vowel_fn(to_utf8(std::u32string(1, ch))) ? 1 : 0);
        else if (*role == vowels::SEMIVOWEL_CONTEXTUAL) fixed.push_back(-1);
        else fixed.push_back(*role == vowels::SEMIVOWEL_NUCLEUS ? 1 : 0);
    }
    std::vector<bool> mask;
    mask.reserve(word.size());
    for (std::size_t i = 0; i < fixed.size(); ++i) {
        int status = fixed[i];
        if (status == -1) {
            int before = 0, after = 0;
            for (std::size_t k = i; k-- > 0;)
                if (uni::combining(word[k]) == 0) { before = fixed[k]; break; }
            for (std::size_t k = i + 1; k < word.size(); ++k)
                if (uni::combining(word[k]) == 0) { after = fixed[k]; break; }
            status = !(before == 1 || after == 1) ? 1 : 0;
        }
        mask.push_back(status == 1);
    }
    return mask;
}

std::vector<bool> nucleus_mask(const std::string& word,
                               const std::function<bool(const std::string&)>& is_vowel_fn,
                               const std::map<std::string, std::string>* semivowels = nullptr) {
    return nucleus_mask(to_utf32(word), is_vowel_fn, semivowels);
}

// The code points of each syllable, with the offset of each syllable's first
// code point in the word the syllables divide.
std::vector<std::u32string> syllable_units(const std::vector<std::string>& syllables) {
    std::vector<std::u32string> out;
    out.reserve(syllables.size());
    for (const auto& syll : syllables) out.push_back(to_utf32(syll));
    return out;
}

// stress.py _capture_coda_liquids: move a syllable-initial liquid that
// heads a consonant cluster back onto the preceding syllable as its coda.
// *nucleus* is the nucleus_mask of the word the syllables divide.
std::vector<std::string> capture_coda_liquids(
    const std::vector<std::string>& syllables, const std::vector<bool>& nucleus) {
    if (syllables.size() < 2) return syllables;
    const auto units = syllable_units(syllables);
    std::vector<std::string> out{syllables[0]};
    std::size_t start = units[0].size();
    for (std::size_t s = 1; s < units.size(); ++s) {
        const std::u32string& syll = units[s];
        // onset = the leading consonant segments before the first nucleus char
        std::size_t j = 0;
        while (j < syll.size() && (uni::combining(syll[j]) != 0 || !nucleus[start + j])) ++j;
        start += syll.size();
        // the base consonant characters of the onset (combining marks ride along)
        std::vector<char32_t> cons;
        for (std::size_t k = 0; k < j; ++k)
            if (uni::combining(syll[k]) == 0) cons.push_back(syll[k]);
        // Capture only a FALLING cluster: liquid followed by an
        // equal-or-lower sonority consonant. A liquid + glide RISES in
        // sonority and is a legal complex onset, so it is left intact.
        const bool peel = cons.size() >= 2 && !out.back().empty() &&
                          is_liquid_cp(cons[0]) && !is_glide_cp(cons[1]);
        if (peel) {
            // peel the leading liquid (plus any combining marks riding on it)
            std::size_t k = 1;
            while (k < syll.size() && uni::combining(syll[k]) != 0) ++k;
            out.back() += to_utf8(syll.substr(0, k));
            out.push_back(to_utf8(syll.substr(k)));
        } else {
            out.push_back(syllables[s]);
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
        : spec_(spec), code_(spec.code), max_onset_(max_onset),
          onsets_(spec.onset_clusters.begin(), spec.onset_clusters.end()) {
        for (const auto& [g, values] : spec.graphemes) {
            if (g.empty()) continue;
            ordered_graphemes_.push_back(to_utf32(g));
            ipa_of_.emplace(to_utf32(g),
                            values.empty() ? std::u32string() : to_utf32(values[0]));
        }
        // Consonant graphemes whose last letter is a vowel letter, such as
        // Spanish ⟨qu gu⟩ (/k ɡ/): every reading is a consonant, so the
        // vowel letter is part of the onset and never a nucleus of its own.
        for (const auto& [g, values] : spec.graphemes) {
            const std::u32string units = to_utf32(g);
            if (units.size() < 2 || values.empty()) continue;
            if (is_vowel_char(to_utf8(units.substr(0, 1))) ||
                    !is_vowel_char(to_utf8(units.substr(units.size() - 1))))
                continue;
            const bool consonants = std::all_of(values.begin(), values.end(), [](const std::string& r) {
                if (r.empty()) return false;
                for (const auto& c : chars(r))
                    if (is_vowel_char(c)) return false;
                return true;
            });
            if (consonants) vowel_final_consonants_.insert(plain_lower(g));
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

    /// stress.py _OnsetJudge.vowel_final_consonants.
    const std::set<std::string>& vowel_final_consonants() const { return vowel_final_consonants_; }

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
        if (!onsets_.empty()) return declared(units);
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

    // _declared: whether the graphemes *units* spell an onset the spec
    // declares (stress.onset_clusters). A final glide rides on the onset
    // before it.
    bool declared(const std::vector<std::u32string>& units) {
        const auto last = segment(units.back());
        if (last.has_value() && vowels::sonority_class(to_utf8(*last)) == vowels::SONORITY_GLIDE) {
            if (units.size() == 2) return true;
            std::u32string head;
            for (std::size_t k = 0; k + 1 < units.size(); ++k) head += units[k];
            return licit(head);
        }
        std::string joined;
        for (const auto& unit : units) {
            const auto seg = segment(unit);
            if (!seg.has_value()) return false;
            joined += to_utf8(*seg);
        }
        return onsets_.count(joined) != 0;
    }

    bool is_palatal_glide_unit(const std::u32string& grapheme) const {
        const auto seg = segment(grapheme);
        return seg.has_value() && vowels::is_palatal_glide(to_utf8(*seg));
    }

private:
    const LanguageSpec& spec_;
    std::string code_;
    std::optional<int> max_onset_;
    std::set<std::string> onsets_;
    std::vector<std::u32string> ordered_graphemes_;
    std::map<std::u32string, std::u32string> ipa_of_;
    std::set<std::string> vowel_final_consonants_;
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
// *nucleus* is the nucleus_mask of the word the syllables divide.
std::vector<std::string> rebalance_onsets(const std::vector<std::string>& syllables,
                                          const std::vector<bool>& nucleus,
                                          OnsetJudge& judge) {
    if (syllables.size() < 2) return syllables;
    const auto units = syllable_units(syllables);
    std::vector<std::string> out{syllables[0]};
    std::size_t start = units[0].size();
    for (std::size_t s = 1; s < units.size(); ++s) {
        const std::u32string& syll = units[s];
        std::size_t j = 0;
        while (j < syll.size() && (uni::combining(syll[j]) != 0 || !nucleus[start + j])) ++j;
        start += syll.size();
        const std::u32string onset = syll.substr(0, j);
        if (onset.empty() || out.back().empty()) {
            out.push_back(syllables[s]);
            continue;
        }
        const auto pieces = judge.graphemes_of(onset);
        std::size_t keep = 0;
        for (std::size_t k = pieces.size(); k >= 1; --k) {
            std::u32string tail;
            for (std::size_t u = pieces.size() - k; u < pieces.size(); ++u) tail += pieces[u];
            if (judge.licit(tail)) {
                keep = tail.size();
                break;
            }
        }
        if (keep >= onset.size()) {
            out.push_back(syllables[s]);
            continue;
        }
        out.back() += to_utf8(onset.substr(0, onset.size() - keep));
        out.push_back(to_utf8(syll.substr(onset.size() - keep)));
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

namespace {

std::string cp_str(char32_t cp) { return to_utf8(std::u32string(1, cp)); }

std::string slice_utf8(const std::u32string& text, std::size_t from, std::size_t to) {
    return to_utf8(text.substr(from, to - from));
}

// stress.py _split_nuclei: split a vowel *run* into nuclei using
// *diphthongs*, greedy longest-first, so an empty *diphthongs* makes every
// vowel a nucleus. *hiatus* holds offsets into *run* of letters that open a
// nucleus of their own: no listed sequence is consumed across one.
std::vector<std::string> split_nuclei(const std::u32string& run,
                                      const std::vector<std::string>& diphthongs,
                                      const std::set<std::size_t>& hiatus) {
    if (run.size() < 2) return {to_utf8(run)};
    std::vector<std::u32string> ordered;
    for (const auto& d : diphthongs) ordered.push_back(to_utf32(d));
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const std::u32string& a, const std::u32string& b) {
                         return a.size() > b.size();
                     });
    const std::u32string lowered = uni::lower(run);
    std::vector<std::string> nuclei;
    std::size_t i = 0;
    while (i < lowered.size()) {
        bool matched = false;
        for (const auto& diph : ordered) {
            if (diph.size() <= lowered.size() - i &&
                    lowered.compare(i, diph.size(), diph) == 0 &&
                    std::none_of(hiatus.begin(), hiatus.end(), [&](std::size_t h) {
                        return i < h && h < i + diph.size();
                    })) {
                nuclei.push_back(slice_utf8(run, i, i + diph.size()));
                i += diph.size();
                matched = true;
                break;
            }
        }
        if (!matched) {
            nuclei.push_back(cp_str(run[i]));
            i += 1;
        }
        // a combining mark, or the length mark, is not a nucleus of its own
        while (i < lowered.size() &&
               (uni::combining(lowered[i]) != 0 || is_length_mark_cp(lowered[i]))) {
            if (!nuclei.empty()) nuclei.back() += cp_str(run[i]);
            else nuclei.push_back(cp_str(run[i]));
            i += 1;
        }
    }
    return nuclei;
}

// Whether the onset so far and the letter *ch* spell one of the
// *onset_graphemes* (stress.py, the ⟨qu gu⟩ test of
// _syllabify_with_diphthongs).
bool extends_onset_grapheme(const std::u32string& onset, char32_t ch,
                            const std::set<std::string>& onset_graphemes) {
    const std::u32string letter = uni::lower(std::u32string(1, ch));
    for (const auto& g : onset_graphemes) {
        const std::u32string units = to_utf32(g);
        if (units.empty() || std::u32string(1, units.back()) != letter) continue;
        const std::size_t head = units.size() - 1;
        if (onset.size() < head) continue;
        if (uni::lower(onset.substr(onset.size() - head)) == units.substr(0, head)) return true;
    }
    return false;
}

// stress.py _syllabify_with_diphthongs: syllabify with a vowel run split
// into nuclei by *diphthongs*, onset-maximising like the plain splitter.
// *nucleus* is the nucleus_mask of *word*. A consonant grapheme of
// *onset_graphemes* that ends in a vowel letter keeps that letter in the
// onset before another vowel (Spanish ⟨quieres⟩ quie-res).
std::vector<std::string> syllabify_with_diphthongs(
    const std::u32string& word, const std::vector<bool>& nucleus,
    const std::vector<std::string>& diphthongs,
    const std::set<std::string>& onset_graphemes = {},
    const std::set<std::size_t>& hiatus = {}) {
    std::vector<std::string> syllables;
    std::u32string onset;
    std::size_t i = 0;
    while (i < word.size()) {
        if (!onset.empty() && i + 1 < word.size() && nucleus[i + 1] &&
                extends_onset_grapheme(onset, word[i], onset_graphemes)) {
            onset += word[i];
            i += 1;
            continue;
        }
        if (!nucleus[i]) {
            onset += word[i];
            i += 1;
            // a combining mark rides along with whatever it sits on
            while (i < word.size() && uni::combining(word[i]) != 0) {
                onset += word[i];
                i += 1;
            }
            continue;
        }
        std::size_t j = i;
        while (j < word.size() &&
               (nucleus[j] || uni::combining(word[j]) != 0 || word[j] == U'ː'))
            ++j;
        std::set<std::size_t> run_hiatus;
        for (const std::size_t h : hiatus)
            if (i < h && h < j) run_hiatus.insert(h - i);
        const auto nuclei = split_nuclei(word.substr(i, j - i), diphthongs, run_hiatus);
        for (std::size_t k = 0; k < nuclei.size(); ++k)
            syllables.push_back((k == 0 ? to_utf8(onset) : std::string()) + nuclei[k]);
        onset.clear();
        i = j;
    }
    if (!onset.empty()) {
        if (!syllables.empty()) syllables.back() += to_utf8(onset);
        else syllables.push_back(to_utf8(onset));
    }
    return syllables;
}

bool any_of_range(const std::vector<bool>& mask, std::size_t from, std::size_t to) {
    for (std::size_t k = from; k < to && k < mask.size(); ++k)
        if (mask[k]) return true;
    return false;
}

} // namespace

std::vector<std::string> syllabify(const std::string& word_utf8,
                                   const std::set<std::string>* vowels_set,
                                   const std::vector<std::string>& diphthongs,
                                   bool coda_liquid_capture,
                                   const LanguageSpec* spec,
                                   std::optional<int> max_onset,
                                   const std::map<std::string, std::string>* semivowels,
                                   const std::set<std::string>& onset_graphemes,
                                   const std::set<std::size_t>& hiatus) {
    auto is_vowel_char_fn = [&](const std::string& c) {
        if (vowels_set != nullptr)
            return vowels_set->count(plain_lower(c)) != 0;  // c.lower() in vowels
        return is_vowel_char(c);
    };
    if (word_utf8.empty()) return {};
    const std::u32string word = to_utf32(word_utf8);
    const std::vector<bool> nucleus = nucleus_mask(word, is_vowel_char_fn, semivowels);
    std::optional<OnsetJudge> judge;
    if (spec != nullptr) judge.emplace(*spec, max_onset);
    if (!diphthongs.empty()) {
        auto sylls = syllabify_with_diphthongs(word, nucleus, diphthongs,
                                               onset_graphemes, hiatus);
        if (judge) sylls = rebalance_onsets(sylls, nucleus, *judge);
        if (coda_liquid_capture) sylls = capture_coda_liquids(sylls, nucleus);
        return sylls;
    }
    // indices of nucleus starts
    std::vector<std::string> syllables;
    std::u32string current;
    std::size_t current_start = 0;
    bool in_nucleus = false;
    for (std::size_t i = 0; i < word.size(); ++i) {
        const char32_t cp = word[i];
        if (uni::combining(cp) != 0 || cp == U'ː') {
            // A combining mark, or the IPA length mark ː, belongs to the
            // character it sits on: neither opens or closes a syllable.
            current += cp;
            continue;
        }
        const bool vowel = nucleus[i];
        if (vowel && !in_nucleus && !current.empty() &&
                any_of_range(nucleus, current_start, i)) {
            // a new nucleus after the previous syllable already has one
            syllables.push_back(to_utf8(current));
            current = std::u32string(1, cp);
            current_start = i;
        } else if (!vowel && in_nucleus) {
            // first consonant after a nucleus: close the syllable here so
            // the consonant opens the next one (onset-maximising)
            syllables.push_back(to_utf8(current));
            current = std::u32string(1, cp);
            current_start = i;
        } else {
            current += cp;
        }
        in_nucleus = vowel;
    }
    if (!current.empty()) {
        if (any_of_range(nucleus, current_start, word.size()) || syllables.empty())
            syllables.push_back(to_utf8(current));
        else
            syllables.back() += to_utf8(current);  // trailing consonant cluster joins the last syllable
    }
    if (judge) syllables = rebalance_onsets(syllables, nucleus, *judge);
    if (coda_liquid_capture) syllables = capture_coda_liquids(syllables, nucleus);
    return syllables;
}

std::vector<std::string> syllabify_for_mark(const std::string& ipa,
                                            const LanguageSpec& spec) {
    const std::vector<bool> nucleus =
        nucleus_mask(ipa, [](const std::string& c) { return is_vowel_char(c); });
    // `ipa_diphthongs`, where the spec declares it, replaces the orthographic
    // `diphthongs` for the transcription.
    std::vector<std::string> sylls =
        spec.ipa_diphthongs
            ? syllabify_with_diphthongs(to_utf32(ipa), nucleus, *spec.ipa_diphthongs)
            : syllabify(ipa, nullptr, spec.diphthongs);
    if (!spec.constrain_mark_onsets) {
        if (spec.coda_liquid_capture) sylls = capture_coda_liquids(sylls, nucleus);
        return sylls;
    }
    const std::optional<int> cap = spec.max_onset_declared
                                       ? std::optional<int>(spec.max_onset)
                                       : std::nullopt;
    IpaOnsetJudge judge(cap, spec.onset_clusters);
    sylls = rebalance_onsets(sylls, nucleus, judge);
    if (spec.coda_liquid_capture) sylls = capture_coda_liquids(sylls, nucleus);
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

namespace {

// stress.py _hiatus_merge_sites: the index of the syllable AFTER each
// boundary where the syllable before ends in a vowel letter and the one
// after starts with one, the site of glide formation.
std::vector<int> hiatus_merge_sites(const std::vector<std::string>& syllables) {
    std::vector<int> sites;
    for (std::size_t i = 1; i < syllables.size(); ++i) {
        const std::vector<std::string> before = chars(syllables[i - 1]);
        const std::vector<std::string> after = chars(syllables[i]);
        if (before.empty() || after.empty()) continue;
        if (vowels::is_basic_vowel_letter(before.back()) &&
                vowels::is_basic_vowel_letter(after.front()))
            sites.push_back(static_cast<int>(i));
    }
    return sites;
}

// stress.py _NON_SYLLABIC: the IPA diacritics that make a vowel
// non-syllabic, inverted breve below and above.
bool is_non_syllabic_cp(char32_t cp) { return cp == U'\u032f' || cp == U'\u0311'; }

enum class Contact { hiatus, merge };

// stress.py _written_vowel_contacts: the boundaries of *syllables* where two
// written vowels are in contact, keyed by the index of the syllable AFTER
// the boundary. A hiatus where the two letters are not one of the spec's
// diphthongs; a merge where they are, across an onset that writes nothing.
std::map<int, Contact> written_vowel_contacts(const std::vector<std::string>& syllables,
                                              const LanguageSpec& rules,
                                              const LanguageSpec* spec) {
    std::set<std::string> diphthongs;
    for (const auto& d : rules.diphthongs) diphthongs.insert(to_utf8(uni::lower(to_utf32(d))));
    std::optional<OnsetJudge> judge;
    if (spec != nullptr) judge.emplace(*spec, std::nullopt);
    std::map<int, Contact> contacts;
    for (std::size_t i = 1; i < syllables.size(); ++i) {
        const std::u32string before = to_utf32(syllables[i - 1]);
        const std::u32string after = to_utf32(syllables[i]);
        if (before.empty() || !vowels::is_basic_vowel_letter(to_utf8(before.substr(before.size() - 1))))
            continue;
        std::size_t j = 0;
        while (j < after.size() && !vowels::is_basic_vowel_letter(to_utf8(after.substr(j, 1)))) ++j;
        if (j == after.size()) continue;
        if (j != 0) {
            bool silent = judge.has_value();
            if (silent) {
                const std::u32string onset = to_utf32(lower_str(to_utf8(after.substr(0, j)), spec->code));
                for (const auto& g : judge->graphemes_of(onset)) {
                    const auto hit = spec->graphemes.find(to_utf8(g));
                    if (hit == spec->graphemes.end() ||
                            std::any_of(hit->second.begin(), hit->second.end(),
                                        [](const std::string& r) { return !r.empty(); })) {
                        silent = false;
                        break;
                    }
                }
            }
            if (!silent) continue;
        }
        const std::string pair =
            to_utf8(uni::lower(std::u32string{before.back(), after[j]}));
        if (!diphthongs.count(pair)) contacts[static_cast<int>(i)] = Contact::hiatus;
        else if (j != 0) contacts[static_cast<int>(i)] = Contact::merge;
    }
    return contacts;
}

// stress.py _split_first_nucleus: *syll* cut after its first syllabic vowel,
// when another one follows; nullopt when the nucleus has one syllabic vowel
// only.
std::optional<std::pair<std::string, std::string>> split_first_nucleus(const std::string& syll8) {
    const std::u32string syll = to_utf32(syll8);
    auto vowel_at = [&](std::size_t k) { return is_vowel_char(to_utf8(syll.substr(k, 1))); };
    std::size_t i = 0;
    while (i < syll.size() && !vowel_at(i)) ++i;
    if (i == syll.size()) return std::nullopt;
    ++i;
    while (i < syll.size() && (uni::combining(syll[i]) != 0 || is_length_mark_cp(syll[i]))) {
        if (is_non_syllabic_cp(syll[i])) return std::nullopt;
        ++i;
    }
    if (i == syll.size() || !vowel_at(i)) return std::nullopt;
    if (i + 1 < syll.size() && is_non_syllabic_cp(syll[i + 1])) return std::nullopt;
    return std::make_pair(to_utf8(syll.substr(0, i)), to_utf8(syll.substr(i)));
}

// stress.py _align_written_syllables: map each written syllable onto the IPA
// syllable that holds its nucleus, where two written vowels in contact show
// where the transcription has fewer syllables. Returns the IPA syllables, cut
// where needed, and the IPA slot of each written syllable; nullopt when no
// contact accounts for the difference.
std::optional<std::pair<std::vector<std::string>, std::vector<int>>> align_written_syllables(
    const std::vector<std::string>& syllables, const std::vector<std::string>& ipa_sylls,
    const LanguageSpec& rules, const LanguageSpec* spec) {
    const auto contacts = written_vowel_contacts(syllables, rules, spec);
    if (contacts.empty()) return std::nullopt;
    std::vector<std::string> out;
    std::vector<int> slots{0};
    std::string pending = ipa_sylls[0];
    std::size_t j = 0;
    for (std::size_t k = 1; k < syllables.size(); ++k) {
        const auto hit = contacts.find(static_cast<int>(k));
        const bool has_kind = hit != contacts.end();
        const bool merge = has_kind && hit->second == Contact::merge;
        const auto cut = has_kind ? split_first_nucleus(pending) : std::nullopt;
        if (merge && cut) {
            slots.push_back(static_cast<int>(out.size()));
            continue;
        }
        if (merge || !cut) {
            ++j;
            if (j == ipa_sylls.size()) return std::nullopt;
            out.push_back(pending);
            pending = ipa_sylls[j];
        } else {
            out.push_back(cut->first);
            pending = cut->second;
        }
        slots.push_back(static_cast<int>(out.size()));
    }
    if (j != ipa_sylls.size() - 1) return std::nullopt;
    out.push_back(pending);
    return std::make_pair(out, slots);
}

// stress.py _glide_anchored_index: the IPA syllable the stress lands on when
// glide formation, and only glide formation, explains a shorter
// transcription. Only a written accent names its own syllable, so the
// stressed syllable must carry one, and the merge sites must EXACTLY account
// for the missing syllables.
std::optional<int> glide_anchored_index(const std::vector<std::string>* syllables,
                                        const std::vector<std::string>& ipa_sylls,
                                        int stress_index, const LanguageSpec& spec) {
    if (syllables == nullptr || ipa_sylls.empty()) return std::nullopt;
    if (stress_index < 0 || static_cast<std::size_t>(stress_index) >= syllables->size())
        return std::nullopt;
    const std::set<std::string> marked(spec.marked_vowels.begin(),
                                       spec.marked_vowels.end());
    const auto stressed = chars((*syllables)[static_cast<std::size_t>(stress_index)]);
    if (marked.empty() ||
            std::none_of(stressed.begin(), stressed.end(),
                         [&](const std::string& ch) { return marked.count(ch) > 0; }))
        return std::nullopt;
    const std::vector<int> sites = hiatus_merge_sites(*syllables);
    if (syllables->size() - sites.size() != ipa_sylls.size()) return std::nullopt;
    const int shifted = stress_index - static_cast<int>(std::count_if(
        sites.begin(), sites.end(), [&](int site) { return site <= stress_index; }));
    if (shifted < 0 || static_cast<std::size_t>(shifted) >= ipa_sylls.size())
        return std::nullopt;
    return shifted;
}

} // namespace

std::string apply_stress_mark(const std::string& ipa, const LanguageSpec& spec,
                              int stress_index,
                              const std::vector<std::string>* syllables,
                              const std::vector<std::string>* ipa_syllables,
                              const std::string& mark,
                              const std::vector<int>& secondary_indices,
                              const LanguageSpec* written) {
    const std::string stress_mark = !mark.empty() ? mark : spec.stress_mark;
    if (ipa.find(stress_mark) != std::string::npos ||
            ipa.find(spec.stress_mark) != std::string::npos)
        return ipa;
    // The spec's diphthongs split the IPA too, not just the spelling.
    std::vector<std::string> ipa_sylls = ipa_syllables != nullptr
                                             ? *ipa_syllables
                                             : syllabify_for_mark(ipa, spec);
    if (ipa_sylls.empty()) return ipa;
    const long long n_written = syllables != nullptr ? static_cast<long long>(syllables->size()) : 0;
    const auto aligned =
        syllables != nullptr && ipa_sylls.size() < syllables->size() &&
                -n_written <= stress_index && stress_index < n_written
            ? align_written_syllables(*syllables, ipa_sylls, spec, written)
            : std::nullopt;
    if (aligned) {
        // The transcription has fewer syllables than the spelling, and the
        // spelling shows where: two written vowels in contact. The anchorings
        // below would guess the place instead, and a merge on the other side
        // of the stress (⟨ahorrábamos⟩ [aoraβamos]) moves the mark.
        ipa_sylls = aligned->first;
        const std::vector<int>& slots = aligned->second;
        const int main_slot =
            slots[static_cast<std::size_t>(stress_index < 0 ? n_written + stress_index : stress_index)];
        std::set<int> marked{main_slot};
        for (const int sec : secondary_indices) {
            if (sec < 0 || static_cast<std::size_t>(sec) >= slots.size()) continue;
            const int slot = slots[static_cast<std::size_t>(sec)];
            if (marked.count(slot)) continue;
            marked.insert(slot);
            ipa_sylls[static_cast<std::size_t>(slot)] =
                prefix_mark(ipa_sylls[static_cast<std::size_t>(slot)], SECONDARY_MARK);
        }
        ipa_sylls[static_cast<std::size_t>(main_slot)] =
            prefix_mark(ipa_sylls[static_cast<std::size_t>(main_slot)], stress_mark);
        std::string out;
        for (const auto& s : ipa_sylls) out += s;
        return out;
    }

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
                   (!ends_in_vowel(ipa) ||
                    glide_anchored_index(syllables, ipa_sylls, stress_index, spec))) {
            // The IPA has FEWER syllables than the orthography, the
            // start-anchored stress index still points inside it, AND the
            // transcription ends in a consonant while the spelling ended in
            // a vowel — word-final vowel APOCOPE. The stressed nucleus
            // keeps its start-anchored index; end-anchoring would drag the
            // mark forward onto an earlier syllable.
            //
            // The second licensed cause is glide formation, which merges two
            // written syllables into one and can do it AFTER the stress. There
            // the index is the one `glide_anchored_index` counts, and the
            // transcription is vowel-final, which the apocope test refuses.
            const int target_index =
                !ends_in_vowel(ipa)
                    ? stress_index
                    : *glide_anchored_index(syllables, ipa_sylls, stress_index, spec);
            for (const int sec : secondary_indices)
                if (sec >= 0 && static_cast<std::size_t>(sec) < ipa_sylls.size() &&
                        sec != target_index)
                    ipa_sylls[static_cast<std::size_t>(sec)] =
                        prefix_mark(ipa_sylls[static_cast<std::size_t>(sec)], SECONDARY_MARK);
            ipa_sylls[static_cast<std::size_t>(target_index)] =
                prefix_mark(ipa_sylls[static_cast<std::size_t>(target_index)], stress_mark);
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

namespace {

// stress.py _holds_diphthong: whether one segment holds a vowel letter
// followed by a second vowel letter, or by a glide letter (stress.py
// _OFFGLIDES) that no vowel letter follows.
bool holds_diphthong(const std::string& segment) {
    std::u32string letters;
    for (const char32_t cp : to_utf32(segment))
        if (uni::combining(cp) == 0) letters += cp;
    std::vector<bool> vowel;
    for (const char32_t cp : letters) {
        const std::string c = to_utf8(std::u32string(1, cp));
        vowel.push_back(vowels::is_ipa_vowel(c) || feats::is_vowel_phone(c));
    }
    vowel.push_back(false);
    static const std::u32string offglides = U"jwɥɰ";
    for (std::size_t i = 0; i + 1 < letters.size(); ++i) {
        const char32_t c = letters[i + 1];
        if (vowel[i] && (vowel[i + 1] ||
                         (offglides.find(c) != std::u32string::npos && !vowel[i + 2])))
            return true;
    }
    return false;
}

} // namespace

std::string syllable_weight(const std::string& syllable,
                            const std::vector<std::string>& atoms) {
    const auto segs = segment_with_affricates(syllable, atoms);
    auto first_vowel = std::find_if(segs.begin(), segs.end(), is_vowel_segment);
    if (first_vowel == segs.end()) return LIGHT;  // no nucleus — nothing to weigh
    const std::size_t start = static_cast<std::size_t>(first_vowel - segs.begin());
    std::size_t end = start;
    while (end < segs.size() && is_vowel_segment(segs[end])) ++end;
    // A long vowel or a diphthong is a branching (heavy) nucleus. A diphthong
    // the spec declares as one atom is one segment that holds a vowel letter
    // and, next to it, a second vowel letter or a glide letter with no vowel
    // after it (``aj``, ``aw``).
    bool long_vowel = end - start > 1;
    for (std::size_t k = start; k < end && !long_vowel; ++k)
        if (segs[k].find(LENGTH) != std::string::npos || holds_diphthong(segs[k]))
            long_vowel = true;
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

namespace {

// stress.py _semivowel_roles: the semivowel_role of each semivowel letter in
// *spec*.
std::map<std::string, std::string> semivowel_roles(const LanguageSpec& spec) {
    std::map<std::string, std::string> roles;
    for (const char letter : std::string(vowels::SEMIVOWEL_LETTERS)) {
        const std::string key(1, letter);
        const auto role = vowels::semivowel_role(key, spec.graphemes,
                                                 spec.positional_graphemes.count(key) != 0);
        if (role.has_value()) roles.emplace(key, *role);
    }
    return roles;
}

// stress.py _bundled_syllables: the bundled splitter's division of *word*,
// read from *spec*.
std::vector<std::string> bundled_syllables(const std::string& word, const LanguageSpec* spec,
                                           const std::vector<std::string>& diphthongs,
                                           const std::set<std::size_t>& hiatus = {}) {
    // Read before the phonotactics guard below can drop the spec: which
    // letters are nuclei is the language's own statement, independent of
    // whether its onset inventory has been checked.
    std::set<std::string> vowel_set;
    const std::set<std::string>* vowels_set = nullptr;
    std::map<std::string, std::string> roles;
    const std::map<std::string, std::string>* semivowels = nullptr;
    if (spec != nullptr && spec->stress_defined && !spec->vowel_letters.empty()) {
        for (const auto& v : spec->vowel_letters) vowel_set.insert(plain_lower(v));
        vowels_set = &vowel_set;
    } else if (spec != nullptr) {
        roles = semivowel_roles(*spec);
        semivowels = &roles;
    }
    std::set<std::string> onset_graphemes;
    if (spec != nullptr) onset_graphemes = OnsetJudge(*spec, std::nullopt).vowel_final_consonants();
    const LanguageSpec* used_spec = spec;
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
                     used_spec, max_onset, semivowels, onset_graphemes, hiatus);
}

bool starts_with(const std::u32string& text, const std::u32string& prefix) {
    return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

// stress.py _closed_hiatus_candidates: indices of the letters of *word* that
// stress.closed_hiatus splits from the vowel before them, before the stress
// is known.
std::vector<std::size_t> closed_hiatus_candidates(const std::string& word,
                                                  const LanguageSpec& spec) {
    const ClosedHiatus& rule = *spec.closed_hiatus;
    const std::u32string lowered = uni::lower(to_utf32(word));
    std::set<std::u32string> pairs, codas;
    for (const auto& p : rule.pairs) pairs.insert(to_utf32(p));
    for (const auto& c : rule.codas) codas.insert(to_utf32(c));
    std::vector<std::u32string> onsets;
    for (const auto& o : rule.onsets) onsets.push_back(to_utf32(o));
    std::vector<std::size_t> found;
    for (std::size_t k = 1; k < lowered.size(); ++k) {
        if (!pairs.count(lowered.substr(k - 1, 2))) continue;
        const std::u32string rest = lowered.substr(k + 1);
        const bool onset = std::any_of(onsets.begin(), onsets.end(),
                                       [&](const std::u32string& o) { return starts_with(rest, o); });
        bool coda = codas.count(rest.substr(0, 1)) != 0 &&
                    !(rest.size() > 1 && vowels::is_orthographic_vowel(rest[1]));
        if (coda) {
            for (const auto& [g, values] : spec.graphemes) {
                (void)values;
                const std::u32string units = to_utf32(g);
                if (units.size() > 1 && starts_with(rest, units)) { coda = false; break; }
            }
        }
        if (onset || coda) found.push_back(k);
    }
    return found;
}

} // namespace

std::set<std::size_t> closed_hiatus_positions(const std::string& word, const LanguageSpec& spec) {
    if (!spec.stress_defined || !spec.closed_hiatus) return {};
    const auto found = closed_hiatus_candidates(word, spec);
    if (found.empty()) return {};
    const std::set<std::size_t> all(found.begin(), found.end());
    const auto sylls = bundled_syllables(word, &spec, spec.diphthongs, all);
    const int stressed = detect_stress(word, spec, &sylls);
    std::size_t start = 0;
    for (int k = 0; k < stressed && k < static_cast<int>(sylls.size()); ++k)
        start += to_utf32(sylls[k]).size();
    std::set<std::size_t> out;
    for (const std::size_t k : found)
        if (k == start) out.insert(k);
    return out;
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
    const std::set<std::size_t> hiatus =
        used_spec != nullptr ? closed_hiatus_positions(word, *used_spec) : std::set<std::size_t>{};
    return bundled_syllables(word, used_spec, diphthongs, hiatus);
}

} // namespace orthography2ipa::stress
