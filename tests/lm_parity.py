#!/usr/bin/env python3
"""lm.py / feats.py parity harness: the C++ phone-LM and distinctive-feature
distance port (src/lm.cpp + the appended half of src/feats.cpp) against the
reference Python package.

The harness runs three steps and fails on any differing value:

  1. tests/lm_feats_ref.py writes the battery, the cases and the reference
     values to tests/_scratch/ (it needs the reference interpreter, with numpy
     and ovos-spec-tools installed)
  2. the C++ check program recomputes every value from the same battery and
     case files
  3. tests/lm_feats_compare.py compares the two dumps byte for byte

Coverage: feats.phonetic_distance over every ordered pair of the battery's
phones (the IPA readings of a 300-spec catalog sample + curated table keys +
decomposed/multi-modifier composites + unknown phones), feats.is_vowel_phone
and feats.vectorize_phones per phone, lm.phoneme_embeddings for 18 specs, and
lm.build_ngram_lm + lm.perplexity for 264 word-list cases over 11 specs at
n = 1..4, including unmapped-character words and empty lists.

Usage: lm_parity.py <path-to-check-binary>

The check binary is orthography2ipa-check-lm_feats, built from
tests/check_lm_feats_driver.cpp. Exit 0 only when every compared value is
identical.
"""
import os
import pathlib
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
CPP_ROOT = HERE.parent
SCRATCH = HERE / "_scratch"
# The reference side needs numpy and the reference package importable from
# $O2I_PYTHON_ROOT: $O2I_PYTHON, else the interpreter running the harness.
REFERENCE_PYTHON = os.environ.get("O2I_PYTHON") or sys.executable
# Python 3.12 made sum() of floats compensated (Neumaier), and the port sums
# as 3.12 does. An older reference sums naively and gives other distances.
MIN_REFERENCE = (3, 12)


def run(command, label):
    print("$ " + " ".join(command))
    result = subprocess.run(command, cwd=str(CPP_ROOT), text=True,
                            capture_output=True)
    if result.stdout.strip():
        print(result.stdout.strip())
    if result.stderr.strip():
        print(result.stderr.strip())
    if result.returncode != 0:
        print(f"FAIL {label}: exit {result.returncode}\n{result.stderr[:4000]}")
        sys.exit(1)
    return result


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(2)
    checker = os.path.abspath(sys.argv[1])
    if not os.access(checker, os.X_OK):
        print(f"check binary not found or not executable: {checker}\n"
              f"build the orthography2ipa-check-lm_feats target first")
        sys.exit(2)
    if not os.path.exists(REFERENCE_PYTHON):
        print(f"reference interpreter missing: {REFERENCE_PYTHON!r} (set $O2I_PYTHON)")
        sys.exit(1)

    found = subprocess.run([REFERENCE_PYTHON, "-c", "import sys; print(*sys.version_info[:2])"],
                           text=True, capture_output=True, check=True).stdout.split()
    version = tuple(int(part) for part in found)
    if version < MIN_REFERENCE:
        print(f"FAIL reference interpreter: {REFERENCE_PYTHON} is Python {version[0]}.{version[1]}. "
              f"The port sums floats as Python {MIN_REFERENCE[0]}.{MIN_REFERENCE[1]} and later do, "
              f"so an older reference gives different distances. Set $O2I_PYTHON to Python "
              f"{MIN_REFERENCE[0]}.{MIN_REFERENCE[1]} or later.")
        sys.exit(1)
    # A dump left from an earlier run must never stand in for one that a step
    # failed to write.
    for stale in SCRATCH.glob("_lm_feats_*"):
        stale.unlink()

    run([REFERENCE_PYTHON, str(HERE / "lm_feats_ref.py")], "reference dump")
    run([checker], "C++ dump")
    run([sys.executable, str(HERE / "lm_feats_compare.py")], "compare")
    sys.exit(0)


if __name__ == "__main__":
    main()
