// Internal access to the syllabifier registry that the deprecated
// register_syllabifier_plugin writes. No engine reads it; the parity drivers
// fill and empty it to show that.
#pragma once

#include "orthography2ipa/orthography2ipa.hpp"

#include <map>
#include <memory>
#include <string>

namespace orthography2ipa {

using SyllabifierRegistry = std::map<std::string, std::shared_ptr<SyllabifierPlugin>>;

/// Put *plugins* in place of every registered syllabifier and return the
/// ones that were registered.
SyllabifierRegistry exchange_syllabifiers(SyllabifierRegistry plugins);

} // namespace orthography2ipa
