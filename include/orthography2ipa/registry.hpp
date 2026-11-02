// Direct port of the code-resolution half of orthography2ipa/registry.py:
// BCP-47 normalisation for a requested language tag, ending at the registered
// spec code that can answer for it.
//
// `resolve` mirrors registry.py `_resolve_code`, whose documented order is
//
//   1. the manual alias table (private-use subtags and the ISO 639-3 codes
//      langcodes may not round-trip cleanly);
//   2. langcodes.standardize_tag(macro=True), skipped for a private-use tag;
//   3. an exact match against the registered spec codes;
//   4. a private-use CASE FOLD (langcodes never sees a `-x-` tag, so
//      `ar-sa-x-najd` would otherwise miss `ar-SA-x-najd`);
//   5. the curated bare-tag / sub-region defaults;
//   6. the nearest registered code by language distance, and no usable match
//      leaves the request unchanged.
//
// The distance is ovos_spec_tools.language.lang_distance: both tags
// standardized, a bare tag measured FROM its norm region, `langcodes
// .tag_distance` when it can compute one and the coarse 3/5/100 measure when
// it cannot; `closest_lang` then takes the minimum within
// DEFAULT_MAX_LANGUAGE_DISTANCE, ties going to the FIRST candidate in the
// order it is given (here: `available_json_codes()` order, as in the
// reference).
#pragma once

#include <string>
#include <vector>

namespace orthography2ipa {
namespace registry {

/// registry.py `_resolve_code` — the registered spec code that *code* resolves
/// to. A code with no usable resolution is returned unchanged, so `get()` can
/// raise for it. *available* is the catalog the request is matched against;
/// passing it explicitly keeps this function pure (the loader supplies it).
std::string resolve_code(const std::string& code, const std::vector<std::string>& available);

/// ovos_spec_tools.language standardize_lang: langcodes standardization with
/// the Tagalog policy override (`tl`/`tgl` stay `tl`, where langcodes folds
/// them into the `fil` macrolanguage).
std::string standardize_lang(const std::string& tag);

/// ovos_spec_tools.language _with_norm_region: give a bare tag its norm region
/// (`pt` -> `pt-PT`); a regioned tag, or a language with no norm region, is
/// returned unchanged.
std::string with_norm_region(const std::string& tag);

/// ovos_spec_tools.language lang_distance: 0 for identical, a small value for a
/// regional difference, >= 100 for a different primary language.
int lang_distance(const std::string& desired, const std::string& supported);

/// ovos_spec_tools.language _coarse_distance: the langcodes-free fallback. The
/// magnitudes are arbitrary and ordering-only (3 = one side is the generic
/// language tag, 5 = two regions of one language, 100 = different language).
int coarse_distance(const std::string& desired, const std::string& supported);

/// ovos_spec_tools.language closest_lang: the entry of *available* with the
/// smallest lang_distance, resolving when that distance is *max_distance* or
/// less; ties go to the first smallest candidate in iteration order. Empty
/// string == the reference's None.
std::string closest_lang(const std::string& target, const std::vector<std::string>& available,
                         int max_distance = 10);

} // namespace registry
} // namespace orthography2ipa
