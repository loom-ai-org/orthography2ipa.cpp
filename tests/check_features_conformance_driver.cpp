// Standalone validation driver for the features.py and conformance.py ports
// (src/features.cpp, src/conformance.cpp). Built against the library the two
// modules link into; prints JSON Lines on stdout for tests/
// features_conformance_parity.py to diff against the reference.
//
//   check_features_conformance features <fixture.json>
//       rebuilds each case's WordFeatures from the reference's own slots and
//       grapheme contexts and prints the as_dict() sequence, plus the
//       GraphemeContext view it read (so a diff is attributable)
//   check_features_conformance conformance
//       runs the conformance kit over the same good / bad plugins the
//       reference's own tests use and prints verdict + message per scenario
//   check_features_conformance rescorer-runtime
//       transcribes through a declared rescore plugin of each kind (one that
//       changes nothing, invents a symbol, leaves no candidates, or answers
//       differently per call) and prints what each public surface returned
#include "orthography2ipa/conformance.hpp"
#include "orthography2ipa/features.hpp"
#include "orthography2ipa/orthography2ipa.hpp"

#include "unicode_util.hpp"

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include <cstdlib>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace {

using orthography2ipa::Candidate;
using orthography2ipa::rescorer::RescoreContext;
using orthography2ipa::rescorer::SegmentSlot;
using orthography2ipa::uni::to_utf32;
using orthography2ipa::uni::to_utf8;

std::string escape(const std::string& text) {
    std::string out;
    for (const char c : text) {
        const auto code = static_cast<unsigned char>(c);
        if (c == '"' || c == '\\') { out += '\\'; out += c; }
        else if (code == 0x08) out += "\\b";
        else if (code == 0x0C) out += "\\f";
        else if (code == 0x0A) out += "\\n";
        else if (code == 0x0D) out += "\\r";
        else if (code == 0x09) out += "\\t";
        else if (code < 0x20) {
            char buffer[8];
            std::snprintf(buffer, sizeof buffer, "\\u%04x", code);
            out += buffer;
        } else out += c;
    }
    return out;
}

std::string jstr(const std::string& text) { return "\"" + escape(text) + "\""; }

std::string jvalue(const orthography2ipa::features::FeatureValue& value) {
    return value.to_json();
}

std::string jdict(const std::vector<std::pair<std::string,
              orthography2ipa::features::FeatureValue>>& entry) {
    std::string out = "{";
    bool first = true;
    for (const auto& [key, value] : entry) {
        if (!first) out += ",";
        first = false;
        out += jstr(key) + ":" + jvalue(value);
    }
    out += "}";
    return out;
}

std::string jlist(const std::vector<std::string>& items) {
    std::string out = "[";
    for (std::size_t i = 0; i < items.size(); ++i) { if (i) out += ","; out += items[i]; }
    out += "]";
    return out;
}

double exact_double(const std::string& text) {
    return std::strtod(text.c_str(), nullptr);
}

// ─── features mode ────────────────────────────────────────────────────────

int features_mode(const std::string& fixture) {
    boost::property_tree::ptree root;
    boost::property_tree::read_json(fixture, root);

    for (const auto& [case_key, item] : root.get_child("cases")) {
        (void)case_key;
        const std::string name = item.get<std::string>("name");
        const std::string lang = item.get<std::string>("lang");
        const std::string word = item.get<std::string>("word");
        const std::string code = item.get<std::string>("code");
        const std::string script = item.get<std::string>("script");
        const std::string mode = item.get<std::string>("mode", "native");
        const double confidence = exact_double(item.get<std::string>("confidence", "1"));

        std::vector<SegmentSlot> slots;
        for (const auto& [slot_key, slot_node] : item.get_child("slots")) {
            (void)slot_key;
            SegmentSlot slot;
            slot.grapheme = slot_node.get<std::string>("grapheme");
            slot.span = {slot_node.get<std::size_t>("start"), slot_node.get<std::size_t>("end")};
            for (const auto& [cand_key, cand_node] : slot_node.get_child("candidates")) {
                (void)cand_key;
                Candidate candidate;
                candidate.ipa = cand_node.get<std::string>("ipa");
                candidate.score = exact_double(cand_node.get<std::string>("cost"));
                slot.candidates.push_back(std::move(candidate));
            }
            slots.push_back(std::move(slot));
        }

        const orthography2ipa::LanguageSpec& spec = orthography2ipa::get(lang);
        std::vector<orthography2ipa::Token> tokens;
        if (mode == "native") {
            tokens = orthography2ipa::PhonetokTokenizer(spec).grapheme_tokens(word);
        } else {
            for (const auto& [token_key, token_node] : item.get_child("tokens")) {
                (void)token_key;
                orthography2ipa::Token token;
                token.kind = orthography2ipa::TokenKind::GRAPHEME;
                token.grapheme = token_node.get<std::string>("grapheme");
                token.position = token_node.get<std::size_t>("position");
                token.length = token_node.get<std::size_t>("length");
                for (const auto& [ipa_key, ipa_node] : token_node.get_child("ipa")) {
                    (void)ipa_key;
                    token.ipa.push_back(ipa_node.get_value<std::string>());
                }
                tokens.push_back(std::move(token));
            }
        }
        // phonetok.py: flat_contexts(grapheme_tokens(word), spec.vowel_graphemes)
        // — the reference's vowel_graphemes is a frozenset, the C++ spec stores
        // the declared list. The fixture carries the set the reference actually
        // handed flat_contexts, so the axis predicates are compared against the
        // same data rather than against a differently-resolved spec.
        std::set<std::string> vowels;
        for (const auto& [vowel_key, vowel_node] : item.get_child("vowel_graphemes")) {
            (void)vowel_key;
            const auto value = vowel_node.get_value<std::string>();
            if (!value.empty()) vowels.insert(value);
        }
        auto sequence = orthography2ipa::flat_contexts(tokens, vowels);

        const auto records = orthography2ipa::features::build_word_features(
            word, slots, *sequence.graphemes, confidence, code, script);

        std::vector<std::string> dicts;
        for (const auto& entry : records.as_dicts()) dicts.push_back(jdict(entry));
        // The context view the record builder actually read, so a mismatch can
        // be pinned to the context or to features.py's own logic.
        std::vector<std::string> contexts;
        for (const auto& context : *sequence.graphemes) {
            auto neighbour = [&context](int offset) {
                const orthography2ipa::GraphemeContext* other = context.at(offset);
                return other == nullptr ? std::string("null") : jstr(other->grapheme());
            };
            std::string out = "{";
            out += "\"grapheme\":" + jstr(context.grapheme()) + ",";
            out += "\"start\":" + std::to_string(context.span().first) + ",";
            out += "\"end\":" + std::to_string(context.span().second) + ",";
            out += "\"is_vowel\":" + std::string(context.is_vowel() ? "true" : "false") + ",";
            out += "\"is_consonant\":" + std::string(context.is_consonant() ? "true" : "false") + ",";
            out += "\"is_front\":" + std::string(context.is_front() ? "true" : "false") + ",";
            out += "\"is_back\":" + std::string(context.is_back() ? "true" : "false") + ",";
            out += "\"prev\":" + neighbour(-1) + ",";
            out += "\"next\":" + neighbour(1) + ",";
            out += "\"prev2\":" + neighbour(-2) + ",";
            out += "\"next2\":" + neighbour(2);
            out += "}";
            contexts.push_back(std::move(out));
        }
        std::cout << "{\"name\":" << jstr(name)
                  << ",\"mode\":" << jstr(mode)
                  << ",\"word\":" << jstr(word)
                  << ",\"n_slots\":" << slots.size()
                  << ",\"n_contexts\":" << sequence.graphemes->size()
                  << ",\"contexts\":" << jlist(contexts)
                  << ",\"dicts\":" << jlist(dicts)
                  << "}\n" << std::flush;
    }
    return 0;
}

// ─── conformance mode ─────────────────────────────────────────────────────

/// conformance.py's own fixtures (tests/test_conformance.py), as C++ plugins.
class Rewriting : public orthography2ipa::SyllabifierPlugin {
public:
    std::vector<std::string> language_codes() const override { return {"pt-PT"}; }
    std::vector<std::string> syllabify(const std::string& word, const std::string&) const override {
        // list(word.replace("ã", "a")) — one "syllable" per character, and the
        // tilde is gone: the word downstream is not the word the caller passed.
        std::vector<std::string> out;
        for (const char32_t cp : to_utf32(word))
            out.push_back(to_utf8(std::u32string(1, cp == U'ã' ? U'a' : cp)));
        return out;
    }
};

class NoClaim : public orthography2ipa::SyllabifierPlugin {
public:
    std::vector<std::string> language_codes() const override { return {}; }
    std::vector<std::string> syllabify(const std::string& word, const std::string&) const override {
        return {word};
    }
};

class MergesHiatus : public orthography2ipa::SyllabifierPlugin {
public:
    std::vector<std::string> language_codes() const override { return {"pt-PT"}; }
    std::vector<std::string> syllabify(const std::string& word, const std::string&) const override {
        if (word == "coelho") return {"coe", "lho"};
        return {word};
    }
};

/// A syllabifier that claims only *code* and splits nothing.
class WholeWord : public orthography2ipa::SyllabifierPlugin {
public:
    explicit WholeWord(std::vector<std::string> codes, int priority = 50)
        : codes_(std::move(codes)), priority_(priority) {}
    std::vector<std::string> language_codes() const override { return codes_; }
    std::vector<std::string> syllabify(const std::string& word, const std::string&) const override {
        return {word};
    }
    int priority() const override { return priority_; }
private:
    std::vector<std::string> codes_;
    int priority_;
};

/// _Backs_a: rewrite /a/ to [ɐ] — a symbol pt-PT declares, so this is legal
/// phonology.
class BacksA : public orthography2ipa::rescorer::LatticeRescorer {
public:
    std::vector<Candidate> rescore(const SegmentSlot& slot, const RescoreContext&) const override {
        if (!slot.candidates.empty() && slot.candidates[0].ipa == "a") return {Candidate{"ɐ", 0.0}};
        return slot.candidates;
    }
};

/// _Invents: rewrite /a/ to a symbol no spec declares.
class Invents : public orthography2ipa::rescorer::LatticeRescorer {
public:
    std::vector<Candidate> rescore(const SegmentSlot& slot, const RescoreContext&) const override {
        if (!slot.candidates.empty() && slot.candidates[0].ipa == "a") return {Candidate{"Q", 0.0}};
        return slot.candidates;
    }
};

/// A secondary stress mark where the spec declares no stress at all: outside
/// the inventory by the same argument as `Q`.
class InsertsSecondary : public orthography2ipa::rescorer::LatticeRescorer {
public:
    std::vector<Candidate> rescore(const SegmentSlot& slot, const RescoreContext& context) const override {
        // The word's FIRST slot becomes a secondary stress mark, whatever the
        // tables said: a symbol the fr-FR spec does not declare (it declares no
        // stress at all).
        if (context.index == 0 && !slot.candidates.empty())
            return {Candidate{orthography2ipa::uni::to_utf8(U"ˌ"), 0.0}};
        return slot.candidates;
    }
};

/// Answers differently for the same word depending on how many words it has
/// already seen — a transcription that varies run to run.
class WordCounting : public orthography2ipa::rescorer::LatticeRescorer {
public:
    mutable std::size_t words_seen = 0;
    mutable bool at_word_start = false;

    std::vector<Candidate> rescore(const SegmentSlot& slot, const RescoreContext& context) const override {
        if (context.index == 0) {
            if (!at_word_start) ++words_seen;
            at_word_start = true;
        } else {
            at_word_start = false;
        }
        if (!slot.candidates.empty() && slot.candidates[0].ipa == "a" && words_seen % 2 == 0)
            return {Candidate{"ɐ", 0.0}};
        return slot.candidates;
    }
};

/// The whole-word RescorerPlugin counterparts (the C++ engine's own seam).
class PathRewriter : public orthography2ipa::RescorerPlugin {
public:
    PathRewriter(std::vector<std::string> codes, std::string from, std::string to)
        : codes_(std::move(codes)), from_(std::move(from)), to_(std::move(to)) {}
    std::vector<std::string> language_codes() const override { return codes_; }
    std::vector<orthography2ipa::IPAPath> rescore(
        const std::string&, const std::string&,
        const std::vector<orthography2ipa::IPAPath>& paths) const override {
        auto out = paths;
        for (auto& path : out) {
            std::string ipa;
            for (const auto& segment : orthography2ipa::segment_ipa(path.ipa))
                ipa += (segment == from_) ? to_ : segment;
            path.ipa = ipa;
            for (auto& segment : path.segments) if (segment == from_) segment = to_;
        }
        return out;
    }
private:
    std::vector<std::string> codes_;
    std::string from_;
    std::string to_;
};

/// A whole-word plugin that claims nothing: nothing can ever name it.
class NoClaimPath : public orthography2ipa::RescorerPlugin {
public:
    std::vector<std::string> language_codes() const override { return {}; }
    std::vector<orthography2ipa::IPAPath> rescore(
        const std::string&, const std::string&,
        const std::vector<orthography2ipa::IPAPath>& paths) const override {
        return paths;
    }
};

/// A path plugin that answers differently for the same word depending on how
/// many words it has already seen.
class WordCountingPaths : public orthography2ipa::RescorerPlugin {
public:
    explicit WordCountingPaths(std::vector<std::string> codes) : codes_(std::move(codes)) {}
    std::vector<std::string> language_codes() const override { return codes_; }
    std::vector<orthography2ipa::IPAPath> rescore(
        const std::string& word, const std::string&,
        const std::vector<orthography2ipa::IPAPath>& paths) const override {
        if (word != last_word) { ++calls; last_word = word; }
        auto out = paths;
        if (calls % 2 == 0) {
            for (auto& path : out) {
                std::string ipa;
                for (const auto& segment : orthography2ipa::segment_ipa(path.ipa))
                    ipa += (segment == "a") ? "ɐ" : segment;
                path.ipa = ipa;
                for (auto& segment : path.segments) if (segment == "a") segment = "ɐ";
            }
        }
        return out;
    }
private:
    std::vector<std::string> codes_;
    mutable std::size_t calls = 0;
    mutable std::string last_word;
};

/// Rewrites one symbol in every candidate of every slot, keeping the costs.
class RewritesCandidates : public orthography2ipa::rescorer::LatticeRescorer {
public:
    RewritesCandidates(std::string from, std::string to)
        : from_(std::move(from)), to_(std::move(to)) {}
    std::vector<Candidate> rescore(const SegmentSlot& slot, const RescoreContext&) const override {
        auto out = slot.candidates;
        for (auto& candidate : out) if (candidate.ipa == from_) candidate.ipa = to_;
        return out;
    }
private:
    std::string from_;
    std::string to_;
};

class KeepsSlots : public orthography2ipa::rescorer::LatticeRescorer {
public:
    std::vector<Candidate> rescore(const SegmentSlot& slot, const RescoreContext&) const override {
        return slot.candidates;
    }
};

class EmptiesEverySlot : public orthography2ipa::rescorer::LatticeRescorer {
public:
    std::vector<Candidate> rescore(const SegmentSlot&, const RescoreContext&) const override {
        return {};
    }
};

class EmptiesFirstSlot : public orthography2ipa::rescorer::LatticeRescorer {
public:
    std::vector<Candidate> rescore(const SegmentSlot& slot, const RescoreContext& context) const override {
        if (context.index == 0) return {};
        return slot.candidates;
    }
};

/// The first slot reads Q on every odd call: same input, different answers.
class FlipsFirstSlot : public orthography2ipa::rescorer::LatticeRescorer {
public:
    std::vector<Candidate> rescore(const SegmentSlot& slot, const RescoreContext& context) const override {
        if (context.index == 0 && ++calls_ % 2 == 1) return {Candidate{"Q", 0.0}};
        return slot.candidates;
    }
private:
    mutable std::size_t calls_ = 0;
};

/// arbtok's plugin shape: all its phonology sits in the lattice rescorer it
/// contributes, and the whole-word pass returns the paths it was given.
class ContributesLattice : public orthography2ipa::RescorerPlugin {
public:
    explicit ContributesLattice(std::shared_ptr<orthography2ipa::rescorer::LatticeRescorer> rescorer)
        : rescorer_(std::move(rescorer)) {}
    std::vector<std::string> language_codes() const override { return {}; }
    std::vector<orthography2ipa::IPAPath> rescore(
        const std::string&, const std::string&,
        const std::vector<orthography2ipa::IPAPath>& paths) const override {
        return paths;
    }
    std::vector<const orthography2ipa::rescorer::LatticeRescorer*> lattice_rescorers(
        const std::string&) const override {
        return {rescorer_.get()};
    }
private:
    std::shared_ptr<orthography2ipa::rescorer::LatticeRescorer> rescorer_;
};

class EmptiesPaths : public orthography2ipa::RescorerPlugin {
public:
    explicit EmptiesPaths(std::vector<std::string> codes) : codes_(std::move(codes)) {}
    std::vector<std::string> language_codes() const override { return codes_; }
    std::vector<orthography2ipa::IPAPath> rescore(
        const std::string&, const std::string&,
        const std::vector<orthography2ipa::IPAPath>&) const override {
        return {};
    }
private:
    std::vector<std::string> codes_;
};

/// Rewrites a to ɐ on every odd call and returns the paths on every even call.
class FlipsPaths : public orthography2ipa::RescorerPlugin {
public:
    explicit FlipsPaths(std::vector<std::string> codes)
        : codes_(codes), rewriter_(std::move(codes), "a", "ɐ") {}
    std::vector<std::string> language_codes() const override { return codes_; }
    std::vector<orthography2ipa::IPAPath> rescore(
        const std::string& word, const std::string& lang,
        const std::vector<orthography2ipa::IPAPath>& paths) const override {
        return ++calls_ % 2 == 1 ? rewriter_.rescore(word, lang, paths) : paths;
    }
private:
    std::vector<std::string> codes_;
    PathRewriter rewriter_;
    mutable std::size_t calls_ = 0;
};

struct Scenario {
    const char* name;
    const char* kind;      // "syllabifier" | "lattice" | "path"
    std::string lang;
    std::vector<std::string> words;
};

void report(const Scenario& scenario, const std::string& verdict, const std::string& message) {
    std::cout << "{\"scenario\":" << jstr(scenario.name)
              << ",\"kind\":" << jstr(scenario.kind)
              << ",\"lang\":" << jstr(scenario.lang)
              << ",\"words\":" << jlist([&] {
                     std::vector<std::string> out;
                     for (const auto& word : scenario.words) out.push_back(jstr(word));
                     return out;
                 }())
              << ",\"verdict\":" << jstr(verdict)
              << ",\"message\":" << jstr(message)
              << "}\n" << std::flush;
}

void run(const Scenario& scenario, const std::function<void()>& body) {
    try {
        body();
        report(scenario, "conforms", "");
    } catch (const orthography2ipa::conformance::ConformanceError& e) {
        report(scenario, "refused", e.what());
    } catch (const std::exception& e) {
        report(scenario, "engine-refused", e.what());
    }
}

int conformance_mode() {
    const auto probe = orthography2ipa::conformance::default_probe_words();
    const std::vector<std::string> one{"coelho"};
    const std::vector<std::string> casa{"casa"};
    const std::vector<std::string> mae{"mãe"};
    const std::vector<std::string> three{"casa", "kata", "mesa"};

    const Rewriting rewriting;
    const NoClaim no_claim;
    const MergesHiatus merges_hiatus;
    const WholeWord portuguese{std::vector<std::string>{"pt-PT"}};
    // Below any plugin registered for pt-PT at the neutral priority: the check
    // must still measure THIS plugin against the built-in splitter.
    const WholeWord portuguese_low{std::vector<std::string>{"pt-PT"}, 40};
    const std::vector<std::string> moved_by_whole_word{
        "naquelas", "leque", "saque", "aguardem", "albergue", "embarque", "inextinguível"};
    const WholeWord french{std::vector<std::string>{"fr-FR"}};
    const WholeWord spanish_only{std::vector<std::string>{"es-ES"}};
    const BacksA backs_a;
    const Invents invents;
    const InsertsSecondary inserts_secondary;
    const std::shared_ptr<WordCounting> word_counting = std::make_shared<WordCounting>();
    const std::shared_ptr<PathRewriter> path_a_to_ae =
        std::make_shared<PathRewriter>(std::vector<std::string>{"pt-PT"}, "a", "æ");
    const std::shared_ptr<PathRewriter> path_a_to_ash =
        std::make_shared<PathRewriter>(std::vector<std::string>{"pt-PT"}, "a", "ɐ");
    // ⟨e⟩ at this seam reads ə (the positional ɛ is a grammatical-ending
    // reading, applied after the rescorer), and fr-FR's mers/fers/chers are
    // word_exceptions — a whole-word override never reaches a plugin, so the
    // probe words are pipeline words.
    const std::shared_ptr<PathRewriter> path_secondary_fr =
        std::make_shared<PathRewriter>(std::vector<std::string>{"fr-FR"}, "ə",
                                       orthography2ipa::uni::to_utf8(U"ˌ"));
    const std::shared_ptr<WordCountingPaths> word_counting_paths =
        std::make_shared<WordCountingPaths>(std::vector<std::string>{"pt-PT"});
    const NoClaimPath no_claim_path;
    const EmptiesEverySlot empties_every_slot;
    const EmptiesPaths empties_paths{std::vector<std::string>{"pt-PT"}};
    const FlipsPaths flips_paths{std::vector<std::string>{"pt-PT"}};
    const ContributesLattice invents_in_lattice{std::make_shared<Invents>()};

    run({"syllabifier:claims nothing, lang given", "syllabifier", "pt-PT", one}, [&]{
        orthography2ipa::conformance::assert_syllabifier_conforms(no_claim, one, "pt-PT");
    });
    run({"syllabifier:claims nothing, no lang", "syllabifier", "", {}}, [&]{
        orthography2ipa::conformance::assert_syllabifier_conforms(no_claim);
    });
    run({"syllabifier:strips the accent", "syllabifier", "pt-PT", mae}, [&]{
        orthography2ipa::conformance::assert_syllabifier_conforms(rewriting, mae, "pt-PT");
    });
    run({"syllabifier:merges the hiatus", "syllabifier", "pt-PT-x-lisbon", one}, [&]{
        orthography2ipa::conformance::assert_syllabifier_conforms(merges_hiatus, one, "pt-PT-x-lisbon");
    });
    run({"syllabifier:whole word, language with no stress", "syllabifier", "fr-FR",
         std::vector<std::string>{"terre", "femme", "verre"}}, [&]{
        orthography2ipa::conformance::assert_syllabifier_conforms(french,
            std::vector<std::string>{"terre", "femme", "verre"}, "fr-FR");
    });
    run({"syllabifier:claims another language only", "syllabifier", "fr-FR",
         std::vector<std::string>{"terre", "femme", "verre"}}, [&]{
        orthography2ipa::conformance::assert_syllabifier_conforms(spanish_only,
            std::vector<std::string>{"terre", "femme", "verre"}, "fr-FR");
    });
    run({"syllabifier:default words, pt-PT", "syllabifier", "pt-PT", probe}, [&]{
        orthography2ipa::conformance::assert_syllabifier_conforms(portuguese, {}, "pt-PT");
    });
    run({"syllabifier:whole word at priority 40, pt-PT", "syllabifier", "pt-PT",
         moved_by_whole_word}, [&]{
        orthography2ipa::conformance::assert_syllabifier_conforms(portuguese_low,
            moved_by_whole_word, "pt-PT");
    });
    run({"lattice:a to ɐ (declared)", "lattice", "pt-PT", casa}, [&]{
        orthography2ipa::conformance::assert_rescorer_conforms(backs_a, "pt-PT", casa);
    });
    run({"lattice:a to Q (invented)", "lattice", "pt-PT", casa}, [&]{
        orthography2ipa::conformance::assert_rescorer_conforms(invents, "pt-PT", casa);
    });
    run({"lattice:first slot to ˌ in a spec with no stress", "lattice", "fr-FR",
         std::vector<std::string>{"terre", "femme", "verre"}}, [&]{
        orthography2ipa::conformance::assert_rescorer_conforms(inserts_secondary, "fr-FR",
            std::vector<std::string>{"terre", "femme", "verre"});
    });
    run({"lattice:answers depend on the run", "lattice", "pt-PT", three}, [&]{
        orthography2ipa::conformance::assert_rescorer_conforms(*word_counting, "pt-PT", three);
    });
    run({"lattice:empties every slot", "lattice", "pt-PT", casa}, [&]{
        orthography2ipa::conformance::assert_rescorer_conforms(empties_every_slot, "pt-PT", casa);
    });
    run({"path:a to æ (undeclared)", "path", "pt-PT", casa}, [&]{
        orthography2ipa::conformance::assert_rescorer_conforms(*path_a_to_ae, casa, "pt-PT");
    });
    run({"path:a to ɐ (declared)", "path", "pt-PT", casa}, [&]{
        orthography2ipa::conformance::assert_rescorer_conforms(*path_a_to_ash, casa, "pt-PT");
    });
    run({"path:ɛ to ˌ in a spec with no stress", "path", "fr-FR",
         std::vector<std::string>{"terre", "femme", "verre"}}, [&]{
        orthography2ipa::conformance::assert_rescorer_conforms(*path_secondary_fr,
            std::vector<std::string>{"terre", "femme", "verre"}, "fr-FR");
    });
    run({"path:answers depend on the run", "path", "pt-PT", three}, [&]{
        orthography2ipa::conformance::assert_rescorer_conforms(*word_counting_paths, three, "pt-PT");
    });
    run({"path:empties every path", "path", "pt-PT", casa}, [&]{
        orthography2ipa::conformance::assert_rescorer_conforms(empties_paths, casa, "pt-PT");
    });
    run({"path:flips on every call", "path", "pt-PT", casa}, [&]{
        orthography2ipa::conformance::assert_rescorer_conforms(flips_paths, casa, "pt-PT");
    });
    run({"path:a to Q in the plugin's lattice rescorer", "path", "pt-PT", casa}, [&]{
        orthography2ipa::conformance::assert_rescorer_conforms(invents_in_lattice, casa, "pt-PT");
    });
    run({"rescorer:claims nothing, no lang", "path", "", {}}, [&]{
        orthography2ipa::conformance::assert_rescorer_conforms(no_claim_path);
    });
    run({"rescorer:default words, no lang given", "path", "", probe}, [&]{
        orthography2ipa::conformance::assert_rescorer_conforms(*path_a_to_ash);
    });
    return 0;
}

std::string attempt(const std::function<std::string()>& body) {
    try {
        return "{\"value\":" + body() + "}";
    } catch (const std::exception& e) {
        return "{\"raised\":" + jstr(e.what()) + "}";
    }
}

std::string jstrings(const std::vector<std::string>& values) {
    std::vector<std::string> out;
    for (const auto& value : values) out.push_back(jstr(value));
    return jlist(out);
}

int rescorer_runtime_mode() {
    using orthography2ipa::G2P;
    using Plugin = std::shared_ptr<orthography2ipa::RescorerPlugin>;
    const auto lattice = [](auto make) {
        return [make]() -> Plugin { return std::make_shared<ContributesLattice>(make()); };
    };
    const std::vector<std::string> codes{"pt-PT", "ro-RO", "ar"};
    const std::vector<std::pair<std::string, std::function<Plugin()>>> cases = {
        {"keeps", lattice([] { return std::make_shared<KeepsSlots>(); })},
        {"a to Q", lattice([] { return std::make_shared<RewritesCandidates>("a", "Q"); })},
        {"a to ɐ", lattice([] { return std::make_shared<RewritesCandidates>("a", "ɐ"); })},
        {"empties every slot", lattice([] { return std::make_shared<EmptiesEverySlot>(); })},
        {"empties the first slot", lattice([] { return std::make_shared<EmptiesFirstSlot>(); })},
        {"flips on every call", lattice([] { return std::make_shared<FlipsFirstSlot>(); })},
        {"path:keeps", [] { return std::make_shared<NoClaimPath>(); }},
        {"path:a to Q", [&] { return std::make_shared<PathRewriter>(codes, "a", "Q"); }},
        {"path:empties", [&] { return std::make_shared<EmptiesPaths>(codes); }},
        {"path:flips", [&] { return std::make_shared<FlipsPaths>(codes); }},
    };
    const std::vector<std::pair<std::string, std::string>> words = {
        {"pt-PT", "casa"}, {"ro-RO", "casa"}, {"ar", "صفر"}};
    const std::string name = "__rescorer_runtime__";
    for (const auto& [lang, word] : words) {
        for (const auto& [label, make] : cases) {
            // A fresh plugin and engine per surface: a stateful rescorer then
            // starts each surface from the same call count.
            const auto engine = [&, &lang = lang, &make = make] {
                orthography2ipa::register_rescorer_plugin(name, make());
                return G2P(lang, {{"rescore", {name}}});
            };
            std::cout << "{\"case\":" << jstr(label) << ",\"lang\":" << jstr(lang)
                      << ",\"word\":" << jstr(word) << ",\"surfaces\":{"
                      << "\"transcribe_word\":" << attempt([&, &word = word] {
                             return jstr(engine().transcribe_word(word)); })
                      << ",\"transcribe\":" << attempt([&, &word = word] {
                             return jstr(engine().transcribe(word)); })
                      << ",\"sentence\":" << attempt([&, &word = word] {
                             return jstr(engine().transcribe(word + " " + word)); })
                      << ",\"transcribe_detailed\":" << attempt([&, &word = word] {
                             return jstr(engine().transcribe_detailed(word).ipa); })
                      << ",\"word_candidates\":" << attempt([&, &word = word] {
                             return jstrings(engine().word_candidates(word)); })
                      << ",\"three_calls\":" << attempt([&, &word = word] {
                             const auto g2p = engine();
                             std::vector<std::string> out;
                             for (int i = 0; i < 3; ++i) out.push_back(g2p.transcribe_word(word));
                             return jstrings(out); })
                      << "}}\n" << std::flush;
        }
    }
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    const std::string mode = argc > 1 ? argv[1] : "";
    try {
        if (mode == "features" && argc > 2) return features_mode(argv[2]);
        if (mode == "conformance") return conformance_mode();
        if (mode == "rescorer-runtime") return rescorer_runtime_mode();
    } catch (const std::exception& e) {
        std::cout << "{\"error\":" << jstr(e.what()) << "}\n" << std::flush;
        return 1;
    }
    std::cout << "{\"error\":\"usage: features <fixture.json> | conformance | rescorer-runtime\"}\n";
    return 2;
}
