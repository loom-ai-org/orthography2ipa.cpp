// A Pipeline belongs to one engine, and no other engine can see it. The cases of
// the reference's tests/test_pipeline.py. Its expected values come from the
// lexicon entries the test writes, from the spec, and from the textbook readings
// of the probe words (pt-PT ⟨gato⟩ [ˈɡatu], es-ES ⟨cantaba⟩ [kanˈtaβa]).
//
// The reference's two tests of a patched `g2p.get_lexicon` name a Python module
// attribute, which has no C++ counterpart. Its deprecation tests read a run-time
// warning: here the attribute text is compared in tests/pipeline_parity.py, and
// the last function of this file fails to compile on a deprecated call.
//
// The engine asks a syllabifier once for each use of a word, where the reference
// caches the division per engine, so the call checks compare the set of calls.
#include "orthography2ipa/inventory.hpp"
#include "orthography2ipa/markup.hpp"
#include "orthography2ipa/orthography2ipa.hpp"
#include "orthography2ipa/stress.hpp"
#include "plugin_registry.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <unistd.h>

using namespace orthography2ipa;
using inventory::emission_inventory;
using inventory::phoneme_atoms;
using inventory::phoneme_inventory;
using inventory::tokenize;
namespace fs = std::filesystem;

namespace {

using Lexicon = std::map<std::string, std::string>;
using Division = std::vector<std::string>;

int failures = 0;

#define CHECK(condition)                                                              \
    do {                                                                              \
        if (!(condition)) {                                                           \
            std::cout << "FAIL " << __func__ << ":" << __LINE__ << ": " #condition "\n"; \
            ++failures;                                                               \
        }                                                                             \
    } while (0)

#define CHECK_THROWS(expression, type, text)                                          \
    do {                                                                              \
        std::string caught;                                                           \
        try {                                                                         \
            expression;                                                               \
        } catch (const type& error) {                                                 \
            caught = error.what();                                                    \
        }                                                                             \
        if (caught.find(text) == std::string::npos) {                                 \
            std::cout << "FAIL " << __func__ << ":" << __LINE__ << ": " #expression   \
                         " gave [" << caught << "]\n";                                \
            ++failures;                                                               \
        }                                                                             \
    } while (0)

const std::string GATO_RULES = "ˈɡatu";
const std::string GATO_ENTRY = "ˈɡaʃtɐ";
const std::string GATO_OTHER = "ˈɡɔtɔ";
const std::string CANTABA_RULES = "kanˈtaβa";
// The mark is anchored from the end of the word, so a division that gives one
// syllable puts it on the last transcribed syllable.
const std::string CANTABA_ONE_SYLLABLE = "kantaˈβa";
// A click: no Portuguese table emits it, so only a lexicon can declare it.
const std::string CLICK = "ʘ";

fs::path tmp;
int serial = 0;

std::string tsv(const std::vector<std::pair<std::string, std::string>>& rows,
                const fs::path& path = {}) {
    const fs::path file = path.empty() ? tmp / ("lexicon-" + std::to_string(++serial) + ".tsv") : path;
    fs::create_directories(file.parent_path());
    std::ofstream out(file, std::ios::binary);
    for (const auto& [word, ipa] : rows) out << word << '\t' << ipa << '\n';
    return file.string();
}

G2P engine(const std::string& lang, const Pipeline& pipeline) {
    return G2P(lang, {}, "", false, true, true, true, nullptr, nullptr, pipeline);
}

G2P with_lexicon(const std::string& lang, const Lexicon& lexicon) {
    return engine(lang, Pipeline{lexicon});
}

// Answers with the division it was given, and records its calls.
class Fixed : public SyllabifierPlugin {
public:
    explicit Fixed(std::optional<Division> division) : division_(std::move(division)) {}
    std::vector<std::string> language_codes() const override { return {"es-ES"}; }
    std::vector<std::string> syllabify(const std::string& word, const std::string& lang) const override {
        calls.emplace_back(word, lang);
        if (!division_.has_value()) throw std::invalid_argument("cannot split");
        return *division_;
    }
    mutable std::vector<std::pair<std::string, std::string>> calls;

private:
    std::optional<Division> division_;
};

const Division ONE_SYLLABLE = {"cantaba"};
const Division RULES_DIVISION = {"can", "ta", "ba"};

G2P with_syllabifier(const std::shared_ptr<const SyllabifierPlugin>& plugin) {
    return engine("es-ES", Pipeline{std::nullopt, plugin});
}

// A plugin discovery finds for es-ES, drawing the division of the rules, for
// the life of the object.
struct Discovered {
    std::shared_ptr<Fixed> plugin = std::make_shared<Fixed>(RULES_DIVISION);
    SyllabifierRegistry saved = exchange_syllabifiers({{"fixed", plugin}});
    ~Discovered() { exchange_syllabifiers(std::move(saved)); }
};

// The calls were all one call: the word and the language the engine asks for.
bool only_cantaba(const Fixed& plugin) {
    if (plugin.calls.empty()) return false;
    for (const auto& call : plugin.calls)
        if (call != std::make_pair(std::string("cantaba"), std::string("es-ES"))) return false;
    return true;
}

// An installed package that supplies a pt-PT lexicon.
std::string install_pt_package() {
    const fs::path root = tmp / ("root-" + std::to_string(++serial));
    const std::string path = tsv({{"gato", GATO_ENTRY}}, root / "pt-PT" / "fixture.tsv");
    setenv("ORTHOGRAPHY2IPA_PACKAGED_LEXICONS", root.c_str(), 1);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    clear_lexicons();
#pragma GCC diagnostic pop
    return path;
}

// ─── the defect: one engine's lexicon reached every other engine ────────────

void plain_engine_keeps_output_after_another_engine_takes_a_lexicon() {
    install_pt_package();
    CHECK(G2P("pt-PT").transcribe_word("gato") == GATO_RULES);
    const G2P other = with_lexicon("pt-PT", packaged_lexicon("pt-PT"));
    CHECK(other.transcribe_word("gato") == GATO_ENTRY);
    CHECK(G2P("pt-PT").transcribe_word("gato") == GATO_RULES);
    CHECK(available_lexicon_codes().empty());
}

void engines_with_different_lexicons_run_side_by_side() {
    const G2P first = with_lexicon("pt-PT", {{"gato", GATO_ENTRY}});
    const G2P second = with_lexicon("pt-PT", {{"gato", GATO_OTHER}});
    const G2P plain("pt-PT");
    for (int i = 0; i < 2; ++i) {
        CHECK(first.transcribe_word("gato") == GATO_ENTRY);
        CHECK(second.transcribe_word("gato") == GATO_OTHER);
        CHECK(plain.transcribe_word("gato") == GATO_RULES);
    }
}

// ─── precedence: spec.word_exceptions > pipeline lexicon > rules ────────────

void word_exceptions_beat_the_pipeline_lexicon() {
    const auto& exceptions = get("pt-PT").word_exceptions;
    CHECK(exceptions.at("e") == "i");
    CHECK(with_lexicon("pt-PT", {{"e", CLICK}}).transcribe_word("e") == "i");
}

void pipeline_lexicon_beats_the_rules() {
    CHECK(with_lexicon("pt-PT", {{"gato", GATO_ENTRY}}).transcribe_word("gato") == GATO_ENTRY);
}

void a_word_outside_the_pipeline_lexicon_takes_the_rules() {
    CHECK(with_lexicon("pt-PT", {{"cão", "ˈkɐ̃w̃"}}).transcribe_word("gato") == GATO_RULES);
}

// ─── a hit takes the same path as a word exception ──────────────────────────

void an_entry_with_no_mark_gets_the_stress_of_the_spec() {
    CHECK(with_lexicon("pt-PT", {{"gato", "ɡaʃtɐ"}}).transcribe_word("gato") == GATO_ENTRY);
}

void a_hit_is_found_through_case_and_edge_punctuation() {
    CHECK(with_lexicon("pt-PT", {{"gato", GATO_ENTRY}}).transcribe("Gato,") == GATO_ENTRY);
}

void a_hit_reports_full_confidence() {
    const auto result = with_lexicon("pt-PT", {{"gato", GATO_ENTRY}}).transcribe_detailed("gato");
    CHECK(result.words.at(0).confidence == 1.0);
}

// ─── load_lexicon and packaged_lexicon return a mapping and set no state ────

void load_lexicon_returns_the_file_entries() {
    const std::string path = tsv({{"gato", GATO_ENTRY}, {"gato", GATO_OTHER}, {"rato", "ˈʁatu"}});
    CHECK(load_lexicon(path) == (Lexicon{{"gato", GATO_ENTRY}, {"rato", "ˈʁatu"}}));
}

void load_lexicon_sets_no_state() {
    load_lexicon(tsv({{"gato", GATO_ENTRY}}, tmp / "state" / "pt-PT.tsv"));
    CHECK(available_lexicon_codes().empty());
    CHECK(G2P("pt-PT").transcribe_word("gato") == GATO_RULES);
}

void load_lexicon_raises_for_a_missing_file() {
    CHECK_THROWS(load_lexicon((tmp / "absent.tsv").string()), FileNotFoundError, "absent.tsv");
}

void packaged_lexicon_returns_the_provider_entries() {
    install_pt_package();
    CHECK(packaged_lexicon("pt-PT") == (Lexicon{{"gato", GATO_ENTRY}}));
}

void packaged_lexicon_resolves_an_alias() {
    install_pt_package();
    CHECK(packaged_lexicon("pt") == (Lexicon{{"gato", GATO_ENTRY}}));
}

void packaged_lexicon_raises_with_no_provider() {
    install_pt_package();
    CHECK_THROWS(packaged_lexicon("en-GB"), LookupError,
                 "no installed package supplies a lexicon file for 'en-GB'; installed: ['pt-PT']");
}

void packaged_lexicon_raises_when_the_provider_fails() {
    const std::string path = install_pt_package();
    tsv({{"gato", GATO_OTHER}}, fs::path(path).parent_path() / "second.tsv");
    CHECK_THROWS(packaged_lexicon("pt-PT"), LookupError, "pt-PT");
}

// ─── the inventory counts the engine's lexicon ──────────────────────────────

void engine_inventory_counts_its_own_lexicon() {
    const std::string forced = "<phoneme ph=\"" + CLICK + "a\">x</phoneme>";
    const G2P own = with_lexicon("pt-PT", {{"x", CLICK + "a"}, {"y", CLICK}});
    CHECK(own.transcribe(forced) == CLICK + "a");
    CHECK_THROWS(G2P("pt-PT").transcribe(forced), markup::MarkupError,
                 "<phoneme ph='" + CLICK + "a'> uses ['" + CLICK + "'], which the pt-PT spec does not declare.\n\n");
}

void forced_check_tokenizes_as_the_inventory_does() {
    CHECK(G2P("de-DE").transcribe("<phoneme ph=\"ɡato\">gato</phoneme>") == "ɡato");
    CHECK(G2P("pt-PT").transcribe("<phoneme ph=\"ə\">x</phoneme>") == "ə");
    CHECK_THROWS(G2P("ht").transcribe("<phoneme ph=\"ˈa\">x</phoneme>"), markup::MarkupError, "ˈ");
}

void inventory_functions_read_the_mapping_they_are_given() {
    const LanguageSpec& spec = get("pt-PT");
    const Lexicon lexicon = {{"x", CLICK + "a"}};
    const auto atoms = phoneme_atoms(spec, &lexicon);
    CHECK(emission_inventory(spec, &lexicon).count(CLICK + "a") == 1);
    CHECK(emission_inventory(spec).count(CLICK + "a") == 0);
    CHECK(phoneme_inventory(spec).count(CLICK) == 0);
    CHECK(std::find(atoms.begin(), atoms.end(), CLICK + "a") != atoms.end());
    CHECK(tokenize(CLICK + "a", spec, &lexicon) == (Division{CLICK + "a"}));
    CHECK(tokenize(CLICK + "a", spec) == (Division{CLICK, "a"}));
}

// ─── the syllabifier ────────────────────────────────────────────────────────

void pipeline_syllabifier_wins_over_discovery() {
    const Discovered discovered;
    const auto mine = std::make_shared<Fixed>(ONE_SYLLABLE);
    CHECK(with_syllabifier(mine).transcribe_word("cantaba") == CANTABA_ONE_SYLLABLE);
    CHECK(only_cantaba(*mine));
    CHECK(discovered.plugin->calls.empty());
}

void engine_with_no_syllabifier_keeps_discovery() {
    const Discovered discovered;
    with_syllabifier(std::make_shared<Fixed>(ONE_SYLLABLE)).transcribe_word("cantaba");
    CHECK(discovered.plugin->calls.empty());
    CHECK(G2P("es-ES").transcribe_word("cantaba") == CANTABA_RULES);
    CHECK(only_cantaba(*discovered.plugin));
}

void engines_with_different_syllabifiers_run_side_by_side() {
    const Discovered discovered;
    const G2P first = with_syllabifier(std::make_shared<Fixed>(ONE_SYLLABLE));
    const G2P second = with_syllabifier(std::make_shared<Fixed>(RULES_DIVISION));
    for (int i = 0; i < 2; ++i) {
        CHECK(first.transcribe_word("cantaba") == CANTABA_ONE_SYLLABLE);
        CHECK(second.transcribe_word("cantaba") == CANTABA_RULES);
    }
}

void a_word_the_syllabifier_cannot_split_goes_to_the_bundled_splitter() {
    for (const std::optional<Division>& division :
             std::vector<std::optional<Division>>{Division{"x"}, Division{}, std::nullopt}) {
        const Discovered discovered;
        const auto mine = std::make_shared<Fixed>(division);
        CHECK(with_syllabifier(mine).transcribe_word("cantaba") == CANTABA_RULES);
        CHECK(only_cantaba(*mine));
        CHECK(discovered.plugin->calls.empty());
    }
}

void detect_stress_takes_the_syllabifier() {
    const Discovered discovered;
    const LanguageSpec& spec = get("es-ES");
    const Fixed mine(ONE_SYLLABLE);
    CHECK(stress::detect_stress("cantaba", spec, nullptr, "es-ES") == 1);
    CHECK(stress::detect_stress("cantaba", spec, nullptr, "es-ES", &mine) == 0);
    CHECK(stress::detect_stress("cantaba", spec, nullptr, "", &mine) == 0);
    CHECK(mine.calls == (std::vector<std::pair<std::string, std::string>>{
                            {"cantaba", "es-ES"}, {"cantaba", ""}}));
}

// ─── the object ─────────────────────────────────────────────────────────────

static_assert(!std::is_copy_assignable_v<Pipeline>, "a Pipeline is frozen");
static_assert(std::is_copy_assignable_v<G2P>, "an engine stays assignable");

void an_engine_holds_its_pipeline() {
    const Lexicon lexicon = {{"gato", GATO_ENTRY}};
    const auto plugin = std::make_shared<const Fixed>(ONE_SYLLABLE);
    const G2P own = engine("pt-PT", Pipeline{lexicon, plugin});
    CHECK(own.pipeline().lexicon == lexicon);
    CHECK(own.pipeline().syllabifier == plugin);
    CHECK(!G2P("pt-PT").pipeline().lexicon.has_value());
    CHECK(G2P("pt-PT").pipeline().syllabifier == nullptr);
}

// ─── the process-wide API is deprecated and still works ─────────────────────

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

void pipeline_lexicon_replaces_the_process_lexicon() {
    register_lexicon("pt-PT", tsv({{"gato", GATO_OTHER}, {"rato", GATO_OTHER}}));
    const G2P own = with_lexicon("pt-PT", {{"gato", GATO_ENTRY}});
    CHECK(own.transcribe_word("gato") == GATO_ENTRY);
    CHECK(own.transcribe_word("rato") == "ˈʁatu");
    clear_lexicons();
}

void empty_pipeline_lexicon_is_a_lexicon_with_no_entries() {
    register_lexicon("pt-PT", tsv({{"gato", GATO_OTHER}}));
    CHECK(with_lexicon("pt-PT", {}).transcribe_word("gato") == GATO_RULES);
    clear_lexicons();
}

void pipeline_without_a_lexicon_reads_the_process_lexicon() {
    register_lexicon("pt-PT", tsv({{"gato", GATO_OTHER}}));
    CHECK(engine("pt-PT", Pipeline{}).transcribe_word("gato") == GATO_OTHER);
    CHECK(G2P("pt-PT").transcribe_word("gato") == GATO_OTHER);
    clear_lexicons();
}

void engine_inventory_ignores_the_process_lexicon() {
    register_lexicon("pt-PT", tsv({{"gato", CLICK}}));
    const std::string forced = "<phoneme ph=\"" + CLICK + "\">x</phoneme>";
    CHECK_THROWS(with_lexicon("pt-PT", {}).transcribe(forced), markup::MarkupError, CLICK);
    clear_lexicons();
}

void forced_check_of_a_plain_engine_reads_the_process_lexicon() {
    register_lexicon("pt-PT", tsv({{"gato", "ǃa"}}));
    const std::string forced = "<phoneme ph=\"ǃa\">x</phoneme>";
    CHECK(G2P("pt-PT").transcribe(forced) == "ǃa");
    CHECK_THROWS(with_lexicon("pt-PT", {}).transcribe(forced), markup::MarkupError, "ǃ");
    clear_lexicons();
}

void inventory_with_a_mapping_ignores_the_process_lexicon() {
    const LanguageSpec& spec = get("pt-PT");
    const Lexicon lexicon = {{"x", CLICK + "a"}}, empty;
    register_lexicon("pt-PT", tsv({{"gato", "ǃ"}}));
    CHECK(emission_inventory(spec).count("ǃ") == 1);
    CHECK(emission_inventory(spec, &lexicon).count("ǃ") == 0);
    CHECK(emission_inventory(spec, &empty).count("ǃ") == 0);
    clear_lexicons();
}

void register_lexicon_works() {
    register_lexicon("pt-PT", tsv({{"gato", GATO_ENTRY}}));
    CHECK(G2P("pt-PT").transcribe_word("gato") == GATO_ENTRY);
    clear_lexicons();
}

void set_lexicon_dir_works() {
    const fs::path dir = tmp / "lexicon-dir";
    tsv({{"gato", GATO_ENTRY}}, dir / "pt-PT.tsv");
    set_lexicon_directory(dir.string());
    CHECK(G2P("pt-PT").transcribe_word("gato") == GATO_ENTRY);
    clear_lexicons();
}

void lexicon_dir_variable_works() {
    const fs::path dir = tmp / "lexicon-variable";
    tsv({{"gato", GATO_ENTRY}}, dir / "pt-PT.tsv");
    setenv("ORTHOGRAPHY2IPA_LEXICON_DIR", dir.c_str(), 1);
    clear_lexicons();
    CHECK(G2P("pt-PT").transcribe_word("gato") == GATO_ENTRY);
    unsetenv("ORTHOGRAPHY2IPA_LEXICON_DIR");
    clear_lexicons();
}

void use_packaged_lexicons_works() {
    install_pt_package();
    CHECK(use_packaged_lexicons({"pt-PT"}) == (Division{"pt-PT"}));
    CHECK(G2P("pt-PT").transcribe_word("gato") == GATO_ENTRY);
    clear_lexicons();
}

void clear_lexicons_works() {
    register_lexicon("pt-PT", tsv({{"gato", GATO_ENTRY}}));
    clear_lexicons();
    CHECK(G2P("pt-PT").transcribe_word("gato") == GATO_RULES);
}

void get_lexicon_works() {
    register_lexicon("pt-PT", tsv({{"gato", GATO_ENTRY}}));
    CHECK(get_lexicon("pt-PT") == (Lexicon{{"gato", GATO_ENTRY}}));
    clear_lexicons();
}

#pragma GCC diagnostic pop

// The reference's test_the_new_path_warns_about_nothing: a deprecated call here
// stops the build.
#pragma GCC diagnostic push
#pragma GCC diagnostic error "-Wdeprecated-declarations"

void the_new_path_uses_no_deprecated_call() {
    const std::string path = install_pt_package();
    const Lexicon lexicon = packaged_lexicon("pt-PT");
    CHECK(load_lexicon(path) == lexicon);
    CHECK(packaged_lexicon_sources() == (Lexicon{{"pt-PT", path}}));
    const G2P own = with_lexicon("pt-PT", lexicon);
    CHECK(own.transcribe("gato") == GATO_ENTRY);
    CHECK(own.transcribe("rato") == "ˈʁatu");
    CHECK(G2P("pt-PT").transcribe("gato") == GATO_RULES);
    CHECK(!phoneme_inventory(get("pt-PT")).empty());
}

#pragma GCC diagnostic pop

}  // namespace

int main() {
    char pattern[] = "/tmp/o2i-pipeline-XXXXXX";
    const char* made = mkdtemp(pattern);
    if (made == nullptr) return 2;
    tmp = made;
    unsetenv("ORTHOGRAPHY2IPA_LEXICON_DIR");
    setenv("ORTHOGRAPHY2IPA_PACKAGED_LEXICONS", "", 1);

    plain_engine_keeps_output_after_another_engine_takes_a_lexicon();
    engines_with_different_lexicons_run_side_by_side();
    word_exceptions_beat_the_pipeline_lexicon();
    pipeline_lexicon_beats_the_rules();
    a_word_outside_the_pipeline_lexicon_takes_the_rules();
    pipeline_lexicon_replaces_the_process_lexicon();
    empty_pipeline_lexicon_is_a_lexicon_with_no_entries();
    pipeline_without_a_lexicon_reads_the_process_lexicon();
    an_entry_with_no_mark_gets_the_stress_of_the_spec();
    a_hit_is_found_through_case_and_edge_punctuation();
    a_hit_reports_full_confidence();
    load_lexicon_returns_the_file_entries();
    load_lexicon_sets_no_state();
    load_lexicon_raises_for_a_missing_file();
    packaged_lexicon_returns_the_provider_entries();
    packaged_lexicon_resolves_an_alias();
    packaged_lexicon_raises_with_no_provider();
    packaged_lexicon_raises_when_the_provider_fails();
    engine_inventory_counts_its_own_lexicon();
    engine_inventory_ignores_the_process_lexicon();
    forced_check_of_a_plain_engine_reads_the_process_lexicon();
    forced_check_tokenizes_as_the_inventory_does();
    inventory_functions_read_the_mapping_they_are_given();
    inventory_with_a_mapping_ignores_the_process_lexicon();
    pipeline_syllabifier_wins_over_discovery();
    engine_with_no_syllabifier_keeps_discovery();
    engines_with_different_syllabifiers_run_side_by_side();
    a_word_the_syllabifier_cannot_split_goes_to_the_bundled_splitter();
    detect_stress_takes_the_syllabifier();
    an_engine_holds_its_pipeline();
    register_lexicon_works();
    set_lexicon_dir_works();
    lexicon_dir_variable_works();
    use_packaged_lexicons_works();
    clear_lexicons_works();
    get_lexicon_works();
    the_new_path_uses_no_deprecated_call();

    std::error_code ignored;
    fs::remove_all(tmp, ignored);
    if (failures == 0) std::cout << "pipeline: ok\n";
    return failures == 0 ? 0 : 1;
}
