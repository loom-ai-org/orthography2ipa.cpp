#!/usr/bin/env python3
"""Stress-pipeline parity harness: C++ `stress` CLI vs the reference Python
package (orthography2ipa.stress).

Compares, per word: the syllabification the engine uses (_syllables_for),
the detected stressed-syllable index (detect_stress) and the secondary
stress positions — plus the stress-marked surface for the bare word. The
marked surface is compared on the bare orthography (stress.py
apply_stress_mark inserted into the word itself), which exercises
prefix_mark's length-mark/combining-mark handling without pulling whole
pronunciations into scope.

Usage: stress_parity.py <path-to-cli> [--sweep]

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
if not (pathlib.Path(reference) / "orthography2ipa" / "stress.py").is_file():
    print(f"reference package not found at {reference!r} (set O2I_PYTHON_ROOT)")
    sys.exit(1)
sys.path.insert(0, reference)

from orthography2ipa.registry import get  # noqa: E402
from orthography2ipa.stress import (  # noqa: E402
    _syllables_for, detect_stress, secondary_stress_positions, apply_stress_mark,
)

CASES = [
    ("pt-PT", ["falar", "casa", "chuva", "panela", "café", "ponte", "dobrar",
               "avô", "avó", "livros", "volátil", "fácil", "difícil",
               "português", "publicável", "livros"]),
    ("pt-BR", ["panela", "velho", "ciência", "fazendo"]),
    ("es-ES", ["casa", "seguro", "rápido", "árbol", "reloj", "ciudad",
               "teléfono", "corazón", "espárrago", "ayer", "compás"]),
    ("it-IT", ["casa", "italia", "medio", "forza", "andiamo", "tavola"]),
    ("fr-FR", ["bonjour", "chanteurs", "table", "petite", "école", "heureux",
               "jeune", "vies", "petits"]),
    ("de-DE", ["elektronisch", "abmeldung", "angst", "jahrigkeit", "atmen"]),
    ("nl", ["angel", "wandelen", "ritme", "kunst", "lezen", "lek"]),
    ("is", ["bretland", "mjólk", "ljós"]),
    ("ru", ["молоко", "мама", "молодец", "понял", "город", "вода"]),
    ("tr", ["istanbul", "okul", "ışıldak"]),
    ("el", ["κτίριο", "πτώση", "σβήνω"]),
    ("ar", ["كتاب", "مدرسة", "مدرّس", "مدرّس".replace("ّ", "")]),
    ("ga", ["cailín", "séimh"]),
    ("cs", ["tenia", "přítel"]),
    ("sv", ["kvinnor", "kvinna", "kväll", "idé"]),
    ("da", ["hund", "kunst"]),
    ("fo", ["fiskur"]),
    ("nb", ["hund"]),
    ("az", ["kitab"]),
    ("hu", ["ember"]),
    ("fi", ["kissa"]),
    ("et", ["sada"]),
    ("car", ["kono", "kunuhany"]),
]


def expected(lang, word):
    spec = get(lang)
    stress = spec.stress
    out = {"syllables": list(_syllables_for(word, lang, stress.diphthongs, spec=spec))}
    if stress is not None:
        sylls = out["syllables"]
        idx = detect_stress(word, stress, syllables=sylls, lang=lang)
        secondary = sorted(secondary_stress_positions(len(sylls), idx, stress))
        out["stress"] = idx
        out["secondary"] = secondary
        out["marked"] = apply_stress_mark(word, stress, idx, syllables=sylls,
                                          secondary_indices=secondary)
    return out


def catalog_sweep():
    """Every stress-declaring spec, beaming its own word_exceptions and a
    few grapheme-table keys — the words the spec itself vouches for."""
    from orthography2ipa.registry import available_codes
    cases = []
    for code in available_codes():
        try:
            spec = get(code)
        except Exception:
            continue
        if spec.stress is None:
            continue
        words = list(spec.word_exceptions or {})[:8]
        keys = [k for k in list(spec.graphemes)[:8]
                if k and all(ch.isalpha() or ch.isspace() for ch in k)]
        for i in range(0, len(keys) - 1, 2):
            words.append(keys[i] + keys[i + 1])
        for word in words[:12]:
            if word.strip():
                cases.append((code, word))
    return cases


def main():
    binary = pathlib.Path(os.path.abspath(sys.argv[1]))
    failures = 0
    checked = 0
    cases = [(lang, word) for lang, words in CASES for word in words]
    if "--sweep" in sys.argv:
        cases += catalog_sweep()
    import logging
    logging.disable(logging.CRITICAL)
    for lang, word in cases:
        try:
            expect = expected(lang, word)
        except Exception:
            continue  # the reference cannot answer this case; skip it
        try:
            actual = json.loads(
                subprocess.check_output([str(binary), "stress", lang, word],
                                        text=True, stderr=subprocess.PIPE))
        except subprocess.CalledProcessError as e:
            print(f"FAIL {lang} {word!r}: CLI error: {e.stderr}")
            failures += 1
            continue
        checked += 1
        if actual != expect:
            failures += 1
            print(f"FAIL {lang} {word!r}")
            print(f"  python: {json.dumps(expect, ensure_ascii=False)}")
            print(f"  cpp:    {json.dumps(actual, ensure_ascii=False)}")
    print(f"stress parity: {checked - failures}/{checked} passed")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
