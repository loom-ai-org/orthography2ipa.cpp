#pragma once

// Direct port of orthography2ipa/script_distance.py — typological distance
// between writing systems.
//
// How structurally different two scripts are, from their typological
// classification, directionality, case system, vowel-marking strategy and
// shared historical ancestry. The feature bundles and the script-type distance
// matrix are data, transcribed from the reference into
// src/script_registry.inc by tests/gen_script_registry.py; nothing here
// re-derives a number.
//
// types.py ScriptType is a str-Enum, and the C++ port keeps its VALUES
// ("alphabet", "abjad", ...) in plain strings — exactly as LanguageSpec
// .script_type already does elsewhere in this library.
#include <map>
#include <string>
#include <vector>

namespace orthography2ipa::script_distance {

/// script_distance.py ScriptFeatures: typological feature bundle for one
/// writing system. A frozen dataclass, so it compares by value.
struct ScriptFeatures {
    std::string name;
    /// ScriptType value: "alphabet", "abjad", "abugida", "syllabary",
    /// "logographic", "featural", "mixed", "reconstruction".
    std::string script_type;
    std::string directionality;   ///< "LTR", "RTL", "TTB"
    bool has_case = false;
    std::string vowel_marking;    ///< "full", "partial", "inherent", "none"
    std::string unicode_block;
    std::vector<std::string> ancestor_scripts;

    bool operator==(const ScriptFeatures& other) const;
};

/// script_distance.py SCRIPT_REGISTRY: every registered script, keyed by name.
const std::map<std::string, ScriptFeatures>& registry();

/// script_distance.py `script_distance(a, b)`: the weighted combined distance
/// in [0.0, 1.0].
double script_distance(const ScriptFeatures& a, const ScriptFeatures& b);

/// script_distance.py `script_distance_by_name(a, b)`. Throws
/// std::out_of_range for an unregistered name, which is the reference's
/// KeyError.
double script_distance_by_name(const std::string& a, const std::string& b);

} // namespace orthography2ipa::script_distance
