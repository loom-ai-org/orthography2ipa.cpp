// Direct port of the subset of the third-party `langcodes` package that
// orthography2ipa/registry.py resolves language tags with: the BCP-47 tag
// parser, the `Language` object's normalization chain (prefer_macrolanguage,
// simplify_script, maximize) and the CLDR language-distance algorithm.
//
// The reference reaches langcodes through registry.py `_resolve_code`
// (`langcodes.standardize_tag(code, macro=True)`) and through linguatag, which
// parses and maximizes tags and calls `langcodes.tag_distance(desired,
// supported)` in `lang_distance` (see linguatag.hpp). Each is re-implemented here
// from langcodes' own source, against tables generated from its own data files
// (src/langcodes_tables.inc, by tests/gen_langcodes_tables.py). Nothing is
// approximated and nothing is added: a tag that langcodes cannot parse raises
// here too, and the reference's callers catch that and fall through, which is
// what the `try_*` / `std::optional` forms express.
//
// Tags are ASCII by definition (`tag_parser.py _is_ascii`), so the subtag
// strings here are plain `std::string`; the only Unicode-aware step is the
// `str.lower()` that `normalize_characters` applies to an input tag that may
// not be ASCII yet, which goes through `uni::lower` over code points.
#pragma once

#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace orthography2ipa::langcodes {

/// tag_parser.py LanguageTagError, itself a subclass of ValueError.
class LanguageTagError : public std::invalid_argument {
public:
    explicit LanguageTagError(const std::string& message);
};

/// __init__.py Language: the attributes a language tag parsed into. An empty
/// string or an empty list is the reference's None / absent subtag.
struct Language {
    std::string language;        ///< 2-4 char code, a grandfathered tag, or "x-…"
    std::vector<std::string> extlangs;
    std::string script;
    std::string territory;
    std::vector<std::string> variants;
    std::vector<std::string> extensions;
    std::string private_use;     ///< `private` is a C++ keyword

    /// Language.to_tag(): the tag as BCP 47 writes it. Extlangs and variants
    /// are sorted; extensions keep the order they were read in.
    std::string to_tag() const;
};

/// __init__.py Language.__eq__: two Languages are equal when their tags are.
bool operator==(const Language& left, const Language& right);

/// The outcome of `parse`. langcodes raises LanguageTagError for a tag that
/// does not parse; the non-throwing form reports it here instead, because the
/// reference's own callers catch it and fall through.
struct ParseResult {
    bool ok = false;
    Language lang;
    std::string error;           ///< the LanguageTagError message when !ok

    explicit operator bool() const { return ok; }
};

/// tag_parser.py normalize_characters: lowercase and '_' -> '-', so that the
/// case-insensitive comparison BCP 47 requires becomes an exact one.
std::string normalize_characters(const std::string& tag);

/// tag_parser.py parse_subtags: one recognized subtag and its kind.
struct Component {
    std::string type;            ///< language, extlang, script, territory, variant,
                                 ///< extension, private or grandfathered
    std::string value;
};

/// tag_parser.py parse_tag: the syntax alone, with no registry lookup and no
/// replacement of deprecated subtags. Throws LanguageTagError.
std::vector<Component> parse_tag(const std::string& tag);

/// __init__.py Language.get: parse a tag and replace deprecated values with
/// the tags they map to. Throws LanguageTagError, as the reference does.
Language get(const std::string& tag, bool normalize = true);
/// `get` without the exception.
ParseResult parse(const std::string& tag, bool normalize = true);

/// __init__.py Language.simplify_script: drop a script that the language
/// already implies.
Language simplify_script(const Language& lang);
/// __init__.py Language.assume_script: add the script a language implies.
Language assume_script(const Language& lang);
/// __init__.py Language.prefer_macrolanguage: replace a dominant individual
/// language with its macrolanguage ("cmn" -> "zh").
Language prefer_macrolanguage(const Language& lang);

/// __init__.py Language.broader_tags: this tag and its increasingly general
/// forms, in the order UTR 35 §4.3 says to try them.
std::vector<std::string> broader_tags(const Language& lang);

/// __init__.py Language.maximize: fill in the CLDR likely subtags.
/// `max_form` is the same thing as a tag, and is what the round-trip check
/// compares. A tag that no likely-subtag entry can fill — which the shipped
/// data never produces — is the reference's RuntimeError: `maximize` throws
/// std::runtime_error and `try_maximize` returns nullopt.
Language maximize(const Language& lang);
std::optional<Language> try_maximize(const Language& lang);
std::string max_form(const std::string& tag);

/// __init__.py standardize_tag: normalize deprecated values, optionally
/// prefer the macrolanguage, drop a redundant script, and format the result.
/// Throws LanguageTagError for a tag that does not parse.
std::string standardize_tag(const std::string& tag, bool macro = false);
std::optional<std::string> try_standardize_tag(const std::string& tag,
                                               bool macro = false);

/// language_distance.py TagTriple: the maximized (language, script, territory)
/// pair the distance is measured between. A subtag the reference does not have
/// carries the text "None", which is exactly what its f-string puts into the
/// lookup keys; no real subtag can collide with it.
struct TagTriple {
    std::string language;
    std::string script;
    std::string territory;
};

/// language_distance.py _tuple_distance (through tuple_distance_cached):
/// 0 for identical triples, and otherwise the sum of the language, script and
/// territory distances. The relation is asymmetric by design.
int tuple_distance(const TagTriple& desired, const TagTriple& supported);

/// __init__.py Language.distance: `desired` against `supported`, both first
/// preferred-to-macrolanguage and maximized. Throws what the reference throws.
int distance(const Language& desired, const Language& supported,
             bool ignore_script = false);

/// __init__.py tag_distance. nullopt where langcodes would raise, which the
/// reference's callers treat as "no usable match".
std::optional<int> tag_distance(const std::string& desired,
                                const std::string& supported,
                                bool ignore_script = false);

} // namespace orthography2ipa::langcodes
