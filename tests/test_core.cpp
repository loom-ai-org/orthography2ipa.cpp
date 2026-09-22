#include "orthography2ipa/orthography2ipa.hpp"

#include <cassert>
#include <fstream>
#include <stdexcept>

using namespace orthography2ipa;

int main() {
    assert(resolve("por") == "pt-PT");
    assert(!available_codes().empty());

    const auto& portuguese = get("pt");
    const auto& spanish = get("es");
    assert(!portuguese.graphemes.empty());
    assert(!portuguese.family_path().empty());
    assert(!get("ga").sources.empty());
    assert(get("ga").timespan.has_value());

    Tokenizer tokenizer(portuguese);
    std::vector<std::string> unmapped;
    const auto tokens = tokenizer.tokenize_word("olá", &unmapped);
    assert(!tokens.empty() && unmapped.empty());
    const auto unicode_tokens = tokenizer.tokenize("CH 3☕.");
    assert(unicode_tokens.size() == 5);
    assert(unicode_tokens[0].kind == TokenKind::GRAPHEME && unicode_tokens[0].grapheme == "ch");
    assert(unicode_tokens[0].length == 2);
    assert(unicode_tokens[1].kind == TokenKind::WHITESPACE);
    assert(unicode_tokens[2].kind == TokenKind::DIGIT);
    assert(unicode_tokens[3].kind == TokenKind::UNKNOWN);
    assert(unicode_tokens[4].kind == TokenKind::PUNCTUATION);
    const auto normalized = tokenizer.tokenize("CAFE\xCC\x81");
    assert(normalized.size() == 4);
    assert(normalized.back().grapheme == "é");
    const auto context = tokenizer.tokenize_with_context("casa!");
    assert(context.graphemes.size() == 4);
    assert(context.graphemes[0].next()->grapheme() == "a");
    assert(context.graphemes[0].prev() == nullptr);
    assert(context.graphemes[3].next() == nullptr);
    assert(context.graphemes[0].span().first == 0 && context.graphemes[0].span().second == 1);
    const auto beam = tokenizer.beam("casa", 4);
    assert(!beam.empty() && beam.size() <= 4);

    G2P engine("pt");
    assert(!engine.transcribe_detailed("olá").words.empty());
    assert(!engine.lattice("casa").empty());
    assert(!engine.features("casa").empty());
    assert(engine.word_confidence("casa") > 0);

    bool missing_plugin = false;
    try { G2P missing("pt", {{"normalize", {"not-installed"}}}); missing.transcribe("casa"); }
    catch (const std::exception&) { missing_plugin = true; }
    assert(missing_plugin);

    const auto vector = feature_vector("p");
    assert(vector.size() == 23);
    assert(segment_distance("p", "p") == 0);
    assert(inventory_distance(portuguese, spanish).feature_mean >= 0);
    assert(spelling_divergence(portuguese, spanish).total_phonemes > 0);
    assert(full_distance(portuguese, spanish) >= 0);
    assert(pairwise_distances({portuguese, spanish}).size() == 2);

    const std::string path = "/tmp/orthography2ipa-cpp-test.tsv";
    { std::ofstream output(path); output << "casa\tkaza\n"; }
    register_lexicon("pt", path);
    assert(get_lexicon("pt").at("casa") == "kaza");
    clear_lexicons();

    const auto malformed = validate_lexicon("bad line\n");
    assert(!malformed.empty());
    const auto unicode_malformed = validate_lexicon("cafe\xCC\x81\tk\n");
    assert(!unicode_malformed.empty());
    return 0;
}
