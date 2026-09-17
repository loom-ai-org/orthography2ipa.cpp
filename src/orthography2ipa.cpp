#include "orthography2ipa/orthography2ipa.hpp"

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <set>
#include <sstream>
#include <stdexcept>

namespace orthography2ipa {
namespace {
using boost::property_tree::ptree;
namespace fs = std::filesystem;
std::string data_dir = O2I_DEFAULT_DATA_DIR;
std::map<std::string, LanguageSpec> cache;
std::map<std::string, std::string> registered_lexicons;
std::optional<std::string> lexicon_directory;
std::map<std::string, std::map<std::string, std::string>> lexicon_cache;
std::map<std::string, std::shared_ptr<NormalizePlugin>> normalize_plugins;
std::map<std::string, std::shared_ptr<SyllabifierPlugin>> syllabifier_plugins;
std::map<std::string, std::shared_ptr<StressPlugin>> stress_plugins;
std::map<std::string, std::shared_ptr<RescorerPlugin>> rescorer_plugins;
std::map<std::string, std::shared_ptr<SandhiPlugin>> sandhi_plugins;

std::string lower_ascii(std::string s) {
    for (char& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

std::vector<std::string> strings(const ptree& p) {
    std::vector<std::string> result;
    for (const auto& item : p) result.push_back(item.second.data());
    return result;
}
std::vector<std::string> string_or_list(const ptree& p) {
    if (p.empty()) return p.data().empty() ? std::vector<std::string>{} : std::vector<std::string>{p.data()};
    return strings(p);
}

std::vector<std::string> optional_strings(const ptree& p, const std::string& key) {
    auto child = p.get_child_optional(key);
    return child ? strings(*child) : std::vector<std::string>{};
}

std::optional<bool> optional_bool(const ptree& p, const std::string& key) {
    auto v = p.get_optional<bool>(key); return v ? std::optional<bool>(*v) : std::nullopt;
}

std::vector<AllophoneRule> parse_allophone_rules(const ptree& p) {
    std::vector<AllophoneRule> result; auto list = p.get_child_optional("allophone_rules");
    if (!list) return result;
    for (const auto& item : *list) {
        const auto& r = item.second; AllophoneRule rule;
        rule.id = r.get<std::string>("id", ""); rule.surface = r.get<std::string>("surface", "");
        rule.append = r.get<std::string>("append", ""); rule.syllable_position = r.get<std::string>("syllable_position", "");
        rule.stress = r.get<std::string>("stress", ""); rule.phonemes = optional_strings(r, "phonemes");
        rule.preceded_by = r.get<std::string>("preceded_by", ""); rule.followed_by = r.get<std::string>("followed_by", "");
        rule.preceded_by_2 = r.get<std::string>("preceded_by_2", ""); rule.followed_by_2 = r.get<std::string>("followed_by_2", "");
        rule.preceded_by_3 = r.get<std::string>("preceded_by_3", "");
        rule.graphemes = optional_strings(r, "grapheme"); rule.word = optional_strings(r, "word");
        rule.preceded_by_phoneme = optional_strings(r, "preceded_by_phoneme"); rule.followed_by_phoneme = optional_strings(r, "followed_by_phoneme");
        rule.preceded_by_phoneme_2 = optional_strings(r, "preceded_by_phoneme_2"); rule.followed_by_phoneme_2 = optional_strings(r, "followed_by_phoneme_2");
        rule.preceded_by_surface_phoneme_2 = optional_strings(r, "preceded_by_surface_phoneme_2");
        rule.preceded_by_grapheme = optional_strings(r, "preceded_by_grapheme"); rule.followed_by_grapheme = optional_strings(r, "followed_by_grapheme");
        rule.followed_by_grapheme_not = optional_strings(r, "followed_by_grapheme_not");
        rule.word_contains_grapheme = optional_strings(r, "word_contains_grapheme"); rule.word_contains_grapheme_not = optional_strings(r, "word_contains_grapheme_not");
        rule.requires_other_nucleus = optional_bool(r, "requires_other_nucleus"); rule.followed_by_nucleus = optional_bool(r, "followed_by_nucleus");
        rule.mutates_neighbor = r.get<std::string>("mutates_neighbor", ""); rule.mutates_neighbor_side = r.get<std::string>("mutates_neighbor_side", "");
        rule.word_initial = optional_bool(r, "word_initial"); rule.word_final = optional_bool(r, "word_final");
        result.push_back(std::move(rule));
    }
    return result;
}

std::map<std::string, std::vector<std::string>> ipa_map(const ptree& p, const std::string& key) {
    std::map<std::string, std::vector<std::string>> result;
    auto child = p.get_child_optional(key);
    if (!child) return result;
    for (const auto& item : *child) {
        auto ipa = item.second.get_child_optional("ipa");
        result[item.first] = ipa ? strings(*ipa) : strings(item.second);
    }
    return result;
}

std::map<std::string, std::map<std::string, std::vector<std::string>>>
positional_map(const ptree& p) {
    std::map<std::string, std::map<std::string, std::vector<std::string>>> result;
    auto child = p.get_child_optional("positional_graphemes");
    if (!child) return result;
    for (const auto& g : *child) for (const auto& pos : g.second)
        result[g.first][pos.first] = strings(pos.second);
    return result;
}

LanguageSpec load_raw(const std::string& code, std::set<std::string>& loading) {
    if (cache.count(code)) return cache.at(code);
    if (loading.count(code)) throw std::runtime_error("cyclic language inheritance: " + code);
    const fs::path path = fs::path(data_dir) / (code + ".json");
    std::ifstream input(path);
    if (!input) throw std::runtime_error("unsupported language: '" + code + "'");
    ptree raw; boost::property_tree::read_json(input, raw);
    loading.insert(code);
    LanguageSpec s;
    s.code = raw.get<std::string>("code", code);
    s.name = raw.get<std::string>("name", s.code);
    s.script = raw.get<std::string>("script", "");
    s.parent = raw.get<std::string>("parent", "");
    s.family = raw.get<std::string>("family", "");
    s.quality = raw.get<std::string>("quality", "research");
    s.clade = raw.get<bool>("clade", false);
    s.graphemes = ipa_map(raw, "graphemes");
    s.allophones = ipa_map(raw, "allophones");
    s.positional_graphemes = positional_map(raw);
    s.phonemes = optional_strings(raw, "phonemes");
    s.marked_vowels = optional_strings(raw.get_child("stress", ptree{}), "marked_vowels");
    if (auto stress = raw.get_child_optional("stress")) {
        s.default_stress_position = stress->get<int>("default_position", -2);
        s.stress_mark = stress->get<std::string>("stress_mark", "ˈ");
        s.final_stress_endings = optional_strings(*stress, "final_stress_endings");
        s.penult_stress_endings = optional_strings(*stress, "penult_stress_endings");
        s.antepenult_stress_endings = optional_strings(*stress, "antepenult_stress_endings");
        s.diphthongs = optional_strings(*stress, "diphthongs");
        s.marked_vowels = optional_strings(*stress, "marked_vowels");
    }
    if (auto loc = raw.get_child_optional("location")) {
        s.latitude = loc->get<double>("latitude", 0); s.longitude = loc->get<double>("longitude", 0);
    }
    if (auto ex = raw.get_child_optional("word_exceptions"))
        for (const auto& x : *ex) s.word_exceptions[x.first] = x.second.data();
    if (auto endings = raw.get_child_optional("grammatical_endings"))
        for (const auto& x : *endings) if (!x.second.empty()) s.grammatical_endings[x.first] = x.second.data();
    s.allophone_rules = parse_allophone_rules(raw);
    if (auto plugins = raw.get_child_optional("plugins"))
        for (const auto& x : *plugins) s.plugins[x.first] = string_or_list(x.second);

    // Resolve the data inheritance used by the Python loader. Own entries override base entries.
    const std::string base = raw.get<std::string>("graphemes_base", "");
    const std::string allo_base = raw.get<std::string>("allophones_base", "");
    if (!base.empty() && base != code) {
        LanguageSpec b = load_raw(base, loading);
        for (const auto& x : b.graphemes) if (!s.graphemes.count(x.first)) s.graphemes[x.first] = x.second;
        for (const auto& x : b.positional_graphemes)
            if (!s.positional_graphemes.count(x.first)) s.positional_graphemes[x.first] = x.second;
        if (s.allophone_rules.empty()) s.allophone_rules = b.allophone_rules;
        for (const auto& [stage, names] : b.plugins) if (!s.plugins.count(stage)) s.plugins[stage] = names;
        if (!raw.get_child_optional("stress") && s.default_stress_position == -2) {
            s.default_stress_position = b.default_stress_position; s.stress_mark = b.stress_mark;
            s.marked_vowels = b.marked_vowels; s.final_stress_endings = b.final_stress_endings;
            s.penult_stress_endings = b.penult_stress_endings; s.antepenult_stress_endings = b.antepenult_stress_endings;
            s.diphthongs = b.diphthongs;
        }
        if (s.family.empty()) s.family = b.family;
    }
    if (!allo_base.empty() && allo_base != code) {
        LanguageSpec b = load_raw(allo_base, loading);
        for (const auto& x : b.allophones) if (!s.allophones.count(x.first)) s.allophones[x.first] = x.second;
    }
    if (s.family.empty() && !s.parent.empty() && s.parent != code) {
        try { s.family = load_raw(s.parent, loading).family; } catch (...) {}
    }
    loading.erase(code);
    cache[code] = s;
    return s;
}

std::vector<std::string> words(const std::string& text) {
    std::vector<std::string> result; std::string current;
    auto flush = [&] { if (!current.empty()) { result.push_back(current); current.clear(); } };
    for (unsigned char c : text) {
        if (c <= 32 || std::ispunct(c)) flush();
        else current.push_back(static_cast<char>(c));
    }
    flush(); return result;
}


std::string add_stress(const LanguageSpec& spec, const std::string& word, const IPAPath& path, std::optional<std::size_t> forced = std::nullopt) {
    if (path.graphemes.empty() || spec.stress_mark.empty()) return path.ipa;
    std::vector<std::size_t> nuclei;
    for (std::size_t i = 0; i < path.graphemes.size(); ++i) {
        const auto& g = path.graphemes[i];
        if (g.find_first_of("aeiouAEIOUáéíóúâêôãõÁÉÍÓÚÂÊÔÃÕ") != std::string::npos) nuclei.push_back(i);
    }
    if (nuclei.empty()) return path.ipa;
    std::size_t target = forced ? std::min(*forced, nuclei.size() - 1) : nuclei.size() - 1;
    bool marked = forced.has_value();
    for (const auto& v : spec.marked_vowels) for (std::size_t i = 0; i < path.graphemes.size(); ++i)
        if (path.graphemes[i].find(v) != std::string::npos) {
            target = static_cast<std::size_t>(std::find(nuclei.begin(), nuclei.end(), i) - nuclei.begin());
            marked = true;
        }
    auto ends_with = [&](const std::vector<std::string>& endings) {
        return std::any_of(endings.begin(), endings.end(), [&](const auto& e) { return word.size() >= e.size() && word.compare(word.size() - e.size(), e.size(), e) == 0; });
    };
    if (forced) {
        // The plugin supplied the syllable decision; do not let declarative
        // endings override it.
    } else if (ends_with(spec.final_stress_endings)) target = nuclei.size() - 1;
    else if (ends_with(spec.penult_stress_endings)) target = nuclei.size() > 1 ? nuclei.size() - 2 : 0;
    else if (ends_with(spec.antepenult_stress_endings)) target = nuclei.size() > 2 ? nuclei.size() - 3 : 0;
    else if (!marked) {
        const int p = spec.default_stress_position;
        target = p > 0 ? static_cast<std::size_t>(std::min<int>(p - 1, nuclei.size() - 1))
                       : static_cast<std::size_t>(std::max<int>(0, static_cast<int>(nuclei.size()) + p));
    }
    // Stress precedes the syllable onset, not necessarily the vowel nucleus.
    const std::size_t grapheme = target == 0 ? 0 : nuclei[target - 1] + 1;
    std::size_t offset = 0;
    for (std::size_t i = 0; i < grapheme; ++i) {
        auto it = spec.graphemes.find(path.graphemes[i]);
        if (it != spec.graphemes.end() && !it->second.empty()) offset += it->second.front().size();
    }
    return path.ipa.substr(0, offset) + spec.stress_mark + path.ipa.substr(offset);
}
std::set<std::string> inventory(const LanguageSpec& s) {
    std::set<std::string> out(s.phonemes.begin(), s.phonemes.end());
    if (!out.empty()) return out;
    for (const auto& [_, values] : s.graphemes) out.insert(values.begin(), values.end());
    out.erase(""); return out;
}
const std::vector<std::string>& plugin_names(const LanguageSpec& spec, const std::string& stage) {
    static const std::vector<std::string> empty;
    auto it = spec.plugins.find(stage); return it == spec.plugins.end() ? empty : it->second;
}

std::map<std::string, std::string> load_lexicon(const std::string& code) {
    auto cached = lexicon_cache.find(code); if (cached != lexicon_cache.end()) return cached->second;
    std::string source;
    if (auto it = registered_lexicons.find(code); it != registered_lexicons.end()) source = it->second;
    else {
        const char* env = std::getenv("ORTHOGRAPHY2IPA_LEXICON_DIR");
        const std::string dir = lexicon_directory ? *lexicon_directory : (env ? env : "");
        if (!dir.empty() && fs::is_regular_file(fs::path(dir) / (code + ".tsv"))) source = (fs::path(dir) / (code + ".tsv")).string();
    }
    std::map<std::string, std::string> result;
    if (!source.empty()) {
        std::ifstream input(source); if (!input) throw std::runtime_error("lexicon not found: " + source);
        std::string line;
        while (std::getline(input, line)) {
            const auto tab = line.find('\t'); if (tab == std::string::npos || tab == 0 || tab + 1 >= line.size()) continue;
            const std::string word = lower_ascii(line.substr(0, tab)), ipa = line.substr(tab + 1);
            if (!result.count(word)) result[word] = ipa;
        }
    }
    lexicon_cache[code] = result; return result;
}

bool in(const std::string& value, const std::vector<std::string>& list) { return std::find(list.begin(), list.end(), value) != list.end(); }
bool vowel_grapheme(const std::string& value) { return value.find_first_of("aeiouAEIOUáéíóúâêôãõÁÉÍÓÚÂÊÔÃÕ") != std::string::npos; }

bool class_match(const std::string& cls, const std::vector<std::string>& gs,
                 const std::vector<std::string>& phones, std::size_t index, int distance) {
    const long pos = static_cast<long>(index) + distance;
    if (cls == "word_boundary") return pos < 0 || pos >= static_cast<long>(gs.size());
    if (cls == "any") return pos >= 0 && pos < static_cast<long>(gs.size());
    if (pos < 0 || pos >= static_cast<long>(gs.size())) return false;
    const auto p = static_cast<std::size_t>(pos);
    if (cls == "vowel" || cls == "front_vowel" || cls == "back_vowel") return vowel_grapheme(gs[p]);
    if (cls == "consonant" || cls == "coda" || cls == "onset") return !vowel_grapheme(gs[p]);
    if (cls == "consonant_cluster") {
        const long next = pos + (distance < 0 ? -1 : 1);
        return !vowel_grapheme(gs[p]) && next >= 0 && next < static_cast<long>(gs.size()) && !vowel_grapheme(gs[static_cast<std::size_t>(next)]);
    }
    if (cls == "coda_nasal") return !vowel_grapheme(gs[p]) && !phones[p].empty() && std::string("mnɲŋɳɴ").find(phones[p][0]) != std::string::npos;
    if (cls == "palatal") return phones[p].find_first_of("ʃʒɲʎjtɕdʑ") != std::string::npos;
    if (cls == "emphatic") return phones[p].find("ˤ") != std::string::npos;
    return false;
}

bool stressed_grapheme(const LanguageSpec& spec, const std::vector<std::string>& gs, std::size_t index) {
    std::vector<std::size_t> nuclei; for (std::size_t i = 0; i < gs.size(); ++i) if (vowel_grapheme(gs[i])) nuclei.push_back(i);
    if (nuclei.empty()) return false;
    std::size_t target = nuclei.size() - 1; bool marked = false;
    for (const auto& mark : spec.marked_vowels) for (std::size_t i = 0; i < gs.size(); ++i) if (gs[i].find(mark) != std::string::npos) { target = static_cast<std::size_t>(std::find(nuclei.begin(), nuclei.end(), i) - nuclei.begin()); marked = true; }
    if (!marked) { const int p = spec.default_stress_position; target = p > 0 ? std::min<std::size_t>(p - 1, nuclei.size() - 1) : static_cast<std::size_t>(std::max<int>(0, static_cast<int>(nuclei.size()) + p)); }
    return target < nuclei.size() && nuclei[target] == index;
}

std::optional<std::size_t> stress_nucleus(const LanguageSpec& spec, const std::vector<std::string>& gs) {
    std::vector<std::size_t> nuclei; for (std::size_t i = 0; i < gs.size(); ++i) if (vowel_grapheme(gs[i])) nuclei.push_back(i);
    if (nuclei.empty()) return std::nullopt;
    std::size_t target = nuclei.size() - 1; bool marked = false;
    for (const auto& mark : spec.marked_vowels) for (std::size_t i = 0; i < gs.size(); ++i) if (gs[i].find(mark) != std::string::npos) { target = static_cast<std::size_t>(std::find(nuclei.begin(), nuclei.end(), i) - nuclei.begin()); marked = true; }
    if (!marked) { const int p = spec.default_stress_position; target = p > 0 ? std::min<std::size_t>(p - 1, nuclei.size() - 1) : static_cast<std::size_t>(std::max<int>(0, static_cast<int>(nuclei.size()) + p)); }
    return nuclei[target];
}

std::size_t utf8_char_size(const std::string& text, std::size_t pos) {
    const unsigned char c = static_cast<unsigned char>(text[pos]);
    if (c < 0x80) return 1;
    if ((c & 0xe0) == 0xc0) return 2;
    if ((c & 0xf0) == 0xe0) return 3;
    return 4;
}
bool ipa_modifier(const std::string& text, std::size_t pos) {
    if (pos >= text.size()) return false;
    const auto cp = [&]() -> unsigned int { const unsigned char c = static_cast<unsigned char>(text[pos]); if (c < 0x80) return c; if ((c & 0xe0) == 0xc0) return ((c & 0x1f) << 6) | (text[pos + 1] & 0x3f); if ((c & 0xf0) == 0xe0) return ((c & 0xf) << 12) | ((text[pos + 1] & 0x3f) << 6) | (text[pos + 2] & 0x3f); return ((c & 7) << 18) | ((text[pos + 1] & 0x3f) << 12) | ((text[pos + 2] & 0x3f) << 6) | (text[pos + 3] & 0x3f); }();
    return (cp >= 0x300 && cp <= 0x36f) || (cp >= 0x2b0 && cp <= 0x2ff) || (cp >= 0x1d00 && cp <= 0x1dff) || cp == 0x2d0 || cp == 0x2d1 || cp == 0x203f;
}
std::vector<std::string> segment_ipa(const std::string& ipa, std::vector<std::string> atoms) {
    std::sort(atoms.begin(), atoms.end(), [](const auto& a, const auto& b) { return a.size() > b.size(); });
    std::vector<std::string> result;
    for (std::size_t i = 0; i < ipa.size();) {
        std::string match;
        for (const auto& atom : atoms) if (ipa.compare(i, atom.size(), atom) == 0 && (i + atom.size() == ipa.size() || !ipa_modifier(ipa, i + atom.size()))) { match = atom; break; }
        if (match.empty()) { const auto n = utf8_char_size(ipa, i); match = ipa.substr(i, n); }
        std::size_t end = i + match.size(); while (end < ipa.size() && ipa_modifier(ipa, end)) end += utf8_char_size(ipa, end);
        result.push_back(ipa.substr(i, end - i)); i = end;
    }
    return result;
}

std::string apply_allophony(const LanguageSpec& spec, const IPAPath& path) {
    if (spec.allophone_rules.empty() || path.graphemes.empty()) return path.ipa;
    std::vector<std::string> original = path.segments;
    original.resize(path.graphemes.size());
    std::size_t offset = 0;
    for (std::size_t i = 0; i < path.graphemes.size(); ++i) {
        if (!path.segments.empty()) { offset += original[i].size(); continue; }
        auto it = spec.graphemes.find(path.graphemes[i]);
        if (it != spec.graphemes.end()) for (const auto& candidate : it->second)
            if (!candidate.empty() && path.ipa.compare(offset, candidate.size(), candidate) == 0) { original[i] = candidate; break; }
        if (original[i].empty() && offset < path.ipa.size()) original[i] = path.ipa.substr(offset, 1);
        offset += original[i].size();
    }
    std::vector<std::string> segments = original; std::string word;
    for (const auto& g : path.graphemes) word += lower_ascii(g);

    // Segmental rules operate on phoneme-sized atoms inside a slot and can see
    // across slot boundaries. This is the distinction Python makes between
    // _realize_segments() and whole-candidate realization.
    std::vector<std::string> atoms;
    for (const auto& rule : spec.allophone_rules) {
        if (!rule.preceded_by_phoneme.empty() || !rule.followed_by_phoneme.empty()) {
            atoms.insert(atoms.end(), rule.phonemes.begin(), rule.phonemes.end());
            atoms.insert(atoms.end(), rule.preceded_by_phoneme.begin(), rule.preceded_by_phoneme.end());
            atoms.insert(atoms.end(), rule.followed_by_phoneme.begin(), rule.followed_by_phoneme.end());
            if (!rule.surface.empty()) atoms.push_back(rule.surface);
        }
    }
    std::vector<std::string> flat; std::vector<std::size_t> owners;
    for (std::size_t i = 0; i < original.size(); ++i) for (const auto& segment : segment_ipa(original[i], atoms)) { flat.push_back(segment); owners.push_back(i); }
    for (std::size_t i = 0; i < flat.size(); ++i) for (const auto& rule : spec.allophone_rules) {
        if (rule.preceded_by_phoneme.empty() && rule.followed_by_phoneme.empty()) continue;
        if (!in(flat[i], rule.phonemes)) continue;
        if (rule.word_initial && *rule.word_initial != (i == 0)) continue;
        if (rule.word_final && *rule.word_final != (i + 1 == flat.size())) continue;
        const auto owner = owners[i];
        if (!rule.graphemes.empty() && !in(path.graphemes[owner], rule.graphemes)) continue;
        if (!rule.word.empty() && !in(word, rule.word)) continue;
        if (!rule.preceded_by_grapheme.empty() && (owner == 0 || !in(path.graphemes[owner - 1], rule.preceded_by_grapheme))) continue;
        if (!rule.followed_by_grapheme.empty() && (owner + 1 == path.graphemes.size() || !in(path.graphemes[owner + 1], rule.followed_by_grapheme))) continue;
        if (owner + 1 < path.graphemes.size() && in(path.graphemes[owner + 1], rule.followed_by_grapheme_not)) continue;
        if (!rule.preceded_by_phoneme.empty() && (i == 0 || !in(flat[i - 1], rule.preceded_by_phoneme))) continue;
        if (!rule.followed_by_phoneme.empty() && (i + 1 == flat.size() || !in(flat[i + 1], rule.followed_by_phoneme))) continue;
        if (rule.stress == "stressed" && !stressed_grapheme(spec, path.graphemes, owner)) continue;
        if (rule.stress == "unstressed" && stressed_grapheme(spec, path.graphemes, owner)) continue;
        if (!rule.syllable_position.empty()) {
            const bool vowel = vowel_grapheme(path.graphemes[owner]);
            if (rule.syllable_position == "nucleus" && !vowel) continue;
            if (rule.syllable_position == "onset" && (owner + 1 == path.graphemes.size() || !vowel_grapheme(path.graphemes[owner + 1]))) continue;
            if (rule.syllable_position == "coda" && owner + 1 < path.graphemes.size() && vowel_grapheme(path.graphemes[owner + 1])) continue;
        }
        flat[i] = rule.append.empty() ? rule.surface : flat[i] + rule.append;
    }
    segments.assign(original.size(), "");
    for (std::size_t i = 0; i < flat.size(); ++i) segments[owners[i]] += flat[i];
    for (std::size_t i = 0; i < original.size(); ++i) for (const auto& rule : spec.allophone_rules) {
        if (!rule.preceded_by_phoneme.empty() || !rule.followed_by_phoneme.empty()) continue;
        if (!in(segments[i], rule.phonemes)) continue;
        if (rule.word_initial && *rule.word_initial != (i == 0)) continue;
        if (rule.word_final && *rule.word_final != (i + 1 == original.size())) continue;
        if (!rule.graphemes.empty() && !in(path.graphemes[i], rule.graphemes)) continue;
        if (!rule.word.empty() && !in(word, rule.word)) continue;
        if (!rule.word_contains_grapheme.empty() && !std::any_of(rule.word_contains_grapheme.begin(), rule.word_contains_grapheme.end(), [&](const auto& x) { return word.find(lower_ascii(x)) != std::string::npos; })) continue;
        if (std::any_of(rule.word_contains_grapheme_not.begin(), rule.word_contains_grapheme_not.end(), [&](const auto& x) { return word.find(lower_ascii(x)) != std::string::npos; })) continue;
        if (!rule.preceded_by_phoneme.empty() && (i == 0 || !in(original[i - 1], rule.preceded_by_phoneme))) continue;
        if (!rule.followed_by_phoneme.empty() && (i + 1 == original.size() || !in(original[i + 1], rule.followed_by_phoneme))) continue;
        if (!rule.preceded_by_phoneme_2.empty() && (i < 2 || !in(original[i - 2], rule.preceded_by_phoneme_2))) continue;
        if (!rule.followed_by_phoneme_2.empty() && (i + 2 >= original.size() || !in(original[i + 2], rule.followed_by_phoneme_2))) continue;
        if (!rule.preceded_by_surface_phoneme_2.empty() && (i < 2 || !in(original[i - 2], rule.preceded_by_surface_phoneme_2))) continue;
        if (!rule.preceded_by_grapheme.empty() && (i == 0 || !in(path.graphemes[i - 1], rule.preceded_by_grapheme))) continue;
        if (!rule.followed_by_grapheme.empty() && (i + 1 == path.graphemes.size() || !in(path.graphemes[i + 1], rule.followed_by_grapheme))) continue;
        if (i + 1 < path.graphemes.size() && in(path.graphemes[i + 1], rule.followed_by_grapheme_not)) continue;
        if (!rule.preceded_by.empty() && !class_match(rule.preceded_by, path.graphemes, original, i, -1)) continue;
        if (!rule.followed_by.empty() && !class_match(rule.followed_by, path.graphemes, original, i, 1)) continue;
        if (!rule.preceded_by_2.empty() && !class_match(rule.preceded_by_2, path.graphemes, original, i, -2)) continue;
        if (!rule.followed_by_2.empty() && !class_match(rule.followed_by_2, path.graphemes, original, i, 2)) continue;
        if (!rule.preceded_by_3.empty() && !class_match(rule.preceded_by_3, path.graphemes, original, i, -3)) continue;
        if (rule.stress == "stressed" && !stressed_grapheme(spec, path.graphemes, i)) continue;
        if (rule.stress == "unstressed" && stressed_grapheme(spec, path.graphemes, i)) continue;
        if (rule.stress == "pretonic" || rule.stress == "posttonic") {
            const auto stressed = stress_nucleus(spec, path.graphemes);
            bool before = false; for (std::size_t j = 0; j < i; ++j) before |= vowel_grapheme(path.graphemes[j]);
            bool after = false; for (std::size_t j = i + 1; j < path.graphemes.size(); ++j) after |= vowel_grapheme(path.graphemes[j]);
            if (!stressed || (rule.stress == "pretonic" ? !(before && i < *stressed) : !(after && i > *stressed))) continue;
        }
        bool other = false, later = false; for (std::size_t j = 0; j < path.graphemes.size(); ++j) if (j != i && vowel_grapheme(path.graphemes[j])) { other = true; later |= j > i; }
        if (rule.requires_other_nucleus && *rule.requires_other_nucleus != other) continue;
        if (rule.followed_by_nucleus && *rule.followed_by_nucleus != later) continue;
        if (rule.syllable_position == "nucleus" && !vowel_grapheme(path.graphemes[i])) continue;
        if (rule.syllable_position == "onset" && (i + 1 == path.graphemes.size() || !vowel_grapheme(path.graphemes[i + 1]))) continue;
        if (rule.syllable_position == "coda" && i + 1 < path.graphemes.size() && vowel_grapheme(path.graphemes[i + 1])) continue;
        segments[i] = rule.append.empty() ? rule.surface : original[i] + rule.append;
        if (!rule.mutates_neighbor.empty() && rule.mutates_neighbor_side == "preceding" && i > 0) segments[i - 1] = original[i - 1] + rule.mutates_neighbor;
        if (!rule.mutates_neighbor.empty() && rule.mutates_neighbor_side == "following" && i + 1 < segments.size()) segments[i + 1] = original[i + 1] + rule.mutates_neighbor;
        break;
    }
    std::string result; for (const auto& segment : segments) result += segment; return result;
}
}

std::vector<std::string> LanguageSpec::family_path() const {
    std::vector<std::string> out;
    std::stringstream stream(family); std::string part;
    while (std::getline(stream, part, '>')) { if (!part.empty() && part[0] == ' ') part.erase(0, 1); out.push_back(part); }
    return out;
}

Tokenizer::Tokenizer(const LanguageSpec& spec) : spec_(spec) {
    for (const auto& [key, _] : spec.graphemes) keys_.push_back(key);
    std::sort(keys_.begin(), keys_.end(), [](const auto& a, const auto& b) { return a.size() > b.size(); });
}

std::vector<std::string> Tokenizer::tokenize_word(const std::string& word, std::vector<std::string>* unmapped) const {
    std::vector<std::string> result; std::string lower = lower_ascii(word);
    for (std::size_t i = 0; i < lower.size();) {
        std::string match;
        for (const auto& key : keys_) if (lower.compare(i, key.size(), lower_ascii(key)) == 0) { match = key; break; }
        if (match.empty()) { if (unmapped) unmapped->push_back(word.substr(i, 1)); ++i; }
        else { result.push_back(match); i += match.size(); }
    }
    return result;
}

std::vector<IPAPath> Tokenizer::beam(const std::string& word, std::size_t width) const {
    std::vector<std::string> unmapped; auto gs = tokenize_word(word, &unmapped);
    std::vector<IPAPath> paths(1);
    for (std::size_t i = 0; i < gs.size(); ++i) {
        std::vector<IPAPath> next;
        auto values = spec_.graphemes.at(gs[i]);
        auto positional = spec_.positional_graphemes.find(gs[i]);
        if (positional != spec_.positional_graphemes.end()) {
            const std::string pos = i == 0 ? "word_initial" : (i + gs[i].size() >= word.size() ? "word_final" : "default");
            auto it = positional->second.find(pos); if (it != positional->second.end()) values = it->second;
            else if (auto d = positional->second.find("default"); d != positional->second.end()) values = d->second;
        }
        for (auto path : paths) for (std::size_t j = 0; j < values.size(); ++j) {
            path.ipa += values[j]; path.score += static_cast<double>(j); path.graphemes.push_back(gs[i]); path.segments.push_back(values[j]); next.push_back(std::move(path));
        }
        std::sort(next.begin(), next.end(), [](const auto& a, const auto& b) { return a.score < b.score; });
        if (next.size() > width) next.resize(width);
        paths = std::move(next);
    }
    if (paths.empty()) paths.push_back({"", 0.0, {}, {}});
    return paths;
}

std::string resolve(const std::string& code) {
    static const std::map<std::string, std::string> aliases{{"por", "pt-PT"}, {"eng", "en-GB"}, {"spa", "es-ES"}, {"fra", "fr-FR"}, {"deu", "de-DE"}, {"ita", "it-IT"}, {"pt", "pt-PT"}, {"en", "en-GB"}, {"es", "es-ES"}};
    auto it = aliases.find(code); if (it != aliases.end()) return it->second;
    for (const auto& c : available_codes(true)) if (lower_ascii(c) == lower_ascii(code)) return c;
    return code;
}
const LanguageSpec& get(const std::string& code) {
    const std::string canonical = resolve(code); std::set<std::string> loading; load_raw(canonical, loading); return cache.at(canonical);
}
void set_data_directory(const std::string& path) { data_dir = path; cache.clear(); }
void register_lexicon(const std::string& code, const std::string& source) { registered_lexicons[resolve(code)] = source; lexicon_cache.erase(resolve(code)); }
void set_lexicon_directory(const std::string& path) { lexicon_directory = path; lexicon_cache.clear(); }
void clear_lexicons() { registered_lexicons.clear(); lexicon_directory.reset(); lexicon_cache.clear(); }
std::vector<std::string> available_lexicon_codes() {
    std::set<std::string> codes; for (const auto& [code, _] : registered_lexicons) codes.insert(code);
    const char* env = std::getenv("ORTHOGRAPHY2IPA_LEXICON_DIR"); const std::string dir = lexicon_directory ? *lexicon_directory : (env ? env : "");
    if (!dir.empty() && fs::is_directory(dir)) for (const auto& e : fs::directory_iterator(dir)) if (e.path().extension() == ".tsv") codes.insert(e.path().stem().string());
    return {codes.begin(), codes.end()};
}
std::map<std::string, std::string> get_lexicon(const std::string& code) { return load_lexicon(resolve(code)); }
std::vector<std::pair<std::size_t, std::string>> validate_lexicon(const std::string& text) {
    std::vector<std::pair<std::size_t, std::string>> errors; std::set<std::string> seen; std::stringstream stream(text); std::string line; std::size_t n = 0;
    while (std::getline(stream, line)) { ++n; if (line.empty()) continue; auto tab = line.find('\t'); if (tab == std::string::npos || line.find('\t', tab + 1) != std::string::npos) { errors.emplace_back(n, "expected word<TAB>ipa"); continue; } auto word = line.substr(0, tab), ipa = line.substr(tab + 1); if (word.empty() || ipa.empty()) errors.emplace_back(n, "empty word or IPA"); if (word != lower_ascii(word)) errors.emplace_back(n, "word not lowercase"); if (!seen.insert(word).second) errors.emplace_back(n, "duplicate word"); }
    return errors;
}
void register_normalize_plugin(const std::string& name, std::shared_ptr<NormalizePlugin> plugin) { normalize_plugins[name] = std::move(plugin); }
void register_syllabifier_plugin(const std::string& name, std::shared_ptr<SyllabifierPlugin> plugin) { syllabifier_plugins[name] = std::move(plugin); }
void register_stress_plugin(const std::string& name, std::shared_ptr<StressPlugin> plugin) { stress_plugins[name] = std::move(plugin); }
void register_rescorer_plugin(const std::string& name, std::shared_ptr<RescorerPlugin> plugin) { rescorer_plugins[name] = std::move(plugin); }
void register_sandhi_plugin(const std::string& name, std::shared_ptr<SandhiPlugin> plugin) { sandhi_plugins[name] = std::move(plugin); }
std::vector<std::string> available_codes(bool include_clades) {
    std::vector<std::string> out; if (!fs::exists(data_dir)) return out;
    for (const auto& e : fs::directory_iterator(data_dir)) if (e.path().extension() == ".json") {
        const auto code = e.path().stem().string();
        if (include_clades) out.push_back(code);
        else if (code.rfind("x-clade-", 0) != 0) out.push_back(code);
    }
    std::sort(out.begin(), out.end()); return out;
}
std::map<std::string, std::vector<std::string>> available_families() {
    // Family derivation requires walking ancestry and would force parsing the
    // entire catalog. Keep this query cheap and report loaded metadata; callers
    // can load individual specs with get() when they need the full graph.
    std::map<std::string, std::vector<std::string>> result;
    for (const auto& [code, spec] : cache) if (!spec.clade) result[spec.family].push_back(code);
    return result;
}
std::string transcribe(const std::string& text, const std::string& language) { return G2P(language).transcribe(text); }

G2P::G2P(std::string language, std::map<std::string, std::vector<std::string>> plugin_overrides)
    : language_(resolve(language)), spec_(&get(language_)), plugin_overrides_(std::move(plugin_overrides)) {}
const LanguageSpec& G2P::spec() const { return *spec_; }
std::vector<IPAPath> G2P::candidates(const std::string& word, std::size_t width) const { return Tokenizer(*spec_).beam(word, width); }
double G2P::word_confidence(const std::string& word, std::size_t width) const {
    if (spec_->word_exceptions.count(lower_ascii(word)) || load_lexicon(language_).count(lower_ascii(word))) return 1.0;
    auto paths = candidates(word, width); return paths.size() > 1 ? 1.0 / (1.0 + paths[1].score) : 1.0;
}
TranscriptionResult G2P::transcribe_detailed(const std::string& text, const std::string& search, std::size_t width) const {
    if (search != "greedy" && search != "beam") throw std::invalid_argument("search must be greedy or beam");
    std::string normalized = text;
    auto stage = [&](const std::string& name) -> const std::vector<std::string>& { auto it = plugin_overrides_.find(name); return it == plugin_overrides_.end() ? plugin_names(*spec_, name) : it->second; };
    for (const auto& name : stage("normalize")) { auto it = normalize_plugins.find(name); if (it == normalize_plugins.end()) throw std::runtime_error("missing normalize plugin: " + name); normalized = it->second->normalize(normalized, language_); }
    TranscriptionResult result; result.lang = language_; std::vector<std::string> surfaces, ipa_words; std::vector<bool> pausal;
    for (const auto& word : words(normalized)) {
        WordTranscription wt; wt.word = word; auto it = spec_->word_exceptions.find(lower_ascii(word));
        auto paths = it != spec_->word_exceptions.end() ? std::vector<IPAPath>{{it->second, 0.0, {}, {}}} : candidates(word, search == "greedy" ? 1 : width);
        auto lex = it == spec_->word_exceptions.end() ? load_lexicon(language_) : std::map<std::string, std::string>{};
        if (it == spec_->word_exceptions.end()) { auto li = lex.find(lower_ascii(word)); if (li != lex.end()) paths = {{li->second, 0.0, {}, {}}}; }
        for (const auto& name : stage("rescore")) { auto p = rescorer_plugins.find(name); if (p == rescorer_plugins.end()) throw std::runtime_error("missing rescore plugin: " + name); paths = p->second->rescore(word, language_, paths); }
        wt.ipa = paths.front().graphemes.empty() ? paths.front().ipa : apply_allophony(*spec_, paths.front());
        std::optional<std::size_t> plugin_stress;
        auto stress_names = stage("stress");
        if (!stress_names.empty()) {
            auto syllables = std::vector<std::string>{}; for (const auto& g : paths.front().graphemes) if (vowel_grapheme(g)) syllables.push_back(g);
            auto plugin = stress_plugins.find(stress_names.front());
            if (plugin == stress_plugins.end()) throw std::runtime_error("missing stress plugin: " + stress_names.front());
            plugin_stress = plugin->second->stressed_index(word, syllables, language_);
        }
        if (!paths.front().graphemes.empty()) wt.ipa = add_stress(*spec_, word, IPAPath{wt.ipa, paths.front().score, paths.front().graphemes, paths.front().segments}, plugin_stress);
        wt.candidates = paths; wt.confidence = word_confidence(word, width);
        result.words.push_back(wt); surfaces.push_back(word); ipa_words.push_back(wt.ipa); pausal.push_back(false);
    }
    for (const auto& name : stage("sandhi")) { auto p = sandhi_plugins.find(name); if (p == sandhi_plugins.end()) throw std::runtime_error("missing sandhi plugin: " + name); ipa_words = p->second->apply(ipa_words, surfaces, pausal, language_); }
    for (std::size_t i = 0; i < ipa_words.size(); ++i) { if (i) result.ipa += " "; result.ipa += ipa_words[i]; result.words[i].ipa = ipa_words[i]; }
    return result;
}
std::string G2P::transcribe(const std::string& text, const std::string& search, std::size_t width) const { return transcribe_detailed(text, search, width).ipa; }

double segment_distance(const std::string& a, const std::string& b) { if (a == b) return 0; if (a.empty() || b.empty()) return 1; return 1.0; }
InventoryDistance inventory_distance(const LanguageSpec& a, const LanguageSpec& b) {
    auto x = inventory(a), y = inventory(b); std::size_t shared = 0; for (const auto& p : x) shared += y.count(p);
    std::set<std::string> union_set = x; union_set.insert(y.begin(), y.end());
    double j = union_set.empty() ? 0 : 1.0 - static_cast<double>(shared) / union_set.size();
    double f = (x.empty() || y.empty()) ? 1 : (j); return {j, f, x.size(), y.size(), shared};
}
GraphemeDivergence grapheme_divergence(const LanguageSpec& a, const LanguageSpec& b) {
    std::set<std::string> x, y; for (const auto& [k, _] : a.graphemes) x.insert(lower_ascii(k)); for (const auto& [k, _] : b.graphemes) y.insert(lower_ascii(k));
    std::size_t shared = 0; double total = 0; for (const auto& k : x) if (y.count(k)) { ++shared; total += a.graphemes.at(k).empty() || b.graphemes.at(k).empty() ? 1 : segment_distance(a.graphemes.at(k).front(), b.graphemes.at(k).front()); }
    std::set<std::string> u = x; u.insert(y.begin(), y.end()); return {shared, u.size(), shared ? total / shared : 1, u.empty() ? 0 : static_cast<double>(shared) / u.size()};
}
double allophone_overlap(const LanguageSpec& a, const LanguageSpec& b) { std::set<std::string> x, y; for (const auto& [_, v] : a.allophones) x.insert(v.begin(), v.end()); for (const auto& [_, v] : b.allophones) y.insert(v.begin(), v.end()); std::set<std::string> u = x; u.insert(y.begin(), y.end()); std::size_t n = 0; for (const auto& p : x) n += y.count(p); return u.empty() ? 1 : static_cast<double>(n) / u.size(); }
PhonologicalDistance phonological_distance(const LanguageSpec& a, const LanguageSpec& b) { auto i = inventory_distance(a, b); auto g = grapheme_divergence(a, b); auto o = allophone_overlap(a, b); return {i, g, o, .6 * i.feature_mean + .4 * (1 - o)}; }
double ancestry_similarity(const LanguageSpec& a, const LanguageSpec& b) { if (a.code == b.code) return 1; if (!a.parent.empty() && a.parent == b.code) return .95; if (!b.parent.empty() && b.parent == a.code) return .95; return (!a.family.empty() && a.family == b.family) ? .4 : 0; }
double geographic_distance(const LanguageSpec& a, const LanguageSpec& b, bool normalize) { if (!a.latitude || !a.longitude || !b.latitude || !b.longitude) return std::numeric_limits<double>::quiet_NaN(); constexpr double R = 6371.0088; auto rad=[](double d){return d*3.141592653589793/180;}; double p1=rad(*a.latitude), p2=rad(*b.latitude), dp=rad(*b.latitude-*a.latitude), dl=rad(*b.longitude-*a.longitude); double h=std::sin(dp/2)*std::sin(dp/2)+std::cos(p1)*std::cos(p2)*std::sin(dl/2)*std::sin(dl/2); double km=2*R*std::asin(std::sqrt(h)); return normalize ? km/(3.141592653589793*R) : km; }
} // namespace orthography2ipa
