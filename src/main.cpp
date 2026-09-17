#include "orthography2ipa/orthography2ipa.hpp"
#include <iomanip>
#include <iostream>

using namespace orthography2ipa;

static void usage() {
    std::cerr << "usage: orthography2ipa [--data DIR] <list|info|transcribe|distance> ...\n";
}

int main(int argc, char** argv) {
    try {
        int i = 1;
        if (i + 1 < argc && std::string(argv[i]) == "--data") {
            set_data_directory(argv[i + 1]); i += 2;
        }
        if (i >= argc) { usage(); return 2; }
        const std::string command = argv[i++];
        if (command == "list") {
            bool families = i < argc && std::string(argv[i]) == "--families";
            if (families) {
                for (const auto& [family, codes] : available_families()) {
                    std::cout << family << ":";
                    for (const auto& code : codes) std::cout << " " << code;
                    std::cout << "\n";
                }
            } else for (const auto& code : available_codes()) std::cout << code << "\n";
            return 0;
        }
        if (command == "info" && i < argc) {
            const auto& s = get(argv[i]);
            std::cout << "Code: " << s.code << "\nName: " << s.name << "\n"
                      << "Family: " << s.family << "\nScript: " << s.script << "\n"
                      << "Graphemes: " << s.graphemes.size() << "\nAllophones: "
                      << s.allophones.size() << "\n";
            return 0;
        }
        if (command == "transcribe" && i + 1 < argc) {
            const std::string code = argv[i++];
            if (i + 1 < argc && std::string(argv[i]) == "--lexicon") {
                register_lexicon(code, argv[i + 1]); i += 2;
            }
            if (i >= argc) { usage(); return 2; }
            std::string text = argv[i++];
            while (i < argc) { text += " "; text += argv[i++]; }
            std::cout << G2P(code).transcribe(text) << "\n";
            return 0;
        }
        if (command == "distance" && i + 1 < argc) {
            const auto d = phonological_distance(get(argv[i]), get(argv[i + 1]));
            std::cout << std::fixed << std::setprecision(4)
                      << "combined: " << d.combined << "\n"
                      << "inventory: " << d.inventory.feature_mean << "\n"
                      << "allophone: " << d.allophone_sim << "\n"
                      << "grapheme: " << d.grapheme.mean_ipa_distance << "\n";
            return 0;
        }
        usage(); return 2;
    } catch (const std::exception& e) {
        std::cerr << "orthography2ipa: " << e.what() << "\n";
        return 1;
    }
}
