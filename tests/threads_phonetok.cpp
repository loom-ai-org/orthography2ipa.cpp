// Four threads share one const PhonetokTokenizer of the pt-PT spec. pt-PT has
// stress.closed_hiatus, so every tokenization reads and fills the tokenizer's
// closed-hiatus cache. Each thread tokenizes a word list that all threads use
// and a word list that only it uses. Under -fsanitize=thread a data race fails
// the test. In every build the tokens of each thread must equal the tokens of
// a tokenizer that one thread uses alone.
#include <iostream>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include "orthography2ipa/orthography2ipa.hpp"
#include "orthography2ipa/phonetok.hpp"

namespace {

using orthography2ipa::PhonetokTokenizer;

std::string tokens(const PhonetokTokenizer& tok, const std::string& word) {
    std::string out;
    for (const auto& t : tok.grapheme_tokens(word)) {
        out += t.grapheme + "@" + std::to_string(t.position) + ":" + std::to_string(t.length) + "=";
        for (const auto& ipa : t.ipa) out += ipa + ",";
        out += " ";
    }
    return out;
}

std::string graphemes(const PhonetokTokenizer& tok, const std::string& word) {
    std::string out;
    for (const auto& t : tok.grapheme_tokens(word)) out += (out.empty() ? "" : " ") + t.grapheme;
    return out;
}

}  // namespace

int main() {
    const auto& spec = orthography2ipa::get("pt-PT");
    const std::vector<std::string> stems = {"rainha", "saia", "juiz",  "moinho", "raiz",  "baile",
                                            "cair",   "caixa", "paul", "ruindade", "coimbra", "ainda"};
    constexpr int n_threads = 4;
    std::vector<std::string> shared;
    for (const auto& s : stems)
        for (char c = 'a'; c <= 'z'; ++c) shared.push_back(s + c);
    std::vector<std::vector<std::string>> words(n_threads, shared);
    for (int t = 0; t < n_threads; ++t)
        for (const auto& s : stems)
            for (int r = 0; r < 60; ++r)
                words[t].push_back(s + static_cast<char>('a' + t) + static_cast<char>('a' + r % 26) +
                                   static_cast<char>('a' + r / 26));

    const PhonetokTokenizer tok(spec);
    std::vector<std::vector<std::string>> threaded(n_threads);
    std::vector<std::thread> threads;
    for (int t = 0; t < n_threads; ++t)
        threads.emplace_back([&, t] {
            for (int round = 0; round < 2; ++round)
                for (const auto& w : words[t]) threaded[t].push_back(tokens(tok, w));
        });
    for (auto& th : threads) th.join();

    int failures = 0;
    std::size_t checked = 0;
    for (int t = 0; t < n_threads; ++t) {
        const PhonetokTokenizer alone(spec);
        std::map<std::string, std::string> serial;
        for (const auto& w : words[t]) serial[w] = tokens(alone, w);
        for (std::size_t i = 0; i < threaded[t].size(); ++i) {
            const std::string& w = words[t][i % words[t].size()];
            ++checked;
            if (threaded[t][i] != serial[w]) {
                if (failures < 10)
                    std::cerr << "thread " << t << " " << w << ": thread gave \"" << threaded[t][i]
                              << "\", serial gave \"" << serial[w] << "\"\n";
                ++failures;
            }
        }
    }
    // The words reach the closed-hiatus split: the reference phonetok.py
    // gives these graphemes for pt-PT.
    const std::map<std::string, std::string> split = {
        {"rainha", "r a i nh a"}, {"juiz", "j u i z"}, {"saia", "s ai a"}};
    for (const auto& [word, want] : split) {
        const std::string got = graphemes(tok, word);
        if (got != want) {
            std::cerr << word << ": got \"" << got << "\", want \"" << want << "\"\n";
            ++failures;
        }
    }
    std::cout << n_threads << " threads, " << checked << " tokenizations, " << failures << " failures\n";
    return failures ? 1 : 0;
}
