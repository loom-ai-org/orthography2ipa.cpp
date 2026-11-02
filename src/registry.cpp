// Direct port of the code-resolution half of orthography2ipa/registry.py and
// of the language-distance helpers it reaches through
// ovos_spec_tools.language. The tables are GENERATED from the reference
// (tests/gen_registry_tables.py -> src/registry_tables.inc); the langcodes
// standardization and tag distance are the port in
// include/orthography2ipa/langcodes.hpp. Nothing here invents a match: the
// order, the private-use carve-outs and the "no usable match leaves the tag
// unchanged" rule are the reference's.
#include "orthography2ipa/registry.hpp"

#include "orthography2ipa/langcodes.hpp"
#include "unicode_util.hpp"

#include <algorithm>
#include <cctype>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace orthography2ipa::registry {
std::string standardize_lang_uncached(const std::string& tag);
int lang_distance_uncached(const std::string& desired, const std::string& supported);

namespace {

// registry.py/ovos_spec_tools' tables, transcribed from the reference.
#include "registry_tables.inc"

const std::pair<const char*, const char*>* find_in(
        const std::pair<const char*, const char*>* table, std::size_t size,
        const std::string& key) {
    for (std::size_t i = 0; i < size; ++i)
        if (key == table[i].first) return &table[i];
    return nullptr;
}


std::string lower_ascii(const std::string& text) {
    std::string out = text;
    for (auto& ch : out) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return out;
}

std::string upper_ascii(const std::string& text) {
    std::string out = text;
    for (auto& ch : out) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    return out;
}

} // namespace

// ovos_spec_tools.language standardize_lang. With langcodes installed — which
// the reference environment has — this is langcodes.standardize_tag; the
// Tagalog carve-out is the package's own deliberate divergence from it.
//
// The reference memoizes hard (functools.lru_cache on `_resolve_code`,
// langcodes' own _PARSE_CACHE / _DISTANCE_CACHE); closest_lang's
// 7662-candidate scan calls standardize+distance once PER CANDIDATE, so the
// port caches at the same two seams. Caches are pure functions of their
// inputs, so this is memoization, not behavior.
std::string standardize_lang(const std::string& tag) {
    static std::mutex mutex;
    static std::unordered_map<std::string, std::string> cache;
    {
        const std::lock_guard<std::mutex> guard(mutex);
        const auto hit = cache.find(tag);
        if (hit != cache.end()) return hit->second;
    }
    const std::string value = standardize_lang_uncached(tag);
    const std::lock_guard<std::mutex> guard(mutex);
    cache.emplace(tag, value);
    return value;
}

std::string standardize_lang_uncached(const std::string& tag) {
    const std::string smashed = lower_ascii(tag);
    if (smashed == "tl" || smashed == "tgl") return "tl";
    if (auto standardized = langcodes::try_standardize_tag(tag, false)) return *standardized;
    // The reference's fallback when standardize_tag raises or langcodes is
    // absent: underscores to hyphens, primary lower-cased, the rest upper-cased.
    std::string normalized = tag;
    std::replace(normalized.begin(), normalized.end(), '_', '-');
    const auto dash = normalized.find('-');
    if (dash == std::string::npos) return lower_ascii(normalized);
    return lower_ascii(normalized.substr(0, dash)) + "-" +
           upper_ascii(normalized.substr(dash + 1));
}

// ovos_spec_tools.language _with_norm_region.
std::string with_norm_region(const std::string& tag) {
    if (tag.find('-') != std::string::npos) return tag;
    const std::string key = lower_ascii(tag);
    const auto* hit = find_in(_NORM_REGION, sizeof(_NORM_REGION) / sizeof(_NORM_REGION[0]), key);
    if (hit == nullptr) return tag;
    return key + "-" + upper_ascii(hit->second);
}

// ovos_spec_tools.language _coarse_distance: the langcodes-free fallback, whose
// 3 / 5 / 100 are ordering-only numbers (the docstring says so explicitly).
int coarse_distance(const std::string& desired, const std::string& supported) {
    const auto primary = [](const std::string& tag) {
        return lower_ascii(tag.substr(0, tag.find('-')));
    };
    if (primary(desired) != primary(supported)) return 100;   // a different language
    if (desired.find('-') == std::string::npos ||
        supported.find('-') == std::string::npos) return 3;   // the generic form
    return 5;                                                 // two regions, one language
}

// ovos_spec_tools.language lang_distance, memoized per (desired, supported)
// pair at langcodes' own _DISTANCE_CACHE seam.
int lang_distance(const std::string& desired, const std::string& supported) {
    static std::mutex mutex;
    static std::unordered_map<std::string, int> cache;
    const std::string key = desired + '\x01' + supported;
    {
        const std::lock_guard<std::mutex> guard(mutex);
        const auto hit = cache.find(key);
        if (hit != cache.end()) return hit->second;
    }
    const int value = lang_distance_uncached(desired, supported);
    const std::lock_guard<std::mutex> guard(mutex);
    cache.emplace(key, value);
    return value;
}

int lang_distance_uncached(const std::string& desired, const std::string& supported) {
    const std::string a = standardize_lang(desired);
    const std::string b = standardize_lang(supported);
    if (lower_ascii(a) == lower_ascii(b)) return 0;
    const std::string expanded_a = with_norm_region(a);
    const std::string expanded_b = with_norm_region(b);
    if (lower_ascii(expanded_a) == lower_ascii(expanded_b)) return 0;
    // _langcodes_distance retries on the candidate's PRIMARY subtag and yields
    // None only when both attempts raise; the coarse measure is the fallback.
    if (auto direct = langcodes::tag_distance(expanded_a, expanded_b)) return *direct;
    const std::string primary = expanded_b.substr(0, expanded_b.find('-'));
    if (auto via_primary = langcodes::tag_distance(expanded_a, primary)) return *via_primary;
    return coarse_distance(expanded_a, expanded_b);
}

// ovos_spec_tools.language closest_lang: strictly-smaller-wins scan, so a tie
// keeps the FIRST candidate in `available` order; None ("" here) when nothing
// is within max_distance.
std::string closest_lang(const std::string& target,
                         const std::vector<std::string>& available, int max_distance) {
    std::string best;
    bool have_best = false;
    int best_distance = 0;
    for (const auto& candidate : available) {
        const int distance = lang_distance(target, candidate);
        if (!have_best || distance < best_distance) {
            best = candidate;
            best_distance = distance;
            have_best = true;
        }
    }
    if (!have_best) return {};
    if (best_distance <= max_distance) return best;
    return {};
}

// registry.py _resolve_code, in the documented order.
std::string resolve_code(const std::string& code, const std::vector<std::string>& available) {
    constexpr std::size_t alias_count = sizeof(_ALIASES) / sizeof(_ALIASES[0]);
    constexpr std::size_t bare_count = sizeof(_BARE_DEFAULTS) / sizeof(_BARE_DEFAULTS[0]);
    constexpr std::size_t region_count = sizeof(_REGION_DEFAULTS) / sizeof(_REGION_DEFAULTS[0]);

    if (const auto* hit = find_in(_ALIASES, alias_count, code)) return hit->second;

    // Step 2: standardize, but NEVER a private-use tag — langcodes ignores
    // private-use content when it measures distance, and standardizing `x-…`
    // would rewrite a lect subtag into something the catalog does not have.
    std::string working = code;
    if (working.find("-x-") == std::string::npos &&
        working.rfind("x-", 0) != 0) {
        if (auto standardized = langcodes::try_standardize_tag(working, true)) working = *standardized;
        if (const auto* hit = find_in(_ALIASES, alias_count, working)) return hit->second;
    }

    // Step 3: an exact hit in the catalog. available_json_codes() is the
    // reference's `available`, and it is unordered set-like data — an exact
    // match is returned verbatim.
    if (std::find(available.begin(), available.end(), working) != available.end())
        return working;

    // Step 4: private-use case fold. BCP-47 tags are case-insensitive and
    // langcodes never saw this tag, so fold it by hand: otherwise
    // `ar-sa-x-najd` misses `ar-SA-x-najd` and the distance step lands it on a
    // sibling lect. Aliases are tried before catalog codes, as in the reference.
    if (working.find("-x-") != std::string::npos) {
        const std::string folded = lower_ascii(working);
        for (std::size_t i = 0; i < alias_count; ++i)
            if (lower_ascii(_ALIASES[i].first) == folded) return _ALIASES[i].second;
        for (const auto& known : available)
            if (lower_ascii(known) == folded) return known;
    }

    if (const auto* hit = find_in(_BARE_DEFAULTS, bare_count, working)) return hit->second;
    if (const auto* hit = find_in(_REGION_DEFAULTS, region_count, working)) return hit->second;

    const std::string match = closest_lang(working, available, DEFAULT_MAX_LANGUAGE_DISTANCE);
    if (!match.empty()) return match;
    return working;
}

} // namespace orthography2ipa::registry
