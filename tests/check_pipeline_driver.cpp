// Reads one tab-separated command per line and writes one result line for
// tests/pipeline_parity.py: "ok<TAB>value", or the exception type and message.
//   reset                        forget every lexicon and discovered syllabifier
//   register<TAB>code<TAB>path   register_lexicon
//   discovered<TAB>code<TAB>mode a discovered syllabifier for code
//   engine<TAB>lang<TAB>lexicon<TAB>mode
//                                the engine the commands below use. lexicon is -
//                                (none), {} (empty), or a file for load_lexicon;
//                                mode is - (none) or a syllabifier mode
//   fields<TAB>lang<TAB>expand<TAB>mark<TAB>rescore<TAB>sandhi<TAB>allow<TAB>old
//                                the engine the commands below use, built from
//                                the Pipeline fields, allow_undeclared_phonemes
//                                and one old argument; the modes are the ones
//                                pipeline_parity.py names. When the build
//                                throws, a later command answers "NoEngine".
//   endings<TAB>lang<TAB>expose  the engine the commands below use, with
//                                expose_ambiguous_endings on (1) or off (0)
//   word, text, detailed, candidates, confidence, lattice, path, pre<TAB>input
//   load<TAB>source              load_lexicon
//   packaged<TAB>code            packaged_lexicon
//   patched<TAB>code             packaged_lexicon with the patches
//   emission, phonemes, atoms<TAB>code<TAB>lexicon
//   tokens<TAB>code<TAB>lexicon<TAB>ipa
//   stress<TAB>word<TAB>lang<TAB>mode   detect_stress on the spec of lang
//   syllables<TAB>word<TAB>lang<TAB>mode   the syllabifier's own split
//
// A syllabifier mode: halves, whole, chars, empty, bad, raise, or silabificador,
// which builds SilabificadorSyllabifier and throws without silabificador.cpp.
#include "orthography2ipa/inventory.hpp"
#include "orthography2ipa/markup.hpp"
#include "orthography2ipa/orthography2ipa.hpp"
#include "orthography2ipa/rescorer.hpp"
#include "orthography2ipa/sentence.hpp"
#include "orthography2ipa/stress.hpp"
#include "orthography2ipa/syllabifiers.hpp"
#include "plugin_registry.hpp"
#include "unicode_util.hpp"

#include <algorithm>
#include <cstdio>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace orthography2ipa;

namespace {

using Lexicon = std::map<std::string, std::string>;

class Mode : public SyllabifierPlugin {
public:
    Mode(std::string mode, std::string code) : mode_(std::move(mode)), code_(std::move(code)) {}
    std::vector<std::string> language_codes() const override { return {code_}; }
    std::vector<std::string> syllabify(const std::string& word, const std::string&) const override {
        const std::u32string units = uni::to_utf32(word);
        if (mode_ == "whole") return {word};
        if (mode_ == "empty") return {};
        if (mode_ == "bad") return {"x"};
        if (mode_ == "raise") throw std::invalid_argument("cannot split");
        if (mode_ == "chars") {
            std::vector<std::string> out;
            for (const char32_t unit : units) out.push_back(uni::to_utf8(std::u32string(1, unit)));
            return out;
        }
        const std::size_t half = units.size() / 2;
        if (half == 0) return {word};
        return {uni::to_utf8(units.substr(0, half)), uni::to_utf8(units.substr(half))};
    }

private:
    std::string mode_, code_;
};

/// /a/ to [ɐ], a symbol pt-PT declares.
class BacksA : public rescorer::LatticeRescorer {
public:
    std::vector<Candidate> rescore(const rescorer::SegmentSlot& slot,
                                             const rescorer::RescoreContext&) const override {
        if (!slot.candidates.empty() && slot.candidates[0].ipa == "a") return {Candidate{"ɐ", 0.0}};
        return slot.candidates;
    }
};

/// A bar after the last word, so a sentence shows where sandhi ran.
class Bar : public SandhiPlugin {
public:
    std::vector<std::string> language_codes() const override { return {}; }
    std::vector<std::string> apply(const std::vector<std::string>& words, const std::vector<std::string>&,
                                   const std::vector<bool>&, const std::string&) const override {
        std::vector<std::string> out = words;
        if (!out.empty()) out.back() += "|";
        return out;
    }
};

/// Keeps the first word only, so it rewrites the sentence and not its sandhi.
class Merge : public SandhiPlugin {
public:
    std::vector<std::string> language_codes() const override { return {}; }
    std::vector<std::string> apply(const std::vector<std::string>& words, const std::vector<std::string>&,
                                   const std::vector<bool>&, const std::string&) const override {
        return words.empty() ? words : std::vector<std::string>{words.front()};
    }
};

class Keeps : public sentence::SentenceRescorer {
public:
    std::string rescore(const sentence::WordSlot& word, const sentence::SentenceRescoreContext&) const override {
        return word.ipa;
    }
};

const BacksA backs_a, backs_a_again;
const Keeps keeps;

std::string replaced(std::string text, const std::string& from, const std::string& to) {
    for (std::size_t at = text.find(from); at != std::string::npos; at = text.find(from, at + to.size()))
        text.replace(at, from.size(), to);
    return text;
}

std::function<std::string(const std::string&)> expand_of(const std::string& mode) {
    if (mode == "digits") return [](const std::string& text) { return replaced(text, "101", "cento e um"); };
    if (mode == "words") return [](const std::string& text) { return replaced(text, "gato", "o gato bom"); };
    return nullptr;
}

std::function<std::string(const std::string&)> mark_of(const std::string& mode) {
    static const std::string acute = "\xcc\x81";
    if (mode == "acute")
        return [](const std::string& text) { return replaced(replaced(text, "a", "a" + acute), "о", "о" + acute); };
    if (mode == "change") return [](const std::string& text) { return replaced(text, "o", "0"); };
    if (mode == "short")
        return [](const std::string& text) {
            const std::u32string units = uni::to_utf32(text);
            return units.empty() ? text : uni::to_utf8(units.substr(0, units.size() - 1));
        };
    if (mode == "strip") return [](const std::string& text) { return replaced(text, acute, ""); };
    return nullptr;
}

std::unique_ptr<G2P> field_engine(const std::vector<std::string>& field) {
    const std::string &lang = field[1], &rescore = field[4], &old = field[7];
    rescorer::RescorerArg fields_rescore;
    if (rescore == "backs") fields_rescore = rescorer::RescorerArg(&backs_a);
    if (rescore == "two") fields_rescore = rescorer::RescorerArg(std::vector<const rescorer::LatticeRescorer*>{&backs_a, &backs_a_again});
    std::map<std::string, std::vector<std::string>> named;
    if (old == "named-rescore") named["rescore"] = {"nothing"};
    if (old == "named-sandhi") named["sandhi"] = {"nothing"};
    if (old == "named-normalize") named["normalize"] = {"nothing"};
    return std::make_unique<G2P>(
        lang, named, "", false, true, true, true,
        old == "sentence_rescorer"         ? sentence::SentenceRescorerArg(&keeps)
        : old == "sentence_rescorer-empty" ? sentence::SentenceRescorerArg(sentence::SentenceRescorers{})
                                           : sentence::SentenceRescorerArg(),
        old == "rescorer"         ? rescorer::RescorerArg(&backs_a)
        : old == "rescorer-empty" ? rescorer::RescorerArg(std::vector<const rescorer::LatticeRescorer*>{})
                                  : rescorer::RescorerArg(),
        Pipeline{std::nullopt, nullptr, expand_of(field[2]), mark_of(field[3]), fields_rescore,
                 field[5] == "bar"     ? std::shared_ptr<const SandhiPlugin>(std::make_shared<const Bar>())
                 : field[5] == "merge" ? std::make_shared<const Merge>()
                                       : nullptr},
        field[6] == "1");
}

std::shared_ptr<const SyllabifierPlugin> plugin(const std::string& mode) {
    if (mode == "-") return nullptr;
    if (mode == "silabificador") return std::make_shared<const syllabifiers::SilabificadorSyllabifier>();
    return std::make_shared<const Mode>(mode, "");
}

std::optional<Lexicon> lexicon_of(const std::string& field) {
    if (field == "-") return std::nullopt;
    if (field == "{}") return Lexicon{};
    return load_lexicon(field);
}

std::string number(double value) {
    char text[64];
    std::snprintf(text, sizeof text, "%.12g", value);
    return text;
}

std::string joined(const std::vector<std::string>& values) {
    std::string out;
    for (std::size_t i = 0; i < values.size(); ++i) out += (i ? "|" : "") + values[i];
    return out;
}

std::string joined(const std::set<std::string>& values) {
    return joined(std::vector<std::string>(values.begin(), values.end()));
}

std::unique_ptr<G2P> current;

/// A command on the engine that a failed "fields" left unset.
struct NoEngine {};

std::string run(const std::vector<std::string>& field) {
    const std::string& command = field[0];
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    if (command == "reset") {
        clear_lexicons();
        exchange_syllabifiers({});
        return "";
    }
    if (command == "register") {
        register_lexicon(field[1], field[2]);
        return "";
    }
#pragma GCC diagnostic pop
    if (command == "discovered") {
        exchange_syllabifiers({{"fixed", std::make_shared<Mode>(field[2], field[1])}});
        return "";
    }
    if (command == "fields") {
        current.reset();
        current = field_engine(field);
        return "";
    }
    if (command == "endings") {
        current.reset();
        current = std::make_unique<G2P>(field[1], std::map<std::string, std::vector<std::string>>{}, "", false,
                                        true, true, true, nullptr, nullptr, Pipeline{}, false, field[2] == "1");
        return "";
    }
    if (command != "engine" && command != "load" && command != "packaged" && command != "patched" &&
        command != "emission" && command != "phonemes" && command != "atoms" && command != "tokens" &&
        command != "stress" && command != "syllables" && command != "reset" && command != "register" && command != "discovered" &&
        current == nullptr)
        throw NoEngine();
    if (command == "engine") {
        current = std::make_unique<G2P>(
            field[1], std::map<std::string, std::vector<std::string>>{}, "", false, true, true, true,
            nullptr, nullptr, Pipeline{lexicon_of(field[2]), plugin(field[3])});
        return "";
    }
    if (command == "word") return current->transcribe_word(field[1]);
    if (command == "text") return current->transcribe(field[1]);
    if (command == "detailed") {
        const auto result = current->transcribe_detailed(field[1]);
        std::string out = result.ipa;
        for (const auto& word : result.words) out += "|" + word.word + "=" + word.ipa + "=" + number(word.confidence);
        return out;
    }
    if (command == "features") {
        std::vector<std::string> out;
        for (const auto& word : current->features(field[1]))
            out.push_back(word.word + "=" + number(word.confidence) + "=" + std::to_string(word.graphemes.size()));
        return joined(out);
    }
    if (command == "sentence") {
        std::vector<std::string> out;
        for (const auto& word : current->sentence_lattice(field[1]).words) out.push_back(word.surface);
        return joined(out);
    }
    if (command == "candidates") return joined(current->word_candidates(field[1], 5));
    if (command == "confidence") return number(current->word_confidence(field[1]));
    if (command == "lattice") {
        std::vector<std::string> out;
        for (const auto& slot : current->ipa_lattice(field[1]))
            out.push_back(slot.top().ipa + ":" + std::to_string(slot.candidates.size()));
        return joined(out);
    }
    if (command == "path") {
        const IPAPath path = current->engine_path(field[1], 1);
        return path.ipa + ":" + number(path.score);
    }
    if (command == "pre") {
        std::string out;
        for (const auto& slot : current->lattice_pre_rescore(field[1], 8)) {
            out += slot.grapheme + "=";
            for (const auto& candidate : slot.candidates) out += candidate.ipa + ":" + number(candidate.score) + ",";
            out += "|";
        }
        return out;
    }
    if (command == "load" || command == "packaged" || command == "patched") {
        const Lexicon entries = command == "load" ? load_lexicon(field[1])
                                                  : packaged_lexicon(field[1], command == "patched");
        std::string out;
        for (const auto& [word, ipa] : entries) out += word + "=" + ipa + ",";
        return out;
    }
    if (command == "emission" || command == "phonemes" || command == "atoms" || command == "tokens") {
        const LanguageSpec& spec = get(field[1]);
        const std::optional<Lexicon> lexicon = lexicon_of(field[2]);
        const Lexicon* mapping = lexicon.has_value() ? &*lexicon : nullptr;
        if (command == "emission") return joined(inventory::emission_inventory(spec, mapping));
        if (command == "phonemes") return joined(inventory::phoneme_inventory(spec, mapping));
        if (command == "tokens") return joined(inventory::tokenize(field[3], spec, mapping));
        auto atoms = inventory::phoneme_atoms(spec, mapping);
        return joined(std::set<std::string>(atoms.begin(), atoms.end()));
    }
    if (command == "syllables") return joined(plugin(field[3])->syllabify(field[1], field[2]));
    if (command == "stress") {
        const auto syllabifier = plugin(field[3]);
        return std::to_string(stress::detect_stress(field[1], get(field[2]), nullptr, field[2],
                                                    syllabifier.get()));
    }
    throw std::invalid_argument("unknown command " + command);
}

}  // namespace

int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        std::vector<std::string> field;
        std::istringstream parts(line);
        for (std::string part; std::getline(parts, part, '\t');) field.push_back(part);
        try {
            const std::string value = run(field);
            std::cout << "ok\t" << value << '\n';
        } catch (const FileNotFoundError& error) {
            std::cout << "FileNotFoundError\t" << error.what() << '\n';
        } catch (const LookupError& error) {
            std::cout << "LookupError\t" << error.what() << '\n';
        } catch (const markup::MarkupError& error) {
            std::string message = error.what();
            std::replace(message.begin(), message.end(), '\n', '|');
            std::cout << "MarkupError\t" << message << '\n';
        } catch (const MissingPlugin& error) {
            std::string message = error.what();
            std::replace(message.begin(), message.end(), '\n', '|');
            std::cout << "MissingPlugin\t" << message << '\n';
        } catch (const std::invalid_argument& error) {
            std::cout << "ValueError\t" << error.what() << '\n';
        } catch (const NoEngine&) {
            std::cout << "NoEngine\t" << '\n';
        } catch (const std::exception& error) {
            // No reference row has this type, so the line differs and the
            // run goes on.
            std::cout << "UnexpectedError\t" << error.what() << '\n';
        }
    }
}
