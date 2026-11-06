// The process-wide lexicon, for the inventory: lexicon.py _process_lexicon, which
// the deprecated get_lexicon wraps.
#pragma once

#include <map>
#include <memory>
#include <string>

namespace orthography2ipa::detail {

/// The process-wide `{word: ipa}` overlay for the resolved *code*, or an empty
/// mapping when none is set. Shared with every caller that reads it.
std::shared_ptr<const std::map<std::string, std::string>> process_lexicon(const std::string& code);

}  // namespace orthography2ipa::detail
