#pragma once

// Direct port of orthography2ipa/lm.py: the language-model surface over IPA
// phone sequences — feature-vector embeddings of a spec's phonemes, an
// n-gram count table built from the tokenizer's top reading of each word,
// and add-1 (Laplace) smoothed perplexity over that table.
//
// feats.py keeps deprecated aliases of all three names (feats.py:606, :618,
// :630) that import and call the lm.py implementation, so the C++ feats::
// names are thin forwarders to the functions declared here: there is one
// implementation of each, exactly as in the reference.
//
// The phone sequence is the reference's own construction. lm.py takes the
// beam's top path and does `list(paths[0].ipa)`, which splits the JOINED IPA
// string into CODE POINTS, so a reading spelled by two symbols ("tʃ") indexes
// as two phones; positions, contexts and membership are therefore counted in
// code points (src/unicode_util.hpp), never in UTF-8 bytes.
#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace orthography2ipa {
struct LanguageSpec;  // defined in orthography2ipa/orthography2ipa.hpp
}

namespace orthography2ipa::lm {

/// lm.py's sentence brackets — the phones that open and close every word's
/// phone sequence ("<s>" / "</s>", not the tokenizer's <bos>/<eos>).
constexpr const char* START_PHONE = "<s>";
constexpr const char* END_PHONE = "</s>";

/// One (n-1)-gram context: its phones in order. A phone is one code point of
/// the top reading, or one of the sentence brackets.
using Context = std::vector<std::string>;

/// The reference's `Counter`: next phone -> how often it followed a context.
using Counts = std::map<std::string, long long>;

/// lm.py build_ngram_lm's return value, `dict[tuple[str, ...], Counter]`.
///
/// The table is ordered by context so it can be walked deterministically
/// (a Python defaultdict iterates in first-seen order). Perplexity only ever
/// looks a context up and adds integer counts, so the ordering cannot change
/// a number: the counts and the arithmetic ORDER inside perplexity are what
/// make the score reproducible.
struct NgramLM {
    std::map<Context, Counts> counts;

    /// `lm.get(ctx, Counter())`: nullptr when the context was never seen.
    const Counts* context(const Context& ctx) const {
        const auto it = counts.find(ctx);
        return it == counts.end() ? nullptr : &it->second;
    }
    /// len(lm): the number of distinct contexts.
    std::size_t size() const { return counts.size(); }
    bool empty() const { return counts.empty(); }
};

/// lm.py phoneme_embeddings: every phoneme the spec's grapheme table reads
/// to, mapped to its feats.py 23-feature vector as doubles — an undefined
/// feature becomes 0.5 (Python's `0.5 if v is None else float(v)`), a known
/// one 0.0 or 1.0. Phonemes the feature table does not know are silently
/// skipped, as in the reference.
std::map<std::string, std::vector<double>> phoneme_embeddings(const LanguageSpec& spec);

/// lm.py build_ngram_lm: count the (n-1)-gram contexts of every word's top
/// reading (`ipa_beam(word, beam_width=1)`) bracketed by <s>/</s>. A word
/// with no path contributes nothing. *n* is the n-gram order (3 = trigram).
/// Throws std::invalid_argument for n < 1: the reference does not validate
/// *n*, and its `range(len(phones) - n + 1)` would index from the END of the
/// phone list at n == 0 — a spelling accident, not a model.
NgramLM build_ngram_lm(const std::vector<std::string>& words, const LanguageSpec& spec,
                       int n = 3);

/// lm.py perplexity: `2 ** (-log_prob / total)` over *test_words*, each
/// conditional probability add-1 smoothed with the reference's denominator
/// `sum(counts) + len(counts) + 1`. Returns +inf when no token was found.
double perplexity(const NgramLM& lm, const std::vector<std::string>& test_words,
                  const LanguageSpec& spec, int n = 3);

} // namespace orthography2ipa::lm
