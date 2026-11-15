#pragma once

// Direct port of orthography2ipa/markup.py — forcing a pronunciation with
// ``<phoneme alphabet="ipa" ph="...">spelling</phoneme>``.
//
// parse_markup() splits the input into plain runs and words whose reading the
// CALLER already knows. Both halves of the element are load-bearing: `ph` is
// the IPA to use verbatim, and the element's text is the SPELLING, which
// cross-word rules still read. The port reproduces the reference's regexes
// over CODE POINTS (src/codepoint_regex.hpp) and its error messages
// character for character, so a TTS frontend sees the same failures.
//
// Nothing here checks `ph` against a phoneme inventory: that is the pipeline's
// job (markup.py documents it as the inventory constraint, and the check lives
// where the spec is).
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace orthography2ipa::markup {

/// markup.py MarkupError(ValueError): the markup is malformed. The message
/// says what, and where. std::invalid_argument is the C++ ValueError.
class MarkupError : public std::invalid_argument {
public:
    explicit MarkupError(const std::string& message) : std::invalid_argument(message) {}
};

/// markup.py Chunk: a run of the input, either plain text or a word with a
/// forced reading. A frozen dataclass, so it compares by value.
struct Chunk {
    std::string text;
    /// The IPA to use verbatim, or absent to transcribe *text* by the rules.
    std::optional<std::string> forced_ipa;

    Chunk() = default;
    explicit Chunk(std::string text, std::optional<std::string> forced_ipa = std::nullopt);

    /// Chunk.is_forced (a @property in the reference).
    bool is_forced() const { return forced_ipa.has_value(); }

    bool operator==(const Chunk& other) const;
};

/// markup.py parse_markup: split *text* into plain runs and
/// forced-pronunciation words. MarkupError on a <phoneme> with no `ph`, an
/// empty `ph`, an `alphabet` other than "ipa", a tag wrapping no text, or an
/// unclosed tag.
std::vector<Chunk> parse_markup(const std::string& text);

} // namespace orthography2ipa::markup
