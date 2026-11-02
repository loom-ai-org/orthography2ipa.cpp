// Direct port of orthography2ipa/types.py's string enums and the
// FIELD_INHERITANCE manifest — the vocabulary the spec data model checks
// against, and the inheritance contract every field of a LanguageSpec file
// resolves under. The VALUES are generated from the reference
// (tests/gen_types_tables.py -> src/types_tables.inc); a name lookup that the
// reference answers at import time (enum members) is answered at runtime here
// because C++ has no import-time construction step for these tables.
//
// The pydantic-era names InheritanceMode/... are the reference's exact
// members and values.
#pragma once

#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace orthography2ipa {
struct LanguageSpec;

namespace types {

/// types.py InheritanceMode(str, Enum): how one LanguageSpec field resolves
/// when the file inherits from a base spec.
enum class InheritanceMode {
    BASE_MERGE,
    OVERLAY_BY_ID,
    NOT_INHERITED,
    BASE_SCALAR,
    OWN_ONLY,
};

/// The enum member's str value ("base_merge" ... "own_only").
const char* to_string(InheritanceMode mode);
/// InheritanceMode(value): the member for a str value; throws where the
/// reference's dict lookup raises ValueError.
InheritanceMode inheritance_mode(const std::string& value);

/// types.py FIELD_INHERITANCE: the manifest field -> InheritanceMode.
InheritanceMode field_inheritance(const std::string& field);

/// types.py fields_missing_inheritance_decision(): the LanguageSpec field
/// names absent from FIELD_INHERITANCE. Empty when the manifest is complete.
std::vector<std::string> fields_missing_inheritance_decision();

/// The string enums (member NAME -> VALUE) for the vocabulary fields a spec
/// carries: orthography_kind / quality / script_type / ancestor roles /
/// grapheme positions / audit conclusions. `member` is the pydantic-era NAME
/// ("NATIVE") and the return is the DATA value ("native"); these are
/// validation-level vocabularies (schema.py's *_VALUES tables read them).
const char* orthography_kind_value(const std::string& member);
const char* quality_tier_value(const std::string& member);
const char* script_type_value(const std::string& member);
const char* ancestor_role_value(const std::string& member);
const char* audit_conclusion_value(const std::string& member);
/// The GraphemePosition member/value table (the 45 positions the loader's
/// positional keys resolve under).
const char* grapheme_position_value(const std::string& member);
bool is_grapheme_position(const std::string& member);

} // namespace types
} // namespace orthography2ipa
