// tests/_scratch: catalog driver for the inventory parity harness.
//
// Reads one spec code per line on stdin and prints one JSON line per code:
//   {"code": ..., "emissions": [...], "tokens": [...], "atoms": [...],
//    "dead": [...], "tokmap": {emission: [tokens...]}}
// With `--tokenize` it instead reads "code<TAB>ipa" lines and prints
//   {"code": ..., "ipa": ..., "tokens": [...]}
// Both modes compare orthography2ipa::inventory against the reference's
// inventory.py under tests/inventory_parity.py.
#include "orthography2ipa/inventory.hpp"
#include "orthography2ipa/orthography2ipa.hpp"

#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <chrono>
#include <vector>

namespace {

std::string json_string(const std::string& text) {
    std::string out = "\"";
    for (const unsigned char c : text) {
        if (c == 0x22) out += "\\\"";
        else if (c == 0x5C) out += "\\\\";
        else if (c < 0x20) {
            static const char* hex = "0123456789abcdef";
            out += "\\u00";
            out += hex[(c >> 4) & 0xf];
            out += hex[c & 0xf];
        } else {
            out += static_cast<char>(c);
        }
    }
    out += "\"";
    return out;
}

std::string json_array(const std::vector<std::string>& items) {
    std::string out = "[";
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i) out += ",";
        out += json_string(items[i]);
    }
    out += "]";
    return out;
}

std::vector<std::string> sorted_vector(const std::set<std::string>& items) {
    return std::vector<std::string>(items.begin(), items.end());
}

int catalog_mode() {
    std::string code;
    while (std::getline(std::cin, code)) {
        if (code.empty()) continue;
        std::ostringstream line;
        try {
            const orthography2ipa::LanguageSpec& spec = orthography2ipa::get(code);
            const std::set<std::string> emissions = orthography2ipa::inventory::emission_inventory(spec);
            const std::set<std::string> tokens = orthography2ipa::inventory::phoneme_inventory(spec);
            const std::vector<std::string> atoms = orthography2ipa::inventory::phoneme_atoms(spec);
            const std::vector<std::string> dead = orthography2ipa::inventory::dead_allophone_rules(spec);
            line << "{\"code\":" << json_string(code)
                 << ",\"emissions\":" << json_array(sorted_vector(emissions))
                 << ",\"tokens\":" << json_array(sorted_vector(tokens))
                 << ",\"atoms\":" << json_array(atoms)
                 << ",\"dead\":" << json_array(dead)
                 << ",\"tokmap\":{";
            bool first = true;
            for (const auto& emission : emissions) {
                if (!first) line << ",";
                first = false;
                line << json_string(emission) << ":"
                     << json_array(orthography2ipa::inventory::tokenize(emission, spec));
            }
            line << "},\"ok\":true}";
        } catch (const std::exception& error) {
            line << "{\"code\":" << json_string(code) << ",\"ok\":false,\"error\":"
                 << json_string(error.what()) << "}";
        }
        std::cout << line.str() << "\n";
        std::cout.flush();
    }
    return 0;
}

int tokenize_mode() {
    std::string row;
    while (std::getline(std::cin, row)) {
        if (row.empty()) continue;
        const std::size_t tab = row.find('\t');
        const std::string code = tab == std::string::npos ? row : row.substr(0, tab);
        const std::string ipa = tab == std::string::npos ? std::string() : row.substr(tab + 1);
        std::ostringstream line;
        try {
            const orthography2ipa::LanguageSpec& spec = orthography2ipa::get(code);
            line << "{\"code\":" << json_string(code) << ",\"ipa\":" << json_string(ipa)
                 << ",\"tokens\":"
                 << json_array(orthography2ipa::inventory::tokenize(ipa, spec)) << ",\"ok\":true}";
        } catch (const std::exception& error) {
            line << "{\"code\":" << json_string(code) << ",\"ok\":false,\"error\":"
                 << json_string(error.what()) << "}";
        }
        std::cout << line.str() << "\n";
        std::cout.flush();
    }
    return 0;
}

} // namespace


int time_mode() {
    std::string code;
    while (std::getline(std::cin, code)) {
        if (code.empty()) continue;
        const auto t0 = std::chrono::steady_clock::now();
        const orthography2ipa::LanguageSpec* spec = nullptr;
        try { spec = &orthography2ipa::get(code); } catch (...) {}
        const auto t1 = std::chrono::steady_clock::now();
        std::set<std::string> emissions;
        std::set<std::string> tokens;
        std::vector<std::string> atoms;
        std::vector<std::string> dead;
        if (spec) {
            emissions = orthography2ipa::inventory::emission_inventory(*spec);
            const auto t2 = std::chrono::steady_clock::now();
            tokens = orthography2ipa::inventory::phoneme_inventory(*spec);
            const auto t3 = std::chrono::steady_clock::now();
            atoms = orthography2ipa::inventory::phoneme_atoms(*spec);
            const auto t4 = std::chrono::steady_clock::now();
            dead = orthography2ipa::inventory::dead_allophone_rules(*spec);
            const auto t5 = std::chrono::steady_clock::now();
            std::size_t tok_calls = 0;
            for (const auto& e : emissions) { orthography2ipa::inventory::tokenize(e, *spec); ++tok_calls; }
            const auto t6 = std::chrono::steady_clock::now();
            auto ms = [](const std::chrono::steady_clock::time_point& a,
                         const std::chrono::steady_clock::time_point& b) {
                return (long long)std::chrono::duration_cast<std::chrono::milliseconds>(b - a).count();
            };
            std::cerr << code << "\t" << ms(t0, t1) << "\t" << ms(t1, t2) << "\t"
                      << ms(t2, t3) << "\t" << ms(t3, t4) << "\t" << ms(t4, t5) << "\t"
                      << ms(t5, t6) << "\t" << emissions.size() << "\t" << atoms.size()
                      << "\n";
        }
    }
    return 0;
}

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--time") return time_mode();
    const bool tokenize_mode_requested = argc > 1 && std::string(argv[1]) == "--tokenize";
    return tokenize_mode_requested ? tokenize_mode() : catalog_mode();
}
