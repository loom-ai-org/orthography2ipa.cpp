// langcodes port: BCP-47 tag parsing, the Language normalization chain, and the
// CLDR language-distance algorithm, as the reference's registry.py and
// ovos_spec_tools.language use them. The tables are generated from langcodes'
// own data files into src/langcodes_tables.inc; the code below is a
// line-for-line port of langcodes/tag_parser.py, langcodes/__init__.py
// (Language, standardize_tag, tag_distance) and
// langcodes/language_distance.py.
#include "orthography2ipa/langcodes.hpp"

#include "unicode_util.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <mutex>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace orthography2ipa::langcodes {
namespace {
// Forward declarations of the memo accessors: defined in the later
// anonymous-namespace block (they need Language/TagTriple, which are declared
// by the time those use them), used by the caching wrappers below.
std::mutex& memo_mutex();
std::map<std::string, Language>& parse_cache();
std::map<std::string, std::string>& standardize_cache();
std::map<std::string, TagTriple>& triple_cache();
std::map<std::string, int>& distance_cache();

using uni::to_utf32;
using uni::to_utf8;

/// The shapes src/langcodes_tables.inc declares its tables in.
struct Kv {
    const char* key;
    const char* value;
};
struct Dist {
    const char* desired;
    const char* supported;
    int value;
};

#include "langcodes_tables.inc"

template <class T, std::size_t N>
constexpr std::size_t count_of(const T (&)[N]) noexcept {
    return N;
}

using Map = std::unordered_map<std::string, std::string>;
using Distances = std::unordered_map<std::string, std::unordered_map<std::string, int>>;

Map map_of(const Kv* entries, std::size_t count) {
    Map out;
    out.reserve(count);
    for (std::size_t i = 0; i < count; ++i) out.emplace(entries[i].key, entries[i].value);
    return out;
}

std::unordered_set<std::string> set_of(const char* const* entries, std::size_t count) {
    std::unordered_set<std::string> out;
    out.reserve(count);
    for (std::size_t i = 0; i < count; ++i) out.insert(entries[i]);
    return out;
}

/// Everything the port reads, built once from the generated tables.
struct Tables {
    Map language_replacements;
    Map territory_replacements;
    Map default_scripts;
    Map normalized_macrolanguages;
    Map likely_subtags;
    Distances language_distances;
    std::unordered_set<std::string> maghreb;
    std::unordered_set<std::string> us;
    std::unordered_set<std::string> cnsar;
    std::unordered_set<std::string> latin_america;
    std::unordered_set<std::string> americas;
    int default_territory_distance = 0;
    int default_language_distance = 0;
    int default_script_distance = 0;
};

const Tables& tables() {
    static const Tables instance = [] {
        Tables t;
        t.language_replacements =
            map_of(LANGUAGE_REPLACEMENTS, count_of(LANGUAGE_REPLACEMENTS));
        t.territory_replacements =
            map_of(TERRITORY_REPLACEMENTS, count_of(TERRITORY_REPLACEMENTS));
        t.default_scripts = map_of(DEFAULT_SCRIPTS, count_of(DEFAULT_SCRIPTS));
        t.normalized_macrolanguages =
            map_of(NORMALIZED_MACROLANGUAGES, count_of(NORMALIZED_MACROLANGUAGES));
        t.likely_subtags = map_of(LIKELY_SUBTAGS, count_of(LIKELY_SUBTAGS));
        for (const Dist& d : LANGUAGE_DISTANCES)
            t.language_distances[d.desired][d.supported] = d.value;
        t.maghreb = set_of(MAGHREB, count_of(MAGHREB));
        t.us = set_of(US_TERRITORIES, count_of(US_TERRITORIES));
        t.cnsar = set_of(CNSAR, count_of(CNSAR));
        t.latin_america = set_of(LATIN_AMERICA, count_of(LATIN_AMERICA));
        t.americas = set_of(AMERICAS, count_of(AMERICAS));
        t.default_territory_distance = DEFAULT_TERRITORY_DISTANCE;
        // language_distance.py reads its two other defaults out of the same
        // matrix, under the '*' and '*_*' keys.
        t.default_language_distance = t.language_distances.at("*").at("*");
        t.default_script_distance = t.language_distances.at("*_*").at("*_*");
        return t;
    }();
    return instance;
}

const Map& replacements() { return tables().language_replacements; }

/// The reference's `_get2`: d.get(k1, {}).get(k2, default).
int get2(const Distances& d, const std::string& k1, const std::string& k2, int fallback) {
    const auto outer = d.find(k1);
    if (outer == d.end()) return fallback;
    const auto inner = outer->second.find(k2);
    return inner == outer->second.end() ? fallback : inner->second;
}

bool contains(const std::unordered_set<std::string>& set, const std::string& value) {
    return set.find(value) != set.end();
}

const std::string* lookup(const Map& map, const std::string& key) {
    const auto it = map.find(key);
    return it == map.end() ? nullptr : &it->second;
}

/// Python `str.isascii()` on an already-encoded UTF-8 string.
bool is_ascii(const std::string& text) {
    for (const char ch : text)
        if (static_cast<unsigned char>(ch) >= 0x80) return false;
    return true;
}

bool is_alpha(char ch) {
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
}
bool is_digit(char ch) { return ch >= '0' && ch <= '9'; }
bool is_alnum(const std::string& text) {
    if (text.empty()) return false;
    for (const char ch : text)
        if (!is_alpha(ch) && !is_digit(ch)) return false;
    return true;
}
bool all_alpha(const std::string& text) {
    if (text.empty()) return false;
    for (const char ch : text)
        if (!is_alpha(ch)) return false;
    return true;
}
bool all_digit(const std::string& text) {
    if (text.empty()) return false;
    for (const char ch : text)
        if (!is_digit(ch)) return false;
    return true;
}

std::string lower_ascii(std::string text) {
    for (char& ch : text)
        if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
    return text;
}
std::string upper_ascii(std::string text) {
    for (char& ch : text)
        if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 'a' + 'A');
    return text;
}
/// Python `str.title()` on a four-letter ASCII subtag: the reference only ever
/// applies it to a subtag it has just recognized as a script.
std::string title_ascii(std::string text) {
    if (!text.empty()) text[0] = static_cast<char>(::toupper(static_cast<unsigned char>(text[0])));
    return text;
}

std::vector<std::string> split_hyphen(const std::string& text) {
    std::vector<std::string> parts;
    std::string current;
    for (const char ch : text) {
        if (ch == '-') {
            parts.push_back(current);
            current.clear();
        } else {
            current.push_back(ch);
        }
    }
    parts.push_back(current);
    return parts;
}

std::string join_hyphen(const std::vector<std::string>& parts, std::size_t first,
                        std::size_t last) {
    std::string out;
    for (std::size_t i = first; i < last; ++i) {
        if (i != first) out.push_back('-');
        out.append(parts[i]);
    }
    return out;
}

/// Python `repr()` of a subtag, as the reference puts it into its messages.
std::string py_repr(const std::string& text) {
    const bool has_single = text.find('\'') != std::string::npos;
    const bool has_double = text.find('"') != std::string::npos;
    const char quote = (has_single && !has_double) ? '"' : '\'';
    std::string out(1, quote);
    for (const char ch : text) {
        if (ch == quote || ch == '\\') {
            out.push_back('\\');
            out.push_back(ch);
        } else if (ch == '\n') {
            out += "\\n";
        } else if (ch == '\t') {
            out += "\\t";
        } else if (ch == '\r') {
            out += "\\r";
        } else {
            out.push_back(ch);
        }
    }
    out.push_back(quote);
    return out;
}

// tag_parser.py: the order subtags come in, and the names the error messages
// use for them.
enum SubtagOrder { EXTLANG = 0, SCRIPT, TERRITORY, VARIANT, EXTENSION };
const char* const SUBTAG_TYPES[] = {"extlang",  "script",  "territory",
                                    "variant", "extension", "end of string"};

[[noreturn]] void subtag_error(const std::string& subtag,
                               const std::string& expected = "a valid subtag") {
    throw LanguageTagError("Expected " + expected + ", got " + py_repr(subtag));
}

[[noreturn]] void order_error(const std::string& subtag, int got, int expected) {
    std::vector<std::string> options;
    for (int i = expected; i <= 5; ++i) options.push_back(SUBTAG_TYPES[i]);
    std::string expect_str;
    if (options.size() == 1) {
        expect_str = options[0];
    } else if (options.size() == 2) {
        expect_str = options[0] + " or " + options[1];
    } else {
        for (std::size_t i = 0; i + 1 < options.size(); ++i) {
            if (i) expect_str += ", ";
            expect_str += options[i];
        }
        expect_str += ", or " + options.back();
    }
    throw LanguageTagError("This " + std::string(SUBTAG_TYPES[got]) + " subtag, " +
                           py_repr(subtag) + ", is out of place. Expected " + expect_str + ".");
}

std::vector<Component> parse_subtags(const std::vector<std::string>& subtags, std::size_t index,
                                      int expect = EXTLANG);

/// tag_parser.py parse_extlang: up to three three-letter codes, all of them
/// read as 'extlang' on the strength of their length alone.
std::vector<Component> parse_extlang(const std::vector<std::string>& subtags, std::size_t index) {
    std::vector<Component> parsed;
    while (index < subtags.size() && subtags[index].size() == 3 && parsed.size() < 3) {
        parsed.push_back({"extlang", subtags[index]});
        ++index;
    }
    const std::vector<Component> rest = parse_subtags(subtags, index, SCRIPT);
    parsed.insert(parsed.end(), rest.begin(), rest.end());
    return parsed;
}

/// tag_parser.py parse_extension: a singleton and the run of subtags after it.
/// 'x' swallows everything that follows; any other singleton stops at the next.
std::vector<Component> parse_extension(const std::vector<std::string>& subtags,
                                       std::size_t index) {
    const std::string& subtag = subtags[index];
    if (index + 1 == subtags.size())
        throw LanguageTagError("The subtag " + py_repr(subtag) +
                               " must be followed by something");
    if (subtag == "x") return {Component{"private", join_hyphen(subtags, index, subtags.size())}};

    std::size_t boundary = index + 1;
    while (boundary < subtags.size() && subtags[boundary].size() != 1) ++boundary;
    if (boundary == index + 1)
        throw LanguageTagError("Tag extensions may not contain two singletons in a row");

    std::vector<Component> parsed{{"extension", join_hyphen(subtags, index, boundary)}};
    const std::vector<Component> rest = parse_subtags(subtags, boundary, EXTENSION);
    parsed.insert(parsed.end(), rest.begin(), rest.end());
    return parsed;
}

std::vector<Component> parse_subtags(const std::vector<std::string>& subtags, std::size_t index,
                                     int expect) {
    if (index >= subtags.size()) return {};
    std::string subtag = subtags[index];
    const std::size_t tag_length = subtag.size();
    int tagtype = -1;

    if (tag_length == 1) {
        // A singleton introduces an extension, and extensions necessarily
        // come last, so nothing is checked about the order here.
        if (is_alnum(subtag)) return parse_extension(subtags, index);
        subtag_error(subtag);
    } else if (tag_length == 2) {
        if (all_alpha(subtag)) tagtype = TERRITORY;
    } else if (tag_length == 3) {
        if (all_alpha(subtag)) {
            if (expect <= EXTLANG) return parse_extlang(subtags, index);
            order_error(subtag, EXTLANG, expect);
        } else if (all_digit(subtag)) {
            tagtype = TERRITORY;
        }
    } else if (tag_length == 4) {
        if (all_alpha(subtag)) {
            tagtype = SCRIPT;
        } else if (is_digit(subtag[0])) {
            tagtype = VARIANT;
        }
    } else {
        tagtype = VARIANT;
    }

    if (tagtype < 0) subtag_error(subtag);
    if (tagtype < expect) order_error(subtag, tagtype, expect);

    // A script or a territory is the only one of its kind, so the next subtag
    // must come strictly later in the order; variants and extensions may
    // repeat.
    expect = (tagtype == SCRIPT || tagtype == TERRITORY) ? tagtype + 1 : tagtype;
    if (tagtype == SCRIPT) {
        subtag = title_ascii(subtag);
    } else if (tagtype == TERRITORY) {
        subtag = upper_ascii(subtag);
    }
    std::vector<Component> parsed{{SUBTAG_TYPES[tagtype], subtag}};
    const std::vector<Component> rest = parse_subtags(subtags, index + 1, expect);
    parsed.insert(parsed.end(), rest.begin(), rest.end());
    return parsed;
}

/// __init__.py Language.to_dict / dict.update: a subtag the other side does
/// not have leaves this side's value alone.
void merge(Language* into, const Language& from) {
    if (!from.language.empty()) into->language = from.language;
    if (!from.extlangs.empty()) into->extlangs = from.extlangs;
    if (!from.script.empty()) into->script = from.script;
    if (!from.territory.empty()) into->territory = from.territory;
    if (!from.variants.empty()) into->variants = from.variants;
    if (!from.extensions.empty()) into->extensions = from.extensions;
    if (!from.private_use.empty()) into->private_use = from.private_use;
}

/// __init__.py Language._filter_attributes: keep a subset of the subtags.
/// None of the keysets the reference uses carries extlangs, variants,
/// extensions or private use, so they are dropped.
Language filter_attributes(const Language& lang, const std::vector<std::string>& keys) {
    Language out;
    for (const std::string& key : keys) {
        if (key == "language") {
            out.language = lang.language;
        } else if (key == "script") {
            out.script = lang.script;
        } else if (key == "territory") {
            out.territory = lang.territory;
        }
    }
    return out;
}

/// __init__.py Language.BROADER_KEYSETS, in the order UTR 35 §4.3 gives.
const std::vector<std::vector<std::string>>& broader_keysets() {
    static const std::vector<std::vector<std::string>> keysets = {
        {"language", "script", "territory"},
        {"language", "territory"},
        {"language", "script"},
        {"language"},
        {"script"},
        {},
    };
    return keysets;
}

/// The reference's f-string rendering of a subtag that is None. "None" cannot
/// collide with a real subtag, and it is what the lookup keys are built from,
/// so the port keeps it verbatim rather than inventing a sentinel.
std::string none_text(const std::string& value) {
    return value.empty() ? std::string("None") : value;
}

/// __init__.py Language.distance: the maximized triple a tag is measured from.
TagTriple triple_of_uncached(const Language& lang, bool ignore_script);
TagTriple triple_of(const Language& lang, bool ignore_script) {
    // The maximize/prefer_macrolanguage chain is the cost of ONE pair in
    // closest_lang's nearest-code scan; language_distance.py already caches
    // at the TRIPLE seam (_DISTANCE_CACHE) and every pair of one desired tag
    // re-derives the same candidate triples, so the triples are memoized
    // per (tag, script mode) here. Pure function of its inputs.
    if (lang.language.empty() && lang.script.empty() && lang.territory.empty()) {
        // CLDR stopped maximizing the unspecified language, because 'und'
        // would otherwise land on English.
        return TagTriple{"und", "Zzzz", "ZZ"};
    }
    const std::string key = lang.to_tag() + "|" + (ignore_script ? "ns1" : "ns0");
    {
        const std::lock_guard<std::mutex> guard(memo_mutex());
        const auto hit = triple_cache().find(key);
        if (hit != triple_cache().end()) return hit->second;
    }
    TagTriple value = triple_of_uncached(lang, ignore_script);
    const std::lock_guard<std::mutex> guard(memo_mutex());
    triple_cache().emplace(key, value);
    return value;
}

TagTriple triple_of_uncached(const Language& lang, bool ignore_script) {
    const Language complete = maximize(prefer_macrolanguage(lang));
    return TagTriple{none_text(complete.language),
                     ignore_script ? std::string("None") : none_text(complete.script),
                     none_text(complete.territory)};
}

} // namespace

LanguageTagError::LanguageTagError(const std::string& message)
    : std::invalid_argument(message) {}

std::string Language::to_tag() const {
    std::vector<std::string> subtags{"und"};
    if (!language.empty()) subtags[0] = language;
    std::vector<std::string> sorted_extlangs = extlangs;
    std::sort(sorted_extlangs.begin(), sorted_extlangs.end());
    for (const std::string& extlang : sorted_extlangs) subtags.push_back(extlang);
    if (!script.empty()) subtags.push_back(script);
    if (!territory.empty()) subtags.push_back(territory);
    std::vector<std::string> sorted_variants = variants;
    std::sort(sorted_variants.begin(), sorted_variants.end());
    for (const std::string& variant : sorted_variants) subtags.push_back(variant);
    for (const std::string& extension : extensions) subtags.push_back(extension);
    if (!private_use.empty()) subtags.push_back(private_use);
    return join_hyphen(subtags, 0, subtags.size());
}

bool operator==(const Language& left, const Language& right) {
    return left.to_tag() == right.to_tag();
}

std::string normalize_characters(const std::string& tag) {
    std::u32string text = uni::lower(to_utf32(tag));
    for (char32_t& cp : text)
        if (cp == U'_') cp = U'-';
    return to_utf8(text);
}

std::vector<Component> parse_tag(const std::string& tag) {
    if (!is_ascii(tag))
        throw LanguageTagError("Language tags must be made of ASCII characters");
    const std::string normalized = normalize_characters(tag);
    for (const char* const exception : GRANDFATHERED)
        if (normalized == exception) return {Component{"grandfathered", normalized}};

    const std::vector<std::string> subtags = split_hyphen(normalized);
    for (const std::string& subtag : subtags) {
        if (subtag.size() < 1 || subtag.size() > 8 || !is_alnum(subtag))
            throw LanguageTagError("Expected 1-8 alphanumeric characters, got " +
                                   py_repr(subtag));
    }
    if (subtags[0] == "x") {
        if (subtags.size() == 1)
            throw LanguageTagError("'x' is not a language tag on its own");
        // The whole tag is private use, but it does fill the language slot.
        return {Component{"language", normalized}};
    }
    if (subtags[0].size() >= 2 && subtags[0].size() <= 4) {
        std::vector<Component> parsed{{"language", subtags[0]}};
        const std::vector<Component> rest = parse_subtags(subtags, 1, EXTLANG);
        parsed.insert(parsed.end(), rest.begin(), rest.end());
        return parsed;
    }
    subtag_error(subtags[0], "a language code");
}

namespace {
// The reference memoizes Language parsing (Language._PARSE_CACHE),
// standardize_tag results, and tuple distances
// (language_distance._DISTANCE_CACHE). closest_lang's nearest-code scan is
// O(catalog of tag_distance calls), so the port keeps the same three memo
// layers. All three are pure functions of their arguments.
std::mutex& memo_mutex() {
    static std::mutex mutex;
    return mutex;
}
std::map<std::string, Language>& parse_cache() {
    static std::map<std::string, Language> cache;   // key: normalize flag + tag
    return cache;
}
std::map<std::string, std::string>& standardize_cache() {
    static std::map<std::string, std::string> cache;   // key: macro flag + tag
    return cache;
}
std::map<std::string, TagTriple>& triple_cache() {
    static std::map<std::string, TagTriple> cache;     // key: to_tag + script flag
    return cache;
}
std::map<std::string, int>& distance_cache() {
    static std::map<std::string, int> cache;           // key: the two triples
    return cache;
}
} // namespace

Language get_uncached(const std::string& tag, bool normalize);
Language get(const std::string& tag, bool normalize) {
    const std::string key = (normalize ? "n:" : "r:") + tag;
    {
        const std::lock_guard<std::mutex> guard(memo_mutex());
        const auto hit = parse_cache().find(key);
        if (hit != parse_cache().end()) return hit->second;
    }
    const Language value = get_uncached(tag, normalize);
    const std::lock_guard<std::mutex> guard(memo_mutex());
    parse_cache().emplace(key, value);
    return value;
}

Language get_uncached(const std::string& tag, bool normalize) {
    std::string working = tag;
    if (normalize) {
        // The complete tag may itself be a deprecated tag; smash case and
        // underscores first, because parse_tag has not normalized yet.
        const std::string* whole = lookup(replacements(), normalize_characters(tag));
        if (whole != nullptr) working = *whole;
    }

    Language lang;
    for (const Component& component : parse_tag(working)) {
        const std::string& type = component.type;
        const std::string& value = component.value;
        if (type == "extlang" && normalize && !lang.language.empty()) {
            // A language plus an extended language is often itself a
            // deprecated tag: 'zh-cmn' is 'zh', 'sgn-es' is 'ssp'.
            const std::string* smashed = lookup(replacements(),
                                                normalize_characters(lang.language + "-" + value));
            if (smashed != nullptr) {
                merge(&lang, get(*smashed, normalize));
            } else {
                lang.extlangs.push_back(value);
            }
        } else if (type == "extlang") {
            lang.extlangs.push_back(value);
        } else if (type == "variant") {
            lang.variants.push_back(value);
        } else if (type == "extension") {
            lang.extensions.push_back(value);
        } else if (type == "language") {
            if (value == "und") {
                continue;  // Language.make() with no language subtag
            }
            if (!normalize) {
                lang.language = value;
                continue;
            }
            const std::string* replacement = lookup(replacements(), lower_ascii(value));
            if (replacement == nullptr) {
                lang.language = value;
            } else {
                // The replacement can be a tag of its own, which has to be
                // parsed -- this is what turns 'sh' into 'sr-Latn'.
                merge(&lang, get(*replacement, normalize));
            }
        } else if (type == "territory") {
            if (!normalize) {
                lang.territory = value;
                continue;
            }
            const std::string* replacement =
                lookup(tables().territory_replacements, lower_ascii(value));
            lang.territory = replacement == nullptr ? value : *replacement;
        } else if (type == "grandfathered") {
            // A grandfathered tag the data cannot normalize: the best that can
            // be done is to keep the whole tag as the language.
            lang.language = value;
        } else if (type == "script") {
            lang.script = value;
        } else {  // private
            lang.private_use = value;
        }
    }
    return lang;
}

ParseResult parse(const std::string& tag, bool normalize) {
    ParseResult result;
    try {
        result.lang = get(tag, normalize);
        result.ok = true;
    } catch (const std::exception& error) {
        result.error = error.what();
    }
    return result;
}

Language simplify_script(const Language& lang) {
    if (!lang.language.empty() && !lang.script.empty()) {
        const std::string* script = lookup(tables().default_scripts, lang.language);
        if (script != nullptr && *script == lang.script) {
            Language out = lang;
            out.script.clear();
            return out;
        }
    }
    return lang;
}

Language assume_script(const Language& lang) {
    if (lang.language.empty() || !lang.script.empty()) return lang;
    const std::string* script = lookup(tables().default_scripts, lang.language);
    if (script == nullptr) return lang;  // DEFAULT_SCRIPTS[self.language] KeyError
    Language out = lang;
    out.script = *script;
    return out;
}

Language prefer_macrolanguage(const Language& lang) {
    // __init__.py: `language = self.language or 'und'`.
    const std::string* macro = lookup(tables().normalized_macrolanguages,
                                      lang.language.empty() ? std::string("und")
                                                            : lang.language);
    if (macro == nullptr) return lang;
    Language out = lang;
    out.language = *macro;
    return out;
}

std::vector<std::string> broader_tags(const Language& lang) {
    std::vector<std::string> broader{lang.to_tag()};
    std::unordered_set<std::string> seen{broader[0]};
    for (const std::vector<std::string>& keyset : broader_keysets()) {
        const std::vector<Language> starts{lang, prefer_macrolanguage(lang)};
        for (const Language& start : starts) {
            const std::string tag = filter_attributes(start, keyset).to_tag();
            if (seen.insert(tag).second) broader.push_back(tag);
        }
    }
    return broader;
}

std::optional<Language> try_maximize(const Language& lang) {
    for (const std::string& tag : broader_tags(lang)) {
        const std::string* likely = lookup(tables().likely_subtags, tag);
        if (likely == nullptr) continue;
        Language result = get(*likely, /*normalize=*/false);
        // The values that were already in the tag win over the likely ones.
        merge(&result, lang);
        return result;
    }
    return std::nullopt;
}

Language maximize(const Language& lang) {
    if (std::optional<Language> filled = try_maximize(lang)) return *filled;
    throw std::runtime_error(
        "Couldn't fill in likely values. This represents a problem with the "
        "LIKELY_SUBTAGS data.");
}

std::string max_form(const std::string& tag) {
    return maximize(get(tag)).to_tag();
}

std::string standardize_tag(const std::string& tag, bool macro) {
    const std::string key = (macro ? "m:" : "s:") + tag;
    {
        const std::lock_guard<std::mutex> guard(memo_mutex());
        const auto hit = standardize_cache().find(key);
        if (hit != standardize_cache().end()) return hit->second;
    }
    Language lang = get(tag, /*normalize=*/true);
    if (macro) lang = prefer_macrolanguage(lang);
    const std::string value = simplify_script(lang).to_tag();
    const std::lock_guard<std::mutex> guard(memo_mutex());
    standardize_cache().emplace(key, value);
    return value;
}

std::optional<std::string> try_standardize_tag(const std::string& tag, bool macro) {
    try {
        return standardize_tag(tag, macro);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

namespace {
std::string triple_key(const TagTriple& triple) {
    // language_distance.py f-strings the triple into the lookup key and prints
    // a missing subtag as the text "None"; no real subtag can collide with it.
    const auto part = [](const std::string& value) {
        return value.empty() ? std::string("None") : value;
    };
    return part(triple.language) + "|" + part(triple.script) + "|" + part(triple.territory);
}
} // namespace

int tuple_distance(const TagTriple& desired, const TagTriple& supported) {
    if (desired.language == supported.language && desired.script == supported.script &&
        desired.territory == supported.territory) {
        return 0;
    }
    const Tables& data = tables();
    int distance = 0;

    if (desired.language != supported.language) {
        distance += get2(data.language_distances, desired.language, supported.language,
                         data.default_language_distance);
    }

    const std::string desired_pair = desired.language + "_" + desired.script;
    const std::string supported_pair = supported.language + "_" + supported.script;

    if (desired.script != supported.script) {
        // Scripts only match scripts when they are paired with a language:
        // there is no reason to assume a 'Latn' reader can read 'Cyrl', but
        // plenty to assume a reader of 'sr-Latn' can read 'sr-Cyrl'.
        distance += get2(data.language_distances, desired_pair, supported_pair,
                         data.default_script_distance);
    }

    if (desired.territory != supported.territory) {
        int tdist = data.default_territory_distance;
        if (desired_pair == supported_pair) {
            if (desired.language == "ar") {
                if (contains(data.maghreb, desired.territory) !=
                    contains(data.maghreb, supported.territory))
                    tdist = 5;
            } else if (desired.language == "en") {
                if (desired.territory == "GB" && !contains(data.us, supported.territory)) {
                    tdist = 3;
                } else if (!contains(data.us, desired.territory) && supported.territory == "GB") {
                    tdist = 3;
                } else if (contains(data.us, desired.territory) !=
                           contains(data.us, supported.territory)) {
                    tdist = 5;
                }
            } else if (contains(data.latin_america, desired.territory) &&
                       supported.territory == "419") {
                // A numeric territory such as '419' names a broad region that
                // contains the specific ones, so 'es-PY' is nearer to 'es-419'
                // than an ordinary territory difference.
                tdist = 1;
            } else if (desired.language == "es" || desired.language == "pt") {
                if (contains(data.americas, desired.territory) !=
                    contains(data.americas, supported.territory))
                    tdist = 5;
            } else if (desired_pair == "zh_Hant") {
                if (contains(data.cnsar, desired.territory) !=
                    contains(data.cnsar, supported.territory))
                    tdist = 5;
            }
        }
        distance += tdist;
    }
    return distance;
}

int distance(const Language& desired, const Language& supported, bool ignore_script) {
    if (supported == desired) return 0;
    // language_distance.py's _DISTANCE_CACHE: keyed on the two triples, since
    // the triples are what actually ranks (the reference caches
    // tuple_distance_cached at exactly this seam).
    const std::string desired_tag = triple_key(triple_of(desired, ignore_script));
    const std::string supported_tag = triple_key(triple_of(supported, ignore_script));
    const std::string key = desired_tag + '\x01' + supported_tag;
    {
        const std::lock_guard<std::mutex> guard(memo_mutex());
        const auto hit = distance_cache().find(key);
        if (hit != distance_cache().end()) return hit->second;
    }
    const int value = tuple_distance(triple_of(desired, ignore_script),
                                     triple_of(supported, ignore_script));
    const std::lock_guard<std::mutex> guard(memo_mutex());
    distance_cache().emplace(key, value);
    return value;
}

std::optional<int> tag_distance(const std::string& desired, const std::string& supported,
                                bool ignore_script) {
    try {
        return distance(get(desired), get(supported), ignore_script);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

} // namespace orthography2ipa::langcodes
