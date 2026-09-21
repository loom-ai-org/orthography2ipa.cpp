#include "orthography2ipa/orthography2ipa.hpp"
#include <iomanip>
#include <iostream>
#include <sstream>

using namespace orthography2ipa;

static void usage() {
    std::cerr << "usage: orthography2ipa [--data DIR] [--json] <list|info|validate|transcribe|distance> ...\n";
}
static std::string json(const std::string& value) {
    std::ostringstream out; out << '"';
    for (unsigned char c : value) { if (c == '"') out << "\\\""; else if (c == '\\') out << "\\\\"; else if (c == '\n') out << "\\n"; else if (c == '\r') out << "\\r"; else if (c == '\t') out << "\\t"; else out << c; }
    return out.str() + '"';
}
static void strings_json(const std::vector<std::string>& values) {
    std::cout << '['; for (std::size_t i = 0; i < values.size(); ++i) { if (i) std::cout << ','; std::cout << json(values[i]); } std::cout << ']';
}
static void path_json(const IPAPath& path) {
    std::cout << "{\"ipa\":" << json(path.ipa) << ",\"score\":" << path.score << ",\"graphemes\": "; strings_json(path.graphemes); std::cout << ",\"segments\":"; strings_json(path.segments); std::cout << '}';
}
static void detailed_json(const TranscriptionResult& result) {
    std::cout << "{\"lang\":" << json(result.lang) << ",\"ipa\":" << json(result.ipa) << ",\"words\":[";
    for (std::size_t i = 0; i < result.words.size(); ++i) { if (i) std::cout << ','; const auto& word = result.words[i];
        std::cout << "{\"word\":" << json(word.word) << ",\"ipa\":" << json(word.ipa) << ",\"coverage\":" << word.coverage << ",\"confidence\":" << word.confidence << ",\"candidates\":[";
        for (std::size_t j = 0; j < word.candidates.size(); ++j) { if (j) std::cout << ','; path_json(word.candidates[j]); } std::cout << "]}";
    }
    std::cout << "]}\n";
}

int main(int argc, char** argv) {
    bool json_output = false;
    try {
        int i = 1;
        if (i + 1 < argc && std::string(argv[i]) == "--data") { set_data_directory(argv[i + 1]); i += 2; }
        if (i < argc && std::string(argv[i]) == "--json") { json_output = true; ++i; }
        if (i >= argc) { usage(); return 2; }
        const std::string command = argv[i++];
        if (i < argc && std::string(argv[i]) == "--json") { json_output = true; ++i; }
        if (command == "list") {
            bool families = false; std::string family_filter;
            while (i < argc) { const std::string option = argv[i++]; if (option == "--json") json_output = true; else if (option == "--families") families = true; else if (option == "--family" && i < argc) family_filter = argv[i++]; else throw std::invalid_argument("unknown list option: " + option); }
            if (families) {
                const auto families_map = available_families();
                if (json_output) { std::cout << '{'; bool first = true; for (const auto& [family, codes] : families_map) { if (!family_filter.empty() && family.find(family_filter) == std::string::npos) continue; if (!first) std::cout << ','; first = false; std::cout << json(family) << ':'; strings_json(codes); } std::cout << "}\n"; }
                else for (const auto& [family, codes] : families_map) if (family_filter.empty() || family.find(family_filter) != std::string::npos) { std::cout << family << ":"; for (const auto& code : codes) std::cout << " " << code; std::cout << "\n"; }
            } else { auto codes = available_codes(); if (json_output) { strings_json(codes); std::cout << '\n'; } else for (const auto& code : codes) std::cout << code << "\n"; }
            return 0;
        }
        if (command == "info" && i < argc) {
            const auto& s = get(argv[i++]); bool show_graphemes = false, show_allophones = false;
            while (i < argc) { const std::string option = argv[i++]; if (option == "--json") json_output = true; else if (option == "--graphemes") show_graphemes = true; else if (option == "--allophones") show_allophones = true; else throw std::invalid_argument("unknown info option: " + option); }
            if (json_output) { std::cout << "{\"code\":" << json(s.code) << ",\"name\":" << json(s.name) << ",\"family\":" << json(s.family) << ",\"script\":" << json(s.script) << ",\"graphemes\":" << s.graphemes.size() << ",\"allophones\":" << s.allophones.size();
                if (show_graphemes) { std::cout << ",\"grapheme_map\":{"; bool first = true; for (const auto& [key, values] : s.graphemes) { if (!first) std::cout << ','; first = false; std::cout << json(key) << ':'; strings_json(values); } std::cout << '}'; }
                if (show_allophones) { std::cout << ",\"allophone_map\":{"; bool first = true; for (const auto& [key, values] : s.allophones) { if (!first) std::cout << ','; first = false; std::cout << json(key) << ':'; strings_json(values); } std::cout << '}'; }
                std::cout << "}\n";
            } else { std::cout << "Code: " << s.code << "\nName: " << s.name << "\nFamily: " << s.family << "\nScript: " << s.script << "\nGraphemes: " << s.graphemes.size() << "\nAllophones: " << s.allophones.size() << "\n";
                if (show_graphemes) for (const auto& [key, values] : s.graphemes) { std::cout << key << ":"; for (const auto& value : values) std::cout << " " << value; std::cout << '\n'; }
                if (show_allophones) for (const auto& [key, values] : s.allophones) { std::cout << key << ":"; for (const auto& value : values) std::cout << " " << value; std::cout << '\n'; }
            }
            return 0;
        }
        if (command == "validate") {
            if (i >= argc) { usage(); return 2; } const std::string code = argv[i++]; if (i < argc && std::string(argv[i]) == "--json") json_output = true; const auto errors = validate(code);
            if (json_output) { std::cout << "{\"valid\":" << (errors.empty() ? "true" : "false") << ",\"errors\":"; strings_json(errors); std::cout << "}\n"; }
            else if (errors.empty()) std::cout << "valid\n"; else for (const auto& error : errors) std::cout << error << "\n";
            return errors.empty() ? 0 : 1;
        }
        if (command == "transcribe" && i + 1 < argc) {
            const std::string code = argv[i++]; std::string dialect, text; std::size_t width = 8; bool detailed = false;
            while (i < argc) { const std::string option = argv[i++]; if (option == "--json") json_output = true; else if (option == "--lexicon" && i < argc) register_lexicon(code, argv[i++]); else if (option == "--dialect" && i < argc) dialect = argv[i++]; else if (option == "--beam" && i < argc) width = static_cast<std::size_t>(std::stoul(argv[i++])); else if (option == "--detailed") detailed = true; else { if (!text.empty()) text += ' '; text += option; } }
            if (text.empty()) { usage(); return 2; } G2P engine(code, {}, dialect);
            const auto result = engine.transcribe_detailed(text, width > 1 ? "beam" : "greedy", width);
            if (json_output || detailed) detailed_json(result); else std::cout << result.ipa << '\n';
            return 0;
        }
        if (command == "distance" && i + 1 < argc) {
            if (i + 2 < argc && std::string(argv[i + 2]) == "--json") json_output = true;
            const auto d = phonological_distance(get(argv[i]), get(argv[i + 1]));
            if (json_output) std::cout << "{\"combined\":" << d.combined << ",\"inventory\":" << d.inventory.feature_mean << ",\"allophone\":" << d.allophone_sim << ",\"grapheme\":" << d.grapheme.mean_ipa_distance << "}\n";
            else std::cout << std::fixed << std::setprecision(4) << "combined: " << d.combined << "\ninventory: " << d.inventory.feature_mean << "\nallophone: " << d.allophone_sim << "\ngrapheme: " << d.grapheme.mean_ipa_distance << "\n";
            return 0;
        }
        usage(); return 2;
    } catch (const std::exception& e) {
        if (json_output) std::cout << "{\"error\":" << json(e.what()) << ",\"type\":\"exception\"}\n"; else std::cerr << "orthography2ipa: " << e.what() << "\n";
        return 1;
    }
}
