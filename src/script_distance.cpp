// script_distance.py port: the typological distance between writing systems.
// SCRIPT_REGISTRY and the script-type distance matrix come from
// src/script_registry.inc, generated from the reference module itself.
#include "orthography2ipa/script_distance.hpp"

#include <algorithm>
#include <cstdlib>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace orthography2ipa::script_distance {
namespace {

/// One entry of script_distance.py `_SCRIPT_TYPE_DISTANCE`'s `raw` table: a
/// pair of ScriptType values and the base distance between them.
struct TypeDistance {
    const char* a;
    const char* b;
    double distance;
};

#include "script_registry.inc"

/// script_distance.py `_init_type_distances()`: the diagonal at 0.0 and every
/// raw pair filled in BOTH directions.
const std::map<std::pair<std::string, std::string>, double>& type_distances() {
    static const std::map<std::pair<std::string, std::string>, double> table = [] {
        std::map<std::pair<std::string, std::string>, double> distances;
        // `types = list(ScriptType)` — the eight values the reference
        // enumerates, which the generated table covers in full.
        std::set<std::string> types;
        for (const auto& entry : type_distance_table()) {
            types.insert(entry.a);
            types.insert(entry.b);
        }
        for (const auto& type : types)
            distances[{type, type}] = 0.0;
        for (const auto& entry : type_distance_table()) {
            distances[{entry.a, entry.b}] = entry.distance;
            distances[{entry.b, entry.a}] = entry.distance;
        }
        return distances;
    }();
    return table;
}

/// script_distance.py `_vm_order`.
int vowel_marking_order(const std::string& marking) {
    if (marking == "full") return 0;
    if (marking == "partial") return 1;
    if (marking == "inherent") return 2;
    if (marking == "none") return 3;
    return 2;                                   // `_vm_order.get(..., 2)`
}

} // namespace

bool ScriptFeatures::operator==(const ScriptFeatures& other) const {
    return name == other.name && script_type == other.script_type &&
           directionality == other.directionality && has_case == other.has_case &&
           vowel_marking == other.vowel_marking && unicode_block == other.unicode_block &&
           ancestor_scripts == other.ancestor_scripts;
}

const std::map<std::string, ScriptFeatures>& registry() {
    static const std::map<std::string, ScriptFeatures> table = [] {
        std::map<std::string, ScriptFeatures> by_name;
        for (const auto& features : registry_table())
            by_name.emplace(features.name, features);
        return by_name;
    }();
    return table;
}

double script_distance(const ScriptFeatures& a, const ScriptFeatures& b) {
    if (a.name == b.name) return 0.0;

    // Script type distance; 0.5 when the pair is not in the matrix.
    const auto found = type_distances().find({a.script_type, b.script_type});
    const double type_d = found == type_distances().end() ? 0.5 : found->second;

    // Directionality.
    const double dir_d = a.directionality == b.directionality ? 0.0 : 1.0;

    // Vowel marking.
    const double vowel_d =
        std::abs(vowel_marking_order(a.vowel_marking) -
                 vowel_marking_order(b.vowel_marking)) / 3.0;

    // Case.
    const double case_d = a.has_case == b.has_case ? 0.0 : 1.0;

    // Shared ancestor bonus: Jaccard similarity over the two ancestor sets.
    const std::set<std::string> set_a(a.ancestor_scripts.begin(), a.ancestor_scripts.end());
    const std::set<std::string> set_b(b.ancestor_scripts.begin(), b.ancestor_scripts.end());
    double ancestor_sim = 0.0;
    if (!set_a.empty() && !set_b.empty()) {
        std::vector<std::string> shared;
        std::set_intersection(set_a.begin(), set_a.end(), set_b.begin(), set_b.end(),
                              std::back_inserter(shared));
        std::vector<std::string> unioned;
        std::set_union(set_a.begin(), set_a.end(), set_b.begin(), set_b.end(),
                       std::back_inserter(unioned));
        ancestor_sim = static_cast<double>(shared.size()) / static_cast<double>(unioned.size());
    }
    const double ancestor_d = 1.0 - ancestor_sim;

    // The reference's own weighted sum, in the reference's term order.
    const double combined =
        0.40 * type_d
        + 0.15 * dir_d
        + 0.15 * vowel_d
        + 0.05 * case_d
        + 0.25 * ancestor_d;
    return std::min(std::max(combined, 0.0), 1.0);
}

double script_distance_by_name(const std::string& a, const std::string& b) {
    const auto& table = registry();
    const auto left = table.find(a);
    if (left == table.end())
        throw std::out_of_range("script not in SCRIPT_REGISTRY: " + a);
    const auto right = table.find(b);
    if (right == table.end())
        throw std::out_of_range("script not in SCRIPT_REGISTRY: " + b);
    return script_distance(left->second, right->second);
}

} // namespace orthography2ipa::script_distance
