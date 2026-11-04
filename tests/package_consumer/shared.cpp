#include <orthography2ipa/orthography2ipa.hpp>

#include <string>

std::string consumer_transcribe(const std::string& text) { return orthography2ipa::transcribe(text, "pt"); }
