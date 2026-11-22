// The engine changes of orthography2ipa 7.124.45a2 over 7.123.12a2, each with a
// case of the reference's own tests: the inventory cuts clusters and syllables
// into segments and joins a bare tilde, ː or ʲ to the segment before it
// (tests/test_inventory.py), the shadda gate reads the spec's own shadda
// grapheme (tests/test_shadda_gemination_gate.py), and the written-syllable
// cuts of the stress mark (tests/test_dutch_w_diphthongs.py,
// tests/test_precomposed_vowel_stress.py).
#include "orthography2ipa/inventory.hpp"
#include "orthography2ipa/orthography2ipa.hpp"
#include "unicode_util.hpp"

#include <iostream>
#include <set>
#include <string>
#include <vector>

using namespace orthography2ipa;

namespace {

int failures = 0;

#define CHECK(condition)                                                          \
    do {                                                                          \
        if (!(condition)) {                                                       \
            std::cout << "FAIL " << __LINE__ << ": " #condition "\n";             \
            ++failures;                                                           \
        }                                                                         \
    } while (0)

std::string nfc(const std::string& text) {
    return uni::to_utf8(uni::nfc(uni::to_utf32(text)));
}

bool contains(const std::set<std::string>& inventory, const std::string& token) {
    return inventory.count(token) != 0;
}

using Tokens = std::vector<std::string>;

}  // namespace

int main() {
    // The coda tilde joins the vowels and glides that can carry it, and the
    // offglide j̃ stands alone.
    const auto pt = inventory::phoneme_inventory(get("pt-PT"));
    for (const char* token : {"ɐ̃", "ẽ", "ĩ", "õ", "ũ", "j̃", "w̃"}) CHECK(contains(pt, nfc(token)));
    for (const char* token : {"̃", "̃j̃", "̃w̃", "ɐ̃j̃", "ɐ̃w̃"})
        CHECK(!contains(pt, token));
    CHECK((inventory::tokenize("ˈɔmɐ̃j̃", get("pt-PT")) == Tokens{"ˈ", "ɔ", "m", "ɐ̃", "j̃"}));

    // A cluster or a syllable a slot emits is cut into its segments.
    struct Cut { const char* code; const char* emission; Tokens segments; };
    const std::vector<Cut> cuts = {
        {"pt-PT", "ks", {"k", "s"}},
        {"pt-PT", "ɡz", {"ɡ", "z"}},
        {"pt-PT", "kw", {"k", "w"}},
        {"pt-PT", "ɡw", {"ɡ", "w"}},
        {"pt-PT", "ku", {"k", "u"}},
        {"pt-PT-x-alentejo", "me", {"m", "e"}},
        {"pt-PT-x-madeira", "õns", {"õ", "n", "s"}},
        {"pt-PT-x-madeira", "ɐ̃ns", {"ɐ̃", "n", "s"}},
    };
    for (const auto& cut : cuts) {
        const auto& spec = get(cut.code);
        CHECK(contains(inventory::emission_inventory(spec), cut.emission));
        const auto tokens = inventory::phoneme_inventory(spec);
        CHECK(!contains(tokens, cut.emission));
        for (const auto& segment : cut.segments) CHECK(contains(tokens, segment));
        CHECK(inventory::tokenize(cut.emission, spec) == cut.segments);
    }

    // An affricate without the tie stays one token.
    for (const auto& [code, segment] : std::vector<std::pair<const char*, const char*>>{
             {"ar-SA-x-najd", "ts"}, {"ar-SA-x-najd", "dz"}, {"en-GB", "tʃ"}, {"en-GB", "dʒ"}}) {
        const auto& spec = get(code);
        CHECK(contains(inventory::phoneme_inventory(spec), segment));
        CHECK((inventory::tokenize(std::string(segment) + "a", spec) == Tokens{segment, "a"}));
    }

    // A spec that declares the shadda geminates it, whatever its script label.
    for (const char* code : {"acq", "aao", "ssh", "ajt", "jrb", "yud"}) {
        const G2P engine(code);
        for (const auto& [word, geminate] : std::vector<std::pair<const char*, const char*>>{
                 {"سِتَّة", "tt"}, {"قُوَّة", "uww"}, {"كَتَّبَ", "tt"}}) {
            const std::string out = engine.transcribe(word);
            CHECK(out.find(geminate) != std::string::npos);
            CHECK(out.find("ː") == std::string::npos);
        }
    }

    // The mark after a prefix counts the vowel after ⟨ouw⟩ (Schultink 1976: 475).
    const G2P nl("nl");
    CHECK(nl.transcribe("getrouwe") == "χəˈtrɑuə");
    CHECK(nl.transcribe("beschermvrouwe") == "bəˈsχɛɹmvrɑuə");
    CHECK(nl.transcribe("bewindsvrouwe") == "bəˈʋɪntsvrɑuə");

    // A dropped vowel letter keeps its syllable for the mark; the text before
    // the mark is the WikiPron transcription of the syllables before the last.
    const G2P tr("tr");
    for (const auto& [word, before] : std::vector<std::pair<const char*, const char*>>{
             {"imkân", "im"}, {"derhâl", "deɾ"}, {"kalbî", "kal"}, {"mahkûm", "mah"},
             {"cüretkâr", "dʒyɾet"}}) {
        const std::string out = tr.transcribe(word);
        const auto at = out.find("ˈ");
        CHECK(at != std::string::npos && out.substr(0, at) == before);
    }
    for (const auto& [word, after] : std::vector<std::pair<const char*, const char*>>{
             {"kâtip", "tip"}, {"kâse", "se"}}) {
        const std::string out = tr.transcribe(word);
        const auto at = out.find("ˈ");
        CHECK(at != std::string::npos && out.substr(at + std::string("ˈ").size()) == after);
    }

    if (failures == 0) std::cout << "reference sync: ok\n";
    return failures == 0 ? 0 : 1;
}
