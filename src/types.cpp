// See include/orthography2ipa/types.hpp for the contract. The tables are
// GENERATED data (src/types_tables.inc).
#include "orthography2ipa/types.hpp"

#include "orthography2ipa/orthography2ipa.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <utility>

namespace orthography2ipa::types {
namespace {

#include "types_tables.inc"

const std::pair<const char*, const char*>* find(
        const std::pair<const char*, const char*>* table, std::size_t count,
        const std::string& key) {
    for (std::size_t i = 0; i < count; ++i)
        if (key == table[i].first) return &table[i];
    return nullptr;
}

std::size_t table_size(const std::pair<const char*, const char*>* table) {
    std::size_t count = 0;
    while (table != nullptr && table[count].first != nullptr) ++count;
    (void)table;
    return count;   // unused sentinel form
}

template <std::size_t N>
const std::pair<const char*, const char*>* find(
        const std::pair<const char*, const char*> (&table)[N],
        const std::string& key) {
    for (std::size_t i = 0; i < N; ++i)
        if (key == table[i].first) return &table[i];
    return nullptr;
}

} // namespace

const char* to_string(InheritanceMode mode) {
    switch (mode) {
        case InheritanceMode::BASE_MERGE: return "base_merge";
        case InheritanceMode::OVERLAY_BY_ID: return "overlay_by_id";
        case InheritanceMode::NOT_INHERITED: return "not_inherited";
        case InheritanceMode::BASE_SCALAR: return "base_scalar";
        case InheritanceMode::OWN_ONLY: return "own_only";
    }
    return "";
}

InheritanceMode inheritance_mode(const std::string& value) {
    const auto* mode = find(kInheritanceMode, value);
    if (mode == nullptr)
        throw std::invalid_argument("'" + value + "' is not a valid InheritanceMode");
    if (std::string(mode->second) == "base_merge") return InheritanceMode::BASE_MERGE;
    if (std::string(mode->second) == "overlay_by_id") return InheritanceMode::OVERLAY_BY_ID;
    if (std::string(mode->second) == "not_inherited") return InheritanceMode::NOT_INHERITED;
    if (std::string(mode->second) == "base_scalar") return InheritanceMode::BASE_SCALAR;
    return InheritanceMode::OWN_ONLY;
}

InheritanceMode field_inheritance(const std::string& field) {
    const auto* entry = find(kFieldInheritance, field);
    if (entry == nullptr)
        throw std::out_of_range("FIELD_INHERITANCE has no entry for " + field);
    return inheritance_mode(entry->second);
}

std::vector<std::string> fields_missing_inheritance_decision() {
    std::set<std::string> declared;
    for (const auto& entry : kSpecFields) declared.insert(entry.first);
    std::vector<std::string> missing;
    for (const auto& field : declared)
        if (find(kFieldInheritance, field) == nullptr) missing.push_back(field);
    return missing;
}

const char* orthography_kind_value(const std::string& member) {
    const auto* entry = find(kOrthographyKind, member);
    return entry == nullptr ? nullptr : entry->second;
}

const char* quality_tier_value(const std::string& member) {
    const auto* entry = find(kQualityTier, member);
    return entry == nullptr ? nullptr : entry->second;
}

const char* script_type_value(const std::string& member) {
    const auto* entry = find(kScriptType, member);
    return entry == nullptr ? nullptr : entry->second;
}

const char* ancestor_role_value(const std::string& member) {
    const auto* entry = find(kAncestorRole, member);
    return entry == nullptr ? nullptr : entry->second;
}

const char* audit_conclusion_value(const std::string& member) {
    const auto* entry = find(kAuditConclusion, member);
    return entry == nullptr ? nullptr : entry->second;
}

const char* grapheme_position_value(const std::string& member) {
    const auto* entry = find(kGraphemePosition, member);
    return entry == nullptr ? nullptr : entry->second;
}

bool is_grapheme_position(const std::string& member) {
    return find(kGraphemePosition, member) != nullptr;
}

} // namespace orthography2ipa::types
