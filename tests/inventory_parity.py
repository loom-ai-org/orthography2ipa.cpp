#!/usr/bin/env python3
"""Inventory-parity harness: the C++ port of inventory.py vs the reference.

Compares ``emission_inventory`` / ``phoneme_inventory`` / ``phoneme_atoms`` /
``dead_allophone_rules`` (plus ``tokenize`` on every emission and on a fixed
battery) for EVERY spec in ``data/`` — 7662 spec files — against the reference
package. The closure invariant is the point of the module, so this is a whole
catalog sweep, not a sample.

The C++ side runs as a driver binary (one JSON line per spec, built from
``src/inventory.cpp`` + ``tests/_scratch/inventory_catalog_driver.cpp`` against
the library); the reference side runs in this process. That is why this file
must be run with the interpreter that can import the reference
(the interpreter that runs this harness must have the reference's
requirements — numpy included), from a path that resolves the
reference package — never a copy in ``/tmp``, which would import the
pip-installed copy and invent diffs.

Usage:
    inventory_parity.py <path-to-driver> [--limit N] [--battery N] [--quiet]
                          [--slice START:END]

``--slice`` restricts the sweep to ``data/*.json`` stems sorted and indexed by
``[START:END)``, so the catalog can be sharded across processes.

Exit status is 0 when every spec matches, 1 otherwise. ``atoms`` ordering is
compared as "length classes, longest first, and the set inside each class":
Python's ``sorted(set, key=len, reverse=True)`` leaves equal-length atoms in the
set's hash-randomised order, and two atoms of equal length cannot both match at
one position, so ``segment_ipa`` cannot see the difference. Every other list
(``dead``, the token lists) is compared exactly, in the reference's order.
"""
import json
import logging
import os
import pathlib
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
CPP_ROOT = HERE.parent

reference = os.environ.get("O2I_PYTHON_ROOT") or str(CPP_ROOT.parent / "orthography2ipa")
if not (pathlib.Path(reference) / "orthography2ipa" / "inventory.py").is_file():
    print(f"reference package not found at {reference!r} (set O2I_PYTHON_ROOT)")
    sys.exit(1)
sys.path.insert(0, reference)

logging.disable(logging.CRITICAL)

from orthography2ipa import inventory as ref_inventory  # noqa: E402
from orthography2ipa.registry import get as ref_get  # noqa: E402

# Strings that are not emissions, fed to ``tokenize`` through the driver's
# ``--tokenize`` mode: stress marks riding inside a word (``_split_on_stress``
# alone reaches them), a space (dropped), a linking tie (a sandhi-only symbol),
# and multi-character atoms that must stay whole.
BATTERY = [
    "ˈa.ba",
    "aˌbɐ.cɐ",
    "ˈtsaː.ka",
    "dʒaˈli.jə",
    "kə.ˈɡɾo",
    "‿a",
    "nʲˈe",
    "a ˈb",
    "ˈ",
    "tɕʰyː",
]


def atoms_key(atoms):
    """The order Python's sort can guarantee: length class, then the set."""
    grouped = {}
    for atom in atoms:
        grouped.setdefault(len(atom), set()).add(atom)
    return [(length, tuple(sorted(grouped[length]))) for length in sorted(grouped, reverse=True)]


TIE = "\u203f"        # U+203F UNDERTIE, the French liaison linking tie

def classify(name, expect, actual):
    """A short cause label for one differing field."""
    if name == "tokmap":
        differing = sorted(k for k in expect if expect[k] != actual.get(k))
        return f"tokenize differs on {len(differing)} emissions: {differing[:4]!r}"
    if name == "atoms":
        if atoms_key(expect) == atoms_key(actual):
            return "atoms-order-only"      # hash-order tie inside one length class
        if set(expect) == set(actual):
            return "atoms-length-order"
        expect, actual = set(expect), set(actual)
    if isinstance(expect, set):
        missing = sorted(expect - actual)
        extra = sorted(actual - expect)
        cause = f"missing={missing[:6]!r} extra={extra[:6]!r}"
        if extra and all(TIE in e for e in extra) and all(len(e) == 2 for e in extra):
            # segment_ipa's modifier test (src/orthography2ipa.cpp ipa_modifier)
            # counts U+203F as a modifier letter; the reference's
            # _is_modifier does not (it is category Pc), so the tie rides on the
            # consonant before it instead of standing as its own segment.
            cause += "  [KNOWN GAP: shared segment_ipa glues U+203F to the atom "                      "before it; not an inventory.py port issue]"
        return cause
    return f"expected={expect!r} actual={actual!r}"


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if not args:
        print(__doc__)
        sys.exit(2)
    driver = os.path.abspath(args[0])
    limit = None
    sliced = None
    for arg in sys.argv[1:]:
        if arg.startswith("--slice="):
            a, b = arg.split("=", 1)[1].split(":")
            sliced = (int(a), int(b))
    battery_specs = 40          # how many stress/sandhi/abugida/rule specs get the battery
    quiet = "--quiet" in sys.argv
    for i, arg in enumerate(sys.argv):
        if arg == "--limit" and i + 1 < len(sys.argv):
            limit = int(sys.argv[i + 1])
        if arg == "--battery" and i + 1 < len(sys.argv):
            battery_specs = int(sys.argv[i + 1])
    if not os.path.isfile(driver):
        print(f"driver not found: {driver}")
        sys.exit(2)

    codes = sorted(p.stem for p in (CPP_ROOT / "data").glob("*.json"))
    # A whole-catalog sweep is CPU-bound on the driver, so it is normally run as
    # a few shards in parallel and the summaries added together.
    if sliced is not None:
        start, stop = sliced
        codes = codes[start:stop]
    if limit is not None:
        codes = codes[:limit]

    proc = subprocess.Popen([driver], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                            text=True, encoding="utf-8")
    proc.stdin.write("\n".join(codes) + "\n")
    proc.stdin.close()

    counts = {"specs": 0, "ref_error": 0, "cpp_error": 0, "both_error": 0,
              "closure_ok": 0, "closure_violation": 0}
    diffs = {}
    fields = ("emissions", "tokens", "atoms", "dead", "tokmap")
    identical = {field: 0 for field in fields}

    for code in codes:
        line = proc.stdout.readline()
        if not line:
            print(f"driver stopped after {counts['specs']} specs")
            break
        got = json.loads(line)
        assert got.get("code") == code, (got.get("code"), code)
        counts["specs"] += 1
        if counts["specs"] % 1000 == 0:
            print(f"... {counts['specs']}/{len(codes)} specs", file=sys.stderr, flush=True)

        try:
            spec = ref_get(code)
        except Exception as error:  # the reference refuses this stem
            counts["ref_error"] += 1
            if got.get("ok"):
                print(f"FAIL {code}: reference raised {error!r}, C++ answered")
            else:
                counts["both_error"] += 1
            continue
        if not got.get("ok"):
            counts["cpp_error"] += 1
            print(f"FAIL {code}: C++ raised {got.get('error')!r}, reference answered")
            continue

        expect_emissions = set(ref_inventory.emission_inventory(spec))
        expect = {
            "emissions": expect_emissions,
            "tokens": set(ref_inventory.phoneme_inventory(spec)),
            "atoms": list(ref_inventory.phoneme_atoms(spec)),
            "dead": list(ref_inventory.dead_allophone_rules(spec)),
            "tokmap": {e: ref_inventory.tokenize(e, spec) for e in expect_emissions},
        }
        actual = {
            "emissions": set(got["emissions"]),
            "tokens": set(got["tokens"]),
            "atoms": list(got["atoms"]),
            "dead": list(got["dead"]),
            "tokmap": {k: list(v) for k, v in got["tokmap"].items()},
        }
        # The closure invariant, checked on both sides independently: every
        # token tokenize() draws from an emission is in phoneme_inventory.
        for side, tokens, tokmap in (("reference", expect["tokens"], expect["tokmap"]),
                                     ("C++", actual["tokens"], actual["tokmap"])):
            for emission, drawn in tokmap.items():
                outside = set(drawn) - tokens
                if outside:
                    counts["closure_violation"] += 1
                    print(f"CLOSURE {side} {code}: {emission!r} -> {sorted(outside)}")
                    break
            else:
                counts["closure_ok"] += 1

        clean = True
        for field in fields:
            if field in ("emissions", "tokens", "dead"):
                same = expect[field] == actual[field]
            elif field == "tokmap":
                same = expect[field] == actual[field]
            else:
                same = atoms_key(expect[field]) == atoms_key(actual[field])
            if same:
                identical[field] += 1
                continue
            clean = False
            cause = classify(field, expect[field], actual[field])
            examples = diffs.setdefault((field, cause[:120]), [])
            if len(examples) < 5:
                examples.append(code)
            print(f"DIFF {code} [{field}] {cause}", flush=True)
            if field != "tokmap":
                ref_show = sorted(expect[field]) if isinstance(expect[field], set) else expect[field]
                cpp_show = sorted(actual[field]) if isinstance(actual[field], set) else actual[field]
                print(f"     ref: {str(ref_show)[:300]}", flush=True)
                print(f"     cpp: {str(cpp_show)[:300]}", flush=True)

    # --tokenize battery: strings no spec emits, run against the specs whose
    # tables actually reach stress marks, sandhi or an abugida's inherent vowel.
    # The battery specs are sampled with a stride across the whole catalog, so
    # they are not only the alphabetically-first ones.
    battery_cases = []
    stride = max(1, len(codes) // max(1, battery_specs * 3))
    for code in codes[::stride]:
        if len(battery_cases) >= battery_specs * len(BATTERY):
            break
        try:
            spec = ref_get(code)
        except Exception:
            continue
        if not (spec.stress or spec.sandhi_rules or spec.script_type == "abugida"
                or spec.allophone_rules):
            continue
        battery_cases.extend((code, ipa) for ipa in BATTERY)

    battery_checked = battery_failed = 0
    if battery_cases:
        proc2 = subprocess.Popen([driver, "--tokenize"], stdin=subprocess.PIPE,
                                 stdout=subprocess.PIPE, text=True, encoding="utf-8")
        proc2.stdin.write("\n".join(f"{c}\t{i}" for c, i in battery_cases) + "\n")
        proc2.stdin.close()
        for code, ipa in battery_cases:
            line = proc2.stdout.readline()
            if not line:
                print("tokenize driver stopped early")
                break
            got = json.loads(line)
            try:
                expect = ref_inventory.tokenize(ipa, ref_get(code))
            except Exception:
                continue
            battery_checked += 1
            if got.get("ok") and got["tokens"] == expect:
                continue
            battery_failed += 1
            print(f"FAIL tokenize {code} {ipa!r}\n     ref: {expect}\n     cpp: {got.get('tokens', got.get('error'))}")

    total = counts["specs"]
    print()
    print(f"catalog specs compared:      {total}  "
          f"(reference errors {counts['ref_error']}, C++ errors {counts['cpp_error']}, "
          f"both-error {counts['both_error']})")
    for field in fields:
        print(f"{field:>12}: {identical[field]}/{total} identical, {total - identical[field]} differing")
    print(f"tokenize battery: {battery_checked - battery_failed}/{battery_checked} identical")
    print(f"closure invariant (emissions -> tokens inside phoneme_inventory): "
          f"{counts['closure_ok']} checks clean, {counts['closure_violation']} violations"
          f" (2 x {total} spec-side checks)")
    failing = sum(1 for field in fields if identical[field] != total)
    passed = (failing == 0 and counts["cpp_error"] == 0 and battery_failed == 0
              and counts["closure_violation"] == 0)
    print(f"VERDICT: {'PASS' if passed else 'FAIL'}")
    sys.exit(0 if passed else 1)


if __name__ == "__main__":
    main()
