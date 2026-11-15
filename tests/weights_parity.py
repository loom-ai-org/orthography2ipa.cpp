#!/usr/bin/env python3
"""weights parity: float(sum(weights)) and weights.candidate_base_costs from
the port against the reference, over weight lists that mix JSON ints and
floats.

Python's sum() adds an int and a float of the same value differently, so the
port must keep the type json.load gives each weight. The lists are the edge
cases of builtin_sum_impl's int and float loops, including ints past the C
long range, plus seeded random lists of small ints and floats. The weights go
through json.loads and the reference's normalize_grapheme_value, as a spec's
weights do. Exit 0 only when every line matches.

sum() is the running interpreter's, so it must be Python 3.12 or 3.13: 3.11
adds floats with no compensation, and 3.14 compensates the ints too.

Usage: weights_parity.py <path-to-check-weights-binary>
"""
import json
import os
import pathlib
import random
import subprocess
import sys
import warnings

if not (3, 12) <= sys.version_info[:2] < (3, 14):
    sys.exit(f"weights parity: Python {sys.version.split()[0]} sums ints and floats differently "
             f"from the 3.12 and 3.13 sum() the port follows; run it with one of those")

HERE = pathlib.Path(__file__).resolve().parent
reference = os.environ.get("O2I_PYTHON_ROOT") or str(HERE.parent.parent / "orthography2ipa")
if not (pathlib.Path(reference) / "orthography2ipa" / "weights.py").is_file():
    sys.exit(f"reference package not found at {reference!r} (set O2I_PYTHON_ROOT)")
sys.path.insert(0, reference)

from orthography2ipa.weights import candidate_base_costs, normalize_grapheme_value  # noqa: E402

EDGES = [
    [2**53 + 1, 0.5], [2**53, 1, 0.5], [0.1, 2**60, 3, -2**60], [0.1, 3], [0.1, 3.0],
    [1, 2, 3], [0.5, 1, 0.25], [-0, 1], [0, 0.0], [3, 0.1, 2**53, 0.1],
    # An int past the C long range, or an int sum that overflows it.
    [2**63, 0.5], [2**62, 2**62, 0.5], [2**63 - 1, 1, 0.1], [0.5, 2**63, 1],
    [0.1, 2**64 + 1, 0.2], [2**63 + 1, 2**63 + 1, 1], [2**70, 0.5, 3],
    [-(2**70), 1.5, 2**70], [-(2**63), -1, 0.25], [10**30, 10**30, 0.1],
    # Past the C long range sum() adds exact ints, then rounds once per step.
    [2**64, 2**11, 2**11], [2**64, 2**11, 2**11, 0.5], [1.0, 2**64, 2**11], [1.0, 2**64, 1.0, -(2**64)],
    # float() of the int overflows: in _valid_weights, or only for the sum.
    [10**400], [1, 10**400], [2**1023, 2**1023], [2**1023, 2**1023, 0.5],
]
rng = random.Random(1)
RANDOM = [[rng.randint(0, 10**rng.randint(1, 6)) if rng.random() < .5 else rng.random() * 10**rng.randint(-3, 6)
           for _ in range(rng.randint(1, 8))] for _ in range(100000)]


def expected(line):
    ipa, weights = normalize_grapheme_value(json.loads(line))
    try:
        total = f"sum {float(sum(weights))!r}"
    except OverflowError as error:
        total = f"sum OverflowError: {error}"
    try:
        with warnings.catch_warnings():
            warnings.simplefilter("ignore")
            costs = "costs" + "".join(f" {cost!r}" for cost in candidate_base_costs(ipa, weights, grapheme="g"))
    except OverflowError as error:
        costs = f"costs OverflowError: {error}"
    return [total, costs]


cases = EDGES + RANDOM
lines = [json.dumps({"ipa": [f"c{i}" for i in range(len(w))], "weights": w}) for w in cases]
out = subprocess.run([os.path.abspath(sys.argv[1])], input="\n".join(lines) + "\n",
                     capture_output=True, text=True, check=True).stdout.splitlines()
if len(out) != 2 * len(cases):
    sys.exit(f"weights parity: {len(cases)} lists in, {len(out)} lines out (2 per list)")
bad = [(line, want, out[2 * i:2 * i + 2]) for i, (line, want) in enumerate(zip(lines, map(expected, lines)))
       if out[2 * i:2 * i + 2] != want]
mixed = sum(1 for w in cases if {type(x) for x in w} == {int, float})
print(f"weights parity: {len(cases)} lists ({mixed} mixing ints and floats), {len(bad)} differ from the reference")
for line, want, got in bad[:10]:
    print(f"  {line}\n    python: {want}\n    cpp:    {got}")
sys.exit(1 if bad else 0)
