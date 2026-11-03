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
    "ru": ["молоко"],
    "fr-FR": ["jeune", "agneau", "les", "amis"],
    "hi": ["कृष्ण", "क्या", "राम", "नमस्ते", "भारत", "किताब", "दो", "क्", "क्ष",
           "श्र", "मुक्ति", "हिन्दी", "बिल्ल्", "सिक्ख"],
    "lo": ["ເອກ", "ເຫຼົ້າ", "ດອກ", "ເບິ່ງ", "ກ", "ງ", "ຈ", "ສາວ", "ລາວ", "ພາສາ",
           "ຂອບໃຈ", "ໃຈ", "ແຜ່ນ", "ໂລກ", "ເຮືອນ", "ຊີວິດ", "ມື", "ບໍ່", "ແມ່", "ຄົນ"],
}

ABUGIDAS = ["hi", "bn", "ta", "te", "kn", "ml", "mr", "ne", "or", "as", "sa", "sid"]
TONE_LANGS = ["th", "lo", "vi", "yo", "km", "my", "bo", "ko", "kix", "zh", "pa"]
ARABIC = ["ar", "ar-IQ", "ar-LB"]
SHADDA = "\u0651"

# The combining-mark / virama / preposed-vowel / geminate / clitic / multi
# builders walk these lists; they are the languages the requirement names.
COMBINING = ["pt-PT", "el", "vi", "tr", "ar", "hi", "cs", "pl", "sv", "hu", "th", "lo", "yo"]
PREPOSED = ["th", "lo", "km", "my", "sa", "bn", "hi"]
GEMINATE = ["ar", "it-IT", "sv", "hu", "en-GB", "fo", "de-DE", "pt-PT", "es-ES", "nn", "hi", "ml"]
GEMINATE_AR = ["ar", "it-IT", "sv", "hu"]
CLITIC = ["fr-FR", "en-GB", "ru", "pt-PT", "ar", "ar-IQ", "it-IT", "ca", "ga", "he", "sv", "da"]
MULTI = ["fr-FR", "sv", "fo", "en-GB", "pt-PT", "ru", "it-IT", "de-DE", "is", "el", "hi", "th", "ko", "my"]

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
