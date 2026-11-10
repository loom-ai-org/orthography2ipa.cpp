#!/usr/bin/env python3
"""lattice_per.py parity harness: the C++ oracle-PER port (src/lattice_per.cpp)
against the reference Python package.

The battery is built from real catalog data. Every spec that declares
`word_exceptions` contributes its declared reading untouched, that reading with
a segment DROPPED, with a segment SUBSTITUTED by a neighbouring phoneme of the
same spec's grapheme table, with two adjacent segments SWAPPED, and another
spec's declared reading of the same word scored under this lect. Specs whose
lect declares `sandhi_rules` also contribute two- and three-word reference
texts, so the engine's final (sandhi-applied) reading is not the pre-sandhi
lattice reading and the min() over the two admissible readings is exercised.
Lectures with no word_exceptions — the abugidas, the tone lectures and the
pure-grapheme sandhi lects — contribute surfaces built from their own grapheme
keys, mutated the same way off the engine's reading. The module docstring's two
`eu` examples are the first rows, and a family of edge rows covers the empty
and unmappable references, the whitespace and normalisation rules of `_segments`
(Python `str.split()`, NFC, the stripped marks) and a lect the registry cannot
resolve. Every row is scored again with weighted=True on a deterministic third,
so the featural substitution path is covered too, and the first 40 rows are
re-scored at beam_width=3 — which the reference's own greedy `search` makes
inert, and the port must reproduce that inertness.

Usage:
  lattice_per_parity.py <path-to-check-binary> [--battery-out <tsv>]
  lattice_per_parity.py --dump-cases <out.tsv>      # internal: run under the
      reference interpreter, write cases + expected values

The check binary is src/lattice_per.cpp + tests/_scratch/lattice_per_check.cpp;
the g++ line is recorded in build/notes/worker_lattice_per.md. It recomputes
every row and compares each field (distance, per, top_distance, top_per,
ref_segments, hyp_segments, lang and the derived variant_credit) with a
tolerance of 1e-12; rows where the reference raised must raise in C++ too.
The harness exits non-zero when any row differs, when the battery is under
MIN_ROWS rows / MIN_LANGS lectures / MIN_CREDITED lattice-beats-final rows, or
when the docstring examples do not reproduce exactly.
"""
import hashlib
import json
import os
import pathlib
import random
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
CPP_ROOT = HERE.parent
SCRATCH = HERE / "_scratch" / "lattice_per"
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

MIN_ROWS = 2000
MIN_LANGS = 25
# Rows whose lattice reading beats the final reading must exist in numbers: they
# are the only rows that test min(lattice_distance, top_distance) taking the
# lattice branch, and variant_credit itself.
MIN_CREDITED = 40
# The classes the brief requires coverage of, by code.
REQUIRED_LANGS = {"ar", "hi", "th"}

# The lects family B covers: abugidas, tone lectures (a non-empty
# tone_inventory or tone rules) and sandhi lectures with no word_exceptions.
FAMILY_B_LANGS = [
    # abugidas / syllabic scripts
    "hi", "ta", "kn", "bn", "ml", "mr", "ne", "si", "sa", "as", "or", "gu", "pa",
    "dv", "am", "gez", "my", "km",
    # tone lectures
    "vi", "yo", "zh", "yue-Latn-jyutping", "wuu-Latn-wugniu", "kix", "mww",
    # sandhi lectures without word_exceptions (th/lo are abugida + tone too)
    "eu", "mt", "kea", "mcm", "pap", "pov", "crs", "idb", "aoa", "mzs", "vkp",
    "th", "lo",
]

WORDS_PER_SPEC = 8            # word_exceptions entries scored per lect
SURFACES_PER_SPEC = 10        # grapheme-key surfaces scored per lect (family B)
SUBSTITUTIONS = 2             # substituted segments per reading
DROPS = 2
SWAPS = 2


# The edge rows: an empty hypothesis, an empty / whitespace / punctuation-only
# reference (no lattice at all), a reference outside the lect, a hypothesis made
# only of stripped marks, modifier letters that must ride their base character,
# and a lect code the registry cannot resolve.
EDGE_CASES = [
    ("", "ez bada", "eu"),
    ("es", "", "eu"),
    ("es", "   ", "eu"),
    ("es", "...", "eu"),
    ("es", "\u4f60\u597d", "eu"),
    ("es", "b-d", "eu"),
    ("\u02c8\u02cc.\u203f|\u2016", "ez bada", "eu"),
    ("kʰa\u02d0", "kaha", "hi"),
    ("m a", "ma1", "zh"),
    ("es", "ez bada", "zz-ZZ"),
    # _segments' whitespace rule is Python's `str.split()`; ZWSP and U+180E are
    # NOT spaces there while NEL, U+001C and every Zs/Zl/Zp separator are.
    ("ez\u00a0bada", "ez bada", "eu"),
    ("ez\u2028bada", "ez bada", "eu"),
    ("ez\u3000bada", "ez bada", "eu"),
    ("ez\u0085bada", "ez bada", "eu"),
    ("ez\u001cbada", "ez bada", "eu"),
    ("ez\u200bbada", "ez bada", "eu"),
    ("ez\u180ebada", "ez bada", "eu"),
    ("ez\tbada", "ez bada", "eu"),
    # NFC before splitting, and marks stripped inside a word, not just at edges.
    ("e\u030cz bada", "ez bada", "eu"),
    ("e\u02c8z bada", "ez bada", "eu"),
    ("e.z bada", "ez bada", "eu"),
    ("e\u203fbada", "ez bada", "eu"),
    ("s\u033bb\u033bada", "ez bada", "eu"),
    ("\u000b", "ez bada", "eu"),
]


def classify(spec):
    """A short tag for the typological class(es) a lect stands in, so a row's
    family says which behaviour it is meant to cover."""
    kind = getattr(spec, "script_type", "")
    kind = getattr(kind, "value", kind)
    tags = []
    if str(kind) == "abugida":
        tags.append("abugida")
    if getattr(spec, "tone", None) or (getattr(spec, "tone_inventory", None) or {}):
        tags.append("tone")
    if getattr(spec, "sandhi_rules", None):
        tags.append("sandhi")
    return "+".join(tags) or "plain"


def build_battery(engine, seed=20261031):
    """Every (hyp, ref, lang, weighted, beam, family) row to score, deterministically."""
    import logging
    logging.disable(logging.CRITICAL)
    from orthography2ipa.registry import available_codes, get
    from orthography2ipa.lattice_per import _segments

    rng = random.Random(seed)
    cases = []      # (hyp, ref, lang, weighted, beam, family)

    def add(hyp, ref, lang, weighted, beam, family):
        if not str(ref).strip():
            return
        cases.append((hyp, ref, lang, bool(weighted), beam, family))

    # ── the docstring examples, verbatim ────────────────────────────────
    add("es\u033B pada", "ez bada", "eu", False, 8, "docstring")
    add("es ba\u00f0a", "ez bada", "eu", False, 8, "docstring")
    add("es\u033B pada", "ez bada", "eu", True, 8, "docstring")
    add("es ba\u00f0a", "ez bada", "eu", True, 8, "docstring")

    # ── edges: the readings with no lattice at all, and the failures the
    # reference lets escape. add() refuses an empty reference, so these rows go
    # in directly — an empty or unmappable reference IS a case to compare.
    for hyp, ref, lang in EDGE_CASES:
        cases.append((hyp, ref, lang, False, 8, "edge"))
        cases.append((hyp, ref, lang, True, 8, "edge-weighted"))

    specs = {}
    for code in available_codes():
        try:
            spec = get(code)
        except Exception:
            continue
        specs[code] = spec

    # word -> [(code, declared ipa)], for mutation (d): another spec's reading.
    declared_by_word = {}
    for code, spec in specs.items():
        for surface, ipa in (spec.word_exceptions or {}).items():
            if str(ipa or "").strip():
                declared_by_word.setdefault(surface, []).append((code, ipa))

    def phoneme_pool(spec):
        """The spec's grapheme-table IPA values, distinct and in a fixed order."""
        pool = []
        for key in sorted(spec.graphemes or {}):
            for value in spec.graphemes[key]:
                if value and value not in pool:
                    pool.append(value)
        return pool

    def mutate(segs, pool, family, index):
        """The three segment-level mutations, each at a rotating position."""
        out = []
        if not segs:
            return out
        for count in range(DROPS):
            i = (index + count * 3) % len(segs)
            out.append(("".join(segs[:i] + segs[i + 1:]), f"{family}-drop"))
        for count in range(SUBSTITUTIONS):
            i = (index + 1 + count * 2) % len(segs)
            if pool:
                if segs[i] in pool:                 # the NEIGHBOURING phoneme of
                    replacement = pool[(pool.index(segs[i]) + 1) % len(pool)]
                else:
                    replacement = pool[(index + count) % len(pool)]
            else:
                replacement = "a"
            out.append(("".join(segs[:i] + [replacement] + segs[i + 1:]),
                        f"{family}-sub"))
        if len(segs) > 1:
            for count in range(SWAPS):
                i = (index + count) % (len(segs) - 1)
                swapped = list(segs)
                swapped[i], swapped[i + 1] = swapped[i + 1], swapped[i]
                out.append(("".join(swapped), f"{family}-swap"))
        return out

    # ── family A: real word_exceptions readings ─────────────────────────
    family_a = [c for c, s in specs.items() if (s.word_exceptions or {})]
    for code in sorted(family_a):
        spec = specs[code]
        pool = phoneme_pool(spec)
        entries = sorted((s, i) for s, i in (spec.word_exceptions or {}).items()
                         if str(i or "").strip())
        picked = entries if len(entries) <= WORDS_PER_SPEC else \
            [entries[i] for i in rng.sample(range(len(entries)), WORDS_PER_SPEC)]
        for word_index, (surface, declared) in enumerate(picked):
            segs = _segments(declared)
            add("".join(segs), surface, code, False, 8, "we-identity")
            for hyp, family in mutate(segs, pool, "we", word_index):
                add(hyp, surface, code, False, 8, family)
            for other_code, other_ipa in declared_by_word.get(surface, []):
                if other_code != code:      # (d) another spec's reading
                    add("".join(_segments(other_ipa)), surface, code, False, 8, "we-crossspec")

        # two-word reference texts for a lect that declares sandhi rules, so the
        # engine's final reading (reading 2) is not the lattice reading (1).
        if getattr(spec, "sandhi_rules", None) and len(entries) >= 2:
            for pair_index in range(min(3, len(entries) - 1)):
                left = entries[pair_index][0]
                right = entries[pair_index + 1][0]
                # a three-word run for the lects whose rules cascade, so a
                # medial word sees both neighbours
                tail = entries[pair_index + 2][0] if pair_index + 2 < len(entries) else ""
                ref = f"{left} {right} {tail}" if tail else f"{left} {right}"
                try:
                    top = engine(code).transcribe(ref)
                except Exception:
                    continue
                segs = _segments(top)
                add("".join(segs), ref, code, False, 8, "sandhi-identity")
                for hyp, family in mutate(segs, pool, "sandhi", pair_index):
                    add(hyp, ref, code, False, 8, family)

    # ── family B: the abugidas, tone lectures and sandhi lects that carry no
    # word_exceptions. Surfaces are built from the spec's OWN grapheme keys, so
    # they stay catalog data; the base reading is the engine's transcription.
    family_b = []
    for code in FAMILY_B_LANGS:
        spec = specs.get(code)
        if spec is None or not (spec.graphemes or {}):
            continue
        family_b.append(code)

    for code in family_b:
        spec = specs[code]
        pool = phoneme_pool(spec)
        keys = [k for k in sorted(spec.graphemes) if k.strip() and len(k) <= 2]
        if not keys:
            continue
        surfaces = list(keys[:SURFACES_PER_SPEC])
        surfaces += [a + b for a, b in zip(keys[:4], keys[1:5])]
        surfaces += [a + " " + b for a, b in zip(keys[2:6], keys[6:10])]
        tag = classify(spec)
        for surface_index, surface in enumerate(surfaces):
            try:
                top = engine(code).transcribe(surface)
            except Exception:
                continue
            segs = _segments(top)
            if not segs:
                continue
            add("".join(segs), surface, code, False, 8, f"{tag}-identity")
            for hyp, family in mutate(segs, pool, tag, surface_index):
                add(hyp, surface, code, False, 8, family)

    # ── the featural (weighted=True) pass over a deterministic third ─────
    unweighted = list(cases)
    for case in unweighted:
        if case[5].startswith("docstring"):
            add(case[0], case[1], case[2], True, case[4], case[5] + "-weighted")
        elif hash_string(case[0] + case[1]) % 3 == 0:
            add(case[0], case[1], case[2], True, case[4], case[5] + "-weighted")

    # beam_width is only a knob the reference can reach through a beam search,
    # and lattice_per() always calls sentence_lattice with its default
    # search="greedy"; a few rows carry a non-default width to prove it is
    # inert exactly as the Python's is.
    for case in unweighted[:40]:
        add(case[0], case[1], case[2], case[3], 3, case[5] + "-beam3")
    return cases


def hash_string(text):
    return int(hashlib.sha256(text.encode("utf-8")).hexdigest()[:8], 16)


class Engines:
    """One G2P per lect, so a battery of thousands of rows is not thousands of
    spec loads. lattice_per() builds its own engine per call, exactly as the
    reference does; this cache is only for building the surfaces/mutations."""

    def __init__(self, g2p_class):
        self._g2p_class = g2p_class
        self._cache = {}

    def __call__(self, code):
        if code not in self._cache:
            self._cache[code] = self._g2p_class(code)
        return self._cache[code]


def dump_cases(path):
    """Evaluate the battery with the REFERENCE and write the case/expected file."""
    sys.path.insert(0, str(CPP_ROOT.parent / "orthography2ipa"))
    import logging
    logging.disable(logging.CRITICAL)
    from orthography2ipa import G2P
    from orthography2ipa.lattice_per import lattice_per

    cases = build_battery(Engines(G2P))
    rows = 0
    errors = 0
    credited = 0
    weighted_rows = 0
    langs = set()
    with open(path, "w", encoding="utf-8") as out:
        out.write("# hyp_hex\tref_hex\tlang_hex\tweighted\tbeam\t"
                  "distance\tper\ttop_distance\ttop_per\tref_segments\thyp_segments\tlang_out_hex\n")
        for hyp, ref, lang, weighted, beam, _family in cases:
            langs.add(lang)
            fields = [hyp.encode("utf-8").hex(), ref.encode("utf-8").hex(),
                      lang.encode("utf-8").hex(), "1" if weighted else "0", str(beam)]
            if weighted:
                weighted_rows += 1
            try:
                r = lattice_per(hyp, ref, lang, weighted=weighted, beam_width=beam)
            except Exception:
                errors += 1
                out.write("\t".join(fields) + "\tERR\n")
                continue
            fields += [repr(r.distance), repr(r.per), repr(r.top_distance), repr(r.top_per),
                       str(r.ref_segments), str(r.hyp_segments), r.lang.encode("utf-8").hex()]
            out.write("\t".join(fields) + "\n")
            rows += 1
            if r.variant_credit:
                credited += 1
    summary = {"rows_scored": rows, "rows_erroring": errors, "languages": len(langs),
               "battery": rows + errors, "weighted_rows": weighted_rows,
               "nonzero_variant_credit": credited,
               "required_present": sorted(REQUIRED_LANGS & langs),
               "missing_required": sorted(REQUIRED_LANGS - langs)}
    with open(str(path) + ".summary.json", "w", encoding="utf-8") as handle:
        json.dump(summary, handle, indent=1, sort_keys=True)
    print("reference battery:", json.dumps(summary, sort_keys=True))
    if rows < MIN_ROWS or len(langs) < MIN_LANGS or summary["missing_required"]:
        print("FAIL battery too small, too few lectures, or a required lect class is absent")
        return 1
    if credited < MIN_CREDITED:
        print(f"FAIL only {credited} rows have a non-zero variant_credit: the battery does not"
              f" compare the lattice reading against the engine's final reading")
        return 1
    return 0


def run(command, label):
    print("$ " + " ".join(command))
    result = subprocess.run(command, cwd=str(CPP_ROOT), text=True, capture_output=True)
    if result.stdout.strip():
        print(result.stdout.strip()[-4000:])
    if result.stderr.strip():
        print(result.stderr.strip()[-2000:])
    if result.returncode != 0:
        print(f"FAIL {label}: exit {result.returncode}")
    return result


def main():
    argv = sys.argv[1:]
    if len(argv) >= 2 and argv[0] == "--dump-cases":
        sys.exit(dump_cases(os.path.abspath(argv[1])))

    if not argv:
        print(__doc__)
        sys.exit(2)
    checker = os.path.abspath(argv[0])
    if not os.access(checker, os.X_OK):
        print(f"check binary not found or not executable: {checker}\n"
              f"build it first (the g++ line is in build/notes/worker_lattice_per.md)")
        sys.exit(2)
    reference_python = os.environ.get("O2I_PYTHON", REFERENCE_PYTHON)
    if not os.path.exists(reference_python):
        print(f"reference interpreter missing: {reference_python!r} (set $O2I_PYTHON)")
        sys.exit(1)

    SCRATCH.mkdir(parents=True, exist_ok=True)
    battery = os.path.abspath(os.path.join(
        str(SCRATCH), "battery.tsv" if "--battery-out" not in argv
        else argv[argv.index("--battery-out") + 1]))

    reference = run([reference_python, str(HERE / "lattice_per_parity.py"),
                     "--dump-cases", battery], "reference dump")
    if reference.returncode != 0:
        sys.exit(1)
    docstring = run([checker, "examples"], "docstring examples")
    comparison = run([checker, "cases", battery], "C++ vs reference")
    sys.exit(0 if (docstring.returncode == 0 and comparison.returncode == 0) else 1)


if __name__ == "__main__":
    main()
