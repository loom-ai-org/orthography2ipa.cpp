// Direct port of orthography2ipa/lm.py: the phone-sequence language model.
//
// The three functions here are the reference's only implementation of
// phoneme_embeddings / build_ngram_lm / perplexity; feats.py:606, :618 and
// :630 are deprecated aliases that import and call them, so the C++ feats::
// forwarders in src/feats.cpp resolve to this file.
#include "orthography2ipa/lm.hpp"

#include "orthography2ipa/feats.hpp"
#include "orthography2ipa/orthography2ipa.hpp"
#include "orthography2ipa/phonetok.hpp"
#include "unicode_util.hpp"

#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

namespace orthography2ipa::lm {
namespace {

/// lm.py does `phones = ["<s>"] + list(paths[0].ipa) + ["</s>"]` in BOTH
/// build_ngram_lm and perplexity: the top beam reading, split into CODE
/// POINTS (so "tʃ" is two phones), bracketed by the sentence symbols.
/// Returns false when the beam found no path at all — the reference's
/// `if not paths: continue`. A word whose reading is empty still yields a
/// path (""), hence a two-phone sequence.
bool sentence_phones(const PhonetokTokenizer& tok, const std::string& word,
                     Context* phones) {
    const std::vector<IPAPath> paths = tok.ipa_beam(word, 1);
    if (paths.empty()) return false;
    phones->clear();
    phones->emplace_back(START_PHONE);
    for (const char32_t cp : uni::to_utf32(paths.front().ipa))
        phones->push_back(uni::to_utf8(std::u32string(1, cp)));
    phones->emplace_back(END_PHONE);
    return true;
}

/// The reference does not validate *n*: `range(len(phones) - n + 1)` and
/// `phones[i + n - 1]` read from the END of the list once n < 1, which
/// counts nothing an n-gram model could mean. A C++ port has no negative
/// indexing to imitate, so the order is checked instead.
void check_order(int n) {
    if (n < 1)
        throw std::invalid_argument("n-gram order must be at least 1, got " + std::to_string(n));
}

} // namespace

std::map<std::string, std::vector<double>> phoneme_embeddings(const LanguageSpec& spec) {
    // lm.py: the phoneme set is the union of the spec's IPA readings —
    // `{p for ipa_list in spec.graphemes.values() for p in ipa_list}`.
    std::set<std::string> phonemes;
    for (const auto& reading : spec.graphemes)
        for (const auto& phone : reading.second) phonemes.insert(phone);

    // Python iterates a set, so its insertion order is arbitrary; the map is
    // ordered by phoneme for a walkable result. Unknown phonemes are skipped
    // (`except ValueError: pass`), i.e. feats::vectorize_phones found nothing.
    std::map<std::string, std::vector<double>> embeddings;
    for (const auto& phone : phonemes) {
        const std::vector<feats::TriState> vec = feats::vectorize_phones(phone);
        if (vec.empty()) continue;
        std::vector<double> row(vec.size());
        for (std::size_t i = 0; i < vec.size(); ++i)
            row[i] = vec[i] == feats::UNKNOWN ? 0.5 : static_cast<double>(vec[i]);
        embeddings.emplace(phone, std::move(row));
    }
    return embeddings;
}

NgramLM build_ngram_lm(const std::vector<std::string>& words, const LanguageSpec& spec, int n) {
    check_order(n);
    // lm.py builds its own tokenizer per call, with the reference's defaults
    // (add_bos/add_eos off, whitespace collapsed).
    const PhonetokTokenizer tok(spec);
    NgramLM lm;
    Context phones;
    const int order = n - 1;  // the context length: tuple(phones[i:i + n - 1])
    for (const auto& word : words) {
        if (!sentence_phones(tok, word, &phones)) continue;
        const int length = static_cast<int>(phones.size());
        for (int i = 0; i < length - n + 1; ++i)
            lm.counts[Context(phones.begin() + i, phones.begin() + i + order)]
                     [phones[static_cast<std::size_t>(i + order)]] += 1;
    }
    return lm;
}

double perplexity(const NgramLM& lm, const std::vector<std::string>& test_words,
                  const LanguageSpec& spec, int n) {
    check_order(n);
    const PhonetokTokenizer tok(spec);
    double log_prob = 0.0;
    long long total = 0;
    Context phones;
    const int order = n - 1;
    for (const auto& word : test_words) {
        if (!sentence_phones(tok, word, &phones)) continue;
        const int length = static_cast<int>(phones.size());
        for (int i = n - 1; i < length; ++i) {
            const Context ctx(phones.begin() + (i - order), phones.begin() + i);
            // lm.get(ctx, Counter()) — an unseen context contributes only the
            // smoothing, and the phone need not have been seen in it.
            const Counts* dist = lm.context(ctx);
            long long seen = 0;
            long long distinct = 0;
            long long next_count = 0;
            if (dist) {
                for (const auto& entry : *dist) seen += entry.second;
                distinct = static_cast<long long>(dist->size());
                const auto next_it = dist->find(phones[static_cast<std::size_t>(i)]);
                if (next_it != dist->end()) next_count = next_it->second;
            }
            // add-1 (Laplace) smoothing, in the reference's exact shape:
            // total_ct = sum(dist.values()) + len(dist) + 1
            const double total_ct = static_cast<double>(seen + distinct + 1);
            const double p = static_cast<double>(next_count + 1) / total_ct;
            log_prob += std::log2(p);
            ++total;
        }
    }
    if (total == 0) return std::numeric_limits<double>::infinity();
    return std::pow(2.0, -log_prob / static_cast<double>(total));
}

} // namespace orthography2ipa::lm
