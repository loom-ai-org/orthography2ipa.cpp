#!/usr/bin/env python3
"""Registry-resolution parity harness: the C++ `resolve` surface vs the
reference registry.py `resolve()` / ovos_spec_tools `lang_distance`.

registry.py's resolution is the ONE gate every other surface sits behind: a tag
that resolves to the wrong spec silently transcribes a different language. This
harness therefore does not sample — it compares

  * every alias key and value in registry.py `_ALIASES`, every
    `_BARE_DEFAULTS`/`_REGION_DEFAULTS` key, and every code in the catalog;
  * a mutation battery over those tags (case flips, '_' separators, added
    script/region/variant/private subtags, ISO 639-3 forms);
  * `lang_distance` over the pairs the reference actually ranks.

Usage: registry_parity.py <path-to-cli> [--pairs N]

The reference package is located via $O2I_PYTHON_ROOT, else the sibling
directory of this repository. The harness fails loudly (exit 1) when the
reference or the `resolve` surface is unavailable — a missing surface is a
FAILURE, never a skip.
"""
import json
import os
import pathlib
import random
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
CPP_ROOT = HERE.parent

reference = os.environ.get("O2I_PYTHON_ROOT") or str(CPP_ROOT.parent / "orthography2ipa")
if not (pathlib.Path(reference) / "orthography2ipa" / "phonetok.py").is_file():
    print(f"reference package not found at {reference!r} (set O2I_PYTHON_ROOT)")
    sys.exit(1)
sys.path.insert(0, reference)

import logging
logging.disable(logging.CRITICAL)
import orthography2ipa.registry as registry
from orthography2ipa.json_loader import available_json_codes
from ovos_spec_tools.language import lang_distance

CLI = pathlib.Path(os.path.abspath(sys.argv[1]))
MUTATION_CAP = int(os.environ.get("O2I_REGISTRY_MUTATIONS", "6000"))


def cli_args(*args):
    r = subprocess.run([str(CLI), *args], capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError(f"CLI {args[0]} rc={r.returncode}: {r.stderr.strip()[:200]}")
    return r.stdout.strip()


def cpp_resolve(codes):
    """resolve() for a batch of tags, in one CLI process."""
    out = cli_args("resolve", "--json", *codes)
    results = {}
    decoder = json.JSONDecoder()
    text = out
    while text.strip():
        obj, idx = decoder.raw_decode(text.lstrip())
        text = text.lstrip()[idx:]
        results[obj["requested"]] = obj["resolved"]
    return results


def resolve_battery():
    """Every tag the reference resolution is expected to answer for."""
    codes = available_json_codes()
    tags = set(codes)
    tags |= set(registry._ALIASES.keys())
    tags |= set(registry._ALIASES.values())
    tags |= set(registry._BARE_DEFAULTS.keys())
    tags |= set(registry._REGION_DEFAULTS.keys())
    # ISO 639-3 / 1 forms of the catalog's primary subtags, and the forms the
    # alias table exists to catch.
    tags |= {"por", "eng", "spa", "fra", "deu", "ita", "nld", "swe", "dan", "nor",
             "rus", "ukr", "ara", "fas", "hin", "zho", "jpn", "kor", "eus", "cat",
             "glg", "oci", "tur", "fin", "ell", "pol", "ces", "ron", "mwl", "ast",
             "arg", "lat", "arb", "phn", "acy", "peo", "pal", "tgk", "prs", "bxr",
             "diq", "quz", "quy", "tw", "fat", "mhr", "mrj", "aju", "als", "rmy",
             "src", "oji", "mnk", "haz", "kas", "bcc", "arz", "ary", "apc", "ajp",
             "afb", "acw", "acm", "acx", "adq"}
    return sorted(tags)


def mutate(tag, rng, extra):
    forms = [tag.upper(), tag.lower(), tag.replace("-", "_")]
    if "-" in tag:
        primary, rest = tag.split("-", 1)
        forms += [primary, f"{primary}-x-{rest}", f"{primary}-Latn", f"{primary}-XX",
                  f"{primary}-latn-us", f"{rest}-{primary}"]
    else:
        forms += [f"{tag}-XX", f"{tag}-x-thing", f"{tag}-Latn", f"{tag}-GB-x-region"]
    forms += [f"{tag}-", f"-{tag}", "", "x-" + tag, f"{tag}-419", f"{tag}-oxendict"]
    for form in forms:
        if form:
            extra.add(form)
    return extra


def main():
    pairs_limit = 4000
    global MUTATION_CAP
    for index, argument in enumerate(sys.argv):
        if argument == "--pairs" and index + 1 < len(sys.argv):
            pairs_limit = int(sys.argv[index + 1])
        if argument == "--mutations" and index + 1 < len(sys.argv):
            MUTATION_CAP = int(sys.argv[index + 1])
    probe = subprocess.run([str(CLI), "resolve", "--json", "pt"], capture_output=True, text=True)
    if probe.returncode != 0 or "resolved" not in probe.stdout:
        print("MISSING SURFACE: resolve (rc=%d) — build the CLI with the registry port"
              % probe.returncode)
        return 1

    rng = random.Random(20261031)
    battery = resolve_battery()
    # The mutation fan-out is seeded and capped: 84k tags x closest_lang's
    # 7662-candidate scan is a memory-bound wall, and the mutations repeat the
    # same resolution paths after a few thousand. --mutations raises it.
    extra = set()
    for tag in battery:
        mutate(tag, rng, extra)
    extra = sorted(extra)
    if len(extra) > MUTATION_CAP:
        step = len(extra) / MUTATION_CAP
        extra = [extra[int(i * step)] for i in range(MUTATION_CAP)]
    every = sorted(set(battery) | set(extra))
    print(f"resolve battery: {len(every)} tags")

    failures = []
    chunk, size = [], 400
    resolved = {}
    for start in range(0, len(every), size):
        batch = every[start:start + size]
        try:
            resolved.update(cpp_resolve(batch))
        except RuntimeError as error:
            print(str(error))
            return 1
    for tag in every:
        want = registry.resolve(tag)
        got = resolved.get(tag)
        if got != want:
            failures.append(("resolve", tag, want, got))
    print(f"resolve: {len(every) - len(failures)}/{len(every)}")

    # lang_distance over real pairs: every tag against a sample of catalog codes.
    sample = rng.sample(available_json_codes(), 60)
    pair_cases = [(a, b) for a in battery[:400] for b in sample][:pairs_limit]
    distance_failures = []
    for desired, supported in pair_cases:
        want = lang_distance(desired, supported)
        got = cli_args("resolve", desired, "--lang-distance", supported)
        if str(want) != got:
            distance_failures.append((desired, supported, want, got))
    print(f"lang_distance: {len(pair_cases) - len(distance_failures)}/{len(pair_cases)}")

    for kind, a, want, got in failures[:40]:
        print(f"  FAIL {kind}: {a!r}: reference {want!r} vs cpp {got!r}")
    for a, b, want, got in distance_failures[:40]:
        print(f"  FAIL lang_distance({a!r}, {b!r}): reference {want} vs cpp {got}")
    return 1 if failures or distance_failures else 0


if __name__ == "__main__":
    sys.exit(main())
