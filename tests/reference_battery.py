#!/usr/bin/env python3
"""The section-8 regression battery: (lang, text) cases per requirement tag.

Every category here answers one bullet of the TODO's "Reference Regression
Gate": the six named languages, Arabic, abugidas, tone languages, combining
marks, viramas, preposed vowels, geminates, clitics and multi-candidate
allophony. Two rules shape it:

* **Pin what the data cannot express.** `hi`, `lo`, `th`, `bn`, `ta`, `te`,
  `kn`, `ml`, `mr`, `ne`, `my`, `bo`, `km`, `ko`, `it`, `sv`, `hu`, `vi`, `yo`,
  `ga`, `fo` declare ZERO `word_exceptions`, so a purely data-driven battery
  silently covers none of them — the PINNED lists below are what carries those
  categories (the coverage trap named in the TODO).
* **Derive the rest.** Each builder walks the reference registry, so the
  battery grows with the catalog instead of rotting.

`build()` returns ``{category: [[lang, text], ...]}``, deduplicated, with
blank surfaces dropped.
"""
import os
import pathlib
import sys
import unicodedata

_ROOT = os.environ.get("O2I_PYTHON_ROOT") or str(
    pathlib.Path(__file__).resolve().parent.parent.parent / "orthography2ipa")
if _ROOT not in sys.path:
    sys.path.insert(0, _ROOT)

import logging  # noqa: E402
logging.disable(logging.CRITICAL)
from orthography2ipa.registry import get  # noqa: E402

# The six languages the requirement names for direct parity.
SIX = ["pt-PT", "hi", "lo", "ru", "fr-FR", "es-ES"]

# Pinned confirmed words. hi/lo (and the other abugidas and tone specs listed in
# the module docstring) have no word_exceptions at all, so these lists ARE the
# battery for them; they are the cases the audit confirmed by hand.
PINNED = {
    "pt-PT": ["chuva", "as", "aves", "os", "dois", "irmãos", "estás", "ver",
              "guarda-chuva", "chamo-me"],
    "pt-BR": ["guarda-chuva", "boa-nova"],
    "pt-AO": ["boa-nova"],
    "pt-MZ": ["guarda-chuva"],
    "es-ES": ["seguro"],
    "en-US": ["i'll.", "(i'm)"],
    "af": ["'n."],
    "ru": ["молоко"],
    "th": ["กรวด", "ขวด"],
    "fr-FR": ["jeune", "agneau", "les", "amis"],
    "oc": ["Califòrnia", "sciéncia"],
    # médico-dentário also needs compound_stress and the followed_by_3
    # allophone condition.
    "pt-PT-x-alentejo": ["capitólio", "aquário", "médico-dentário"],
    "pt-PT-x-acores": ["3Aüau;-üj3bíe"],
    "pt-CV": [",jrü1êAxà"],
    "hi": ["कृष्ण", "क्या", "राम", "नमस्ते", "भारत", "किताब", "दो", "क्", "क्ष",
           "श्र", "मुक्ति", "हिन्दी", "बिल्ल्", "सिक्ख"],
    "pt-BR-x-bahia": ["em", "nos", "botar os pingos nos is"],
    "pt-BR-x-mg": ["em"],
    "pt-BR-x-recife": ["os"],
    "lo": ["ເອກ", "ເຫຼົ້າ", "ດອກ", "ເບິ່ງ", "ກ", "ງ", "ຈ", "ສາວ", "ລາວ", "ພາສາ",
           "ຂອບໃຈ", "ໃຈ", "ແຜ່ນ", "ໂລກ", "ເຮືອນ", "ຊີວິດ", "ມື", "ບໍ່", "ແມ່", "ຄົນ",
           "ອາໂຫລ", "ອາໂຫຼ"],
}

# A quote mark and a backslash inside a word: every JSON surface escapes them.
PINNED["it-IT"] = ['"casa"', "ca\\sa"]
# Every spec with a silent stress mark: an exception key, a clitic and an ending.
# The endings take the stress on, before and after the declared vowels, and
# ⟨-чего⟩ follows a consonant that some dialects read as an allophone.
for _code in ("ru", "ru-x-arkhange", "ru-x-don", "ru-x-kursk-orel", "ru-x-moscow", "ru-x-northern",
              "ru-x-pskov", "ru-x-siberian", "ru-x-southern", "ru-x-ural", "ru-x-vologda"):
    PINNED.setdefault(_code, []).extend(["его\u0301", "во\u0301", "большо\u0301го", "самого\u0301",
                                         "ста\u0301рого", "горячего", "горя\u0301чего", "зо\u0301дчего",
                                         "что", "сего\u0301дня"])

ABUGIDAS = ["hi", "bn", "ta", "te", "kn", "ml", "mr", "ne", "or", "as", "sa", "sid"]
TONE_LANGS = ["th", "lo", "vi", "yo", "km", "my", "bo", "ko", "kix", "zh", "pa"]
ARABIC = ["ar", "ar-IQ", "ar-LB"]
SHADDA = "\u0651"

# Specs that take constrain_onsets from their graphemes base.
PINNED["nl-BE"] = ["arco", "alto"]
PINNED["de-AT"] = ["Fenster"]

# The combining-mark / virama / preposed-vowel / geminate / clitic / multi
# builders walk these lists; they are the languages the requirement names.
COMBINING = ["pt-PT", "el", "vi", "tr", "ar", "hi", "cs", "pl", "sv", "hu", "th", "lo", "yo"]
PREPOSED = ["th", "lo", "km", "my", "sa", "bn", "hi"]
GEMINATE = ["ar", "it-IT", "sv", "hu", "en-GB", "fo", "de-DE", "pt-PT", "es-ES", "nn", "hi", "ml"]
GEMINATE_AR = ["ar", "it-IT", "sv", "hu"]
CLITIC = ["fr-FR", "en-GB", "ru", "pt-PT", "ar", "ar-IQ", "it-IT", "ca", "ga", "he", "sv", "da"]
MULTI = ["fr-FR", "sv", "fo", "en-GB", "pt-PT", "ru", "it-IT", "de-DE", "is", "el", "hi", "th", "ko", "my"]

# Aliases: a surface reports the code that the alias resolves to.
PINNED["gl"] = ["casa"]
PINNED["ro"] = ["casă"]
PINNED["nl"] = ["kunst"]
PINNED["nld"] = ["lezen"]
# A region with no spec of its own resolves through the renamed nl key.
PINNED["nl-SR"] = ["kunst"]

# Spelling rules the word_exceptions do not reach: a final ⟨iu ui⟩ after a
# vowel (stress.final_diphthongs), a stressed vowel after a diphthong
# (stress.tonic_after_diphthong), the nasal diphthong ⟨ãi⟩ before a coda
# nasal, the Limburgish unstressed prefixes (stress.unstressed_prefixes) and
# ⟨ew⟩ before a vowel (prevocalic_glide_backoff), long [ʏː] and doubled [ɔɔ].
PINNED["pt-PT"] += ["distraiu", "saiu", "instruiu", "pauis", "viu", "baiuca",
                    "feiura", "tuiuiú", "maiúscula", "cheiinho", "abaiucado",
                    "cãibra", "cãimbra", "põem", "Piauí", "cauim", "cauila"]
PINNED["pt-BR"] += ["saiu", "pauis", "baiuca", "teiú", "cãimbra", "falar", "comer",
                    "Piauí", "cauim", "cauila"]
# Specs that take the pt-PT and pt-BR stress block whole.
PINNED["pt-AO"] += ["distraiu", "baiuca"]
PINNED["pt-BR-x-mg"] += ["saiu", "feiura"]
PINNED["pt-PT-x-lisbon"] = ["Piauí", "atraiu", "baiuca", "cãi", "cãimbra"]
PINNED["pt-BR-x-sp"] = ["Piauí", "atraiu", "baiuca", "cãi", "cãimbra", "falar"]
PINNED["pt-UY"] = ["Piauí", "atraiu", "baiuca", "pauis", "cãimbra"]
PINNED["li"] = ["geweun", "bewaar", "eweg", "kewa", "gebroek", "verstand",
                "verlange", "gebeure", "geve", "beste", "Gennep", "deh", "nih",
                "zàèle", "vàèl"]
PINNED["li-x-rheindok"] = ["gebroek", "ve\u0329rstand", "Gele\u0329"]
PINNED["nl-NL-x-limburg"] = ["huis", "zeggen", "geweest"]
PINNED["nl-NL-x-polder"] = ["tijd", "twijfelen", "vrijheid", "huis"]
PINNED["nl-NL-x-groninge"] = ["vier", "fier", "goed", "lachen"]
PINNED["nl-BE"] += ["wang", "wit", "kwaad", "zwak"]
PINNED["sv"] = ["blyerts", "cykel"]
PINNED["nb"] = ["gynge"]
PINNED["enm"] = ["toot", "poo"]
# A coda nasal before a velar (ES_NASAL_VELAR), in es-ES, in the specs that
# inherit it and in the specs that carry their own copy.
PINNED["es-ES"] += ["banco", "cinco"]
for code in ["es-MX", "es-VE", "es-CO-x-llanero", "es-ES-x-canarias",
             "ast-x-cantabri", "ast-x-pasiego", "ext"]:
    PINNED[code] = ["banco", "cinco"]
# Galician falling diphthongs after a rising one (Paraguai, anunciou).
PINNED["gl-ES"] = ["Paraguai", "anunciou"]
PINNED["gl-ES-x-central"] = ["apaziguou"]
PINNED["gl-ES-x-eonavia"] = ["Paraguai"]
# Pontic Greek ⟨αυ ευ⟩ before a voiceless sound, the palatals before a front
# vowel, ⟨νγκ⟩, word-initial ⟨μπ⟩ and the dotted ⟨τ̇⟩.
PINNED["pnt"] = ["αυ", "Τουρκικὸν", "ένγκεν", "ΕΝΓΚΑ", "μπαλόν’", "τ\u0307σ"]
# pt-PT reads porque with [u] and ⟨ã⟩ as [ɐ̃]; the specs with full unstressed
# vowels switch the porque rule off again.
PINNED["pt-PT"] += ["porque", "amanhã"]
for code in ["pt-AO", "pt-GW", "pt-MZ", "pt-ST", "pt-TL", "pt-PT-x-algarve"]:
    PINNED.setdefault(code, []).append("porque")
# pt-BR raises a word-initial pretonic ⟨e⟩ before a coda sibilant and ⟨en em⟩
# to the close nasal vowel, but not the one-syllable ⟨em⟩; pt-UY switches both
# rules off. ⟨pôr⟩ and ⟨lugar⟩ hold the two other r-drop rules
# (BR_INFINITIVE_R_DROP_POR, BR_FINAL_R_NONVERB).
PINNED["pt-BR"] += ["escola", "entrar", "embora", "em", "amanhã", "pôr", "lugar"]
PINNED["pt-UY"] += ["escola", "entrar", "amanhã"]
PINNED["pt-BR-x-cuiaba"] = ["tchau", "tchibum"]
# Netherlandic ⟨ee eu oo⟩ as closing diphthongs and ⟨g ch⟩ as uvular [χ]; nl-BE
# and af keep their own readings, and nl-NL-x-limburg keeps the velar [x].
PINNED["nl-NL"] = ["been", "boom", "neus", "deur", "goed", "acht", "regen"]
PINNED["nl-BE"] += ["goed", "acht", "school", "berg"]
PINNED["af"] += ["kné", "café", "vér"]
PINNED["nl-NL-x-groninge"] += ["been", "boom"]
PINNED["nl-NL-x-polder"] += ["been", "acht"]
PINNED["nl-NL-x-limburg"] += ["tjerkgaast", "school", "acht"]
# Netherlandic coda /r/ is the approximant [ɹ]; nl-BE keeps the trill,
# voiceless finally and before /p t k/, and af keeps its own /r/. nl-NL leaves
# the prefixes be- ge- ver- ont- unstressed.
PINNED["nl-NL"] += ["vaart", "kaart", "arm", "dorp", "kerk", "door", "bier", "dichter",
                    "betalen", "bedoelen", "geloven", "verkopen", "bezien", "ontkennen",
                    "beste"]
PINNED["nl-BE"] += ["door", "vaart", "dorp", "kerk", "harder", "betalen", "verkopen"]
PINNED["af"] += ["vaart"]
PINNED["nl-NL-x-polder"] += ["vaart", "betalen", "verkopen"]
PINNED["nl-NL-x-limburg"] += ["vaart", "geloven", "berg"]
PINNED["nl-NL-x-groninge"] += ["verkopen"]
# Catalan ⟨gu⟩ reads [ɡu] before a consonant in the positional table only, so
# its vowel is written and the stress count keeps its syllable.
PINNED["ca"] = ["vingut", "vengut"]
# A final syllable in stress.schwa_rimes leaves the word without a prefix, so
# the stress stays on the root. bevel, gestel, gezel and gezwel have a stem with
# a full vowel and are word exceptions; bestel is not one. nl-BE reads a doubled
# consonant letter as one consonant, and nl-NL-x-limburg keeps coda /r/ as [r].
PINNED["nl-NL"] += ["geven", "beter", "beren", "bezem", "veren", "verder", "beker", "beten",
                    "gebed", "geleden", "vereren", "bevel", "gestel", "gezel", "gezwel",
                    "bestel"]
PINNED["nl-BE"] += ["geven", "beter", "bezem", "verder", "gebed", "bevel", "gestel", "gezel",
                    "gezwel", "kennen", "zitten", "hebben", "zeggen", "bakken", "uittrekken",
                    "induffelde"]
PINNED["nl-NL-x-polder"] += ["geven", "beter", "gezel"]
PINNED["nl-NL-x-groninge"] += ["geven", "beter", "gezel"]
PINNED["nl-NL-x-limburg"] += ["bergen", "kerk", "tietjerk", "tjerkwerd", "geven", "beter",
                              "gestel", "gezel", "gezwel"]
# nl-BE-x-antwerp: the onset tap, a schwa after /r/ in a coda cluster, long
# stressed [i] and [u], and the stress block of nl-BE.
PINNED["nl-BE-x-antwerp"] = ["rok", "brood", "beraad", "sturen", "kerk", "berg", "bergen",
                             "arm", "door", "Piet", "spoed", "geven", "beter", "gezel",
                             "kennen", "harder"]
# pt-BR inserts [i] after a stop that follows the stressed vowel, and the mark
# stays on the stressed syllable (pacto [ˈpakitu]); ⟨ck⟩ takes no vowel; an
# obstruent cluster that opens a stressed word takes [i] (pseudo). The
# southern children and pt-UY switch the rules off.
PINNED["pt-BR"] += ["pacto", "capto", "rapto", "compacto", "egípcio", "helicóptero",
                    "hockey", "pseudo", "psique", "pneu", "psicologia", "Djavan"]
# A pretonic stop before a nasal takes [i] too (hipnose); the southern specs keep
# the cluster after the stressed vowel (mogno). Two nasals keep it everywhere.
PINNED["pt-BR"] += ["hipnose", "abnegado", "admitir", "etnia", "mogno", "magnata", "amnésia"]
for code in ["pt-BR-x-sul", "pt-BR-x-floripa", "pt-BR-x-pr"]:
    PINNED.setdefault(code, []).extend(["hipnose", "mogno", "magnata"])
# pt-PT reads the article ⟨o os⟩ with [u]; a spec that keeps an unstressed ⟨o⟩
# as [o] switches the rule off.
PINNED["pt-PT"] += ["o", "os", "O", "Os", "osso", "ovo"]
for code in ["pt-PT-x-lisbon", "pt-PT-x-porto", "pt-MO", "pt-CV", "ext-PT-x-barranco"]:
    PINNED.setdefault(code, []).extend(["o", "os"])
for code in ["pt-AO", "pt-GW", "pt-MZ", "pt-ST", "pt-TL", "pt-PT-x-algarve", "aoa", "cri",
             "idb", "kea", "mcm", "mzs", "pap", "pov", "pre", "vkp"]:
    PINNED.setdefault(code, []).extend(["o", "os"])
for code in ["pt-BR-x-sul", "pt-BR-x-floripa", "pt-BR-x-pr", "pt-BR-x-sp", "pt-BR-x-rj",
             "pt-UY"]:
    PINNED.setdefault(code, []).extend(["capto", "pseudo"])
# An inserted vowel has its own unstressed syllable: pneu and advogado read it as
# pretonic, pacto and egípcio as post-tonic. A written stressed ⟨i⟩ before a nasal
# stays nasal, and the [u] of a positional ⟨qu⟩ is written, so it keeps its stress.
PINNED["pt-BR"] += ["advogado", "gnomo", "tmese", "cima", "taqu"]
# muito reads the nasal diphthong [ũj̃], and the creoles switch the rules off.
PINNED["pt-PT"] += ["muito", "muita", "muitos", "muitas", "muitíssimo", "muitíssima", "Muito",
                    "mui", "fortuito", "circuito", "gratuito", "cuidado", "mudo", "fui"]
PINNED["pt-BR"] += ["muito", "muita", "muitos", "muitas", "muitíssimo", "muitíssima", "mui",
                    "fortuito", "circuito", "gratuito", "cuidado", "mudo", "fui"]
# pt-PT reads the contraction ao as [aw], and the specs below keep their own
# reading of it or switch the rules off.
PINNED["pt-PT"] += ["ao", "Ao", "AO", "aos", "caos", "pau", "mau"]
PINNED["pt-BR"] += ["ao"]
# A pt-PT coda sibilant is voiced word-internally before a voiced consonant, and
# not word-initially. A word-internal [ʒ] takes the sibilant away before [ʒ].
PINNED["pt-PT"] += ["esbelto", "desdém", "musgo", "desligar", "mesmo", "abismo", "lesma",
                    "desmaio", "desregrado", "asno", "cisne", "desnível", "esnobe", "desnudar",
                    "desgelo", "desjejum", "disjunto", "desjeito", "snobe", "slogan", "smoking",
                    "sbirro", "pasta", "mesquita", "esfera", "chmm"]
for code in ["pt-PT-x-acores", "pt-PT-x-alentejo", "pt-PT-x-alfena", "pt-PT-x-algarve",
             "pt-PT-x-aveiro", "pt-PT-x-beira", "pt-PT-x-braga", "pt-PT-x-coimbra",
             "pt-PT-x-lisbon", "pt-PT-x-madeira", "pt-PT-x-minho", "pt-PT-x-porto",
             "pt-PT-x-sao-miguel", "pt-PT-x-terceira", "pt-PT-x-tras-os-montes",
             "pt-PT-x-viana", "pt-MO"]:
    PINNED.setdefault(code, []).extend(["muito", "ao", "mesmo", "desdém"])
for code in ["pt-AO", "pt-CV", "pt-GW", "pt-MZ", "pt-ST", "pt-TL", "ext-PT-x-barranco", "aoa",
             "cri", "idb", "kea", "mcm", "mzs", "pap", "pov", "pre", "vkp", "pt-PT-x-medieval"]:
    PINNED.setdefault(code, []).extend(["muito", "ao", "mesmo", "desdém", "pasta"])
# pt-BR: a stressed final ⟨el ol⟩ has an open vowel, and an unstressed one, ⟨gol⟩
# and a coda ⟨l⟩ inside the word keep their vowel. pt-UY switches the rules off.
PINNED["pt-BR"] += ["papel", "mel", "hotel", "aluguel", "pastel", "gel", "futebol", "farol",
                    "lençol", "girassol", "caracol", "gol", "nível", "móvel", "túnel", "solto",
                    "feltro", "amor", "vez"]
for code in ["pt-BR-x-bahia", "pt-BR-x-ce", "pt-BR-x-floripa", "pt-BR-x-fluminen", "pt-BR-x-mg",
             "pt-BR-x-norte", "pt-BR-x-pr", "pt-BR-x-recife", "pt-BR-x-rj", "pt-BR-x-sp",
             "pt-BR-x-sul", "pt-BR-x-caipira", "pt-BR-x-cuiaba", "pt-UY"]:
    PINNED.setdefault(code, []).extend(["papel", "lençol", "muito"])
# pt-BR Djavan takes the final stress of its spelling, and each child keeps the
# segments of its own rules. ⟨an⟩ does not draw the stress by itself.
PINNED["pt-BR"] += ["barman"]
for code in ["pt-BR-x-floripa", "pt-BR-x-recife", "pt-BR-x-cuiaba", "pt-UY"]:
    PINNED.setdefault(code, []).append("Djavan")
# nl: a coda obstruent devoices inside the one-slot keys ⟨veld⟩ and ⟨berg⟩.
PINNED["nl-NL"] += ["veldkeuken", "veldslag", "veldpost", "veldtocht", "velden", "veldbed",
                    "slagveld"]
PINNED["nl-NL-x-polder"] += ["veldslag"]
PINNED["nl-NL-x-groninge"] += ["veldkeuken"]
PINNED["nl-NL-x-limburg"] += ["veldslag"]
PINNED["nl-BE"] += ["veldslag", "veldpost", "bergtop", "bergketen", "gebergte", "velden",
                    "veldbed", "bergen", "berg"]
PINNED["nl-BE-x-antwerp"] += ["veldslag", "bergtop", "bergketen", "gebergte", "Aarveld"]
# nl: ⟨qu⟩ reads [kʋ], and [k] in the French loans the guide lists.
for code in ["nl-NL", "nl-NL-x-polder", "nl-NL-x-groninge", "nl-NL-x-limburg", "nl-BE",
             "nl-BE-x-antwerp"]:
    PINNED[code] += ["quiz", "quota", "quantum", "equator", "aquarel", "eloquent", "frequent",
                     "requiem", "colloquium", "queue", "quarantaine", "quotiënt", "equipe",
                     "etiquette", "cheque", "croquet", "calqueren", "communiqué", "huurquote",
                     "pubquiz"]
PINNED["nl-NL"] += ["enquête", "etiquettes"]
# cnr reads both Montenegrin alphabets. С́ and З́ are a base letter and U+0301,
# which has no precomposed form.
PINNED["cnr"] = ["с́екира", "с́евер", "с́еница", "с́еме", "пос́ећи", "гус́еница", "мус́", "з́еница",
                 "из́ести", "из́елица", "коз́и", "З́агора", "љубав", "Њујорк", "џем", "ЏЕМ", "ЉЕТО",
                 "ЊЕДРА", "рђа", "прст", "крст", "смртан", "срп", "Подгорица", "Никшић", "Жабљак",
                 "расходовати", "читај", "фонем", "бизин", "śekira", "źenica", "ljubav", "voda"]
# pt-BR breaks a post-tonic [t] before a nasal and [k] before [n]. The southern specs
# keep the cluster, and the clusters that no source gives stay.
PINNED["pt-BR"] += ["ritmo", "étnico", "técnico", "técnica", "acne", "dogma", "digno", "mogno",
                    "dracma"]
for code in ["pt-BR-x-sul", "pt-BR-x-floripa", "pt-BR-x-pr", "pt-UY", "pt-BR-x-rj"]:
    PINNED.setdefault(code, []).extend(["ritmo", "técnico"])
# ⟨uei⟩ after a consonant has the diphthong of ⟨ei⟩ in each spec that has the grapheme.
for code in ["ext-PT-x-barranco", "pt-AO", "pt-BR", "pt-BR-x-bahia", "pt-BR-x-caipira",
             "pt-BR-x-ce", "pt-BR-x-cuiaba", "pt-BR-x-floripa", "pt-BR-x-fluminen", "pt-BR-x-mg",
             "pt-BR-x-norte", "pt-BR-x-pr", "pt-BR-x-recife", "pt-BR-x-rj", "pt-BR-x-sp",
             "pt-BR-x-sul", "pt-CV", "pt-GW", "pt-MO", "pt-MZ", "pt-PT", "pt-PT-x-acores",
             "pt-PT-x-alentejo", "pt-PT-x-alfena", "pt-PT-x-algarve", "pt-PT-x-aveiro",
             "pt-PT-x-beira", "pt-PT-x-braga", "pt-PT-x-coimbra", "pt-PT-x-lisbon",
             "pt-PT-x-madeira", "pt-PT-x-medieval", "pt-PT-x-minho", "pt-PT-x-porto",
             "pt-PT-x-sao-miguel", "pt-PT-x-terceira", "pt-PT-x-tras-os-montes", "pt-PT-x-viana",
             "pt-ST", "pt-TL", "pt-UY", "roa-x-galaico-pt"]:
    PINNED.setdefault(code, []).extend(["bueiro", "cueiro", "continuei", "feira"])
# A plural keeps the stress of its singular when the loss stands before the stress.
# A loss after the stress keeps the count from the start.
for code in ["pt-BR", "pt-PT"]:
    PINNED[code] += ["distribuidoras", "distribuidores", "construidoras", "atribuidores",
                     "bueiros", "osteointegradas", "jersey"]
PINNED["pt-PT-x-alentejo"] += ["teatro", "diabo", "havaiano", "coiote"]
PINNED.setdefault("ca", []).extend(["lingüístics", "aqüífers", "redueixin"])
PINNED["nl-BE"] += ["vertrouwen", "betrouwen"]
# A stressed pt-PT ⟨a⟩ is [a], and [ɐ] before an onset nasal. Each spec below takes the
# rule, keeps an open vowel, gives the nasal vowel, or switches the rule off.
PINNED["pt-PT"] += ["ar", "arte", "ato", "água", "cama", "ramo", "banho", "ano", "amo", "ama",
                    "anho", "falamos", "falámos", "amámos", "amar", "abrir", "manhã", "a", "as",
                    "campo", "ambos", "alto", "montanha"]
for code in ["pt-PT-x-lisbon", "pt-PT-x-tras-os-montes", "pt-PT-x-beira", "pt-PT-x-coimbra",
             "pt-PT-x-aveiro", "pt-PT-x-minho", "pt-PT-x-braga", "pt-PT-x-viana", "pt-PT-x-porto",
             "pt-PT-x-alfena", "pt-PT-x-alentejo", "pt-PT-x-algarve", "pt-AO", "pt-CV", "pt-GW",
             "pt-MZ", "pt-ST", "pt-TL", "ext-PT-x-barranco", "pt-MO", "pt-PT-x-acores",
             "pt-PT-x-madeira", "pt-PT-x-sao-miguel", "pt-PT-x-terceira", "aoa", "cri", "idb",
             "kea", "mcm", "mzs", "pap", "pov", "pre", "vkp"]:
    PINNED.setdefault(code, []).extend(["cama", "ano", "ramo", "banho", "ar", "arte"])
PINNED["pt-PT-x-terceira"] += ["gato"]
PINNED["ext-PT-x-barranco"] += ["areia"]
# pt-BR reads a coda ⟨x⟩ as the coda sibilant of the spec and ⟨xc⟩ as one /s/.
PINNED["pt-BR"] += ["texto", "sexta", "explicar", "expor", "extra", "exceção", "exceto",
                    "excesso", "excluir", "Vixnu", "exato", "festa"]
for code in ["pt-BR-x-rj", "pt-BR-x-norte", "pt-BR-x-recife", "pt-BR-x-floripa", "pt-BR-x-cuiaba"]:
    PINNED.setdefault(code, []).extend(["festa", "sexta", "mesmo", "Vixnu", "excesso"])

# The code points of the viramas this catalog actually uses.
VIRAMAS = {0x094D, 0x09CD, 0x0A4D, 0x0ACD, 0x0B4D, 0x0BCD, 0x0C4D, 0x0CCD,
           0x0D4D, 0x0D3B, 0x0E3A, 0x1039, 0x17D2, 0x0F84, 0x1714}


def _spec(lang):
    try:
        return get(lang)
    except Exception:
        return None


def _exceptions(spec, limit=None):
    words = list(spec.word_exceptions or {})
    return words[:limit] if limit else words


def _is_combining(ch):
    return unicodedata.combining(ch) != 0


def _combining_keys(spec):
    return [k for k in spec.graphemes if len(k) == 1 and _is_combining(k)]


def _virama_key(spec):
    for k in spec.graphemes:
        if len(k) == 1 and ord(k) in VIRAMAS:
            return k
    return None


def _consonants(spec, limit=6):
    dependent = set(spec.dependent_vowels or [])
    preposed = set(spec.preposed_vowels or [])
    out = []
    for k in spec.graphemes:
        if len(k) != 1 or not k.isalpha():
            continue
        if _is_combining(k) or k in dependent or k in preposed:
            continue
        if ord(k) in VIRAMAS:
            continue
        out.append(k)
        if len(out) >= limit:
            break
    return out


def _letters(spec, limit=12):
    return [k for k in spec.graphemes
            if k and len(k) <= 2 and k[0].isalpha() and not _is_combining(k[0])][:limit]


def _doubled(spec, limit=10):
    out = []
    for k in spec.graphemes:
        if len(k) == 2 and k[0] == k[1] and k[0].isalpha():
            out.append(k)
        if len(out) >= limit:
            break
    return out


def _multi_reading(spec, limit=8):
    return [k for k, v in spec.graphemes.items() if len(v) > 1][:limit]


def _dedupe(pairs):
    seen, out = set(), []
    for pair in pairs:
        key = tuple(pair)
        if pair[1].strip() and key not in seen:
            seen.add(key)
            out.append(list(pair))
    return out


def build():
    cats = {}

    # 1. the six named languages: their data, plus the pinned confirmations.
    six = []
    for lang in SIX:
        spec = _spec(lang)
        if spec is not None:
            six += [[lang, w] for w in _exceptions(spec)]
        six += [[lang, w] for w in PINNED.get(lang, [])]
    # Pinned rows for specs outside the six.
    six += [[lang, w] for lang in PINNED if lang not in SIX for w in PINNED[lang]]
    cats["six"] = _dedupe(six)

    # 2. Arabic: exceptions, shadda + harakat spellings, presentation forms.
    ar = []
    for lang in ARABIC:
        spec = _spec(lang)
        if spec is None:
            continue
        ar += [[lang, w] for w in _exceptions(spec)]
        keys = list(spec.graphemes)
        harakat = [k for k in keys if len(k) == 1 and _is_combining(k)]
        consonants = [k for k in keys if len(k) == 1 and k.isalpha() and not _is_combining(k)]
        for c in consonants[:6]:
            ar.append([lang, c + SHADDA])
            ar.append([lang, c + SHADDA + "\u064e"])
            ar.append([lang, c + "\u064e" + SHADDA])
            if harakat:
                ar.append([lang, c + harakat[0]])
        for lig in ["\ufeed", "\ufb71", "\ufb73", "\ufb75", "\ufb6f"]:
            ar.append([lang, lig])
        for c in consonants[:10:2]:
            for c2 in consonants[2:4]:
                ar.append([lang, c + c2 + "\u064f" + c2])
    cats["arabic"] = _dedupe(ar)

    # 3. abugidas: inherent vowel, conjunct, dependent and preposed vowels.
    ab = []
    for lang in ABUGIDAS:
        spec = _spec(lang)
        if spec is None:
            continue
        ab += [[lang, w] for w in _exceptions(spec)]
        ab += [[lang, w] for w in PINNED.get(lang, [])]
        consonants = _consonants(spec, 5)
        virama = _virama_key(spec)
        dependent = [k for k in (spec.dependent_vowels or []) if k in spec.graphemes][:3]
        preposed = [k for k in (spec.preposed_vowels or []) if k in spec.graphemes][:2]
        for c in consonants:
            ab.append([lang, c])
            ab.append([lang, c + c])
            if virama:
                ab.append([lang, c + virama])
                ab.append([lang, c + virama + c])
            for d in dependent:
                ab.append([lang, c + d])
                if virama:
                    ab.append([lang, c + virama + c + d])
            for p in preposed:
                ab.append([lang, p + c])
    cats["abugida"] = _dedupe(ab)

    # 4. tone languages.
    to = []
    for lang in TONE_LANGS:
        spec = _spec(lang)
        if spec is None:
            continue
        to += [[lang, w] for w in _exceptions(spec)]
        keys = [k for k in spec.graphemes if k][:14]
        to += [[lang, k] for k in keys]
        for c in _consonants(spec, 4):
            to.append([lang, c])
        for i in range(0, min(len(keys) - 1, 8), 2):
            to.append([lang, keys[i] + keys[i + 1]])
    cats["tone"] = _dedupe(to)

    # 5. combining marks: base + mark, bare marks, NFD-style spellings.
    cm = []
    for lang in COMBINING:
        spec = _spec(lang)
        if spec is None:
            continue
        marks = _combining_keys(spec)[:6]
        letters = _letters(spec, 6)
        for base in letters[:4]:
            for mark in marks[:3]:
                cm.append([lang, base + mark])
        for mark in marks[:4]:
            cm.append([lang, mark])
        for pair in letters[:6:2]:
            index = letters.index(pair)
            if index + 1 < len(letters):
                cm.append([lang, letters[index] + letters[index + 1]])
    cats["combining_marks"] = _dedupe(cm)

    # 6. viramas: C+v, C+v+C, C+v+C+v+C.
    vi = []
    for lang in ABUGIDAS:
        spec = _spec(lang)
        if spec is None:
            continue
        virama = _virama_key(spec)
        consonants = _consonants(spec, 4)
        if not virama:
            continue
        for c in consonants:
            vi.append([lang, c + virama])
            vi.append([lang, c + virama + c])
            vi.append([lang, c + virama + consonants[0]])
            vi.append([lang, consonants[0] + virama + c])
            vi.append([lang, c + virama + c + virama + c])
    cats["virama"] = _dedupe(vi)

    # 7. preposed vowels: bare, P+C, P+C+C.
    pv = []
    for lang in PREPOSED:
        spec = _spec(lang)
        if spec is None:
            continue
        preposed = [k for k in (spec.preposed_vowels or [])]
        consonants = _consonants(spec, 4)
        for p in preposed[:5]:
            pv.append([lang, p])
            for c in consonants[:2]:
                pv.append([lang, p + c])
                pv.append([lang, p + c + c])
    cats["preposed_vowel"] = _dedupe(pv)

    # 8. geminates: doubled keys, doubled+X, shadda, collapse_geminates specs.
    ge = []
    for lang in GEMINATE:
        spec = _spec(lang)
        if spec is None:
            continue
        letters = _letters(spec, 10)
        for key in _doubled(spec, 8):
            ge.append([lang, key])
            if letters:
                ge.append([lang, key + letters[0]])
                ge.append([lang, letters[0] + key])
        for c in letters[:4]:
            if c.isalpha() and len(c) == 1:
                ge.append([lang, c + c])
                ge.append([lang, c + c + c])
        ge += [[lang, w] for w in _exceptions(spec, 6)]
    for lang in GEMINATE_AR:
        spec = _spec(lang)
        if spec is not None:
            ge += [[lang, w] for w in _exceptions(spec, 10)]
    cats["geminate"] = _dedupe(ge)

    # 9. clitics: declared cliticless words, apostrophe forms, host-attached.
    cl = []
    for lang in CLITIC:
        spec = _spec(lang)
        if spec is None:
            continue
        words = list((spec.stress.cliticless_words if spec.stress else []) or [])
        cl += [[lang, w] for w in words]
        cl += [[lang, w] for w in _exceptions(spec) if "'" in w or "\u2019" in w]
        for w in words[:8]:
            cl.append([lang, w + "'"])
            cl.append([lang, "'" + w])
            cl.append([lang, w + w])
        for key in [k for k in list(spec.graphemes)[:40] if k and k.isalpha()][:4]:
            cl.append([lang, key])
    cats["clitic"] = _dedupe(cl)

    # 10. multi-candidate allophony: graphemes with >1 reading, rule targets.
    mc = []
    for lang in MULTI:
        spec = _spec(lang)
        if spec is None:
            continue
        for key in _multi_reading(spec, 10):
            mc.append([lang, key])
            mc.append([lang, key + key])
            base = "a" if "a" in spec.graphemes else key
            if spec.allophone_rules:
                mc.append([lang, base + key])
        mc += [[lang, w] for w in _exceptions(spec, 12)]
        if spec.allophone_rules:
            phonemes = [r.phonemes[0] for r in spec.allophone_rules if r.phonemes][:4]
            for p in phonemes:
                for key in _multi_reading(spec, 3):
                    mc.append([lang, key + p if len(p) < 3 else key])
    cats["multi_candidate_allophony"] = _dedupe(mc)

    return cats


if __name__ == "__main__":
    counts = {k: len(v) for k, v in build().items()}
    print(counts, sum(counts.values()))
