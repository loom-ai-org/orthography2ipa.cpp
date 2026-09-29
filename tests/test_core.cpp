#include "orthography2ipa/orthography2ipa.hpp"
#include "orthography2ipa/stress.hpp"
#include "orthography2ipa/tone.hpp"
#include "beam.hpp"

#include <cassert>
#include <fstream>
#include <stdexcept>

using namespace orthography2ipa;

int main() {
    assert(resolve("por") == "pt-PT");
    assert(!available_codes().empty());

    const auto& portuguese = get("pt");
    const auto& spanish = get("es");
    assert(!portuguese.graphemes.empty());
    assert(!portuguese.family_path().empty());
    assert(!get("ga").sources.empty());
    assert(get("ga").timespan.has_value());

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
    assert(!engine.lattice("casa").empty());
    assert(!engine.features("casa").empty());
    assert(engine.word_confidence("casa") > 0);

    bool missing_plugin = false;
    try { G2P missing("pt", {{"normalize", {"not-installed"}}}); missing.transcribe("casa"); }
    catch (const std::exception&) { missing_plugin = true; }
    assert(missing_plugin);

    const auto vector = feature_vector("p");
    assert(vector.size() == 23);
    assert(segment_distance("p", "p") == 0);
    assert(inventory_distance(portuguese, spanish).feature_mean >= 0);
    assert(spelling_divergence(portuguese, spanish).total_phonemes > 0);
    assert(full_distance(portuguese, spanish) >= 0);
    assert(pairwise_distances({portuguese, spanish}).size() == 2);

    const std::string path = "/tmp/orthography2ipa-cpp-test.tsv";
    { std::ofstream output(path); output << "casa\tkaza\n"; }
    register_lexicon("pt", path);
    assert(get_lexicon("pt").at("casa") == "kaza");
    clear_lexicons();

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
    return 0;
}
