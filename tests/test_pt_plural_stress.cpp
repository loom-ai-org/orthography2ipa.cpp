// A plural keeps the stress of its singular, whichever syllabifier runs. The cases
// of the reference's tests/test_pt_plural_stress.py.
//
// The expected place of the mark is the first sound of the stressed syllable. The
// sources are in that file: Infopédia rows, Acordo Ortográfico 1990 Base IX, the
// written accent of Catalan, and Schultink (1976: 475) for Dutch.
//
// The Python silabificador divides dis-tri-bu-i-do-ras, one syllable more than the
// transcription has, and the bundled splitter gives dis-tri-bui-do-ras. Recorded
// holds the divisions of silabificador 2.1.1a2, so each cell runs both paths.
#include "orthography2ipa/orthography2ipa.hpp"
#include "orthography2ipa/stress.hpp"

#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

using namespace orthography2ipa;

namespace {

int failures = 0;

using Syllables = std::vector<std::string>;

// Refuses every word, so the engine falls to the bundled splitter.
class Refuses : public SyllabifierPlugin {
public:
    std::vector<std::string> language_codes() const override { return {}; }
    Syllables syllabify(const std::string&, const std::string&) const override {
        throw std::invalid_argument("cannot split");
    }
};

class Recorded : public SyllabifierPlugin {
public:
    std::vector<std::string> language_codes() const override { return {}; }
    Syllables syllabify(const std::string& word, const std::string&) const override {
        static const std::map<std::string, Syllables> divisions = {
            {"distribuidoras", {"dis", "tri", "bu", "i", "do", "ras"}},
            {"distribuidores", {"dis", "tri", "bu", "i", "do", "res"}},
            {"construidoras", {"cons", "tru", "i", "do", "ras"}},
            {"atribuidores", {"a", "tri", "bu", "i", "do", "res"}},
            {"bueiros", {"bu", "ei", "ros"}},
            {"osteointegradas", {"os", "te", "o", "in", "te", "gra", "das"}},
            {"teatro", {"te", "a", "tro"}},
            {"diabo", {"di", "a", "bo"}},
            {"havaiano", {"ha", "vai", "a", "no"}},
            {"coiote", {"coi", "o", "te"}},
            {"jersey", {"jer", "sey"}},
        };
        return divisions.at(word);
    }
};

std::string reading(const std::string& word, const std::string& lang, const Pipeline& pipeline) {
    return G2P(lang, {}, "", false, true, true, true, nullptr, nullptr, pipeline).transcribe_word(word);
}

void check_onset(const std::string& word, const std::string& lang, const Pipeline& pipeline,
                 const std::string& onset) {
    const std::string got = reading(word, lang, pipeline);
    const std::size_t mark = got.find("ˈ");
    if (mark == std::string::npos || got.compare(mark + std::string("ˈ").size(), onset.size(), onset) != 0) {
        std::cout << "FAIL " << lang << " " << word << " gave " << got << ", stressed onset " << onset << "\n";
        ++failures;
    }
}

struct Lost {
    std::string ipa;
    Syllables syllables;
    Syllables ipa_syllables;
    int stress_index;
    bool lost_after;
};

} // namespace

int main() {
    const std::vector<Pipeline> paths = {Pipeline{std::nullopt, std::make_shared<Refuses>()},
                                         Pipeline{std::nullopt, std::make_shared<Recorded>()}};

    for (const Pipeline& pipeline : paths) {
        for (const std::string lang : {"pt-BR", "pt-PT"})
            for (const auto& [word, onset] : std::vector<std::pair<std::string, std::string>>{
                     {"distribuidoras", "d"},
                     {"distribuidores", "d"},
                     {"construidoras", "d"},
                     {"atribuidores", "d"},
                     {"bueiros", "bw"},
                     {"osteointegradas", "ɡ"},
                 })
                check_onset(word, lang, pipeline, onset);

        // pt-PT-x-alentejo drops the final vowel, so the loss stands after the stress.
        for (const auto& [word, onset] : std::vector<std::pair<std::string, std::string>>{
                 {"teatro", "a"}, {"diabo", "a"}, {"havaiano", "j"}, {"coiote", "j"}})
            check_onset(word, "pt-PT-x-alentejo", pipeline, onset);

        // The reference holds this value in a strict xfail: ⟨y⟩ reads as a nucleus,
        // not as the glide of jérsei [ˈʒɛrsɐj].
        if (reading("jersey", "pt-PT", pipeline) == "ˈʒɛɾsej") {
            std::cout << "FAIL jersey reads as jérsei, and the reference does not\n";
            ++failures;
        }
    }

    check_onset("lingüístics", "ca", {}, "ʒw");
    check_onset("aqüífers", "ca", {}, "kw");
    check_onset("vertrouwen", "nl-BE", {}, "tr");
    check_onset("betrouwen", "nl-BE", {}, "tr");

    const std::vector<Lost> rows = {
        // The site u|e stands at the stress of bu-ei-ros.
        {"bwejɾus", {"bu", "ei", "ros"}, {"bwej", "ɾus"}, 1, false},
        // The site a|in stands after the stress of ca-ba-in.
        {"kabajn", {"ca", "ba", "in"}, {"ka", "bajn"}, 1, true},
        // Two syllables are gone and one site can explain only one of them.
        {"abks", {"a", "ba", "i", "kes"}, {"abks"}, 3, true},
        // bu|i merged and do|u did not, so the mark on do counts from the end.
        {"bujdoutas", {"bu", "i", "do", "u", "tas"}, {"buj", "do", "u", "tas"}, 2, false},
        // The final vowel of te-a-tro is gone and the hiatus e|a stays.
        {"tɨatɾ", {"te", "a", "tro"}, {"tɨ", "atɾ"}, 1, true},
        // The hiatus u|e of re-du-ei-xin stays: [ə] opens a syllable.
        {"rəðuəʃjn", {"re", "du", "ei", "xin"}, {"rə", "ðu", "əʃjn"}, 2, true},
        // [j] opens the syllable of ⟨a⟩, so vai|a did not merge and the final vowel
        // of ha-vai-a-no is what is gone.
        {"ɐvajan", {"ha", "vai", "a", "no"}, {"ɐ", "va", "jan"}, 2, true},
        // Two sites merged and one syllable is missing: the count does not add up.
        {"bwejɾutus", {"bu", "e", "i", "ros"}, {"bwej", "ɾu", "tus"}, 3, true},
        // No site at all: ver-trou-wen.
        {"vərtrɑuən", {"ver", "trou", "wen"}, {"vər", "trɑuən"}, 1, true},
        // No syllable stands at the site u|is, so it merged and the count adds up. The
        // values are the reference's. The engine calls this only with a stress index
        // inside the transcription, as in the second row.
        {"bujs", {"bu", "is"}, {"bujs"}, 1, false},
        {"bujs", {"bu", "is"}, {"bujs"}, 0, true},
    };
    for (const Lost& row : rows)
        if (stress::lost_after_stress(row.ipa, row.syllables, row.ipa_syllables, row.stress_index) != row.lost_after) {
            std::cout << "FAIL lost_after_stress " << row.ipa << "\n";
            ++failures;
        }

    if (failures != 0) {
        std::cout << failures << " failures\n";
        return 1;
    }
    std::cout << "plural stress: all passed\n";
    return 0;
}
