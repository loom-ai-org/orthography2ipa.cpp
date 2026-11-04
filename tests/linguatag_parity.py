#!/usr/bin/env python3
"""linguatag-port parity harness: the C++ `orthography2ipa::linguatag` module
against the installed Python linguatag that the reference resolves language
tags with.

registry-parity reaches linguatag only through `resolve` and
`resolve --distances`, and a rule of the port can change without changing any
resolution. This harness calls each public function of the port directly and
compares it with linguatag: `standardize_lang`, `default_region`,
`with_default_region`, `private_use`, `without_private_use`, `lect_steps`,
`lang_distance` with no lect mode and with each lect mode, and `closest_lang`.

The tag battery is every code in the reference catalog, every CLDR
likely-region key, every node of the lect tree, every linguatag `OVERRIDES`
key, every registry `_ALIASES` key and value, the primary subtag of each
catalog code, a list of unusual tags (empty subtags, voice ids, grandfathered
tags, non-ASCII case and digits), and seeded case, separator and private-use
forms of a fifth of them. Distances and lect steps are compared over every
edge of the lect tree in both directions, the pt-UY rows, the unusual tags
against each other and a seeded random sample. `closest_lang` is compared over
tie lists, where the candidate that spells the target must win a tied
distance, and over seeded random candidate lists.

Usage: linguatag_parity.py <path-to-orthography2ipa-check-linguatag>

The reference package is located via $O2I_PYTHON_ROOT, else the sibling
directory of this repository. The harness fails loudly (exit 1) when the
reference, linguatag or the driver is unavailable.
"""
import logging
import os
import pathlib
import random
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
CPP_ROOT = HERE.parent

reference = os.environ.get("O2I_PYTHON_ROOT") or str(CPP_ROOT.parent / "orthography2ipa")
if not (pathlib.Path(reference) / "orthography2ipa" / "registry.py").is_file():
    print(f"reference package not found at {reference!r} (set O2I_PYTHON_ROOT)")
    sys.exit(1)
sys.path.insert(0, reference)

logging.disable(logging.CRITICAL)
try:
    import linguatag
    from linguatag import lects, matching, regions
except ImportError as exc:
    print(f"this harness needs the linguatag package ({exc}); "
          f"install it into the interpreter that runs this harness")
    sys.exit(1)
import orthography2ipa.registry as registry
from orthography2ipa.json_loader import available_json_codes

DRIVER = pathlib.Path(os.path.abspath(sys.argv[1]))
MODES = ("none", "dialect", "standard")
ERR = "!ERR"

ODD = ["", "-", "x-foo", "en--US", "ar-", "-ar", "ar_", "zh-HK-HiuMaanNeural", "tl", "tgl", "TGL", "fil",
       "iw", "zh-yue", "zh-min-nan", "i-klingon", "en-Dsrt", "yue-Hant", "yue-Hans", "yue-HK", "yue-MO",
       "pt-UY", "pt-UY-x-foo", "pt-uy", "pt_UY", "pt-Latn", "pt-Latn-UY", "pt-419", "es-419", "ar-001",
       "arb", "ar-Latn", "arz-Latn", "en-١٢٣", "en-١٢", "ｅｎ", "EN-gb", "sr-Latn", "sr-Cyrl", "zh-Hant",
       "zh", "cmn", "zh-CN", "zh-TW", "ur", "ur-PK", "ur-Arab", "en-US-x-twang", "gl-x-central",
       "an-x-cheso", "an-x-ansotano", "ǅa", "İt", "ß", "sh", "mo", "no", "nb", "nn", "und", "root",
       "x-clade-romance", "art-lojban", "zh-guoyu", "sgn-BR", "en-GB-oed", "ast-PT-x-rionor",
       "pt-PT-x-trasosmontes", "tmg", "aja", "ajg", "ajg-BJ", "pt-BR-x-brasilia", "pt-BR-x-cuiaba"]

# closest_lang cases where two candidates share the smallest distance.
TIES = [("pt-PT", ["pt", "pt-PT"]), ("pt-PT", ["pt-PT", "pt"]), ("pt", ["pt-PT", "pt"]),
        ("pt", ["pt", "pt-PT"]), ("pt", ["pt-BR", "pt-PT", "pt"]), ("PT-pt", ["pt", "pt-pt", "pt-PT"]),
        ("en", ["en-GB", "en"]), ("en-GB", ["en", "en-GB"]), ("pt-UY", ["pt-BR", "pt-PT", "pt"]),
        ("pt-UY", ["pt-PT", "pt-BR"]), ("pt-UY-x-foo", ["pt-BR", "pt-UY", "pt-PT"]),
        ("ar", ["arb", "ar-001", "ar"]), ("ar-001", ["ar", "arb", "ar-001"]),
        ("yue-HK", ["yue", "zh-HK", "zh-Hant"]), ("an-x-cheso", ["an", "an-x-ansotano"]),
        ("tl", ["fil", "tl"]), ("tgl", ["fil", "tl", "tgl"])]


def none_or(value):
    return "None" if value is None else str(value)


def lects_arg(mode):
    return None if mode == "none" else mode


def tag_battery(rng):
    catalog = sorted(available_json_codes())
    parents = lects._parents()
    tags = set(catalog) | set(regions._likely_regions()) | set(parents) | set(parents.values())
    tags |= set(linguatag.OVERRIDES)
    tags |= set(registry._ALIASES) | set(registry._ALIASES.values())
    tags |= {code.split("-")[0] for code in catalog}
    tags |= set(ODD)
    for tag in sorted(tags):
        if rng.random() < 0.2:
            tags |= {tag.lower(), tag.upper(), tag.replace("-", "_"), tag + "-x-zz"}
    return sorted(tags), parents


def requests():
    """(request line, linguatag's answer) for every case of the battery."""
    rng = random.Random(48)
    tags, parents = tag_battery(rng)
    cases = []

    def add(fields, call):
        try:
            want = call()
        except Exception:
            want = ERR
        cases.append(("\t".join(fields), want))

    for tag in tags:
        add(["std", tag], lambda: linguatag.standardize_lang(tag))
        add(["dr", tag], lambda: none_or(linguatag.default_region(tag)))
        add(["wdr", tag], lambda: linguatag.with_default_region(tag))
        add(["pu", tag], lambda: ",".join(lects.private_use(tag)))
        add(["wpu", tag], lambda: lects.without_private_use(tag))
    pairs = [pair for child, parent in parents.items() for pair in ((child, parent), (parent, child))]
    pt = [tag for tag in tags if tag.lower().startswith("pt")]
    pairs += [("pt-UY", b) for b in pt] + [(a, "pt-UY") for a in pt] + [("pt-UY-x-foo", b) for b in pt]
    pairs += [(rng.choice(tags), rng.choice(tags)) for _ in range(30000)]
    pairs += [(a, b) for a in ODD for b in ODD]
    for a, b in pairs:
        add(["steps", a, b], lambda: none_or(lects.lect_steps(a, b)))
        for mode in MODES:
            add(["dist", a, b, mode], lambda: str(linguatag.lang_distance(a, b, lects_arg(mode))))
    closest = [(target, available, max_distance, mode)
               for target, available in TIES for max_distance in (0, 4, 10, 30) for mode in MODES]
    for _ in range(3000):
        target = rng.choice(tags)
        lang = target.split("-")[0].split("_")[0].lower()
        available = [tag for tag in tags if tag.lower().startswith(lang)][:40] + rng.sample(tags, 10)
        rng.shuffle(available)
        closest.append((target, [tag for tag in available if tag], rng.choice((0, 5, 10, 20)),
                        rng.choice(MODES)))
    for target, available, max_distance, mode in closest:
        add(["closest", target, str(max_distance), mode, *available],
            lambda: none_or(matching.closest_lang(target, available, max_distance, lects_arg(mode))))
    return len(tags), cases


def main():
    if not DRIVER.is_file():
        print(f"driver not found at {DRIVER}")
        return 1
    n_tags, cases = requests()
    run = subprocess.run([str(DRIVER)], input="".join(line + "\n" for line, _ in cases),
                         capture_output=True, text=True)
    if run.returncode != 0:
        print(f"driver rc={run.returncode}: {run.stderr.strip()[:300]}")
        return 1
    got = run.stdout.split("\n")[:-1]
    print(f"linguatag parity: {n_tags} tags; requests {len(cases)} in, {len(got)} out")
    counts = {}
    diffs = []
    for (line, want), answer in zip(cases, got):
        fn = line.split("\t")[0]
        total, differ = counts.get(fn, (0, 0))
        if answer != want:
            differ += 1
            diffs.append((line[:160], want, answer))
        counts[fn] = (total + 1, differ)
    for fn, (total, differ) in counts.items():
        print(f"  {fn}: {total} cases, {differ} differ")
    for line, want, answer in diffs[:40]:
        print(f"  DIFF {line!r}: linguatag {want!r}, cpp {answer!r}")
    if len(got) != len(cases) or diffs:
        print("FAIL")
        return 1
    print("OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
