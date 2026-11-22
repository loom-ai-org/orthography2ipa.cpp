// Direct port of the code-resolution half of orthography2ipa/registry.py.
// The tables are GENERATED from the reference (tests/gen_registry_tables.py ->
// src/registry_tables.inc); the tag measures are the linguatag port in
// include/orthography2ipa/linguatag.hpp, and the langcodes standardization is
// the port in include/orthography2ipa/langcodes.hpp. Nothing here invents a
// match: the order, the private-use carve-outs and the "no usable match leaves
// the tag unchanged" rule are the reference's.
#include "orthography2ipa/registry.hpp"

#include "orthography2ipa/langcodes.hpp"
#include "orthography2ipa/linguatag.hpp"
#include "unicode_util.hpp"

#include <unicode/locid.h>
#include <unicode/uchar.h>
#include <unicode/unistr.h>

#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace orthography2ipa::registry {
namespace {

// registry.py's tables, transcribed from the reference.
#include "registry_tables.inc"

template <std::size_t N>
const std::pair<const char*, const char*>* find_in(
        const std::pair<const char*, const char*> (&table)[N], const std::string& key) {
    for (const auto& entry : table)
        if (key == entry.first) return &entry;
    return nullptr;
}

std::string py_lower(const std::string& text) {
    return uni::to_utf8(uni::lower(uni::to_utf32(text)));
}

// registry.py _PRIVATE_USE: the singleton "-x-", matched case-insensitively.
// The offset of its first match, or npos.
std::size_t private_use_at(const std::string& code) {
    for (std::size_t at = 0; at + 3 <= code.size(); ++at)
        if (code[at] == '-' && (code[at + 1] == 'x' || code[at + 1] == 'X') && code[at + 2] == '-')
            return at;
    return std::string::npos;
}

bool has_private_use(const std::string& code) {
    return private_use_at(code) != std::string::npos;
}

// The private-use tag without a script subtag that is the language's IANA
// Suppress-Script (langcodes DEFAULT_SCRIPTS), lowered; empty when the tag
// carries no such script. RFC 5646 §3.1.9 says such a script adds no
// information, so pt-Latn-BR-x-sp names pt-BR-x-sp. Only the part before the
// private use goes to langcodes, unnormalized, so no macrolanguage folding
// happens and a private-use subtag over 8 characters never reaches the parser.
std::string suppressed_script_form(const std::string& code) {
    const std::size_t at = private_use_at(code);
    const std::string head = code.substr(0, at);
    const auto parsed = langcodes::parse(head, /*normalize=*/false);
    if (!parsed) return {};
    const std::string bare = langcodes::simplify_script(parsed.lang).to_tag();
    if (bare.empty() || py_lower(bare) == py_lower(head)) return {};
    return py_lower(bare + "-x-" + code.substr(at + 3));
}

// registry.py _casefold_lookup: the alias target or registered code whose
// lower case is `folded`, aliases first.
std::optional<std::string> casefold_lookup(const std::string& folded,
                                           const std::vector<std::string>& available) {
    for (const auto& [known, target] : _ALIASES)
        if (py_lower(known) == folded) return std::string(target);
    for (const auto& known : available)
        if (py_lower(known) == folded) return known;
    return std::nullopt;
}

// The primary language subtag of a tag, lowered.
std::string primary_language(const std::string& tag) {
    return py_lower(tag.substr(0, tag.find('-')));
}

bool in_catalog(const std::vector<std::string>& available, const std::string& code) {
    return std::find(available.begin(), available.end(), code) != available.end();
}

// registry.py _region_subtag: the region subtag of *code* in upper case, or
// nothing (RFC 5646 §2.2.4). A singleton starts the extensions, so nothing
// after it is a region.
std::optional<std::string> region_subtag(const std::string& code) {
    std::vector<std::string> parts;
    for (std::size_t start = 0;;) {
        const auto at = code.find('-', start);
        parts.push_back(code.substr(start, at - start));
        if (at == std::string::npos) break;
        start = at + 1;
    }
    for (std::size_t i = 1; i < parts.size(); ++i) {
        const std::u32string subtag = uni::to_utf32(parts[i]);
        if (subtag.size() == 1) return std::nullopt;
        const bool letters = subtag.size() == 2 && std::all_of(subtag.begin(), subtag.end(), uni::is_alpha);
        const bool digits = subtag.size() == 3 && std::all_of(subtag.begin(), subtag.end(), [](char32_t c) {
            const auto type = u_getIntPropertyValue(static_cast<UChar32>(c), UCHAR_NUMERIC_TYPE);
            return type == U_NT_DECIMAL || type == U_NT_DIGIT;
        });
        if (letters || digits) {
            icu::UnicodeString value = icu::UnicodeString::fromUTF8(parts[i]);
            value.toUpper(icu::Locale::getRoot());
            std::string out;
            value.toUTF8String(out);
            return out;
        }
    }
    return std::nullopt;
}

} // namespace

// registry.py _resolve_code, in the documented order.
std::string resolve_code(const std::string& code_in, const std::vector<std::string>& available,
                         bool allow_nearest) {
    if (const auto* hit = find_in(_ALIASES, code_in)) return hit->second;
    // A registered code names its own spec: macro=True below would fold an
    // individual language into its macrolanguage and miss it.
    if (in_catalog(available, code_in)) return code_in;
    // BCP-47 tags are case-insensitive (RFC 5646 §2.1.1), so MNK names mnk
    // before macrolanguage folding can turn it into man.
    if (auto known = casefold_lookup(py_lower(code_in), available)) return *known;
    std::string code = code_in;
    if (!has_private_use(code) && code.rfind("x-", 0) != 0) {
        if (auto standardized = langcodes::try_standardize_tag(code, true)) code = *standardized;
        if (const auto* hit = find_in(_ALIASES, code)) return hit->second;
    }
    if (in_catalog(available, code)) return code;
    // A script subtag that linguatag gives as the likely script of the rest of
    // the tag adds nothing to it, so apc-Arab-SY names apc-SY. The language
    // must be the one the tag gives: kas-Arab standardizes to ks-Arab, and the
    // own-language search below keeps it on kas.
    std::vector<std::string> parts;
    for (std::size_t start = 0;;) {
        const auto at = code.find('-', start);
        parts.push_back(code.substr(start, at - start));
        if (at == std::string::npos) break;
        start = at + 1;
    }
    const std::string primary = py_lower(parts[0]);
    const std::string rest_script = parts.size() > 1 ? parts[1] : std::string();
    const auto is_script = [](const std::string& subtag) {
        const std::u32string wide = uni::to_utf32(subtag);
        return wide.size() == 4 && std::all_of(wide.begin(), wide.end(), uni::is_alpha);
    };
    if (parts.size() > 1 && is_script(rest_script) && primary == primary_language(code_in)) {
        std::string unscripted = primary;
        for (std::size_t i = 2; i < parts.size(); ++i) unscripted += "-" + parts[i];
        if (in_catalog(available, unscripted)) {
            const auto likely = linguatag::likely_script(unscripted);
            if (likely && *likely == rest_script) return unscripted;
        }
    }
    if (has_private_use(code)) {
        const std::string folded = suppressed_script_form(code);
        if (!folded.empty())
            if (auto known = casefold_lookup(folded, available)) return *known;
    }
    // A bare tag names its motherland variety (pt -> pt-PT).
    if (code.find('-') == std::string::npos) {
        const std::string fallback = linguatag::with_default_region(code);
        if (in_catalog(available, fallback)) return fallback;
    }
    if (const auto* hit = find_in(_REGION_DEFAULTS, code)) return hit->second;
    // A region of a _MOTHERLAND_FALLBACK language with no spec of its own has
    // no sourced difference either, so it reads the motherland variety: a tag
    // whose region has a spec reads that spec (fr-CA-u-mu-celsius -> fr-CA),
    // any other the default region (fr-SN -> fr-FR).
    const std::string language = primary_language(code);
    if (std::find(std::begin(_MOTHERLAND_FALLBACK), std::end(_MOTHERLAND_FALLBACK), language)
            != std::end(_MOTHERLAND_FALLBACK)
        && !has_private_use(code)) {
        if (const auto region = region_subtag(code); region && in_catalog(available, language + "-" + *region))
            return language + "-" + *region;
        if (const std::string fallback = linguatag::with_default_region(language); in_catalog(available, fallback))
            return fallback;
    }
    if (!allow_nearest) return code;
    // An unknown variant falls back to the standard variety: the candidates
    // are the catalog and the old keys of renamed specs, sorted as one set.
    std::vector<std::string> candidates = available;
    for (const auto& entry : _RENAMED) candidates.emplace_back(entry.first);
    std::sort(candidates.begin(), candidates.end());
    candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
    // The bs spec reads Cyrillic too, so a Bosnian Cyrillic tag of any region
    // or private use is Bosnian.
    if (primary == "bs" && parts.size() > 1 && py_lower(parts[1]) == "cyrl") return "bs";
    // Likely subtags make bare ar Egyptian, so the distance puts a region no
    // Arabic spec names beside whichever dialect spec sorts first. No source
    // states a difference from Modern Standard Arabic for such a region.
    const bool scriptless_or_arab = !(parts.size() > 1 && is_script(rest_script)) ||
                                    py_lower(rest_script) == "arab";
    if (primary == "ar" && scriptless_or_arab) {
        if (const auto region = region_subtag(code)) {
            bool named = false;
            for (const auto& candidate : candidates)
                if (candidate.substr(0, candidate.find('-')) == "ar" &&
                    region_subtag(candidate) == region) {
                    named = true;
                    break;
                }
            if (!named) return "ar";
        }
    }
    // The primary language subtag names the language (RFC 5646 §2.2.1). When
    // that language has a spec, the match stays inside it: sr-Latn-RS is
    // Serbian, not cnr, whose likely subtags are sr-Latn-ME. The requested
    // subtag comes first, because standardization folds als-AL into sq; the
    // standardized one serves a tag whose own language has no spec, such as
    // sh (sr-Latn). A one-letter subtag (x-, i-) is a singleton. The
    // own-language specs are measured from the tag that names their language:
    // langcodes folds fat-GH into ak-GH, and CLDR 49 makes Fanti a language
    // apart from Akan. With no own-language spec near enough, every candidate
    // is searched.
    std::optional<std::string> match;
    for (const std::string& tag : {code_in, code}) {
        const std::string own_language = primary_language(tag);
        std::vector<std::string> own;
        if (own_language.size() > 1)
            for (const auto& candidate : candidates)
                if (primary_language(candidate) == own_language) own.push_back(candidate);
        if (!own.empty()) {
            // CLDR lists no likely script for a language it does not name, so
            // Latn has nothing to be measured against and would put every
            // own-language spec out of reach. Another script stays.
            std::string measured = tag;
            if (!linguatag::likely_script(own_language)) {
                const std::size_t dash = measured.find('-');
                if (dash != std::string::npos && dash > 0 && py_lower(measured.substr(dash, 5)) == "-latn" &&
                    (measured.size() == dash + 5 || measured[dash + 5] == '-'))
                    measured.erase(dash, 5);
            }
            match = linguatag::closest_lang(measured, own, linguatag::default_max_language_distance,
                                            linguatag::Lects::standard);
            break;
        }
    }
    if (!match)
        match = linguatag::closest_lang(code, candidates, linguatag::default_max_language_distance,
                                        linguatag::Lects::standard);
    if (!match) return code;
    if (const auto* renamed = find_in(_RENAMED, *match)) return renamed->second;
    return *match;
}

} // namespace orthography2ipa::registry
