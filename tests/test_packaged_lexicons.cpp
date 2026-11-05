// Packaged lexicons: discoverable when installed, used only when enabled.
// The cases of the reference's tests/test_lexicon_packaged.py, over provider
// roots in a temporary directory that $ORTHOGRAPHY2IPA_PACKAGED_LEXICONS names.
#include "orthography2ipa/orthography2ipa.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include <unistd.h>

using namespace orthography2ipa;
namespace fs = std::filesystem;

namespace {

int failures = 0;

#define CHECK(condition)                                                              \
    do {                                                                              \
        if (!(condition)) {                                                           \
            std::cout << "FAIL " << __func__ << ":" << __LINE__ << ": " #condition "\n"; \
            ++failures;                                                               \
        }                                                                             \
    } while (0)

// English ⟨yacht⟩ is /jɒt/, which the en-GB rules do not give from the spelling;
// ⟨colonel⟩ is /ˈkɜːnəl/. Each fixture lexicon holds these, so a hit is visible.
const std::string YACHT = "jɒt";
const std::string COLONEL = "ˈkɜːnəl";
const std::vector<std::string> WORDS = {"yacht", "colonel", "nation", "the", "choir"};

fs::path tmp;

fs::path tsv(const fs::path& path, const std::string& body) {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << body;
    return path;
}

// The roots the next discovery reads. clear_lexicons forgets the last one.
void install(const std::vector<fs::path>& roots) {
    std::string value;
    for (const auto& root : roots) value += (value.empty() ? "" : ":") + root.string();
    setenv("ORTHOGRAPHY2IPA_PACKAGED_LEXICONS", value.c_str(), 1);
    clear_lexicons();
}

// A fresh root holding an en-GB provider, installed and not enabled.
fs::path installed_en(const std::string& name) {
    const fs::path root = tmp / name;
    tsv(root / "en-GB" / "wikipron.tsv", "colonel\t" + COLONEL + "\nyacht\t" + YACHT + "\n");
    install({root});
    return root;
}

std::vector<std::string> transcribe_all(const std::string& code) {
    std::vector<std::string> out;
    const G2P engine(code);
    for (const auto& word : WORDS) out.push_back(engine.transcribe_word(word));
    return out;
}

struct Stderr {
    std::ostringstream text;
    std::streambuf* saved = std::cerr.rdbuf(text.rdbuf());
    ~Stderr() { std::cerr.rdbuf(saved); }
    bool has(const std::string& part) const { return text.str().find(part) != std::string::npos; }
};

std::vector<std::string> rules;

// ─── installed, not enabled: nothing changes ────────────────────────────────

void installed_provider_changes_no_output() {
    installed_en("unused");
    const auto got = transcribe_all("en-GB");
    CHECK(available_lexicon_codes().empty());
    for (const auto& code : available_codes()) CHECK(get_lexicon(code).empty());
    CHECK(got == rules);
    CHECK(got[0] != "ˈ" + YACHT);
    // Transcription never discovers: the roots named after it are the ones read.
    const fs::path later = tmp / "later";
    tsv(later / "pt-PT" / "x.tsv", "casa\tkazɐ\n");
    setenv("ORTHOGRAPHY2IPA_PACKAGED_LEXICONS", later.c_str(), 1);
    CHECK(packaged_lexicon_sources() == (std::map<std::string, std::string>{{"pt-PT", (later / "pt-PT" / "x.tsv").string()}}));
}

void installed_provider_is_discoverable() {
    const fs::path root = installed_en("discoverable");
    CHECK(packaged_lexicon_sources() == (std::map<std::string, std::string>{{"en-GB", (root / "en-GB" / "wikipron.tsv").string()}}));
    CHECK(available_lexicon_codes().empty());   // listed, still not enabled
    CHECK(get_lexicon("en-GB").empty());
}

// ─── enabled ────────────────────────────────────────────────────────────────

void enabled_lexicon_is_used() {
    installed_en("enabled");
    CHECK(use_packaged_lexicons() == std::vector<std::string>{"en-GB"});
    CHECK(get_lexicon("en-GB") == (std::map<std::string, std::string>{{"colonel", COLONEL}, {"yacht", YACHT}}));
    CHECK(G2P("en-GB").transcribe_word("yacht") == "ˈ" + YACHT);
    CHECK(G2P("en").transcribe_word("yacht") == "ˈ" + YACHT);
    CHECK(available_lexicon_codes() == std::vector<std::string>{"en-GB"});
}

void enable_by_code_and_by_alias() {
    installed_en("alias");
    CHECK(use_packaged_lexicons({"en"}) == std::vector<std::string>{"en-GB"});
    CHECK(G2P("en-GB").transcribe_word("colonel") == COLONEL);
}

void enabling_a_code_with_no_provider_raises() {
    installed_en("missing");
    std::string message;
    try {
        use_packaged_lexicons({"pt-PT"});
    } catch (const LookupError& error) {
        message = error.what();
    }
    CHECK(message == "no installed package provides a lexicon for ['pt-PT']; installed: ['en-GB']");
    CHECK(available_lexicon_codes().empty());
    install({});
    message.clear();
    try {
        use_packaged_lexicons({"en-GB"});
    } catch (const LookupError& error) {
        message = error.what();
    }
    CHECK(message == "no installed package provides a lexicon for ['en-GB']; installed: nothing");
}

void the_language_filter_enables_only_the_named_codes() {
    const fs::path root = installed_en("filter");
    tsv(root / "pt-PT" / "x.tsv", "casa\tkazɐ\n");
    clear_lexicons();
    CHECK(use_packaged_lexicons({"en-GB"}) == std::vector<std::string>{"en-GB"});
    CHECK(get_lexicon("pt-PT").empty());
    CHECK(available_lexicon_codes() == std::vector<std::string>{"en-GB"});
    CHECK(use_packaged_lexicons(std::vector<std::string>{}) == std::vector<std::string>{"en-GB"});
    CHECK(use_packaged_lexicons() == (std::vector<std::string>{"en-GB", "pt-PT"}));
    CHECK(get_lexicon("pt-PT").at("casa") == "kazɐ");
}

void provider_loads_once_and_only_for_its_own_language() {
    const fs::path root = installed_en("once");
    use_packaged_lexicons();
    CHECK(get_lexicon("pt-PT").empty());
    CHECK(get_lexicon("en-GB").size() == 2);
    // Read once: the second lookup does not see the file go, nor a new root.
    fs::remove(root / "en-GB" / "wikipron.tsv");
    setenv("ORTHOGRAPHY2IPA_PACKAGED_LEXICONS", "", 1);
    CHECK(get_lexicon("en-GB").size() == 2);
    CHECK(available_lexicon_codes() == std::vector<std::string>{"en-GB"});
}

// The provider directory's name is the entry point's name: a provider named
// for an alias serves the spec the alias resolves to.
void provider_is_named_for_its_directory() {
    const fs::path root = tmp / "named";
    tsv(root / "en" / "lexicon.tsv", "yacht\t" + YACHT + "\n");
    install({root});
    CHECK(use_packaged_lexicons() == std::vector<std::string>{"en-GB"});
    CHECK(lexicon_path("en-GB") == (root / "en" / "lexicon.tsv").string());
}

void clear_disables_the_packaged_lexicon() {
    installed_en("clear");
    use_packaged_lexicons();
    CHECK(G2P("en-GB").transcribe_word("yacht") == "ˈ" + YACHT);
    clear_lexicons();
    CHECK(available_lexicon_codes().empty());
    CHECK(transcribe_all("en-GB") == rules);
}

// lexicon.py use_packaged_lexicons calls get_lexicon.cache_clear(): a lexicon
// read empty before the call is read again after it.
void enabling_drops_a_cached_empty_lexicon() {
    const fs::path root = tmp / "cached-empty";
    tsv(root / "pt-PT" / "x.tsv", "casa\tkazɐ\n");
    install({root});
    CHECK(get_lexicon("pt-PT").empty());
    CHECK(use_packaged_lexicons({"pt-PT"}) == std::vector<std::string>{"pt-PT"});
    CHECK(get_lexicon("pt-PT") == (std::map<std::string, std::string>{{"casa", "kazɐ"}}));
}

// ─── precedence: word_exceptions > register / dir > packaged > rules ────────

void registered_lexicon_beats_packaged() {
    installed_en("registered");
    use_packaged_lexicons();
    CHECK(G2P("en-GB").transcribe_word("colonel") == COLONEL);
    register_lexicon("en-GB", tsv(tmp / "mine.tsv", "colonel\tkɝnəl\n").string());
    CHECK(G2P("en-GB").transcribe_word("colonel") == "ˈkɝnəl");
}

void lexicon_dir_beats_packaged() {
    installed_en("lexicon-dir");
    use_packaged_lexicons();
    CHECK(G2P("en-GB").transcribe_word("colonel") == COLONEL);
    tsv(tmp / "dir" / "en-GB.tsv", "colonel\tkɝnəl\n");
    set_lexicon_directory((tmp / "dir").string());
    CHECK(G2P("en-GB").transcribe_word("colonel") == "ˈkɝnəl");
}

void spec_word_exceptions_beat_packaged() {
    CHECK(get("en-GB").word_exceptions.at("the") == "ðə");
    const fs::path root = tmp / "exceptions";
    tsv(root / "en-GB" / "packaged.tsv", "colonel\t" + COLONEL + "\nthe\tθiː\n");
    install({root});
    use_packaged_lexicons();
    const G2P engine("en-GB");
    CHECK(engine.transcribe_word("colonel") == COLONEL);
    CHECK(engine.transcribe_word("the") == "ðə");
}

// lexicon.py register_lexicon calls get_lexicon.cache_clear(): every lexicon,
// not only the registered one, is read again on its next lookup.
void register_lexicon_reads_every_lexicon_again() {
    const fs::path dir = tmp / "reread";
    tsv(dir / "pt-PT.tsv", "casa\tkazɐ\n");
    set_lexicon_directory(dir.string());
    CHECK(get_lexicon("pt-PT").at("casa") == "kazɐ");
    tsv(dir / "pt-PT.tsv", "casa\tkɐzɐ\n");
    CHECK(get_lexicon("pt-PT").at("casa") == "kazɐ");
    register_lexicon("en-GB", tsv(tmp / "reread-en.tsv", "yacht\t" + YACHT + "\n").string());
    CHECK(get_lexicon("pt-PT").at("casa") == "kɐzɐ");
}

// ─── providers that cannot be used ──────────────────────────────────────────

void provider_for_an_unregistered_code_is_skipped() {
    const fs::path root = tmp / "unregistered";
    tsv(root / "en-GB-x-nowhere" / "x.tsv", "yacht\t" + YACHT + "\n");
    install({root});
    const Stderr log;
    CHECK(use_packaged_lexicons().empty());
    CHECK(log.has("lexicon provider 'en-GB-x-nowhere' names no registered spec; skipped"));
    CHECK(get_lexicon("en-GB").empty());
}

void failing_provider_falls_back_to_rules() {
    const fs::path root = tmp / "failing";
    tsv(root / "en-GB" / "a.tsv", "yacht\t" + YACHT + "\n");
    tsv(root / "en-GB" / "b.tsv", "yacht\t" + YACHT + "\n");
    install({root});
    CHECK(use_packaged_lexicons() == std::vector<std::string>{"en-GB"});
    const Stderr log;
    CHECK(get_lexicon("en-GB").empty());
    CHECK(log.has("failed for 'en-GB': 'en-GB' has 2 lexicon files, expected one: a.tsv, b.tsv"));
    CHECK(packaged_lexicon_sources().empty());
}

void provider_returning_a_missing_file_is_ignored() {
    const fs::path root = tmp / "not-a-file";
    fs::create_directories(root / "en-GB" / "gone.tsv");
    install({root});
    use_packaged_lexicons();
    const Stderr log;
    CHECK(get_lexicon("en-GB").empty());
    CHECK(log.has("returned " + (root / "en-GB" / "gone.tsv").string() + ", which is not a file"));
}

void exact_code_provider_wins_over_an_alias() {
    const fs::path first = tmp / "alias-first", second = tmp / "exact-second";
    tsv(first / "en" / "en.tsv", "colonel\tkɝnəl\n");
    tsv(second / "en-GB" / "en-GB.tsv", "colonel\t" + COLONEL + "\n");
    install({first, second});
    const Stderr log;
    use_packaged_lexicons();
    CHECK(get_lexicon("en-GB") == (std::map<std::string, std::string>{{"colonel", COLONEL}}));
    CHECK(log.has("both serve 'en-GB'; using '" + (second / "en-GB").string() + "'"));
}

// ─── the search path ────────────────────────────────────────────────────────

// Where the reference orders two providers by distribution name, the port
// orders them by their root's place in the search path.
void the_earlier_root_wins_between_equal_providers() {
    const fs::path first = tmp / "first", second = tmp / "second";
    tsv(first / "en-GB" / "x.tsv", "colonel\tkɝnəl\n");
    tsv(second / "en-GB" / "x.tsv", "colonel\t" + COLONEL + "\n");
    install({second, first});
    const Stderr log;
    use_packaged_lexicons();
    CHECK(get_lexicon("en-GB").at("colonel") == COLONEL);
    install({first, second});
    use_packaged_lexicons();
    CHECK(get_lexicon("en-GB").at("colonel") == "kɝnəl");
}

// Two providers with different names, neither named for the exact code: the
// root decides before the name. The reference's key (name != code, dist, name)
// gives the first distribution's provider, pt-pt, and the name alone would
// give pt.
void the_root_decides_before_the_name() {
    const fs::path first = tmp / "r0", second = tmp / "r1";
    tsv(first / "pt-pt" / "x.tsv", "casa\tkazɐ\n");
    tsv(second / "pt" / "x.tsv", "casa\tkɐzɐ\n");
    install({first, second});
    const Stderr log;
    CHECK(use_packaged_lexicons({"pt-PT"}) == std::vector<std::string>{"pt-PT"});
    CHECK(get_lexicon("pt-PT") == (std::map<std::string, std::string>{{"casa", "kazɐ"}}));
    CHECK(log.has("both serve 'pt-PT'; using '" + (first / "pt-pt").string() + "'"));
}

void a_directory_without_a_tsv_is_no_provider() {
    const fs::path root = tmp / "no-tsv";
    tsv(root / "en-GB" / "wikipron.meta.json", "{}\n");
    install({root});
    CHECK(packaged_lexicon_sources().empty());
    CHECK(use_packaged_lexicons().empty());
}

void the_default_root_is_beside_the_spec_data_directory() {
    const fs::path share = tmp / "prefix" / "share";
    tsv(share / "o2i-lexicons" / "en-GB" / "x.tsv", "yacht\t" + YACHT + "\n");
    const char* saved = std::getenv("ORTHOGRAPHY2IPA_DATA_DIR");
    const std::string saved_value = saved ? saved : "";
    setenv("ORTHOGRAPHY2IPA_DATA_DIR", (share / "orthography2ipa").c_str(), 1);
    unsetenv("ORTHOGRAPHY2IPA_PACKAGED_LEXICONS");
    clear_lexicons();
    CHECK(packaged_lexicon_sources() == (std::map<std::string, std::string>{{"en-GB", (share / "o2i-lexicons" / "en-GB" / "x.tsv").string()}}));
    if (saved) setenv("ORTHOGRAPHY2IPA_DATA_DIR", saved_value.c_str(), 1);
    else unsetenv("ORTHOGRAPHY2IPA_DATA_DIR");
    install({});
    CHECK(packaged_lexicon_sources().empty());   // set and empty: no root at all
}

void a_root_may_start_with_a_tilde() {
    const fs::path home = tmp / "home";
    tsv(home / "lexicons" / "en-GB" / "x.tsv", "yacht\t" + YACHT + "\n");
    const std::string saved = std::getenv("HOME") ? std::getenv("HOME") : "";
    setenv("HOME", home.c_str(), 1);
    setenv("ORTHOGRAPHY2IPA_PACKAGED_LEXICONS", "::~/lexicons", 1);
    clear_lexicons();
    CHECK(packaged_lexicon_sources() == (std::map<std::string, std::string>{{"en-GB", (home / "lexicons" / "en-GB" / "x.tsv").string()}}));
    setenv("HOME", saved.c_str(), 1);
}

} // namespace

int main() {
    std::string pattern = (fs::temp_directory_path() / "o2i-packaged-XXXXXX").string();
    if (mkdtemp(pattern.data()) == nullptr) return 2;
    tmp = pattern;
    install({});
    rules = transcribe_all("en-GB");
    for (const auto test : {installed_provider_changes_no_output, installed_provider_is_discoverable,
                            enabled_lexicon_is_used, enable_by_code_and_by_alias,
                            enabling_a_code_with_no_provider_raises,
                            the_language_filter_enables_only_the_named_codes,
                            provider_loads_once_and_only_for_its_own_language, provider_is_named_for_its_directory,
                            clear_disables_the_packaged_lexicon, registered_lexicon_beats_packaged,
                            lexicon_dir_beats_packaged, spec_word_exceptions_beat_packaged,
                            register_lexicon_reads_every_lexicon_again,
                            provider_for_an_unregistered_code_is_skipped, failing_provider_falls_back_to_rules,
                            provider_returning_a_missing_file_is_ignored, exact_code_provider_wins_over_an_alias,
                            enabling_drops_a_cached_empty_lexicon,
                            the_earlier_root_wins_between_equal_providers, the_root_decides_before_the_name,
                            a_directory_without_a_tsv_is_no_provider,
                            the_default_root_is_beside_the_spec_data_directory, a_root_may_start_with_a_tilde}) {
        test();
        clear_lexicons();
    }
    fs::remove_all(tmp);
    std::cout << (failures ? "FAILED: " + std::to_string(failures) + " checks\n" : "all packaged lexicon checks pass\n");
    return failures ? 1 : 0;
}
