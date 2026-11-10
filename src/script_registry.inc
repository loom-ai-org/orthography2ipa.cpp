// Generated from the reference orthography2ipa/script_distance.py by
// tests/gen_script_registry.py: the SCRIPT_REGISTRY feature bundles and the
// script-type distance matrix, transcribed from the reference rather than
// retyped. Do not edit by hand; regenerate.

// script_distance.py SCRIPT_REGISTRY, in the reference's own declaration
// order. script_type carries the ScriptType VALUE (types.py: alphabet,
// abjad, ...), the same spelling LanguageSpec.script_type keeps.
const std::vector<ScriptFeatures>& registry_table() {
    static const std::vector<ScriptFeatures> table = {
        // "Latin": ScriptType.ALPHABET, LTR, has_case=true, vowel_marking=full, block Basic Latin
        ScriptFeatures{"Latin", "alphabet", "LTR", true, "full", "Basic Latin", {"Etruscan", "Greek", "Phoenician"}},
        // "Cyrillic": ScriptType.ALPHABET, LTR, has_case=true, vowel_marking=full, block Cyrillic
        ScriptFeatures{"Cyrillic", "alphabet", "LTR", true, "full", "Cyrillic", {"Greek", "Phoenician"}},
        // "Greek": ScriptType.ALPHABET, LTR, has_case=true, vowel_marking=full, block Greek
        ScriptFeatures{"Greek", "alphabet", "LTR", true, "full", "Greek", {"Phoenician"}},
        // "Armenian": ScriptType.ALPHABET, LTR, has_case=true, vowel_marking=full, block Armenian
        ScriptFeatures{"Armenian", "alphabet", "LTR", true, "full", "Armenian", {"Greek", "Phoenician"}},
        // "Georgian": ScriptType.ALPHABET, LTR, has_case=false, vowel_marking=full, block Georgian
        ScriptFeatures{"Georgian", "alphabet", "LTR", false, "full", "Georgian", {"Greek", "Phoenician"}},
        // "Arabic": ScriptType.ABJAD, RTL, has_case=false, vowel_marking=partial, block Arabic
        ScriptFeatures{"Arabic", "abjad", "RTL", false, "partial", "Arabic", {"Nabataean", "Aramaic", "Phoenician"}},
        // "Hebrew": ScriptType.ABJAD, RTL, has_case=false, vowel_marking=partial, block Hebrew
        ScriptFeatures{"Hebrew", "abjad", "RTL", false, "partial", "Hebrew", {"Aramaic", "Phoenician"}},
        // "Devanagari": ScriptType.ABUGIDA, LTR, has_case=false, vowel_marking=inherent, block Devanagari
        ScriptFeatures{"Devanagari", "abugida", "LTR", false, "inherent", "Devanagari", {"Brahmi"}},
        // "Bengali": ScriptType.ABUGIDA, LTR, has_case=false, vowel_marking=inherent, block Bengali
        ScriptFeatures{"Bengali", "abugida", "LTR", false, "inherent", "Bengali", {"Brahmi"}},
        // "Tamil": ScriptType.ABUGIDA, LTR, has_case=false, vowel_marking=inherent, block Tamil
        ScriptFeatures{"Tamil", "abugida", "LTR", false, "inherent", "Tamil", {"Brahmi"}},
        // "Telugu": ScriptType.ABUGIDA, LTR, has_case=false, vowel_marking=inherent, block Telugu
        ScriptFeatures{"Telugu", "abugida", "LTR", false, "inherent", "Telugu", {"Brahmi"}},
        // "Kannada": ScriptType.ABUGIDA, LTR, has_case=false, vowel_marking=inherent, block Kannada
        ScriptFeatures{"Kannada", "abugida", "LTR", false, "inherent", "Kannada", {"Brahmi"}},
        // "Gurmukhi": ScriptType.ABUGIDA, LTR, has_case=false, vowel_marking=inherent, block Gurmukhi
        ScriptFeatures{"Gurmukhi", "abugida", "LTR", false, "inherent", "Gurmukhi", {"Brahmi"}},
        // "Thai": ScriptType.ABUGIDA, LTR, has_case=false, vowel_marking=inherent, block Thai
        ScriptFeatures{"Thai", "abugida", "LTR", false, "inherent", "Thai", {"Brahmi"}},
        // "Tibetan": ScriptType.ABUGIDA, LTR, has_case=false, vowel_marking=inherent, block Tibetan
        ScriptFeatures{"Tibetan", "abugida", "LTR", false, "inherent", "Tibetan", {"Brahmi"}},
        // "Ethiopic": ScriptType.ABUGIDA, LTR, has_case=false, vowel_marking=inherent, block Ethiopic
        ScriptFeatures{"Ethiopic", "abugida", "LTR", false, "inherent", "Ethiopic", {"South Arabian"}},
        // "Hangul": ScriptType.FEATURAL, LTR, has_case=false, vowel_marking=full, block Hangul
        ScriptFeatures{"Hangul", "featural", "LTR", false, "full", "Hangul", {}},
        // "Hanzi": ScriptType.LOGOGRAPHIC, LTR, has_case=false, vowel_marking=none, block CJK Unified
        ScriptFeatures{"Hanzi", "logographic", "LTR", false, "none", "CJK Unified", {}},
        // "Kana": ScriptType.SYLLABARY, LTR, has_case=false, vowel_marking=full, block Hiragana
        ScriptFeatures{"Kana", "syllabary", "LTR", false, "full", "Hiragana", {}},
        // "Cherokee": ScriptType.SYLLABARY, LTR, has_case=false, vowel_marking=full, block Cherokee
        ScriptFeatures{"Cherokee", "syllabary", "LTR", false, "full", "Cherokee", {}},
        // "IPA-reconstruction": ScriptType.RECONSTRUCTION, LTR, has_case=false, vowel_marking=full, block IPA Extensions
        ScriptFeatures{"IPA-reconstruction", "reconstruction", "LTR", false, "full", "IPA Extensions", {}},
    };
    return table;
}

// script_distance.py _init_type_distances(): the `raw` table of base
// distances. The reference fills BOTH halves of every pair plus a 0.0
// diagonal; one entry per unordered pair is enough, and the C++
// initialiser mirrors _init_type_distances in filling the rest.
const std::vector<TypeDistance>& type_distance_table() {
    static const std::vector<TypeDistance> table = {
    {"alphabet", "abjad", 0.3},  // ScriptType.ALPHABET x ScriptType.ABJAD
    {"alphabet", "abugida", 0.4},  // ScriptType.ALPHABET x ScriptType.ABUGIDA
    {"alphabet", "syllabary", 0.5},  // ScriptType.ALPHABET x ScriptType.SYLLABARY
    {"alphabet", "featural", 0.3},  // ScriptType.ALPHABET x ScriptType.FEATURAL
    {"alphabet", "logographic", 0.9},  // ScriptType.ALPHABET x ScriptType.LOGOGRAPHIC
    {"alphabet", "mixed", 0.7},  // ScriptType.ALPHABET x ScriptType.MIXED
    {"abjad", "abugida", 0.4},  // ScriptType.ABJAD x ScriptType.ABUGIDA
    {"abjad", "syllabary", 0.6},  // ScriptType.ABJAD x ScriptType.SYLLABARY
    {"abjad", "featural", 0.5},  // ScriptType.ABJAD x ScriptType.FEATURAL
    {"abjad", "logographic", 0.9},  // ScriptType.ABJAD x ScriptType.LOGOGRAPHIC
    {"abjad", "mixed", 0.7},  // ScriptType.ABJAD x ScriptType.MIXED
    {"abugida", "syllabary", 0.4},  // ScriptType.ABUGIDA x ScriptType.SYLLABARY
    {"abugida", "featural", 0.5},  // ScriptType.ABUGIDA x ScriptType.FEATURAL
    {"abugida", "logographic", 0.8},  // ScriptType.ABUGIDA x ScriptType.LOGOGRAPHIC
    {"abugida", "mixed", 0.6},  // ScriptType.ABUGIDA x ScriptType.MIXED
    {"syllabary", "featural", 0.5},  // ScriptType.SYLLABARY x ScriptType.FEATURAL
    {"syllabary", "logographic", 0.7},  // ScriptType.SYLLABARY x ScriptType.LOGOGRAPHIC
    {"syllabary", "mixed", 0.4},  // ScriptType.SYLLABARY x ScriptType.MIXED
    {"logographic", "featural", 0.8},  // ScriptType.LOGOGRAPHIC x ScriptType.FEATURAL
    {"featural", "mixed", 0.6},  // ScriptType.FEATURAL x ScriptType.MIXED
    {"logographic", "mixed", 0.3},  // ScriptType.LOGOGRAPHIC x ScriptType.MIXED
    {"alphabet", "reconstruction", 0.1},  // ScriptType.ALPHABET x ScriptType.RECONSTRUCTION
    {"abjad", "reconstruction", 0.3},  // ScriptType.ABJAD x ScriptType.RECONSTRUCTION
    {"abugida", "reconstruction", 0.3},  // ScriptType.ABUGIDA x ScriptType.RECONSTRUCTION
    {"syllabary", "reconstruction", 0.5},  // ScriptType.SYLLABARY x ScriptType.RECONSTRUCTION
    {"featural", "reconstruction", 0.3},  // ScriptType.FEATURAL x ScriptType.RECONSTRUCTION
    {"logographic", "reconstruction", 0.8},  // ScriptType.LOGOGRAPHIC x ScriptType.RECONSTRUCTION
    {"mixed", "reconstruction", 0.6},  // ScriptType.MIXED x ScriptType.RECONSTRUCTION
    };
    return table;
}

