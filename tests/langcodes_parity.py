#!/usr/bin/env python3
"""langcodes-port parity harness: the C++ `orthography2ipa::langcodes` module
against the installed Python `langcodes` the reference actually resolves
language tags with.

Everything orthography2ipa/registry.py reaches for is compared: tag parsing
(`Language.get` with and without normalization), `standardize_tag` with
`macro=True` and `macro=False`, `simplify_script`, `assume_script`,
`broader_tags`, the likely-subtag fill-in behind `maximize()` / `max_form()`
(including round-tripping the maximized form back through it), and
`tag_distance` with and without `ignore_script`.

The tag battery comes from the places tags actually arrive from: every spec
code in the C++ data catalog, every key and value of the reference registry's
alias tables, every key and value of langcodes' own replacement and
likely-subtag tables, every tag langcodes writes into its own docstrings, and a
deterministic fuzz battery built by mutating those (case flips, '_' separators,
appended scripts/regions/variants/extensions/private subtags, malformed and
truncated pieces). Distances are compared for every ordered pair of catalog
codes whose primary subtags match or that the distance matrix itself names --
which is where resolution actually decides -- plus a deterministic random
sample over the whole pool.

Usage: langcodes_parity.py <path-to-check_langcodes> [--full] [--verbose]
                         [--seeds N] [--sample N]

`check_langcodes` is the standalone driver in tests/_scratch; build it against
the library as described in build/notes/worker_langcodes.md. Without --full the
fuzz seeds and the random distance sample are smaller, so the harness stays
usable as a CTest case.

The reference package is located via $O2I_PYTHON_ROOT, else the sibling
directory of this repository. langcodes must be importable, so this harness
only runs under an interpreter that has it installed (the reference environment
is /home/flavio/.venvs/piper/bin/python3); it fails loudly otherwise.
"""
import collections
import os
import pathlib
import random
import re
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
CPP_ROOT = HERE.parent

try:
    import langcodes
    from langcodes import Language, standardize_tag, tag_distance
    from langcodes.data_dicts import (DEFAULT_SCRIPTS, LANGUAGE_DISTANCES,
                                      LANGUAGE_REPLACEMENTS, LIKELY_SUBTAGS,
                                      NORMALIZED_MACROLANGUAGES,
                                      TERRITORY_REPLACEMENTS)
except ImportError as exc:
    print(f"this harness needs the langcodes package ({exc}); "
          f"install it into the interpreter that runs this harness")
    sys.exit(1)

REFERENCE = os.environ.get("O2I_PYTHON_ROOT") or str(CPP_ROOT.parent / "orthography2ipa")
if not (pathlib.Path(REFERENCE) / "orthography2ipa" / "registry.py").is_file():
    print(f"reference package not found at {REFERENCE!r} (set O2I_PYTHON_ROOT)")
    sys.exit(1)

SEP = "\x1f"
ERR = "!ERR"

SCRIPTS = ["Latn", "Cyrl", "Arab", "Hans", "Hant", "Zzzz", "Aran", "Qaag", "Dsrt",
           "Mand", "Tfng", "Geor", "latn", "CYRL", "hAnS"]
REGIONS = ["US", "GB", "FR", "TW", "HK", "MO", "419", "001", "003", "019", "EU",
           "QU", "ZY", "ZZ", "XY", "cs", "eg", "gb", "us", "in", "au", "za", "es",
           "br", "pt", "004", "142", "202"]
VARIANTS = ["1901", "1994", "hepburn", "oxendict", "valencia", "scouse", "lipaw",
            "fonipa", "arevmda", "foobar", "12345678", "123456789", "a1", "xy",
            "latn", "us", "1901"]
EXTENSIONS = ["u-co-backward", "a-bbb", "u-ka-foobar", "x-pig-latin", "0-foo",
              "a-b-foo", "u-", "x-", "x-123456789", "?-foo", "u-co-"]
PIECES = ["x-thingy", "u-ka-foobar", "a-bbb-ccc", "ibe", "min", "nan", "yue",
          "cmn", "hans", "hakka", "oed", "bok", "nyn", "default", "1", "12345",
          "123456789", "", "zzzzzzzzz", "Latn", "US"]
BAD = ["", "-", "a", "1", "12", "abcde", "abcdef", "abcdefghi", "x", "x-", "-x",
       "und-u-", "und-0-", "und-?-foo", "und-x-123456789", "en-a-b-foo",
       "C.UTF-8", "en_US.UTF-8", "ar-\u0660\u0660\u0661", "en--US", "en-US--",
       "u-co-backward", "\u00fcnd", "II", "II-", "en-latn-Latn", "zh-tw-hant",
       "ja-hepburn-latn", "spa-mx-latn", "sgn-ase-bfi", "de-1901-1901",
       "en-a-bbb-a-ddd", "zh-min", "zh-min-nan", "zh-min-nan-nan", "root",
       "sh-QU", "sgn-US", "en-gb-oed", "i-navajo", "art-lojban", "no-bok",
       "cel-gaulish", "und-ibe", "zh-cmn-hans-cn", "und-Latn-US", "und-CH",
       "und-Zzzz", "und-ZZ", "mul", "mis", "enc", "unp", "qww", "xxx", "zz",
       "i-enochian", "sgn-be-fr", "sgn-ch-de", "tl", "in", "iw", "ar-EG",
       "en-US-oxendict-biscayan", "fi-u-ca-gregory-nydirectly-p-ms",
       "und-US", "und-Zzzz-ZZ", "zzz", "zxx", "und-Arab", "arb-Arab",
       "sr-Latn-RS", "sr-Cyrl-RS", "hr-Latn", "bs", "sh", "tol", "cu", "hbs",
       # the shapes the reference registry actually gets handed, and the
       # non-ASCII tags whose lowercasing must still fail the same way
       "EN-gb", "DE_at", "pt_BR", "ar-sa-x-najdi", "ar-SA-x-NAJD", "foo-XX",
       "en-NZ", "zh-latn-poj", "mni-MTEI", "mn-Mong-CN", "de-1901-x-",
       "\u212a", "en-\u212a", "\u00fcnd-US", "stra\u00dfe", "\u03a3\u03c2",
       "mn-Cyrl-MN", "en\u00a0US", "ja-Latn-hepburn-heploc", "und-ABCD-EFGH",
       "cel-gaulish", "i-ami", "i-bnn", "sgn-be-fr", "rlin"]

TAGISH = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_-]{0,29}$")


def join(values):
    return ",".join(values or ())


def fields(lang):
    """The same view of a parsed tag that the C++ driver prints."""
    return SEP.join([
        lang.language or "",
        join(lang.extlangs),
        lang.script or "",
        lang.territory or "",
        join(lang.variants),
        join(lang.extensions),
        lang.private or "",
        str(lang),
    ])


# ── the tag pool ────────────────────────────────────────────────────────────

def catalog_codes():
    data = CPP_ROOT / "data"
    codes = sorted(p.name[: -len(".json")] for p in data.glob("*.json"))
    if not codes:
        print(f"no spec catalog found under {data}")
        sys.exit(1)
    return codes


def registry_tags():
    sys.path.insert(0, REFERENCE)
    import logging
    logging.disable(logging.CRITICAL)
    from orthography2ipa import registry
    tags = set()
    for table in (registry._ALIASES, registry._BARE_DEFAULTS, registry._REGION_DEFAULTS):
        tags.update(table.keys())
        tags.update(table.values())
    return tags


def langcodes_table_tags():
    """Everything the port's generated tables can rewrite or look up, so that
    no entry of any table is left unexercised by the battery."""
    tags = set()
    for table in (LANGUAGE_REPLACEMENTS, TERRITORY_REPLACEMENTS, LIKELY_SUBTAGS,
                  DEFAULT_SCRIPTS, NORMALIZED_MACROLANGUAGES):
        tags.update(table.keys())
        tags.update(table.values())
    for desired, by_supported in LANGUAGE_DISTANCES.items():
        for supported in list(by_supported) + [desired]:
            if "_" not in supported and supported.isalnum():
                tags.add(supported)
    return tags


def docstring_tags():
    """Every tag langcodes itself writes into its docstrings: the cases its own
    authors thought were worth pinning."""
    root = pathlib.Path(langcodes.__file__).parent
    call = re.compile(r">>>\s+(?:.*?)"
                      r"(?:Language\.get|Language\.make|standardize_tag|tag_distance)"
                      r"\((.*?)\)")
    quoted = re.compile(r"['\"]([^'\"]*)['\"]")
    tags = set()
    for path in sorted(root.rglob("*.py")):
        if "__pycache__" in path.parts:
            continue
        for line in path.read_text(encoding="utf-8").splitlines():
            match = call.search(line)
            if match:
                tags.update(v for v in quoted.findall(match.group(1)) if TAGISH.match(v))
    return tags


def mutate(tag, rng):
    """Deterministic mutations of one real tag: the shapes locale names, config
    values and hand-typed tags actually arrive in, plus the shapes BCP 47
    forbids."""
    out = {tag.lower(), tag.upper(), tag.replace("-", "_")}
    subtags = tag.split("-")
    for i, subtag in enumerate(subtags):
        flipped = subtag.upper() if subtag.islower() else subtag.lower()
        rebuilt = list(subtags)
        rebuilt[i] = flipped
        out.add("-".join(rebuilt))
    for piece in rng.sample(SCRIPTS + REGIONS + VARIANTS + EXTENSIONS + PIECES, 8):
        out.add(f"{tag}-{piece}")
        out.add(f"{tag}_{piece}")
        out.add(f"{tag}-{piece}-x")
    for piece in rng.sample(REGIONS + SCRIPTS, 3):
        out.add(f"{piece}-{tag}")
    if len(subtags) > 1:
        out.add("-".join(subtags[1:]))
        out.add("-".join(subtags[:-1]))
    out.add(tag[: max(1, len(tag) - 1)])
    out.add(tag + "-" + subtags[-1])
    return out


def build_pool(full, seed_cap=None):
    catalog = catalog_codes()
    base = sorted(set(catalog) | registry_tags() | langcodes_table_tags()
                  | docstring_tags() | set(BAD))
    rng = random.Random(20241031)
    seeds = base if full else base[::4]
    cap = seed_cap or (6000 if full else 2000)
    if len(seeds) > cap:
        seeds = rng.sample(seeds, cap) if full else seeds[:cap]
    tags = set(base)
    # The three cheapest mutations apply to every tag in the pool, not just to
    # the sampled seeds: case folding and '_' separators are what locale names
    # and hand-typed config values actually look like, and the reference's
    # grandfathered tags are matched case-insensitively, so their upper-case
    # forms have to be in the battery at every battery size.
    for tag in base:
        tags.add(tag.lower())
        tags.add(tag.upper())
        tags.add(tag.replace("-", "_"))
    for seed in seeds:
        tags.update(mutate(seed, rng))
    return catalog, base, sorted(tags)


# ── the reference answers ───────────────────────────────────────────────────

def reference_answer(mode, a, b, flag):
    try:
        if mode == "P":
            return fields(Language.get(a, normalize=bool(flag)))
        if mode == "S":
            return str(standardize_tag(a, macro=bool(flag)))
        if mode == "M":
            return str(Language.get(a).maximize())
        if mode == "A":
            return str(Language.get(a).assume_script())
        if mode == "R":
            return str(Language.get(a).simplify_script())
        if mode == "Q":
            return " ".join(Language.get(a).broader_tags())
        if mode == "D":
            return str(tag_distance(a, b, ignore_script=bool(flag)))
    except Exception:
        return ERR
    return ERR


def primary(tag):
    return tag.split("-")[0].lower()


def distance_pairs(catalog, pool, rng, limit):
    """Every ordered catalog pair whose primaries match, every pair the distance
    matrix itself names, and a deterministic random sample over the pool."""
    by_primary = collections.defaultdict(list)
    for code in catalog:
        by_primary[primary(code)].append(code)

    pairs = []
    for codes in by_primary.values():
        pairs.extend((a, b) for a in codes for b in codes)
    for desired, by_supported in LANGUAGE_DISTANCES.items():
        if "_" in desired:
            continue
        for supported in by_supported:
            if "_" in supported:
                continue
            for a in by_primary.get(desired, ()):
                for b in by_primary.get(supported, ()):
                    pairs.append((a, b))
    unique = list(dict.fromkeys(pairs))
    for _ in range(limit):
        unique.append((rng.choice(pool), rng.choice(pool)))
    return unique


def build_cases(full, seed_cap, sample):
    catalog, base, tags = build_pool(full, seed_cap)
    cases = []
    for tag in tags:
        cases.append(("P", tag, "", 1))
        cases.append(("P", tag, "", 0))
        cases.append(("S", tag, "", 1))
        cases.append(("S", tag, "", 0))
        for mode in ("M", "A", "R", "Q"):
            cases.append((mode, tag, "", 0))

    # Round-trip: a maximized tag fed back through maximize/standardize has to
    # be a fixed point there too, on both sides.
    round_trip = set()
    for tag in tags:
        answer = reference_answer("M", tag, "", 0)
        if answer != ERR:
            round_trip.add(answer)
    for tag in sorted(round_trip - set(tags)):
        cases.append(("P", tag, "", 1))
        cases.append(("S", tag, "", 1))
        cases.append(("M", tag, "", 0))

    rng = random.Random(20241031)
    pairs = distance_pairs(catalog, base, rng, sample)
    for a, b in pairs:
        cases.append(("D", a, b, 0))
        cases.append(("D", b, a, 0))
        if primary(a) == primary(b):
            # The resolution-relevant neighbourhood: same language, both
            # directions, with the script ignored as well.
            cases.append(("D", a, b, 1))
            cases.append(("D", b, a, 1))
    return catalog, tags, round_trip, cases


def run_cpp(binary, cases, tag):
    workdir = CPP_ROOT / "build" / "notes"
    workdir.mkdir(parents=True, exist_ok=True)
    # one case file per battery, so concurrent runs cannot read each other's
    path = workdir / ("langcodes_cases%s.tsv" % tag)
    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        for mode, a, b, flag in cases:
            handle.write(f"{mode}\t{a}\t{b}\t{flag}\n")
    out = subprocess.run([str(binary), str(path)], check=True,
                         stdout=subprocess.PIPE).stdout.decode("utf-8")
    answers = []
    for line in out.split("\n"):
        if not line:
            continue
        parts = line.split("\t")
        answers.append(parts[4] if len(parts) > 4 else ERR)
    return answers


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(2)
    binary = pathlib.Path(os.path.abspath(sys.argv[1]))
    if not binary.is_file():
        print(f"check binary not found at {binary}")
        sys.exit(2)
    full = "--full" in sys.argv
    verbose = "--verbose" in sys.argv

    def option(name, default):
        return int(sys.argv[sys.argv.index(name) + 1]) if name in sys.argv else default

    seed_cap = option("--seeds", 0)
    sample = 200000 if full else option("--sample", 20000)

    catalog, tags, round_trip, cases = build_cases(full, seed_cap, sample)
    n_pairs = sum(1 for mode, *_ in cases if mode == "D") // 2
    print(f"batteries: {len(catalog)} catalog codes, {len(tags)} tags "
          f"({'fully' if full else 'partially'} fuzzed), "
          f"{len(round_trip)} maximized round-trip tags, {len(cases)} cases "
          f"of which {n_pairs} distance pairs")

    answers = run_cpp(binary, cases, "_full" if full else f"_{seed_cap or 'd'}_{sample}")
    if len(answers) != len(cases):
        print(f"FAIL: the driver answered {len(answers)} of {len(cases)} cases")
        sys.exit(1)

    total = collections.Counter()
    errored = collections.Counter()
    bad = collections.Counter()
    classes = collections.Counter()
    examples = {}
    for (mode, a, b, flag), actual in zip(cases, answers):
        expected = reference_answer(mode, a, b, flag)
        total[mode] += 1
        if expected == ERR:
            errored[mode] += 1
        if expected != actual:
            bad[mode] += 1
            kind = ("cpp-raises" if expected != ERR and actual == ERR else
                    "ref-raises" if expected == ERR and actual != ERR else "value")
            cls = (mode, kind)
            classes[cls] += 1
            examples.setdefault(cls, (a, b, flag, expected, actual))

    print(f"{'mode':>6} {'checked':>10} {'raises':>10} {'diffs':>8}")
    for mode in sorted(total):
        print(f"{mode:>6} {total[mode]:>10} {errored[mode]:>10} {bad[mode]:>8}")
    print(f"total: {sum(total.values())} cases checked against the installed "
          f"langcodes, {sum(bad.values())} diffs in {len(classes)} diff classes")
    for cls, count in classes.most_common(20):
        print(f"  {cls}: {count}")
    if verbose:
        for key, (a, b, flag, expected, actual) in list(examples.items())[:40]:
            print(f"  {key} {a!r} {b!r} flag={flag}\n     ref: {expected!r}"
                  f"\n     cpp: {actual!r}")
    if bad:
        print("FAIL")
        sys.exit(1)
    print("PASS")


if __name__ == "__main__":
    main()
