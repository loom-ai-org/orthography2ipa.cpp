// Direct port of linguatag: standardize.py, regions.py, lects.py and
// matching.py, in the source's order. The tables are GENERATED from the
// installed linguatag (tests/gen_registry_tables.py ->
// src/linguatag_tables.inc). Python's str methods are Unicode-aware, so the
// subtag tests (isalpha, isdigit, len) and the case maps run on code points.
#include "orthography2ipa/linguatag.hpp"

#include "orthography2ipa/langcodes.hpp"
#include "unicode_util.hpp"

#include <unicode/locid.h>
#include <unicode/uchar.h>
#include <unicode/unistr.h>

#include <algorithm>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace orthography2ipa::linguatag {
namespace {

#include "linguatag_tables.inc"

using Map = std::unordered_map<std::string, std::string>;

template <std::size_t N>
Map to_map(const std::pair<const char*, const char*> (&table)[N]) {
    Map out;
    for (const auto& [key, value] : table) out.emplace(key, value);
    return out;
}

const Map& likely_regions() {
    static const Map regions = to_map(LIKELY_REGIONS);
    return regions;
}

const Map& lect_parents() {
    static const Map parents = to_map(LECT_PARENTS);
    return parents;
}

template <std::size_t N>
bool listed(const char* const (&table)[N], const std::string& key) {
    return std::find_if(std::begin(table), std::end(table),
                        [&](const char* entry) { return key == entry; }) != std::end(table);
}

std::vector<std::string> split(const std::string& text, char separator = '-') {
    std::vector<std::string> parts;
    for (std::size_t start = 0;;) {
        const auto at = text.find(separator, start);
        parts.push_back(text.substr(start, at - start));
        if (at == std::string::npos) return parts;
        start = at + 1;
    }
}

std::string join(const std::vector<std::string>& parts, std::size_t begin, std::size_t end) {
    std::string out;
    for (std::size_t i = begin; i < end; ++i) {
        if (i > begin) out += '-';
        out += parts[i];
    }
    return out;
}

std::string py_lower(const std::string& text) {
    return uni::to_utf8(uni::lower(uni::to_utf32(text)));
}

std::string icu_to_utf8(const icu::UnicodeString& value) {
    std::string out;
    value.toUTF8String(out);
    return out;
}

// str.upper(): full case mapping, root locale.
std::string py_upper(const std::string& text) {
    icu::UnicodeString value = icu::UnicodeString::fromUTF8(text);
    value.toUpper(icu::Locale::getRoot());
    return icu_to_utf8(value);
}

// str.title(): a cased character after a cased one is lowercased, any other
// character titlecased.
std::string py_title(const std::string& text) {
    std::string out;
    bool previous_cased = false;
    for (char32_t cp : uni::to_utf32(text)) {
        icu::UnicodeString one(static_cast<UChar32>(cp));
        if (previous_cased) one.toLower(icu::Locale::getRoot());
        else one.toTitle(nullptr, icu::Locale::getRoot(),
                         U_TITLECASE_NO_LOWERCASE | U_TITLECASE_NO_BREAK_ADJUSTMENT);
        out += icu_to_utf8(one);
        previous_cased = u_hasBinaryProperty(static_cast<UChar32>(cp), UCHAR_CASED);
    }
    return out;
}

std::size_t py_len(const std::string& text) { return uni::to_utf32(text).size(); }

bool py_isalpha(const std::string& text) {
    const std::u32string units = uni::to_utf32(text);
    return !units.empty() && std::all_of(units.begin(), units.end(), uni::is_alpha);
}

// str.isdigit(): every character has Numeric_Type Digit or Decimal.
bool py_isdigit(const std::string& text) {
    const std::u32string units = uni::to_utf32(text);
    return !units.empty() && std::all_of(units.begin(), units.end(), [](char32_t cp) {
        const auto type = u_getIntPropertyValue(static_cast<UChar32>(cp), UCHAR_NUMERIC_TYPE);
        return type == U_NT_DECIMAL || type == U_NT_DIGIT;
    });
}

// str.casefold() for the comparison closest_lang makes. Tags are ASCII
// whenever langcodes accepted them, and the case fold is then lowercasing.
std::string py_casefold(const std::string& text) {
    icu::UnicodeString value = icu::UnicodeString::fromUTF8(text);
    value.foldCase();
    return icu_to_utf8(value);
}

// ── standardize.py ──────────────────────────────────────────────────────────

std::string case_subtag(std::size_t index, const std::string& subtag, bool after_singleton) {
    if (index == 0 || after_singleton) return py_lower(subtag);
    const std::size_t length = py_len(subtag);
    if (length == 4 && py_isalpha(subtag)) return py_title(subtag);
    if (length == 2 && py_isalpha(subtag)) return py_upper(subtag);
    return py_lower(subtag);
}

// _fold: BCP-47 case conventions without langcodes.
std::string fold(const std::string& tag) {
    std::string hyphenated = tag;
    std::replace(hyphenated.begin(), hyphenated.end(), '_', '-');
    std::vector<std::string> out;
    bool after_singleton = false;
    const auto subtags = split(hyphenated);
    for (std::size_t index = 0; index < subtags.size(); ++index) {
        after_singleton = after_singleton || (index > 0 && py_len(subtags[index]) == 1);
        out.push_back(case_subtag(index, subtags[index], after_singleton));
    }
    return join(out, 0, out.size());
}

std::string standardize_uncached(const std::string& tag) {
    const std::string lowered = py_lower(tag);
    if (lowered == "tl" || lowered == "tgl") return "tl";
    if (auto standardized = langcodes::try_standardize_tag(tag, false)) return *standardized;
    return fold(tag);
}

// ── regions.py ──────────────────────────────────────────────────────────────

bool is_script(const std::string& subtag) { return py_len(subtag) == 4 && py_isalpha(subtag); }

bool is_region(const std::string& subtag) {
    const std::size_t length = py_len(subtag);
    return (length == 2 && py_isalpha(subtag)) || (length == 3 && py_isdigit(subtag));
}

struct Split {
    std::string language;
    std::optional<std::string> script;
    std::optional<std::string> region;
    bool extended = false;
};

Split split_tag(const std::string& tag) {
    const auto parts = split(tag);
    Split out;
    out.language = parts[0];
    std::size_t index = 1;
    out.extended = index < parts.size() && py_len(parts[index]) == 3 && py_isalpha(parts[index]);
    if (index < parts.size() && is_script(parts[index])) out.script = parts[index++];
    if (index < parts.size() && is_region(parts[index])) out.region = parts[index];
    return out;
}

// ── lects.py ────────────────────────────────────────────────────────────────

std::vector<std::string> ancestors(const std::string& tag) {
    const Map& parents = lect_parents();
    std::vector<std::string> chain{tag};
    while (chain.size() <= parents.size()) {
        const std::string node = chain.back();
        const auto parent = parents.find(node);
        if (parent != parents.end()) {
            chain.push_back(parent->second);
            continue;
        }
        const std::string base = without_private_use(node);
        if (base != node) chain.push_back(base);
        else break;
    }
    for (auto& node : chain) node = py_lower(node);
    return chain;
}

// ── matching.py ─────────────────────────────────────────────────────────────

bool is_simplified(const std::string& tag) {
    const auto parsed = langcodes::parse(tag);
    if (!parsed) return false;
    const auto maximized = langcodes::try_maximize(parsed.lang);
    return maximized && maximized->script == "Hans";
}

std::string parseable_prefix_uncached(const std::string& tag) {
    const auto parts = split(tag);
    for (std::size_t end = parts.size(); end > 0; --end) {
        const std::string candidate = join(parts, 0, end);
        if (langcodes::parse(candidate)) return candidate;
    }
    return tag;
}

std::string parseable_prefix(const std::string& tag) {
    static std::mutex mutex;
    static Map cache;
    {
        const std::lock_guard<std::mutex> guard(mutex);
        const auto hit = cache.find(tag);
        if (hit != cache.end()) return hit->second;
    }
    const std::string value = parseable_prefix_uncached(tag);
    const std::lock_guard<std::mutex> guard(mutex);
    cache.emplace(tag, value);
    return value;
}

std::string primary_lower(const std::string& tag) {
    return py_lower(tag.substr(0, tag.find('-')));
}

bool cantonese_against_mandarin(const std::string& a, const std::string& b) {
    // A dict keyed by primary subtag: when both share one, b's entry wins.
    Map languages{{primary_lower(a), a}};
    languages[primary_lower(b)] = b;
    if (languages.count(CANTONESE) == 0) return false;
    if (languages.count(MANDARIN) != 0) return true;
    const auto chinese = languages.find(CHINESE);
    return chinese != languages.end() && is_simplified(chinese->second);
}

int coarse_distance(const std::string& desired_tag, const std::string& supported_tag) {
    const std::string desired = without_private_use(desired_tag);
    const std::string supported = without_private_use(supported_tag);
    if (py_lower(desired) == py_lower(supported)) return 0;
    if (primary_lower(desired) != primary_lower(supported)) return 100;
    if (desired.find('-') == std::string::npos || supported.find('-') == std::string::npos)
        return 3;
    return 5;
}

// _as_world_arabic: every spelling of Modern Standard Arabic as ar-001, its
// private-use part kept.
std::string as_world_arabic(const std::string& tag) {
    const auto at = tag.find("-x-");
    const std::string base = tag.substr(0, at);
    if (!listed(MSA, py_lower(base))) return tag;
    return at == std::string::npos ? std::string("ar-001") : "ar-001" + tag.substr(at);
}

bool has_empty_subtag(const std::string& tag) {
    const auto parts = split(tag);
    return std::find(parts.begin(), parts.end(), std::string()) != parts.end();
}

int language_distance(const std::string& desired, const std::string& supported) {
    std::string a = standardize_lang(desired);
    std::string b = standardize_lang(supported);
    if (has_empty_subtag(a) || has_empty_subtag(b)) return DIFFERENT_LANGUAGE_DISTANCE;
    a = as_world_arabic(parseable_prefix(a));
    b = as_world_arabic(parseable_prefix(b));
    if (py_lower(a) == py_lower(b)) return 0;
    const bool to_msa = listed(MSA, py_lower(without_private_use(b)));
    a = with_default_region(a);
    b = with_default_region(b);
    if (py_lower(a) == py_lower(b)) return 0;
    int distance;
    if (auto measured = langcodes::tag_distance(a, b)) distance = *measured;
    else distance = coarse_distance(a, b);
    if (cantonese_against_mandarin(a, b))
        distance = std::max(distance, DIFFERENT_LANGUAGE_DISTANCE);
    std::vector<std::string> scripts;
    const auto subtags = split(without_private_use(a));
    for (std::size_t i = 1; i < subtags.size(); ++i)
        if (py_len(subtags[i]) == 4 && py_isalpha(subtags[i])) scripts.push_back(py_lower(subtags[i]));
    if (to_msa && listed(ARABIC_VARIETIES, primary_lower(a)) &&
        (scripts.empty() || (scripts.size() == 1 && scripts.front() == "arab")))
        distance = std::min(distance, ARABIC_FALLBACK_DISTANCE);
    return distance;
}

// The source memoizes standardize_lang and _parseable_prefix; the distance is
// a pure function of the two tags, so it is memoized at the same seam.
int cached_language_distance(const std::string& desired, const std::string& supported) {
    static std::mutex mutex;
    static std::unordered_map<std::string, int> cache;
    const std::string key = desired + '\x01' + supported;
    {
        const std::lock_guard<std::mutex> guard(mutex);
        const auto hit = cache.find(key);
        if (hit != cache.end()) return hit->second;
    }
    const int value = language_distance(desired, supported);
    const std::lock_guard<std::mutex> guard(mutex);
    cache.emplace(key, value);
    return value;
}

int with_lects(int distance, const std::string& desired, const std::string& supported,
               Lects lects) {
    if (lects == Lects::none) return distance;
    const int ceiling = std::max(distance, DEFAULT_MAX_LANGUAGE_DISTANCE);
    return std::min(distance + lect_score(desired, supported, lects), ceiling);
}

} // namespace

const int default_max_language_distance = DEFAULT_MAX_LANGUAGE_DISTANCE;

std::string standardize_lang(const std::string& tag) {
    static std::mutex mutex;
    static Map cache;
    {
        const std::lock_guard<std::mutex> guard(mutex);
        const auto hit = cache.find(tag);
        if (hit != cache.end()) return hit->second;
    }
    const std::string value = standardize_uncached(tag);
    const std::lock_guard<std::mutex> guard(mutex);
    cache.emplace(tag, value);
    return value;
}

std::optional<std::string> default_region(const std::string& tag) {
    const Split parts = split_tag(standardize_lang(tag));
    if (parts.region) return parts.region;
    if (parts.extended) return std::nullopt;
    const Map& regions = likely_regions();
    if (parts.script) {
        const auto hit = regions.find(parts.language + "-" + *parts.script);
        if (hit != regions.end()) return hit->second;
    }
    for (const auto& entry : OVERRIDES) {
        if (parts.language != entry.language) continue;
        if (!parts.script || *parts.script == entry.script) return std::string(entry.region);
        break;
    }
    const auto hit = regions.find(parts.language);
    if (hit == regions.end()) return std::nullopt;
    return hit->second;
}

std::string with_default_region(const std::string& tag_in) {
    const std::string tag = standardize_lang(tag_in);
    const Split parts = split_tag(tag);
    if (parts.region) return tag;
    const auto region = default_region(tag);
    if (!region) return tag;
    auto subtags = split(tag);
    const std::size_t at = parts.script ? 2 : 1;
    subtags.insert(subtags.begin() + static_cast<std::ptrdiff_t>(std::min(at, subtags.size())),
                   *region);
    return join(subtags, 0, subtags.size());
}

std::vector<std::string> private_use(const std::string& tag) {
    const auto parts = split(py_lower(standardize_lang(tag)));
    const auto x = std::find(parts.begin(), parts.end(), "x");
    if (x == parts.end()) return {};
    return {x + 1, parts.end()};
}

std::string without_private_use(const std::string& tag_in) {
    const std::string tag = standardize_lang(tag_in);
    const auto at = tag.find("-x-");
    return at == std::string::npos ? tag : tag.substr(0, at);
}

std::optional<int> lect_steps(const std::string& a, const std::string& b) {
    const auto up_a = ancestors(standardize_lang(a));
    const auto up_b = ancestors(standardize_lang(b));
    for (std::size_t steps_a = 0; steps_a < up_a.size(); ++steps_a) {
        const auto hit = std::find(up_b.begin(), up_b.end(), up_a[steps_a]);
        if (hit != up_b.end()) return static_cast<int>(steps_a + (hit - up_b.begin()));
    }
    return std::nullopt;
}

int lect_score(const std::string& desired, const std::string& supported, Lects mode) {
    const auto wanted = private_use(desired);
    const auto offered = private_use(supported);
    if (wanted == offered) return 0;
    const auto steps = lect_steps(desired, supported);
    const int near = steps ? std::min(*steps, LECT_FAR) : LECT_FAR;
    if (!wanted.empty() && offered.empty())
        return mode == Lects::dialect ? LECT_STANDARD_LAST : 1;
    if (!offered.empty() && wanted.empty()) return 1 + near;
    return mode == Lects::dialect ? near : 1 + near;
}

int lang_distance(const std::string& desired, const std::string& supported, Lects lects) {
    return with_lects(cached_language_distance(desired, supported), desired, supported, lects);
}

std::optional<std::string> closest_lang(const std::string& target,
                                        const std::vector<std::string>& available,
                                        int max_distance, Lects lects) {
    const std::string wanted = py_casefold(standardize_lang(target));
    std::optional<std::string> best;
    std::pair<int, bool> best_rank{};
    for (const auto& candidate : available) {
        const int distance = cached_language_distance(target, candidate);
        if (distance > max_distance) continue;
        const std::pair<int, bool> rank{
            with_lects(distance, target, candidate, lects),
            py_casefold(standardize_lang(candidate)) != wanted};
        if (!best || rank < best_rank) {
            best = candidate;
            best_rank = rank;
        }
    }
    return best;
}

} // namespace orthography2ipa::linguatag
