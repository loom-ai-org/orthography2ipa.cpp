// Direct port of orthography2ipa/transforms.py — the IPA dialect transform
// system for the Portuguese and Ibero-Romance varieties.
//
// This module is the reference's DE-BIASING and RULE ENGINE over an IPA string:
// a profile is data (`DIALECT_PROFILES`, an ordered list of IPARule /
// IPAChainShift / IPALexicalRule entries), and apply_transform() runs the
// reference's stage order — de-bias, chain shifts, lexical rules, then the
// ordered phonological rules with their named contexts. The tables themselves
// are generated from the reference into src/transform_tables.inc, so no rule is
// reworded here and no variety has a hand-written branch.
//
// Like the rest of the port, every position is a Unicode CODE POINT: the rules
// match IPA characters, several of which are multi-byte in UTF-8.
#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace orthography2ipa::transform {

/// transforms.py AnyRule: the three rule kinds a profile mixes.
enum class Kind { Plain, Chain, Lexical };

/// One source/target pair of an IPAChainShift mapping.
using Pair = std::pair<std::string, std::string>;
using Mapping = std::vector<Pair>;

struct Rule {
    Kind kind = Kind::Plain;
    std::string id;
    std::string name;
    /// IPARule / IPALexicalRule: the substring to find. Chain: unused.
    std::string find;
    /// IPARule / IPALexicalRule: the replacement.
    std::string replace;
    /// IPALexicalRule: the orthographic word that triggers the rule.
    std::string word;
    /// IPARule: the named context, empty for None (unconditional).
    std::string context;
    bool requires_ortho = false;
    /// IPAChainShift: source → target, applied in ONE pass so one mapping's
    /// output is never another's input.
    std::vector<Pair> mapping;

    static Rule plain(const std::string& id, const std::string& name,
                      const std::string& find, const std::string& replace,
                      const char* context = nullptr, bool requires_ortho = false);
    static Rule chain(const std::string& id, const std::string& name,
                      Mapping mapping, const char* context = nullptr);
    static Rule lexical(const std::string& id, const std::string& word,
                        const std::string& find, const std::string& replace);
};

/// transforms.py DialectTransform.
struct Profile {
    std::string code;
    std::string name;
    std::string cintra_zone;
    bool requires_debiasing = true;
    std::vector<Rule> rules;
};

/// transforms.py DIALECT_PROFILES, in the reference's own declaration order.
const std::vector<Profile>& profiles();
/// transforms.py available_profiles(): the profile codes, sorted.
std::vector<std::string> available_profiles();
/// The profile named *code*, or nullptr.
const Profile* profile_for(const std::string& code);

/// transforms.py debias_lisbon.
std::string debias_lisbon(const std::string& ipa, const std::string& ortho = "");
/// transforms.py debias_lisbon_preserve_spirants.
std::string debias_lisbon_preserve_spirants(const std::string& ipa,
                                            const std::string& ortho = "");

/// transforms.py apply_transform. An empty *profile* is not a transform and
/// returns *ipa* unchanged; an unknown profile throws, as KeyError does.
/// `debias` mirrors the reference's keyword of the same name, and
/// `spirantization_rate` is the one CLUP weight the stage consults: above 0.02
/// the de-biasing keeps the region's real spirants.
std::string apply_transform(const std::string& ipa, const std::string& profile,
                            const std::string& ortho = "", bool debias = true,
                            double spirantization_rate = 0.0);

} // namespace orthography2ipa::transform
