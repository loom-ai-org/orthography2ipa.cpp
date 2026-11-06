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
    if (!allow_nearest) return code;
    // An unknown variant falls back to the standard variety: the candidates
    // are the catalog and the old keys of renamed specs, sorted as one set.
    std::vector<std::string> candidates = available;
    for (const auto& entry : _RENAMED) candidates.emplace_back(entry.first);
    std::sort(candidates.begin(), candidates.end());
    candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
    // The primary language subtag names the language (RFC 5646 §2.2.1). When
    // that language has a spec, the match stays inside it: sr-Latn-RS is
    // Serbian, not cnr, whose likely subtags are sr-Latn-ME. The requested
    // subtag comes first, because standardization folds als-AL into sq; the
    // standardized one serves a tag whose own language has no spec, such as
    // sh (sr-Latn). A one-letter subtag (x-, i-) is a singleton. With no
    // own-language spec near enough, every candidate is searched.
    std::vector<std::string> own;
    for (const std::string& language : {primary_language(code_in), primary_language(code)}) {
        if (language.size() > 1)
            for (const auto& candidate : candidates)
                if (primary_language(candidate) == language) own.push_back(candidate);
        if (!own.empty()) break;
    }
    std::optional<std::string> match;
    if (!own.empty())
        match = linguatag::closest_lang(code, own, linguatag::default_max_language_distance,
                                        linguatag::Lects::standard);
    if (!match)
        match = linguatag::closest_lang(code, candidates, linguatag::default_max_language_distance,
                                        linguatag::Lects::standard);
    if (!match) return code;
    if (const auto* renamed = find_in(_RENAMED, *match)) return renamed->second;
    return *match;
}

} // namespace orthography2ipa::registry
