#!/usr/bin/env python3
"""Tone-syllable-model parity harness: the C++ computed-tone and
tone-mark-docking port (src/tone.cpp, porting tone.py) vs the reference
Python package.

Covers the three surfaces the tone model owns: computed tone lookup
(class x rime-shape x mark table over `_syllable_slots` maximal-onset
grouping), tone-mark docking to syllable edges (dock_tone_marks — the kix
convention), and their order in the word-final pipeline (computed tone ->
geminate collapse -> virama vowel -> docking -> stress). The th battery
includes the mark-on-onset spellings that shifted tone between neighbours
before the port (เป็นต้อง).

Usage: tone_parity.py <path-to-cli> [--sweep]

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
if not (pathlib.Path(reference) / "orthography2ipa" / "tone.py").is_file():
    print(f"reference package not found at {reference!r} (set O2I_PYTHON_ROOT)")
    sys.exit(1)
sys.path.insert(0, reference)

from orthography2ipa.g2p import G2P  # noqa: E402
from orthography2ipa.registry import available_codes, get  # noqa: E402

CASES = [
    # th: computed tone (class x shape x mark) and mai marks riding the
    # onset consonant — เป็นต้อง's mai tho names the tone of the syllable
    # the marked consonant OPENS.
    ("th", ["ง", "ก่อน", "ไหม", "แหล่", "เป็นต้อง", "ครับ", "ไม่", "คะ",
            "สวัสดี", "พูด", "ป้า", "ตา", "น้ำ", "กลางคืน", "ฉัน", "ข้าว",
            "เขา", "หนังสือ", "กราบ", "กระดาษ", "เพราะ", "ปู่", "ย่า"]),
    # kix: docking only (tone_marks_syllable_final, no computed table).
    ("kix", ["tsh", "ch", "ts", "sh", "th", "kh", "ph", "ny"]),
    # Tone neighbours without tone rules must be untouched.
    ("my", ["က", "ခ"]),
    ("bo", ["པ", "སྤ", "རྦ"]),
    ("lo", ["ກ", "ງ", "ຈ"]),
    ("km", ["ក", "ង"]),
]


def catalog_sweep():
    """Every tone spec's word_exceptions and grapheme keys."""
    import logging
    logging.disable(logging.CRITICAL)
    cases = []
    for code in available_codes():
        try:
            spec = get(code)
        except Exception:
            continue
        tone_rules = getattr(spec, "tone_rules", None)
        if tone_rules is None and not getattr(spec, "tone_marks_syllable_final", False):
            continue
        words = list(spec.word_exceptions or {})
        words += [k for k in list(spec.graphemes)[:12] if k]
        for word in words:
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
    print(f"tone parity: {checked - failures}/{checked} passed")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
