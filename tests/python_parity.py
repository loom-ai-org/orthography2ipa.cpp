#!/usr/bin/env python3
"""Small optional parity smoke test against the reference Python package."""
import os
import pathlib
import subprocess
import sys

reference = os.environ.get("O2I_PYTHON_ROOT")
if not reference:
    raise SystemExit(0)
sys.path.insert(0, reference)
from orthography2ipa import transcribe  # noqa: E402

binary = pathlib.Path(sys.argv[1])
cases = [("pt", "olá mundo"), ("es", "casa")]
for language, text in cases:
    native = subprocess.check_output([str(binary), "transcribe", language, text], text=True).strip()
    expected = transcribe(text, language)
    if native != expected:
        raise SystemExit(f"parity mismatch for {language} {text!r}: {native!r} != {expected!r}")
