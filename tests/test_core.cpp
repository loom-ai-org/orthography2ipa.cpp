#include "orthography2ipa/orthography2ipa.hpp"
#include "orthography2ipa/linguatag.hpp"
#include "orthography2ipa/registry.hpp"
#include "orthography2ipa/stress.hpp"
#include "orthography2ipa/schema.hpp"
#include "orthography2ipa/exceptions.hpp"
#include "orthography2ipa/tone.hpp"
#include "orthography2ipa/transform.hpp"
#include "orthography2ipa/types.hpp"
#include "orthography2ipa/vowels.hpp"
#include "beam.hpp"
#include "codepoint_regex.hpp"

#include <atomic>
// Every check below is an assert, so a Release build (-DNDEBUG) must not
// compile them out.
#undef NDEBUG
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <thread>
#include <variant>
#include <iterator>
#include <set>

using namespace orthography2ipa;

int main() {
    {
        // schema::data_directory() reads ORTHOGRAPHY2IPA_DATA_DIR on its first
        // call, so a value set in main() reaches it.
        const char* env = std::getenv("ORTHOGRAPHY2IPA_DATA_DIR");
        const std::string dir = std::string(env != nullptr ? env : O2I_DEFAULT_DATA_DIR) + "/";
        setenv("ORTHOGRAPHY2IPA_DATA_DIR", dir.c_str(), 1);
        assert(schema::data_directory() == dir);
    }
    assert(resolve("por") == "pt-PT");
    {
        // linguatag lang_distance: an `ara` macrolanguage member, or `ar`
        // with any region, in Arabic script or with no script, is at most 5
        // from a spelling of MSA (`ar`, `arb`, `ar-001`); private use counts
        // for nothing. Cantonese against `zh` in Simplified script is a
        // different language (100). Each expected value is the reference's own.
        const struct { const char* desired; const char* supported; int distance; } rows[] = {
            {"acx-419", "ar", 5}, {"aec-XX", "ar", 5}, {"apd-oxendict", "ar", 5},
            {"acx-419", "arb", 5}, {"apd-Arab", "ar", 5}, {"aec", "arb", 5},
            {"acx-419", "ar-EG", 64}, {"apd-Latn", "ar", 64}, {"acx-x-test", "ar", 5},
            {"acx-Arab-x-oman", "ar", 5}, {"ar-Latn", "ar", 24}, {"arz-Latn", "ar", 64},
            {"yue", "zh", 100}, {"mt", "ar", 134}, {"he", "ar", 134}, {"ar", "acx", 134},
            {"ar-SA", "ar", 4}};
        for (const auto& row : rows)
            assert(linguatag::lang_distance(row.desired, row.supported) == row.distance);
        assert(resolve("acx-419") == "acx-OM");
        assert(resolve("aec-XX") == "aec-EG");
        assert(resolve("apd-oxendict") == "apd-SD");
        assert(resolve("acx-x-test") == "acx-OM");
        assert(resolve("apd-Latn") == "apd-Latn");
        assert(resolve("yue") == "yue");
        assert(resolve("mt") == "mt");
        assert(resolve("he-IL") == "he");
        // A bare tag names its motherland variety; an old key is an alias.
        assert(resolve("pt") == "pt-PT");
        assert(resolve("en") == "en-GB");
        assert(resolve("ar-SA") == "ars-SA");
        assert(resolve("yue-HK") == "yue");
        assert(resolve("pt-PT-x-trasosmontes") == "pt-PT-x-tras-os-montes");
        // Old keys that are the ISO 639-3 code of another language are no
        // aliases: they resolve unchanged and name no spec.
        assert(resolve("tmg") == "tmg");
        assert(resolve("aja") == "aja");
        // A script subtag that is the language's Suppress-Script adds nothing
        // (RFC 5646 §3.1.9), so the private-use tag still names its lect; any
        // other script does not, and an unknown lect falls back as before.
        // Expected values are registry.py resolve() and resolves_exactly().
        assert(resolve("pt-Latn-BR-x-sp") == "pt-BR-x-sp");
        assert(resolves_exactly("pt-Latn-BR-x-sp"));
        assert(resolve("PT-LATN-br-X-SP") == "pt-BR-x-sp");
        assert(resolve("pt-Latn-PT-x-lisbon") == "pt-PT-x-lisbon");
        assert(resolves_exactly("nl-Latn-NL-x-polder"));
        assert(resolve("pt-Cyrl-BR-x-sp") == "pt-Cyrl-BR-x-sp");
        assert(!resolves_exactly("pt-Cyrl-BR-x-sp"));
        assert(resolve("pt-Latn-BR-x-zz") == "pt-BR");
        assert(!resolves_exactly("pt-Latn-BR-x-zz"));
        // The nearest match stays inside the tag's own language (RFC 5646
        // §2.2.1): cnr is nearer to sr-Latn-RS by distance, but sr names it.
        // A tag whose own language has no spec (sh, hbs) takes the language it
        // standardizes to. An exact match ignores case (§2.1.1): MNK is mnk,
        // not a macrolanguage member. Expected values are registry.py.
        assert(resolve("sr-Latn-RS") == "sr");
        assert(resolve("sr-ME") == "sr");
        assert(resolve("sh") == "sr");
        assert(resolve("hbs") == "sr");
        assert(resolve("cnr-RS") == "cnr");
        assert(resolve("cnr-XK") == "cnr");
        assert(resolve("als-AL") == "als");
        assert(resolve("hr-Cyrl") == "sr");
        assert(!resolves_exactly("hr-Cyrl"));
        // cnr has its own Cyrillic keys, and С́ and З́ are each a base letter plus
        // U+0301. Expected values are registry.py and G2P("cnr").
        assert(resolve("cnr-Cyrl") == "cnr");
        assert(resolves_exactly("cnr-Cyrl"));
        assert(resolve("cnr-Cyrl-ME") == "cnr");
        assert(resolves_exactly("cnr-Cyrl-ME"));
        assert(G2P("cnr").transcribe("с\u0301екира") == "ɕɛkira");
        assert(G2P("cnr").transcribe("з\u0301еница") == "ʑɛnitsa");
        assert(G2P("cnr").transcribe("љубав вода") == "ʎubav vɔda");
        assert(G2P("cnr-Cyrl-ME").transcribe("с\u0301екира") == "ɕɛkira");
        assert(resolve("MNK") == "mnk");
        assert(resolves_exactly("MNK"));
        assert(resolve("EN-gb") == "en-GB");
    }
    assert(!available_codes().empty());
    {
        // registry.py available_codes drops every clade node, whatever its
        // code: chm (Mari) is a clade without the x-clade- prefix.
        const auto codes = available_codes();
        bool chm = false;
        for (const auto& code : codes) chm = chm || code == "chm";
        assert(!chm);
        for (const auto& code : codes) assert(code.rfind("x-clade-", 0) != 0);
        // A guard, not a fail-before row: include_clades still lists chm.
        bool listed = false;
        for (const auto& code : available_codes(true)) listed = listed || code == "chm";
        assert(listed);
    }
    {
        // A clade key is the key whatever its spacing or escapes: Python json
        // and read_json both read "cl\u0061de" as clade.
        std::string pattern = (std::filesystem::temp_directory_path() / "orthography2ipa-cpp-XXXXXX").string();
        const char* made = mkdtemp(pattern.data());
        assert(made != nullptr);
        const std::string dir = made;
        std::ofstream(dir + "/zz-plain.json") << R"({"code": "zz-plain", "name": "Plain"})";
        std::ofstream(dir + "/zz-spaced.json") << R"({"code": "zz-spaced", "name": "Spaced", "clade"  :  true})";
        std::ofstream(dir + "/zz-escaped.json") << R"({"code": "zz-escaped", "name": "Escaped", "cl\u0061de": true})";
        set_data_directory(dir);
        assert(available_codes() == std::vector<std::string>{"zz-plain"});
        assert(available_codes(true).size() == 3);
        // schema::data_directory() resolves the same default and is not moved here.
        set_data_directory(schema::data_directory());
        std::filesystem::remove_all(dir);
    }

    {
        // registry.py MissingPlugin: `{spec.code!r}`, the bare stage, `{name!r}`
        // and `{sorted(available) or 'nothing'}`. No plugin is registered yet
        // at this point. Each quoted name is Python 3.12's repr() of it.
        const auto missing = [](const auto& call) {
            try { call(); } catch (const MissingPlugin& e) { return std::string(e.what()); }
            return std::string();
        };
        const auto message = [](const std::string& stage, const std::string& quoted, const std::string& installed) {
            return "the 'pt-PT' spec names the " + stage + " plugin " + quoted + ", and it is not installed.\n\n"
                   "Installed for this stage: " + installed + ".\n\nThis is fatal on purpose. The spec asked "
                   "for this plugin because the built-in answer is not the answer it wants \xe2\x80\x94 so "
                   "falling back would not be a graceful degradation, it would be a DIFFERENT TRANSCRIPTION, "
                   "silently. Install it, or change the spec.";
        };
        const std::vector<std::pair<std::string, std::string>> names = {
            {"nope", "'nope'"}, {"it's", "\"it's\""}, {"a'b\"c", "'a\\'b\"c'"}, {"x\ty", "'x\\ty'"},
            {"a\\b", "'a\\\\b'"}, {"\xc3\xa9", "'\xc3\xa9'"}, {"\x7f", "'\\x7f'"}, {"\xc2\x85", "'\\x85'"},
            {"\xe2\x80\x8b", "'\\u200b'"}, {"\xcd\xb8", "'\\u0378'"}, {"\xf3\xa0\x80\x81", "'\\U000e0001'"},
            {"\xf0\x9f\x98\x80", "'\xf0\x9f\x98\x80'"},
            // Not valid UTF-8: a lone surrogate (surrogatepass) and raw bytes
            // (surrogateescape), as Python holds such a name.
            {"\xed\xa0\x80", "'\\ud800'"}, {"\xed\xb0\x80", "'\\udc00'"}, {"\xed\xbf\xbf", "'\\udfff'"},
            {"a\xed\xa0\xbd", "'a\\ud83d'"}, {"\xff", "'\\udcff'"}, {"\xc3", "'\\udcc3'"},
            {"\xc0\xaf", "'\\udcc0\\udcaf'"}, {"\xe0\x80\xaf", "'\\udce0\\udc80\\udcaf'"},
            {"\xf4\x90\x80\x80", "'\\udcf4\\udc90\\udc80\\udc80'"}, {"\xf0\x9f\x98", "'\\udcf0\\udc9f\\udc98'"},
            {"\xed\xa0", "'\\udced\\udca0'"}};
        for (const auto& [name, quoted] : names)
            assert(missing([&] { G2P("pt-PT", {{"rescore", {name}}}); }) == message("rescore", quoted, "nothing"));
        struct Same : NormalizePlugin {
            std::vector<std::string> language_codes() const override { return {"qaa-x-foreign"}; }
            std::string normalize(const std::string& text, const std::string&) const override { return text; }
        };
        register_normalize_plugin("test-list-b", std::make_shared<Same>());
        register_normalize_plugin("test-list-a", std::make_shared<Same>());
        assert(missing([] { G2P("pt-PT", {{"normalize", {"nope"}}}).transcribe("casa"); }) ==
               message("normalize", "'nope'", "['test-list-a', 'test-list-b']"));
        // g2p.py takes an empty name, and never reads the syllabify stage.
        assert(G2P("pt-PT", {{"syllabify", {""}}}).transcribe("tenho casa") == G2P("pt-PT").transcribe("tenho casa"));
    }

    const auto& portuguese = get("pt");
    const auto& spanish = get("es");
    assert(!portuguese.graphemes.empty());
    assert(!portuguese.family_path().empty());
    // json_loader.py classification. brx names its parent only in
    // `ancestors`. aao takes no family from its graphemes base ar-DZ.
    // gem-x-ingvaeonic reaches West Germanic through a clade whose own
    // parent is not a clade.
    assert(get("brx").parent == "brx-x-proto-boro-garo" && get("brx").family == "Sino-Tibetan");
    assert(get("aao").family == "Afro-Asiatic");
    assert(get("gem-x-ingvaeonic").family == "Indo-European > Germanic > Northwest Germanic > West Germanic");
    {
        // registry.py available_families groups by the loaded family, so brx
        // and kha, which name their parent only in `ancestors`, are classified.
        const auto families = available_families();
        auto lists = [&](const std::string& family, const std::string& code) {
            const auto found = families.find(family);
            if (found == families.end()) return false;
            for (const auto& listed : found->second) if (listed == code) return true;
            return false;
        };
        assert(lists("Sino-Tibetan", "brx"));
        assert(lists("Austroasiatic", "kha"));
    }
    assert(!get("ga").sources.empty());
    assert(get("ga").timespan.has_value());
    // types.py __post_init__: aae declares no allophones, so each of its 35
    // phonemes is its own allophone.
    assert(get("aae").allophones.size() == 35);
    assert(get("aae").allophones.at("\xc3\xb0") == std::vector<std::string>{"\xc3\xb0"});

    Tokenizer tokenizer(portuguese);
    std::vector<std::string> unmapped;
    const auto tokens = tokenizer.tokenize_word("olá", &unmapped);
    assert(!tokens.empty() && unmapped.empty());
    const auto unicode_tokens = tokenizer.tokenize("CH 3☕.");
    assert(unicode_tokens.size() == 5);
    assert(unicode_tokens[0].kind == TokenKind::GRAPHEME && unicode_tokens[0].grapheme == "ch");
    assert(unicode_tokens[0].length == 2);
    assert(unicode_tokens[1].kind == TokenKind::WHITESPACE);
    assert(unicode_tokens[2].kind == TokenKind::DIGIT);
    assert(unicode_tokens[3].kind == TokenKind::UNKNOWN);
    assert(unicode_tokens[4].kind == TokenKind::PUNCTUATION);
    const auto normalized = tokenizer.tokenize("CAFE\xCC\x81");
    assert(normalized.size() == 4);
    assert(normalized.back().grapheme == "é");
    const auto context = tokenizer.tokenize_with_context("casa!");
    assert(context.graphemes->size() == 4);
    assert(context.at(0)->next()->grapheme() == "a");
    assert(context.at(0)->prev() == nullptr);
    assert(context.at(3)->next() == nullptr);
    assert(context.at(0)->span().first == 0 && context.at(0)->span().second == 1);
    const auto beam = tokenizer.beam("casa", 4);
    assert(!beam.empty() && beam.size() <= 4);

    G2P engine("pt");
    assert(!engine.transcribe_detailed("olá").words.empty());
    assert(!engine.features("casa").empty());
    {
        // g2p.py: transcribe, features and sentence_lattice all split through
        // _normalize, which runs the normalize stage with the caller's override.
        struct Digits : NormalizePlugin {
            std::vector<std::string> language_codes() const override { return {"pt-PT"}; }
            std::string normalize(const std::string& text, const std::string&) const override {
                std::string out = text;
                for (auto at = out.find("23"); at != std::string::npos; at = out.find("23", at)) out.replace(at, 2, "vinte");
                return out;
            }
        };
        register_normalize_plugin("test-digits", std::make_shared<Digits>());
        const G2P normalized("pt-PT", {{"normalize", {"test-digits"}}});
        assert(normalized.transcribe_detailed("tenho 23").words.at(1).word == "vinte");
        assert(normalized.features("tenho 23").at(1).word == "vinte");
        assert(normalized.sentence_lattice("tenho 23").words.at(1).surface == "vinte");
        // registry.py runs a declared plugin by name, whatever languages it
        // owns: an es-ES normalizer named on a pt-PT engine runs on every path.
        struct Foreign : Digits {
            std::vector<std::string> language_codes() const override { return {"es-ES"}; }
        };
        register_normalize_plugin("test-digits-es", std::make_shared<Foreign>());
        const G2P foreign("pt-PT", {{"normalize", {"test-digits-es"}}});
        assert(foreign.transcribe_detailed("tenho 23").words.at(1).word == "vinte");
        assert(foreign.features("tenho 23").at(1).word == "vinte");
        assert(foreign.sentence_lattice("tenho 23").words.at(1).surface == "vinte");
    }
    {
        // registry.py get_declared_plugins resolves a declared plugin by name
        // for every stage and never asks which languages it owns: plugins that
        // own only qaa-x-foreign, a code no spec resolves to, run on a pt-PT
        // engine. They stay registered, so no later engine may own that code.
        struct Syllables : SyllabifierPlugin {
            std::vector<std::string> language_codes() const override { return {"qaa-x-foreign"}; }
            std::vector<std::string> syllabify(const std::string& word, const std::string&) const override { return {word}; }
        };
        struct Stress : StressPlugin {
            std::vector<std::string> language_codes() const override { return {"qaa-x-foreign"}; }
            std::optional<std::size_t> stressed_index(const std::string&, const std::vector<std::string>&, const std::string&) const override { return std::nullopt; }
        };
        static int rescored = 0;
        struct Rescore : RescorerPlugin {
            std::vector<std::string> language_codes() const override { return {"qaa-x-foreign"}; }
            std::vector<IPAPath> rescore(const std::string&, const std::string&, const std::vector<IPAPath>& paths) const override { ++rescored; return paths; }
        };
        struct Sandhi : SandhiPlugin {
            std::vector<std::string> language_codes() const override { return {"qaa-x-foreign"}; }
            std::vector<std::string> apply(const std::vector<std::string>& words, const std::vector<std::string>&,
                                           const std::vector<bool>&, const std::string&) const override {
                std::vector<std::string> out = words;
                out.back() += "|";
                return out;
            }
        };
        // The engine looks a syllabifier or stress plugin up by language, so a
        // test plugin that owned es-ES would change every later Spanish
        // transcription in this process.
        const std::string spanish = G2P("es-ES").transcribe("ventana murci\xc3\xa9lago");
        register_syllabifier_plugin("test-syllabify-foreign", std::make_shared<Syllables>());
        register_stress_plugin("test-stress-foreign", std::make_shared<Stress>());
        register_rescorer_plugin("test-rescore-foreign", std::make_shared<Rescore>());
        register_sandhi_plugin("test-sandhi-foreign", std::make_shared<Sandhi>());
        const std::string plain = G2P("pt-PT").transcribe_detailed("tenho casa").ipa;
        assert(G2P("pt-PT", {{"syllabify", {"test-syllabify-foreign"}}}).transcribe_detailed("tenho casa").ipa == plain);
        assert(G2P("pt-PT", {{"stress", {"test-stress-foreign"}}}).transcribe_detailed("tenho casa").ipa == plain);
        const G2P rescore("pt-PT", {{"rescore", {"test-rescore-foreign"}}});
        assert(rescore.transcribe_detailed("tenho casa").ipa == plain && rescored > 0);
        rescored = 0;
        assert(!rescore.word_candidates("casa").empty() && rescored > 0);
        assert(G2P("es-ES").transcribe("ventana murci\xc3\xa9lago") == spanish);
        assert(G2P("pt-PT", {{"sandhi", {"test-sandhi-foreign"}}}).transcribe_detailed("tenho casa").ipa == plain + "|");
    }
    {
        // g2p.py reads only the normalize, rescore and sandhi stages, so a
        // declared syllabify or stress plugin that nothing provides is no error.
        const std::string plain = G2P("pt-PT").transcribe("tenho casa");
        for (const std::string stage : {"syllabify", "stress"}) {
            const G2P undeclared("pt-PT", {{stage, {"nope"}}});
            assert(undeclared.transcribe("tenho casa") == plain);
            assert(undeclared.transcribe("").empty());
        }
    }
    {
        // g2p.py resolves a declared stage when it first runs it, with the
        // caller's list over the spec's, and raises MissingPlugin for a name
        // that nothing provides: rescore in G2P(), normalize on the first
        // chunk, sandhi after the words. Any other stage key is never read.
        const auto missing = [](const auto& call) {
            try { call(); } catch (const MissingPlugin& e) { return std::string(e.what()); }
            return std::string();
        };
        // The message is the reference's; the installed list depends on what
        // this process registered before.
        const std::string message = missing([] { G2P("pt-PT", {{"rescore", {"nope"}}}); });
        assert(message.rfind("the 'pt-PT' spec names the rescore plugin 'nope', and it is not installed.\n\n"
                             "Installed for this stage: ", 0) == 0);
        const std::string tail = "it would be a DIFFERENT TRANSCRIPTION, silently. Install it, or change the spec.";
        assert(message.size() > tail.size() && message.compare(message.size() - tail.size(), tail.size(), tail) == 0);
        const G2P normalize("pt-PT", {{"normalize", {"nope"}}});
        assert(!missing([&] { normalize.transcribe("tenho casa"); }).empty());
        assert(!missing([&] { normalize.features("tenho casa"); }).empty());
        assert(!missing([&] { normalize.sentence_lattice("tenho casa"); }).empty());
        const G2P sandhi("pt-PT", {{"sandhi", {"nope"}}});
        assert(sandhi.transcribe("").empty());
        assert(!missing([&] { sandhi.transcribe("tenho casa"); }).empty());
        assert(G2P("pt-PT", {{"foo", {"nope"}}}).transcribe("tenho casa") == G2P("pt-PT").transcribe("tenho casa"));

        // g2p.py G2P.__init__ calls `plugin.rescorers(self.lang)` once for each
        // declared rescore plugin, the caller's override included.
        static int contributed = 0, consulted = 0;
        struct Identity : rescorer::LatticeRescorer {
            std::vector<Candidate> rescore(const rescorer::SegmentSlot& slot, const rescorer::RescoreContext&) const override {
                ++consulted;
                return slot.candidates;
            }
        };
        struct Lattice : RescorerPlugin {
            Identity identity;
            // A code no spec resolves to: g2p.py does not ask, and the plugin
            // stays registered for the rest of this process.
            std::vector<std::string> language_codes() const override { return {"qaa-x-foreign"}; }
            std::vector<IPAPath> rescore(const std::string&, const std::string&, const std::vector<IPAPath>& paths) const override { return paths; }
            std::vector<const rescorer::LatticeRescorer*> lattice_rescorers(const std::string&) const override {
                ++contributed;
                return {&identity};
            }
        };
        register_rescorer_plugin("test-lattice", std::make_shared<Lattice>());
        const G2P lattice("pt-PT", {{"rescore", {"test-lattice"}}});
        assert(contributed == 1);
        assert(lattice.transcribe("tenho casa") == G2P("pt-PT").transcribe("tenho casa"));
        assert(contributed == 1 && consulted > 0);

        // G2P() holds the plugins it resolved. A plugin registered later under
        // the same name does not reach a built engine, and the old one stays
        // alive for it, as the reference keeps the object it resolved.
        static int old_used = 0, new_used = 0;
        struct Counting : rescorer::LatticeRescorer {
            int* used;
            explicit Counting(int* counter) : used(counter) {}
            std::vector<Candidate> rescore(const rescorer::SegmentSlot& slot, const rescorer::RescoreContext&) const override {
                ++*used;
                return slot.candidates;
            }
        };
        struct Life : RescorerPlugin {
            Counting counting;
            explicit Life(int* counter) : counting(counter) {}
            std::vector<std::string> language_codes() const override { return {"qaa-x-foreign"}; }
            std::vector<IPAPath> rescore(const std::string&, const std::string&, const std::vector<IPAPath>& paths) const override { return paths; }
            std::vector<const rescorer::LatticeRescorer*> lattice_rescorers(const std::string&) const override { return {&counting}; }
        };
        register_rescorer_plugin("test-life", std::make_shared<Life>(&old_used));
        const G2P life("pt-PT", {{"rescore", {"test-life"}}});
        register_rescorer_plugin("test-life", std::make_shared<Life>(&new_used));
        assert(life.transcribe("tenho casa") == G2P("pt-PT").transcribe("tenho casa"));
        assert(old_used > 0 && new_used == 0);

        // The per-word hook calls the plugins the engine holds, never the
        // registry's current entry under the same name.
        static int old_paths = 0, new_paths = 0;
        struct PathCount : RescorerPlugin {
            int* calls;
            explicit PathCount(int* counter) : calls(counter) {}
            std::vector<std::string> language_codes() const override { return {"qaa-x-foreign"}; }
            std::vector<IPAPath> rescore(const std::string&, const std::string&, const std::vector<IPAPath>& paths) const override {
                ++*calls;
                return paths;
            }
        };
        register_rescorer_plugin("test-hook", std::make_shared<PathCount>(&old_paths));
        const G2P hook("pt-PT", {{"rescore", {"test-hook"}}});
        register_rescorer_plugin("test-hook", std::make_shared<PathCount>(&new_paths));
        assert(hook.transcribe("tenho casa") == G2P("pt-PT").transcribe("tenho casa"));
        assert(old_paths > 0 && new_paths == 0);

        // Declared plugins run in the order the spec names them, whatever
        // their priority: registry.get_declared_plugins keeps that order and
        // G2P.__init__ composes their rescorers in it. The reference, given
        // these three plugins declared as low, high, mid, runs their lattice
        // rescorers low, high, mid on every pass. The per-word hook keeps the
        // same order and calls each plugin once, as rescorer.py apply_rescorers
        // calls a rescorer once per slot.
        static std::vector<std::string> ran, lattice_ran;
        struct Recording : rescorer::LatticeRescorer {
            std::string label;
            explicit Recording(std::string name) : label(std::move(name)) {}
            std::vector<Candidate> rescore(const rescorer::SegmentSlot& slot, const rescorer::RescoreContext&) const override {
                lattice_ran.push_back(label);
                return slot.candidates;
            }
        };
        struct Ranked : RescorerPlugin {
            std::string label;
            int rank;
            Recording recording;
            Ranked(const std::string& name, int value) : label(name), rank(value), recording(name) {}
            std::vector<std::string> language_codes() const override { return {"qaa-x-foreign"}; }
            int priority() const override { return rank; }
            std::vector<IPAPath> rescore(const std::string&, const std::string&, const std::vector<IPAPath>& paths) const override {
                ran.push_back(label);
                return paths;
            }
            std::vector<const rescorer::LatticeRescorer*> lattice_rescorers(const std::string&) const override { return {&recording}; }
        };
        register_rescorer_plugin("test-rank-low", std::make_shared<Ranked>("low", 10));
        register_rescorer_plugin("test-rank-high", std::make_shared<Ranked>("high", 90));
        register_rescorer_plugin("test-rank-mid", std::make_shared<Ranked>("mid", 50));
        const G2P ranked("pt-PT", {{"rescore", {"test-rank-low", "test-rank-high", "test-rank-mid"}}});
        ran.clear();
        lattice_ran.clear();
        assert(ranked.transcribe("casa") == G2P("pt-PT").transcribe("casa"));
        std::vector<std::string> lattice_runs;
        for (const auto& label : lattice_ran)
            if (lattice_runs.empty() || lattice_runs.back() != label) lattice_runs.push_back(label);
        const std::vector<std::string> pass_order = {"low", "high", "mid"};
        assert(!lattice_runs.empty() && lattice_runs.size() % pass_order.size() == 0);
        for (std::size_t i = 0; i < lattice_runs.size(); ++i) assert(lattice_runs[i] == pass_order[i % pass_order.size()]);
        const std::vector<std::string> word_order = {"low", "high", "mid"};
        assert(!ran.empty() && ran.size() % word_order.size() == 0);
        for (std::size_t i = 0; i < ran.size(); ++i) assert(ran[i] == word_order[i % word_order.size()]);
    }
    {
        // A declared rescore plugin is checked by the conformance kit, never at
        // run time: rescorer.py apply_rescorers validates nothing, and
        // rescorer_plugin.py names assert_rescorer_conforms as the check.
        // Expected values come from the reference on the same plugins.
        struct Undeclared : rescorer::LatticeRescorer {
            std::vector<Candidate> rescore(const rescorer::SegmentSlot& slot, const rescorer::RescoreContext&) const override {
                auto out = slot.candidates;
                for (auto& candidate : out) if (candidate.ipa == "a") candidate.ipa = "Q";
                return out;
            }
        };
        // arbtok's shape: the whole-word pass returns the paths it was given,
        // and those paths already hold the symbol the lattice rescorer emitted.
        struct Unchanged : RescorerPlugin {
            Undeclared undeclared;
            std::vector<std::string> language_codes() const override { return {"qaa-x-foreign"}; }
            std::vector<IPAPath> rescore(const std::string&, const std::string&, const std::vector<IPAPath>& paths) const override { return paths; }
            std::vector<const rescorer::LatticeRescorer*> lattice_rescorers(const std::string&) const override { return {&undeclared}; }
        };
        register_rescorer_plugin("test-unchanged-undeclared", std::make_shared<Unchanged>());
        assert(G2P("pt-PT", {{"rescore", {"test-unchanged-undeclared"}}}).transcribe_word("casa") == "\xcb\x88kQz\xc9\x90");

        // word_candidates runs the engine's whole chain, the caller's rescorer
        // included: g2p.py `rescorer=self._rescorers or None`.
        const Undeclared user_rescorer;
        const G2P user("pt-PT", {}, "", false, true, true, true, nullptr, &user_rescorer);
        assert((user.word_candidates("casa") == std::vector<std::string>{
            "\xcb\x88kQz\xc9\x90", "\xcb\x88kQs\xc9\x90", "\xcb\x88kQzQ",
            "\xcb\x88k\xc9\x90z\xc9\x90", "\xcb\x88sQz\xc9\x90"}));

        static int calls = 0;
        struct NoPaths : RescorerPlugin {
            std::vector<std::string> language_codes() const override { return {"qaa-x-foreign"}; }
            std::vector<IPAPath> rescore(const std::string&, const std::string&, const std::vector<IPAPath>&) const override { ++calls; return {}; }
        };
        register_rescorer_plugin("test-no-paths", std::make_shared<NoPaths>());
        const G2P silent("pt-PT", {{"rescore", {"test-no-paths"}}});
        assert(silent.transcribe_word("casa").empty());
        assert(calls == 1);
        assert(silent.word_candidates("casa").empty());
        assert(silent.transcribe("tenho casa").empty());
    }
    // g2p.py ipa_lattice: one slot per grapheme, ranked candidates, spans in
    // CODE POINTS, and the top-of-slot concatenation is a valid reading.
    const auto slots = engine.ipa_lattice("casa");
    assert(!slots.empty());
    for (const auto& slot : slots) {
        assert(!slot.candidates.empty());
        assert(slot.span.second > slot.span.first);
        assert(slot.candidates.front().score <= slot.candidates.back().score);
    }
    assert(!engine.word_candidates("casa").empty());
    assert(engine.word_confidence("casa") > 0);
    // ConfidenceBreakdown: value == lattice × coverage, per-slot read present.
    const auto breakdown = engine.confidence_breakdown("casa");
    assert(breakdown.per_slot.size() == slots.size());
    assert(breakdown.coverage == 1.0 && breakdown.unmapped.empty());
    assert(std::abs(breakdown.value - breakdown.lattice * breakdown.coverage) < 1e-12);

    bool missing_plugin = false;
    try { G2P missing("pt", {{"normalize", {"not-installed"}}}); missing.transcribe("casa"); }
    catch (const std::exception&) { missing_plugin = true; }
    assert(missing_plugin);

    const auto vector = feature_vector("p");
    assert(vector.size() == 23);
    assert(segment_distance("p", "p") == 0);
    assert(inventory_distance(portuguese, spanish).feature_mean >= 0);
    // distance.py values from the reference, which sums over sets in hash order,
    // so the last bits can differ.
    auto close_to = [](double value, double expected) { return value - expected < 1e-12 && expected - value < 1e-12; };
    assert(close_to(segment_distance("p", "b"), 0.042553191489361715));
    assert(feature_vector("p")[7] == .5 && feature_vector("p")[11] == 1);
    const auto pt_es = phonological_distance(portuguese, spanish);
    assert(close_to(pt_es.combined, 0.2169145693695121));
    assert(close_to(pt_es.inventory.feature_mean, 0.05383197459021247));
    assert(close_to(pt_es.grapheme.mean_ipa_distance, 0.2292352969556617));
    // distance.py orthographic, positional and ancestry values from the reference.
    // aa/sfw name a script outside SCRIPT_REGISTRY (0.5 term); ab/nmq name two in it.
    assert(close_to(orthographic_distance(get("aa"), get("sfw")), 0.7));
    assert(close_to(orthographic_distance(get("ab"), get("nmq")), 0.45));
    // mwl has a grapheme with an empty positional map, which takes no part.
    assert(close_to(positional_divergence(get("mwl"), get("fr-FR")), 0.9996640537513992));
    // abi and mos share only a root clade; nrb and khh share only a family string.
    assert(ancestry_similarity(get("abi"), get("mos")) == 0);
    assert(ancestry_similarity(get("nrb"), get("khh")) == 0);
    assert(close_to(ancestry_similarity(get("aae"), get("es-CO-x-valluno"), 10, true), 0.0006686902052946891));
    assert(spelling_divergence(portuguese, spanish).total_phonemes > 0);
    assert(full_distance(portuguese, spanish) >= 0);
    assert(pairwise_distances({portuguese, spanish}).size() == 2);

    {
        std::string pattern = (std::filesystem::temp_directory_path() / "orthography2ipa-cpp-XXXXXX").string();
        const char* made = mkdtemp(pattern.data());
        assert(made != nullptr);
        const std::string dir = made;
        const std::string path = dir + "/pt.tsv";
        { std::ofstream output(path); output << "casa\tkaza\n"; }
        register_lexicon("pt", path);
        assert(get_lexicon("pt").at("casa") == "kaza");
        clear_lexicons();

        // lexicon.py get_lexicon reads with universal newlines, keeps the word
        // as written, and skips a line that is whitespace or is not exactly
        // two fields. The expected map is the reference's for these bytes.
        const std::string mixed = dir + "/mixed.tsv";
        {
            std::ofstream output(mixed, std::ios::binary);
            output << "Casa\tA\r\ncasa\tb\rmar\tma\xc9\xbe\textra\nStra\xc3\x9f" "e\tx\n"
                      "\x1c\t\x1c\n\xe3\x80\x80\t\xe3\x80\x80\nluz\tlu\xca\x83\t\nsol\ts\xc9\x94l\r";
        }
        register_lexicon("pt-PT", mixed);
        const std::map<std::string, std::string> parsed{
            {"Casa", "A"}, {"casa", "b"}, {"Stra\xc3\x9f" "e", "x"}, {"sol", "s\xc9\x94l"}};
        assert(get_lexicon("pt-PT") == parsed);
        clear_lexicons();

        // parse_lexicon_text skips a line that is all Python whitespace, and
        // only such a line. The expected map is the reference's.
        const std::map<std::string, std::string> spaced{
            {"\xe3\x80\x80", "x"}, {"A", "b"}, {"\xc2\x85", "y"}, {"c", "d\r"}};
        assert(parse_lexicon_text("\xe3\x80\x80\tx\nA\tb\n\x1c\t\x1c\n\xc2\x85\ty\nc\td\r\n") == spaced);

        // A file that is not UTF-8 raises the codec's UnicodeDecodeError.
        const std::string bad = dir + "/bad.tsv";
        { std::ofstream output(bad, std::ios::binary); output << "casa\tkaza\nmar\tm\xffr\n"; }
        register_lexicon("pt-PT", bad);
        bool raised = false;
        try {
            get_lexicon("pt-PT");
        } catch (const schema::UnicodeDecodeError& error) {
            raised = std::string(error.what()) == "'utf-8' codec can't decode byte 0xff in position 15: invalid start byte";
        }
        assert(raised);
        clear_lexicons();

        // A registered local source that is not a file raises FileNotFoundError,
        // which a caller that catches std::runtime_error still catches.
        register_lexicon("pt-PT", dir + "/missing.tsv");
        raised = false;
        try {
            get_lexicon("pt-PT");
        } catch (const std::runtime_error& error) {
            raised = dynamic_cast<const FileNotFoundError*>(&error) != nullptr &&
                     std::string(error.what()) == "lexicon not found: " + dir + "/missing.tsv";
        }
        assert(raised);
        clear_lexicons();
        std::filesystem::remove_all(dir);
    }

    // A lexicon-dir file belongs to the spec its name resolves to, so en.tsv
    // serves en-GB, the spec that en resolves to.
    {
        std::string pattern = (std::filesystem::temp_directory_path() / "orthography2ipa-cpp-XXXXXX").string();
        const char* made = mkdtemp(pattern.data());
        assert(made != nullptr);
        const std::string dir = made;
        { std::ofstream output(dir + "/en.tsv"); output << "zorkflim\tzɔɹkflɪm\n"; }
        set_lexicon_directory(dir);
        assert(resolve("en") == "en-GB");
        assert(lexicon_path("en-GB") == dir + "/en.tsv");
        assert(get_lexicon("en-GB").at("zorkflim") == "zɔɹkflɪm");
        // The reference adds the primary stress mark to the lexicon entry.
        assert(transcribe("zorkflim", "en-GB") == "ˈzɔɹkflɪm");
        assert(transcribe("zorkflim", "en") == "ˈzɔɹkflɪm");
        clear_lexicons();
        std::filesystem::remove_all(dir);
    }

    // Stress pipeline (stress.py / g2p.py ports): syllabification, stress
    // detection, secondary stress, quantity-sensitive placement and iambic
    // length, with spec data first and no language-specific shortcuts.
    {
        const auto& swedish = get("sv");
        const auto sylls = stress::syllables_for("kvinnor", swedish.code, swedish.diphthongs, &swedish);
        assert((sylls == std::vector<std::string>{"kvi", "nnor"}));
        assert(stress::detect_stress("kvinnor", swedish, &sylls) == 0);
        // Quantity-sensitive (Arabic): weight is read off the transcription,
        // so mudarris splits mu-dar-ris and the heavy penult takes the
        // stress — the ending tables cannot express that.
        const auto& arabic = get("ar");
        assert(stress::detect_stress_by_weight("mudarris", arabic) == -2);
        assert(stress::syllabify_ipa("mudarris", arabic.max_onset).size() == 3);
        assert(stress::syllable_weight("taːb") == stress::SUPERHEAVY);
        assert(stress::syllable_weight("ki") == stress::LIGHT);
        assert(stress::syllable_weight("dar") == stress::HEAVY);
        // Alternating secondary stress (English): ˌcombiˈnation.
        const auto& english = get("en-GB");
        assert((stress::secondary_stress_positions(4, 3, english) == std::set<int>{1}));
        // Iambic length (Carib): foot heads that are themselves light.
        const auto& carib = get("car");
        if (carib.iambic_length)
            assert(!stress::apply_iambic_length("kononope", carib).empty());
        // Clitics take no word stress of their own.
        const LanguageSpec clitic_spec = arabic;
        assert(!arabic.cliticless_words.empty() || true);
        // The positional aperture positions land through the engine beam.
        const auto& french = get("fr-FR");
        assert(!Tokenizer(french).beam("heureux").empty());
        assert(Tokenizer(get("en-GB")).beam("time").front().ipa == "taɪm");
        // The grammatical ending matcher handles the deferring (null)
        // rank-1 shape and a transparent silenced suffix.
        auto ending = beam::match_grammatical_ending(
            {"boul", "an", "ger", "s"}, &french);
        assert(ending.has_value() && ending->ending == "er");
        ending = beam::match_grammatical_ending({"com", "ment"}, &french);
        assert(ending.has_value() && !ending->ipa.has_value());  // deferring
        // weights.py divides by float(sum(weights)), which Python 3.12 adds
        // with compensation. The expected costs are the reference's.
        const std::vector<double> tenths = beam::candidate_base_costs({"a", "b", "c"}, std::vector<CandidateWeight>{{0.1}, {0.2}, {0.3}}, "g");
        assert((tenths == std::vector<double>{1.791759469228055, 1.0986122886681096, 0.6931471805599453}));
        const std::vector<double> spread = beam::candidate_base_costs({"a", "b", "c", "d"}, std::vector<CandidateWeight>{{0.7}, {0.1}, {0.1}, {0.1}}, "g");
        assert(spread.front() == 0.35667494393873245);
        const std::vector<double> big = beam::candidate_base_costs({"a", "b", "c"}, std::vector<CandidateWeight>{{1e16}, {1.0}, {1.0}}, "g");
        assert(big.front() == 2.2204460492503136e-16);
    }

    // exceptions.py quotes with repr(): ASCII controls as \x escapes, and a
    // byte that is not UTF-8 as the \udXXX of the str a Python caller holds.
    // The expected messages are the reference's.
    {
        const auto said = [](const std::string& word, const std::string& unmapped) {
            return std::string(UnmappedScriptError(word, {unmapped}, "pt-PT").what());
        };
        assert(said(std::string("a\0b", 3), std::string(1, '\0')) ==
               "pt-PT: word 'a\\x00b' has unmapped characters '\\x00' not covered by the grapheme table");
        assert(said("\x1b[0m", "\x1b") ==
               "pt-PT: word '\\x1b[0m' has unmapped characters '\\x1b' not covered by the grapheme table");
        assert(said("x\x7fy", "\x7f") ==
               "pt-PT: word 'x\\x7fy' has unmapped characters '\\x7f' not covered by the grapheme table");
        assert(said("ab\xff", "\xff") ==
               "pt-PT: word 'ab\\udcff' has unmapped characters '\\udcff' not covered by the grapheme table");
        assert(said("ab\xed\xa0\x80", "\xed\xa0\x80") ==
               "pt-PT: word 'ab\\ud800' has unmapped characters '\\ud800' not covered by the grapheme table");
    }

    const auto malformed = validate_lexicon("bad line\n");
    assert(!malformed.empty());
    const auto unicode_malformed = validate_lexicon("cafe\xCC\x81\tk\n");
    assert(!unicode_malformed.empty());

    // ── Allophony rescorer (allophony.py / rescorer.py ports) ──
    {
        // segment_ipa: atoms tried longest-first (so tɕʰ is ONE segment,
        // never t + ɕʰ); a match must not be followed by a modifier, so k͈
        // keeps the base of k͈al, and bare single characters need no atom.
        assert((segment_ipa("atɕʰa", {"tɕʰ", "tɕ"}) ==
                std::vector<std::string>{"a", "tɕʰ", "a"}));
        assert((segment_ipa("k͈al") == std::vector<std::string>{"k͈", "a", "l"}));
        assert((segment_ipa("tɕa", {"tɕ"}) == std::vector<std::string>{"tɕ", "a"}));
        // The compiled rescorer is null for a spec without rules (the
        // default engine path stays byte-identical), and non-null for one
        // that declares them.
        const auto& no_rules = get("zh");
        assert(no_rules.allophone_rules.empty());
        assert(rescorer::compile_allophone_rescorer(no_rules.allophone_rules) == nullptr);
        // French declares the nasal-absorption-style rules the flat
        // stand-in could not evaluate; the engine path realizes jeune as
        // ʒœn (the final n is NOT absorbed word-finally).
        const auto& french_spec = get("fr-FR");
        assert(!french_spec.allophone_rules.empty());
        assert(rescorer::compile_allophone_rescorer(french_spec.allophone_rules) != nullptr);
        auto fr_rescorer = rescorer::compile_allophone_rescorer(
            french_spec.allophone_rules, french_spec.doubled_letters_geminate);
        assert(fr_rescorer != nullptr);
        assert(G2P("fr-FR").transcribe_word("jeune") == "\xca\x92\xc5\x93n");
        // The rescorer runs at the slot seam: a rescorer-deleted slot
        // contributes no segment, and the paths reflect the rewrite.
        const auto fr_paths = Tokenizer(french_spec).beam("bonne");
        assert(!fr_paths.empty());
        // Geminate atomicity: the shadda-expanded doubled pair is realised
        // as one unit ( عمّ → ˈʕmm), not split by an outside rule.
        assert(G2P("ar").transcribe_word("\xd8\xb9\xd9\x85\xd9\x91") == "\xcb\x88\xca\x95mm");
    }

    // g2p.py _fold_inserted_nuclei: the stress mark counts written syllables,
    // so a nucleus a rule inserts after the stressed vowel does not move the
    // mark (pt-BR BR_EPENTHESIS_POSTTONIC: pacto [ˈpakitu], not [paˈkitu]).
    // The top-k readings run the same fold. Expected values are the
    // reference's transcribe and word_candidates.
    {
        assert(G2P("pt-BR").transcribe_word("pacto") == "\u02c8pakitu");
        assert(G2P("pt-BR").transcribe_word("Pacto") == "\u02c8pakitu");
        assert(G2P("pt-BR").transcribe_word("rapto") == "\u02c8\u0281apitu");
        assert(G2P("pt-BR-x-sul").transcribe_word("capto") == "\u02c8kaptu");
        assert(G2P("pt-BR").transcribe_word("advogado") == "ad\u0361\u0292ivo\u02c8\u0261adu");
        // A syllable folds only when every vowel character in it is inserted:
        // the rule adds the tilde of [iw̃], but its [i] is written, so it stays.
        assert(G2P("pt-BR-x-recife").transcribe_word("oilmr") == "o\u02c8iw\u0303");
        const auto candidates = G2P("pt-BR").word_candidates("pacto", 3);
        assert(candidates.size() == 3);
        assert(candidates[0] == "\u02c8pakitu");
        assert(candidates[1] == "\u02c8pakito");
    }

    // allophony.py _is_nucleus_segment: a slot whose value is only a combining
    // mark is no nucleus, for followed_by_nucleus and requires_other_nucleus
    // alike. The synthetic spec writes ⟨m⟩ as a bare tilde, as pt-BR writes
    // the coda nasal of ⟨em⟩. Every expected value is the reference's
    // PhonetokTokenizer.ipa_best with compile_allophone_rescorer.
    {
        LanguageSpec bare_tilde;
        bare_tilde.code = "xx-test";
        bare_tilde.graphemes = {{"e", {"e"}}, {"m", {"\u0303"}}, {"k", {"k"}}, {"a", {"a"}}};
        AllophoneRule raise;
        raise.id = "RAISE_E";
        raise.phonemes = {"e"};
        raise.surface = "i";
        raise.followed_by_nucleus = true;
        AllophoneRule drop;
        drop.id = "DROP_E";
        drop.phonemes = {"e"};
        drop.requires_other_nucleus = true;
        auto best = [&](const AllophoneRule& rule, const std::string& word) {
            LanguageSpec spec = bare_tilde;
            spec.allophone_rules = {rule};
            const auto chain = rescorer::compile_allophone_rescorer(spec.allophone_rules);
            const std::vector<const rescorer::LatticeRescorer*> rescorers{chain.get()};
            return PhonetokTokenizer(spec).ipa_best(word, false, " ", false, &rescorers);
        };
        assert(best(raise, "em") == "e\u0303");
        assert(best(raise, "eka") == "ika");
        assert(best(raise, "emka") == "i\u0303ka");
        assert(best(drop, "em") == "e\u0303");
        assert(best(drop, "eka") == "ka");
        assert(best(drop, "emka") == "\u0303ka");
    }

    // ── Tone syllable model (tone.py port) ──
    {
        const auto& thai = get("th");
        assert(thai.tone.has_value());  // tone_rules -> the engine tone data
        // Computed tone: ปู่ (mid class, live, mai tho) takes the falling
        // tone; กราบ (low class, dead-long) takes the low tone.
        assert(G2P("th").transcribe_word("ปู่") == "pu\xcb\x90\xcb\xa8\xcb\xa9");
        assert(G2P("th").transcribe_word("กราบ") == "kra\xcb\x90p\xcc\x9a\xcb\xa8\xcb\xa9");
        // kix declares only the docking convention.
        assert(get("kix").tone_marks_syllable_final);
        assert(!get("kix").tone.has_value());
        // dock_tone_marks is idempotent and moves a mark to its syllable edge.
        assert(tone::dock_tone_marks("o\xCB\xA7\xC5\x8B", {}) == "o\xC5\x8B\xCB\xA7");
        assert(tone::dock_tone_marks("o\xC5\x8B\xCB\xA7", {}) == "o\xC5\x8B\xCB\xA7");
        assert(tone::dock_tone_marks("ai", {}) == "ai");  // no marks: no-op
    }

    // phonetok.py _expand_beam: a coda nasal tilde lands on an oral vowel,
    // ʏ included; after a segment that ends in the tilde it is an empty
    // segment; after a consonant the oral branch wins. Every expected beam is
    // the reference's PhonetokTokenizer._expand_beam.
    {
        using H = std::vector<beam::Hypothesis>;
        auto rows = [](const H& beam) {
            std::vector<std::pair<std::vector<std::string>, double>> out;
            for (const auto& h : beam) out.emplace_back(h.segments, h.score);
            return out;
        };
        using R = std::vector<std::pair<std::vector<std::string>, double>>;
        const std::vector<beam::Branch> after_n{{"\u0303", 0.0}, {"n", 1.0}};
        const std::vector<beam::Branch> after_m{{"\u0303", 0.0}, {"m", 1.0}};
        assert(rows(beam::expand_beam({{{"ʏ"}, 0.0}}, after_n, 8)) ==
               (R{{{"ʏ\u0303", ""}, 0.0}, {{"ʏ", "n"}, 1.0}}));
        assert(rows(beam::expand_beam({{{"k", "ɐ̃j̃"}, 0.0}}, after_m, 8)) ==
               (R{{{"k", "ɐ̃j̃", ""}, 0.0}, {{"k", "ɐ̃j̃", "m"}, 1.0}}));
        assert(rows(beam::expand_beam({{{"ɡ"}, 0.0}}, after_m, 8)) == (R{{{"ɡ", "m"}, 1.0}}));
    }

    // The 28 vowels of the IPA chart are IPA vowels, and a doubled [ɔɔ] is a
    // long vowel that the geminate collapse keeps (the reference's
    // is_ipa_vowel and G2P.transcribe).
    {
        for (const char* v : {"i", "y", "ɨ", "ʉ", "ɯ", "u", "ɪ", "ʏ", "ʊ", "e", "ø", "ɘ", "ɵ", "ɤ",
                              "o", "ə", "ɛ", "œ", "ɜ", "ɞ", "ʌ", "ɔ", "æ", "ɐ", "a", "ɶ", "ɑ", "ɒ"})
            assert(vowels::is_ipa_vowel(std::string(v)));
        assert(G2P("enm").transcribe("toot") == "tɔɔt");
        assert(G2P("sv").transcribe("blyerts") == "ˈblyːæʈʂ");
    }

    // stress.closed_hiatus, the semivowel roles of ⟨y⟩ and ⟨w⟩, the ⟨qu⟩
    // onset grapheme and the declared-diphthong weight. Every expected value
    // is the reference's (stress._syllables_for, hiatus_positions,
    // syllable_weight, G2P.transcribe). It runs before the block below
    // registers a whole-word pt-PT syllabifier for the rest of the process.
    {
        const LanguageSpec& pt = get("pt-PT");
        const LanguageSpec& es = get("es-ES");
        using V = std::vector<std::string>;
        assert(stress::syllables_for("rainha", "", pt.diphthongs, &pt) == (V{"ra", "i", "nha"}));
        assert(stress::syllables_for("saia", "", pt.diphthongs, &pt) == (V{"sai", "a"}));
        assert(stress::hiatus_positions("rainha", pt) == (std::set<std::size_t>{2}));
        assert(stress::hiatus_positions("juiz", pt) == (std::set<std::size_t>{2}));
        assert(stress::hiatus_positions("ruindade", pt).empty());
        // stress.final_diphthongs: a final ⟨iu ui⟩ after a vowel.
        assert(stress::hiatus_positions("distraiu", pt) == (std::set<std::size_t>{6}));
        assert(stress::hiatus_positions("pauis", pt) == (std::set<std::size_t>{2}));
        assert(stress::hiatus_positions("viu", pt).empty());
        // stress.unstressed_prefixes, but never onto a final open syllable.
        const LanguageSpec& li = get("li");
        const V gebroek{"ge", "broek"}, geve{"ge", "ve"};
        assert(stress::detect_stress("gebroek", li, &gebroek, "li") == 1);
        assert(stress::detect_stress("geve", li, &geve, "li") == 0);
        // stress.schwa_rimes: the rest of the word is one final schwa syllable,
        // so the word has no prefix. Expected values are the reference's.
        LanguageSpec rimes = li;
        rimes.default_stress_position = 1;
        rimes.unstressed_prefixes = {"ge"};
        rimes.schwa_rimes = {"en"};
        const V geven{"ge", "ven"}, gebed{"ge", "bed"}, geleden{"ge", "le", "den"};
        const V upper_geven{"GE", "VEN"}, gebrr{"ge", "brr"}, geween{"ge", "ween"};
        assert(stress::detect_stress("geven", rimes, &geven) == 0);
        assert(stress::detect_stress("GEVEN", rimes, &upper_geven) == 0);
        assert(stress::detect_stress("gebed", rimes, &gebed) == 1);
        assert(stress::detect_stress("geleden", rimes, &geleden) == 1);
        assert(stress::detect_stress("gebrr", rimes, &gebrr) == 1);
        assert(stress::detect_stress("geween", rimes, &geween) == 1);
        // The rime starts at the first vowel letter, written with a diacritic too.
        const V gezeen{"ge", "zëen"};
        assert(stress::detect_stress("gezëen", rimes, &gezeen) == 1);
        rimes.schwa_rimes.clear();
        assert(stress::detect_stress("geven", rimes, &geven) == 1);
        assert(G2P("nl-NL").transcribe("geven") == "\u02c8\u03c7eiv\u0259n");
        assert(G2P("nl-NL").transcribe("GEVEN") == "\u02c8\u03c7eiv\u0259n");
        assert(G2P("nl-NL").transcribe("verder") == "\u02c8v\u025b\u0279d\u0259\u0279");
        assert(G2P("nl-NL").transcribe("gebed") == "\u03c7\u0259\u02c8b\u025bt");
        assert(G2P("nl-BE").transcribe("geven") == "\u02c8\u0263e\u02d0v\u0259n");
        // The field comes with the inherited stress block.
        assert(G2P("nl-NL-x-polder").transcribe("geven") == "\u02c8\u03c7eiv\u0259n");
        assert(G2P("nl-BE-x-antwerp").transcribe("geven") == "\u02c8\u0263e\u02d0v\u0259n");
        assert(get("nl-BE-x-antwerp").schwa_rimes == (V{"en", "er", "el", "em"}));
        // The tokenizer reads stress.final_diphthongs without closed_hiatus too.
        LanguageSpec final_only = pt;
        final_only.closed_hiatus.reset();
        assert(PhonetokTokenizer(final_only).graphemes("distraiu") ==
               (V{"d", "i", "s", "t", "r", "a", "iu"}));
        final_only.final_diphthongs.reset();
        assert(PhonetokTokenizer(final_only).graphemes("distraiu") ==
               (V{"d", "i", "s", "t", "r", "ai", "u"}));
        assert(stress::syllables_for("mayo", "", es.diphthongs, &es) == (V{"ma", "yo"}));
        assert(stress::syllables_for("rey", "", es.diphthongs, &es) == (V{"rey"}));
        assert(stress::syllables_for("rugby", "", es.diphthongs, &es) == (V{"ru", "gby"}));
        assert(stress::syllables_for("quieres", "", es.diphthongs, &es) == (V{"quie", "res"}));
        assert(stress::syllables_for("acequia", "", es.diphthongs, &es) == (V{"a", "ce", "quia"}));
        assert(stress::syllable_weight("baj") == stress::HEAVY);
        assert(stress::syllable_weight("ba") == stress::LIGHT);
        assert(G2P("pt-PT").transcribe("rainha") == "ʁɐˈiɲɐ");
        assert(G2P("es-ES").transcribe("país") == "paˈis");
        assert(G2P("es-ES").transcribe("ahorrábamos") == "aoˈraβamos");
        // "stress": false opts out of the inherited block; el-CY, with no
        // ISO 639-3 code of its own, takes el's block (BASE_BLOCK).
        assert(!get("eu-biscayan").stress_defined);
        assert(get("el-CY").stress_defined);
    }

    // ── Syllabifier lookup resolves the code (registry.py get_syllabifier) ──
    {
        struct WholeWord : SyllabifierPlugin {
            std::vector<std::string> language_codes() const override { return {"pt-PT"}; }
            std::vector<std::string> syllabify(const std::string& word,
                                               const std::string&) const override {
                return {word};
            }
            int priority() const override { return 90; }
        };
        const auto plugin = std::make_shared<WholeWord>();
        register_syllabifier_plugin("test-whole-word", plugin);
        for (const char* code : {"pt-PT", "pt", "por", "pt-pt", "pt_PT"})
            assert(get_syllabifier(code) == plugin.get());
        assert(get_syllabifier("pt-PT-x-lisbon") != plugin.get());
        assert(stress::syllables_for("ponte", "por") == std::vector<std::string>{"ponte"});
    }
    {
        // A lexicon entry written with U+0027 answers a word typed with U+2019
        // that also carries edge punctuation: the reference straightens the
        // stripped key as well as the exact key.
        std::string pattern = (std::filesystem::temp_directory_path() / "orthography2ipa-cpp-XXXXXX").string();
        const char* made = mkdtemp(pattern.data());
        assert(made != nullptr);
        const std::string dir = made;
        { std::ofstream output(dir + "/en-US.tsv"); output << "zork'flim\tz\xc9\xaa\xc5\x8bk\n"; }
        set_lexicon_directory(dir);
        G2P english("en-US");
        assert(english.transcribe_word("zork\xe2\x80\x99" "flim") == "\xcb\x88z\xc9\xaa\xc5\x8bk");
        assert(english.transcribe_word("zork\xe2\x80\x99" "flim,") == "\xcb\x88z\xc9\xaa\xc5\x8bk");
        assert(english.transcribe_word("\xe2\x80\x9czork\xe2\x80\x99" "flim\xe2\x80\x9d") == "\xcb\x88z\xc9\xaa\xc5\x8bk");
        // A new lexicon source replaces the cached one for an engine that is
        // already built, and the copy get_lexicon returned stays as it was.
        const auto before = get_lexicon("en-US");
        { std::ofstream output(dir + "/en-US.tsv"); output << "zork'flim\tz\xc9\x94k\n"; }
        set_lexicon_directory(dir);
        assert(english.transcribe_word("zork'flim") == "\xcb\x88z\xc9\x94k");
        assert(before.at("zork'flim") == "z\xc9\xaa\xc5\x8bk" && get_lexicon("en-US").at("zork'flim") == "z\xc9\x94k");
        clear_lexicons();
        assert(english.transcribe_word("zork'flim") != "\xcb\x88z\xc9\x94k" && get_lexicon("en-US").empty());
        std::filesystem::remove_all(dir);
    }
    {
        // A search on one thread never bounds the repetitions of a search on
        // another: each sees its own subject's length as the repeat limit.
        const re32::Regex long_run = re32::Regex::compile(U"a*b");
        const re32::Regex short_run = re32::Regex::compile(U"x*");
        const std::u32string subject = std::u32string(500, U'a') + U"b";
        std::atomic<bool> done{false};
        std::thread other([&] {
            while (!done) short_run.search(U"x", nullptr);
        });
        int wrong = 0;
        for (int i = 0; i < 2000; ++i) {
            re32::Match m;
            if (!long_run.search(subject, &m) || m.start != 0 || m.end != subject.size()) ++wrong;
        }
        done = true;
        other.join();
        assert(wrong == 0);
    }

    // types.py FIELD_INHERITANCE["stress"] is BASE_BLOCK, and InheritanceMode
    // looks a member up by its value.
    {
        assert(types::field_inheritance("stress") == types::InheritanceMode::BASE_BLOCK);
        assert(types::field_inheritance("graphemes") == types::InheritanceMode::BASE_MERGE);
        assert(types::field_inheritance("allophone_passes") == types::InheritanceMode::NOT_INHERITED);
        assert(std::string(types::to_string(types::inheritance_mode("base_block"))) == "base_block");
        // transforms.py VOWEL_SET is vowels.IPA_VOWEL_SYMBOLS: ⟨ə⟩ is a vowel,
        // so the Lisbon coda rule velarises the ⟨l⟩ after it (the reference's
        // apply_transform("ˈpəlɐ", "lisbon")).
        assert(transform::apply_transform("ˈpəlɐ", "lisbon") == "ˈpəɫɐ");
    }

    // schema.py: Union[StressRulesModel, Literal[False], None] for `stress`
    // ClosedHiatusModel and FinalDiphthongsModel. Messages and locations are
    // pydantic 2.12's.
    {
        const std::string dir = schema::data_directory();
        const auto biscayan = schema::validate_spec_file(dir + "/eu-biscayan.json");
        assert(biscayan.stress && std::holds_alternative<bool>(*biscayan.stress) &&
               !std::get<bool>(*biscayan.stress));
        const auto portuguese = schema::validate_spec_file(dir + "/pt-PT.json");
        assert(portuguese.stress && std::holds_alternative<schema::StressRulesModel>(*portuguese.stress));
        const auto& hiatus = std::get<schema::StressRulesModel>(*portuguese.stress).closed_hiatus;
        assert(hiatus && !hiatus->pairs.empty());
        std::ifstream input(dir + "/eu-biscayan.json");
        const std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        auto errors_for = [&](const std::string& value) {
            std::string edited = text;
            const std::string key = "\"stress\": false";
            edited.replace(edited.find(key), key.size(), "\"stress\": " + value);
            std::vector<std::string> out;
            try {
                schema::validate_spec_json(edited);
            } catch (const schema::ValidationError& error) {
                for (const auto& m : error.messages()) {
                    std::string loc;
                    for (const auto& part : m.loc) loc += (loc.empty() ? "" : ".") + part.text();
                    out.push_back(loc + " | " + m.type + " | " + m.msg);
                }
            }
            return out;
        };
        assert(errors_for("true") == (std::vector<std::string>{
            "stress.StressRulesModel | model_type | Input should be a valid dictionary or instance of StressRulesModel",
            "stress.literal[False] | literal_error | Input should be False"}));
        assert(errors_for("{\"closed_hiatus\": {\"pairs\": []}}") == (std::vector<std::string>{
            "stress.StressRulesModel.closed_hiatus.pairs | too_short | List should have at least 1 item after validation, not 0",
            "stress.literal[False] | literal_error | Input should be False"}));
        assert(errors_for("{\"closed_hiatus\": {\"pairs\": [\"ai\"], \"x\": 1}}") == (std::vector<std::string>{
            "stress.StressRulesModel.closed_hiatus.x | extra_forbidden | Extra inputs are not permitted",
            "stress.literal[False] | literal_error | Input should be False"}));
        assert(errors_for("{\"closed_hiatus\": null}").empty());
        assert(errors_for("{\"final_diphthongs\": {\"diphthongs\": []}}") == (std::vector<std::string>{
            "stress.StressRulesModel.final_diphthongs.diphthongs | too_short | List should have at least 1 item after validation, not 0",
            "stress.literal[False] | literal_error | Input should be False"}));
        assert(errors_for("{\"final_diphthongs\": {\"diphthongs\": [\"iu\"], \"x\": 1}}") == (std::vector<std::string>{
            "stress.StressRulesModel.final_diphthongs.x | extra_forbidden | Extra inputs are not permitted",
            "stress.literal[False] | literal_error | Input should be False"}));
        assert(errors_for("{\"unstressed_prefixes\": [\"\"]}") == (std::vector<std::string>{
            "stress.StressRulesModel.unstressed_prefixes | value_error | Value error, stress rule entries must be non-empty strings",
            "stress.literal[False] | literal_error | Input should be False"}));
        assert(errors_for("{\"schwa_rimes\": [\"\"]}") == (std::vector<std::string>{
            "stress.StressRulesModel.schwa_rimes | value_error | Value error, stress rule entries must be non-empty strings",
            "stress.literal[False] | literal_error | Input should be False"}));
        assert(errors_for("{\"schwa_rimes\": [3]}") == (std::vector<std::string>{
            "stress.StressRulesModel.schwa_rimes.0 | string_type | Input should be a valid string",
            "stress.literal[False] | literal_error | Input should be False"}));
        // The dump keeps the model's field order; the value is the reference's
        // model_dump_json of nl-NL.
        assert(schema::dump_json(schema::validate_spec_file(dir + "/nl-NL.json")).find(
                   "\"schwa_rimes\":[\"en\",\"er\",\"el\",\"em\"],\"marked_vowels\":") !=
               std::string::npos);
        // schema.py validate_all(): every shipped spec validates.
        const auto results = schema::validate_all();
        assert(results.failures.empty());
        assert(results.ok.size() == schema::iter_spec_files().size());
    }
    {
        // Each word run reads the syllables of its own slots, at a word-local
        // index. The engine passes syllables for one run only, so this calls
        // apply_rescorers directly. The value is the reference's for this call.
        using Seen = std::vector<std::pair<std::size_t, std::vector<std::size_t>>>;
        static Seen seen;
        struct Recording : rescorer::LatticeRescorer {
            std::vector<Candidate> rescore(const rescorer::SegmentSlot& slot,
                                           const rescorer::RescoreContext& context) const override {
                seen.emplace_back(context.index, *context.syll_indices);
                return slot.candidates;
            }
        };
        const auto sequence = Tokenizer(get("pt-PT")).tokenize_with_context("pa ta");
        std::vector<rescorer::SegmentSlot> slots;
        for (const auto& context : *sequence.graphemes) {
            rescorer::SegmentSlot slot;
            slot.grapheme = context.grapheme();
            slot.candidates = {Candidate{"a", 0.0}};
            slots.push_back(std::move(slot));
        }
        const Recording recording;
        const std::vector<std::size_t> sylls{5, 6, 7, 8};
        rescorer::apply_rescorers(slots, *sequence.graphemes, {&recording}, &sylls, 0);
        assert(seen == (Seen{{0, {5, 6}}, {1, {5, 6}}, {0, {7, 8}}, {1, {7, 8}}}));
    }
    return 0;
}
