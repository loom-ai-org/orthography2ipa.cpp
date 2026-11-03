// GENERATED C++ data tables translated from orthography2ipa/feats.py --
// the curated distinctive-feature matrix (pyphone-derived, MIT license)
// and the diacritic-modifier table. Each vector is indexed by the feature
// layout documented in feats.py; UNKNOWN is Python None, FALSE is Python
// False, TRUE is Python True.
#include "orthography2ipa/feats.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace orthography2ipa::feats {
namespace {

struct FeatVec {
    TriState v[NUM_FEATURES];
};

const std::unordered_map<std::string, FeatVec>& phone_table() {
    static const std::unordered_map<std::string, FeatVec> table = {
        {{"a"}, {TRUE, TRUE, FALSE, TRUE, FALSE, FALSE, FALSE, UNKNOWN, TRUE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, TRUE, TRUE, FALSE, TRUE, FALSE, FALSE, FALSE}},
        {{"b"}, {FALSE, FALSE, TRUE, FALSE, FALSE, FALSE, FALSE, UNKNOWN, TRUE, FALSE, FALSE, TRUE, FALSE, UNKNOWN, TRUE, FALSE, FALSE, FALSE, FALSE, UNKNOWN, FALSE, FALSE, FALSE}},
        {{"c"}, {FALSE, FALSE, TRUE, FALSE, FALSE, FALSE, FALSE, UNKNOWN, FALSE, FALSE, FALSE, FALSE, FALSE, UNKNOWN, FALSE, TRUE, FALSE, FALSE, FALSE, UNKNOWN, FALSE, FALSE, FALSE}},
        {{"d"}, {FALSE, FALSE, TRUE, FALSE, FALSE, FALSE, FALSE, FALSE, TRUE, FALSE, FALSE, TRUE, TRUE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, UNKNOWN, FALSE, FALSE, FALSE}},
        {{"e"}, {TRUE, TRUE, FALSE, TRUE, FALSE, FALSE, FALSE, UNKNOWN, TRUE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, TRUE, FALSE, FALSE, FALSE}},
        {{"f"}, {FALSE, FALSE, TRUE, TRUE, FALSE, FALSE, FALSE, UNKNOWN, FALSE, FALSE, FALSE, TRUE, FALSE, UNKNOWN, TRUE, FALSE, FALSE, FALSE, FALSE, UNKNOWN, FALSE, FALSE, FALSE}},
        {{"h"}, {FALSE, TRUE, TRUE, TRUE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, UNKNOWN, FALSE, FALSE, FALSE, FALSE, FALSE, UNKNOWN, FALSE, FALSE, FALSE}},
        {{"i"}, {TRUE, TRUE, FALSE, TRUE, FALSE, FALSE, FALSE, UNKNOWN, TRUE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, TRUE, FALSE, FALSE, FALSE, TRUE, FALSE, FALSE, FALSE}},
        {{"j"}, {FALSE, TRUE, FALSE, TRUE, FALSE, FALSE, FALSE, UNKNOWN, TRUE, FALSE, FALSE, FALSE, FALSE, UNKNOWN, FALSE, TRUE, FALSE, FALSE, FALSE, UNKNOWN, FALSE, FALSE, FALSE}},
        {{"k"}, {FALSE, FALSE, TRUE, FALSE, FALSE, FALSE, FALSE, UNKNOWN, FALSE, FALSE, FALSE, FALSE, FALSE, UNKNOWN, FALSE, TRUE, FALSE, TRUE, FALSE, UNKNOWN, FALSE, FALSE, FALSE}},
        {{"k\u02bc"}, {FALSE, FALSE, TRUE, FALSE, FALSE, FALSE, FALSE, UNKNOWN, FALSE, FALSE, TRUE, FALSE, FALSE, UNKNOWN, FALSE, TRUE, FALSE, TRUE, FALSE, UNKNOWN, FALSE, FALSE, FALSE}},
        {{"l"}, {FALSE, TRUE, TRUE, TRUE, FALSE, TRUE, FALSE, FALSE, TRUE, FALSE, FALSE, TRUE, TRUE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, UNKNOWN, FALSE, FALSE, FALSE}},
        {{"m"}, {FALSE, TRUE, TRUE, FALSE, FALSE, FALSE, TRUE, UNKNOWN, TRUE, FALSE, FALSE, TRUE, FALSE, UNKNOWN, TRUE, FALSE, FALSE, FALSE, FALSE, UNKNOWN, FALSE, FALSE, FALSE}},
        {{"n"}, {FALSE, TRUE, TRUE, FALSE, FALSE, FALSE, TRUE, FALSE, TRUE, FALSE, FALSE, TRUE, TRUE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, UNKNOWN, FALSE, FALSE, FALSE}},
        {{"o"}, {TRUE, TRUE, FALSE, TRUE, FALSE, FALSE, FALSE, UNKNOWN, TRUE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, TRUE, TRUE, TRUE, FALSE, FALSE, FALSE}},
        {{"p"}, {FALSE, FALSE, TRUE, FALSE, FALSE, FALSE, FALSE, UNKNOWN, FALSE, FALSE, FALSE, TRUE, FALSE, UNKNOWN, TRUE, FALSE, FALSE, FALSE, FALSE, UNKNOWN, FALSE, FALSE, FALSE}},
        {{"p\u02bc"}, {FALSE, FALSE, TRUE, FALSE, FALSE, FALSE, FALSE, UNKNOWN, FALSE, FALSE, TRUE, TRUE, FALSE, UNKNOWN, TRUE, FALSE, FALSE, FALSE, FALSE, UNKNOWN, FALSE, FALSE, FALSE}},
        {{"q"}, {FALSE, FALSE, TRUE, FALSE, FALSE, FALSE, FALSE, UNKNOWN, FALSE, FALSE, FALSE, FALSE, FALSE, UNKNOWN, FALSE, FALSE, FALSE, TRUE, FALSE, UNKNOWN, FALSE, FALSE, FALSE}},
        {{"q\u02bc"}, {FALSE, FALSE, TRUE, FALSE, FALSE, FALSE, FALSE, UNKNOWN, FALSE, FALSE, TRUE, FALSE, FALSE, UNKNOWN, FALSE, FALSE, FALSE, TRUE, FALSE, UNKNOWN, FALSE, FALSE, FALSE}},
        {{"r"}, {FALSE, TRUE, TRUE, TRUE, UNKNOWN, FALSE, FALSE, FALSE, TRUE, FALSE, FALSE, TRUE, TRUE, FALSE, FALSE, UNKNOWN, UNKNOWN, UNKNOWN, FALSE, UNKNOWN, FALSE, FALSE, FALSE}},
        {{"s"}, {FALSE, FALSE, TRUE, TRUE, FALSE, FALSE, FALSE, TRUE, FALSE, FALSE, FALSE, TRUE, TRUE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, UNKNOWN, FALSE, FALSE, FALSE}},
        {{"t"}, {FALSE, FALSE, TRUE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, TRUE, TRUE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, UNKNOWN, FALSE, FALSE, FALSE}},
        {{"ts\u02bc"}, {FALSE, FALSE, TRUE, FALSE, TRUE, FALSE, FALSE, TRUE, FALSE, FALSE, TRUE, TRUE, TRUE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, UNKNOWN, FALSE, FALSE, FALSE}},
        {{"t\u0283\u02bc"}, {FALSE, FALSE, TRUE, FALSE, TRUE, FALSE, FALSE, TRUE, FALSE, FALSE, TRUE, FALSE, TRUE, TRUE, FALSE, FALSE, FALSE, FALSE, FALSE, UNKNOWN, FALSE, FALSE, FALSE}},
        {{"t\u02bc"}, {FALSE, FALSE, TRUE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, TRUE, TRUE, TRUE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, UNKNOWN, FALSE, FALSE, FALSE}},
        {{"u"}, {TRUE, TRUE, FALSE, TRUE, FALSE, FALSE, FALSE, UNKNOWN, TRUE, FALSE, FALSE, FALSE, FALSE, FALSE, TRUE, TRUE, FALSE, TRUE, TRUE, TRUE, FALSE, FALSE, FALSE}},
        {{"v"}, {FALSE, FALSE, TRUE, TRUE, FALSE, FALSE, FALSE, UNKNOWN, TRUE, FALSE, FALSE, TRUE, FALSE, UNKNOWN, TRUE, FALSE, FALSE, FALSE, FALSE, UNKNOWN, FALSE, FALSE, FALSE}},
        {{"w"}, {FALSE, TRUE, FALSE, TRUE, FALSE, FALSE, FALSE, UNKNOWN, TRUE, FALSE, FALSE, FALSE, FALSE, UNKNOWN, TRUE, TRUE, FALSE, TRUE, TRUE, UNKNOWN, FALSE, FALSE, FALSE}},
        {{"x"}, {FALSE, FALSE, TRUE, TRUE, FALSE, FALSE, FALSE, UNKNOWN, FALSE, FALSE, FALSE, FALSE, FALSE, UNKNOWN, FALSE, TRUE, FALSE, TRUE, FALSE, UNKNOWN, FALSE, FALSE, FALSE}},
        {{"y"}, {TRUE, TRUE, FALSE, TRUE, FALSE, FALSE, FALSE, UNKNOWN, TRUE, FALSE, FALSE, FALSE, FALSE, FALSE, TRUE, TRUE, FALSE, FALSE, TRUE, TRUE, FALSE, FALSE, FALSE}},
        {{"z"}, {FALSE, FALSE, TRUE, TRUE, FALSE, FALSE, FALSE, TRUE, TRUE, FALSE, FALSE, TRUE, TRUE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, UNKNOWN, FALSE, FALSE, FALSE}},
        {{"\u00e3"}, {TRUE, TRUE, FALSE, TRUE, FALSE, FALSE, TRUE, UNKNOWN, TRUE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, TRUE, TRUE, FALSE, TRUE, FALSE, FALSE, TRUE}},
        {{"\u00e6"}, {TRUE, TRUE, FALSE, TRUE, FALSE, FALSE, FALSE, UNKNOWN, TRUE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, TRUE, FALSE, FALSE, TRUE, FALSE, FALSE, FALSE}},
        {{"\u00e7"}, {FALSE, FALSE, TRUE, TRUE, FALSE, FALSE, FALSE, UNKNOWN, FALSE, FALSE, FALSE, FALSE, FALSE, UNKNOWN, FALSE, TRUE, FALSE, FALSE, FALSE, UNKNOWN, FALSE, FALSE, FALSE}},
        {{"\u00f0"}, {FALSE, FALSE, TRUE, TRUE, FALSE, FALSE, FALSE, FALSE, TRUE, FALSE, FALSE, TRUE, TRUE, TRUE, FALSE, FALSE, FALSE, FALSE, FALSE, UNKNOWN, FALSE, FALSE, FALSE}},
        {{"\u00f5"}, {TRUE, TRUE, FALSE, TRUE, FALSE, FALSE, TRUE, UNKNOWN, TRUE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, TRUE, TRUE, TRUE, FALSE, FALSE, TRUE}},
        {{"\u00f8"}, {TRUE, TRUE, FALSE, TRUE, FALSE, FALSE, FALSE, UNKNOWN, TRUE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, TRUE, TRUE, FALSE, FALSE, FALSE}},
        {{"\u0127"}, {FALSE, FALSE, TRUE, TRUE, FALSE, FALSE, FALSE, UNKNOWN, FALSE, FALSE, FALSE, FALSE, FALSE, UNKNOWN, FALSE, FALSE, TRUE, TRUE, FALSE, UNKNOWN, FALSE, FALSE, FALSE}},
        {{"\u0129"}, {TRUE, TRUE, FALSE, TRUE, FALSE, FALSE, TRUE, UNKNOWN, TRUE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, TRUE, FALSE, FALSE, FALSE, TRUE, FALSE, FALSE, TRUE}},
        {{"\u014b"}, {FALSE, TRUE, TRUE, FALSE, FALSE, FALSE, TRUE, UNKNOWN, TRUE, FALSE, FALSE, FALSE, FALSE, UNKNOWN, FALSE, TRUE, FALSE, TRUE, FALSE, UNKNOWN, FALSE, FALSE, FALSE}},
        {{"\u0153"}, {TRUE, TRUE, FALSE, TRUE, FALSE, FALSE, FALSE, UNKNOWN, TRUE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, TRUE, FALSE, FALSE, FALSE, FALSE}},
        {{"\u0169"}, {TRUE, TRUE, FALSE, TRUE, FALSE, FALSE, TRUE, UNKNOWN, TRUE, FALSE, FALSE, FALSE, FALSE, FALSE, TRUE, TRUE, FALSE, TRUE, TRUE, TRUE, FALSE, FALSE, TRUE}},
        {{"\u01c0"}, {FALSE, FALSE, TRUE, FALSE, FALSE, FALSE, FALSE, UNKNOWN, FALSE, FALSE, FALSE, TRUE, TRUE, TRUE, FALSE, FALSE, FALSE, FALSE, FALSE, UNKNOWN, FALSE, TRUE, FALSE}},
        {{"\u01c1"}, {FALSE, FALSE, TRUE, FALSE, FALSE, TRUE, FALSE, UNKNOWN, FALSE, FALSE, FALSE, TRUE, TRUE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, UNKNOWN, FALSE, TRUE, FALSE}},
        {{"\u01c2"}, {FALSE, FALSE, TRUE, FALSE, FALSE, FALSE, FALSE, UNKNOWN, FALSE, FALSE, FALSE, FALSE, TRUE, TRUE, FALSE, TRUE, FALSE, FALSE, FALSE, UNKNOWN, FALSE, TRUE, FALSE}},
        {{"\u01c3"}, {FALSE, FALSE, TRUE, FALSE, FALSE, FALSE, FALSE, UNKNOWN, FALSE, FALSE, FALSE, TRUE, TRUE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, UNKNOWN, FALSE, TRUE, FALSE}},
        {{"\u0250"}, {TRUE, TRUE, FALSE, TRUE, FALSE, FALSE, FALSE, UNKNOWN, TRUE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, TRUE, FALSE, TRUE, FALSE, FALSE, FALSE}},
        {{"\u0251"}, {TRUE, TRUE, FALSE, TRUE, UNKNOWN, FALSE, FALSE, UNKNOWN, TRUE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, TRUE, TRUE, FALSE, TRUE, FALSE, FALSE, FALSE}},
        {{"\u0252"}, {TRUE, TRUE, FALSE, TRUE, FALSE, FALSE, FALSE, UNKNOWN, TRUE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, TRUE, TRUE, TRUE, TRUE, FALSE, FALSE, FALSE}},
        {{"\u0253"}, {FALSE, FALSE, TRUE, FALSE, FALSE, FALSE, FALSE, UNKNOWN, TRUE, FALSE, TRUE, TRUE, FALSE, UNKNOWN, TRUE, FALSE, FALSE, FALSE, FALSE, UNKNOWN, FALSE, FALSE, FALSE}},
        {{"\u0254"}, {TRUE, TRUE, FALSE, TRUE, FALSE, FALSE, FALSE, UNKNOWN, TRUE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, TRUE, TRUE, FALSE, FALSE, FALSE, FALSE}},
        {{"\u0255"}, {FALSE, FALSE, TRUE, TRUE, TRUE, FALSE, FALSE, TRUE, FALSE, FALSE, FALSE, TRUE, TRUE, TRUE, FALSE, TRUE, FALSE, FALSE, FALSE, UNKNOWN, FALSE, FALSE, FALSE}},
        {{"\u0256"}, {FALSE, FALSE, TRUE, FALSE, FALSE, FALSE, FALSE, UNKNOWN, TRUE, FALSE, FALSE, FALSE, TRUE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, UNKNOWN, FALSE, FALSE, FALSE}},
        {{"\u0257"}, {FALSE, FALSE, TRUE, FALSE, FALSE, FALSE, FALSE, FALSE, TRUE, FALSE, TRUE, TRUE, TRUE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, UNKNOWN, FALSE, FALSE, FALSE}},
        {{"\u0258"}, {TRUE, TRUE, FALSE, TRUE, UNKNOWN, FALSE, FALSE, UNKNOWN, TRUE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, TRUE, FALSE, FALSE, FALSE}},
        {{"\u0259"}, {TRUE, TRUE, FALSE, TRUE, FALSE, FALSE, FALSE, UNKNOWN, TRUE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, TRUE, FALSE, FALSE, FALSE, FALSE, FALSE}},
        {{"\u025b"}, {TRUE, TRUE, FALSE, TRUE, FALSE, FALSE, FALSE, UNKNOWN, TRUE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE}},
        {{"\u025c"}, {TRUE, TRUE, FALSE, TRUE, FALSE, FALSE, FALSE, UNKNOWN, TRUE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, TRUE, FALSE, TRUE, FALSE, FALSE, FALSE}},
        {{"\u025e"}, {TRUE, TRUE, FALSE, TRUE, UNKNOWN, FALSE, FALSE, UNKNOWN, TRUE, FALSE, FALSE, FALSE, FALSE, FALSE, TRUE, FALSE, FALSE, FALSE, TRUE, FALSE, FALSE, FALSE, FALSE}},
        {{"\u025f"}, {FALSE, FALSE, TRUE, FALSE, FALSE, FALSE, FALSE, UNKNOWN, TRUE, FALSE, FALSE, FALSE, FALSE, UNKNOWN, FALSE, TRUE, FALSE, FALSE, FALSE, UNKNOWN, FALSE, FALSE, FALSE}},
        {{"\u0260"}, {FALSE, FALSE, TRUE, FALSE, FALSE, FALSE, FALSE, UNKNOWN, TRUE, FALSE, TRUE, FALSE, FALSE, UNKNOWN, FALSE, TRUE, FALSE, TRUE, FALSE, UNKNOWN, FALSE, FALSE, FALSE}},
        {{"\u0261"}, {FALSE, FALSE, TRUE, FALSE, FALSE, FALSE, FALSE, UNKNOWN, TRUE, FALSE, FALSE, FALSE, FALSE, UNKNOWN, FALSE, TRUE, FALSE, TRUE, FALSE, UNKNOWN, FALSE, FALSE, FALSE}},
        {{"\u0262"}, {FALSE, FALSE, TRUE, FALSE, FALSE, FALSE, FALSE, UNKNOWN, TRUE, FALSE, FALSE, FALSE, FALSE, UNKNOWN, FALSE, FALSE, FALSE, TRUE, FALSE, UNKNOWN, FALSE, FALSE, FALSE}},
        {{"\u0263"}, {FALSE, FALSE, TRUE, TRUE, FALSE, FALSE, FALSE, UNKNOWN, TRUE, FALSE, FALSE, FALSE, FALSE, UNKNOWN, FALSE, TRUE, FALSE, TRUE, FALSE, UNKNOWN, FALSE, FALSE, FALSE}},
        {{"\u0264"}, {TRUE, TRUE, FALSE, TRUE, UNKNOWN, FALSE, FALSE, UNKNOWN, TRUE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, TRUE, FALSE, TRUE, FALSE, FALSE, FALSE}},
        {{"\u0265"}, {FALSE, TRUE, FALSE, TRUE, UNKNOWN, FALSE, FALSE, UNKNOWN, TRUE, FALSE, FALSE, UNKNOWN, FALSE, UNKNOWN, TRUE, TRUE, FALSE, FALSE, TRUE, TRUE, FALSE, FALSE, FALSE}},
        {{"\u0266"}, {FALSE, TRUE, TRUE, TRUE, FALSE, FALSE, FALSE, UNKNOWN, FALSE, FALSE, FALSE, FALSE, FALSE, UNKNOWN, FALSE, FALSE, FALSE, FALSE, FALSE, UNKNOWN, FALSE, FALSE, FALSE}},
        {{"\u0267"}, {FALSE, FALSE, TRUE, TRUE, TRUE, FALSE, FALSE, UNKNOWN, FALSE, FALSE, FALSE, FALSE, TRUE, TRUE, FALSE, TRUE, FALSE, UNKNOWN, FALSE, UNKNOWN, FALSE, FALSE, FALSE}},
        {{"\u0268"}, {TRUE, TRUE, FALSE, TRUE, FALSE, FALSE, FALSE, UNKNOWN, TRUE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, TRUE, FALSE, TRUE, FALSE, TRUE, FALSE, FALSE, FALSE}},
        {{"\u026a"}, {TRUE, TRUE, FALSE, TRUE, FALSE, FALSE, FALSE, UNKNOWN, TRUE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, TRUE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE}},
        {{"\u026b"}, {FALSE, TRUE, TRUE, TRUE, UNKNOWN, TRUE, FALSE, FALSE, TRUE, FALSE, FALSE, TRUE, TRUE, FALSE, FALSE, FALSE, FALSE, TRUE, FALSE, UNKNOWN, FALSE, FALSE, FALSE}},
        {{"\u026c"}, {FALSE, FALSE, TRUE, TRUE, TRUE, TRUE, FALSE, FALSE, FALSE, FALSE, FALSE, TRUE, TRUE, FALSE, FALSE, UNKNOWN, UNKNOWN, UNKNOWN, FALSE, UNKNOWN, FALSE, FALSE, FALSE}},
        {{"\u026d"}, {FALSE, TRUE, TRUE, TRUE, UNKNOWN, TRUE, FALSE, FALSE, TRUE, FALSE, FALSE, FALSE, TRUE, FALSE, FALSE, UNKNOWN, UNKNOWN, UNKNOWN, FALSE, UNKNOWN, FALSE, FALSE, FALSE}},
        {{"\u026e"}, {FALSE, FALSE, TRUE, TRUE, TRUE, TRUE, FALSE, FALSE, TRUE, FALSE, FALSE, TRUE, TRUE, FALSE, FALSE, UNKNOWN, UNKNOWN, UNKNOWN, FALSE, UNKNOWN, FALSE, FALSE, FALSE}},
        {{"\u026f"}, {TRUE, TRUE, FALSE, TRUE, FALSE, FALSE, FALSE, UNKNOWN, TRUE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, TRUE, FALSE, TRUE, FALSE, FALSE, FALSE, FALSE, FALSE}},
        {{"\u0270"}, {FALSE, TRUE, FALSE, TRUE, UNKNOWN, FALSE, FALSE, UNKNOWN, TRUE, FALSE, FALSE, UNKNOWN, FALSE, UNKNOWN, FALSE, TRUE, FALSE, UNKNOWN, FALSE, TRUE, FALSE, FALSE, FALSE}},
        {{"\u0271"}, {FALSE, TRUE, TRUE, FALSE, UNKNOWN, FALSE, TRUE, UNKNOWN, TRUE, FALSE, FALSE, TRUE, FALSE, UNKNOWN, TRUE, UNKNOWN, UNKNOWN, UNKNOWN, FALSE, UNKNOWN, FALSE, FALSE, FALSE}},
        {{"\u0272"}, {FALSE, TRUE, TRUE, FALSE, FALSE, FALSE, TRUE, FALSE, TRUE, FALSE, FALSE, TRUE, FALSE, UNKNOWN, FALSE, TRUE, FALSE, FALSE, FALSE, UNKNOWN, FALSE, FALSE, FALSE}},
        {{"\u0273"}, {FALSE, TRUE, TRUE, FALSE, FALSE, FALSE, TRUE, FALSE, TRUE, FALSE, FALSE, FALSE, TRUE, UNKNOWN, FALSE, FALSE, FALSE, FALSE, FALSE, UNKNOWN, FALSE, FALSE, FALSE}},
        {{"\u0274"}, {FALSE, TRUE, TRUE, FALSE, FALSE, FALSE, TRUE, UNKNOWN, TRUE, FALSE, FALSE, FALSE, FALSE, UNKNOWN, FALSE, FALSE, FALSE, TRUE, FALSE, UNKNOWN, FALSE, FALSE, FALSE}},
        {{"\u0275"}, {TRUE, TRUE, FALSE, TRUE, UNKNOWN, FALSE, FALSE, UNKNOWN, TRUE, FALSE, FALSE, FALSE, FALSE, FALSE, TRUE, FALSE, FALSE, FALSE, TRUE, TRUE, FALSE, FALSE, FALSE}},
        {{"\u0276"}, {TRUE, TRUE, FALSE, TRUE, FALSE, FALSE, FALSE, UNKNOWN, TRUE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, TRUE, FALSE, TRUE, TRUE, FALSE, FALSE, FALSE}},
        {{"\u0278"}, {FALSE, FALSE, TRUE, TRUE, FALSE, FALSE, FALSE, UNKNOWN, FALSE, FALSE, FALSE, TRUE, FALSE, UNKNOWN, TRUE, FALSE, FALSE, FALSE, FALSE, UNKNOWN, FALSE, FALSE, FALSE}},
        {{"\u0279"}, {FALSE, TRUE, FALSE, TRUE, FALSE, FALSE, FALSE, FALSE, TRUE, FALSE, FALSE, FALSE, TRUE, FALSE, FALSE, TRUE, FALSE, TRUE, TRUE, UNKNOWN, FALSE, FALSE, FALSE}},
        {{"\u027a"}, {FALSE, TRUE, TRUE, TRUE, UNKNOWN, TRUE, FALSE, FALSE, TRUE, FALSE, FALSE, TRUE, TRUE, FALSE, FALSE, UNKNOWN, UNKNOWN, UNKNOWN, FALSE, UNKNOWN, FALSE, FALSE, FALSE}},
        {{"\u027b"}, {FALSE, TRUE, FALSE, TRUE, FALSE, FALSE, FALSE, FALSE, TRUE, FALSE, FALSE, FALSE, TRUE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, UNKNOWN, FALSE, FALSE, FALSE}},
        {{"\u027d"}, {FALSE, TRUE, TRUE, TRUE, UNKNOWN, FALSE, FALSE, FALSE, TRUE, FALSE, FALSE, FALSE, TRUE, FALSE, FALSE, UNKNOWN, UNKNOWN, UNKNOWN, FALSE, UNKNOWN, FALSE, FALSE, FALSE}},
        {{"\u027e"}, {FALSE, TRUE, TRUE, TRUE, UNKNOWN, FALSE, FALSE, FALSE, TRUE, FALSE, FALSE, TRUE, TRUE, FALSE, FALSE, UNKNOWN, UNKNOWN, UNKNOWN, FALSE, UNKNOWN, FALSE, FALSE, FALSE}},
        {{"\u0280"}, {FALSE, TRUE, TRUE, TRUE, UNKNOWN, FALSE, FALSE, UNKNOWN, TRUE, FALSE, FALSE, UNKNOWN, FALSE, UNKNOWN, FALSE, FALSE, FALSE, TRUE, FALSE, UNKNOWN, FALSE, FALSE, FALSE}},
        {{"\u0281"}, {FALSE, FALSE, TRUE, TRUE, FALSE, FALSE, FALSE, UNKNOWN, TRUE, FALSE, FALSE, FALSE, FALSE, UNKNOWN, FALSE, FALSE, FALSE, TRUE, FALSE, UNKNOWN, FALSE, FALSE, FALSE}},
        {{"\u0282"}, {FALSE, FALSE, TRUE, TRUE, FALSE, FALSE, FALSE, TRUE, FALSE, FALSE, FALSE, FALSE, TRUE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, UNKNOWN, FALSE, FALSE, FALSE}},
        {{"\u0283"}, {FALSE, FALSE, TRUE, TRUE, FALSE, FALSE, FALSE, TRUE, FALSE, FALSE, FALSE, FALSE, TRUE, TRUE, FALSE, FALSE, FALSE, FALSE, FALSE, UNKNOWN, FALSE, FALSE, FALSE}},
        {{"\u0284"}, {FALSE, FALSE, TRUE, FALSE, FALSE, FALSE, FALSE, UNKNOWN, TRUE, FALSE, TRUE, FALSE, FALSE, UNKNOWN, FALSE, TRUE, FALSE, FALSE, FALSE, UNKNOWN, FALSE, FALSE, FALSE}},
        {{"\u0288"}, {FALSE, FALSE, TRUE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, TRUE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, UNKNOWN, FALSE, FALSE, FALSE}},
        {{"\u0289"}, {TRUE, TRUE, FALSE, TRUE, UNKNOWN, FALSE, FALSE, UNKNOWN, TRUE, FALSE, FALSE, FALSE, FALSE, FALSE, TRUE, TRUE, FALSE, FALSE, TRUE, TRUE, FALSE, FALSE, FALSE}},
        {{"\u028a"}, {TRUE, TRUE, FALSE, TRUE, FALSE, FALSE, FALSE, UNKNOWN, TRUE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, TRUE, FALSE, TRUE, TRUE, FALSE, FALSE, FALSE, FALSE}},
        {{"\u028b"}, {FALSE, TRUE, FALSE, TRUE, UNKNOWN, FALSE, FALSE, UNKNOWN, TRUE, FALSE, FALSE, TRUE, FALSE, UNKNOWN, TRUE, UNKNOWN, UNKNOWN, UNKNOWN, FALSE, UNKNOWN, FALSE, FALSE, FALSE}},
        {{"\u028c"}, {TRUE, TRUE, FALSE, TRUE, FALSE, FALSE, FALSE, UNKNOWN, TRUE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, TRUE, FALSE, TRUE, FALSE, FALSE, FALSE}},
        {{"\u028d"}, {FALSE, TRUE, FALSE, TRUE, FALSE, FALSE, FALSE, UNKNOWN, FALSE, FALSE, FALSE, FALSE, FALSE, UNKNOWN, TRUE, TRUE, FALSE, TRUE, TRUE, UNKNOWN, FALSE, FALSE, FALSE}},
        {{"\u028e"}, {FALSE, TRUE, TRUE, TRUE, UNKNOWN, TRUE, FALSE, UNKNOWN, TRUE, FALSE, FALSE, FALSE, TRUE, TRUE, FALSE, TRUE, FALSE, FALSE, FALSE, UNKNOWN, FALSE, FALSE, FALSE}},
        {{"\u028f"}, {TRUE, TRUE, FALSE, TRUE, FALSE, FALSE, FALSE, UNKNOWN, TRUE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, TRUE, FALSE, FALSE, TRUE, FALSE, FALSE, FALSE, FALSE}},
        {{"\u0290"}, {FALSE, FALSE, TRUE, TRUE, FALSE, FALSE, FALSE, TRUE, TRUE, FALSE, FALSE, FALSE, TRUE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, UNKNOWN, FALSE, FALSE, FALSE}},
        {{"\u0291"}, {FALSE, FALSE, TRUE, TRUE, TRUE, FALSE, FALSE, TRUE, TRUE, FALSE, FALSE, TRUE, TRUE, TRUE, FALSE, TRUE, FALSE, FALSE, FALSE, UNKNOWN, FALSE, FALSE, FALSE}},
        {{"\u0292"}, {FALSE, FALSE, TRUE, TRUE, FALSE, FALSE, FALSE, TRUE, TRUE, FALSE, FALSE, FALSE, TRUE, TRUE, FALSE, FALSE, FALSE, FALSE, FALSE, UNKNOWN, FALSE, FALSE, FALSE}},
        {{"\u0294"}, {FALSE, TRUE, FALSE, FALSE, FALSE, FALSE, FALSE, UNKNOWN, FALSE, FALSE, TRUE, FALSE, FALSE, UNKNOWN, FALSE, FALSE, FALSE, FALSE, FALSE, UNKNOWN, FALSE, FALSE, FALSE}},
        {{"\u0295"}, {FALSE, FALSE, TRUE, TRUE, FALSE, FALSE, FALSE, UNKNOWN, TRUE, FALSE, FALSE, FALSE, FALSE, UNKNOWN, FALSE, FALSE, TRUE, TRUE, FALSE, UNKNOWN, FALSE, FALSE, FALSE}},
        {{"\u0298"}, {FALSE, FALSE, TRUE, FALSE, FALSE, FALSE, FALSE, UNKNOWN, FALSE, FALSE, FALSE, TRUE, FALSE, UNKNOWN, TRUE, FALSE, FALSE, FALSE, FALSE, UNKNOWN, FALSE, TRUE, FALSE}},
        {{"\u0299"}, {FALSE, TRUE, TRUE, TRUE, UNKNOWN, FALSE, FALSE, UNKNOWN, TRUE, FALSE, FALSE, TRUE, FALSE, UNKNOWN, TRUE, UNKNOWN, UNKNOWN, UNKNOWN, FALSE, UNKNOWN, FALSE, FALSE, FALSE}},
        {{"\u029b"}, {FALSE, FALSE, TRUE, FALSE, FALSE, FALSE, FALSE, UNKNOWN, TRUE, FALSE, TRUE, FALSE, FALSE, UNKNOWN, FALSE, FALSE, FALSE, TRUE, FALSE, UNKNOWN, FALSE, FALSE, FALSE}},
        {{"\u029d"}, {FALSE, FALSE, TRUE, TRUE, FALSE, FALSE, FALSE, UNKNOWN, TRUE, FALSE, FALSE, FALSE, FALSE, UNKNOWN, FALSE, TRUE, FALSE, FALSE, FALSE, UNKNOWN, FALSE, FALSE, FALSE}},
        {{"\u029f"}, {FALSE, TRUE, TRUE, TRUE, UNKNOWN, TRUE, FALSE, UNKNOWN, TRUE, FALSE, FALSE, UNKNOWN, FALSE, UNKNOWN, FALSE, TRUE, FALSE, UNKNOWN, FALSE, UNKNOWN, FALSE, FALSE, FALSE}},
        {{"\u03b2"}, {FALSE, FALSE, TRUE, TRUE, FALSE, FALSE, FALSE, UNKNOWN, TRUE, FALSE, FALSE, TRUE, FALSE, UNKNOWN, TRUE, FALSE, FALSE, FALSE, FALSE, UNKNOWN, FALSE, FALSE, FALSE}},
        {{"\u03b8"}, {FALSE, FALSE, TRUE, TRUE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, TRUE, TRUE, TRUE, FALSE, FALSE, FALSE, FALSE, FALSE, UNKNOWN, FALSE, FALSE, FALSE}},
        {{"\u03c7"}, {FALSE, FALSE, TRUE, TRUE, FALSE, FALSE, FALSE, UNKNOWN, FALSE, FALSE, FALSE, FALSE, FALSE, UNKNOWN, FALSE, FALSE, FALSE, TRUE, FALSE, UNKNOWN, FALSE, FALSE, FALSE}},
        {{"\u1d50b"}, {FALSE, FALSE, TRUE, FALSE, FALSE, FALSE, TRUE, UNKNOWN, TRUE, FALSE, FALSE, TRUE, FALSE, UNKNOWN, TRUE, FALSE, FALSE, FALSE, FALSE, UNKNOWN, FALSE, FALSE, FALSE}},
        {{"\u1d51\u0261"}, {FALSE, FALSE, TRUE, FALSE, FALSE, FALSE, TRUE, UNKNOWN, TRUE, FALSE, FALSE, FALSE, FALSE, UNKNOWN, FALSE, TRUE, FALSE, TRUE, FALSE, UNKNOWN, FALSE, FALSE, FALSE}},
        {{"\u1ebd"}, {TRUE, TRUE, FALSE, TRUE, FALSE, FALSE, TRUE, UNKNOWN, TRUE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, TRUE, FALSE, FALSE, TRUE}},
        {{"\u207fd"}, {FALSE, FALSE, TRUE, FALSE, FALSE, FALSE, TRUE, FALSE, TRUE, FALSE, FALSE, TRUE, TRUE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, UNKNOWN, FALSE, FALSE, FALSE}},
    };
    return table;
}

// feats.py modifiers: one diacritic -> {feature index: value} overrides.
const std::unordered_map<std::string, std::vector<std::pair<std::size_t, TriState>>>& modifier_table() {
    static const std::unordered_map<std::string, std::vector<std::pair<std::size_t, TriState>>> table = {
        {{"\u02d0"}, {{20, TRUE}}},
        {{" \u033b"}, {{13, TRUE}}},
        {{"\u02e4"}, {{16, TRUE}, {17, TRUE}}},
        {{"\u02de"}, {{11, FALSE}, {15, TRUE}, {18, TRUE}}},
        {{"\u0303"}, {{6, TRUE}, {22, TRUE}}},
        {{"\u0325"}, {{8, FALSE}}},
        {{"\u02b2"}, {{15, TRUE}, {17, FALSE}}},
        {{"\u02b7"}, {{14, TRUE}, {18, TRUE}}},
        {{"\u207f"}, {{6, TRUE}}},
        {{"\u02bc"}, {{10, TRUE}}},
    };
    return table;
}

// feats.py _bad_phones: irregular spellings normalized to the table key.
const std::unordered_map<std::string, std::string>& bad_phones() {
    static const std::unordered_map<std::string, std::string> table = {
        {"\u025d", "\u025c\u02de"},
    };
    return table;
}

} // namespace

std::vector<TriState> vectorize_phones(const std::string& phones) {
    if (phones.empty()) return {};

    // --- Step 1: normalize irregular phones ---
    std::string key = phones;
    if (const auto it = bad_phones().find(key); it != bad_phones().end()) key = it->second;

    const auto& table = phone_table();

    // Decode UTF-8 code points (Python iterates characters; the table keys
    // above are multi-byte but single-character spellings).
    std::vector<std::string> chars;
    for (std::size_t i = 0; i < key.size();) {
        const unsigned char c = static_cast<unsigned char>(key[i]);
        std::size_t n = c < 0x80 ? 1 : (c & 0xe0) == 0xc0 ? 2 : (c & 0xf0) == 0xe0 ? 3 : 4;
        chars.push_back(key.substr(i, n));
        i += n;
    }

    // feats.py:498 `if len(phones) == 1: return phone_features[phones]` —
    // the whole-spelling table lookup happens ONLY for a single character. A
    // multi-code-point key ("tsʼ", "tʃʼ", "ᵐb", "ᵑɡ") is NOT a lookup: the
    // reference always merges that spelling's base characters, and a base it
    // does not know ("ᵐ") raises, which vectorize_phones reports as the empty
    // (unknown) vector. Consulting the table for those four keys silently
    // returned a curated row the reference never reaches.
    if (chars.size() == 1) {
        const auto row = table.find(key);
        if (row == table.end()) return {};
        const FeatVec& vec = row->second;
        return std::vector<TriState>(vec.v, vec.v + NUM_FEATURES);
    }

    // Composite: merge the base phones' vectors per feature with
    // TRUE > FALSE > UNKNOWN dominance, then layer the modifiers on top.
    const auto& mods = modifier_table();
    std::vector<TriState> vec(NUM_FEATURES, UNKNOWN);
    std::vector<const std::vector<std::pair<std::size_t, TriState>>*> new_modifiers;
    bool known = false;
    for (const auto& ch : chars) {
        if (const auto it = mods.find(ch); it != mods.end()) {
            new_modifiers.push_back(&it->second);
            known = true;   // a modifier is a known contributor
            continue;
        }
        const auto it = table.find(ch);
        if (it == table.end()) return {};
        known = true;
        for (std::size_t f = 0; f < NUM_FEATURES; ++f) {
            const TriState base_val = it->second.v[f];
            if (base_val == TRUE) { vec[f] = TRUE; continue; }
            if (base_val == FALSE && vec[f] == UNKNOWN) vec[f] = FALSE;
        }
    }
    if (!known) return {};
    for (const auto* mod : new_modifiers)
        for (const auto& [idx, val] : *mod) vec[idx] = val;
    return vec;
}

bool is_known_phone(const std::string& phones) {
    return !vectorize_phones(phones).empty();
}

} // namespace orthography2ipa::feats

// ---------------------------------------------------------------------------
// feats.py's distance-and-classification layer, and the deprecated lm.py
// aliases. Appended block; the tables above are the reference's own
// (feats.py:348-456), transcribed with the reference's exact decimal literals.
// ---------------------------------------------------------------------------
#include "orthography2ipa/lm.hpp"

#include <cmath>

namespace orthography2ipa::feats {
namespace {

/// Python's sum() of floats (bltinmodule.c builtin_sum_impl), which since
/// Python 3.12 adds with Neumaier compensation. A plain running total differs
/// from it in the last bit for many of the distance sums below.
class PythonFloatSum {
public:
    void add(double x) {
        const double t = sum_ + x;
        compensation_ += std::fabs(sum_) >= std::fabs(x) ? (sum_ - t) + x : (x - t) + sum_;
        sum_ = t;
    }
    double value() const {
        return compensation_ != 0.0 && std::isfinite(compensation_) ? sum_ + compensation_ : sum_;
    }

private:
    double sum_ = 0.0;
    double compensation_ = 0.0;
};

/// feats.py:452-453 — `sum(feature_weights[i] for i in <the index set>)`, in
/// ascending index order; the sums are memoised like the module-level
/// constants are.
double sum_weights(const std::vector<std::size_t>& indices,
                   const std::vector<double>& weights) {
    PythonFloatSum total;
    for (const std::size_t idx : indices) total.add(weights[idx]);
    return total.value();
}

} // namespace

const std::vector<std::size_t>& vowel_features() {
    static const std::vector<std::size_t> indices{0, 1, 3, 8, 14, 15, 16, 17, 18, 19, 20, 22};
    return indices;
}

const std::vector<std::size_t>& consonant_features() {
    static const std::vector<std::size_t> indices{1, 2, 3, 4, 5, 6, 7, 8, 10,
                                                  11, 12, 13, 14, 15, 16, 17, 18, 19,
                                                  21};
    return indices;
}

const std::vector<double>& feature_weights() {
    static const std::vector<double> weights{
        0.13793103448275862,  // 0 syllabic          (major class)
        0.13793103448275862,  // 1 sonorant          (major class)
        0.13793103448275862,  // 2 consonantal       (major class)
        0.06896551724137931,  // 3 continuant
        0.03448275862068965,  // 4 delayed release
        0.03448275862068965,  // 5 lateral
        0.03448275862068965,  // 6 nasal
        0.01724137931034483,  // 7 strident
        0.01724137931034483,  // 8 voice
        0.01724137931034483,  // 9 spread_glottis
        0.01724137931034483,  // 10 constricted_glottis
        0.03448275862068965,  // 11 anterior
        0.03448275862068965,  // 12 coronal
        0.01724137931034483,  // 13 distributed
        0.03448275862068965,  // 14 labial
        0.03448275862068965,  // 15 high
        0.03448275862068965,  // 16 low
        0.03448275862068965,  // 17 back
        0.03448275862068965,  // 18 round
        0.03448275862068965,  // 19 tense
        0.01724137931034483,  // 20 long
        0.01724137931034483,  // 21 click
        0.01724137931034483,  // 22 nasal_vowel
    };
    return weights;
}

const std::vector<double>& standard_weights() {
    static const std::vector<double> weights(NUM_FEATURES, 1.0 / NUM_FEATURES);
    return weights;
}

const std::vector<double>& zero_weights() {
    static const std::vector<double> weights(NUM_FEATURES, 0.0);
    return weights;
}

double total_vowel_weight() {
    static const double total = sum_weights(vowel_features(), feature_weights());
    return total;
}

double total_consonant_weight() {
    static const double total = sum_weights(consonant_features(), feature_weights());
    return total;
}

TriState vowel_phone_flag(const std::string& phone) {
    const std::vector<TriState> vec = vectorize_phones(phone);
    if (vec.empty()) return FALSE;  // Python raises; is_vowel_phone returns False
    return vec[0];
}

bool is_vowel_phone(const std::string& phone) {
    return vowel_phone_flag(phone) == TRUE;
}

double phonetic_distance(const std::string& phone_a, const std::string& phone_b) {
    // feats.py: a space is a phone that matches only itself.
    if (phone_a == " " || phone_b == " ") return phone_a == phone_b ? 0.0 : 1.0;

    // feats.py compares the raw feature[0] values with `!=`, where an
    // undefined feature is None — different from both True and False, so a
    // phone whose feature[0] is undefined is "a different class" from any
    // definite phone and "the same class" as another undefined one (whose
    // branch is then the consonant one, because `if vowel:` sees a falsy
    // None).
    const TriState vowel = vowel_phone_flag(phone_a);
    if (vowel != vowel_phone_flag(phone_b)) return 1.0;

    const std::vector<double>& weights = feature_weights();
    const std::vector<std::size_t>* feature_indices;
    double total_weights;
    double factor;
    if (vowel == TRUE) {
        feature_indices = &vowel_features();
        total_weights = total_vowel_weight();
        factor = 1.0;
    } else {
        feature_indices = &consonant_features();
        total_weights = total_consonant_weight();
        factor = 2.0;
    }

    const std::vector<TriState> vec_a = vectorize_phones(phone_a);
    const std::vector<TriState> vec_b = vectorize_phones(phone_b);
    if (vec_a.empty() || vec_b.empty()) return 3.0;  // the caught ValueError

    PythonFloatSum diff_sum;
    for (const std::size_t idx : *feature_indices)
        if (vec_a[idx] != vec_b[idx]) diff_sum.add(weights[idx]);
    return (diff_sum.value() / total_weights) * factor;
}

std::map<std::string, std::vector<double>> phoneme_embeddings(const LanguageSpec& spec) {
    // feats.py:606 is a deprecated alias: it warns and calls lm.py.
    return lm::phoneme_embeddings(spec);
}

::orthography2ipa::lm::NgramLM build_ngram_lm(const std::vector<std::string>& words,
                                              const LanguageSpec& spec, int n) {
    // feats.py:618 is a deprecated alias: it warns and calls lm.py.
    return ::orthography2ipa::lm::build_ngram_lm(words, spec, n);
}

double perplexity(const ::orthography2ipa::lm::NgramLM& lm,
                  const std::vector<std::string>& test_words, const LanguageSpec& spec, int n) {
    // feats.py:630 is a deprecated alias: it warns and calls lm.py.
    return ::orthography2ipa::lm::perplexity(lm, test_words, spec, n);
}

} // namespace orthography2ipa::feats
