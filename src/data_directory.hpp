// The spec directory that holds before any set_data_directory() call.
#pragma once

#include <string>

namespace orthography2ipa::schema {

/// ORTHOGRAPHY2IPA_DATA_DIR, then the install beside the shared library or the
/// program that holds this code, then the source tree, then the install path
/// that the build compiles in. A program of the build tree does not read an
/// install outside the build tree, so it reads the source tree.
std::string default_data_directory();

}  // namespace orthography2ipa::schema
