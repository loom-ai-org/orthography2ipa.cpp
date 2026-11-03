#!/usr/bin/env python3
"""lm.py / feats.py parity harness: the C++ phone-LM and distinctive-feature
distance port (src/lm.cpp + the appended half of src/feats.cpp) against the
reference Python package.

Pass `--strict` as the second argument to fail on ANY differing value; the
default tolerates only the documented known gap in
build/notes/worker_lm_feats.md section 5 (the four multi-code-point spellings
that src/feats.cpp:181 resolves differently from feats.py) and says so loudly.

The harness runs three steps and reports the union of their diffs:

  1. tests/_scratch/_lm_feats_ref.py   (reference values; needs the reference
     interpreter, because only it has ovos_spec_tools installed)
  2. the C++ check program, which recomputes every value from the same
     battery and case files
  3. tests/_scratch/_lm_feats_compare.py, which diffs the two dumps

Coverage: feats.phonetic_distance over every ordered pair of the battery's
phones (the IPA readings of a 300-spec catalog sample + curated table keys +
decomposed/multi-modifier composites + unknown phones), feats.is_vowel_phone
and feats.vectorize_phones per phone, lm.phoneme_embeddings for 15 specs, and
lm.build_ngram_lm + lm.perplexity for 264 word-list cases over 11 specs at
n = 1..4, including unmapped-character words and empty lists.

Usage: lm_parity.py <path-to-check-binary>

The check binary is tests/_scratch/lm_feats_check.cpp built against the port;
the exact g++ command line is in build/notes/worker_lm_feats.md. Exit 0 only
when every compared value is identical.
"""
import os
import pathlib
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
CPP_ROOT = HERE.parent
SCRATCH = HERE / "_scratch"
# The reference side needs numpy + the reference package importable from
# $O2I_PYTHON_ROOT. Order: $O2I_PYTHON, the interpreter RUNNING the harness
# (that is what a CI venv is: its python has the installed deps), then the
# documented local venv path this repository was developed against.
def _reference_python() -> str:
    env = os.environ.get("O2I_PYTHON")
    if env:
        return env
    for candidate in (sys.executable,
                      "/home/flavio/.venvs/piper/bin/python3"):
        if os.path.exists(candidate):
            return candidate
    return sys.executable


REFERENCE_PYTHON = _reference_python()


def run(command, label):
    print("$ " + " ".join(command))
    result = subprocess.run(command, cwd=str(CPP_ROOT), text=True,
                            capture_output=True)
    if result.stdout.strip():
        print(result.stdout.strip()[-1200:])
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
              f"build it first (command in build/notes/worker_lm_feats.md)")
        sys.exit(2)
    reference_python = os.environ.get("O2I_PYTHON", REFERENCE_PYTHON)
    if not os.path.exists(reference_python):
        print(f"reference interpreter missing: {reference_python!r} "
              f"(set $O2I_PYTHON)")
        sys.exit(1)

    extra = [a for a in sys.argv[2:] if a == "--strict"]
    run([reference_python, str(SCRATCH / "_lm_feats_ref.py")], "reference dump")
    run([checker], "C++ dump")
    comparison = run([sys.executable, str(SCRATCH / "_lm_feats_compare.py")] + extra, "compare")
    sys.exit(0)


if __name__ == "__main__":
    main()
