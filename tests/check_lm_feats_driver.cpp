// Standalone validation program for the lm.py / feats.py distance-and-LM port
// (src/lm.cpp and the appended half of src/feats.cpp). It reads the SAME
// battery and case files the reference dump (tests/lm_feats_ref.py) wrote to
// tests/_scratch/, computes the ported functions over them, and writes
// parallel files for tests/lm_feats_compare.py to compare. tests/lm_parity.py
// runs the three steps; CMake builds this file as orthography2ipa-check-lm_feats.
#include "orthography2ipa/feats.hpp"
#include "orthography2ipa/lm.hpp"
#include "orthography2ipa/orthography2ipa.hpp"
#include "orthography2ipa/phonetok.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace {

const char* kScratch = "tests/_scratch/";
const char* kSep = "\x1f";   // context-phone separator (never inside a phone)

std::string scratch(const std::string& name) { return std::string(kScratch) + name; }

std::vector<std::string> read_lines(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::cerr << "cannot read " << path << "\n";
        std::exit(2);
    }
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
    }
    return lines;
}

std::string field(const std::string& line, std::size_t index) {
    std::size_t start = 0;
    for (std::size_t i = 0; i < index; ++i) {
        const std::size_t tab = line.find('\t', start);
        if (tab == std::string::npos) return "";
        start = tab + 1;
    }
    return line.substr(start);
}

std::string number(double value) {
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, "%.17g", value);
    return buffer;
}

/// The battery: line 0 is the count, the rest one phone per line.
std::vector<std::string> load_battery() {
    const std::vector<std::string> lines = read_lines(scratch("_lm_feats_battery.txt"));
    std::vector<std::string> phones(lines.begin() + 1, lines.end());
    if (static_cast<long>(phones.size()) != std::stol(lines[0])) {
        std::cerr << "battery count mismatch: header " << lines[0]
                  << ", body " << phones.size() << "\n";
        std::exit(2);
    }
    return phones;
}

struct Case {
    std::string lang;
    int n = 3;
    std::vector<std::string> train;
    std::vector<std::string> test;
};

std::vector<Case> load_cases() {
    const std::vector<std::string> lines = read_lines(scratch("_lm_feats_cases.txt"));
    std::vector<Case> cases;
    std::size_t i = 0;
    while (i < lines.size()) {
        if (lines[i] != "CASE") { ++i; continue; }
        Case c;
        c.lang = field(lines[i + 1], 1);
        c.n = std::stoi(field(lines[i + 2], 1));
        const std::size_t train_count = std::stoul(field(lines[i + 3], 1));
        i += 4;
        for (std::size_t k = 0; k < train_count; ++k) c.train.push_back(lines[i++]);
        const std::size_t test_count = std::stoul(field(lines[i], 1));
        ++i;
        for (std::size_t k = 0; k < test_count; ++k) c.test.push_back(lines[i++]);
        if (i < lines.size() && lines[i] == "END") ++i;
        cases.push_back(std::move(c));
    }
    return cases;
}

} // namespace

int main() {
    using namespace orthography2ipa;

    const std::vector<std::string> phones = load_battery();
    const std::size_t n = phones.size();
    std::cout << "battery phones: " << n << "\n";

    // --- vectorize_phones / is_vowel_phone -------------------------------
    {
        std::ofstream vec(scratch("_lm_feats_cpp_vec.txt"), std::ios::binary);
        std::ofstream vowel(scratch("_lm_feats_cpp_vowel.txt"), std::ios::binary);
        std::size_t unknown = 0, none_flag = 0;
        for (const auto& phone : phones) {
            const std::vector<feats::TriState> v = feats::vectorize_phones(phone);
            if (v.empty()) { vec << "E"; ++unknown; }
            else {
                for (std::size_t i = 0; i < v.size(); ++i)
                    vec << (i ? " " : "") << (v[i] == feats::UNKNOWN ? -1
                                                 : v[i] == feats::TRUE ? 1 : 0);
            }
            vec << "\n";
            const feats::TriState flag = feats::vowel_phone_flag(phone);
            vowel << (flag == feats::TRUE ? "T" : flag == feats::FALSE ? "F" : "N") << "\n";
            if (flag == feats::UNKNOWN) ++none_flag;
        }
        std::cout << "vectorize unknown (ValueError): " << unknown
                  << ", undefined feature[0]: " << none_flag << "\n";
    }

    // --- phonetic_distance over every ordered pair ------------------------
    {
        std::ofstream bin(scratch("_lm_feats_cpp_fdist.bin"),
                          std::ios::binary | std::ios::trunc);
        double max_distance = 0.0;
        std::uint64_t bytes = 0;
        for (std::size_t i = 0; i < n; ++i) {
            for (std::size_t j = 0; j < n; ++j) {
                const double d = feats::phonetic_distance(phones[i], phones[j]);
                bin.write(reinterpret_cast<const char*>(&d), sizeof d);
                if (d > max_distance) max_distance = d;
            }
        }
        bytes = static_cast<std::uint64_t>(n) * n * sizeof(double);
        std::cout << "phonetic_distance pairs: " << n * n << ", bytes: " << bytes
                  << ", max: " << number(max_distance) << "\n";
    }

    // --- phoneme_embeddings ----------------------------------------------
    {
        static const char* kEmbLangs[] = {"en-US", "fr-FR", "pt-PT", "de-DE", "hi", "ar",
                                          "th", "my", "ja", "ru", "tr", "ta", "bn", "kw", "dz",
                                          // specs reading to the multi-code-point table spellings
                                          // (ᵐb, ᵑɡ, tsʼ, tʃʼ): the case the fast path covers
                                          "ags", "am", "bbl"};
        std::ofstream out(scratch("_lm_feats_cpp_embed.txt"), std::ios::binary);
        for (const char* lang : kEmbLangs) {
            std::map<std::string, std::vector<double>> emb;
            try {
                emb = lm::phoneme_embeddings(orthography2ipa::get(lang));
            } catch (const std::exception& e) {
                out << "EMB " << lang << " ERROR \"" << e.what() << "\"\n";
                continue;
            }
            out << "EMB " << lang << " " << emb.size() << "\n";
            for (const auto& entry : emb) {
                out << entry.first << "\t";
                for (std::size_t i = 0; i < entry.second.size(); ++i)
                    out << (i ? "\t" : "") << number(entry.second[i]);
                out << "\n";
            }
        }
    }

    // --- build_ngram_lm / perplexity --------------------------------------
    {
        const std::vector<Case> cases = load_cases();
        std::ofstream out(scratch("_lm_feats_cpp_lm.txt"), std::ios::binary);
        for (const Case& c : cases) {
            const LanguageSpec& spec = orthography2ipa::get(c.lang);
            const lm::NgramLM table = lm::build_ngram_lm(c.train, spec, c.n);
            const double ppl = lm::perplexity(table, c.test, spec, c.n);
            long long seen = 0;
            for (const auto& entry : table.counts)
                for (const auto& count : entry.second) seen += count.second;
            std::vector<std::pair<std::string, const lm::Counts*>> rows;
            for (const auto& entry : table.counts) {
                std::string joined;
                for (const auto& phone : entry.first) joined += (joined.empty() ? "" : kSep) + phone;
                rows.emplace_back(joined, &entry.second);
            }
            std::sort(rows.begin(), rows.end(),
                      [](const std::pair<std::string, const lm::Counts*>& a,
                         const std::pair<std::string, const lm::Counts*>& b) {
                          return a.first < b.first;
                      });
            const PhonetokTokenizer tok(spec);
            out << "CASE\t" << c.lang << "/" << c.n << "\t" << table.size() << "\t" << seen
                << "\t" << number(ppl) << "\n";
            for (const auto& row : rows) {
                out << "C\t" << row.first << "\n";
                out << "K\t" << row.second->size() << "\n";
                for (const auto& count : *row.second)
                    out << "S\t" << count.first << "\t" << count.second << "\n";
            }
            out << "P\t" << c.test.size() << "\n";
            for (std::size_t i = 0; i < c.test.size(); ++i) {
                const std::vector<IPAPath> paths = tok.ipa_beam(c.test[i], 1);
                out << "W\t" << i << "\t" << (paths.empty() ? "-" : paths.front().ipa) << "\n";
            }
            out << "END\n";
        }
        std::cout << "lm cases: " << cases.size() << "\n";
    }
    return 0;
}
