// Direct port of orthography2ipa/inventory.py — the closed set of symbols a
// spec can emit. See include/orthography2ipa/inventory.hpp for the module's
// contract and the closure invariant.
//
// All string and position logic runs on Unicode CODE POINTS (the reference's
// `len`, slicing and `in` are code-point operations), with the UTF-8 boundary
// conversions done here. The sandhi `transform` backreference scan is a Python
// `re` substitution, so it runs on re32 (src/codepoint_regex.hpp) rather than
// std::regex over UTF-8 bytes.
#include "orthography2ipa/inventory.hpp"

#include "orthography2ipa/orthography2ipa.hpp"

#include "beam.hpp"
#include "codepoint_regex.hpp"
#include "unicode_util.hpp"

#include <algorithm>
#include <optional>
#include <set>
#include <utility>
#include <string>
#include <vector>

// positional.py normalize_ending_value, in the C++ library as
// orthography2ipa::beam::normalize_ending_value (src/beam.cpp). src/beam.hpp
// defines the beam seam but does not declare this one function, so the
// prototype is restated here (identical signature; a later header declaration
// is a no-op redeclaration). Adding it to src/beam.hpp is the orchestrator's
// call — see build/notes/worker_inventory.md.
namespace orthography2ipa::beam {
void normalize_ending_value(const std::vector<std::optional<std::string>>& value,
                            std::optional<std::string>& rank1,
                            std::vector<std::string>& alternatives);
} // namespace orthography2ipa::beam

namespace orthography2ipa::inventory {

using uni::to_utf32;
using uni::to_utf8;

const std::vector<std::string> STRESS_MARKS = {to_utf8(U"ˈ"), to_utf8(U"ˌ")};

namespace {

/// inventory.py: `ch in STRESS_MARKS` / `e not in STRESS_MARKS` — membership
/// in the two-mark tuple, on a whole string.
bool is_stress_mark(const std::string& text) {
    return std::find(STRESS_MARKS.begin(), STRESS_MARKS.end(), text) != STRESS_MARKS.end();
}

bool is_stress_mark(char32_t cp) {
    return cp == U'ˈ' || cp == U'ˌ';
}

/// Python `str.isspace()`: non-empty, and every character is one CPython's
/// Py_UNICODE_ISSPACE accepts (the Unicode White_Space property plus the
/// separators U+001C..U+001F, U+0085 and U+3000).
bool is_space(const std::string& text) {
    static const char32_t spaces[] = {
        0x0009, 0x000A, 0x000B, 0x000C, 0x000D, 0x001C, 0x001D, 0x001E, 0x001F,
        0x0020, 0x0085, 0x00A0, 0x1680, 0x2000, 0x2001, 0x2002, 0x2003, 0x2004,
        0x2005, 0x2006, 0x2007, 0x2008, 0x2009, 0x200A, 0x2028, 0x2029, 0x202F,
        0x205F, 0x3000,
    };
    const std::u32string wide = to_utf32(text);
    if (wide.empty()) return false;
    for (const char32_t cp : wide)
        if (std::find(std::begin(spaces), std::end(spaces), cp) == std::end(spaces)) return false;
    return true;
}

/// inventory.py _positional_readings: every IPA reading in the positional
/// table, across all positions.
std::vector<std::string> positional_readings(const LanguageSpec& spec) {
    std::vector<std::string> out;
    for (const auto& by_position : spec.positional_graphemes)
        for (const auto& entry : by_position.second)
            for (const auto& reading : entry.second) out.push_back(reading);
    return out;
}

/// inventory.py `_BACKREF = re.compile(r"\\g<[^>]*>|\\\d+")`: a
/// regex-substitution backreference — `\1` or `\g<name>` — inside a sandhi
/// `transform`. It re-emits text the input already carried, so it introduces no
/// NEW symbol; only the literal characters around it do.
const re32::Regex& backref_pattern() {
    static const re32::Regex pattern = re32::Regex::compile(to_utf32(R"(\\g<[^>]*>|\\\d+)"));
    return pattern;
}

/// inventory.py _transform_literals: the literal text a sandhi `transform`
/// inserts, stripped of backrefs (`.sub("", transform)`) and of every remaining
/// backslash (`.replace("\\", "")`). A sandhi rule's `transform` is a `re.sub`
/// replacement string: the backreferences copy the matched input — already in
/// the inventory by construction — and only the literal remainder is a new
/// source of symbols. French liaison's linking tie `‿` is added exactly this
/// way.
std::string transform_literals(const std::string& transform) {
    if (transform.empty()) return "";
    const std::u32string stripped = backref_pattern().sub(to_utf32(transform), U"");
    std::u32string out;
    for (const char32_t cp : stripped)
        if (cp != U'\\') out += cp;
    return to_utf8(out);
}

/// inventory.py _atoms (and the atom list _sandhi_emissions builds by hand):
/// the segments that must never be split, longest first — the declared
/// `phonemes` plus every multi-character emission that is not a stress mark.
/// Length is measured in code points, as Python's `len` is. Ties are ordered
/// lexicographically here where Python's `sorted` leaves them in the set's
/// iteration order: two atoms of equal length cannot both match at one
/// position, so `segment_ipa` cannot see the difference.
std::vector<std::string> atoms_from(const std::set<std::string>& emissions,
                                    const std::vector<std::string>& phonemes) {
    std::set<std::string> pool;
    for (const auto& phoneme : phonemes)
        if (!phoneme.empty()) pool.insert(phoneme);
    for (const auto& emission : emissions)
        if (to_utf32(emission).size() > 1 && !is_stress_mark(emission)) pool.insert(emission);
    // Python's `sorted(pool, key=len, reverse=True)` evaluates the key ONCE per
    // element, so the sort here decorates each atom with its code-point length
    // first; a comparator that decoded both strings on every comparison would
    // recompute what CPython caches.
    std::vector<std::pair<std::size_t, std::string>> decorated;
    decorated.reserve(pool.size());
    for (const auto& atom : pool) decorated.emplace_back(to_utf32(atom).size(), atom);
    std::stable_sort(decorated.begin(), decorated.end(),
                    [](const std::pair<std::size_t, std::string>& a,
                       const std::pair<std::size_t, std::string>& b) {
                        return a.first > b.first;
                    });
    std::vector<std::string> atoms;
    atoms.reserve(decorated.size());
    for (auto& entry : decorated) atoms.push_back(std::move(entry.second));
    return atoms;
}

/// inventory.py _base_emissions: every IPA string the *within-word* tables can
/// place on a slot.
std::set<std::string> base_emissions(const LanguageSpec& spec) {
    std::set<std::string> emissions;

    for (const auto& entry : spec.graphemes)
        emissions.insert(entry.second.begin(), entry.second.end());

    for (const auto& reading : positional_readings(spec)) emissions.insert(reading);

    for (const auto& entry : spec.allophones)
        emissions.insert(entry.second.begin(), entry.second.end());

    for (const auto& rule : spec.allophone_rules) {
        if (!rule.surface.empty()) emissions.insert(rule.surface);
        if (!rule.append.empty()) {
            // An inserting rule emits the inserted segment; the phoneme it
            // attaches to is already an emission of the grapheme table.
            emissions.insert(rule.append);
        }
    }

    // Word-level overrides hand the engine whole transcriptions, so their IPA
    // never passes through the grapheme table at all.
    for (const auto& entry : spec.word_exceptions) emissions.insert(entry.second);

    // Suffix morphology replaces the word's tail wholesale, so its IPA also
    // bypasses the grapheme table. An ambiguous ending declares an ordered
    // candidate LIST and every element of it can surface, so the whole list is
    // an emission source; a deferring rank 1 (null) emits nothing of its own.
    for (const auto& entry : spec.grammatical_endings) {
        std::optional<std::string> rank1;
        std::vector<std::string> alternatives;
        beam::normalize_ending_value(entry.second, rank1, alternatives);
        if (rank1.has_value()) emissions.insert(*rank1);
        emissions.insert(alternatives.begin(), alternatives.end());
    }

    // The sidecar lexicon (caller-registered, never bundled) carries arbitrary
    // IPA per word, exactly like word_exceptions.
    for (const auto& entry : get_lexicon(spec.code)) emissions.insert(entry.second);

    if (spec.stress_defined)
        emissions.insert(STRESS_MARKS.begin(), STRESS_MARKS.end());

    emissions.erase("");
    return emissions;
}

/// inventory.py _sandhi_emissions: the segments cross-word `sandhi_rules` can
/// insert into the stream. Sandhi fires *after* the word tables, between words,
/// so anything its `transform`/`right_transform` adds is a symbol source the
/// base tables never saw. The literal inserted text is cut with `segment_ipa`
/// against the BASE atoms (so a re-emitted affricate stays whole and the tie
/// stands as its own segment), exactly as tokenize() will cut a real
/// transcription.
std::set<std::string> sandhi_emissions(const LanguageSpec& spec, const std::set<std::string>& base) {
    std::set<std::string> out;
    if (spec.sandhi_rules.empty()) return out;
    const std::vector<std::string> atoms = atoms_from(base, spec.phonemes);
    for (const auto& rule : spec.sandhi_rules) {
        for (const std::optional<std::string>* transform : {&rule.transform, &rule.right_transform}) {
            // _transform_literals returns "" for a rule with no transform, and
            // segment_ipa("") yields no segments, so an absent one contributes
            // nothing.
            const std::string literal = transform_literals(transform->value_or(std::string()));
            for (const auto& segment : segment_ipa(literal, atoms))
                if (!segment.empty() && !is_space(segment)) out.insert(segment);
        }
    }
    out.erase("");
    return out;
}

/// inventory.py _atoms: the atom list of the FULL emission inventory (the one
/// tokenize() and phoneme_atoms() cut with).
std::vector<std::string> atoms_of(const LanguageSpec& spec) {
    return atoms_from(emission_inventory(spec), spec.phonemes);
}

/// inventory.py _split_on_stress: split *ipa* so each stress mark stands alone.
std::vector<std::string> split_on_stress(const std::string& ipa) {
    std::vector<std::string> out;
    std::u32string buf;
    for (const char32_t cp : to_utf32(ipa)) {
        if (is_stress_mark(cp)) {
            if (!buf.empty()) {
                out.push_back(to_utf8(buf));
                buf.clear();
            }
            out.push_back(to_utf8(std::u32string(1, cp)));
        } else {
            buf += cp;
        }
    }
    if (!buf.empty()) out.push_back(to_utf8(buf));
    return out;
}

} // namespace

std::set<std::string> emission_inventory(const LanguageSpec& spec) {
    std::set<std::string> base = base_emissions(spec);
    for (const auto& segment : sandhi_emissions(spec, base)) base.insert(segment);
    return base;
}

std::set<std::string> phoneme_inventory(const LanguageSpec& spec) {
    std::set<std::string> tokens;
    for (const auto& emission : emission_inventory(spec))
        for (const auto& token : tokenize(emission, spec)) tokens.insert(token);
    return tokens;
}

std::vector<std::string> phoneme_atoms(const LanguageSpec& spec) {
    return atoms_of(spec);
}

std::vector<std::string> tokenize(const std::string& ipa, const LanguageSpec& spec) {
    const std::vector<std::string> atoms = atoms_of(spec);
    std::vector<std::string> tokens;
    for (const auto& chunk : split_on_stress(ipa)) {
        if (is_stress_mark(chunk)) {
            tokens.push_back(chunk);
            continue;
        }
        for (const auto& segment : segment_ipa(chunk, atoms))
            if (!segment.empty() && !is_space(segment)) tokens.push_back(segment);
    }
    return tokens;
}

std::vector<std::string> dead_allophone_rules(const LanguageSpec& spec) {
    // Reachability is checked against BOTH inventories, because the rescorer
    // matches both ways: a rule's target is compared against a slot's whole IPA
    // *and*, for segmental rules, against the segments within it. So a rule on
    // `a` is live in a spec whose only source of `a` is the emission `ja`, and a
    // rule on `dʒa` is live only if some slot emits exactly that.
    std::set<std::string> producible = emission_inventory(spec);
    for (const auto& token : phoneme_inventory(spec)) producible.insert(token);

    // An abugida adds one more source the tables do not show: a consonant
    // letter carries its inherent vowel unless a matra or a virama cancels it,
    // so a slot can hold `kə` in a spec whose grapheme table only ever writes
    // `k`. The snapshot is the reference's `tuple(producible)` — the added
    // readings must not feed the same pass again.
    if (spec.script_type == "abugida" && !spec.inherent_vowel.empty()) {
        const std::vector<std::string> snapshot(producible.begin(), producible.end());
        for (const auto& phoneme : snapshot)
            producible.insert(phoneme + spec.inherent_vowel);
    }

    std::vector<std::string> dead;
    for (const auto& rule : spec.allophone_rules) {
        // A rule with no phonemes targets nothing, so `if targets` is false and
        // it is never reported dead.
        if (rule.phonemes.empty()) continue;
        bool reachable = false;
        for (const auto& target : rule.phonemes) {
            if (producible.count(target) == 0) continue;
            reachable = true;
            break;
        }
        if (!reachable) dead.push_back(rule.id);
    }
    return dead;
}

} // namespace orthography2ipa::inventory
