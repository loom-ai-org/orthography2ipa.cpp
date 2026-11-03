#include "orthography2ipa/orthography2ipa.hpp"
#include "orthography2ipa/stress.hpp"
#include "orthography2ipa/registry.hpp"
#include "orthography2ipa/tone.hpp"
#include "py_float.hpp"
#include <climits>
#include <optional>
#include <algorithm>
#include <iomanip>
#include <iostream>
#include <sstream>

#include "unicode_util.hpp"

using namespace orthography2ipa;

static void usage() {
    std::cerr << "usage: orthography2ipa [--data DIR] [--json] <list|info|validate|transcribe|word|word-candidates|lattice|tone|stress|beam|tokens|resolve|distance> ...\n";
}
// The escapes of Python's json.dumps(..., ensure_ascii=False), which the
// reference CLI uses: the short forms where JSON has one, \u00XX for the
// other control characters, and every other byte as it is.
static std::string json(const std::string& value) {
    std::ostringstream out; out << '"';
    for (unsigned char c : value) {
        switch (c) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\b': out << "\\b"; break;
            case '\f': out << "\\f"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (c < 0x20) out << "\\u00" << "0123456789abcdef"[c >> 4] << "0123456789abcdef"[c & 15];
                else out << c;
        }
    }
    return out.str() + '"';
}
// Python str.strip().lower(): str.isspace() code points off both ends, then
// the full Unicode lowercase.
static std::string strip_lower(const std::string& text) {
    const auto is_space = [](char32_t c) {
        return (c >= 0x09 && c <= 0x0D) || (c >= 0x1C && c <= 0x20) || c == 0x85 || c == 0xA0 || c == 0x1680
            || (c >= 0x2000 && c <= 0x200A) || c == 0x2028 || c == 0x2029 || c == 0x202F || c == 0x205F || c == 0x3000;
    };
    const std::u32string value = uni::to_utf32(text);
    const auto first = std::find_if_not(value.begin(), value.end(), is_space);
    const auto last = std::find_if_not(value.rbegin(), value.rend(), is_space).base();
    return first < last ? uni::to_utf8(uni::lower(std::u32string(first, last))) : std::string();
}
// cli.py _cmd_list: a family matches on its whole path or on any one step of it.
static bool family_matches(const std::string& family, const std::string& key) {
    if (uni::to_utf8(uni::lower(uni::to_utf32(family))) == key) return true;
    for (std::size_t start = 0;;) {
        const std::size_t end = family.find('>', start);
        if (strip_lower(family.substr(start, end - start)) == key) return true;
        if (end == std::string::npos) return false;
        start = end + 1;
    }
}
static void strings_json(const std::vector<std::string>& values) {
    std::cout << '['; for (std::size_t i = 0; i < values.size(); ++i) { if (i) std::cout << ','; std::cout << json(values[i]); } std::cout << ']';
}
static void path_json(const IPAPath& path) {
    std::cout << "{\"ipa\":" << json(path.ipa) << ",\"score\":" << std::setprecision(17) << path.score << ",\"graphemes\":"; strings_json(path.graphemes); std::cout << ",\"segments\":"; strings_json(path.segments); std::cout << '}';
}
static void detailed_json(const TranscriptionResult& result) {
    // g2p.py WordTranscription's field set exactly: word, ipa, candidates,
    // unmapped, coverage, confidence. coverage/confidence are doubles derived
    // from the same arithmetic as the reference's, so they print at full
    // precision (17 significant digits) and a byte-compare is meaningful.
    std::cout << std::setprecision(17);
    std::cout << "{\"lang\":" << json(result.lang) << ",\"ipa\":" << json(result.ipa) << ",\"words\":[";
    for (std::size_t i = 0; i < result.words.size(); ++i) { if (i) std::cout << ','; const auto& word = result.words[i];
        std::cout << "{\"word\":" << json(word.word) << ",\"ipa\":" << json(word.ipa)
                  << ",\"coverage\":" << word.coverage << ",\"confidence\":" << word.confidence
                  << ",\"unmapped\":"; strings_json(word.unmapped);
        std::cout << ",\"candidates\":[";
        for (std::size_t j = 0; j < word.candidates.size(); ++j) { if (j) std::cout << ','; path_json(word.candidates[j]); } std::cout << "]}";
    }
    std::cout << "]}\n";
}


static void tokens_slots_json(const std::vector<rescorer::SegmentSlot>& slots);
static void tone_json(const std::string& code, const std::string& word,
                      const std::string& supplied);

static void tokens_json(const std::vector<Token>& tokens) {
    std::cout << '[';
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        if (i) std::cout << ',';
        const auto& t = tokens[i];
        std::cout << "{\"kind\":" << json(to_string(t.kind)) << ",\"grapheme\":" << json(t.grapheme)
                  << ",\"ipa\":["; 
        for (std::size_t j = 0; j < t.ipa.size(); ++j) {
            if (j) std::cout << ',';
            std::cout << json(t.ipa[j]);
        }
        std::cout << "],\"position\":" << t.position << ",\"length\":" << t.length << '}';
    }
    std::cout << "]\n";
}

static void tokens_slots_json(const std::vector<rescorer::SegmentSlot>& slots) {
    std::cout << std::setprecision(17) << '[';
    for (std::size_t i = 0; i < slots.size(); ++i) {
        if (i) std::cout << ',';
        const auto& slot = slots[i];
        std::cout << "{\"grapheme\":" << json(slot.grapheme) << ",\"span\":["
                  << slot.span.first << ',' << slot.span.second << "],\"candidates\":[";
        for (std::size_t j = 0; j < slot.candidates.size(); ++j) {
            if (j) std::cout << ',';
            std::cout << "{\"ipa\":" << json(slot.candidates[j].ipa) << ",\"cost\":"
                      << slot.candidates[j].score << '}';
        }
        std::cout << "]}";
    }
    std::cout << "]\n";
}

// tone.py's four functions, exposed as the intermediate reads the reference
// makes: _syllable_slots grouping, _rime, the class/mark/shape of
// _syllable_tone, assign_computed_tones and dock_tone_marks.
static void tone_json(const std::string& code, const std::string& word,
                      const std::string& supplied) {
    const auto& spec = get(code);
    const G2P engine(code);
    std::vector<std::string> graphemes, segments;
    if (!supplied.empty()) {
        // --ipa: the pure string functions on a caller-supplied reading. The
        // reference keys class/mark off GRAPHEMES, so a constructed case names
        // its own onsets by passing the IPA it wants grouped.
        segments = segment_ipa(supplied, std::vector<std::string>(spec.phonemes.begin(),
                                                                  spec.phonemes.end()));
        graphemes = segments;
    } else {
        const auto path = engine.engine_path(word, 1);
        graphemes = path.graphemes;
        segments = path.segments;
    }
    const std::vector<std::string> atoms(spec.phonemes.begin(), spec.phonemes.end());
    const auto slots = tone::syllable_slots(segments);
    const bool has_rules = spec.tone.has_value();
    const ToneData rules = has_rules ? *spec.tone : ToneData{};

    std::cout << std::setprecision(17) << "{\"lang\":" << json(spec.code)
              << ",\"word\":" << json(supplied.empty() ? word : supplied)
              << ",\"atoms\":"; strings_json(atoms);
    std::cout << ",\"graphemes\":"; strings_json(graphemes);
    std::cout << ",\"segments\":"; strings_json(segments);
    std::cout << ",\"slots\":[";
    for (std::size_t i = 0; i < slots.size(); ++i) {
        if (i) std::cout << ',';
        std::cout << '[';
        for (std::size_t j = 0; j < slots[i].size(); ++j) {
            if (j) std::cout << ',';
            std::cout << slots[i][j];
        }
        std::cout << ']';
    }
    std::cout << "],\"syllables\":[";
    for (std::size_t i = 0; i < slots.size(); ++i) {
        if (i) std::cout << ',';
        std::string ipa, syl_graphemes;
        std::vector<std::string> syl_g;
        for (const auto idx : slots[i]) { ipa += segments[idx]; syl_g.push_back(graphemes[idx]); }
        std::cout << "{\"indices\":[";
        for (std::size_t j = 0; j < slots[i].size(); ++j) { if (j) std::cout << ','; std::cout << slots[i][j]; }
        std::cout << "],\"graphemes\":"; strings_json(syl_g);
        std::cout << ",\"ipa\":" << json(ipa);
        const auto rime = tone::rime(ipa, atoms, rules.dead_codas);
        if (has_rules && rime.has_value()) {
            std::cout << ",\"rime\":{\"nucleus_end\":" << rime->nucleus_end
                      << ",\"long_vowel\":" << (rime->long_vowel ? "true" : "false")
                      << ",\"dead\":" << (rime->dead ? "true" : "false") << "}";
        } else {
            std::cout << ",\"rime\":null";
        }
        if (has_rules) {
            const std::string cls = tone::syllable_class(syl_g, rules);
            const std::string mark = tone::syllable_mark(syl_g, rules);
            std::cout << ",\"class\":" << json(cls) << ",\"mark\":" << json(mark)
                      << ",\"shape\":" << json(!rime.has_value() ? std::string()
                                          : (rime->dead ? (rime->long_vowel ? "dead_long" : "dead_short")
                                                         : std::string("live")))
                      << ",\"tone\":" << json(tone::syllable_tone(syl_g, ipa, rules, atoms));
        } else {
            std::cout << ",\"class\":null,\"mark\":null,\"shape\":null,\"tone\":null";
        }
        std::cout << '}';
    }
    std::cout << ']';
    const std::string computed = has_rules
            ? tone::assign_computed_tones(graphemes, segments, rules, atoms)
            : [](const std::vector<std::string>& v) { std::string s; for (const auto& x : v) s += x; return s; }(segments);
    const std::string docked = tone::dock_tone_marks(computed, atoms);
    std::cout << ",\"computed\":" << json(computed) << ",\"docked\":" << json(docked);
    if (supplied.empty()) std::cout << ",\"final\":" << json(engine.transcribe_word(word));
    std::cout << ",\"tone_marks_syllable_final\":" << (spec.tone_marks_syllable_final ? "true" : "false")
              << ",\"declares_tone_rules\":" << (has_rules ? "true" : "false") << "}\n";
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
        if (command == "resolve" && i < argc) {
            // Debug/parity surface for registry.py resolve(): the spec code a
            // request maps to, including the nearest-language fallback that no
            // `get()` of an unknown code can show. Every positional argument
            // is a code — even one with a leading dash, which the reference
            // resolves unchanged ('-aa' stays '-aa') — and --json prints one
            // document PER code in argument order, so a parity harness can
            // batch codes into one process.
            // --lang-distance is a one-shot mode: it measures the LAST given
            // code against its value, prints the integer, and stops.
            const std::vector<std::string> catalog = available_codes(true);
            std::vector<std::string> codes;
            std::string other;
            bool distance_mode = false;
            while (i < argc) {
                const std::string option = argv[i++];
                if (option == "--json") json_output = true;
                else if (option == "--lang-distance") {
                    if (i >= argc) { usage(); return 2; }
                    other = argv[i++];
                    distance_mode = true;
                } else codes.push_back(option);
            }
            if (codes.empty()) { usage(); return 2; }
            if (distance_mode) {
                std::cout << registry::lang_distance(codes.back(), other) << "\n";
                return 0;
            }
            for (const auto& requested : codes) {
                const std::string resolved = registry::resolve_code(requested, catalog);
                if (json_output)
                    std::cout << "{\"requested\":" << json(requested)
                              << ",\"resolved\":" << json(resolved) << ",\"known\":"
                              << (std::find(catalog.begin(), catalog.end(), resolved) != catalog.end() ? "true" : "false")
                              << "}\n";
                else std::cout << resolved << "\n";
            }
            return 0;
        }

        if (command == "list") {
            bool families = false; std::string family_filter;
            while (i < argc) { const std::string option = argv[i++]; if (option == "--json") json_output = true; else if (option == "--families") families = true; else if (option == "--family" && i < argc) family_filter = argv[i++]; else throw std::invalid_argument("unknown list option: " + option); }
            const bool filtered = !family_filter.empty();
            if (families || filtered) {
                auto families_map = available_families();
                if (filtered) {
                    const std::string key = strip_lower(family_filter);
                    for (auto it = families_map.begin(); it != families_map.end();) it = family_matches(it->first, key) ? std::next(it) : families_map.erase(it);
                    if (families_map.empty()) { std::cerr << "Unknown family: " << family_filter << "\n"; return 1; }
                }
                for (auto& [family, codes] : families_map) std::sort(codes.begin(), codes.end());
                if (json_output) { std::cout << '{'; bool first = true; for (const auto& [family, codes] : families_map) { if (!first) std::cout << ','; first = false; std::cout << json(family) << ':'; strings_json(codes); } std::cout << "}\n"; }
                else for (const auto& [family, codes] : families_map) { std::cout << family << ": "; for (std::size_t c = 0; c < codes.size(); ++c) std::cout << (c ? ", " : "") << codes[c]; std::cout << "\n"; }
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
            bool have_text = false;
            while (i < argc) { const std::string option = argv[i++]; if (option == "--json") json_output = true; else if (option == "--lexicon" && i < argc) register_lexicon(code, argv[i++]); else if (option == "--dialect" && i < argc) dialect = argv[i++]; else if (option == "--beam" && i < argc) width = static_cast<std::size_t>(std::stoul(argv[i++])); else if (option == "--detailed") detailed = true; else { if (have_text && !text.empty()) text += ' '; text += option; have_text = true; } }
            // An explicitly empty text is a valid utterance: the reference
            // returns "" for it rather than a usage error.
            if (!have_text) { usage(); return 2; } G2P engine(code, {}, dialect);
            const auto result = engine.transcribe_detailed(text, width > 1 ? "beam" : "greedy", width);
            if (json_output || detailed) detailed_json(result); else std::cout << result.ipa << '\n';
            return 0;
        }
        if (command == "word" && i + 1 < argc) {
            // Debug/parity surface for the reference G2P.transcribe_word: the
            // per-word pipeline with NO word splitting. The sentence path
            // re-tokenizes a shadda-expanded or clitic-broken surface, so a
            // per-word battery must be compared against this, not `transcribe`.
            const std::string code = argv[i++];
            if (i >= argc) { usage(); return 2; }
            const std::string text = argv[i++];
            std::string search = "greedy"; std::size_t width = 8;
            while (i < argc) {
                const std::string option = argv[i++];
                if (option == "--json") json_output = true;
                else if (option == "--beam") { search = "beam"; if (i < argc) width = static_cast<std::size_t>(std::stoul(argv[i++])); }
                else throw std::invalid_argument("unknown word option: " + option);
            }
            std::cout << G2P(code).transcribe_word(text, search, width) << "\n";
            return 0;
        }
        if (command == "tokens" && i + 1 < argc) {
            const std::string code = argv[i++];
            if (i >= argc) { usage(); return 2; }
            const std::string text = argv[i++];
            bool grapheme_only = false;
            while (i < argc) {
                const std::string option = argv[i++];
                if (option == "--json") json_output = true;
                else if (option == "--grapheme-only") grapheme_only = true;
                else throw std::invalid_argument("unknown tokens option: " + option);
            }
            // phonetok.py PhonetokTokenizer.grapheme_tokens: the same token
            // stream restricted to GRAPHEME kind — the object the engine feeds
            // to flat_contexts, whose spans are what SegmentSlot.span reports.
            tokens_json(grapheme_only ? Tokenizer(get(code)).grapheme_tokens(text)
                                      : Tokenizer(get(code)).tokenize(text));
            return 0;
        }
        if (command == "beam" && i + 1 < argc) {
            // Debug/parity surface for PhonetokTokenizer::ipa_beam.
            const std::string code = argv[i++];
            if (i >= argc) { usage(); return 2; }
            const std::string text = argv[i++];
            std::size_t width = 8; bool allophones = false;
            while (i < argc) {
                const std::string option = argv[i++];
                if (option == "--json") json_output = true;
                else if (option == "--width" && i < argc) width = static_cast<std::size_t>(std::stoul(argv[i++]));
                else if (option == "--allophones") allophones = true;
                else throw std::invalid_argument("unknown beam option: " + option);
            }
            const auto paths = PhonetokTokenizer(get(code)).ipa_beam(text, width, allophones);
            std::cout << '[';
            for (std::size_t j = 0; j < paths.size(); ++j) { if (j) std::cout << ','; path_json(paths[j]); }
            std::cout << "]\n";
            return 0;
        }
        if (command == "lattice" && i + 1 < argc) {
            // Debug/parity surface for G2P.ipa_lattice: one slot per GRAPHEME
            // token with its ranked candidates AFTER the rescorer chain, so
            // slot rewrites are visible. --width -1 keeps every candidate.
            const std::string code = argv[i++];
            if (i >= argc) { usage(); return 2; }
            const std::string text = argv[i++];
            long long width = 8; bool allophones = false; bool pre_rescore = false;
            while (i < argc) {
                const std::string option = argv[i++];
                if (option == "--json") json_output = true;
                else if (option == "--width" && i < argc) width = std::stoll(argv[i++]);
                else if (option == "--allophones") allophones = true;
                else if (option == "--pre-rescore") pre_rescore = true;
                else throw std::invalid_argument("unknown lattice option: " + option);
            }
            const std::size_t keep = width < 0 ? SIZE_MAX : static_cast<std::size_t>(width);
            const G2P engine(code, {}, "", allophones);
            tokens_slots_json(pre_rescore ? engine.lattice_pre_rescore(text, keep)
                                          : engine.ipa_lattice(text, keep));
            return 0;
        }
        if (command == "word-candidates" && i + 1 < argc) {
            // Debug/parity surface for G2P.word_candidates: the top-k FINALIZED
            // readings (same word pipeline as `word`), duplicates collapsed.
            const std::string code = argv[i++];
            if (i >= argc) { usage(); return 2; }
            const std::string text = argv[i++];
            long long k = 5; std::optional<long long> beam;
            while (i < argc) {
                const std::string option = argv[i++];
                if (option == "--json") json_output = true;
                else if ((option == "-k" || option == "--k") && i < argc) k = std::stoll(argv[i++]);
                else if (option == "--beam" && i < argc) beam = std::stoll(argv[i++]);
                else throw std::invalid_argument("unknown word-candidates option: " + option);
            }
            const auto readings = G2P(code).word_candidates(
                text, static_cast<std::size_t>(k),
                beam.has_value() ? std::optional<std::size_t>(*beam) : std::nullopt);
            std::cout << "{\"lang\":" << json(get(code).code) << ",\"word\":" << json(text)
                      << ",\"k\":" << k << ",\"beam_width\":"
                      << (beam.has_value() ? *beam : (k < 8 ? 8 : k))
                      << ",\"candidates\":";
            strings_json(readings);
            std::cout << ",\"greedy\":" << json(G2P(code).transcribe_word(text)) << "}\n";
            return 0;
        }
        if (command == "tone" && i + 1 < argc) {
            // Debug/parity surface for tone.py: the syllable grouping, each
            // syllable's rime/class/mark/shape, the computed tone string, and
            // the docking result. --ipa reads a caller-supplied string through
            // the pure functions instead of the beam.
            const std::string code = argv[i++];
            if (i >= argc) { usage(); return 2; }
            const std::string text = argv[i++];
            std::string supplied;
            while (i < argc) {
                const std::string option = argv[i++];
                if (option == "--json") json_output = true;
                else if (option == "--ipa" && i < argc) supplied = argv[i++];
                else throw std::invalid_argument("unknown tone option: " + option);
            }
            tone_json(code, text, supplied);
            return 0;
        }
        if (command == "stress" && i + 1 < argc) {
            // Debug/parity surface for the stress pipeline (stress.py):
            // syllabify → detect_stress → apply_stress_mark with secondary
            // positions, on the word's orthographic syllables. With --ipa the
            // QUANTITY-SENSITIVE path is exposed instead: syllabify_ipa →
            // syllable_weight → detect_stress_by_weight → iambic positions →
            // apply_stress_mark over the IPA division, which is the half of
            // stress.py the 38 quantity_sensitive specs actually use.
            const std::string code = argv[i++];
            if (i < argc && std::string(argv[i]) == "--ipa") {
                if (i + 1 >= argc) { usage(); return 2; }
                const std::string ipa = argv[i + 1];
                i += 2;
                const LanguageSpec& spec = get(code);
                while (i < argc) {
                    const std::string option = argv[i++];
                    if (option == "--json") json_output = true;
                    else throw std::invalid_argument("unknown stress option: " + option);
                }
                const std::vector<std::string> atoms(spec.phonemes.begin(), spec.phonemes.end());
                const auto sylls = stress::syllabify_ipa(ipa, spec.max_onset, atoms);
                std::cout << std::setprecision(17) << "{\"lang\":" << json(spec.code)
                          << ",\"input\":" << json(ipa) << ",\"mode\":\"ipa\",\"syllables\":";
                strings_json(sylls);
                std::cout << ",\"weights\":[";
                for (std::size_t k = 0; k < sylls.size(); ++k) {
                    if (k) std::cout << ',';
                    std::cout << json(stress::syllable_weight(sylls[k], atoms));
                }
                std::cout << ']';
                // stress.py's own gate: a spec with no stress rules has no
                // detect_stress_by_weight to consult at all.
                if (!spec.stress_defined) { std::cout << "}\n"; return 0; }
                const int idx = stress::detect_stress_by_weight(ipa, spec, atoms);
                std::cout << ",\"stress\":" << idx << ",\"secondary\":[";
                bool first_secondary = true;
                for (const int sv : stress::secondary_stress_positions(
                             static_cast<int>(sylls.size()), idx, spec)) {
                    if (!first_secondary) std::cout << ',';
                    first_secondary = false; std::cout << sv;
                }
                std::cout << "],\"iambic\":[";
                bool first_iambic = true;
                for (const int pv : stress::iambic_length_positions(sylls, atoms)) {
                    if (!first_iambic) std::cout << ',';
                    first_iambic = false; std::cout << pv;
                }
                std::cout << "],\"marked\":"
                          << json(stress::apply_stress_mark(
                                 ipa, spec, idx, nullptr, &sylls)) << "}\n";
                return 0;
            }
            if (i >= argc) { usage(); return 2; }
            const std::string word = argv[i++];
            const LanguageSpec& spec = get(code);
            const auto sylls = stress::syllables_for(word, spec.code, spec.diphthongs, &spec);
            std::cout << '{';
            std::cout << "\"syllables\":";
            strings_json(sylls);
            if (spec.stress_defined) {
                const int idx = stress::detect_stress(word, spec, &sylls);
                const auto secondary = stress::secondary_stress_positions(
                    static_cast<int>(sylls.size()), idx, spec);
                std::cout << ",\"stress\":" << idx;
                std::cout << ",\"secondary\":[";
                bool first = true;
                for (const int s : secondary) { if (!first) std::cout << ','; std::cout << s; first = false; }
                std::cout << ']';
                std::cout << ",\"marked\":"
                          << json(stress::apply_stress_mark(
                                 word, spec, idx, &sylls, nullptr, "",
                                 std::vector<int>(secondary.begin(), secondary.end())));
            }
            std::cout << "}\n";
            return 0;
        }
        if (command == "distance" && i + 1 < argc) {
            if (i + 2 < argc && std::string(argv[i + 2]) == "--json") json_output = true;
            const auto d = phonological_distance(get(argv[i]), get(argv[i + 1]));
            if (json_output) std::cout << "{\"combined\":" << detail::py_float(d.combined) << ",\"inventory\":" << detail::py_float(d.inventory.feature_mean) << ",\"allophone\":" << detail::py_float(d.allophone_sim) << ",\"grapheme\":" << detail::py_float(d.grapheme.mean_ipa_distance) << "}\n";
            else std::cout << std::fixed << std::setprecision(4) << "combined: " << d.combined << "\ninventory: " << d.inventory.feature_mean << "\nallophone: " << d.allophone_sim << "\ngrapheme: " << d.grapheme.mean_ipa_distance << "\n";
            return 0;
        }
        usage(); return 2;
    } catch (const std::exception& e) {
        if (json_output) std::cout << "{\"error\":" << json(e.what()) << ",\"type\":\"exception\"}\n"; else std::cerr << "orthography2ipa: " << e.what() << "\n";
        return 1;
    }
}
