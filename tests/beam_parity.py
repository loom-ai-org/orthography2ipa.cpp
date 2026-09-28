#!/usr/bin/env python3
"""Beam-model parity harness: C++ `beam` CLI vs the reference Python
package (orthography2ipa.phonetok.PhonetokTokenizer.ipa_beam).

Compares the COMPLETE beam — per-path segments, graphemes and additive
cost — for a battery of representative inputs (rank costs, weights,
nasal-tilde guards, allophone expansion, punctuation/digit claiming).
Scores are compared exactly: both sides derive -log costs from the same
double arithmetic, so byte equality is the regression gate.

Usage: beam_parity.py <path-to-cli> [--data DIR] [--sweep]

The reference package is located via $O2I_PYTHON_ROOT, else the sibling
directory of this repository (../orthography2ipa). The harness fails
loudly (exit 1) when the reference is unavailable.
"""
import json
import math
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

# (lang, text) pairs — same battery as token_parity.py, exercising the
# beam machinery end to end.
CASES = [
    ("pt-PT", "chuva"),
    ("pt-PT", "CHUVA"),
    ("pt-PT", "café"),
    ("pt-PT", "panela"),
    ("en-GB", "the cat"),
    ("es-ES", "seguro"),
    ("es-ES", "agua"),
    ("es-ES", "guarda"),
    ("es-ES", "guitarra"),
    ("fr-FR", "agneau"),
    ("fr-FR", "bonjour"),
    ("ru", "мама"),
    ("tr", "Istanbul İzmir Iı"),
    ("el", "όλα"),
    ("ar", "عمّ"),
    ("ar", "السَّلَامُ عَلَيْكُمْ"),
    ("hi", "कृष्ण"),
    ("hi", "क्या"),
    ("hi", "राम"),
    ("bn", "বাংলা"),
    ("sa", "क्रम"),
    ("th", "ก่อน"),
    ("th", "ไหม"),
    ("th", "แหล่"),
    ("lo", "ເອກ"),
    ("lo", "ເບິ່ງ"),
    ("km", "ប៉ង"),
    ("my", "ကန်"),
    ("bo", "ཁམས"),
    ("ko", "한국"),
    ("pt-PT", "xyzZYX42?!"),
]


def expected_beam(lang, text, width=8, expand_allophones=False):
    tok = PhonetokTokenizer(get(lang))
    return [
        {
            "ipa": p.ipa,
            "score": p.score,
            "graphemes": list(p.graphemes),
            "segments": list(p.segments),
        }
        for p in tok.ipa_beam(text, beam_width=width,
                              expand_allophones=expand_allophones)
    ]


def catalog_sweep():
    """Every spec in the catalog, beaming its own real data: up to 8
    word_exceptions words and the first 12 grapheme-table keys joined
    pairwise (maximal munch across key boundaries)."""
    from orthography2ipa.registry import available_codes
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
    binary = pathlib.Path(sys.argv[1])
    data_dir = None
    if "--data" in sys.argv:
        i = sys.argv.index("--data")
        data_dir = sys.argv[i + 1]
    failures = 0
    checked = 0
    cases = list(CASES)
    if "--sweep" in sys.argv:
        cases += catalog_sweep()
    for lang, text in cases:
        for expand in (False, True):
            expected = expected_beam(lang, text, expand_allophones=expand)
            args = [str(binary)]
            if data_dir:
                args += ["--data", data_dir]
            args += ["beam", lang, text]
            if expand:
                args.append("--allophones")
            try:
                actual = json.loads(
                    subprocess.check_output(args, text=True))
            except subprocess.CalledProcessError as e:
                print(f"FAIL {lang} {text!r} expand={expand}: CLI error: {e.stderr}")
                failures += 1
                continue
            checked += 1
            if actual != expected:
                failures += 1
                print(f"FAIL {lang} {text!r} expand={expand}")
                print(f"  python: {json.dumps(expected, ensure_ascii=False)}")
                print(f"  cpp:    {json.dumps(actual, ensure_ascii=False)}")
    print(f"beam parity: {checked - failures}/{checked} passed")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
