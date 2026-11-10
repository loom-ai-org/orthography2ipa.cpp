// underspecification.py port: which letters' vowels the writing did not say.
//
// The reference is three functions over a LanguageSpec's script_type and
// optional_marks; the positions it returns are CODE POINT indices, so the word
// is decoded once and scanned as std::u32string.
#include "orthography2ipa/underspecification.hpp"

#include "orthography2ipa/orthography2ipa.hpp"

#include "unicode_util.hpp"

#include <stdexcept>          // orthography2ipa.hpp uses std::runtime_error
#include <string>             // without including it
#include <vector>

namespace orthography2ipa::underspecification {
namespace {

using uni::is_alpha;
using uni::to_utf32;

/// _UNDERSPECIFYING_SCRIPTS = (ScriptType.ABJAD,). ScriptType is a str-Enum, so
/// membership is the value comparison the reference performs.
bool underspecifying_script(const std::string& script_type) {
    return script_type == "abjad";
}

/// Whether *ch* is one of spec.optional_marks. Each entry is a single combining
/// codepoint, and Python compares a one-character string against the whole
/// entry, so an entry of any other length matches no character.
bool is_mark(char32_t ch, const std::vector<std::string>& marks) {
    for (const auto& mark : marks) {
        const std::u32string decoded = to_utf32(mark);
        if (decoded.size() == 1 && decoded[0] == ch) return true;
    }
    return false;
}

/// _is_letter: a base letter — not one of the omissible marks, not punctuation.
bool is_letter(char32_t ch, const LanguageSpec& spec) {
    if (is_mark(ch, spec.optional_marks)) return false;
    return is_alpha(ch);
}

} // namespace

std::vector<std::size_t> underdetermined_positions(const std::string& word,
                                                   const LanguageSpec& spec) {
    if (spec.optional_marks.empty() || !underspecifying_script(spec.script_type))
        return {};

    const std::u32string text = to_utf32(word);
    std::vector<std::size_t> letters;
    for (std::size_t i = 0; i < text.size(); ++i)
        if (is_letter(text[i], spec)) letters.push_back(i);
    if (letters.empty()) return {};
    const std::size_t last_letter = letters.back();

    std::vector<std::size_t> positions;
    for (const std::size_t i : letters) {
        // The marks that belong to a letter follow it, so look ahead over the
        // run of marks sitting on this letter.
        bool carries_mark = false;
        for (std::size_t j = i + 1; j < text.size() && is_mark(text[j], spec.optional_marks); ++j)
            carries_mark = true;
        if (carries_mark) continue;

        // A mater lectionis needs no mark of its own: the mark BEFORE it is
        // what licenses it, so the reading is determined.
        if (i > 0 && is_mark(text[i - 1], spec.optional_marks)) continue;

        // The last letter needs no mark either: in the pausal form that is
        // actually spoken, a word-final consonant carries no ending.
        if (i == last_letter) continue;

        positions.push_back(i);
    }
    return positions;
}

bool is_underdetermined(const std::string& word, const LanguageSpec& spec) {
    return !underdetermined_positions(word, spec).empty();
}

double mark_density(const std::string& word, const LanguageSpec& spec) {
    if (spec.optional_marks.empty() || !underspecifying_script(spec.script_type))
        return 1.0;

    const std::u32string text = to_utf32(word);
    std::size_t letters = 0;
    for (const char32_t ch : text)
        if (is_letter(ch, spec)) ++letters;
    if (letters == 0) return 1.0;

    const double unmarked = static_cast<double>(underdetermined_positions(word, spec).size());
    const double total = static_cast<double>(letters);
    return (total - unmarked) / total;
}

} // namespace orthography2ipa::underspecification
