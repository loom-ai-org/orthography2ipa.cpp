// Direct port of orthography2ipa/conformance.py — what a plugin is allowed to
// change, and the kit that proves it does not change more.
//
//     A plugin refines an answer. It must not redefine the question.
//
// The syllabifier contract (conformance.py "The contract"):
//   1. Round-trip. A syllabifier splits a word; it does not rewrite it:
//      `"".join(syllabify(word)) == word`, always.
//   2. Language claim. A plugin speaks only for the codes it claims.
//   3. Downstream neutrality. Installing a plugin must not change any stage the
//      plugin does not own: transcribe the probe words with it and without it,
//      and the output must be identical.
//
// The rescorer contract inverts rule 3 — changing the transcription is the whole
// job — and enforces what remains: determinism and the declared inventory
// (rescorer_plugin.py's three rules; the language claim is enforced by the
// engine, which routes a declared `rescore` plugin only for a code it claims).
//
// Both checks raise ConformanceError with the reference's message text, which
// says which rule broke and why it matters: a plugin that cannot pass rule 3 is
// not a bad plugin, it is the library telling you its own built-in answer is
// wrong for that language.
//
// The seams differ from the reference in one place, and it is the C++ engine's
// own structure rather than a relaxed rule: the reference installs the plugin in
// `registry._syllabifiers` / `registry._declared["rescore"]` for the duration of
// the check and restores them afterwards. The C++ registry keys plugins by name
// and has no unregister, so the kit registers under its own name
// ("__conformance__", the reference's name too) and disables that entry on the
// way out — claiming no codes at all, which is what makes `get_syllabifier` and
// the declared-plugin lookup route nothing to it, i.e. the built-in answer runs.
#pragma once

#include <stdexcept>
#include <string>
#include <vector>

namespace orthography2ipa {
class RescorerPlugin;
class SyllabifierPlugin;
namespace rescorer { class LatticeRescorer; }

namespace conformance {

/// conformance.py ConformanceError(AssertionError): a plugin broke the contract.
/// The message says which rule, and why it matters.
class ConformanceError : public std::runtime_error {
public:
    explicit ConformanceError(const std::string& message) : std::runtime_error(message) {}
};

/// conformance.py DEFAULT_PROBE_WORDS: words used when the caller supplies
/// none. Deliberately awkward: hiatus, real diphthongs, written accents, a long
/// word, a clitic-looking one. A plugin that only ever sees `casa` proves
/// nothing.
const std::vector<std::string>& default_probe_words();

/// conformance.py assert_syllabifier_conforms: assert *plugin* keeps the
/// syllabifier contract, or explain what it broke.
///
/// Args:
///   plugin: the syllabifier to check.
///   words: probe words. Empty means default_probe_words(); a plugin author
///       should pass their own language's awkward cases — hiatus, real
///       diphthongs, written accents.
///   lang: the code to transcribe under. "" means the plugin's first claim.
///
/// Throws ConformanceError for a plugin with no claim and no *lang*, for a
/// syllabification that does not round-trip, and for a plugin that moves the
/// transcription.
void assert_syllabifier_conforms(const SyllabifierPlugin& plugin,
                                 const std::vector<std::string>& words = {},
                                 const std::string& lang = "");

/// conformance.py assert_rescorer_conforms: assert a declared rescore plugin
/// keeps its contract.
///
/// It MAY change the transcription — that is what it is for. It may not invent
/// a symbol, wander into another language, or answer differently twice. The
/// plugin is installed under the kit's own name and DECLARED to the engine
/// (`G2P(lang, plugins={"rescore": "__conformance__"})`), which is exactly how a
/// downstream engine composes its own pipeline; the two passes then run
/// `transcribe_word` over the probe words, the second with a fresh engine per
/// word, and every emitted symbol is read back against the spec's declared
/// inventory.
///
/// Note that the C++ engine enforces the same contract at this seam and raises
/// `std::runtime_error` itself ("rescore plugin does not own language …",
/// "… returned non-deterministic output …", "… emitted undeclared IPA …"), so a
/// plugin that breaks one of those three rules can be stopped by the engine
/// before the kit gets to describe it. Both reject; the engine's message names
/// the offending symbol or language instead of explaining the contract.
void assert_rescorer_conforms(const RescorerPlugin& plugin,
                              const std::vector<std::string>& words = {},
                              const std::string& lang = "");

/// conformance.py assert_rescorer_conforms, at the seam a rescorer actually
/// lives on: the shared lattice.
///
/// The reference's rescore plugins *contribute* `LatticeRescorer`s, which the
/// engine composes into its slot chain (user rescorers first, then the spec's
/// own allophone rules, which keep the last word). A bare LatticeRescorer claims
/// no language codes, so *lang* is the caller's — the code this rescorer is being
/// checked under. The probe words run through the lattice with the same chain
/// and the same two passes as the whole-word form; determinism and the declared
/// inventory are the two rules the reference checks, with the same messages.
void assert_rescorer_conforms(const rescorer::LatticeRescorer& plugin,
                              const std::string& lang,
                              const std::vector<std::string>& words = {});

} // namespace conformance
} // namespace orthography2ipa
