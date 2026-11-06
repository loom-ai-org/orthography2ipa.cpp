// A vowel that an `append` rule inserts is matched in its own syllable, and that
// syllable takes no stress. The cases of the reference's
// tests/test_allophony_inserted_vowel_stress.py.
//
// Three probe rules stand before the pt-BR rules and rewrite an inserted [i] by
// its stress position, so each case reads that position from the output. The
// engine reads its spec from a data directory, so the probed spec is a copy of
// that directory with the rules in pt-BR.json.
#include "orthography2ipa/orthography2ipa.hpp"
#include "orthography2ipa/schema.hpp"
#include "unicode_util.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <unistd.h>

using namespace orthography2ipa;
namespace fs = std::filesystem;

namespace {

int failures = 0;

#define CHECK_IPA(actual, expected)                                                    \
    do {                                                                               \
        const std::string got = (actual);                                              \
        if (got != (expected)) {                                                       \
            std::cout << "FAIL " << __LINE__ << ": " #actual " gave " << got << "\n";  \
            ++failures;                                                                \
        }                                                                              \
    } while (0)

std::string rule(const std::string& id, const std::string& phoneme, const std::string& surface,
                 const std::string& stress, const std::string& after) {
    return "{\"id\": \"" + id + "\", \"phonemes\": [\"" + phoneme + "\"], \"surface\": \"" + surface +
           "\", \"stress\": \"" + stress + "\", \"preceded_by_phoneme\": [" + after + "]},";
}

// Divides a word in two halves, whatever its letters.
class Halves : public SyllabifierPlugin {
public:
    std::vector<std::string> language_codes() const override { return {""}; }
    std::vector<std::string> syllabify(const std::string& word, const std::string&) const override {
        const std::u32string units = uni::to_utf32(word);
        const std::size_t half = units.size() / 2;
        if (half == 0) return {word};
        return {uni::to_utf8(units.substr(0, half)), uni::to_utf8(units.substr(half))};
    }
};

// The data directory *dir* with *rules* before the pt-BR rules. Every other
// spec links to the shipped file.
void use_probed_spec(const fs::path& dir, const fs::path& shipped, const std::string& rules) {
    fs::create_directories(dir);
    for (const auto& entry : fs::directory_iterator(shipped))
        if (entry.path().filename() != "pt-BR.json")
            fs::create_symlink(entry.path(), dir / entry.path().filename());
    std::ifstream in(shipped / "pt-BR.json", std::ios::binary);
    std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    const std::string anchor = "\"allophone_rules\": [";
    const auto at = text.find(anchor);
    if (at == std::string::npos) {
        std::cout << "FAIL pt-BR.json has no " << anchor << "\n";
        std::exit(2);
    }
    text.insert(at + anchor.size(), rules);
    std::ofstream(dir / "pt-BR.json", std::ios::binary) << text;
    set_data_directory(dir.string());
}

}  // namespace

int main() {
    const fs::path shipped = schema::data_directory();
    char pattern[] = "/tmp/o2i-inserted-vowel-XXXXXX";
    const char* made = mkdtemp(pattern);
    if (made == nullptr) return 2;
    const fs::path tmp = made;

    {
        const G2P engine("pt-BR");
        // Collischonn (1996: 151), reported by Nascimento (2015: 62-63): "pneu [pi'new]".
        CHECK_IPA(engine.transcribe("pneu"), "piˈnew");
        // Barbosa & Albano (2004: 227): "The epenthetic vowel is always [i] or
        // [ɪ]", and a /t/ before it is the affricate (p. 228).
        CHECK_IPA(engine.transcribe("gnomo").substr(0, std::string("ɡiˈn").size()), "ɡiˈn");
        CHECK_IPA(engine.transcribe("tmese").substr(0, std::string("t͡ʃiˈm").size()), "t͡ʃiˈm");
        // Barbosa & Albano (2004: 229): "(em) cima sĩmɐ".
        CHECK_IPA(engine.transcribe("cima"), "ˈsĩmɐ");
    }

    const std::string after = "\"p\", \"b\", \"t\", \"d\", \"k\", \"ɡ\", \"f\", \"v\"";
    use_probed_spec(tmp / "by-stress", shipped,
                    rule("PROBE_STRESSED", "i", "ɪ", "stressed", after) +
                    rule("PROBE_PRETONIC", "i", "ɨ", "pretonic", after) +
                    rule("PROBE_POSTTONIC", "i", "ə", "posttonic", after));
    {
        // The division ada|ptar puts ⟨p⟩ in the syllable after the vowel before
        // it. The inserted vowel stands after the syllable of that vowel.
        const G2P halves("pt-BR", {}, "", false, true, true, true, nullptr, nullptr,
                         Pipeline{std::nullopt, std::make_shared<const Halves>()});
        CHECK_IPA(halves.transcribe("adaptar"), "adapɨˈta");
        const G2P probed("pt-BR");
        CHECK_IPA(probed.transcribe("pneu"), "pɨˈnew");
        CHECK_IPA(probed.transcribe("advogado"), "ad͡ʒɨvoˈɡadu");
        CHECK_IPA(probed.transcribe("pacto"), "ˈpakətu");
        // Collischonn (2004: 67) counts egípcios among the post-tonic contexts:
        // the [i] stands after the stressed syllable, not after the first one.
        CHECK_IPA(probed.transcribe("egípcio").substr(0, std::string("eˈʒipə").size()), "eˈʒipə");
    }

    // pt-BR reads ⟨qu⟩ as [ku] only in a positional value (word-final), so that
    // [u] is written, not inserted, and keeps the stress of its slot.
    use_probed_spec(tmp / "positional", shipped, rule("PROBE_STRESSED_U", "u", "ʉ", "stressed", "\"k\""));
    CHECK_IPA(G2P("pt-BR").transcribe("taqu"), "taˈkʉ");

    set_data_directory(shipped.string());
    std::error_code ignored;
    fs::remove_all(tmp, ignored);
    if (failures == 0) std::cout << "inserted vowel: ok\n";
    return failures == 0 ? 0 : 1;
}
