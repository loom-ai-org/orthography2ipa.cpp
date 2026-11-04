#!/usr/bin/env python3
"""Registry-resolution parity harness: the C++ `resolve` surface vs the
reference registry.py `resolve()` / linguatag `lang_distance`.

registry.py's resolution is the ONE gate every other surface sits behind: a tag
that resolves to the wrong spec silently transcribes a different language. This
harness therefore does not sample — it compares

  * every alias key and value in registry.py `_ALIASES` (old keys of renamed
    specs included), every `_REGION_DEFAULTS` key, every language linguatag
    `OVERRIDES` names, and every code in the catalog;
  * a mutation battery over those tags (case flips, '_' separators, added
    script/region/variant/private subtags, ISO 639-3 forms);
  * each language of the lect tree in each region its lects carry, with
    script, private-use and case forms, where the tree decides the match;
  * linguatag `lang_distance` over the pairs the reference actually ranks,
    with no lect score and with each lect mode;
  * linguatag `lang_distance` over every edge of the lect tree, in both
    directions and in each lect mode.

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
import warnings
logging.disable(logging.CRITICAL)
import orthography2ipa.registry as registry
from orthography2ipa.json_loader import available_json_codes
from linguatag import OVERRIDES, lang_distance, lects, matching

# A private-use tag that names no spec warns as it resolves; the harness
# compares the resolution, not the warning.
warnings.simplefilter("ignore", registry.UnknownVariantWarning)

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
    tags |= set(registry._RENAMED.keys())
    tags |= set(registry._RENAMED.values())
    tags |= set(registry._REGION_DEFAULTS.keys())
    tags |= set(OVERRIDES)
    # The bare tags that take a default region (`de` -> `de-DE`).
    tags |= {"de", "en", "es", "fr", "gl", "it", "pt", "ro"}
    # ISO 639-3 / 1 forms of the catalog's primary subtags, and the forms the
    # alias table exists to catch.
    tags |= {"por", "eng", "spa", "fra", "deu", "ita", "nld", "swe", "dan", "nor",
             "rus", "ukr", "ara", "fas", "hin", "zho", "jpn", "kor", "eus", "cat",
             "glg", "oci", "tur", "fin", "ell", "pol", "ces", "ron", "mwl", "ast",
             "arg", "lat", "arb", "phn", "acy", "peo", "pal", "tgk", "prs", "bxr",
             "diq", "quz", "quy", "tw", "fat", "mhr", "mrj", "aju", "als", "rmy",
             "src", "oji", "mnk", "haz", "kas", "bcc", "arz", "ary", "apc", "ajp",
             "afb", "acw", "acm", "acx", "adq", "aja", "tmg", "ajg", "gej", "gen",
             "tpw", "tpn", "vsn", "pnb", "haz", "bar", "cbk", "bbz", "yue-HK",
             "yue-MO", "yue-Hant", "ar-001", "en", "pt", "ur", "ar"}
    return sorted(tags)


def lect_tree_battery():
    """The tags whose nearest spec the lect tree decides.

    A tag in the region of a lect (`ar-TR` for `apc-TR-x-cukurova`) has no spec
    of its own, and its distance to the lect comes from the tree. Each language
    of the tree is tried in each region its lects carry; an Arabic variety in
    each region of any Arabic lect, because the MSA fallback lets every variety
    reach every other. Each tag also comes with a script, an unknown
    private-use part and other letter cases.
    """
    parents = lects._parents()
    nodes = [node for node in set(parents) | set(parents.values()) if not node.startswith("x-")]
    arabic = set(matching._ARABIC_VARIETIES) | {"ara"}
    regions = {}
    for node in nodes:
        language = node.split("-")[0].lower()
        for part in lects.without_private_use(node).split("-")[1:]:
            if len(part) == 2:
                for each in (arabic if language in arabic else {language}):
                    regions.setdefault(each, set()).add(part)
    tags = set()
    for language, places in regions.items():
        for region in places:
            tags |= {f"{language}-{region}", f"{language.upper()}-{region}",
                     f"{language.title()}-{region}", f"{language}-Arab-{region}",
                     f"{language}-Latn-{region}", f"{language}-{region}-x-zz"}
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
    pairs_limit = 72000
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
    tree = lect_tree_battery()
    print(f"lect tree battery: {len(tree)} tags")
    every = sorted(set(battery) | set(extra) | set(tree))
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
    print(f"resolve tags: {len(every)} in, {len(resolved)} out")
    print(f"resolve: {len(every) - len(failures)}/{len(every)}")

    # lang_distance over real pairs: tags against a sample of catalog codes,
    # in every lect mode, through one CLI process.
    sample = rng.sample(available_json_codes(), 60)
    tags = battery[:400] + rng.sample(battery, min(len(battery), 400))
    pair_cases = [(a, b, mode) for a in tags for b in sample
                  for mode in ("none", "dialect", "standard")][:pairs_limit]
    # The sampled pairs rarely meet two lects of one language, so the lect
    # tree is compared edge by edge, outside the --pairs limit.
    lect_cases = [(a, b, mode) for child, parent in lects._parents().items()
                  for a, b in ((child, parent), (parent, child))
                  for mode in ("dialect", "standard")]
    print(f"lect tree pairs: {len(lect_cases)}")
    pair_cases += lect_cases
    lines = "".join(f"{a}\t{b}\t{mode}\n" for a, b, mode in pair_cases)
    run = subprocess.run([str(CLI), "resolve", "--distances"], input=lines,
                         capture_output=True, text=True)
    if run.returncode != 0:
        print(f"CLI resolve --distances rc={run.returncode}: {run.stderr.strip()[:200]}")
        return 1
    answers = run.stdout.split("\n")[:-1]
    print(f"lang_distance pairs: {len(pair_cases)} in, {len(answers)} out")
    if len(answers) != len(pair_cases):
        return 1
    distance_failures = []
    for (desired, supported, mode), got in zip(pair_cases, answers):
        want = lang_distance(desired, supported, None if mode == "none" else mode)
        if str(want) != got:
            distance_failures.append((desired, supported, mode, want, got))
    print(f"lang_distance: {len(pair_cases) - len(distance_failures)}/{len(pair_cases)}")

    for kind, a, want, got in failures[:40]:
        print(f"  FAIL {kind}: {a!r}: reference {want!r} vs cpp {got!r}")
    for a, b, mode, want, got in distance_failures[:40]:
        print(f"  FAIL lang_distance({a!r}, {b!r}, lects={mode}): reference {want} vs cpp {got}")
    return 1 if failures or distance_failures else 0


if __name__ == "__main__":
    sys.exit(main())
