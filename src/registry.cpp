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
bool has_private_use(const std::string& code) {
    for (std::size_t at = 0; at + 3 <= code.size(); ++at)
        if (code[at] == '-' && (code[at + 1] == 'x' || code[at + 1] == 'X') && code[at + 2] == '-')
            return true;
    return false;
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
    std::string code = code_in;
    if (!has_private_use(code) && code.rfind("x-", 0) != 0) {
        if (auto standardized = langcodes::try_standardize_tag(code, true)) code = *standardized;
        if (const auto* hit = find_in(_ALIASES, code)) return hit->second;
    }
    if (in_catalog(available, code)) return code;
    // BCP-47 tags are case-insensitive and langcodes never saw a private-use
    // tag, so its case is folded here; aliases are tried before catalog codes.
    if (has_private_use(code)) {
        const std::string folded = py_lower(code);
        for (const auto& [known, target] : _ALIASES)
            if (py_lower(known) == folded) return target;
        for (const auto& known : available)
            if (py_lower(known) == folded) return known;
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
    const auto match = linguatag::closest_lang(code, candidates,
                                               linguatag::default_max_language_distance,
                                               linguatag::Lects::standard);
    if (!match) return code;
    if (const auto* renamed = find_in(_RENAMED, *match)) return renamed->second;
    return *match;
}

} // namespace orthography2ipa::registry
