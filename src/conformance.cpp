// Direct port of orthography2ipa/conformance.py.
#include "orthography2ipa/conformance.hpp"

#include "orthography2ipa/orthography2ipa.hpp"
#include "unicode_util.hpp"

#include <algorithm>
#include <cstdio>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>

namespace orthography2ipa::conformance {
namespace {

using uni::category;
using uni::to_utf32;
using uni::to_utf8;

// conformance.py installs the kit's plugin in the registry for the duration of
// the check and restores what was there in a `finally`. The C++ registry keys
// plugins by name and has no unregister, so the kit registers ONE entry under
// its own name (the reference's `"__conformance__"`) that delegates to the
// caller's plugin while enabled and claims no language code at all while
// disabled. `owns_language` (registry.py) then routes nothing to it, which is
// the same observable state as `registry._syllabifiers = {}`: the built-in
// answer runs.
const char* const CONFORMANCE_NAME = "__conformance__";

/// The registry gate's common half, so one RAII object restores either kind.
class Gate {
public:
    Gate() = default;
    Gate(const Gate&) = delete;
    Gate& operator=(const Gate&) = delete;
    virtual ~Gate() = default;
    virtual void set_enabled(bool enabled) = 0;
};

/// conformance.py's `finally: registry.<stage> = saved`.
class RestoreOnExit {
public:
    explicit RestoreOnExit(Gate& gate) : gate_(gate) {}
    RestoreOnExit(const RestoreOnExit&) = delete;
    RestoreOnExit& operator=(const RestoreOnExit&) = delete;
    ~RestoreOnExit() { gate_.set_enabled(false); }

private:
    Gate& gate_;
};

class ScopedSyllabifier : public SyllabifierPlugin, public Gate {
public:
    ScopedSyllabifier(const SyllabifierPlugin& plugin, std::vector<std::string> codes)
        : plugin_(plugin), codes_(std::move(codes)) {}

    void set_enabled(bool enabled) override { enabled_ = enabled; }

    std::vector<std::string> language_codes() const override {
        return enabled_ ? codes_ : std::vector<std::string>{};
    }
    std::vector<std::string> syllabify(const std::string& word,
                                       const std::string& lang) const override {
        return plugin_.syllabify(word, lang);
    }
    int priority() const override { return plugin_.priority(); }

private:
    const SyllabifierPlugin& plugin_;
    std::vector<std::string> codes_;
    bool enabled_ = false;
};

class ScopedRescorer : public RescorerPlugin, public Gate {
public:
    ScopedRescorer(const RescorerPlugin& plugin, std::vector<std::string> codes)
        : plugin_(plugin), codes_(std::move(codes)) {}

    void set_enabled(bool enabled) override { enabled_ = enabled; }

    std::vector<std::string> language_codes() const override {
        return enabled_ ? codes_ : std::vector<std::string>{};
    }
    std::vector<IPAPath> rescore(const std::string& word, const std::string& lang,
                                 const std::vector<IPAPath>& paths) const override {
        return plugin_.rescore(word, lang, paths);
    }
    int priority() const override { return plugin_.priority(); }

private:
    const RescorerPlugin& plugin_;
    std::vector<std::string> codes_;
    bool enabled_ = false;
};

// ─── Python's repr, so a failure reads exactly like the reference's ────────
//
// conformance.py formats every reported word, syllable list and transcription
// with f-string `!r`. "printable" is CPython's: any category but Cc, Cf, Cs,
// Co, Cn, Zl, Zp, Zs (a plain space being the exception), and the escapes are
// CPython's \t \n \r \xXX \uXXXX \UXXXXXXXX.

bool printable(char32_t cp) {
    if (cp == U' ') return true;
    const std::string kind = category(cp);
    return kind != "Cc" && kind != "Cf" && kind != "Cs" && kind != "Co" &&
           kind != "Cn" && kind != "Zl" && kind != "Zp" && kind != "Zs";
}

void append_escape(std::string& out, const char* prefix, char32_t cp, int digits) {
    out += prefix;
    char buffer[8];
    std::snprintf(buffer, sizeof buffer, "%0*X", digits, static_cast<unsigned int>(cp));
    out += buffer;
}

std::string repr_str(const std::string& text) {
    const std::u32string wide = to_utf32(text);
    const bool has_single = std::find(wide.begin(), wide.end(), U'\'') != wide.end();
    const bool has_double = std::find(wide.begin(), wide.end(), U'"') != wide.end();
    const char32_t quote = (has_single && !has_double) ? U'"' : U'\'';
    std::string out;
    out += static_cast<char>(quote);
    for (const char32_t cp : wide) {
        if (cp == quote || cp == U'\\') {
            out += '\\';
            out += to_utf8(std::u32string(1, cp));
        } else if (cp == U'\t') {
            out += "\\t";
        } else if (cp == U'\n') {
            out += "\\n";
        } else if (cp == U'\r') {
            out += "\\r";
        } else if (cp < 0x20) {
            append_escape(out, "\\x", cp, 2);
        } else if (!printable(cp)) {
            if (cp < 0x100) append_escape(out, "\\x", cp, 2);
            else if (cp < 0x10000) append_escape(out, "\\u", cp, 4);
            else append_escape(out, "\\U", cp, 8);
        } else {
            out += to_utf8(std::u32string(1, cp));
        }
    }
    out += static_cast<char>(quote);
    return out;
}

std::string repr_list(const std::vector<std::string>& values) {
    std::string out = "[";
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i) out += ", ";
        out += repr_str(values[i]);
    }
    out += "]";
    return out;
}

/// A dict in Python's sense: insertion-ordered, unique keys, last write wins.
/// Every reported mapping in conformance.py is built by iterating *words*, so
/// the order the keys were first seen is the order `repr` prints.
template <class Value>
class Dict {
public:
    void insert(const std::string& key, Value value) {
        for (auto& item : items_)
            if (item.first == key) { item.second = std::move(value); return; }
        items_.emplace_back(key, std::move(value));
    }
    const Value* find(const std::string& key) const {
        for (const auto& item : items_) if (item.first == key) return &item.second;
        return nullptr;
    }
    bool empty() const { return items_.empty(); }
    std::size_t size() const { return items_.size(); }
    const std::vector<std::pair<std::string, Value>>& items() const { return items_; }

private:
    std::vector<std::pair<std::string, Value>> items_;
};

std::string repr_dict(const Dict<std::pair<std::string, std::string>>& entries) {
    std::string out = "{";
    bool first = true;
    for (const auto& [word, pair] : entries.items()) {
        if (!first) out += ", ";
        first = false;
        out += repr_str(word) + ": (" + repr_str(pair.first) + ", " + repr_str(pair.second) + ")";
    }
    out += "}";
    return out;
}

std::string repr_dict(const Dict<std::vector<std::string>>& entries) {
    std::string out = "{";
    bool first = true;
    for (const auto& [word, values] : entries.items()) {
        if (!first) out += ", ";
        first = false;
        out += repr_str(word) + ": " + repr_list(values);
    }
    out += "}";
    return out;
}

// ─── inventory.py: what this spec is allowed to emit ──────────────────────

/// inventory.py STRESS_MARKS: primary and secondary stress, inserted by
/// `stress.apply_stress_mark`.
const std::vector<std::string>& stress_marks() {
    static const std::vector<std::string> marks = {to_utf8(U"ˈ"), to_utf8(U"ˌ")};
    return marks;
}

bool is_stress_mark(const std::string& text) {
    const auto& marks = stress_marks();
    return std::find(marks.begin(), marks.end(), text) != marks.end();
}

bool is_stress_mark(char32_t cp) {
    return cp == U'ˈ' || cp == U'ˌ';
}

/// Python `str.isspace()`: every character carries the Unicode White_Space
/// property, and the string is not empty.
bool is_space(const std::string& text) {
    static const char32_t spaces[] = {
        0x0009, 0x000A, 0x000B, 0x000C, 0x000D, 0x001C, 0x001D, 0x001E, 0x001F,
        0x0020, 0x0085, 0x00A0, 0x1680, 0x2000, 0x2001, 0x2002, 0x2003, 0x2004,
        0x2005, 0x2006, 0x2007, 0x2008, 0x2009, 0x200A, 0x2028, 0x2029, 0x202F,
        0x205F, 0x3000,
    };
    const std::u32string wide = to_utf32(text);
    if (wide.empty()) return false;
    for (const char32_t cp : wide)
        if (std::find(std::begin(spaces), std::end(spaces), cp) == std::end(spaces)) return false;
    return true;
}

// inventory.py is ported as its own module (include/orthography2ipa/inventory.hpp,
// src/inventory.cpp); conformance reads it through the public API rather than
// carrying a second copy of the emission-inventory derivation. The two
// implementations were diffed line by line before the copy was deleted, and the
// whole-catalog inventory sweep is measured on the public one.

// conformance.py's determinism probe for the whole-word seam: transcribe each
// word with an engine that runs the declared plugins (first pass reuses one
// engine, second pass builds a fresh one), and return word -> IPA. Word-level
// plugins are the C++ whole-word `RescorerPlugin::rescore` seam; the reference
// composes plugin.rescorers() into the slot chain instead, which
// `lattice_pass` below covers.
Dict<std::string> transcribe_pass(const std::string& lang,
                                  const std::vector<std::string>& words,
                                  const std::map<std::string, std::vector<std::string>>& plugins,
                                  bool fresh_engine) {
    Dict<std::string> results;
    std::unique_ptr<G2P> engine;
    for (const auto& word : words) {
        if (fresh_engine || engine == nullptr) {
            engine = std::make_unique<G2P>(plugins.count(lang) ? lang : lang, plugins);
        }
        results.insert(word, engine->transcribe_word(word));
    }
    return results;
}

/// The lattice seam's counterpart: the tokenizer beam the slot chain runs on,
/// width 1 = the engine's default `search="greedy"`.
Dict<std::string> lattice_pass(const LanguageSpec& spec, const std::vector<std::string>& words,
                              const std::vector<const rescorer::LatticeRescorer*>& chain,
                              bool fresh_engine) {
    Dict<std::string> results;
    std::unique_ptr<PhonetokTokenizer> tokenizer;
    if (!fresh_engine) tokenizer = std::make_unique<PhonetokTokenizer>(spec);
    for (const auto& word : words) {
        std::unique_ptr<PhonetokTokenizer> per_word;
        if (fresh_engine) per_word = std::make_unique<PhonetokTokenizer>(spec);
        const auto paths = (per_word ? *per_word : *tokenizer)
            .ipa_beam(word, 1, false, " ", false, &chain);
        results.insert(word, paths.empty() ? std::string() : paths.front().ipa);
    }
    return results;
}

/// conformance.py: `lang = lang or next(iter(plugin.language_codes), None)`, and
/// the codes the kit installs the plugin under — the claimed ones AND the code
/// under test, because "the check is about the plugin's effect, not about code
/// resolution". The C++ engine resolves *lang* before it routes anything, so
/// the resolved code is claimed as well.
std::vector<std::string> install_codes(const std::vector<std::string>& claimed,
                                       const std::string& lang) {
    std::vector<std::string> codes = claimed;
    codes.push_back(lang);
    try {
        codes.push_back(resolve(lang));
    } catch (const std::exception&) {
        // The code under test may not resolve yet: transcribing it is what
        // raises, and it raises with the engine's own message.
    }
    std::sort(codes.begin(), codes.end());
    codes.erase(std::unique(codes.begin(), codes.end()), codes.end());
    codes.erase(std::remove(codes.begin(), codes.end(), std::string()), codes.end());
    return codes;
}

std::vector<std::string> probe_words(const std::vector<std::string>& words) {
    // conformance.py: `words = list(words or DEFAULT_PROBE_WORDS)`.
    return words.empty() ? default_probe_words() : words;
}

/// conformance.py `assert_rescorer_conforms`'s two checks, over whatever the
/// seam produced: determinism first, then the declared inventory.
void check_rescorer_rules(const Dict<std::string>& first, const Dict<std::string>& second,
                          const std::vector<std::string>& words, const LanguageSpec& spec,
                          const std::set<std::string>& declared, const std::string& lang) {
    Dict<std::pair<std::string, std::string>> unstable;
    for (const auto& word : words) {
        const std::string* before = first.find(word);
        const std::string* after = second.find(word);
        if (before == nullptr || after == nullptr) continue;
        if (*before != *after) unstable.insert(word, std::make_pair(*before, *after));
    }
    if (!unstable.empty())
        throw ConformanceError(
            "determinism: this rescorer gives different answers for the same word "
            "on two runs \xE2\x80\x94 " + repr_dict(unstable) +
            ". A transcription that varies run to run is not a transcription.");

    Dict<std::vector<std::string>> escaped;
    for (const auto& [word, ipa] : first.items()) {
        std::vector<std::string> outside;
        for (const auto& token : tokenize(ipa, spec))
            if (declared.count(token) == 0) outside.push_back(token);
        if (!outside.empty()) escaped.insert(word, outside);
    }
    if (escaped.empty()) return;
    throw ConformanceError(
        "inventory: this rescorer emits symbols the " + lang + " spec never "
        "declares \xE2\x80\x94 " + repr_dict(escaped) + ".\n\n"
        "That is invisible in a diff and fatal downstream: a TTS frontend "
        "builds its embedding table from the declared inventory BEFORE "
        "training, so a symbol that appears only at inference has no vector, "
        "and every word carrying it is mispronounced permanently and silently.\n\n"
        "A new symbol is a claim about the phonology. Declare it in the spec, "
        "where it can be read, cited and diffed \xE2\x80\x94 not at runtime.");
}

/// conformance.py _check_round_trip.
void check_round_trip(const SyllabifierPlugin& plugin, const std::vector<std::string>& words,
                      const std::string& lang) {
    for (const auto& word : words) {
        const auto syllables = plugin.syllabify(word, lang);
        std::string joined;
        for (const auto& syllable : syllables) joined += syllable;
        if (joined == word) continue;
        throw ConformanceError(
            "round-trip: syllabifying " + repr_str(word) + " gave " + repr_list(syllables) +
            ", which joins back to " + repr_str(joined) + ".\n\n"
            "A syllabifier SPLITS a word. It does not rewrite it \xE2\x80\x94 normalising, "
            "stripping an accent or fixing a spelling here means every stage "
            "downstream is working on a different word than the caller passed.");
    }
}

/// conformance.py _check_language_claim.
void check_language_claim(const SyllabifierPlugin& plugin) {
    if (!plugin.language_codes().empty()) return;
    throw ConformanceError(
        "language claim: the plugin claims no language codes, so discovery can "
        "never route anything to it. It would be installed and never called.");
}

/// conformance.py _check_downstream_neutrality: the rule that matters —
/// installing the plugin must not move the answer.
void check_downstream_neutrality(const SyllabifierPlugin& plugin,
                                 const std::vector<std::string>& words,
                                 const std::string& lang) {
    const std::map<std::string, std::vector<std::string>> no_plugins;
    auto wrapper = std::make_shared<ScopedSyllabifier>(plugin, install_codes(plugin.language_codes(), lang));
    register_syllabifier_plugin(CONFORMANCE_NAME, wrapper);
    const RestoreOnExit restore(*wrapper);

    wrapper->set_enabled(true);
    auto with_plugin = transcribe_pass(lang, words, no_plugins, /*fresh_engine=*/true);

    wrapper->set_enabled(false);
    auto without_plugin = transcribe_pass(lang, words, no_plugins, /*fresh_engine=*/true);

    Dict<std::pair<std::string, std::string>> moved;
    for (const auto& word : words) {
        const std::string* before = without_plugin.find(word);
        const std::string* after = with_plugin.find(word);
        if (before == nullptr || after == nullptr) continue;
        if (*before != *after) moved.insert(word, std::make_pair(*before, *after));
    }
    if (moved.empty()) return;

    std::vector<std::pair<std::string, std::pair<std::string, std::string>>> sorted(
        moved.items().begin(), moved.items().end());
    std::sort(sorted.begin(), sorted.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    std::string detail;
    for (std::size_t i = 0; i < sorted.size(); ++i) {
        if (i) detail += "\n";
        detail += "    " + sorted[i].first + ":  without the plugin " +
                  repr_str(sorted[i].second.first) + "   with it " +
                  repr_str(sorted[i].second.second);
    }
    throw ConformanceError(
        "downstream neutrality: installing this plugin CHANGES THE TRANSCRIPTION "
        "for " + lang + ":\n\n" + detail + "\n\n"
        "A plugin may answer a question better. It may not change what the question "
        "was. If a better syllabification moves the stress, then the syllable count "
        "is load-bearing for stress \xE2\x80\x94 and that is a bug in the BUILT-IN answer, not "
        "a feature of this plugin.\n\n"
        "So this is probably not your bug. It is orthography2ipa telling you that "
        "its own syllabifier is wrong for " + lang + " and that the spec is leaning on it. "
        "Fix the spec (a missing `diphthongs` list is the usual cause \xE2\x80\x94 see pt-PT), "
        "and this plugin should pass unchanged.");
}

} // namespace

const std::vector<std::string>& default_probe_words() {
    // conformance.py DEFAULT_PROBE_WORDS.
    static const std::vector<std::string> words = {
        "coelho", "viagem", "moeda", "poeta", "saudade", "ciência",
        "mãe", "pão", "coisa", "hoje",
        "paralelepípedo", "água",
    };
    return words;
}

void assert_syllabifier_conforms(const SyllabifierPlugin& plugin,
                                 const std::vector<std::string>& words,
                                 const std::string& lang) {
    const auto probe = probe_words(words);
    std::string language = lang;
    if (language.empty()) {
        const auto codes = plugin.language_codes();
        if (!codes.empty()) language = codes.front();
    }
    if (language.empty())
        throw ConformanceError("the plugin claims no language codes");

    check_language_claim(plugin);
    check_round_trip(plugin, probe, language);
    check_downstream_neutrality(plugin, probe, language);
}

void assert_rescorer_conforms(const RescorerPlugin& plugin,
                              const std::vector<std::string>& words,
                              const std::string& lang) {
    std::string language = lang;
    if (language.empty()) {
        const auto codes = plugin.language_codes();
        if (!codes.empty()) language = codes.front();
    }
    if (language.empty())
        throw ConformanceError("the plugin claims no language codes");
    const auto probe = probe_words(words);

    const LanguageSpec& spec = get(language);
    const auto declared = phoneme_inventory(spec);

    // Declared, not discovered. A rescorer changes the transcription, so it only
    // ever runs because something NAMED it — here, the caller, which is exactly
    // how a downstream engine composes its own pipeline.
    auto wrapper = std::make_shared<ScopedRescorer>(plugin, install_codes(plugin.language_codes(), language));
    register_rescorer_plugin(CONFORMANCE_NAME, wrapper);
    const RestoreOnExit restore(*wrapper);
    wrapper->set_enabled(true);

    const std::map<std::string, std::vector<std::string>> plugins = {
        {"rescore", std::vector<std::string>{CONFORMANCE_NAME}}};
    const auto first = transcribe_pass(language, probe, plugins, /*fresh_engine=*/false);
    const auto second = transcribe_pass(language, probe, plugins, /*fresh_engine=*/true);

    check_rescorer_rules(first, second, probe, spec, declared, language);
}

void assert_rescorer_conforms(const rescorer::LatticeRescorer& plugin,
                              const std::string& lang,
                              const std::vector<std::string>& words) {
    // A bare LatticeRescorer claims no language codes; the caller names the code
    // the check runs under.
    if (lang.empty()) throw ConformanceError("the plugin claims no language codes");
    const auto probe = probe_words(words);

    const LanguageSpec& spec = get(lang);
    const auto declared = phoneme_inventory(spec);

    // rescorer.py `self._rescorers`: the caller's rescorer first, then the
    // spec's own allophone rules, which keep the last word.
    const auto allophone = rescorer::compile_allophone_rescorer(
        spec.allophone_rules, spec.doubled_letters_geminate);
    std::vector<const rescorer::LatticeRescorer*> chain = {&plugin};
    if (allophone != nullptr)
        for (int pass = 0; pass < std::max(1, spec.allophone_passes); ++pass)
            chain.push_back(allophone.get());

    const auto first = lattice_pass(spec, probe, chain, /*fresh_engine=*/false);
    const auto second = lattice_pass(spec, probe, chain, /*fresh_engine=*/true);

    check_rescorer_rules(first, second, probe, spec, declared, lang);
}

} // namespace orthography2ipa::conformance
