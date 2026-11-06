#!/usr/bin/env python3
"""Token-model parity harness: C++ `tokens` CLI vs the reference Python
package (orthography2ipa.phonetok.PhonetokTokenizer).

Compares the COMPLETE token stream — kind, grapheme, ipa candidates,
code-point position and length — for a battery of representative inputs
covering Latin maximal munch, casing, NFC, punctuation/digit claiming,
Arabic normalization and gemination, abugidas (virama, dependent and
preposed vowels, inherent vowels, silent marks), Hangul decomposition,
and the vowel-gated digraph back-off.

Usage: token_parity.py <path-to-cli> [--data DIR]

The reference package is located via $O2I_PYTHON_ROOT, else the sibling
directory of this repository (../orthography2ipa). The harness fails
loudly (exit 1) when the reference is unavailable.
"""
import json
import os
import pathlib
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
CPP_ROOT = HERE.parent

reference = os.environ.get("O2I_PYTHON_ROOT") or str(CPP_ROOT.parent / "orthography2ipa")
if not (pathlib.Path(reference) / "orthography2ipa" / "phonetok.py").is_file():
    print(f"reference package not found at {reference!r} (set O2I_PYTHON_ROOT)")
    sys.exit(1)
sys.path.insert(0, reference)

from orthography2ipa.registry import get  # noqa: E402
from orthography2ipa.phonetok import PhonetokTokenizer  # noqa: E402

# (lang, text) pairs. Representative of TODO "Reference Regression Gate":
# Arabic, abugidas, tone languages, combining marks, viramas, preposed
# vowels, geminates, clitics and multi-candidate allophony.
CASES = [
    # Latin maximal munch + casing + NFC
    ("pt-PT", "chuva"),
    ("pt-PT", "CHUVA"),
    ("pt-PT", "café"),          # NFD input → NFC
    ("pt-PT", "CH 3☕."),
    ("en-GB", "the cat"),
    ("es-ES", "seguro"),        # gu digraph back-off
    ("es-ES", "agua"),
    ("es-ES", "guarda"),
    ("es-ES", "guitarra"),
    ("fr-FR", "agneau"),
    ("ru", "мама"),
    ("tr", "Istanbul İzmir Iı"),  # Turkish casing
    ("el", "όλα"),               # Greek fold_diacritics
    # Arabic: presentation forms, shadda, harakat ordering, commas
    ("ar", "عمّ"),
    ("ar", "عَمَّ"),
    ("ar", "السَّلَامُ عَلَيْكُمْ"),
    ("ar", "ﻻ ﻷ ﻵ ﻹ"),          # lam-alif ligatures
    ("ar", "في، البيتِ"),
    ("ar-x-gulf", "پشڪ"),        # Perso-Arabic letters
    # Devanagari: virama, inherent vowel, dependent vowels, vocalic R
    ("hi", "कृष्ण"),
    ("hi", "क्या"),
    ("hi", "राम"),
    ("bn", "বাংলা"),
    ("sa", "क्रम"),
    # Thai/Lao: preposed vowels, tone marks, silent marker heads, coda rule
    ("th", "ก่อน"),
    ("th", "ไหม"),
    ("th", "เก"),
    ("th", "โหมด"),
    ("th", "คลอโรฟอร์ม"),
    ("th", "แหล่"),
    ("lo", "ເອກ"),
    ("lo", "ເຫຼົ້າ"),
    ("lo", "ດອກ"),
    ("lo", "ເບິ່ງ"),
    # Khmer register shifters, Myanmar rhyme units, Tibetan subjoined stack
    ("km", "ប៉ង"),
    ("my", "ကန်"),
    ("bo", "ཁམས"),
    ("bo", "གསུམ"),
    # Hangul canonical decomposition
    ("ko", "한국"),
    # Punctuation-claimed graphemes / digits as graphemes where declared
    ("tet", "naran"),           # Tetum apostrophe specs exist as tet-x-*; use base
    # Tone languages with combining marks
    ("vi", "cà phê"),
    ("yo", "bábá"),
    ("zh", "你好"),
    ("ja", "こんにちは"),
    # Unknown-heavy input
    ("pt-PT", "xyzZYX42?!"),
]


def expected_tokens(lang, text):
    tok = PhonetokTokenizer(get(lang))
    return [
        {
            "kind": t.kind.name,
            "grapheme": t.grapheme,
            "ipa": list(t.ipa),
            "position": t.position,
            "length": t.length,
        }
        for t in tok.tokenize(text)
    ]


def catalog_sweep():
    """Every spec in the catalog, tokenizing its own real data: up to 8
    word_exceptions words and the first 12 grapheme-table keys joined
    pairwise (maximal munch across key boundaries)."""
    from orthography2ipa.registry import available_codes, get
    cases = []
    for code in available_codes():
        try:
            spec = get(code)
        except Exception:
            continue
        words = list(spec.word_exceptions or {})[:8]
        keys = [k for k in list(spec.graphemes)[:12] if k]
        for i in range(0, len(keys) - 1, 2):
            words.append(keys[i] + keys[i + 1])
        for word in words[:20]:
            if word.strip():
                cases.append((code, word))
    return cases


def main():
    binary = pathlib.Path(os.path.abspath(sys.argv[1]))  # avoid a PATH lookup shadowing the local build
    data_dir = None
    if "--data" in sys.argv:
        i = sys.argv.index("--data")
        data_dir = sys.argv[i + 1]
    args = [str(binary)]
    if data_dir:
        args += ["--data", data_dir]
    args += ["tokens"]
    failures = 0
    cases = list(CASES)
    if "--sweep" in sys.argv:
        cases += catalog_sweep()
    for lang, text in cases:
        expected = json.dumps(expected_tokens(lang, text), ensure_ascii=False,
                               separators=(",", ":"))
        try:
            actual = subprocess.check_output(args + [lang, text], text=True).strip()
        except subprocess.CalledProcessError as e:
            print(f"FAIL {lang} {text!r}: CLI error: {e.stderr}")
            failures += 1
            continue
        if actual != expected:
            failures += 1
            print(f"FAIL {lang} {text!r}")
            print(f"  python: {expected}")
            print(f"  cpp:    {actual}")
    print(f"token parity: {len(cases) - failures}/{len(cases)} passed")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
