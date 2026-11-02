// Direct port of orthography2ipa/exceptions.py — the public exception types
// that downstream consumers catch without importing the internal engine
// modules. Kept in its own header for the same reason the reference keeps the
// module: `UnmappedScriptError` is raised by the transcription engine and
// caught by callers that never see the engine.
#pragma once

#include <stdexcept>
#include <string>
#include <vector>

namespace orthography2ipa {

namespace detail {
/// CPython's `repr()` of a `str`. exceptions.py and schema.py both build their
/// messages with `{value!r}`, so the one implementation lives with the exception
/// types it serves and the schema port reuses it.
std::string py_repr(const std::string& utf8);
} // namespace detail

/// exceptions.py UnmappedScriptError(ValueError): raised when a word contains
/// characters absent from a spec's grapheme table and the G2P engine was
/// configured with `on_unmapped="raise"`.
///
/// `unmapped` is the reference's `Tuple[str, ...]` — the characters of *word*
/// with no grapheme mapping, in the order the tokenizer produced them.
class UnmappedScriptError : public std::runtime_error {
public:
    UnmappedScriptError(const std::string& word, const std::vector<std::string>& unmapped,
                        const std::string& lang);

    const std::string& word() const noexcept { return word_; }
    const std::vector<std::string>& unmapped() const noexcept { return unmapped_; }
    const std::string& lang() const noexcept { return lang_; }

private:
    std::string word_;
    std::vector<std::string> unmapped_;
    std::string lang_;
};

} // namespace orthography2ipa
