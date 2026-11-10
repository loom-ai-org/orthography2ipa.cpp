// Direct port of the code-resolution half of orthography2ipa/registry.py:
// BCP-47 normalisation for a requested language tag, ending at the registered
// spec code that can answer for it.
//
// `resolve_code` mirrors registry.py `_resolve_code`, whose documented order is
//
//   1. the manual alias table, which holds the old keys of renamed specs;
//   2. an exact match against the registered spec codes;
//   3. a CASE FOLD of every tag, before standardization: the lowered tag
//      against the alias keys, then the catalog, so `MNK` names `mnk` before
//      macrolanguage folding can turn it into `man`, and `ar-sa-x-najd` names
//      `ar-SA-x-najd`;
//   4. langcodes.standardize_tag(macro=True), skipped for a private-use tag,
//      then the alias table and the catalog again. A private-use tag also
//      matches, without regard to case, with a script subtag dropped when that
//      script is the language's Suppress-Script (`pt-Latn-BR-x-sp` ->
//      `pt-BR-x-sp`);
//   5. the default region of a bare tag (linguatag with_default_region,
//      `pt` -> `pt-PT`) when that spec is registered, then the curated
//      sub-region defaults (`ar-SA` -> `ars-SA`);
//   6. the nearest code by linguatag closest_lang with lects="standard". The
//      own-language search comes first: the catalog codes and old keys of the
//      requested primary language, or of the standardized one when the
//      requested language has none (`sr-Latn-RS` -> `sr`, not `cnr`). A
//      one-letter primary subtag selects no language. With no match there, the
//      search takes every catalog code and old key. An old key maps to its
//      new one, and no usable match leaves the request unchanged.
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
