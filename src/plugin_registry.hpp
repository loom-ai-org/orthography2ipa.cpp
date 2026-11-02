// Internal access to the plugin registry, for the conformance kit: the
// reference's conformance.py reassigns `registry._syllabifiers` directly.
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
