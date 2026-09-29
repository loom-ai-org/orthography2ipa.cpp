#include "beam.hpp"
#include "orthography2ipa/orthography2ipa.hpp"
#include "orthography2ipa/tone.hpp"
#include "orthography2ipa/stress.hpp"
#include "unicode_util.hpp"
#include "orthography2ipa/vowels.hpp"

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
using uni::to_utf32;
using uni::to_utf8;

std::string silent_marks_of(const LanguageSpec& spec) {
    std::string marks;
    for (const auto& mark : spec.marked_vowels) {
        if (auto it = spec.graphemes.find(mark); it != spec.graphemes.end() && it->second.empty())
            marks += mark;
    }
    return marks;
}
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
    // A declared-but-empty entry SHADOWS the base's entry for that
    // grapheme (own {} replaces it in the {**base, **own} merge), so the
    // key is created even when no position follows.
    for (const auto& g : *child) {
        auto& entry = result[g.first];
        for (const auto& pos : g.second) entry[pos.first] = strings(pos.second);
    }
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
    s.orthography_kind = raw.get<std::string>("orthography_kind", "native");
    s.script_type = raw.get<std::string>("script_type", "alphabet");
    s.inherent_vowel = raw.get<std::string>("inherent_vowel", "");
    if (auto value = raw.get_optional<std::string>("inherent_vowel_final"))
        s.inherent_vowel_final = *value;
    s.virama_final_vowel = raw.get<std::string>("virama_final_vowel", "");
    s.coda_no_inherent_vowel = raw.get<bool>("coda_no_inherent_vowel", false);
    s.collapse_geminates = raw.get<bool>("collapse_geminates", false);
    s.doubled_letters_geminate = raw.get<bool>("doubled_letters_geminate", true);
    s.constrain_onsets = raw.get<bool>("constrain_onsets", false);
    s.fold_diacritics = optional_list(raw, "fold_diacritics"); s.vowel_graphemes = optional_list(raw, "vowel_graphemes");
    s.dependent_vowels = optional_list(raw, "dependent_vowels"); s.preposed_vowels = optional_list(raw, "preposed_vowels");
    s.trailing_vowel_axis_digraphs = optional_list(raw, "trailing_vowel_axis_digraphs");
    s.parent = raw.get<std::string>("parent", "");
    if (auto ancestors = raw.get_child_optional("ancestors")) for (const auto& item : *ancestors) {
        const auto& a = item.second; s.ancestors.push_back({a.get<std::string>("code", ""), a.get<std::string>("role", "parent"), a.get<std::string>("notes", ""), a.get<double>("weight", 1.0)});
    }
    s.family = raw.get<std::string>("family", "");
    s.quality = raw.get<std::string>("quality", "research");
    s.clade = raw.get<bool>("clade", false);
    s.graphemes = ipa_map(raw, "graphemes");
    // Per-candidate weights from the weighted-object grapheme form
    // ({"ipa": [...], "weights": [...]}); own-only, sparse — an absent
    // weights table means rank cost (weights.py split_weighted_graphemes).
    if (auto graphemes = raw.get_child_optional("graphemes"))
        for (const auto& item : *graphemes)
            if (auto weights = item.second.get_child_optional("weights")) {
                std::vector<double> values;
                for (const auto& w : *weights) values.push_back(w.second.get_value<double>(0.0));
                s.grapheme_weights[item.first] = std::move(values);
            }
    s.allophones = ipa_map(raw, "allophones");
    s.positional_graphemes = positional_map(raw);
    s.phonemes = optional_strings(raw, "phonemes");
    s.marked_vowels = optional_strings(raw.get_child("stress", ptree{}), "marked_vowels");
    if (auto stress = raw.get_child_optional("stress")) {
        s.stress_defined = true;
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
        // A declared cap is the language owner speaking directly; the
        // default 1 is a placeholder (types.py max_onset_declared).
        s.max_onset_declared = stress->get_child_optional("max_onset").has_value();
        s.stress_source = stress->get<std::string>("source", "rules");
        s.secondary_stress = stress->get<std::string>("secondary_stress", "");
        s.accent2_mark = stress->get<std::string>("accent2_mark", "");
        s.accent2_final_letters = optional_list(*stress, "accent2_final_letters"); s.cliticless_words = optional_list(*stress, "cliticless_words");
        s.constrain_mark_onsets = stress->get<bool>("constrain_mark_onsets", true); s.coda_liquid_capture = stress->get<bool>("coda_liquid_capture", false); s.iambic_length = stress->get<bool>("iambic_length", false);
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
    // json_loader.py: `tone_inventory` is a data-model field only; the
    // ENGINE's tone system (computed tones, docking) reads `tone_rules`
    // alone — spec.tone is set exclusively from that key.
    if (auto tone = raw.get_child_optional("tone_inventory"))
        for (const auto& x : *tone) s.tone_inventory[x.first] = x.second.data();
    s.tone_marks_syllable_final = raw.get<bool>("tone_marks_syllable_final", false);
    if (auto rules = raw.get_child_optional("tone_rules")) {
        ToneData value;
        if (auto table = rules->get_child_optional("table")) for (const auto& c : *table) for (const auto& sclass : c.second) for (const auto& mark : sclass.second) value.table[c.first][sclass.first][mark.first] = mark.second.data();
        if (auto classes = rules->get_child_optional("classes")) for (const auto& x : *classes) value.classes[x.first] = x.second.data();
        if (auto marks = rules->get_child_optional("marks")) for (const auto& x : *marks) value.marks[x.first] = x.second.data();
        if (auto tones = rules->get_child_optional("tones")) for (const auto& x : *tones) value.tones[x.first] = x.second.data();
        value.no_mark = rules->get<std::string>("no_mark", "none"); value.notes = rules->get<std::string>("notes", ""); value.dead_codas = optional_list(*rules, "dead_codas"); s.tone = value;
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
                    // property_tree renders a JSON null as the unquoted
                    // literal "null" (boost::property_tree does not track
                    // quoting); an empty string renders as empty data. In
                    // the ending schema null is only legal at rank one
                    // (normalize_ending_value: the deferring shape), so the
                    // two are distinguished by position.
                    const auto data = value.second.data();
                    if (first && (data.empty() || data == "null"))
                        values.push_back(std::nullopt);
                    else values.push_back(data);
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
    const std::string exceptions_base = raw.get<std::string>("word_exceptions_base", "");
    if (!base.empty() && base != code) {
        LanguageSpec b = load_raw(base, loading);
        for (const auto& x : b.graphemes) if (!s.graphemes.count(x.first)) s.graphemes[x.first] = x.second;
        for (const auto& x : b.positional_graphemes)
            if (!s.positional_graphemes.count(x.first)) s.positional_graphemes[x.first] = x.second;
        const std::string positional_base = raw.get<std::string>("positional_graphemes_base", "");
        // Positional entries refine the grapheme they key: inheritance is
        // id-keyed like the grapheme table itself — a child declaring an
        // entry for a grapheme replaces the base's entry WHOLESALE, never
        // position by position (json_loader.py BASE_MERGE dict merge).
        if (!positional_base.empty() && positional_base != code)
            for (const auto& x : load_raw(positional_base, loading).positional_graphemes)
                if (!s.positional_graphemes.count(x.first)) s.positional_graphemes[x.first] = x.second;
        s.allophone_rules = overlay_allophone(b.allophone_rules, s.allophone_rules);
        s.sandhi_rules = overlay_sandhi(b.sandhi_rules, s.sandhi_rules);
        if (!raw.get_child_optional("stress") && s.default_stress_position == -2) {
            // No own stress block: inherit the graphemes-base spec's
            // resolved stress rules WHOLESALE (json_loader.py), so a child
            // that shares its base's orthography also shares its
            // accentuation — every StressRules field, cliticless_words and
            // the accent-2 block included.
            s.default_stress_position = b.default_stress_position; s.stress_mark = b.stress_mark; s.stress_defined = b.stress_defined;
            s.marked_vowels = b.marked_vowels; s.final_stress_endings = b.final_stress_endings;
            s.penult_stress_endings = b.penult_stress_endings; s.antepenult_stress_endings = b.antepenult_stress_endings;
            s.diphthongs = b.diphthongs; s.vowel_letters = b.vowel_letters;
            s.onset_clusters = b.onset_clusters; s.quantity_sensitive = b.quantity_sensitive;
            s.superheavy_final_attracts = b.superheavy_final_attracts; s.max_onset = b.max_onset;
            s.max_onset_declared = b.max_onset_declared; s.stress_source = b.stress_source;
            s.secondary_stress = b.secondary_stress;
            s.cliticless_words = b.cliticless_words;
            s.accent2_mark = b.accent2_mark; s.accent2_final_letters = b.accent2_final_letters;
            s.constrain_mark_onsets = b.constrain_mark_onsets; s.coda_liquid_capture = b.coda_liquid_capture;
            s.iambic_length = b.iambic_length;
        }
        if (s.family.empty()) s.family = b.family;
    }
    const std::string positional_base = raw.get<std::string>("positional_graphemes_base", "");
    if (!positional_base.empty() && positional_base != code)
        for (const auto& x : load_raw(positional_base, loading).positional_graphemes)
            if (!s.positional_graphemes.count(x.first)) s.positional_graphemes[x.first] = x.second;
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
    if (!exceptions_base.empty() && exceptions_base != code) {
        const auto b = load_raw(exceptions_base, loading);
        for (const auto& [word, ipa] : b.word_exceptions) if (!s.word_exceptions.count(word)) s.word_exceptions[word] = ipa;
    }
    if (base.empty() && !s.parent.empty() && s.parent != code) {
        LanguageSpec ancestor = load_raw(s.parent, loading);
        std::set<std::string> seen{code, s.parent};
        while (ancestor.clade && !ancestor.parent.empty() && !seen.count(ancestor.parent)) {
            seen.insert(ancestor.parent); ancestor = load_raw(ancestor.parent, loading);
        }
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
    // Inheritance nulls (JSON null) delete the inherited entry wholesale —
    // types.py __post_init__ pops them after the base merge. property_tree
    // cannot distinguish null from an empty list, so an empty entry acts as
    // the tombstone and is dropped here, AFTER every merge had the chance
    // to see it.
    const auto drop_null_entries = [](auto& table) {
        for (auto it = table.begin(); it != table.end();) {
            if (it->second.empty()) it = table.erase(it);
            else ++it;
        }
    };
    drop_null_entries(s.graphemes);
    drop_null_entries(s.allophones);
    // positional_graphemes: an empty entry is a MEANINGFUL shadow (it
    // blocks inheriting the base's refinements for that grapheme), so it
    // stays.
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

std::vector<std::string> unicode_units(const std::string& text) {
    std::vector<std::string> out;
    for (std::size_t i = 0; i < text.size();) {
        const auto n = utf8_char_size(text, i);
        out.push_back(text.substr(i, n));
        i += n;
    }
    return out;
}

bool vowel_grapheme(const std::string& value) {
    const auto folded = unicode_fold_nfc(value);
    const auto units = unicode_units(folded);
    for (const auto& unit : units) {
        auto u = icu::UnicodeString::fromUTF8(unit); UChar32 cp = 0; int32_t i = 0;
        U16_NEXT(u.getBuffer(), i, u.length(), cp);
        if (u_getCombiningClass(cp) != 0) continue;
        // Unicode-aware orthographic vowel classification. IPA vowels are
        // included because positional rules also inspect resolved IPA atoms.
        if (std::string("aeiouyɑɐɒæɛɨɯɵəɔœøʉʊɪɤɞɜɘɚɝɶ").find(unit) != std::string::npos)
            return true;
        auto name = std::string(); icu::UnicodeString(cp).toUTF8String(name);
        if (name == "á" || name == "é" || name == "í" || name == "ó" || name == "ú" ||
            name == "à" || name == "è" || name == "ì" || name == "ò" || name == "ù" ||
            name == "â" || name == "ê" || name == "î" || name == "ô" || name == "û" ||
            name == "ã" || name == "õ" || name == "ä" || name == "ë" || name == "ï" || name == "ö" || name == "ü") return true;
    }
    return false;
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
std::vector<std::string> segment_ipa_impl(const std::string& ipa, const std::vector<std::string>& atoms) {

    // allophony.py segment_ipa — public; also used by the stress port. Atoms
    // are tried longest-first; a match must not be followed by a modifier.
    std::vector<std::string> sorted_atoms = atoms;
    std::sort(sorted_atoms.begin(), sorted_atoms.end(), [](const auto& a, const auto& b) { return a.size() > b.size(); });
    std::vector<std::string> result;
    for (std::size_t i = 0; i < ipa.size();) {
        std::string match;
        for (const auto& atom : sorted_atoms) if (ipa.compare(i, atom.size(), atom) == 0 && (i + atom.size() == ipa.size() || !ipa_modifier(ipa, i + atom.size()))) { match = atom; break; }
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

std::vector<IPAPath> apply_grammatical_endings(const LanguageSpec& spec,
                                               const std::string& word,
                                               std::vector<IPAPath> paths) {
    // g2p.py _apply_grammatical_ending: rewrite each path's tail when
    // *word* ends in a declared grammatical_endings entry. The word is
    // tokenized and searched exactly as before: this stage only replaces
    // the *emitted segments* of the trailing tokens the ending covers, so
    // no interior grapheme sees a different neighbour and no digraph is
    // re-cut. Paths shorter than the matched tail (a rescorer deleted a
    // slot inside it) are left alone rather than mis-spliced.
    if (paths.empty() || spec.grammatical_endings.empty()) return paths;
    std::vector<std::string> tokens;
    for (const auto& t : PhonetokTokenizer(spec).grapheme_tokens(word))
        tokens.push_back(t.grapheme);
    // Endings are declared in bare orthography, so a silent stress mark
    // anywhere in the tail must not hide the ending. Match on the unmarked
    // token sequence, then widen the span back over the original tokens.
    std::string marks = silent_marks_of(spec);
    std::optional<beam::GrammaticalEnding> match;
    if (marks.empty()) {
        match = beam::match_grammatical_ending(tokens, &spec);
    } else {
        std::vector<std::string> kept;
        std::vector<std::size_t> kept_idx;
        for (std::size_t i = 0; i < tokens.size(); ++i) {
            const std::u32string g32 = to_utf32(tokens[i]);
            bool is_mark = false;
            if (g32.size() == 1) {
                const std::string g8 = to_utf8(g32);
                for (std::size_t k = 0; k < marks.size() && !is_mark;)
                    if (marks.compare(k, utf8_char_size(marks, k), g8) == 0) is_mark = true;
                    else k += utf8_char_size(marks, k);
            }
            if (!is_mark) { kept.push_back(tokens[i]); kept_idx.push_back(i); }
        }
        match = beam::match_grammatical_ending(kept, &spec);
        if (match.has_value() && match->tokens > 0)
            match->tokens =
                tokens.size() - kept_idx[kept_idx.size() - match->tokens];
    }
    if (!match.has_value()) return paths;

    std::vector<IPAPath> rewritten;
    std::set<std::string> seen;
    // *path* with its matched tail replaced by *ipa*.
    auto rewrite = [&](const IPAPath& path, const std::string& ipa, double extra_cost) {
        if (path.segments.size() <= match->tokens) return path;
        IPAPath out = path;
        out.segments.resize(path.segments.size() - match->tokens);
        out.segments.push_back(ipa);
        out.ipa.clear();
        for (const auto& segment : out.segments) out.ipa += segment;
        out.graphemes.resize(path.graphemes.size() - match->tokens);
        if (!path.graphemes.empty()) {
            std::string joined;
            for (std::size_t k = path.graphemes.size() - match->tokens; k < path.graphemes.size(); ++k)
                joined += path.graphemes[k];
            out.graphemes.push_back(joined);
        }
        out.score += extra_cost;
        return out;
    };
    for (const auto& path : paths) {
        const IPAPath next = match->ipa.has_value() ? rewrite(path, *match->ipa, 0.0) : path;
        if (seen.insert(next.ipa).second) rewritten.push_back(next);
    }
    if (!match->alternatives.empty() && !rewritten.empty()) {
        // Rank costs over the declared list, exactly as an ordered grapheme
        // candidate list is costed: element 0 is free, element i pays i.
        std::vector<std::string> candidates;
        candidates.push_back(match->ipa.value_or(""));
        for (const auto& alt : match->alternatives) candidates.push_back(alt);
        const auto costs = beam::candidate_base_costs(candidates, std::nullopt, match->ending);
        // The UNrewritten rank-1 path: an alternative replaces the same
        // tail rank 1 replaced, so splicing it onto the already rewritten
        // path would eat the tail twice.
        const IPAPath& base = paths[0];
        for (std::size_t i = 1; i < match->alternatives.size(); ++i) {
            const IPAPath next = rewrite(base, match->alternatives[i - 1], costs[i]);
            if (seen.insert(next.ipa).second) rewritten.push_back(next);
        }
        // Stable sort: the alternatives take their cost-ordered place among
        // the existing readings, and ties keep beam order.
        std::stable_sort(rewritten.begin(), rewritten.end(),
                         [](const IPAPath& a, const IPAPath& b) { return a.score < b.score; });
    }
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

Tokenizer::Tokenizer(const LanguageSpec& spec) : spec_(spec), tokenizer_(spec) {
    // g2p.py G2P.__init__ engine-side derivations.
    // _uses_aperture: does any grapheme in this spec key on syllable
    // APERTURE? If not, the aperture question is never asked: a stress-less
    // spec skips syllabification altogether, and a stress-declaring spec
    // still skips the per-nucleus open/closed computation.
    static const std::set<std::string> aperture_positions = {
        "open_syllable", "closed_syllable", "nucleus_stressed_open",
        "nucleus_stressed_closed", "nucleus_unstressed_open",
        "nucleus_unstressed_closed"};
    for (const auto& [_, entry] : spec.positional_graphemes) {
        bool disjoint = true;
        for (const auto& [key, __] : entry)
            if (aperture_positions.count(key)) { disjoint = false; break; }
        if (!disjoint) { uses_aperture_ = true; break; }
    }
    // _silent_stress_marks: graphemes this spec declares as stress marks
    // that emit nothing — a written accent whose only job is to say WHERE
    // the stress is (the Russian combining acute). Stress detection reads
    // them; every whole-word key is spelled without them.
    for (const auto& mark : spec.marked_vowels) {
        if (auto it = spec.graphemes.find(mark); it != spec.graphemes.end() && it->second.empty())
            silent_stress_marks_ += mark;
    }
    // g2p.py G2P.__init__ _rescorers: the spec's declarative allophone
    // rules compile into the post-lexical rescorer; a spec with no rules
    // compiles to null, so the chain is empty and the default path stays
    // byte-identical. The allophone pass may run more than once (bounded):
    // each repeat rebuilds the segment context from the previous pass.
    allophone_rescorer_ =
        rescorer::compile_allophone_rescorer(spec.allophone_rules,
                                             spec.doubled_letters_geminate);
    if (allophone_rescorer_ != nullptr)
        rescorers_.assign(static_cast<std::size_t>(std::max(1, spec.allophone_passes)),
                          allophone_rescorer_.get());
}

namespace {
using uni::to_utf32;
using uni::to_utf8;

// g2p.py _is_cliticless: whether *word* is a declared prosodic clitic
// that takes no stress. A written stress mark outranks the class.
bool engine_is_cliticless(const LanguageSpec& spec, const std::string& silent_marks,
                          const std::string& word) {
    if (spec.cliticless_words.empty()) return false;
    for (std::size_t i = 0; i < silent_marks.size();)
        if (word.find(silent_marks.substr(i, utf8_char_size(silent_marks, i))) != std::string::npos)
            return false;
        else i += utf8_char_size(silent_marks, i);
    return stress::is_cliticless(word, spec);
}

// g2p.py _map_tokens_to_syllables: map each grapheme token index to its
// 0-based syllable index. Each token is LOCATED in the syllabified word
// rather than counted into it (the tokenizer emits nothing for a character
// the spec has no grapheme for, so counting desynchronised the two).
// A token that cannot be located at all keeps the syllable of the token
// before it, which is the nearest true answer available.
std::vector<std::size_t> map_tokens_to_syllables(const std::vector<Token>& tokens,
                                                 const std::vector<std::string>& sylls) {
    if (sylls.empty()) return std::vector<std::size_t>(tokens.size(), 0);
    const std::u32string joined = [&] {
        std::u32string j;
        for (const auto& s : sylls) j += to_utf32(s);
        return j;
    }();
    // syllable index of each CHARACTER position of the joined word
    std::vector<std::size_t> owner;
    for (std::size_t idx = 0; idx < sylls.size(); ++idx)
        owner.insert(owner.end(), to_utf32(sylls[idx]).size(), idx);
    // A LENGTH-PRESERVING fold: lower can change a string's length
    // (Turkish İ folds to two characters), which would slide every offset
    // after it against owner — casefolding per character keeps the
    // indices aligned.
    auto fold = [](const std::u32string& text) {
        std::u32string out;
        for (char32_t c : text) {
            const std::u32string lowered = uni::lower_one(c);
            out += lowered.size() == 1 ? lowered : std::u32string(1, c);
        }
        return out;
    };
    const std::u32string folded = fold(joined);
    std::vector<std::size_t> result;
    std::size_t cursor = 0;
    for (const auto& token : tokens) {
        const std::u32string grapheme = to_utf32(token.grapheme);
        long long at = -1;
        if (!grapheme.empty()) {
            const std::u32string needle = fold(grapheme);
            for (std::size_t pos = cursor; pos + needle.size() <= folded.size(); ++pos)
                if (folded.compare(pos, needle.size(), needle) == 0) {
                    at = static_cast<long long>(pos);
                    break;
                }
        }
        if (at < 0) {
            result.push_back(result.empty() ? 0 : result.back());
            continue;
        }
        result.push_back(static_cast<std::size_t>(at) < owner.size()
                             ? owner[static_cast<std::size_t>(at)]
                             : sylls.size() - 1);
        cursor = static_cast<std::size_t>(at) + grapheme.size();
    }
    return result;
}

// g2p.py _collapse_geminates: collapse a run of the same consonant to one.
// Only identical adjacent CONSONANT segments merge; a doubled vowel letter
// is a long vowel and is left alone, and a length or stress mark riding
// between two identical consonants does not block the merge.
std::string collapse_geminates(const std::string& ipa) {
    static const std::u32string VOWEL_IPA = to_utf32("aeiouɑɐɒæɓəɘɛɜɞɤɪɨɯɵøœʊʉʌʏyɶ");
    std::u32string out;
    for (char32_t ch : to_utf32(ipa)) {
        if (!out.empty() && ch == out.back() && VOWEL_IPA.find(ch) == std::u32string::npos &&
                ch != U'ː' && ch != U'ˑ' && uni::category(ch)[0] != 'Z')
            continue;
        out.push_back(ch);
    }
    return to_utf8(out);
}
} // namespace

namespace {
}

std::vector<Token> Tokenizer::tokenize(const std::string& text) const { return tokenizer_.tokenize(text); }

std::vector<Token> Tokenizer::grapheme_tokens(const std::string& text) const {
    return tokenizer_.grapheme_tokens(text);
}

TokenSequence Tokenizer::tokenize_with_context(const std::string& text) const {
    return tokenizer_.tokenize_with_context(text);
}

std::vector<std::string> Tokenizer::tokenize_word(const std::string& word, std::vector<std::string>* unmapped) const {
    std::vector<std::string> result;
    for (const auto& token : tokenize(word)) {
        if (token.kind == TokenKind::GRAPHEME) result.push_back(token.grapheme);
        else if (token.kind == TokenKind::UNKNOWN && unmapped) unmapped->push_back(token.grapheme);
    }
    return result;
}

std::vector<IPAPath> Tokenizer::beam(const std::string& word, std::size_t width) const {
    // g2p.py _positional_beam, sharing the token model's beam machinery
    // (resolve_branches / constrain_nasal_carriers / _expand_beam) so
    // greedy (width 1) and beam search expand slots through the exact
    // same code.
    const auto g_tokens = tokenizer_.grapheme_tokens(word);
    if (g_tokens.empty()) return std::vector<IPAPath>{{"", 0.0, {}, {}}};
    const std::set<std::string> vowel_set(spec_.vowel_graphemes.begin(),
                                          spec_.vowel_graphemes.end());
    const auto seq = flat_contexts(g_tokens, vowel_set);
    const auto& contexts = *seq.graphemes;

    std::vector<std::string> gs;
    gs.reserve(g_tokens.size());
    for (const auto& token : g_tokens) gs.push_back(token.grapheme);

    // Stress/syllable context for the positional beam. Syllabification is
    // needed for TWO independent things: the stress positions (which need
    // the spec's stress rules) and the aperture positions (which need only
    // the syllable's own shape). It is therefore computed unconditionally;
    // a spec with no stress rules still gets open/closed syllables, and
    // still gets no nucleus_stressed/unstressed because those stay gated
    // on the stressed index below.
    std::vector<std::string> sylls;
    if (spec_.stress_defined || uses_aperture_)
        sylls = stress::syllables_for(word, spec_.code, spec_.diphthongs, &spec_);
    std::optional<long long> stressed_syll_idx;
    std::set<std::size_t> secondary_syll_idxs;
    if (spec_.stress_defined) {
        if (sylls.size() > 1)
            stressed_syll_idx = stress::detect_stress(word, spec_, &sylls);
        else
            stressed_syll_idx = 0;  // monosyllable → always stressed
        if (engine_is_cliticless(spec_, silent_stress_marks_, word))
            stressed_syll_idx = -1;  // _CLITIC_NO_STRESS
        // Prominence LEVEL 2. Empty unless the spec declares
        // stress.secondary_stress; a clitic has no main stress, so nothing
        // below it either (the sentinel is negative).
        const auto secondary = stress::secondary_stress_positions(
            static_cast<int>(sylls.size()), stressed_syll_idx, spec_);
        for (const int s : secondary) secondary_syll_idxs.insert(static_cast<std::size_t>(s));
    }

    // Map each grapheme token index to its syllable index.
    const auto syll_for_token = map_tokens_to_syllables(g_tokens, sylls);

    const beam::ApertureView aperture(sylls, &spec_, uses_aperture_);

    std::vector<std::vector<beam::Branch>> slot_branches;
    slot_branches.reserve(contexts.size());
    for (std::size_t i = 0; i < contexts.size(); ++i)
        slot_branches.push_back(beam::resolve_branches(
            spec_, contexts[i], tokenizer_,
            /*allophone_map=*/nullptr, syll_for_token[i], stressed_syll_idx,
            secondary_syll_idxs, aperture.syllable(syll_for_token[i]),
            aperture.is_final(syll_for_token[i])));
    // g2p.py _positional_beam: when a rescorer is configured the slots are
    // re-costed through it — the engine path, unlike the standalone
    // tokenizer, supplies syllable/stress context to the RescoreContext.
    // The rescorers see the pre-rescore candidates of every slot (pure
    // function of slot + context) and run BEFORE the nasal-carrier guard
    // and beam path selection; a slot whose candidates are emptied is
    // deleted and contributes no segment.
    if (!rescorers_.empty()) {
        std::vector<rescorer::SegmentSlot> slots;
        slots.reserve(contexts.size());
        for (std::size_t i = 0; i < contexts.size(); ++i) {
            rescorer::SegmentSlot slot;
            slot.grapheme = g_tokens[i].grapheme;
            slot.span = {g_tokens[i].position, g_tokens[i].position + g_tokens[i].length};
            slot.candidates.reserve(slot_branches[i].size());
            for (const auto& branch : slot_branches[i])
                slot.candidates.push_back({branch.ipa, branch.cost});
            slots.push_back(std::move(slot));
        }
        const auto rescored = rescorer::apply_rescorers(
            slots, contexts, rescorers_, &syll_for_token, stressed_syll_idx);
        for (std::size_t i = 0; i < slot_branches.size(); ++i) {
            auto& branches = slot_branches[i];
            branches.clear();
            for (const auto& cand : rescored[i].candidates)
                branches.push_back({cand.ipa, cand.score});
        }
    }
    beam::constrain_nasal_carriers(slot_branches);

    std::vector<beam::Hypothesis> hyps{{{}, 0.0}};
    std::vector<std::string> spelled;
    for (std::size_t i = 0; i < slot_branches.size(); ++i) {
        if (slot_branches[i].empty()) continue; // deleted/silent slot: no segment
        spelled.push_back(gs[i]);
        hyps = beam::expand_beam(std::move(hyps), slot_branches[i], width);
    }
    std::vector<IPAPath> paths;
    paths.reserve(hyps.size());
    for (auto& hyp : hyps) {
        IPAPath path;
        path.score = hyp.score;
        path.segments = std::move(hyp.segments);
        path.graphemes = spelled;
        for (const auto& segment : path.segments) path.ipa += segment;
        paths.push_back(std::move(path));
    }
    std::sort(paths.begin(), paths.end(), [](const IPAPath& a, const IPAPath& b) {
        if (a.score != b.score) return a.score < b.score;
        return a.ipa < b.ipa;
    });
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

const StressPlugin* get_stress_plugin(const std::string& code) {
    // registry.py get_stress_plugin: the plugin registered for *code*, if
    // any. The C++ registry keys plugins by name; ownership comes from the
    // plugin's declared language codes (priority breaks a tie).
    const StressPlugin* best = nullptr;
    for (const auto& [_, plugin] : stress_plugins)
        if (owns_language(*plugin, code) &&
            (best == nullptr || plugin->priority() > best->priority()))
            best = plugin.get();
    return best;
}

const SyllabifierPlugin* get_syllabifier(const std::string& code) {
    const SyllabifierPlugin* best = nullptr;
    for (const auto& [_, plugin] : syllabifier_plugins)
        if (owns_language(*plugin, code) &&
            (best == nullptr || plugin->priority() > best->priority()))
            best = plugin.get();
    return best;
}
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
// The engine's _silent_stress_marks for *spec* (g2p.py G2P.__init__):
// graphemes declared as stress marks that emit nothing.


namespace {
// g2p.py _finalize_word_ipa: apply the per-path word-final stages to one
// beam path's *ipa* — the stages that turn a raw beam path into the string
// transcribe_word emits: computed tone, geminate collapse, word-final
// virama vowel, iambic length and stress marking. They are factored out so
// that a caller wanting the top-*k* readings runs the SAME pipeline on
// every path instead of re-implementing it.
//
// Cross-word stages (sandhi, dialect transform) are NOT applied here: they
// act on the whole utterance, not on a word.
std::string finalize_word_ipa(const LanguageSpec& spec, const std::string& word,
                              const std::string& ipa_in,
                              const std::optional<std::string>& forced_ipa,
                              const IPAPath* path, bool collapse_geminates_flag) {
    std::string ipa = ipa_in;
    // Computed tone runs first, on the path's own slots: it is the only
    // stage that needs to see which grapheme produced which segment, and
    // every stage after it works on the string.
    if (spec.tone.has_value() && path != nullptr &&
            !path->graphemes.empty() && ipa == path->ipa) {
        ipa = tone::assign_computed_tones(path->graphemes, path->segments,
                                          *spec.tone, spec.phonemes);
    }
    if (collapse_geminates_flag && spec.collapse_geminates && !ipa.empty())
        ipa = collapse_geminates(ipa);
    // Word-final virama: run AFTER geminate collapse so a doubled letter
    // that closes with a virama gets the vowel attached to the
    // already-collapsed geminate.
    if (!spec.virama_final_vowel.empty() && !word.empty() && !ipa.empty()) {
        const std::u32string word32 = to_utf32(word);
        if (uni::combining(word32.back()) == 9 /* _VIRAMA_COMBINING_CLASS */) {
            const std::u32string ipa32 = to_utf32(ipa);
            if (!vowels::is_ipa_vowel(ipa32.back())) ipa += spec.virama_final_vowel;
        }
    }
    // Tone-mark docking (g2p.py _finalize_word_ipa): a spec whose
    // convention writes the tone mark at the SYLLABLE EDGE reads the
    // computed marks back into place — after the virama vowel, before
    // iambic length and stress.
    if (spec.tone_marks_syllable_final && !ipa.empty())
        ipa = tone::dock_tone_marks(ipa, spec.phonemes);
    // A forced reading is not re-stressed: `ph` is the pronunciation, mark
    // and all. A caller who wrote a mark has placed the stress.
    if (!forced_ipa.has_value() && spec.stress_defined && !ipa.empty() &&
            spec.iambic_length && !engine_is_cliticless(spec, silent_marks_of(spec), word)) {
        // Runs BEFORE the stress mark: it lengthens a nucleus by weight,
        // never moves the mark.
        ipa = stress::apply_iambic_length(ipa, spec);
    }
    if (!forced_ipa.has_value() && spec.stress_defined && !ipa.empty() &&
            !engine_is_cliticless(spec, silent_marks_of(spec), word)) {
        if (spec.quantity_sensitive) {
            // Weight is a property of the transcription, not the spelling.
            const int idx = stress::detect_stress_by_weight(ipa, spec);
            // Mark the mark against the SAME division the weights were read
            // off: the naive syllabify cuts saːliq as sa|ːliq, which would
            // drop the mark inside the long vowel.
            const std::vector<std::string> ipa_sylls =
                stress::syllabify_ipa(ipa, spec.max_onset);
            ipa = stress::apply_stress_mark(ipa, spec, idx, /*syllables=*/nullptr,
                                            &ipa_sylls);
        } else {
            const auto sylls = stress::syllables_for(word, spec.code, spec.diphthongs, &spec);
            const int idx = stress::detect_stress(word, spec, &sylls);
            std::string mark;
            if (!spec.accent2_mark.empty() && sylls.size() >= 2) {
                const bool penult =
                    idx >= 0 ? idx == static_cast<int>(sylls.size()) - 2 : idx == -2;
                if (penult && !word.empty()) {
                    const std::u32string lowered_last =
                        uni::lower_one(to_utf32(word).back());
                    for (const auto& letter : spec.accent2_final_letters)
                        if (to_utf8(lowered_last) == letter) mark = spec.accent2_mark;
                }
            }
            const auto secondary = stress::secondary_stress_positions(
                static_cast<int>(sylls.size()), idx, spec);
            const std::vector<int> secondary_indices(secondary.begin(), secondary.end());
            ipa = stress::apply_stress_mark(ipa, spec, idx, &sylls,
                                            /*ipa_syllables=*/nullptr, mark,
                                            secondary_indices);
        }
    }
    // NOT NFC-composed here: cross-word sandhi still needs to run on this
    // per-word IPA (see the note in g2p.py _transcribe_word).
    return ipa;
}
} // namespace

WordTranscription G2P::transcribe_one(const std::string& word, std::size_t width,
                                      bool forced, const std::string& forced_ipa) const {
    // g2p.py _transcribe_word: the per-word stage of the pipeline —
    // whole-word overrides (forced / word_exceptions / lexicon), the beam,
    // grammatical endings, rescorer plugins, then the word-final ordering
    // (computed tone, geminate collapse, word-final virama, tone-mark
    // docking, iambic length, stress). Cross-word stages (sandhi, dialect
    // transform) belong to transcribe_detailed.
    WordTranscription wt; wt.word = word;
    auto it = spec_->word_exceptions.find(lower_ascii(word));
    if (forced) {
        if (forced_ipa.empty()) throw std::invalid_argument("empty forced pronunciation");
        auto atoms = inventory(*spec_); const std::vector<std::string> atom_list(atoms.begin(), atoms.end()); const auto segments = segment_ipa(forced_ipa, atom_list);
        for (const auto& segment : segments) if (!atoms.count(segment) && segment != "ˈ" && segment != "ˌ") throw std::invalid_argument("forced pronunciation contains undeclared IPA: " + segment);
    }
    auto paths = forced ? std::vector<IPAPath>{{forced_ipa, 0.0, {}, {}}} :
        (it != spec_->word_exceptions.end() ? std::vector<IPAPath>{{it->second, 0.0, {}, {}}} : candidates(word, width));
    auto lex = it == spec_->word_exceptions.end() ? load_lexicon(language_) : std::map<std::string, std::string>{};
    if (it == spec_->word_exceptions.end()) { auto li = lex.find(unicode_fold_nfc(word)); if (li != lex.end()) paths = {{li->second, 0.0, {}, {}}}; }
    if (!forced && it == spec_->word_exceptions.end() && lex.find(unicode_fold_nfc(word)) == lex.end())
        paths = apply_grammatical_endings(*spec_, word, std::move(paths));
    auto ordered_rescorers = plugin_names(*spec_, "rescore");
    {
        auto override_names = plugin_overrides_.find("rescore");
        if (override_names != plugin_overrides_.end()) ordered_rescorers = override_names->second;
    }
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
    // The word-final pipeline runs per word, BEFORE the cross-word stages.
    if (!paths.empty()) {
        const bool has_override =
            forced || it != spec_->word_exceptions.end() ||
            lex.find(unicode_fold_nfc(word)) != lex.end();
        wt.ipa = finalize_word_ipa(
            *spec_, word, paths.front().ipa,
            forced ? std::optional<std::string>(forced_ipa) : std::nullopt,
            has_override ? nullptr : &paths.front(),
            /*collapse_geminates_flag=*/!has_override);
    } else {
        wt.ipa = word;
    }
    wt.candidates = paths; wt.confidence = word_confidence(word, width);
    return wt;
}

std::string G2P::transcribe_word(const std::string& word, const std::string& search,
                                 std::size_t beam_width) const {
    if (search != "greedy" && search != "beam") throw std::invalid_argument("search must be greedy or beam");
    // Output contract: NFC composed here, at the emission boundary (see
    // the note in g2p.py transcribe_word).
    const std::string ipa = transcribe_one(
        word, search == "greedy" ? 1 : beam_width, false, "").ipa;
    return ipa.empty() ? ipa : unicode_nfc(ipa, false);
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
        const WordTranscription wt = transcribe_one(
            input.surface, search == "greedy" ? 1 : width, input.forced, input.forced_ipa);
        result.words.push_back(wt); surfaces.push_back(wt.word); ipa_words.push_back(wt.ipa); pausal.push_back(input.pausal);
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
        if (i) result.ipa += " ";
        result.ipa += ipa_words[i];
        result.words[i].ipa = ipa_words[i];
    }
    if (!dialect_profile_.empty()) {
        std::string orthography; for (std::size_t i = 0; i < surfaces.size(); ++i) { if (i) orthography += " "; orthography += surfaces[i]; }
        result.ipa = apply_dialect_impl(result.ipa, dialect_profile_, orthography);
        std::stringstream split(result.ipa); for (auto& word : result.words) split >> word.ipa;
    }
    result.ipa = unicode_nfc(result.ipa, false);
    for (auto& word : result.words) word.ipa = unicode_nfc(word.ipa, false);
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
std::vector<std::string> segment_ipa(const std::string& ipa, std::vector<std::string> atoms) {
    // allophony.py segment_ipa — public; also used by the stress port. Atoms
    // are tried longest-first; a match must not be followed by a modifier.
    return segment_ipa_impl(ipa, atoms);
}

} // namespace orthography2ipa
