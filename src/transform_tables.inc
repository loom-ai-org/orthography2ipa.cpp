// Generated from the reference orthography2ipa/transforms.py by tests/_scratch/gen_tables.py:
// the profile rule tables, the character sets and the regex sources of the
// context predicates, transcribed from the reference rather than retyped.

namespace {

// transforms.py VOWEL_SET / PALATAL_SET / STRESS.
const char32_t VOWEL_SET[] = {0x61, 0x65, 0x69, 0x6f, 0x75, 0x79, 0xe3, 0xe6, 0xf5, 0xf8, 0x129, 0x153, 0x169, 0x250, 0x251, 0x254, 0x25b, 0x268, 0x1ebd, 0};
const char32_t PALATAL_SET[] = {0x6a, 0x272, 0x283, 0x28e, 0x292, 0};
const char32_t STRESS_MARK = 0x2c8;

// The regex sources the predicates and the de-biasing steps compile.
const char DB1_RE[] = "(?<=[^ae\u025b\u0250i\u0268o\u0254u\u0251\u00e6\u00f8\u0153y\u00e3\u1ebd\u0129\u00f5\u0169\s])\u0250j";
const char DB2_RE[] = "o(?=[tdsz\u0283nl\u027e])";
const char BEFORE_R_C[] = "[\u027er][bcdf\u0261hjklmnpqrstvwxz\u0283\u0292\u014b\u0272\u028e]";
const char BEFORE_NASAL_C[] = "[mn][bcdf\u0261hjklmnpqrstvwxz\u0283\u0292\u014b\u0272\u028e]";
const char ORTHO_C_EI[] = "c[ei]";
const char ORTHO_INTERVOCALIC_S[] = "[aeiou\u00e1\u00e9\u00ed\u00f3\u00fa\u00e2\u00ea\u00f4\u00e3\u00f5]s[aeiou\u00e1\u00e9\u00ed\u00f3\u00fa\u00e2\u00ea\u00f4\u00e3\u00f5]";

const std::vector<Profile>& profiles_table() {
    static const std::vector<Profile> table = {
        // estremenho: Centro-Litoral / Estremenho (Central-Meridional)
        Profile{"estremenho", "Centro-Litoral / Estremenho", "Central-Meridional", true, {
        }},
        // lisbon: Lisbon / Lisboa (Central-Meridional (Lisbon))
        Profile{"lisbon", "Lisbon / Lisboa", "Central-Meridional (Lisbon)", false, {
            Rule::plain("LX1", "lisbon_ei_lowering", "ej", "\u0250j", nullptr, false),
            Rule::plain("LX2", "lisbon_ou_monophthong", "ow", "o", nullptr, false),
            Rule::plain("LX3", "lisbon_unstressed_reduction", "e", "\u0268", "unstressed_pretonic", false),
            Rule::plain("LX4", "lisbon_coda_l_velarization", "l", "\u026b", "coda", false),
        }},
        // ribatejano: Ribatejano / Baixo-Beirão / Alentejano (Central-Meridional)
        Profile{"ribatejano", "Ribatejano / Baixo-Beir\u00e3o / Alentejano", "Central-Meridional", true, {
            Rule::plain("RA1", "ei_monophthong", "ej", "e", nullptr, false),
        }},
        // beira_baixa: Beira-Baixa / Alto-Alentejo (Central-Meridional)
        Profile{"beira_baixa", "Beira-Baixa / Alto-Alentejo", "Central-Meridional", true, {
            Rule::plain("RA1", "ei_monophthong", "ej", "e", nullptr, false),
            Rule::plain("BB1", "u_palatalization", "u", "y", "stressed", false),
            Rule::plain("BB2a", "a_palatalization_palatal_context", "a", "\u00e6", "stressed_and_adjacent_to_palatal", false),
            Rule::plain("BB3", "o_fronting", "o", "\u00f8", "stressed_and_from_historical_ou", true),
            Rule::plain("BB4", "open_e_labialization", "\u025b", "\u0153", "stressed", false),
            Rule::plain("BB5a", "final_u_deletion", "u", "", "word_final_unstressed", false),
            Rule::plain("BB5b", "final_schwa_deletion", "\u0268", "", "word_final", false),
        }},
        // algarve_barlavento: Barlavento Algarvio (Meridional)
        Profile{"algarve_barlavento", "Barlavento Algarvio", "Meridional", true, {
            Rule::plain("RA1", "ei_monophthong", "ej", "e", nullptr, false),
            Rule::chain("ALG_CHAIN", "barlavento_chain_shift", Mapping{{"a", "\u0254"}, {"\u0254", "o"}, {"o", "u"}, {"u", "y"}, {"\u025b", "\u00e6"}, {"e", "\u025b"}}, "stressed"),
            Rule::plain("ALG2a", "final_u_deletion_alg", "u", "", "word_final_unstressed", false),
        }},
        // northern: Northern Portuguese (common) (Setentrional)
        Profile{"northern", "Northern Portuguese (common)", "Setentrional", true, {
            Rule::plain("N1", "betacism", "v", "b", nullptr, false),
            Rule::plain("N2", "ou_diphthong_preservation", "o", "ow", "ortho_has_ou", true),
        }},
        // transmontano: Transmontano / Alto-Minhoto (Setentrional)
        Profile{"transmontano", "Transmontano / Alto-Minhoto", "Setentrional", true, {
            Rule::plain("N1", "betacism", "v", "b", nullptr, false),
            Rule::plain("N2", "ou_diphthong_preservation", "o", "ow", "ortho_has_ou", true),
            Rule::plain("TM1a", "apicoalveolar_s", "s", "s\u033a", "ortho_source_is_ss_or_s", true),
            Rule::plain("TM1b", "predorsodental_s", "s", "s\u033b", "ortho_source_is_c_or_\u00e7", true),
            Rule::plain("TM1c", "apicoalveolar_z", "z", "z\u033a", "ortho_source_is_intervocalic_s", true),
            Rule::plain("TM1d", "predorsodental_z", "z", "z\u033b", "ortho_source_is_z", true),
            Rule::plain("TM1_fallback", "apicoalveolar_sibilants_all", "s", "s\u033a", "no_ortho_available", false),
            Rule::plain("TM2", "ch_affrication", "\u0283", "t\u0283", "ortho_source_is_ch", true),
            Rule::plain("TM3", "nasal_diphthong_reduction", "\u0250\u0303w\u0303", "\u00f5", "word_final", false),
        }},
        // baixo_minhoto: Baixo-Minhoto / Duriense / Beirão (Setentrional)
        Profile{"baixo_minhoto", "Baixo-Minhoto / Duriense / Beir\u00e3o", "Setentrional", true, {
            Rule::plain("N1", "betacism", "v", "b", nullptr, false),
            Rule::plain("N2", "ou_diphthong_preservation", "o", "ow", "ortho_has_ou", true),
            Rule::plain("BMD1a", "apicoalveolar_s_merged", "s", "s\u033a", nullptr, false),
            Rule::plain("BMD1b", "apicoalveolar_z_merged", "z", "z\u033a", nullptr, false),
        }},
        // porto: Porto / Baixo-Minho / Douro Litoral (Setentrional)
        Profile{"porto", "Porto / Baixo-Minho / Douro Litoral", "Setentrional", true, {
            Rule::plain("N1", "betacism", "v", "b", nullptr, false),
            Rule::plain("N2", "ou_diphthong_preservation", "o", "ow", "ortho_has_ou", true),
            Rule::plain("BMD1a", "apicoalveolar_s_merged", "s", "s\u033a", nullptr, false),
            Rule::plain("BMD1b", "apicoalveolar_z_merged", "z", "z\u033a", nullptr, false),
            Rule::plain("PT1", "porto_e_diphthong", "e", "je", "stressed_and_before_r_or_closed_syllable", false),
            Rule::plain("PT2", "porto_o_diphthong", "o", "wo", "stressed_and_before_r_or_closed_syllable", false),
            Rule::plain("PT3", "porto_nasal_a_velarization", "\u0250\u0303", "\u0251\u0303", "stressed", false),
        }},
        // beira_alta: Beira-Alta (General Beirão) (Setentrional)
        Profile{"beira_alta", "Beira-Alta (General Beir\u00e3o)", "Setentrional", true, {
            Rule::plain("N1", "betacism", "v", "b", nullptr, false),
            Rule::plain("N2", "ou_diphthong_preservation", "o", "ow", "ortho_has_ou", true),
            Rule::plain("BMD1a", "apicoalveolar_s_merged", "s", "s\u033a", nullptr, false),
            Rule::plain("BMD1b", "apicoalveolar_z_merged", "z", "z\u033a", nullptr, false),
        }},
        // galician: Galician / Galego (Eastern) (Galician)
        Profile{"galician", "Galician / Galego (Eastern)", "Galician", true, {
            Rule::plain("G1", "betacism", "v", "b", nullptr, false),
            Rule::plain("G2a", "palatal_devoicing", "\u0292", "\u0283", nullptr, false),
            Rule::plain("G2b", "alveolar_devoicing", "z", "s", nullptr, false),
            Rule::plain("G3a", "galician_unstressed_e", "\u0268", "e", "unstressed", false),
            Rule::plain("G3c", "galician_unstressed_a", "\u0250", "a", "unstressed", false),
        }},
        // galician_west: Western Galician (with geada) (Galician)
        Profile{"galician_west", "Western Galician (with geada)", "Galician", true, {
            Rule::plain("G1", "betacism", "v", "b", nullptr, false),
            Rule::plain("G2a", "palatal_devoicing", "\u0292", "\u0283", nullptr, false),
            Rule::plain("G2b", "alveolar_devoicing", "z", "s", nullptr, false),
            Rule::plain("G3a", "galician_unstressed_e", "\u0268", "e", "unstressed", false),
            Rule::plain("G3c", "galician_unstressed_a", "\u0250", "a", "unstressed", false),
            Rule::plain("GW1", "geada", "\u0261", "x", nullptr, false),
        }},
        // leonese: Leonese enclave (common) (Astur-Leonese)
        Profile{"leonese", "Leonese enclave (common)", "Astur-Leonese", true, {
            Rule::plain("N1", "betacism", "v", "b", nullptr, false),
            Rule::plain("N2", "ou_diphthong_preservation", "o", "ow", "ortho_has_ou", true),
            Rule::plain("TM1a", "apicoalveolar_s", "s", "s\u033a", "ortho_source_is_ss_or_s", true),
            Rule::plain("TM1b", "predorsodental_s", "s", "s\u033b", "ortho_source_is_c_or_\u00e7", true),
            Rule::plain("TM1c", "apicoalveolar_z", "z", "z\u033a", "ortho_source_is_intervocalic_s", true),
            Rule::plain("TM1d", "predorsodental_z", "z", "z\u033b", "ortho_source_is_z", true),
            Rule::plain("TM1_fallback", "apicoalveolar_sibilants_all", "s", "s\u033a", "no_ortho_available", false),
            Rule::plain("LEO1", "leonese_e_diphthong", "\u025b", "je", "stressed_before_r_C_or_nasal_C", false),
            Rule::plain("LEO2", "leonese_o_diphthong", "\u0254", "w\u0254", "stressed_before_r_C", false),
            Rule::plain("LEO3", "lh_yodization", "\u028e", "j", nullptr, false),
            Rule::plain("LEO4", "leonese_palatal_devoicing", "\u0292", "\u0283", nullptr, false),
            Rule::plain("LEO5", "participle_d_syncope", "adu", "aw", "word_final", false),
        }},
        // rionorese: Rionorese (Rionor de Castela) (Astur-Leonese)
        Profile{"rionorese", "Rionorese (Rionor de Castela)", "Astur-Leonese", true, {
            Rule::plain("N1", "betacism", "v", "b", nullptr, false),
            Rule::plain("N2", "ou_diphthong_preservation", "o", "ow", "ortho_has_ou", true),
            Rule::plain("TM1a", "apicoalveolar_s", "s", "s\u033a", "ortho_source_is_ss_or_s", true),
            Rule::plain("TM1b", "predorsodental_s", "s", "s\u033b", "ortho_source_is_c_or_\u00e7", true),
            Rule::plain("TM1c", "apicoalveolar_z", "z", "z\u033a", "ortho_source_is_intervocalic_s", true),
            Rule::plain("TM1d", "predorsodental_z", "z", "z\u033b", "ortho_source_is_z", true),
            Rule::plain("TM1_fallback", "apicoalveolar_sibilants_all", "s", "s\u033a", "no_ortho_available", false),
            Rule::plain("LEO1", "leonese_e_diphthong", "\u025b", "je", "stressed_before_r_C_or_nasal_C", false),
            Rule::plain("LEO2", "leonese_o_diphthong", "\u0254", "w\u0254", "stressed_before_r_C", false),
            Rule::plain("LEO3", "lh_yodization", "\u028e", "j", nullptr, false),
            Rule::plain("LEO4", "leonese_palatal_devoicing", "\u0292", "\u0283", nullptr, false),
            Rule::plain("LEO5", "participle_d_syncope", "adu", "aw", "word_final", false),
            Rule::lexical("RIO_ART", "o", "u", "al"),
            Rule::lexical("RIO_PRON", "eu", "ew", "jew"),
        }},
        // guadramilese: Guadramilese (Guadramil) (Astur-Leonese)
        Profile{"guadramilese", "Guadramilese (Guadramil)", "Astur-Leonese", true, {
            Rule::plain("N1", "betacism", "v", "b", nullptr, false),
            Rule::plain("N2", "ou_diphthong_preservation", "o", "ow", "ortho_has_ou", true),
            Rule::plain("TM1a", "apicoalveolar_s", "s", "s\u033a", "ortho_source_is_ss_or_s", true),
            Rule::plain("TM1b", "predorsodental_s", "s", "s\u033b", "ortho_source_is_c_or_\u00e7", true),
            Rule::plain("TM1c", "apicoalveolar_z", "z", "z\u033a", "ortho_source_is_intervocalic_s", true),
            Rule::plain("TM1d", "predorsodental_z", "z", "z\u033b", "ortho_source_is_z", true),
            Rule::plain("TM1_fallback", "apicoalveolar_sibilants_all", "s", "s\u033a", "no_ortho_available", false),
            Rule::plain("LEO1", "leonese_e_diphthong", "\u025b", "je", "stressed_before_r_C_or_nasal_C", false),
            Rule::plain("LEO2", "leonese_o_diphthong", "\u0254", "w\u0254", "stressed_before_r_C", false),
            Rule::plain("LEO3", "lh_yodization", "\u028e", "j", nullptr, false),
            Rule::plain("LEO4", "leonese_palatal_devoicing", "\u0292", "\u0283", nullptr, false),
            Rule::plain("LEO5", "participle_d_syncope", "adu", "aw", "word_final", false),
            Rule::lexical("GUA_PRON", "eu", "ew", "jow"),
            Rule::plain("GUA_IMPF", "guadramilese_3pl_imperfect", "i\u0250\u0303w\u0303", "j\u1ebd", "word_final", false),
            Rule::lexical("GUA_IR", "ia", "i\u0250", "dib\u0250"),
        }},
    };
    return table;
}

} // namespace
