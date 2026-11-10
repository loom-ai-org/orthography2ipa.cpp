#!/usr/bin/env python3
"""Sentence-pipeline parity harness: the reference `G2P.transcribe(text)` over
MULTI-WORD input vs the C++ CLI `transcribe <lang> <text>`.

Every other harness compares single words, so nothing here before now exercised
the cross-word half of the pipeline: the declarative sandhi pass (whose contexts
are code-point regexes — matched over UTF-8 BYTES they silently stop firing, and
`regex_replace` cuts a multi-byte character in half and emits U+FFFD), the pause
flags that keep a rule inside its prosodic domain, the token-driven word splitter
that decides which apostrophes belong to a word, and the dialect transform that
reads the assembled sentence.

Usage: sentence_parity.py <path-to-cli> [--sweep]

The reference package is located via $O2I_PYTHON_ROOT, else the sibling
directory of this repository (../orthography2ipa). The harness fails loudly
(exit 1) when the reference is unavailable.
"""
import os
import pathlib
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
CPP_ROOT = HERE.parent

reference = os.environ.get("O2I_PYTHON_ROOT") or str(CPP_ROOT.parent / "orthography2ipa")
if not (pathlib.Path(reference) / "orthography2ipa" / "sandhi.py").is_file():
    print(f"reference package not found at {reference!r} (set O2I_PYTHON_ROOT)")
    sys.exit(1)
sys.path.insert(0, reference)

from orthography2ipa.g2p import G2P  # noqa: E402
from orthography2ipa.registry import available_codes, get  # noqa: E402

# (lang, text) — each line names the stage it exists to pin down.
CASES = [
    # Portuguese coda /s/ voicing before a vowel-initial word: the left
    # context is `^([ˈˌ]?)…` over code points, so the BYTE engine never
    # matched a stress-marked word and never voiced the s.
    ("pt-PT", "as aves"),
    ("pt-PT", "os dois"),
    ("pt-PT", "as aulas"),
    # A nasal vowel the rule names by its DECOMPOSED shape; the left rewrite
    # must not split a character in half (this produced U+FFFD before).
    ("pt-PT", "os irmãos"),
    ("pt-PT", "estás a ver"),
    ("pt-PT", "um dia"),
    # Catalan vowel contact: both sides of one boundary, and a rule guarded
    # by a negative lookahead so exactly one vowel is lost.
    ("ca", "els nens i les nenes"),
    ("ca", "la ea"),
    ("ca", "cap i casa"),
    ("ca-x-valencia", "els nens"),
    # French liaison, which the `‿` bridge character in the transform made a
    # byte-boundary casualty.
    ("fr-FR", "les amis"),
    ("fr-FR", "un homme"),
    ("fr-FR", "c''est à dire"),
    # Basque, Galician: word-final devoicing / coda rules over code points.
    ("eu", "bi aitona"),
    ("gl", "os nenos"),
    # Arabic sun-letter assimilation across the boundary.
    ("ar", "البيت الكبير"),
    ("ar-LB", "بُكْرَا الْيَوم"),
    ("ar-x-gulf", "البيت كبير"),
    # Pausal blocking: a comma is an IP boundary, so no rule may cross it.
    ("pt-PT", "as aves, as aves"),
    ("ca", "els nens, les nenes"),
    ("fr-FR", "les amis, les amies"),
    # The word splitter: an apostrophe the spec's grapheme table CLAIMS is
    # inside the word (Afrikaans `'n`, the en-GB possessive), and one it does
    # not is a break. Both faces of the ispunct stand-in were wrong.
    ("af", "'n man is 'n mens"),
    ("af", "hy's hier hier."),
    ("en-GB", "don't stop; it's over."),
    ("en-GB", "it's the cat's"),
    ("it-IT", "e anche"),
    ("ro-RO", "un a"),
    # Dialect transform applied to the assembled sentence (and NOT pushed back
    # into the per-word readings, which keep their pre-transform IPA).
    ("pt-PT", "o velho verde foi ver a vaca"),
]
DIALECT_CASES = [
    ("pt-PT", "o velho verde foi ver a vaca", "rionorese"),
    ("pt-PT", "as aves estão cá", "lisbon"),
    ("pt-PT", "a casa do senhor", "northern"),
    ("pt-PT", "o velho verde foi ver a vaca", "galician"),
    ("pt-PT", "a minha terra", "beira_baixa"),
    ("pt-PT", "ele disse isso", "algarve_barlavento"),
    ("pt-PT", "os livros meus", "leonese"),
]


def catalog_sweep():
    """Two-word sentences from every spec that declares sandhi rules, built
    from its own word_exceptions so both engines see real vocabulary."""
    import logging
    logging.disable(logging.CRITICAL)
    cases = []
    for code in available_codes():
        try:
            spec = get(code)
        except Exception:
            continue
        if not (getattr(spec, "sandhi_rules", None) or []):
            continue
        words = [w for w in (spec.word_exceptions or {}) if w.strip()]
        if len(words) < 2:
            continue
        for i in range(0, len(words) - 1, 2):
            pair = f"{words[i]} {words[i + 1]}"
            cases.append((code, pair))
            # The same pair with a comma between the words: a rule that fires
            # in the phrase must NOT fire across the pause.
            cases.append((code, f"{words[i]}, {words[i + 1]}"))
    return cases


def main():
    binary = pathlib.Path(os.path.abspath(sys.argv[1]))  # absolute: a relative path can hit a stale installed binary
    import logging
    logging.disable(logging.CRITICAL)
    cases = list(CASES)
    dialect = list(DIALECT_CASES)
    if "--sweep" in sys.argv:
        cases += catalog_sweep()
    failures = 0
    checked = 0
    invalid = 0

    def run(lang, text, dialect_profile=None):
        cmd = [str(binary), "transcribe", lang, text]
        if dialect_profile:
            cmd += ["--dialect", dialect_profile]
        done = subprocess.run(cmd, capture_output=True)
        if done.returncode != 0:
            return None, done.stderr.decode("utf-8", "replace").strip()
        out = done.stdout.decode("utf-8")
        # A U+FFFD in the output means a multi-byte character was cut in half
        # somewhere in the cross-word stages.
        return out.replace("\n", "").strip(), None

    for lang, text in cases:
        try:
            expect = G2P(lang).transcribe(text)
        except Exception:
            continue
        actual, err = run(lang, text)
        checked += 1
        if err is not None:
            failures += 1
            print(f"FAIL {lang} {text!r}: CLI error: {err}")
            continue
        if "\ufffd" in actual:
            invalid += 1
            failures += 1
            print(f"FAIL {lang} {text!r}: U+FFFD in output (a split character): {actual!r}")
            continue
        if actual != expect:
            failures += 1
            print(f"FAIL {lang} {text!r}")
            print(f"  python: {expect!r}")
            print(f"  cpp:    {actual!r}")
    for lang, text, profile in dialect:
        try:
            expect = G2P(lang, dialect_profile=profile).transcribe(text)
        except Exception as e:
            print(f"SKIP {lang} {text!r} [{profile}]: reference refused: {e}")
            continue
        actual, err = run(lang, text, profile)
        checked += 1
        if err is not None:
            failures += 1
            print(f"FAIL {lang} {text!r} [{profile}]: CLI error: {err}")
            continue
        if actual != expect:
            failures += 1
            print(f"FAIL {lang} {text!r} [{profile}]")
            print(f"  python: {expect!r}")
            print(f"  cpp:    {actual!r}")

    print(f"sentence parity: {checked - failures}/{checked} passed")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
