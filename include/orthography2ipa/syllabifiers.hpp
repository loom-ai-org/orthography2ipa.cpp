// Direct port of orthography2ipa/syllabifiers.py — the plugin implementations
// the package itself ships.
//
// SilabificadorSyllabifier is DIFFERENT from every other port in one respect:
// the reference wraps a third-party Python package (`silabificador`, installed
// via the optional `orthography2ipa[portuguese]` extra) and the reference's own
// module fails to IMPORT when that package is absent — discovery then carries
// on without it, which is why the built-in splitter must still be right on its
// own (pt-PT's declared `diphthongs`). C++ has no entry-point machinery to
// reach a Python package, so this port mirrors the MEANING rather than the
// wrapping: the plugin exists, claims the Portuguese lects, sits at priority
// 50 ("neutral — a downstream plugin that knows better wins by saying so"),
// and its construction either delegates to a caller-supplied syllabify
// function or fails loudly with the same "the 'silabificador' package is not
// present" class of error the reference's import raises.
#pragma once

#include "orthography2ipa/orthography2ipa.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace orthography2ipa {
namespace syllabifiers {

/// syllabifiers.py SilabificadorSyllabifier: Portuguese syllables from the
/// external `silabificador` package. Register the provider BEFORE construction
/// (a binding set by whatever resolved the libraries at startup — a dlopen of
/// a real C implementation, a language server, a test); a construction without
/// a provider throws std::runtime_error, which is the reference's ImportError
/// on `from silabificador import Syllabifier`.
class SilabificadorSyllabifier : public SyllabifierPlugin {
public:
    using Provider = std::function<std::vector<std::string>(const std::string& word)>;

    SilabificadorSyllabifier();

    std::vector<std::string> language_codes() const override;
    std::vector<std::string> syllabify(const std::string& word,
                                       const std::string& lang) const override;
    int priority() const override { return 50; }   // neutral

    /// Point the plugin at an implementation: returns true when the provider
    /// was accepted. Unregistered engines keep the reference's ImportError
    /// behavior (a loud construction failure).
    static bool provide(Provider provider);

private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};

} // namespace syllabifiers
} // namespace orthography2ipa
