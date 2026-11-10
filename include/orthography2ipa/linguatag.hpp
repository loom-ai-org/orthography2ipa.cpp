// Direct port of the `linguatag` package that orthography2ipa/registry.py
// measures language tags with: standardization, the default region of a tag
// that names none, the nearness of lects named with a private-use subtag, the
// distance between two tags, and the closest match among candidates.
//
// The data linguatag reads from its own files (CLDR likely regions, the lect
// tree) and its constants are generated into src/linguatag_tables.inc by
// tests/gen_registry_tables.py. `langcodes` is always present here: the port
// in langcodes.hpp is what linguatag reaches when the library is installed,
// and a tag it cannot parse takes linguatag's own fallback, as in the source.
#pragma once

#include <optional>
#include <string>
#include <vector>

namespace orthography2ipa::linguatag {

/// linguatag.lects LECT_MODES, with `none` for the source's `lects=None`.
enum class Lects { none, dialect, standard };

/// matching.py DEFAULT_MAX_LANGUAGE_DISTANCE: the inclusive upper bound of a
/// usable match.
extern const int default_max_language_distance;

/// standardize.py standardize_lang: `langcodes.standardize_tag` when it
/// accepts the tag, the BCP 47 case fold otherwise; `tl` and `tgl` stay `tl`.
std::string standardize_lang(const std::string& tag);

/// regions.py default_region: the region *tag* names, or the one it defaults
/// to; nullopt when no region applies.
std::optional<std::string> default_region(const std::string& tag);

/// regions.py with_default_region: *tag*, standardized, with its default
/// region inserted after the language and script (`en` -> `en-GB`).
std::string with_default_region(const std::string& tag);

/// lects.py private_use: the private-use subtags of *tag*, lowercase.
std::vector<std::string> private_use(const std::string& tag);

/// lects.py without_private_use: *tag* standardized, without its private-use
/// subtags.
std::string without_private_use(const std::string& tag);

/// lects.py lect_steps: steps between two tags in the lect tree, or nullopt
/// when they do not meet.
std::optional<int> lect_steps(const std::string& a, const std::string& b);

/// lects.py lect_score: what lang_distance adds for *mode*, 0 to 4. *mode*
/// must not be Lects::none (the source raises ValueError for it).
int lect_score(const std::string& desired, const std::string& supported, Lects mode);

/// matching.py lang_distance: 0 for identical tags, a larger number further
/// apart, above 10 no usable match. With a lect mode, the lect score is added
/// without raising a distance at or below the threshold above it.
int lang_distance(const std::string& desired, const std::string& supported,
                  Lects lects = Lects::none);

/// matching.py closest_lang: the entry of *available* with the smallest
/// distance to *target* within *max_distance*, ranked by the lect distance
/// when *lects* is set. A tie goes to the candidate that spells *target*
/// itself, then to the first in iteration order. nullopt is the source's None.
std::optional<std::string> closest_lang(const std::string& target,
                                        const std::vector<std::string>& available,
                                        int max_distance = default_max_language_distance,
                                        Lects lects = Lects::none);

} // namespace orthography2ipa::linguatag
