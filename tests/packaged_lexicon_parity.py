#!/usr/bin/env python3
"""packaged lexicon parity: the port's search-path discovery against the
reference's entry-point discovery, over the same lexicon files.

The reference finds packaged lexicons through the ``orthography2ipa.lexicons``
entry points of installed distributions. The port reads the roots that
$ORTHOGRAPHY2IPA_PACKAGED_LEXICONS lists, each laid out as the o2i-lexicons
package's data directory. Every scenario gives both sides the same providers:
the reference gets one installed distribution per root, ordered by name as the
roots are ordered, and the port gets the roots.

The first scenario uses the installed o2i-lexicons package itself. It
transcribes every word of every packaged lexicon with the rules alone, then
with the packaged lexicon enabled, then after clear_lexicons. The second uses
two fixture distributions that cover precedence, the language filter, an alias
provider, a provider for no registered spec and a failing provider.

Both sides run the same command list and write one line per command, warnings
included, with each provider's identity (an entry point's value, a provider
directory) replaced by its name. Exit 0 only when every line matches.

Usage: packaged_lexicon_parity.py <path-to-check-packaged-lexicons-binary>
"""
import importlib.metadata
import inspect
import logging
import os
import pathlib
import re
import subprocess
import sys
import tempfile
import warnings

HERE = pathlib.Path(__file__).resolve().parent
reference = os.environ.get("O2I_PYTHON_ROOT") or str(HERE.parent.parent / "orthography2ipa")
if not (pathlib.Path(reference) / "orthography2ipa" / "lexicon.py").is_file():
    sys.exit(f"reference package not found at {reference!r} (set O2I_PYTHON_ROOT)")
sys.path.insert(0, reference)

import o2i_lexicons  # noqa: E402  the installed provider package is required

import orthography2ipa  # noqa: E402
from orthography2ipa import lexicon  # noqa: E402
from orthography2ipa.exceptions import UnmappedGraphemeWarning  # noqa: E402

# Lexicon words such as ⟨abiquiú⟩ hold letters the en-GB rules do not map; the
# comparison covers the transcription, not the reference's warning about it.
warnings.simplefilter("ignore", UnmappedGraphemeWarning)

GROUP = lexicon.ENTRY_POINT_GROUP


class Captured(logging.Handler):
    def __init__(self):
        super().__init__(logging.WARNING)
        self.messages = []

    def emit(self, record):
        self.messages.append(record.getMessage())


def reference_lines(commands, distributions, masks):
    """Run *commands* in this process with only *distributions* installed."""
    real = importlib.metadata.entry_points
    lexicon.entry_points = lambda group=None: [
        ep for ep in real(group=group) if ep.dist is not None and ep.dist.name in distributions]
    handler = Captured()
    log = logging.getLogger(lexicon.__name__)
    log.addHandler(handler)
    log.propagate = False
    engines = {}
    out = []
    try:
        lexicon.clear_lexicons()
        for field in commands:
            try:
                value = run_reference(field, engines)
                result = f"ok\t{value}"
            except (LookupError, FileNotFoundError) as error:
                result = f"{type(error).__name__}\t{error}"
            out.append(f"{result}\tW:{mask(' | '.join(handler.messages), masks)}")
            handler.messages.clear()
    finally:
        lexicon.clear_lexicons()
        lexicon.entry_points = real
        log.removeHandler(handler)
        log.propagate = True
    return out


def run_reference(field, engines):
    command = field[0]
    if command == "use":
        if len(field) == 1:
            return ",".join(orthography2ipa.use_packaged_lexicons())
        return ",".join(orthography2ipa.use_packaged_lexicons([c for c in field[1:] if c]))
    if command == "sources":
        return "".join(f"{c}={p};" for c, p in sorted(orthography2ipa.packaged_lexicon_sources().items()))
    if command == "available":
        return ",".join(lexicon.available_lexicon_codes())
    if command == "get":
        return "".join(f"{w}={i}," for w, i in sorted(lexicon.get_lexicon(orthography2ipa.resolve(field[1])).items()))
    if command == "path":
        return str(lexicon.lexicon_path(field[1]))
    if command == "word":
        if field[1] not in engines:
            engines[field[1]] = orthography2ipa.G2P(field[1])
        return engines[field[1]].transcribe_word(field[2])
    if command == "register":
        lexicon.register_lexicon(field[1], field[2])
    elif command == "dir":
        lexicon.set_lexicon_dir(field[1])
    elif command == "clear":
        lexicon.clear_lexicons()
    else:
        raise ValueError(f"unknown command {command}")
    return ""


def mask(text, masks):
    for identity, name in masks.items():
        text = text.replace(repr(identity), f"<{name}>")
    return text


def port_lines(binary, commands, roots, masks):
    env = dict(os.environ, ORTHOGRAPHY2IPA_PACKAGED_LEXICONS=":".join(str(r) for r in roots))
    text = "".join("\t".join(c) + "\n" for c in commands)
    out = subprocess.run([binary], input=text, capture_output=True, text=True,
                         check=True, env=env).stdout.splitlines()
    return [line.split("\tW:", 1)[0] + "\tW:" + mask(line.split("\tW:", 1)[1], masks) for line in out]


def compare(label, binary, commands, distributions, roots, reference_masks, port_masks):
    want = reference_lines(commands, distributions, reference_masks)
    got = port_lines(binary, commands, roots, port_masks)
    if len(got) != len(commands) or len(want) != len(commands):
        sys.exit(f"packaged lexicon parity {label}: {len(commands)} commands in, "
                 f"{len(want)} reference and {len(got)} port lines out")
    bad = [(c, w, g) for c, w, g in zip(commands, want, got) if w != g]
    for c, w, g in bad[:20]:
        print(f"FAIL {label}: {chr(9).join(c)!r}\n  python: {w[:300]!r}\n  cpp:    {g[:300]!r}")
    words = sum(1 for c in commands if c[0] == "word")
    print(f"packaged lexicon parity {label}: {len(commands)} commands in, {len(got)} lines out, "
          f"{words} word transcriptions, {len(bad)} differ")
    return len(bad)


def package_scenario(binary):
    """The installed o2i-lexicons package: every word of every packaged lexicon."""
    codes = o2i_lexicons.available_codes()
    if not codes:
        sys.exit("o2i-lexicons packages no lexicon")
    words = {code: sorted(lexicon.parse_lexicon_text(o2i_lexicons.tsv_path(code).read_text(encoding="utf-8")))
             for code in codes}
    commands = [("available",), ("sources",)]
    commands += [("word", code, w) for code in codes for w in words[code]]
    commands += [("use", codes[0]), ("available",), ("use", "zz-NOWHERE"), ("use",), ("available",)]
    commands += [("get", code) for code in codes] + [("path", code) for code in codes]
    commands += [("word", code, w) for code in codes for w in words[code]]
    commands += [("clear",), ("available",)]
    commands += [("word", code, w) for code in codes for w in words[code][:50]]
    for code in codes:
        commands += [("clear",), ("use", code.split("-")[0]), ("word", code, words[code][0])]
    dist = importlib.metadata.distribution("o2i-lexicons")
    reference_masks = {ep.value: "o2i-lexicons" for ep in dist.entry_points if ep.group == GROUP}
    port_masks = {str(o2i_lexicons.DATA_DIR / code): "o2i-lexicons" for code in codes}
    print(f"o2i-lexicons {o2i_lexicons.__version__}: "
          + ", ".join(f"{code} {len(words[code])} words" for code in codes))
    return compare("o2i-lexicons", binary, commands, {"o2i-lexicons"}, [o2i_lexicons.DATA_DIR],
                   reference_masks, port_masks)


PROVIDER_SOURCE = '''from pathlib import Path

DATA_DIR = Path({root!r})


{tsv_path}'''


def write_distribution(site, name, root, codes):
    """An installed distribution *name* whose entry points serve *codes* from
    *root* through a copy of o2i_lexicons.tsv_path."""
    module = name.replace("-", "_")
    (site / f"{module}.py").write_text(
        PROVIDER_SOURCE.format(root=str(root), tsv_path=inspect.getsource(o2i_lexicons.tsv_path)),
        encoding="utf-8")
    info = site / f"{module}-0.0.0.dist-info"
    info.mkdir()
    (info / "METADATA").write_text(f"Metadata-Version: 2.1\nName: {name}\nVersion: 0.0.0\n", encoding="utf-8")
    (info / "entry_points.txt").write_text(
        f"[{GROUP}]\n" + "".join(f"{code} = {module}:tsv_path\n" for code in codes), encoding="utf-8")
    return {f"{module}:tsv_path": name}


def fixture_scenario(binary, tmp):
    site = tmp / "site"
    site.mkdir()
    roots = [tmp / "a", tmp / "b"]
    files = {
        # root a: an alias provider, a provider for no registered spec, the
        # first of two pt-PT providers
        "a/en/alias.tsv": "colonel\tkɝnəl\nyacht\tjɑt\n",
        "a/en-GB-x-nowhere/x.tsv": "yacht\tjɒt\n",
        "a/pt-PT/first.tsv": "casa\tkazɐ\n",
        # root b: the exact en-GB provider, a second pt-PT, a failing fr-FR
        "b/en-GB/exact.tsv": "colonel\tˈkɜːnəl\nyacht\tjɒt\nthe\tθiː\n",
        "b/pt-PT/second.tsv": "casa\tkɐzɐ\n",
        "b/fr-FR/one.tsv": "chat\tʃa\n",
        "b/fr-FR/two.tsv": "chat\tʃat\n",
        "b/es-ES/notes.txt": "a directory with no *.tsv is no provider\n",
        "mine.tsv": "colonel\tkɝnəl\n",
        "dir/en-GB.tsv": "colonel\tkɜːnl\n",
    }
    for rel, body in files.items():
        (tmp / rel).parent.mkdir(parents=True, exist_ok=True)
        (tmp / rel).write_text(body, encoding="utf-8")
    reference_masks = {}
    for name, root in zip(["fixture-a-lexicons", "fixture-b-lexicons"], roots):
        codes = sorted(d.name for d in root.iterdir() if any(d.glob("*.tsv")))
        reference_masks.update(write_distribution(site, name, root, codes))
    sys.path.insert(0, str(site))
    port_masks = {str(d): f"fixture-{root.name}-lexicons" for root in roots for d in root.iterdir()}
    sample = [("word", "en-GB", w) for w in ("colonel", "yacht", "the", "nation")]
    sample += [("word", "pt-PT", "casa"), ("word", "fr-FR", "chat")]
    commands = [*sample, ("available",), ("path", "en-GB"), ("sources",), ("available",), ("get", "en-GB"),
                ("use", "pt-PT"), ("available",), ("get", "pt-PT"), ("get", "en-GB"), *sample,
                ("use", "en", "zz-NOWHERE", "xx-ALSO"), ("available",),
                ("use", ""), ("use", "fr-FR"), ("get", "fr-FR"), ("path", "fr-FR"), *sample,
                ("use",), ("available",), ("get", "en-GB"), ("path", "en-GB"), *sample,
                ("register", "en-GB", str(tmp / "mine.tsv")), ("path", "en-GB"), *sample,
                ("clear",), ("use", "en-GB"), ("dir", str(tmp / "dir")), ("path", "en-GB"), *sample,
                ("dir", str(tmp / "nowhere")), ("path", "en-GB"), *sample,
                ("clear",), ("available",), ("get", "en-GB"), *sample]
    return compare("fixtures", binary, commands, {"fixture-a-lexicons", "fixture-b-lexicons"}, roots,
                   reference_masks, port_masks)


def main():
    binary = os.path.abspath(sys.argv[1])
    failures = package_scenario(binary)
    with tempfile.TemporaryDirectory() as tmp:
        failures += fixture_scenario(binary, pathlib.Path(tmp))
    if failures:
        sys.exit(f"packaged lexicon parity: {failures} lines differ")
    print("packaged lexicon parity: every line matches")


if __name__ == "__main__":
    main()
