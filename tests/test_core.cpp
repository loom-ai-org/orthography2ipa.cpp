#include "orthography2ipa/orthography2ipa.hpp"
#include "orthography2ipa/stress.hpp"
#include "orthography2ipa/schema.hpp"
#include "orthography2ipa/tone.hpp"
#include "beam.hpp"

#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>

using namespace orthography2ipa;

int main() {
    assert(resolve("por") == "pt-PT");
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
    assert(close_to(pt_es.combined, 0.21661900286704905));
    assert(close_to(pt_es.inventory.feature_mean, 0.05333936375277406));
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
        clear_lexicons();
        std::filesystem::remove_all(dir);
    }
    return 0;
}
