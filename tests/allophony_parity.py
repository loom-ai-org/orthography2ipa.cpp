#!/usr/bin/env python3
"""Allophony-rescorer parity harness: the C++ engine's context-aware
allophone rescorer (src/allophony.cpp + src/rescorer.cpp, porting
allophony.py + rescorer.py) vs the reference Python package.

Compares the final word transcription — the output of the full per-word engine
pipeline whose allophone stage is now the compiled AllophoneRescorer
running at the lattice-slot seam, before the nasal-carrier guard and beam
path selection — for a battery of rule-feature representatives plus the
word_exceptions of every spec that declares allophone_rules.

The allophone rules are evaluated against each slot's ORIGINAL top
candidate context (pre-rescore lattice state), which is why the flat
apply_allophony stand-in could not express them; the known cases it got
wrong (fr nasal absorption, fo shorten/retroflex, sv closed-syllable
shortening) are in the battery below.

Usage: allophony_parity.py <path-to-cli> [--sweep]

The reference package is located via $O2I_PYTHON_ROOT, else the sibling
directory of this repository (../orthography2ipa). The harness fails
loudly (exit 1) when the reference is unavailable.
"""
import os
import pathlib
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
CPP_ROOT = HERE.parent

reference = os.environ.get("O2I_PYTHON_ROOT") or str(CPP_ROOT.parent / "orthography2ipa")
if not (pathlib.Path(reference) / "orthography2ipa" / "allophony.py").is_file():
    print(f"reference package not found at {reference!r} (set O2I_PYTHON_ROOT)")
    sys.exit(1)
sys.path.insert(0, reference)

from orthography2ipa.g2p import G2P  # noqa: E402
from orthography2ipa.registry import available_codes, get  # noqa: E402

CASES = [
    # The confirmed mismatches the flat apply_allophony stand-in produced
    # (handover, TODO section 5): fr ⟨n⟩-absorption, fo shorten/retroflex,
    # sv closed-syllable shortening, fr ⟨gn⟩ palatal, en ⟨ll⟩ digraph.
    ("fr-FR", ["agneau", "jeune", "bonne", "bonjour", "vienne", "grenouille"]),
    ("fo", ["fiskur", "húsið", "bátur", "rendur"]),
    ("sv", ["kvinnna", "kvinnor", "kväll", "hälla"]),
    ("en-GB", ["hello", "silly", "villain"]),
    # Phoneme-neighbour and two-away conditions (Russian palatalisation
    # reads the grapheme TWO away by its underlying phoneme).
    ("ru", ["гости", "мать", "молоко", "вода", "город"]),
    # Gahawa-style epenthesis, pharyngeal/emphatic neighbours, geminate
    # atomicity (a rule fired outside a geminate must not split it).
    ("ar", ["عمّ", "بخّ", "كتاب", "مدرّسة", "مدرسة"]),
    # Abugida CV-unit slots (segmentation inside a multi-phoneme candidate)
    # and vowel-deleting rules guarded by requires_other_nucleus.
    ("hi", ["कृष्ण", "क्या", "राम"]),
    ("ta", ["கக", "க்க"]),
    ("pt-PT", ["chuva", "panela", "ponte"]),
    ("ga", ["cailín", "belfast"]),
    ("es-ES", ["seguro", "agua"]),
    ("ko", ["한국"]),
    ("yi", ["טאָג"]),
]


def catalog_sweep():
    """Every spec's word_exceptions — the engine transcription of each, run
    through both pipelines and compared. This sweeps the rescorer over
    every allophone_rules spec in the catalog (322 of them)."""
    import logging
    logging.disable(logging.CRITICAL)
    cases = []
    for code in available_codes():
        try:
            spec = get(code)
        except Exception:
            continue
        if not getattr(spec, "allophone_rules", None):
            continue
        for word in (spec.word_exceptions or {}):
            if word.strip():
                cases.append((code, word))
    return cases


def main():
    binary = pathlib.Path(os.path.abspath(sys.argv[1]))  # avoid a PATH lookup shadowing the local build
    import logging
    logging.disable(logging.CRITICAL)
    cases = [(lang, word) for lang, words in CASES for word in words]
    if "--sweep" in sys.argv:
        cases += catalog_sweep()
    failures = 0
    checked = 0
    for lang, word in cases:
        try:
            expect = G2P(lang).transcribe_word(word)
        except Exception:
            continue  # the reference cannot answer this case; skip it
        try:
            # `word`, not `transcribe`: this battery compares the reference's
            # per-word pipeline (transcribe_word), which does no word splitting.
            # The sentence path re-tokenizes a shadda-expanded surface, so
            # comparing it against transcribe_word invents diffs.
            actual = subprocess.check_output(
                [str(binary), "word", lang, word],
                text=True, stderr=subprocess.PIPE).strip()
        except subprocess.CalledProcessError as e:
            print(f"FAIL {lang} {word!r}: CLI error: {e.stderr}")
            failures += 1
            continue
        checked += 1
        if actual != expect:
            failures += 1
            print(f"FAIL {lang} {word!r}")
            print(f"  python: {expect}")
            print(f"  cpp:    {actual}")
    print(f"allophony parity: {checked - failures}/{checked} passed")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
