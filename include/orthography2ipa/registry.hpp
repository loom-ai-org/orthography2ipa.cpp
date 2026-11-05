// Direct port of the code-resolution half of orthography2ipa/registry.py:
// BCP-47 normalisation for a requested language tag, ending at the registered
// spec code that can answer for it.
//
// `resolve_code` mirrors registry.py `_resolve_code`, whose documented order is
//
//   1. the manual alias table, which holds the old keys of renamed specs;
//   2. an exact match against the registered spec codes;
//   3. langcodes.standardize_tag(macro=True), skipped for a private-use tag,
//      then the alias table and the catalog again;
//   4. a private-use CASE FOLD (langcodes never sees a `-x-` tag, so
//      `ar-sa-x-najd` would otherwise miss `ar-SA-x-najd`);
//   5. the default region of a bare tag (linguatag with_default_region,
//      `pt` -> `pt-PT`) when that spec is registered, then the curated
//      sub-region defaults (`ar-SA` -> `ars-SA`);
//   6. the nearest code by linguatag closest_lang with lects="standard" over
//      the catalog and the old keys, an old key mapped to its new one; no
//      usable match leaves the request unchanged.
//
// The tag measures themselves are the linguatag port in linguatag.hpp.
#pragma once

#include <string>
#include <vector>

namespace orthography2ipa {
namespace registry {

/// registry.py `_resolve_code` — the registered spec code that *code* resolves
/// to. A code with no usable resolution is returned unchanged, so `get()` can
/// raise for it. *available* is the catalog the request is matched against;
/// passing it explicitly keeps this function pure (the loader supplies it).
/// With *allow_nearest* false, the nearest-language match is not tried, and a
/// code that the earlier steps do not name is returned unchanged.
std::string resolve_code(const std::string& code, const std::vector<std::string>& available,
                         bool allow_nearest = true);

} // namespace registry
} // namespace orthography2ipa
