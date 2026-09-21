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
#include <regex>
#include <mutex>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <dlfcn.h>
#include <unicode/normalizer2.h>
#include <unicode/unistr.h>
#include <unicode/uchar.h>

namespace orthography2ipa {
namespace {
using boost::property_tree::ptree;
namespace fs = std::filesystem;
std::string initial_data_directory() {
    if (const char* override_dir = std::getenv("ORTHOGRAPHY2IPA_DATA_DIR")) return override_dir;
    if (fs::is_directory(O2I_DEFAULT_DATA_DIR)) return O2I_DEFAULT_DATA_DIR;
    return O2I_INSTALL_DATA_DIR;
}
std::string data_dir = initial_data_directory();
std::map<std::string, LanguageSpec> cache;
std::map<std::string, std::string> registered_lexicons;
std::optional<std::string> lexicon_directory;
std::map<std::string, std::map<std::string, std::string>> lexicon_cache;
std::map<std::string, std::shared_ptr<NormalizePlugin>> normalize_plugins;
std::map<std::string, std::shared_ptr<SyllabifierPlugin>> syllabifier_plugins;
std::map<std::string, std::shared_ptr<StressPlugin>> stress_plugins;
std::map<std::string, std::shared_ptr<RescorerPlugin>> rescorer_plugins;
std::map<std::string, std::shared_ptr<SandhiPlugin>> sandhi_plugins;
std::vector<void*> plugin_handles;

std::string lower_ascii(std::string s) {
    for (char& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

std::string unicode_nfc(const std::string& input, bool fold_case) {
    UErrorCode status = U_ZERO_ERROR;
    const auto* normalizer = icu::Normalizer2::getNFCInstance(status);
    if (U_FAILURE(status)) throw std::runtime_error("unable to initialize Unicode NFC normalizer");
    icu::UnicodeString value = icu::UnicodeString::fromUTF8(input);
    if (fold_case) value.foldCase();
    icu::UnicodeString normalized; normalizer->normalize(value, normalized, status);
    if (U_FAILURE(status)) throw std::runtime_error("Unicode NFC normalization failed");
    std::string output; normalized.toUTF8String(output); return output;
}
std::string unicode_fold_nfc(const std::string& input) { return unicode_nfc(input, true); }

bool valid_ipa_string(const std::string& input) {
    static const std::string modifiers = "ːˑˈˌʰʷʲˠˤʼⁿ‿͜͡.|‖˥˦˧˨˩";
    if (input.empty() || unicode_nfc(input, false) != input) return false;
    const icu::UnicodeString value = icu::UnicodeString::fromUTF8(input);
    for (int32_t i = 0; i < value.length();) {
        UChar32 cp; U16_NEXT(value.getBuffer(), i, value.length(), cp);
        if (cp < 0) return false;
        std::string utf8; icu::UnicodeString(cp).toUTF8String(utf8);
        if (modifiers.find(utf8) != std::string::npos) continue;
        const auto category = u_charType(cp);
        if (category < U_UPPERCASE_LETTER || category > U_OTHER_LETTER) {
            if (category < U_NON_SPACING_MARK || category > U_ENCLOSING_MARK) return false;
        }
    }
    return true;
}

fs::path lexicon_cache_directory() {
    const char* xdg = std::getenv("XDG_CACHE_HOME");
    const char* home = std::getenv("HOME");
    return fs::path(xdg ? xdg : (home ? fs::path(home) / ".cache" : fs::temp_directory_path())) /
           "orthography2ipa" / "lexicons";
}

fs::path remote_cache_path(const std::string& source) {
    const auto hash = std::hash<std::string>{}(source);
    return lexicon_cache_directory() / (std::to_string(hash) + ".tsv");
}

std::string fetch_url(const std::string& url) {
    int pipefd[2];
    if (pipe(pipefd) != 0) throw std::runtime_error("unable to create lexicon download pipe");
    const pid_t pid = fork();
    if (pid == 0) {
        dup2(pipefd[1], STDOUT_FILENO); close(pipefd[0]); close(pipefd[1]);
        execlp("curl", "curl", "--fail", "--silent", "--show-error", "--location", url.c_str(), nullptr);
        _exit(127);
    }
    if (pid < 0) { close(pipefd[0]); close(pipefd[1]); throw std::runtime_error("unable to start lexicon download"); }
    close(pipefd[1]); std::string body; char buffer[8192]; ssize_t count;
    while ((count = read(pipefd[0], buffer, sizeof(buffer))) > 0) body.append(buffer, static_cast<std::size_t>(count));
    close(pipefd[0]); int status = 0; waitpid(pid, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) throw std::runtime_error("failed to fetch lexicon: " + url);
    return body;
}

fs::path resolve_remote_lexicon(const std::string& source) {
    const auto cached = remote_cache_path(source);
    if (fs::is_regular_file(cached)) return cached;
    std::string url = source;
    if (source.rfind("hf://", 0) == 0) {
        const std::string spec = source.substr(5); const auto slash = spec.find('/');
        const auto second = slash == std::string::npos ? std::string::npos : spec.find('/', slash + 1);
        if (slash == std::string::npos || second == std::string::npos)
            throw std::invalid_argument("hf lexicon must be hf://owner/repo/file[@revision]");
        const std::string repo = spec.substr(0, second); const std::string rest = spec.substr(second + 1);
        const auto at = rest.rfind('@'); const std::string file = at == std::string::npos ? rest : rest.substr(0, at);
        const std::string revision = at == std::string::npos ? "main" : rest.substr(at + 1);
        url = "https://huggingface.co/datasets/" + repo + "/resolve/" + revision + "/" + file;
    }
    fs::create_directories(cached.parent_path());
    std::ofstream output(cached, std::ios::binary); if (!output) throw std::runtime_error("unable to create lexicon cache");
    output << fetch_url(url); if (!output) throw std::runtime_error("unable to write lexicon cache");
    return cached;
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

std::vector<SandhiRule> parse_sandhi_rules(const ptree& p) {
    std::vector<SandhiRule> result;
    auto list = p.get_child_optional("sandhi_rules");
    if (!list) return result;
    for (const auto& item : *list) {
        const auto& r = item.second;
        SandhiRule rule;
        rule.id = r.get<std::string>("id", "");
        rule.name = r.get<std::string>("name", rule.id);
        rule.left_context = r.get<std::string>("left_context", "");
        rule.right_context = r.get<std::string>("right_context", "");
        if (auto value = r.get_optional<std::string>("transform"); value && *value != "null") rule.transform = *value;
        if (auto value = r.get_optional<std::string>("right_transform"); value && *value != "null") rule.right_transform = *value;
        rule.obligatory = r.get<bool>("obligatory", true);
        rule.notes = r.get<std::string>("notes", "");
        result.push_back(std::move(rule));
    }
    return result;
}

std::vector<SandhiRule> overlay_sandhi(const std::vector<SandhiRule>& base,
                                       const std::vector<SandhiRule>& own) {
    auto result = base;
    for (const auto& rule : own) {
        auto it = std::find_if(result.begin(), result.end(), [&](const auto& inherited) { return inherited.id == rule.id; });
        if (it == result.end()) result.push_back(rule);
        else *it = rule;
    }
    return result;
}

std::vector<AllophoneRule> overlay_allophone(const std::vector<AllophoneRule>& base,
                                             const std::vector<AllophoneRule>& own) {
    auto result = base;
    for (const auto& rule : own) {
        if (rule.id.empty()) { result.push_back(rule); continue; }
        auto it = std::find_if(result.begin(), result.end(), [&](const auto& inherited) { return inherited.id == rule.id; });
        if (it == result.end()) result.push_back(rule); else *it = rule;
    }
    return result;
}

template <typename T>
std::optional<T> optional_value(const ptree& p, const std::string& key) {
    return p.get_optional<T>(key);
}

std::vector<LinguisticSource> parse_sources(const ptree& p) {
    std::vector<LinguisticSource> out;
    if (auto values = p.get_child_optional("sources")) for (const auto& item : *values) {
        const auto& v = item.second; LinguisticSource source;
        source.id = v.get<std::string>("id", ""); source.author = v.get<std::string>("author", "");
        source.year = v.get<int>("year", 0); source.title = v.get<std::string>("title", "");
        source.publisher = v.get<std::string>("publisher", ""); source.url = v.get<std::string>("url", "");
        source.doi = v.get<std::string>("doi", ""); source.wikipedia_url = v.get<std::string>("wikipedia_url", "");
        source.pages = v.get<std::string>("pages", ""); source.notes = v.get<std::string>("notes", ""); out.push_back(std::move(source));
    }
    return out;
}

std::vector<OrthographyStandard> parse_orthography_standards(const ptree& p) {
    std::vector<OrthographyStandard> out; auto values = p.get_child_optional("orthography_standard");
    if (!values) return out;
    if (values->empty()) { out.push_back({values->data(), "", "", "", 0}); return out; }
    for (const auto& item : *values) {
        const auto& v = item.second; OrthographyStandard standard;
        standard.name = v.get<std::string>("name", v.data()); standard.authority = v.get<std::string>("authority", "");
        standard.year = v.get<int>("year", 0); standard.url = v.get<std::string>("url", "");
        standard.notes = v.get<std::string>("notes", ""); out.push_back(std::move(standard));
    }
    return out;
}

std::vector<std::string> optional_list(const ptree& p, const std::string& key) {
    if (auto child = p.get_child_optional(key)) return string_or_list(*child);
    return {};
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

void merge_positional(
    std::map<std::string, std::map<std::string, std::vector<std::string>>>& target,
    const std::map<std::string, std::map<std::string, std::vector<std::string>>>& base) {
    for (const auto& [grapheme, positions] : base) {
        auto& destination = target[grapheme];
        for (const auto& [position, values] : positions)
            if (!destination.count(position)) destination[position] = values;
    }
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
    s.notes = raw.get<std::string>("notes", "");
    s.glottolog_code = raw.get<std::string>("glottolog_code", ""); s.iso639_3 = raw.get<std::string>("iso639_3", "");
    s.wikidata_qid = raw.get<std::string>("wikidata_qid", ""); s.phoible_id = raw.get<std::string>("phoible_id", "");
    s.wals_code = raw.get<std::string>("wals_code", ""); s.sources = parse_sources(raw);
    s.orthography_standards = parse_orthography_standards(raw);
    s.wikipedia = optional_list(raw, "wikipedia"); s.urls = optional_list(raw, "urls");
    s.optional_marks = optional_list(raw, "optional_marks");
    for (const auto& key : {"glottolog_code", "iso639_3", "wikidata_qid", "phoible_id", "wals_code"})
        if (auto value = raw.get_optional<std::string>(key)) s.identifiers[key] = *value;
    s.script = raw.get<std::string>("script", "");
    s.script_type = raw.get<std::string>("script_type", "alphabet");
    s.inherent_vowel = raw.get<std::string>("inherent_vowel", "");
    s.inherent_vowel_final = raw.get<std::string>("inherent_vowel_final", "");
    s.virama_final_vowel = raw.get<std::string>("virama_final_vowel", "");
    s.coda_no_inherent_vowel = raw.get<bool>("coda_no_inherent_vowel", false);
    s.parent = raw.get<std::string>("parent", "");
    if (auto ancestors = raw.get_child_optional("ancestors")) for (const auto& item : *ancestors) {
        const auto& a = item.second; s.ancestors.push_back({a.get<std::string>("code", ""), a.get<std::string>("role", "parent"), a.get<std::string>("notes", ""), a.get<double>("weight", 1.0)});
    }
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
        s.vowel_letters = optional_strings(*stress, "vowel_letters");
        s.onset_clusters = optional_strings(*stress, "onset_clusters");
        s.quantity_sensitive = stress->get<bool>("quantity_sensitive", false);
        s.superheavy_final_attracts = stress->get<bool>("superheavy_final_attracts", false);
        s.max_onset = stress->get<int>("max_onset", 1);
        s.secondary_stress = stress->get<std::string>("secondary_stress", "");
        s.marked_vowels = optional_strings(*stress, "marked_vowels");
    }
    if (auto loc = raw.get_child_optional("location")) {
        s.latitude = loc->get<double>("latitude", 0); s.longitude = loc->get<double>("longitude", 0);
        s.location = Location{*s.latitude, *s.longitude, loc->get<std::string>("source", ""), loc->get<std::string>("notes", "")};
    }
    if (auto timespan = raw.get_child_optional("timespan")) {
        TimeSpan value; value.start_year = timespan->get<int>("start_year", 0);
        if (auto end = timespan->get_optional<int>("end_year")) value.end_year = *end;
        s.timespan = value;
    }
    if (auto tone = raw.get_child_optional("tone_inventory")) {
        ToneData value; value.no_mark = raw.get<std::string>("tone_rules.no_mark", "none");
        value.notes = raw.get<std::string>("tone_rules.notes", "");
        if (auto classes = tone->get_child_optional("classes")) for (const auto& x : *classes) value.classes[x.first] = x.second.data();
        if (auto marks = tone->get_child_optional("marks")) for (const auto& x : *marks) value.marks[x.first] = x.second.data();
        if (auto tones = tone->get_child_optional("tones")) for (const auto& x : *tones) value.tones[x.first] = x.second.data();
        value.dead_codas = optional_list(*tone, "dead_codas"); s.tone = value;
    }
    if (auto ex = raw.get_child_optional("word_exceptions"))
        for (const auto& x : *ex) s.word_exceptions[x.first] = x.second.data();
    if (auto endings = raw.get_child_optional("grammatical_endings")) {
        for (const auto& x : *endings) {
            std::vector<std::optional<std::string>> values;
            if (x.second.empty()) values.push_back(x.second.data());
            else {
                bool first = true;
                for (const auto& value : x.second) {
                    // property_tree represents JSON null and an empty string
                    // identically. In the ending schema null is only legal at
                    // rank one, so preserve that distinction by position.
                    if (first && value.second.data().empty()) values.push_back(std::nullopt);
                    else values.push_back(value.second.data());
                    first = false;
                }
            }
            if (!values.empty()) s.grammatical_endings[x.first] = std::move(values);
        }
    }
    s.allophone_rules = parse_allophone_rules(raw);
    s.allophone_passes = std::max(1, std::min(4, raw.get<int>("allophone_passes", 1)));
    s.sandhi_rules = parse_sandhi_rules(raw);
    if (auto plugins = raw.get_child_optional("plugins"))
        for (const auto& x : *plugins) s.plugins[x.first] = string_or_list(x.second);

    // Resolve the data inheritance used by the Python loader. Own entries override base entries.
    const std::string base = raw.get<std::string>("graphemes_base", "");
    const std::string allo_base = raw.get<std::string>("allophones_base", "");
    const std::string endings_base = raw.get<std::string>("grammatical_endings_base", "");
    if (!base.empty() && base != code) {
        LanguageSpec b = load_raw(base, loading);
        for (const auto& x : b.graphemes) if (!s.graphemes.count(x.first)) s.graphemes[x.first] = x.second;
        for (const auto& x : b.positional_graphemes)
            if (!s.positional_graphemes.count(x.first)) s.positional_graphemes[x.first] = x.second;
        const std::string positional_base = raw.get<std::string>("positional_graphemes_base", "");
        if (!positional_base.empty() && positional_base != code)
            merge_positional(s.positional_graphemes, load_raw(positional_base, loading).positional_graphemes);
        s.allophone_rules = overlay_allophone(b.allophone_rules, s.allophone_rules);
        s.sandhi_rules = overlay_sandhi(b.sandhi_rules, s.sandhi_rules);
        for (const auto& [stage, names] : b.plugins) if (!s.plugins.count(stage)) s.plugins[stage] = names;
        if (!raw.get_child_optional("stress") && s.default_stress_position == -2) {
            s.default_stress_position = b.default_stress_position; s.stress_mark = b.stress_mark;
            s.marked_vowels = b.marked_vowels; s.final_stress_endings = b.final_stress_endings;
            s.penult_stress_endings = b.penult_stress_endings; s.antepenult_stress_endings = b.antepenult_stress_endings;
            s.diphthongs = b.diphthongs; s.vowel_letters = b.vowel_letters;
            s.onset_clusters = b.onset_clusters; s.quantity_sensitive = b.quantity_sensitive;
            s.superheavy_final_attracts = b.superheavy_final_attracts; s.max_onset = b.max_onset;
            s.secondary_stress = b.secondary_stress;
        }
        if (s.family.empty()) s.family = b.family;
    }
    const std::string positional_base = raw.get<std::string>("positional_graphemes_base", "");
    if (!positional_base.empty() && positional_base != code)
        merge_positional(s.positional_graphemes, load_raw(positional_base, loading).positional_graphemes);
    if (!allo_base.empty() && allo_base != code) {
        LanguageSpec b = load_raw(allo_base, loading);
        for (const auto& x : b.allophones) if (!s.allophones.count(x.first)) s.allophones[x.first] = x.second;
        s.allophone_rules = overlay_allophone(b.allophone_rules, s.allophone_rules);
        s.sandhi_rules = overlay_sandhi(b.sandhi_rules, s.sandhi_rules);
    }
    if (!endings_base.empty() && endings_base != code) {
        LanguageSpec b = load_raw(endings_base, loading);
        for (const auto& [ending, values] : b.grammatical_endings)
            if (!s.grammatical_endings.count(ending)) s.grammatical_endings[ending] = values;
    }
    if (base.empty() && !s.parent.empty() && s.parent != code) {
        LanguageSpec ancestor = load_raw(s.parent, loading);
        std::set<std::string> seen{code, s.parent};
        while (ancestor.clade && !ancestor.parent.empty() && !seen.count(ancestor.parent)) {
            seen.insert(ancestor.parent); ancestor = load_raw(ancestor.parent, loading);
        }
        for (const auto& x : ancestor.graphemes) if (!s.graphemes.count(x.first)) s.graphemes[x.first] = x.second;
        for (const auto& x : ancestor.allophones) if (!s.allophones.count(x.first)) s.allophones[x.first] = x.second;
        merge_positional(s.positional_graphemes, ancestor.positional_graphemes);
        s.allophone_rules = overlay_allophone(ancestor.allophone_rules, s.allophone_rules);
        s.sandhi_rules = overlay_sandhi(ancestor.sandhi_rules, s.sandhi_rules);
    }
    if (!s.parent.empty() && s.parent != code) {
        std::vector<std::string> path; std::set<std::string> seen{code}; std::string ancestor = s.parent;
        while (!ancestor.empty() && !seen.count(ancestor)) {
            seen.insert(ancestor); LanguageSpec a = load_raw(ancestor, loading);
            if (a.clade && !a.name.empty()) path.push_back(a.name);
            ancestor = a.parent;
        }
        std::reverse(path.begin(), path.end());
        s.family_path_metadata = path;
        if (s.family.empty()) for (std::size_t i = 0; i < path.size(); ++i) { if (i) s.family += " > "; s.family += path[i]; }
    }
    loading.erase(code);
    cache[code] = s;
    return s;
}

std::size_t utf8_char_size(const std::string& text, std::size_t pos);

std::pair<std::vector<std::string>, std::vector<bool>> sentence_words(const std::string& text) {
    std::vector<std::string> result; std::vector<bool> pausal; std::string current;
    bool pause = false;
    auto flush = [&] { if (!current.empty()) { result.push_back(current); pausal.push_back(pause); current.clear(); pause = false; } };
    for (std::size_t i = 0; i < text.size();) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        std::size_t n = c < 0x80 ? 1 : utf8_char_size(text, i);
        const std::string character = text.substr(i, n);
        const bool ascii_pause = n == 1 && std::string(",.;:!?...").find(character) != std::string::npos;
        const bool unicode_pause = character == "\xD8\x8C" || character == "\xD8\x9F" || character == "\xE2\x80\xA6";
        if (c <= 32 || std::ispunct(c) || ascii_pause || unicode_pause) {
            if (ascii_pause || unicode_pause) pause = true;
            flush();
        } else current += character;
        i += n;
    }
    flush();
    return {std::move(result), std::move(pausal)};
}

struct InputWord { std::string surface, forced_ipa; bool forced = false, pausal = false; };

std::vector<InputWord> parse_input(const std::string& text,
                                   const std::function<std::string(const std::string&)>& normalize) {
    std::vector<InputWord> words;
    const std::regex tag(R"(<phoneme\b([^>]*)>([\s\S]*?)</phoneme\s*>)", std::regex::icase);
    std::smatch match; std::string remaining = text; bool found_tag = false;
    while (std::regex_search(remaining, match, tag)) {
        found_tag = true;
        auto plain = sentence_words(normalize(match.prefix().str()));
        for (std::size_t i = 0; i < plain.first.size(); ++i) words.push_back({plain.first[i], "", false, plain.second[i]});
        const std::string attrs = match[1].str();
        std::smatch ph; const std::regex ph_attr(R"(\bph\s*=\s*["']([^"']+)["'])", std::regex::icase);
        if (!std::regex_search(attrs, ph, ph_attr) || ph[1].str().empty())
            throw std::invalid_argument("<phoneme> requires a non-empty ph attribute");
        const std::string surface = match[2].str();
        if (surface.find_first_not_of(" \t\r\n") == std::string::npos)
            throw std::invalid_argument("<phoneme> must wrap text");
        words.push_back({surface, ph[1].str(), true, false});
        remaining = match.suffix().str();
    }
    auto plain = sentence_words(normalize(remaining));
    for (std::size_t i = 0; i < plain.first.size(); ++i) words.push_back({plain.first[i], "", false, plain.second[i]});
    if (!found_tag && text.find("<phoneme") != std::string::npos)
        throw std::invalid_argument("unclosed <phoneme> tag");
    return words;
}

bool in(const std::string& value, const std::vector<std::string>& list);
bool vowel_grapheme(const std::string& value);

unsigned int utf8_codepoint(const std::string& text, std::size_t pos) {
    const unsigned char c = static_cast<unsigned char>(text[pos]);
    if (c < 0x80) return c;
    if ((c & 0xe0) == 0xc0) return ((c & 0x1f) << 6) | (text[pos + 1] & 0x3f);
    if ((c & 0xf0) == 0xe0) return ((c & 0xf) << 12) | ((text[pos + 1] & 0x3f) << 6) | (text[pos + 2] & 0x3f);
    return ((c & 7) << 18) | ((text[pos + 1] & 0x3f) << 12) | ((text[pos + 2] & 0x3f) << 6) | (text[pos + 3] & 0x3f);
}

bool combining_mark(unsigned int cp) {
    return (cp >= 0x300 && cp <= 0x36f) || (cp >= 0x903 && cp <= 0x983) ||
           (cp >= 0x9bc && cp <= 0x9cd) || (cp >= 0xa3c && cp <= 0xa4d) ||
           (cp >= 0xabc && cp <= 0xacd) || (cp >= 0xb3c && cp <= 0xb4d) ||
           (cp >= 0xbcd && cp <= 0xbcd) || (cp >= 0xc3e && cp <= 0xc56) ||
           (cp >= 0xcc3 && cp <= 0xcdc) || (cp >= 0xd3b && cp <= 0xd4d) ||
           (cp >= 0x102b && cp <= 0x103e) || (cp >= 0x17b4 && cp <= 0x17d6);
}

std::string normalize_script_text(const LanguageSpec& spec, const std::string& input) {
    std::string text = input;
    if (spec.script.find("Arabic") != std::string::npos || spec.script_type == "abjad") {
        // Presentation forms are compatibility glyphs, not distinct letters.
        std::string decomposed;
        for (std::size_t i = 0; i < text.size();) {
            const auto n = utf8_char_size(text, i); const auto cp = utf8_codepoint(text, i);
            if ((cp >= 0xfb50 && cp <= 0xfdff) || (cp >= 0xfe70 && cp <= 0xfeff)) {
                if (cp == 0xfefb || cp == 0xfefc) decomposed += "لا";
                else decomposed += text.substr(i, n);
            } else decomposed += text.substr(i, n);
            i += n;
        }
        text = decomposed;
        // Expand consonant + shadda, accepting either mark ordering.
        std::string expanded;
        for (std::size_t i = 0; i < text.size();) {
            const auto n = utf8_char_size(text, i);
            if (i + n < text.size()) {
                const auto next = utf8_codepoint(text, i + n);
                if (next == 0x651) {
                    expanded += text.substr(i, n) + text.substr(i, n); i += n + utf8_char_size(text, i + n); continue;
                }
                const auto mark_n = utf8_char_size(text, i + n);
                if (i + n + mark_n < text.size() && utf8_codepoint(text, i + n + mark_n) == 0x651) {
                    expanded += text.substr(i, n) + text.substr(i, n) + text.substr(i + n, mark_n);
                    i += n + mark_n + utf8_char_size(text, i + n + mark_n); continue;
                }
            }
            expanded += text.substr(i, n); i += n;
        }
        text = expanded;
    }
    return text;
}

std::string add_stress(const LanguageSpec& spec, const std::string& word, const IPAPath& path, std::optional<std::size_t> forced = std::nullopt) {
    if (path.graphemes.empty() || spec.stress_mark.empty()) return path.ipa;
    std::vector<std::vector<std::size_t>> nuclei;
    auto ipa_vowel = [](const std::string& value) { return value.find_first_of("aeiouɑɐɒəɛɪɔʊɨʉɵøœy") != std::string::npos; };
    auto is_vowel = [&](const std::string& g, std::size_t i) { return (!spec.vowel_letters.empty() && in(g, spec.vowel_letters)) || (spec.vowel_letters.empty() && vowel_grapheme(g)) || (i < path.segments.size() && ipa_vowel(path.segments[i])); };
    for (std::size_t i = 0; i < path.graphemes.size();) {
        if (!is_vowel(path.graphemes[i], i)) { ++i; continue; }
        std::vector<std::size_t> group{ i }; std::size_t j = i + 1;
        while (j < path.graphemes.size() && is_vowel(path.graphemes[j], j)) {
            std::string joined; for (auto k : group) joined += lower_ascii(path.graphemes[k]); joined += lower_ascii(path.graphemes[j]);
            bool allowed = spec.diphthongs.empty();
            for (const auto& d : spec.diphthongs) if (lower_ascii(d).compare(0, joined.size(), joined) == 0) allowed = true;
            if (!allowed) break;
            group.push_back(j++);
            if (!spec.diphthongs.empty() && !std::any_of(spec.diphthongs.begin(), spec.diphthongs.end(), [&](const auto& d) { return lower_ascii(d) == joined; })) break;
        }
        nuclei.push_back(std::move(group)); i = j;
    }
    if (nuclei.empty()) return path.ipa;
    std::size_t target = forced ? std::min(*forced, nuclei.size() - 1) : nuclei.size() - 1;
    bool marked = forced.has_value();
    for (const auto& v : spec.marked_vowels) for (std::size_t n = 0; n < nuclei.size(); ++n)
        for (auto i : nuclei[n]) if (path.graphemes[i].find(v) != std::string::npos) { target = n; marked = true; }
    auto ends_with = [&](const std::vector<std::string>& endings) { return std::any_of(endings.begin(), endings.end(), [&](const auto& e) { return word.size() >= e.size() && word.compare(word.size() - e.size(), e.size(), e) == 0; }); };
    if (forced) {
    } else if (spec.quantity_sensitive) {
        auto heavy = [&](std::size_t n) { std::string nucleus; for (auto i : nuclei[n]) if (i < path.segments.size()) nucleus += path.segments[i]; const bool long_vowel = nucleus.find("ː") != std::string::npos || nucleus.find("ˑ") != std::string::npos; const bool coda = nuclei[n].back() + 1 < (n + 1 < nuclei.size() ? nuclei[n + 1].front() : path.graphemes.size()); return long_vowel || coda; };
        if (spec.superheavy_final_attracts && heavy(nuclei.size() - 1) && nuclei.back().back() + 1 < path.graphemes.size()) target = nuclei.size() - 1;
        else if (nuclei.size() > 1 && heavy(nuclei.size() - 2)) target = nuclei.size() - 2;
        else { const int p = spec.default_stress_position; target = p > 0 ? static_cast<std::size_t>(std::min<int>(p - 1, nuclei.size() - 1)) : static_cast<std::size_t>(std::max<int>(0, static_cast<int>(nuclei.size()) + p)); }
    } else if (ends_with(spec.final_stress_endings)) target = nuclei.size() - 1;
    else if (ends_with(spec.penult_stress_endings)) target = nuclei.size() > 1 ? nuclei.size() - 2 : 0;
    else if (ends_with(spec.antepenult_stress_endings)) target = nuclei.size() > 2 ? nuclei.size() - 3 : 0;
    else if (!marked) { const int p = spec.default_stress_position; target = p > 0 ? static_cast<std::size_t>(std::min<int>(p - 1, nuclei.size() - 1)) : static_cast<std::size_t>(std::max<int>(0, static_cast<int>(nuclei.size()) + p)); }
    auto onset_for = [&](std::size_t n) {
        if (n == 0) return std::size_t{0};
        // The grapheme immediately after the preceding nucleus is the
        // syllable boundary. IPA onset constraints affect boundary drawing,
        // but never move the nucleus selected for stress.
        return nuclei[n - 1].back() + 1;
    };
    auto offset_for = [&](std::size_t grapheme) { std::size_t offset = 0; for (std::size_t i = 0; i < grapheme; ++i) if (i < path.segments.size()) offset += path.segments[i].size(); return offset; };
    std::vector<std::pair<std::size_t, std::string>> marks;
    if (spec.secondary_stress == "alternating") for (std::size_t n = target; n >= 2; n -= 2) marks.push_back({offset_for(onset_for(n - 2)), "ˌ"});
    marks.push_back({offset_for(onset_for(target)), spec.stress_mark});
    std::sort(marks.rbegin(), marks.rend());
    std::string result = path.ipa; for (const auto& [offset, mark] : marks) result.insert(std::min(offset, result.size()), mark); return result;
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

template <typename Plugin>
bool owns_language(const Plugin& plugin, const std::string& language) {
    const auto codes = plugin.language_codes();
    return std::find(codes.begin(), codes.end(), language) != codes.end() ||
           std::find(codes.begin(), codes.end(), "*") != codes.end();
}

std::vector<std::string> fallback_syllables(const std::string& word) {
    std::vector<std::string> result; std::string current; bool nucleus = false;
    for (std::size_t i = 0; i < word.size();) {
        const auto n = utf8_char_size(word, i); const auto g = word.substr(i, n); current += g;
        if (vowel_grapheme(g)) nucleus = true;
        if (nucleus && i + n < word.size() && vowel_grapheme(word.substr(i + n, utf8_char_size(word, i + n)))) {
            result.push_back(current); current.clear(); nucleus = false;
        }
        i += n;
    }
    if (!current.empty()) result.push_back(current);
    return result;
}

std::vector<std::string> syllables_for(const std::string& word, const std::string& language,
                                       const std::vector<std::string>& requested = {}) {
    const SyllabifierPlugin* selected = nullptr;
    if (!requested.empty()) {
        for (const auto& name : requested) {
            auto it = syllabifier_plugins.find(name);
            if (it == syllabifier_plugins.end())
                throw std::runtime_error("missing syllabify plugin: " + name + "; installed: " +
                                         (syllabifier_plugins.empty() ? std::string("(none)") :
                                          [&] { std::string names; for (const auto& p : syllabifier_plugins) { if (!names.empty()) names += ", "; names += p.first; } return names; }()));
            if (!owns_language(*it->second, language))
                throw std::runtime_error("syllabify plugin does not own language " + language + ": " + name);
            if (!selected || it->second->priority() > selected->priority()) selected = it->second.get();
        }
    } else {
        for (const auto& [_, plugin] : syllabifier_plugins)
            if (owns_language(*plugin, language) && (!selected || plugin->priority() > selected->priority())) selected = plugin.get();
    }
    if (selected) {
        auto result = selected->syllabify(word, language);
        std::string joined; for (const auto& syllable : result) joined += syllable;
        if (joined != word) throw std::runtime_error("syllabifier plugin returned a non-round-tripping result");
        const auto again = selected->syllabify(word, language);
        if (again != result) throw std::runtime_error("syllabify plugin returned non-deterministic output");
        return result;
    }
    return fallback_syllables(word);
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
        fs::path path = source;
        if (source.rfind("http://", 0) == 0 || source.rfind("https://", 0) == 0 || source.rfind("hf://", 0) == 0)
            path = resolve_remote_lexicon(source);
        std::ifstream input(path); if (!input) throw std::runtime_error("lexicon not found: " + source);
        std::string line;
        while (std::getline(input, line)) {
            const auto tab = line.find('\t'); if (tab == std::string::npos || tab == 0 || tab + 1 >= line.size()) continue;
            const std::string word = unicode_fold_nfc(line.substr(0, tab));
            const std::string ipa = unicode_nfc(line.substr(tab + 1), false);
            if (!result.count(word)) result[word] = ipa;
        }
    }
    lexicon_cache[code] = result; return result;
}

bool in(const std::string& value, const std::vector<std::string>& list) { return std::find(list.begin(), list.end(), value) != list.end(); }
bool vowel_grapheme(const std::string& value) { return value.find_first_of("aeiouAEIOUáéíóúâêôãõÁÉÍÓÚÂÊÔÃÕ") != std::string::npos; }
bool front_grapheme(const std::string& value);
bool back_grapheme(const std::string& value);
bool palatal_ipa(const std::string& value);

std::string replacement_pattern(std::string replacement) {
    // Python's re.sub uses \1; std::regex_replace uses $1.
    for (std::size_t i = 0; i + 1 < replacement.size(); ++i)
        if (replacement[i] == '\\' && replacement[i + 1] >= '1' && replacement[i + 1] <= '9') {
            replacement[i] = '$'; replacement.erase(i + 1, 1);
        }
    return replacement;
}

std::vector<std::string> apply_sandhi(const LanguageSpec& spec, std::vector<std::string> ipa,
                                      const std::vector<bool>& pausal) {
    if (pausal.size() != ipa.size()) throw std::invalid_argument("pausal flag count must match word count");
    if (ipa.size() < 2 || spec.sandhi_rules.empty()) return ipa;
    for (std::size_t i = 0; i + 1 < ipa.size(); ++i) {
        if (pausal[i]) continue;
        const auto left = ipa[i], right = ipa[i + 1];
        bool left_done = false, right_done = false;
        for (const auto& rule : spec.sandhi_rules) {
            if (left_done && right_done) break;
            try {
                const std::regex l(rule.left_context), r(rule.right_context);
                if (!std::regex_search(left, l) || !std::regex_search(right, r)) continue;
                if (rule.transform && !left_done) { ipa[i] = std::regex_replace(left, l, replacement_pattern(*rule.transform)); left_done = true; }
                if (rule.right_transform && !right_done) { ipa[i + 1] = std::regex_replace(right, r, replacement_pattern(*rule.right_transform)); right_done = true; }
            } catch (const std::regex_error& e) { throw std::runtime_error("invalid sandhi rule " + rule.id + ": " + e.what()); }
        }
    }
    return ipa;
}

bool class_match(const std::string& cls, const std::vector<std::string>& gs,
                 const std::vector<std::string>& phones, std::size_t index, int distance) {
    const long pos = static_cast<long>(index) + distance;
    if (cls == "word_boundary") return pos < 0 || pos >= static_cast<long>(gs.size());
    if (cls == "any") return pos >= 0 && pos < static_cast<long>(gs.size());
    if (pos < 0 || pos >= static_cast<long>(gs.size())) return false;
    const auto p = static_cast<std::size_t>(pos);
    if (cls == "vowel") return vowel_grapheme(gs[p]);
    if (cls == "front_vowel") return vowel_grapheme(gs[p]) && front_grapheme(gs[p]);
    if (cls == "back_vowel") return vowel_grapheme(gs[p]) && back_grapheme(gs[p]);
    if (cls == "consonant" || cls == "coda" || cls == "onset") return !vowel_grapheme(gs[p]);
    if (cls == "consonant_cluster") {
        const long next = pos + (distance < 0 ? -1 : 1);
        return !vowel_grapheme(gs[p]) && next >= 0 && next < static_cast<long>(gs.size()) && !vowel_grapheme(gs[static_cast<std::size_t>(next)]);
    }
    if (cls == "coda_nasal") return !vowel_grapheme(gs[p]) && !phones[p].empty() && std::string("mnɲŋɳɴ").find(phones[p][0]) != std::string::npos;
    if (cls == "palatal") return palatal_ipa(phones[p]);
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

bool front_grapheme(const std::string& value) {
    return value.find_first_of("eEiIyYéÉêÊíÍëËïÏ") != std::string::npos;
}
bool back_grapheme(const std::string& value) {
    return value.find_first_of("aAoOuUáÁâÂãÃóÓôÔõÕ") != std::string::npos;
}
bool palatal_ipa(const std::string& value) {
    return value.find_first_of("ʃʒɲʎjtɕdʑ") != std::string::npos || value.find("ʲ") != std::string::npos;
}

std::vector<std::string> positional_values(const LanguageSpec& spec,
                                           const std::vector<std::string>& gs,
                                           std::size_t index,
                                           const std::vector<std::string>& base) {
    auto entry = spec.positional_graphemes.find(gs[index]);
    if (entry == spec.positional_graphemes.end()) return base;
    const auto& positions = entry->second;
    std::vector<std::string> candidates;
    auto choose = [&](const std::string& position) {
        auto it = positions.find(position);
        if (it != positions.end()) { candidates = it->second; return true; }
        return false;
    };
    const bool initial = index == 0;
    const bool final = index + 1 == gs.size();
    const bool vowel = vowel_grapheme(gs[index]);
    const bool prev_vowel = index > 0 && vowel_grapheme(gs[index - 1]);
    const bool next_vowel = index + 1 < gs.size() && vowel_grapheme(gs[index + 1]);
    const auto next_ipa = index + 1 < gs.size() && !spec.graphemes.at(gs[index + 1]).empty()
        ? spec.graphemes.at(gs[index + 1]).front() : std::string{};
    const auto prev_ipa = index > 0 && !spec.graphemes.at(gs[index - 1]).empty()
        ? spec.graphemes.at(gs[index - 1]).front() : std::string{};

    // Most-specific contexts first, matching positional.py.
    if (index + 1 < gs.size()) {
        const std::string next = lower_ascii(gs[index + 1]);
        const std::string exact = next.empty() ? "" : std::string(1, next[0]);
        if (!exact.empty() && choose("before_" + exact)) return candidates;
        if (front_grapheme(next) && choose("before_front_vowel")) return candidates;
        if (back_grapheme(next) && choose("before_back_vowel")) return candidates;
        if (palatal_ipa(next_ipa) && choose("before_palatal")) return candidates;
    }
    if (initial && choose("word_initial")) return candidates;
    if (final && choose("word_final")) return candidates;
    if (prev_vowel && next_vowel && choose("intervocalic")) return candidates;

    if (vowel) {
        const auto stressed = stress_nucleus(spec, gs);
        if (stressed && *stressed == index && choose("nucleus_stressed")) return candidates;
        if (stressed && *stressed != index) {
            bool before_stress = index < *stressed;
            if (before_stress && choose("first_pretonic")) return candidates;
            if (before_stress && choose("pretonic")) return candidates;
            if (!before_stress && choose("posttonic")) return candidates;
            if (choose("nucleus_unstressed")) return candidates;
        }
        if (choose("nucleus")) return candidates;
    }
    if (prev_vowel && choose("after_vowel")) return candidates;
    if (!prev_vowel && index > 0 && choose("after_consonant")) return candidates;
    if (next_vowel && choose("before_vowel")) return candidates;
    if (!next_vowel && index + 1 < gs.size() && choose("before_consonant")) return candidates;
    if (choose("default")) return candidates;
    return base;
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

bool same_paths(const std::vector<IPAPath>& a, const std::vector<IPAPath>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i].ipa != b[i].ipa || a[i].score != b[i].score ||
            a[i].graphemes != b[i].graphemes || a[i].segments != b[i].segments) return false;
    return true;
}

void validate_rescorer_output(const std::vector<IPAPath>& paths, const LanguageSpec& spec,
                              const std::string& name) {
    if (paths.empty()) throw std::runtime_error("rescore plugin returned no candidates: " + name);
    const auto declared = inventory(spec);
    const std::vector<std::string> atoms(declared.begin(), declared.end());
    for (const auto& path : paths) {
        for (const auto& segment : segment_ipa(path.ipa, atoms)) {
            if (segment == "ˈ" || segment == "ˌ" || segment == "ː" || segment == "ˑ") continue;
            bool known = declared.count(segment) != 0;
            if (!known) for (const auto& atom : atoms) {
                if (segment.rfind(atom, 0) != 0) continue;
                known = true;
                for (std::size_t offset = atom.size(); offset < segment.size();) {
                    if (!ipa_modifier(segment, offset)) { known = false; break; }
                    offset += utf8_char_size(segment, offset);
                }
                if (known) break;
            }
            if (!known)
                throw std::runtime_error("rescore plugin emitted undeclared IPA " + segment + ": " + name);
        }
    }
}

std::string apply_allophony(const LanguageSpec& spec, IPAPath& path) {
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
         if (!rule.word_contains_grapheme.empty() && !std::any_of(rule.word_contains_grapheme.begin(), rule.word_contains_grapheme.end(), [&](const auto& x) { return word.find(lower_ascii(x)) != std::string::npos; })) continue;
         if (std::any_of(rule.word_contains_grapheme_not.begin(), rule.word_contains_grapheme_not.end(), [&](const auto& x) { return word.find(lower_ascii(x)) != std::string::npos; })) continue;
         if (!rule.preceded_by_grapheme.empty() && (owner == 0 || !in(path.graphemes[owner - 1], rule.preceded_by_grapheme))) continue;
         if (!rule.followed_by_grapheme.empty() && (owner + 1 == path.graphemes.size() || !in(path.graphemes[owner + 1], rule.followed_by_grapheme))) continue;
        if (owner + 1 < path.graphemes.size() && in(path.graphemes[owner + 1], rule.followed_by_grapheme_not)) continue;
         if (!rule.preceded_by_phoneme.empty() && (i == 0 || !in(flat[i - 1], rule.preceded_by_phoneme))) continue;
         if (!rule.followed_by_phoneme.empty() && (i + 1 == flat.size() || !in(flat[i + 1], rule.followed_by_phoneme))) continue;
         if (!rule.preceded_by.empty() && !class_match(rule.preceded_by, path.graphemes, original, owner, -1)) continue;
         if (!rule.followed_by.empty() && !class_match(rule.followed_by, path.graphemes, original, owner, 1)) continue;
         if (!rule.preceded_by_2.empty() && !class_match(rule.preceded_by_2, path.graphemes, original, owner, -2)) continue;
         if (!rule.followed_by_2.empty() && !class_match(rule.followed_by_2, path.graphemes, original, owner, 2)) continue;
         if (!rule.preceded_by_3.empty() && !class_match(rule.preceded_by_3, path.graphemes, original, owner, -3)) continue;
         if (!rule.preceded_by_phoneme_2.empty() && (i < 2 || !in(flat[i - 2], rule.preceded_by_phoneme_2))) continue;
         if (!rule.followed_by_phoneme_2.empty() && (i + 2 >= flat.size() || !in(flat[i + 2], rule.followed_by_phoneme_2))) continue;
         bool other_nucleus = false;
         for (std::size_t j = 0; j < path.graphemes.size(); ++j)
             if (j != owner && vowel_grapheme(path.graphemes[j])) other_nucleus = true;
         if (rule.requires_other_nucleus && *rule.requires_other_nucleus != other_nucleus) continue;
         if (rule.followed_by_nucleus && *rule.followed_by_nucleus != std::any_of(path.graphemes.begin() + static_cast<std::ptrdiff_t>(owner + 1), path.graphemes.end(), vowel_grapheme)) continue;
        if (rule.stress == "stressed" && !stressed_grapheme(spec, path.graphemes, owner)) continue;
        if (rule.stress == "unstressed" && stressed_grapheme(spec, path.graphemes, owner)) continue;
        if (!rule.syllable_position.empty()) {
            const bool vowel = vowel_grapheme(path.graphemes[owner]);
            if (rule.syllable_position == "nucleus" && !vowel) continue;
            if (rule.syllable_position == "onset" && (owner + 1 == path.graphemes.size() || !vowel_grapheme(path.graphemes[owner + 1]))) continue;
            if (rule.syllable_position == "coda" && owner + 1 < path.graphemes.size() && vowel_grapheme(path.graphemes[owner + 1])) continue;
        }
        flat[i] = rule.append.empty() ? rule.surface : flat[i] + rule.append;
        if (!rule.mutates_neighbor.empty()) {
            if (rule.mutates_neighbor_side == "preceding" && i > 0)
                flat[i - 1] += rule.mutates_neighbor;
            if (rule.mutates_neighbor_side == "following" && i + 1 < flat.size())
                flat[i + 1] += rule.mutates_neighbor;
        }
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
    path.segments = segments;
    std::string result; for (const auto& segment : segments) result += segment; return result;
}

std::vector<IPAPath> apply_grammatical_endings(const LanguageSpec& spec,
                                               std::vector<IPAPath> paths) {
    if (paths.empty() || spec.grammatical_endings.empty()) return paths;
    std::string surface;
    for (const auto& g : paths.front().graphemes) surface += lower_ascii(g);
    std::string ending;
    const std::vector<std::optional<std::string>>* values = nullptr;
    for (const auto& [candidate, declared] : spec.grammatical_endings) {
        if (surface.size() > candidate.size() && surface.size() >= candidate.size() &&
            surface.compare(surface.size() - candidate.size(), candidate.size(), lower_ascii(candidate)) == 0 &&
            (values == nullptr || candidate.size() > ending.size())) {
            ending = candidate; values = &declared;
        }
    }
    if (!values || ending.empty()) return paths;
    std::size_t bytes = 0, tokens = 0;
    for (auto it = paths.front().graphemes.rbegin(); it != paths.front().graphemes.rend() && bytes < ending.size(); ++it) {
        bytes += it->size(); ++tokens;
    }
    if (tokens >= paths.front().segments.size()) return paths;
    const auto rank1 = values->empty() ? std::optional<std::string>{} : (*values)[0];
    std::vector<IPAPath> rewritten;
    auto rewrite = [&](const IPAPath& path, const std::optional<std::string>& value, double cost) {
        if (!value || path.segments.size() <= tokens) return path;
        IPAPath out = path;
        out.segments.resize(path.segments.size() - tokens);
        out.segments.push_back(*value); out.ipa.clear();
        for (const auto& segment : out.segments) out.ipa += segment;
        out.graphemes.resize(path.graphemes.size() - tokens);
        out.graphemes.push_back(ending); out.score += cost;
        return out;
    };
    for (const auto& path : paths) rewritten.push_back(rewrite(path, rank1, 0));
    for (std::size_t i = 1; i < values->size(); ++i)
        rewritten.push_back(rewrite(paths.front(), (*values)[i], static_cast<double>(i)));
    std::sort(rewritten.begin(), rewritten.end(), [](const auto& a, const auto& b) { return a.score < b.score; });
    return rewritten;
}

bool dialect_profile_known(const std::string& profile) {
    static const std::set<std::string> profiles{
        "estremenho", "lisbon", "ribatejano", "beira_baixa", "algarve_barlavento",
        "northern", "transmontano", "baixo_minhoto", "porto", "beira_alta",
        "galician", "galician_west", "leonese", "rionorese", "guadramilese"};
    return profiles.count(profile) != 0;
}

bool transform_context(const std::string& ipa, std::size_t pos, std::size_t length,
                       const std::string& context, const std::string& orthography) {
    if (context.empty()) return true;
    if (context == "word_final") return pos + length >= ipa.size() || ipa[pos + length] == ' ';
    if (context == "ortho_has_ou") return lower_ascii(orthography).find("ou") != std::string::npos;
    if (context == "ortho_source_is_ch") return lower_ascii(orthography).find("ch") != std::string::npos;
    if (context == "ortho_source_is_z") return lower_ascii(orthography).find('z') != std::string::npos;
    if (context == "stressed") {
        return ipa.rfind("ˈ", pos) != std::string::npos;
    }
    if (context == "unstressed_pretonic") {
        return ipa.find("ˈ", pos) != std::string::npos;
    }
    return false;
}

std::string replace_rule(std::string ipa, const std::string& from, const std::string& to,
                         const std::string& context = "", const std::string& orthography = "") {
    if (from.empty()) return ipa;
    for (std::size_t pos = 0; (pos = ipa.find(from, pos)) != std::string::npos;) {
        if (transform_context(ipa, pos, from.size(), context, orthography)) { ipa.replace(pos, from.size(), to); pos += to.size(); }
        else pos += from.size();
    }
    return ipa;
}

std::string apply_dialect_impl(std::string ipa, const std::string& profile, const std::string& orthography) {
    if (profile == "rionorese" || profile == "guadramilese") {
        std::stringstream sounds(ipa), spellings(orthography); std::vector<std::string> iw, ow; std::string value;
        while (sounds >> value) iw.push_back(value);
        while (spellings >> value) ow.push_back(value);
        if (iw.size() == ow.size()) for (std::size_t i = 0; i < iw.size(); ++i) {
            if (profile == "rionorese" && ow[i] == "o") iw[i] = replace_rule(iw[i], "u", "al");
            if (profile == "rionorese" && ow[i] == "eu") iw[i] = replace_rule(iw[i], "ew", "jew");
            if (profile == "guadramilese" && ow[i] == "eu") iw[i] = replace_rule(iw[i], "ew", "jow");
            if (profile == "guadramilese" && ow[i] == "ia") iw[i] = replace_rule(iw[i], "iɐ", "dibɐ");
        }
        ipa.clear(); for (const auto& word : iw) { if (!ipa.empty()) ipa += " "; ipa += word; }
    }
    if (profile == "lisbon") { ipa = replace_rule(ipa, "ej", "ɐj"); ipa = replace_rule(ipa, "ow", "o"); ipa = replace_rule(ipa, "e", "ɨ", "unstressed_pretonic", orthography); ipa = replace_rule(ipa, "l", "ɫ"); return ipa; }
    const bool northern = profile == "northern" || profile == "transmontano" || profile == "baixo_minhoto" || profile == "porto" || profile == "beira_alta" || profile == "leonese" || profile == "rionorese" || profile == "guadramilese";
    const bool galician = profile == "galician" || profile == "galician_west";
    if (northern || galician) { ipa = replace_rule(ipa, "v", "b"); ipa = replace_rule(ipa, "o", "ow", "ortho_has_ou", orthography); }
    if (galician) { ipa = replace_rule(ipa, "ʒ", "ʃ"); ipa = replace_rule(ipa, "z", "s"); if (profile == "galician_west") ipa = replace_rule(ipa, "ɡ", "x"); }
    if (profile == "transmontano" || profile == "leonese" || profile == "rionorese" || profile == "guadramilese") { ipa = replace_rule(ipa, "ʃ", "tʃ", "ortho_source_is_ch", orthography); ipa = replace_rule(ipa, "s", "s̺"); }
    if (profile == "baixo_minhoto" || profile == "beira_alta" || profile == "porto") { ipa = replace_rule(ipa, "s", "s̺"); ipa = replace_rule(ipa, "z", "z̺"); }
    if (profile == "ribatejano" || profile == "beira_baixa" || profile == "algarve_barlavento") ipa = replace_rule(ipa, "ej", "e");
    if (profile == "beira_baixa") ipa = replace_rule(ipa, "u", "y", "stressed");
    if (profile == "algarve_barlavento") {
        ipa = replace_rule(ipa, "a", "\x01"); ipa = replace_rule(ipa, "ɔ", "\x02");
        ipa = replace_rule(ipa, "o", "\x03"); ipa = replace_rule(ipa, "u", "\x04");
        ipa = replace_rule(ipa, "ɛ", "\x05"); ipa = replace_rule(ipa, "e", "\x06");
        ipa = replace_rule(ipa, "\x01", "ɔ"); ipa = replace_rule(ipa, "\x02", "o");
        ipa = replace_rule(ipa, "\x03", "u"); ipa = replace_rule(ipa, "\x04", "y");
        ipa = replace_rule(ipa, "\x05", "æ"); ipa = replace_rule(ipa, "\x06", "ɛ");
    }
    return ipa;
}
}

std::vector<std::string> LanguageSpec::family_path() const {
    if (!family_path_metadata.empty()) return family_path_metadata;
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
    std::vector<std::string> result; std::string lower = normalize_script_text(spec_, word);
    lower = lower_ascii(lower);
    for (std::size_t i = 0; i < lower.size();) {
        std::string match;
        for (const auto& key : keys_) if (lower.compare(i, key.size(), lower_ascii(key)) == 0) { match = key; break; }
        if (match.empty()) { if (unmapped) unmapped->push_back(word.substr(i, 1)); ++i; }
        else { result.push_back(match); i += match.size(); }
    }
    return result;
}

std::vector<IPAPath> Tokenizer::beam(const std::string& word, std::size_t width) const {
    std::vector<std::string> unmapped; const std::string normalized = normalize_script_text(spec_, word); auto gs = tokenize_word(normalized, &unmapped);
    std::vector<IPAPath> paths(1);
    for (std::size_t i = 0; i < gs.size(); ++i) {
        std::vector<IPAPath> next;
        auto values = positional_values(spec_, gs, i, spec_.graphemes.at(gs[i]));
        const bool abugida = spec_.script_type == "abugida" && !spec_.inherent_vowel.empty();
        const bool current_consonant = !vowel_grapheme(gs[i]) && !values.empty();
        const bool next_supplies_vowel = i + 1 < gs.size() && (vowel_grapheme(gs[i + 1]) ||
            (!gs[i + 1].empty() && combining_mark(utf8_codepoint(gs[i + 1], 0))));
        if (abugida && current_consonant && !next_supplies_vowel) {
            const bool final = i + 1 == gs.size();
            if (!final || spec_.inherent_vowel_final.empty()) {
                for (auto& value : values) if (!value.empty()) value += spec_.inherent_vowel;
            } else {
                for (auto& value : values) if (!value.empty()) value += spec_.inherent_vowel_final;
            }
        }
        for (auto path : paths) for (std::size_t j = 0; j < values.size(); ++j) {
            path.ipa += values[j]; path.score += static_cast<double>(j); path.graphemes.push_back(gs[i]); path.segments.push_back(values[j]); next.push_back(std::move(path));
        }
        std::sort(next.begin(), next.end(), [](const auto& a, const auto& b) { return a.score < b.score; });
        if (next.size() > width) next.resize(width);
        paths = std::move(next);
    }
    if (paths.empty()) paths.push_back({"", 0.0, {}, {}});
    if (!spec_.allophone_rules.empty()) {
        for (auto& path : paths) {
            for (int pass = 0; pass < spec_.allophone_passes; ++pass)
                path.ipa = apply_allophony(spec_, path);
        }
        std::sort(paths.begin(), paths.end(), [](const auto& a, const auto& b) { return a.score < b.score; });
    }
    return paths;
}

std::string resolve(const std::string& code) {
    static const std::map<std::string, std::string> aliases{{"por", "pt-PT"}, {"eng", "en-GB"}, {"spa", "es-ES"}, {"fra", "fr-FR"}, {"deu", "de-DE"}, {"ita", "it-IT"}, {"de", "de-DE"}, {"en", "en-GB"}, {"es", "es-ES"}, {"fr", "fr-FR"}, {"it", "it-IT"}, {"pt", "pt-PT"}, {"ron", "ro-RO"}, {"rus", "ru"}, {"ara", "ar"}, {"fas", "fa"}, {"zho", "zh"}, {"jpn", "ja"}, {"kor", "ko"}, {"cat", "ca"}, {"glg", "gl"}, {"eus", "eu"}, {"tur", "tr"}, {"nld", "nl"}, {"pol", "pl"}, {"ces", "cs"}, {"ell", "el"}};
    std::string normalized = code; std::replace(normalized.begin(), normalized.end(), '_', '-');
    if (auto it = aliases.find(lower_ascii(normalized)); it != aliases.end()) return it->second;
    std::stringstream parts(normalized); std::string part; std::vector<std::string> subtags;
    while (std::getline(parts, part, '-')) if (!part.empty()) subtags.push_back(part);
    if (!subtags.empty()) { subtags[0] = lower_ascii(subtags[0]); for (std::size_t i = 1; i < subtags.size(); ++i) subtags[i] = (subtags[i].size() == 2 || (subtags[i].size() == 3 && std::all_of(subtags[i].begin(), subtags[i].end(), ::isdigit))) ? [&] { std::string x = subtags[i]; for (char& c : x) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c))); return x; }() : lower_ascii(subtags[i]); }
    normalized.clear(); for (std::size_t i = 0; i < subtags.size(); ++i) { if (i) normalized += "-"; normalized += subtags[i]; }
    const auto codes = available_codes(true);
    for (const auto& c : codes) if (lower_ascii(c) == lower_ascii(normalized)) return c;
    static const std::map<std::string, std::string> defaults{{"de", "de-DE"}, {"en", "en-GB"}, {"es", "es-ES"}, {"fr", "fr-FR"}, {"it", "it-IT"}, {"pt", "pt-PT"}, {"ro", "ro-RO"}};
    if (auto it = defaults.find(normalized); it != defaults.end()) return it->second;
    std::string language = subtags.empty() ? normalized : subtags.front();
    for (const auto& c : codes) if (lower_ascii(c).rfind(language + "-", 0) == 0 || lower_ascii(c) == language) return c;
    return normalized;
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
std::optional<std::string> lexicon_source(const std::string& code) {
    const auto canonical = resolve(code);
    if (auto it = registered_lexicons.find(canonical); it != registered_lexicons.end()) return it->second;
    const char* env = std::getenv("ORTHOGRAPHY2IPA_LEXICON_DIR");
    const std::string dir = lexicon_directory ? *lexicon_directory : (env ? env : "");
    if (!dir.empty() && fs::is_regular_file(fs::path(dir) / (canonical + ".tsv")))
        return (fs::path(dir) / (canonical + ".tsv")).string();
    return std::nullopt;
}
std::optional<std::string> lexicon_path(const std::string& code) {
    const auto source = lexicon_source(code); if (!source) return std::nullopt;
    fs::path path = *source;
    if (source->rfind("http://", 0) == 0 || source->rfind("https://", 0) == 0 || source->rfind("hf://", 0) == 0)
        path = resolve_remote_lexicon(*source);
    return path.string();
}
std::vector<std::pair<std::size_t, std::string>> validate_lexicon(const std::string& text) {
    std::vector<std::pair<std::size_t, std::string>> errors; std::set<std::string> seen; std::stringstream stream(text); std::string line; std::size_t n = 0;
    while (std::getline(stream, line)) {
        ++n; if (line.empty()) continue; const auto tab = line.find('\t');
        if (tab == std::string::npos || line.find('\t', tab + 1) != std::string::npos) { errors.emplace_back(n, "expected word<TAB>ipa"); continue; }
        const auto word = line.substr(0, tab); const auto ipa = line.substr(tab + 1);
        if (word.empty() || ipa.empty()) { errors.emplace_back(n, "empty word or IPA"); continue; }
        if (unicode_nfc(word, false) != word) errors.emplace_back(n, "word not NFC-normalized");
        if (unicode_fold_nfc(word) != word) errors.emplace_back(n, "word not lowercase");
        if (!valid_ipa_string(ipa)) errors.emplace_back(n, "IPA not NFC / not IPA-only");
        if (!seen.insert(word).second) errors.emplace_back(n, "duplicate word");
    }
    return errors;
}
void register_normalize_plugin(const std::string& name, std::shared_ptr<NormalizePlugin> plugin) { if (name.empty() || !plugin) throw std::invalid_argument("normalize plugin name and instance are required"); normalize_plugins[name] = std::move(plugin); }
void register_syllabifier_plugin(const std::string& name, std::shared_ptr<SyllabifierPlugin> plugin) { if (name.empty() || !plugin) throw std::invalid_argument("syllabify plugin name and instance are required"); syllabifier_plugins[name] = std::move(plugin); }
void register_stress_plugin(const std::string& name, std::shared_ptr<StressPlugin> plugin) { if (name.empty() || !plugin) throw std::invalid_argument("stress plugin name and instance are required"); stress_plugins[name] = std::move(plugin); }
void register_rescorer_plugin(const std::string& name, std::shared_ptr<RescorerPlugin> plugin) { if (name.empty() || !plugin) throw std::invalid_argument("rescore plugin name and instance are required"); rescorer_plugins[name] = std::move(plugin); }
void register_sandhi_plugin(const std::string& name, std::shared_ptr<SandhiPlugin> plugin) { if (name.empty() || !plugin) throw std::invalid_argument("sandhi plugin name and instance are required"); sandhi_plugins[name] = std::move(plugin); }
void discover_plugins(const std::string& directory) {
    const char* env = std::getenv("ORTHOGRAPHY2IPA_PLUGIN_DIR");
    const fs::path dir = directory.empty() ? fs::path(env ? env : "") : fs::path(directory);
    if (dir.empty() || !fs::is_directory(dir)) return;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.path().extension() != ".so") continue;
        void* handle = dlopen(entry.path().c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!handle) throw std::runtime_error("unable to load plugin " + entry.path().string() + ": " + dlerror());
        using Register = void (*)();
        auto register_function = reinterpret_cast<Register>(dlsym(handle, "orthography2ipa_register_plugins"));
        if (!register_function) { dlclose(handle); throw std::runtime_error("plugin lacks orthography2ipa_register_plugins: " + entry.path().string()); }
        register_function(); plugin_handles.push_back(handle);
    }
}
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
    std::map<std::string, std::vector<std::string>> result;
    std::map<std::string, ptree> raw_specs;
    if (!fs::is_directory(data_dir)) return result;
    for (const auto& entry : fs::directory_iterator(data_dir)) if (entry.path().extension() == ".json") {
        std::ifstream input(entry.path()); if (!input) continue; ptree raw; boost::property_tree::read_json(input, raw); raw_specs[entry.path().stem().string()] = std::move(raw);
    }
    for (const auto& [code, raw] : raw_specs) {
        if (raw.get<bool>("clade", false)) continue;
        std::string family = raw.get<std::string>("family", "");
        if (family.empty()) { std::vector<std::string> path; std::set<std::string> seen{code}; std::string parent = raw.get<std::string>("parent", "");
            while (!parent.empty() && !seen.count(parent)) { seen.insert(parent); auto it = raw_specs.find(parent); if (it == raw_specs.end()) break; if (it->second.get<bool>("clade", false)) path.push_back(it->second.get<std::string>("name", parent)); parent = it->second.get<std::string>("parent", ""); }
            std::reverse(path.begin(), path.end()); for (std::size_t i = 0; i < path.size(); ++i) { if (i) family += " > "; family += path[i]; }
        }
        result[family].push_back(code);
    }
    for (auto& [_, codes] : result) std::sort(codes.begin(), codes.end());
    return result;
}
std::vector<std::string> validate(const std::string& code) {
    std::vector<std::string> errors; const auto canonical = resolve(code);
    try {
        const auto& spec = get(canonical);
        if (spec.code.empty()) errors.push_back("missing code");
        if (spec.name.empty()) errors.push_back("missing name");
        if (spec.script_type != "alphabet" && spec.script_type != "abjad" && spec.script_type != "abugida" &&
            spec.script_type != "syllabary" && spec.script_type != "logographic" && spec.script_type != "featural" &&
            spec.script_type != "mixed" && spec.script_type != "reconstruction") errors.push_back("invalid script_type: " + spec.script_type);
        std::set<std::string> ids; for (const auto& rule : spec.allophone_rules) if (!rule.id.empty() && !ids.insert(rule.id).second) errors.push_back("duplicate allophone rule id: " + rule.id);
        ids.clear(); for (const auto& rule : spec.sandhi_rules) if (!rule.id.empty() && !ids.insert(rule.id).second) errors.push_back("duplicate sandhi rule id: " + rule.id);
        if (!spec.parent.empty()) { try { get(spec.parent); } catch (const std::exception&) { errors.push_back("missing parent: " + spec.parent); } }
    } catch (const std::exception& e) { errors.push_back(e.what()); }
    return errors;
}
std::map<std::string, std::vector<PluginAnswer>> who_answers(const std::string& code) {
    const auto language = resolve(code);
    std::map<std::string, std::vector<PluginAnswer>> result;
    auto add = [&](const auto& plugins, const std::string& stage, auto priority) {
        for (const auto& [name, plugin] : plugins)
            if (owns_language(*plugin, language)) result[stage].push_back({name, priority(*plugin), false});
        auto& answers = result[stage];
        std::sort(answers.begin(), answers.end(), [](const auto& a, const auto& b) {
            if (a.priority != b.priority) return a.priority > b.priority;
            return a.name < b.name;
        });
        if (answers.empty()) answers.push_back({"built-in", 0, true});
        else answers.front().selected = true;
    };
    add(normalize_plugins, "normalize", [](const auto&) { return 50; });
    add(syllabifier_plugins, "syllabify", [](const auto& plugin) { return plugin.priority(); });
    add(stress_plugins, "stress", [](const auto& plugin) { return plugin.priority(); });
    add(rescorer_plugins, "rescore", [](const auto& plugin) { return plugin.priority(); });
    add(sandhi_plugins, "sandhi", [](const auto&) { return 50; });
    return result;
}
std::string transcribe(const std::string& text, const std::string& language) { return G2P(language).transcribe(text); }
std::string apply_dialect_transform(const std::string& ipa, const std::string& profile,
                                    const std::string& orthography) {
    if (!dialect_profile_known(profile)) throw std::invalid_argument("unknown dialect profile: " + profile);
    return apply_dialect_impl(ipa, profile, orthography);
}
std::vector<std::string> available_dialect_profiles() {
    return {"algarve_barlavento", "baixo_minhoto", "beira_alta", "beira_baixa", "estremenho",
            "galician", "galician_west", "guadramilese", "leonese", "lisbon", "northern",
            "porto", "ribatejano", "rionorese", "transmontano"};
}

G2P::G2P(std::string language, std::map<std::string, std::vector<std::string>> plugin_overrides,
         std::string dialect_profile)
    : language_(resolve(language)), spec_(&get(language_)), plugin_overrides_(std::move(plugin_overrides)),
      dialect_profile_(std::move(dialect_profile)) {
    static std::once_flag discovery_once;
    std::call_once(discovery_once, [] { discover_plugins(); });
    if (!dialect_profile_.empty() && !dialect_profile_known(dialect_profile_))
        throw std::invalid_argument("unknown dialect profile: " + dialect_profile_);
    for (const auto& [stage, names] : plugin_overrides_) {
        if (stage != "normalize" && stage != "syllabify" && stage != "stress" &&
            stage != "rescore" && stage != "sandhi")
            throw std::invalid_argument("unknown plugin stage: " + stage);
        if (std::any_of(names.begin(), names.end(), [](const auto& name) { return name.empty(); }))
            throw std::invalid_argument("plugin names cannot be empty: " + stage);
    }
}
const LanguageSpec& G2P::spec() const { return *spec_; }
const std::map<std::string, std::vector<std::string>>& G2P::plugin_overrides() const { return plugin_overrides_; }
std::vector<IPAPath> G2P::candidates(const std::string& word, std::size_t width) const { return Tokenizer(*spec_).beam(word, width); }
std::vector<IPAPath> G2P::lattice(const std::string& word, std::size_t width) const { return candidates(word, width); }
std::vector<GraphemeFeature> G2P::features(const std::string& word) const {
    std::vector<std::string> unmapped; const auto graphemes = Tokenizer(*spec_).tokenize_word(word, &unmapped); std::vector<GraphemeFeature> result;
    for (std::size_t i = 0; i < graphemes.size(); ++i) {
        const auto& g = graphemes[i]; const auto it = spec_->graphemes.find(g); const bool vowel = vowel_grapheme(g);
        GraphemeFeature feature; feature.grapheme = g; feature.position = i == 0 ? "initial" : (i + 1 == graphemes.size() ? "final" : "medial");
        feature.previous = i ? graphemes[i - 1] : ""; feature.next = i + 1 < graphemes.size() ? graphemes[i + 1] : ""; feature.vowel = vowel; feature.consonant = !vowel;
        if (it != spec_->graphemes.end()) feature.candidates = it->second;
        result.push_back(std::move(feature));
    }
    return result;
}
double G2P::word_confidence(const std::string& word, std::size_t width) const {
    if (spec_->word_exceptions.count(lower_ascii(word)) || load_lexicon(language_).count(unicode_fold_nfc(word))) return 1.0;
    auto paths = candidates(word, width); return paths.size() > 1 ? 1.0 / (1.0 + paths[1].score) : 1.0;
}
TranscriptionResult G2P::transcribe_detailed(const std::string& text, const std::string& search, std::size_t width) const {
    if (search != "greedy" && search != "beam") throw std::invalid_argument("search must be greedy or beam");
    auto stage = [&](const std::string& name) -> const std::vector<std::string>& { auto it = plugin_overrides_.find(name); return it == plugin_overrides_.end() ? plugin_names(*spec_, name) : it->second; };
    auto validate_stage = [&](const std::string& name, const auto& plugins) {
        for (const auto& plugin_name : stage(name)) {
            auto plugin = plugins.find(plugin_name);
            if (plugin == plugins.end())
                throw std::runtime_error("missing " + name + " plugin: " + plugin_name);
            if (!owns_language(*plugin->second, language_))
                throw std::runtime_error(name + " plugin does not own language " + language_ + ": " + plugin_name);
        }
    };
    validate_stage("normalize", normalize_plugins);
    validate_stage("syllabify", syllabifier_plugins);
    validate_stage("stress", stress_plugins);
    validate_stage("rescore", rescorer_plugins);
    validate_stage("sandhi", sandhi_plugins);
    TranscriptionResult result; result.lang = language_; std::vector<std::string> surfaces, ipa_words; std::vector<bool> pausal;
    std::vector<IPAPath> stress_paths; std::vector<std::optional<std::size_t>> forced_stress;
    auto normalize = [&](std::string value) {
        for (const auto& name : stage("normalize")) {
            auto it = normalize_plugins.find(name);
            if (it == normalize_plugins.end()) throw std::runtime_error("missing normalize plugin: " + name);
            if (!owns_language(*it->second, language_)) throw std::runtime_error("normalize plugin does not own language " + language_ + ": " + name);
            const auto normalized = it->second->normalize(value, language_);
            if (it->second->normalize(value, language_) != normalized)
                throw std::runtime_error("normalize plugin returned non-deterministic output: " + name);
            value = normalized;
        }
        return value;
    };
    const auto input_words = parse_input(text, normalize);
    for (const auto& input : input_words) {
        const auto& word = input.surface;
        WordTranscription wt; wt.word = word; auto it = spec_->word_exceptions.find(lower_ascii(word));
        if (input.forced) {
            if (input.forced_ipa.empty()) throw std::invalid_argument("empty forced pronunciation");
            auto atoms = inventory(*spec_); const std::vector<std::string> atom_list(atoms.begin(), atoms.end()); const auto segments = segment_ipa(input.forced_ipa, atom_list);
            for (const auto& segment : segments) if (!atoms.count(segment) && segment != "ˈ" && segment != "ˌ") throw std::invalid_argument("forced pronunciation contains undeclared IPA: " + segment);
        }
        auto paths = input.forced ? std::vector<IPAPath>{{input.forced_ipa, 0.0, {}, {}}} :
            (it != spec_->word_exceptions.end() ? std::vector<IPAPath>{{it->second, 0.0, {}, {}}} : candidates(word, search == "greedy" ? 1 : width));
        auto lex = it == spec_->word_exceptions.end() ? load_lexicon(language_) : std::map<std::string, std::string>{};
        if (it == spec_->word_exceptions.end()) { auto li = lex.find(unicode_fold_nfc(word)); if (li != lex.end()) paths = {{li->second, 0.0, {}, {}}}; }
        if (!input.forced && it == spec_->word_exceptions.end() && lex.find(unicode_fold_nfc(word)) == lex.end())
            paths = apply_grammatical_endings(*spec_, std::move(paths));
        auto rescorers = stage("rescore"); std::vector<std::string> ordered_rescorers(rescorers.begin(), rescorers.end());
        std::sort(ordered_rescorers.begin(), ordered_rescorers.end(), [&](const auto& a, const auto& b) {
            const auto x = rescorer_plugins.find(a), y = rescorer_plugins.find(b);
            return x != rescorer_plugins.end() && y != rescorer_plugins.end() && x->second->priority() > y->second->priority();
        });
        for (const auto& name : ordered_rescorers) {
            auto p = rescorer_plugins.find(name);
            if (p == rescorer_plugins.end()) throw std::runtime_error("missing rescore plugin: " + name);
            if (!owns_language(*p->second, language_)) throw std::runtime_error("rescore plugin does not own language " + language_ + ": " + name);
            const auto rescored = p->second->rescore(word, language_, paths);
            if (!same_paths(rescored, p->second->rescore(word, language_, paths)))
                throw std::runtime_error("rescore plugin returned non-deterministic output: " + name);
            validate_rescorer_output(rescored, *spec_, name);
            paths = rescored;
        }
        wt.ipa = paths.front().ipa;
        std::optional<std::size_t> plugin_stress;
        auto stress_names = stage("stress"); std::vector<std::string> ordered_stress(stress_names.begin(), stress_names.end());
        std::sort(ordered_stress.begin(), ordered_stress.end(), [&](const auto& a, const auto& b) {
            const auto x = stress_plugins.find(a), y = stress_plugins.find(b);
            return x != stress_plugins.end() && y != stress_plugins.end() && x->second->priority() > y->second->priority();
        });
        if (!ordered_stress.empty()) {
            auto syllables = syllables_for(word, language_, stage("syllabify"));
            auto plugin = stress_plugins.find(ordered_stress.front());
            if (plugin == stress_plugins.end()) throw std::runtime_error("missing stress plugin: " + ordered_stress.front());
            if (!owns_language(*plugin->second, language_)) throw std::runtime_error("stress plugin does not own language " + language_ + ": " + ordered_stress.front());
            plugin_stress = plugin->second->stressed_index(word, syllables, language_);
            if (plugin->second->stressed_index(word, syllables, language_) != plugin_stress)
                throw std::runtime_error("stress plugin returned non-deterministic output: " + ordered_stress.front());
            if (plugin_stress && *plugin_stress >= syllables.size())
                throw std::runtime_error("stress plugin returned an out-of-range syllable: " + ordered_stress.front());
        }
        stress_paths.push_back({wt.ipa, paths.front().score, paths.front().graphemes, paths.front().segments});
        forced_stress.push_back(plugin_stress);
        wt.candidates = paths; wt.confidence = word_confidence(word, width);
        result.words.push_back(wt); surfaces.push_back(word); ipa_words.push_back(wt.ipa); pausal.push_back(input.pausal);
    }
    ipa_words = apply_sandhi(*spec_, std::move(ipa_words), pausal);
    for (const auto& name : stage("sandhi")) {
        auto p = sandhi_plugins.find(name);
        if (p == sandhi_plugins.end()) throw std::runtime_error("missing sandhi plugin: " + name);
        if (!owns_language(*p->second, language_)) throw std::runtime_error("sandhi plugin does not own language " + language_ + ": " + name);
        const auto transformed = p->second->apply(ipa_words, surfaces, pausal, language_);
        if (transformed.size() != ipa_words.size()) throw std::runtime_error("sandhi plugin changed word count: " + name);
        if (p->second->apply(ipa_words, surfaces, pausal, language_) != transformed)
            throw std::runtime_error("sandhi plugin returned non-deterministic output: " + name);
        ipa_words = transformed;
    }
    for (std::size_t i = 0; i < ipa_words.size(); ++i) {
        if (!stress_paths[i].graphemes.empty())
            ipa_words[i] = add_stress(*spec_, surfaces[i], IPAPath{ipa_words[i], stress_paths[i].score, stress_paths[i].graphemes, stress_paths[i].segments}, forced_stress[i]);
        if (i) result.ipa += " ";
        result.ipa += ipa_words[i];
        result.words[i].ipa = ipa_words[i];
    }
    if (!dialect_profile_.empty()) {
        std::string orthography; for (std::size_t i = 0; i < surfaces.size(); ++i) { if (i) orthography += " "; orthography += surfaces[i]; }
        result.ipa = apply_dialect_impl(result.ipa, dialect_profile_, orthography);
        std::stringstream split(result.ipa); for (auto& word : result.words) split >> word.ipa;
    }
    return result;
}
std::string G2P::transcribe(const std::string& text, const std::string& search, std::size_t width) const { return transcribe_detailed(text, search, width).ipa; }

std::vector<std::string> feature_names() {
    return {"syllabic", "sonorant", "consonantal", "continuant", "delayed_release", "lateral", "nasal", "strident", "voice", "spread_glottis", "constricted_glottis", "anterior", "coronal", "distributed", "labial", "high", "low", "back", "round", "tense", "long", "click", "nasal_vowel"};
}
std::vector<double> feature_vector(const std::string& segment) {
    std::vector<double> v(23, .5); if (segment.empty() || segment == "∅") return v;
    std::size_t n = utf8_char_size(segment, 0); const std::string base = segment.substr(0, n);
    const std::string vowels = "iɪeɛæaɑɒɔoʊuʉyʏøœɨɯɐəɵ";
    const std::string high = "iɪɨʉuʊyʏɯ"; const std::string low = "aɑɒæɐ";
    const std::string back = "ɯuʊoɔɑɒɒɤɣqχ"; const std::string round = "uʊoɔyʏøœɵʉ";
    const std::string nasals = "mɱnɲŋɳɴ"; const std::string voiced = "bdɡɢɟɖɗʒʐzvðɣʁɲɳmnŋɲɾrl";
    const std::string labial = "mpbvfβɸɱ"; const std::string coronal = "tdszʃʒnlɾɹɻɲɳʈɖθð";
    const std::string strident = "sʃʒzʂʐɕʑ";
    const bool vowel = vowels.find(base) != std::string::npos;
    const bool known = vowel || std::string("pbtdkɡcqʔfvszʃʒʂʐxɣχħʕmɱnɲŋɳɴlrɾɹj wɥɰ").find(base) != std::string::npos;
    if (!known) return v;
    v[0] = vowel ? 1 : 0; v[1] = vowel || nasals.find(base) != std::string::npos || std::string("lɾɹrwj").find(base) != std::string::npos ? 1 : 0; v[2] = vowel ? 0 : 1;
    v[3] = vowel || std::string("fvszʃʒʂʐxɣχħʕθðj wɥɰ").find(base) != std::string::npos ? 1 : 0;
    v[4] = (segment.find("tʃ") == 0 || segment.find("dʒ") == 0 || segment.find("ɕ") == 0) ? 1 : 0;
    v[5] = std::string("lɬɮʎ").find(base) != std::string::npos; v[6] = nasals.find(base) != std::string::npos;
    v[7] = strident.find(base) != std::string::npos; v[8] = voiced.find(base) != std::string::npos;
    v[9] = segment.find("ʰ") != std::string::npos; v[10] = segment.find("ʼ") != std::string::npos || segment.find("ˤ") != std::string::npos;
    v[11] = coronal.find(base) != std::string::npos; v[12] = coronal.find(base) != std::string::npos; v[13] = std::string("ʃʒʂʐɕʑθð").find(base) != std::string::npos;
    v[14] = labial.find(base) != std::string::npos; v[15] = high.find(base) != std::string::npos || std::string("cjɟɲʎj").find(base) != std::string::npos;
    v[16] = low.find(base) != std::string::npos; v[17] = back.find(base) != std::string::npos; v[18] = round.find(base) != std::string::npos; v[19] = vowel ? 1 : .5;
    v[20] = segment.find("ː") != std::string::npos; v[21] = std::string("ǀǁǂǃ").find(base) != std::string::npos; v[22] = segment.find("̃") != std::string::npos && vowel;
    return v;
}
double segment_distance(const std::string& a, const std::string& b) {
    if (a == b) return 0;
    if (a.empty() || b.empty() || a == "∅" || b == "∅") return 1;
    const auto x = feature_vector(a), y = feature_vector(b); bool unknown_x = true, unknown_y = true;
    for (double value : x) unknown_x = unknown_x && value == .5;
    for (double value : y) unknown_y = unknown_y && value == .5;
    if (unknown_x || unknown_y) return 1;
    if (x[0] != y[0]) return 1;
    double total = 0, weight = 0; for (std::size_t i = 0; i < x.size(); ++i) { const double w = i < 3 ? 7.0 : 1.0; total += w * std::abs(x[i] - y[i]); weight += w; }
    return std::min(1.0, total / weight);
}
InventoryDistance inventory_distance(const LanguageSpec& a, const LanguageSpec& b) {
    auto x = inventory(a), y = inventory(b); std::size_t shared = 0; for (const auto& p : x) shared += y.count(p);
    std::set<std::string> union_set = x; union_set.insert(y.begin(), y.end());
    double j = union_set.empty() ? 0 : 1.0 - static_cast<double>(shared) / union_set.size();
    auto mean_min = [](const auto& source, const auto& target) { if (source.empty() || target.empty()) return 1.0; double sum = 0; for (const auto& p : source) { double best = 1; for (const auto& q : target) best = std::min(best, segment_distance(p, q)); sum += best; } return sum / source.size(); };
    return {j, (mean_min(x, y) + mean_min(y, x)) / 2, x.size(), y.size(), shared};
}
GraphemeDivergence grapheme_divergence(const LanguageSpec& a, const LanguageSpec& b) {
    std::set<std::string> x, y; for (const auto& [k, _] : a.graphemes) x.insert(lower_ascii(k)); for (const auto& [k, _] : b.graphemes) y.insert(lower_ascii(k));
    std::size_t shared = 0; double total = 0; for (const auto& k : x) if (y.count(k)) { ++shared; auto find = [](const auto& m, const auto& key) -> const std::vector<std::string>& { for (const auto& [k, v] : m) if (lower_ascii(k) == key) return v; static const std::vector<std::string> empty; return empty; }; const auto& va = find(a.graphemes, k); const auto& vb = find(b.graphemes, k); if (va.empty() || vb.empty()) total += 1; else { double d = 0; for (const auto& p : va) { double best = 1; for (const auto& q : vb) best = std::min(best, segment_distance(p, q)); d += best; } for (const auto& q : vb) { double best = 1; for (const auto& p : va) best = std::min(best, segment_distance(q, p)); d += best; } total += d / (va.size() + vb.size()); } }
    std::set<std::string> u = x; u.insert(y.begin(), y.end()); return {shared, u.size(), shared ? total / shared : 1, u.empty() ? 0 : static_cast<double>(shared) / u.size()};
}
double allophone_overlap(const LanguageSpec& a, const LanguageSpec& b) { std::set<std::string> x, y; for (const auto& [_, v] : a.allophones) x.insert(v.begin(), v.end()); for (const auto& [_, v] : b.allophones) y.insert(v.begin(), v.end()); std::set<std::string> u = x; u.insert(y.begin(), y.end()); std::size_t n = 0; for (const auto& p : x) n += y.count(p); return u.empty() ? 1 : static_cast<double>(n) / u.size(); }
PhonologicalDistance phonological_distance(const LanguageSpec& a, const LanguageSpec& b) { auto i = inventory_distance(a, b); auto g = grapheme_divergence(a, b); auto o = allophone_overlap(a, b); return {i, g, o, .6 * i.feature_mean + .4 * (1 - o)}; }
std::map<std::string, double> ancestry_weights(const LanguageSpec& spec, int depth = 10, bool temporal_decay = false, double decay_halflife = 1000.0) {
    std::map<std::string, double> result; std::vector<std::tuple<std::string, double, int>> queue;
    if (!spec.ancestors.empty()) for (const auto& link : spec.ancestors) queue.emplace_back(link.code, link.weight, 1);
    else if (!spec.parent.empty()) queue.emplace_back(spec.parent, 1.0, 1);
    while (!queue.empty()) { auto [code, weight, level] = queue.back(); queue.pop_back(); if (level > depth || code.empty()) continue; try { const auto& current = get(code); if (current.clade && !current.parent.empty()) queue.emplace_back(current.parent, weight, level + 1); else { auto it = result.find(code); if (it == result.end() || weight > it->second) result[code] = weight; auto push = [&](const std::string& target, double edge) { double decay = 1; if (temporal_decay) try { const auto& ancestor = get(target); if (spec.timespan && ancestor.timespan) { const int end = ancestor.timespan->end_year.value_or(ancestor.timespan->start_year); const int gap = spec.timespan->start_year - end; if (gap > 0) decay = std::exp(-static_cast<double>(gap) / decay_halflife); } } catch (...) {} queue.emplace_back(target, weight * edge * decay, level + 1); }; for (const auto& link : current.ancestors) push(link.code, link.weight); if (current.ancestors.empty() && !current.parent.empty()) push(current.parent, 1.0); } } catch (...) {} }
    return result;
}
double ancestry_similarity(const LanguageSpec& a, const LanguageSpec& b, int max_depth, bool temporal_decay, double decay_halflife) {
    if (a.code == b.code) return 1;
    const auto x = ancestry_weights(a, max_depth, temporal_decay, decay_halflife), y = ancestry_weights(b, max_depth, temporal_decay, decay_halflife); double best = 0;
    if (auto it = x.find(b.code); it != x.end()) best = std::max(best, it->second);
    if (auto it = y.find(a.code); it != y.end()) best = std::max(best, it->second);
    for (const auto& [code, weight] : x) if (auto it = y.find(code); it != y.end()) best = std::max(best, weight * it->second);
    if (best == 0 && !a.family.empty() && a.family == b.family) best = .4;
    return std::min(1.0, best);
}
SpellingDivergence spelling_divergence(const LanguageSpec& a, const LanguageSpec& b) {
    std::map<std::string, std::set<std::string>> x, y;
    for (const auto& [g, values] : a.graphemes) for (const auto& p : values) if (!p.empty()) x[p].insert(lower_ascii(g));
    for (const auto& [g, values] : b.graphemes) for (const auto& p : values) if (!p.empty()) y[p].insert(lower_ascii(g));
    std::set<std::string> shared, all; for (const auto& [p, _] : x) all.insert(p); for (const auto& [p, _] : y) { all.insert(p); if (x.count(p)) shared.insert(p); }
    if (shared.empty()) return {0, all.size(), 0, 0, 1};
    double total = 0; std::size_t identical = 0, disjoint = 0;
    for (const auto& p : shared) { std::set<std::string> u = x[p]; u.insert(y[p].begin(), y[p].end()); std::set<std::string> overlap; for (const auto& g : x[p]) if (y[p].count(g)) overlap.insert(g); const double d = 1.0 - static_cast<double>(overlap.size()) / u.size(); total += d; if (d == 0) ++identical; if (overlap.empty()) ++disjoint; }
    return {shared.size(), all.size(), identical, disjoint, total / shared.size()};
}
std::optional<double> temporal_distance(const LanguageSpec& a, const LanguageSpec& b, int reference_year) {
    if (!a.timespan || !b.timespan) return std::nullopt;
    const int ea = a.timespan->end_year.value_or(reference_year), eb = b.timespan->end_year.value_or(reference_year);
    const int overlap = std::max(0, std::min(ea, eb) - std::max(a.timespan->start_year, b.timespan->start_year)); const int span = std::max(ea, eb) - std::min(a.timespan->start_year, b.timespan->start_year); return span <= 0 ? 0 : 1.0 - static_cast<double>(overlap) / span;
}
double positional_divergence(const LanguageSpec& a, const LanguageSpec& b) {
    std::set<std::string> keys; for (const auto& [g, _] : a.positional_graphemes) keys.insert(lower_ascii(g)); for (const auto& [g, _] : b.positional_graphemes) keys.insert(lower_ascii(g)); if (keys.empty()) return 0;
    double total = 0; for (const auto& key : keys) { auto lookup = [&](const auto& map) { for (const auto& [g, positions] : map) if (lower_ascii(g) == key) return positions; return std::map<std::string, std::vector<std::string>>{}; }; const auto x = lookup(a.positional_graphemes), y = lookup(b.positional_graphemes); std::set<std::string> positions; for (const auto& [p, _] : x) positions.insert(p); for (const auto& [p, _] : y) positions.insert(p); if (x.empty() || y.empty()) total += 1; else for (const auto& p : positions) { auto ix = x.find(p), iy = y.find(p); if (ix == x.end() || iy == y.end() || ix->second.empty() || iy->second.empty()) total += 1.0 / positions.size(); else total += segment_distance(ix->second.front(), iy->second.front()) / positions.size(); } }
    return std::min(1.0, total / keys.size());
}
double phoneme_coverage(const LanguageSpec& native, const LanguageSpec& target) { const auto x = inventory(native), y = inventory(target); if (y.empty()) return 1; std::size_t shared = 0; for (const auto& p : y) shared += x.count(p); return static_cast<double>(shared) / y.size(); }
double orthographic_distance(const LanguageSpec& a, const LanguageSpec& b) { const auto g = grapheme_divergence(a, b); if (a.script == b.script) return g.mean_ipa_distance; const double script = a.script_type == b.script_type ? .25 : .5; return .6 * script + .4 * g.mean_ipa_distance; }
double full_distance(const LanguageSpec& a, const LanguageSpec& b, double w_phonological, double w_ancestry) { const auto p = phonological_distance(a, b); return w_phonological * p.combined + w_ancestry * (1.0 - ancestry_similarity(a, b)); }
WeightedDistance weighted_full_distance(const LanguageSpec& a, const LanguageSpec& b, double wi, double wg, double wa, double wy, double wt, int year) {
    const auto i = inventory_distance(a, b); const auto g = grapheme_divergence(a, b); const double o = allophone_overlap(a, b), an = ancestry_similarity(a, b); const auto temporal = temporal_distance(a, b, year); const double effective = temporal ? wt : 0; const double total = wi + wg + wa + wy + effective;
    const double combined = total == 0 ? 0 : (wi * i.feature_mean + wg * g.mean_ipa_distance + wa * (1 - o) + wy * (1 - an) + effective * temporal.value_or(0)) / total;
    return {i.feature_mean, g.mean_ipa_distance, o, an, temporal.value_or(std::numeric_limits<double>::quiet_NaN()), combined, {wi, wg, wa, wy, wt}};
}
std::vector<std::vector<double>> pairwise_distances(const std::vector<LanguageSpec>& specs, const std::string& metric) {
    std::vector<std::vector<double>> matrix(specs.size(), std::vector<double>(specs.size(), 0)); for (std::size_t i = 0; i < specs.size(); ++i) for (std::size_t j = i + 1; j < specs.size(); ++j) { double value; const auto p = phonological_distance(specs[i], specs[j]); if (metric == "inventory") value = p.inventory.feature_mean; else if (metric == "grapheme") value = p.grapheme.mean_ipa_distance; else if (metric == "allophone") value = 1 - p.allophone_sim; else if (metric == "ancestry") value = 1 - ancestry_similarity(specs[i], specs[j]); else value = p.combined; matrix[i][j] = matrix[j][i] = value; } return matrix;
}
double geographic_distance(const LanguageSpec& a, const LanguageSpec& b, bool normalize) { if (!a.latitude || !a.longitude || !b.latitude || !b.longitude) return std::numeric_limits<double>::quiet_NaN(); constexpr double R = 6371.0088; auto rad=[](double d){return d*3.141592653589793/180;}; double p1=rad(*a.latitude), p2=rad(*b.latitude), dp=rad(*b.latitude-*a.latitude), dl=rad(*b.longitude-*a.longitude); double h=std::sin(dp/2)*std::sin(dp/2)+std::cos(p1)*std::cos(p2)*std::sin(dl/2)*std::sin(dl/2); double km=2*R*std::asin(std::sqrt(h)); return normalize ? km/(3.141592653589793*R) : km; }
} // namespace orthography2ipa
