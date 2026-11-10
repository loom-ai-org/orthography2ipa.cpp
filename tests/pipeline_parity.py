#!/usr/bin/env python3
"""Pipeline parity: an engine's own lexicon and syllabifier, against the reference.

Each command runs in the C++ driver and in the reference's pipeline.py path, and
the two result lines must be equal. The grid crosses languages, words, a lexicon
that is absent, empty or a file, and each syllabifier mode, over the word, text,
detailed, candidates, confidence and lattice surfaces, with a process lexicon and
a registered syllabifier set, so an engine that reads either shows up. A second
grid crosses the expand, mark, rescore and sandhi fields, allow_undeclared_phonemes
and each old argument that conflicts with a field. It also runs load_lexicon,
packaged_lexicon with and without the patches, the inventory functions with a
mapping, detect_stress with a syllabifier, and the deprecation text.

A forced pronunciation runs in each language, so the grid holds the type and the
text of each MarkupError. A newline in that text is `|` on the driver's line.

Usage: pipeline_parity.py <path-to-check-pipeline-binary> [--only grid|fields|packaged|deprecation]
"""
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
if not (pathlib.Path(reference) / "orthography2ipa" / "pipeline.py").is_file():
    sys.exit(f"reference package not found at {reference!r} (set O2I_PYTHON_ROOT)")
sys.path.insert(0, reference)
warnings.simplefilter("ignore")
logging.disable(logging.CRITICAL)

from orthography2ipa import (  # noqa: E402
    G2P, Pipeline, SandhiPlugin, SyllabifierPlugin, detect_stress, emission_inventory, get,
    load_lexicon, packaged_lexicon, phoneme_inventory)
from orthography2ipa import lexicon as lexmod  # noqa: E402
from orthography2ipa import registry  # noqa: E402
from orthography2ipa.inventory import phoneme_atoms, tokenize  # noqa: E402
from orthography2ipa.markup import MarkupError  # noqa: E402
from orthography2ipa.phonetok import Candidate  # noqa: E402
from orthography2ipa.rescorer import LatticeRescorer  # noqa: E402
from orthography2ipa.sentence import SentenceRescorer  # noqa: E402
from reference_regression import engine_path, pre_rescore_slots  # noqa: E402

CLICK = "ʘ"
MODES = ["halves", "whole", "chars", "empty", "bad", "raise"]
WORDS = {
    "pt-PT": ["gato", "rato", "pasta", "mesmo", "muitíssimo", "e", "cão", "Lisboa,", "abacaxi",
              "distribuidoras", "bueiros"],
    "pt-BR": ["gato", "pneu", "pacto", "advogado", "papel", "cima", "egípcio", "Djavan", "taqu",
              "distribuidoras", "bueiros"],
    "pt-PT-x-alentejo": ["teatro", "havaiano"],
    "ca": ["lingüístics"],
    "es-ES": ["cantaba", "gato", "ahí", "país", "construcción", "hola"],
    "en-GB": ["colonel", "yacht", "nation", "the", "water"],
    "nl-NL": ["geven", "gebed", "verder", "veldslag", "quota"],
    "ru": ["вода", "его", "молоко", "хорошо", "его\u0301", "его\u0300", "во\u0301", "большо\u0301го", "самого\u0301",
           "горячего", "ЕГО\u0301"],
    "ru-x-southern": ["его", "его\u0301"],
    "ar": ["كتاب", "مدرسة", "هذا"],
    "hi": ["कमल", "नमस्ते", "भारत"],
    "fr-FR": ["maison", "amis", "garçon"],
    "de-DE": ["Wasser", "Vater", "gehen"],
    "cnr": ["с́екира", "љубав", "ljubav"],
    "fi": ["kalevala", "talo"],
    "ht": ["kay", "lapli"],
}
TEXTS = {"pt-PT": ["os gatos", "gato, rato"],
         "pt-BR": ["os gatos", "muito bem"], "en-GB": ["the yacht", "colonel Mustard"]}
# A click no table emits, the click of each lexicon, a word that only segmentation
# declares, a lone stress mark before a vowel, a vowel of few specs, a repeated token
# that the message lists twice, an apostrophe that changes the repr quotes, and a
# padded ph that the message quotes stripped. Then
# a word that the check passes before a word that it refuses.
FORCED = [f"<phoneme ph=\"{ipa}\">x</phoneme>"
          for ipa in ("ʘa", "ʘɐ", "ɡato", "ˈa", "ə", "ʘʘ", "a'ʘ", " ʘa ")]
FORCED.append(f"gato {FORCED[4]} {FORCED[0]}")
# A compound element is split from the word, so the check runs there too.
FORCED_WORDS = [f"guarda-{FORCED[0]}", f"guarda-{FORCED[4]}"]


class Mode(SyllabifierPlugin):
    def __init__(self, mode, code=""):
        self.mode, self.code = mode, code

    def syllabify(self, word, lang=None):
        if self.mode == "whole":
            return [word]
        if self.mode == "empty":
            return []
        if self.mode == "bad":
            return ["x"]
        if self.mode == "raise":
            raise ValueError("cannot split")
        if self.mode == "chars":
            return list(word)
        half = len(word) // 2
        return [word[:half], word[half:]] if half else [word]

    @property
    def language_codes(self):
        return [self.code]


class BacksA(LatticeRescorer):
    """/a/ to [ɐ], a symbol pt-PT declares."""

    def rescore(self, slot, context):
        if slot.candidates and slot.candidates[0].ipa == "a":
            return (Candidate(ipa="ɐ", cost=0.0),)
        return slot.candidates


class Bar(SandhiPlugin):
    """A bar after the last word, so a sentence shows where sandhi ran."""

    def apply(self, words, surfaces, pausal, lang):
        return [*words[:-1], words[-1] + "|"] if words else []

    @property
    def language_codes(self):
        return []


class Keeps(SentenceRescorer):
    def rescore(self, word, context):
        return word.ipa


ACUTE = "\u0301"
# Each expand and mark mode, with the same meaning in the driver.
EXPAND = {"digits": lambda text: text.replace("101", "cento e um"), "words": lambda text: text.replace("gato", "o gato bom")}
MARK = {"acute": lambda text: text.replace("a", "a" + ACUTE).replace("о", "о" + ACUTE),
        "change": lambda text: text.replace("o", "0"),
        "short": lambda text: text[:-1],
        "strip": lambda text: text.replace("\u0301", "")}
# Inputs for the fields grid: plain words, an acute already written, a forced
# word with a declared and an undeclared phoneme, a forced word that expand
# would rewrite, digits, and punctuation.
FIELD_TEXTS = {
    "pt-PT": ["gato casa", "o gato, a casa.", "casa 101 gatos", "pá e pato",
              f"gato {FORCED[0]} casa", f"a {FORCED[4]} casa", "101", "3a", "a1b2",
              '<phoneme ph="ɡatu">gato</phoneme> 101 casa'],
    "es-ES": ["la casa", "cantaba 22", f"gata {FORCED[0]}", "Málaga"],
    "en-GB": ["the yacht", "101 dalmatians", f"a {FORCED[0]}"],
    "ru": ["вода молоко", "его хорошо", f"вода {FORCED[0]}"],
}
FIELD_MODES = [
    ("-", "-", "-", "-", "0", "-"), ("digits", "-", "-", "-", "0", "-"), ("words", "-", "-", "-", "0", "-"),
    ("-", "acute", "-", "-", "0", "-"), ("words", "acute", "-", "-", "0", "-"),
    ("-", "change", "-", "-", "0", "-"), ("-", "short", "-", "-", "0", "-"), ("-", "strip", "-", "-", "0", "-"),
    ("-", "-", "backs", "-", "0", "-"), ("-", "-", "two", "-", "0", "-"),
    ("-", "-", "-", "bar", "0", "-"), ("-", "-", "-", "-", "1", "-"),
    ("digits", "acute", "backs", "bar", "1", "-"),
    ("-", "-", "backs", "-", "0", "rescorer"), ("-", "-", "-", "bar", "0", "sentence_rescorer"),
    ("-", "-", "backs", "-", "0", "named-rescore"), ("-", "-", "-", "bar", "0", "named-sandhi"),
    ("digits", "-", "-", "-", "0", "named-normalize"), ("-", "acute", "-", "-", "0", "named-normalize"),
    ("-", "-", "-", "-", "0", "rescorer"), ("-", "-", "-", "-", "0", "named-sandhi"),
]


def field_engine(lang, expand, mark, rescore, sandhi, allow, old):
    pipeline = Pipeline(expand=EXPAND.get(expand), mark=MARK.get(mark),
                        rescore={"-": (), "backs": (BacksA(),), "two": (BacksA(), BacksA())}[rescore],
                        sandhi=Bar() if sandhi == "bar" else None)
    extra = {"rescorer": {"rescorer": BacksA()}, "sentence_rescorer": {"sentence_rescorer": Keeps()},
             "named-rescore": {"plugins": {"rescore": "nothing"}},
             "named-sandhi": {"plugins": {"sandhi": "nothing"}},
             "named-normalize": {"plugins": {"normalize": "nothing"}}}.get(old, {})
    return G2P(lang, pipeline=pipeline, allow_undeclared_phonemes=allow == "1", **extra)


class _Dist:
    name = "fake-lexicons"


class _EntryPoint:
    group = lexmod.ENTRY_POINT_GROUP
    dist = _Dist()

    def __init__(self, name, provider):
        self.name, self.value, self.provider = name, f"fake:{name}", provider

    def load(self):
        return self.provider


def plugin(mode):
    return None if mode == "-" else Mode(mode)


def lexicon_of(field):
    if field == "-":
        return None
    return {} if field == "{}" else load_lexicon(field)


def number(value):
    return f"{value:.12g}"


def joined(values):
    return "|".join(sorted(values))


engine = None


def run(field):
    global engine
    command = field[0]
    if command == "reset":
        lexmod.clear_lexicons()
        registry._syllabifiers = {}
        return ""
    if command == "register":
        lexmod.register_lexicon(field[1], field[2])
        return ""
    if command == "discovered":
        registry._syllabifiers = {registry.resolve(field[1]): Mode(field[2], field[1])}
        return ""
    if command == "engine":
        engine = G2P(field[1], pipeline=Pipeline(lexicon=lexicon_of(field[2]),
                                                 syllabifier=plugin(field[3])))
        return ""
    if command == "fields":
        engine = None
        engine = field_engine(*field[1:])
        return ""
    if command == "word":
        return engine.transcribe_word(field[1])
    if command == "text":
        return engine.transcribe(field[1])
    if command == "detailed":
        result = engine.transcribe_detailed(field[1])
        return result.ipa + "".join(f"|{w.word}={w.ipa}={number(w.confidence)}" for w in result.words)
    if command == "features":
        return "|".join(f"{w.word}={number(w.confidence)}={len(w.graphemes)}"
                        for w in engine.features(field[1]))
    if command == "sentence":
        return "|".join(w.surface for w in engine.sentence_lattice(field[1]).words)
    if command == "candidates":
        return "|".join(engine.word_candidates(field[1], k=5))
    if command == "confidence":
        return number(engine.word_confidence(field[1]))
    if command == "lattice":
        return "|".join(f"{slot.top.ipa}:{len(slot.candidates)}" for slot in engine.ipa_lattice(field[1]))
    if command == "path":
        path = engine_path(engine, field[1])
        return f"{path.ipa}:{number(path.score)}"
    if command == "pre":
        return "".join(slot["grapheme"] + "=" + "".join(f"{c['ipa']}:{number(c['cost'])}," for c in slot["candidates"]) + "|"
                       for slot in pre_rescore_slots(engine, field[1]))
    if command in ("load", "packaged", "patched"):
        entries = (load_lexicon(field[1]) if command == "load" else
                   packaged_lexicon(field[1], patches=command == "patched"))
        return "".join(f"{word}={ipa}," for word, ipa in sorted(entries.items()))
    if command in ("emission", "phonemes", "atoms", "tokens"):
        spec, mapping = get(field[1]), lexicon_of(field[2])
        if command == "emission":
            return joined(emission_inventory(spec, mapping))
        if command == "phonemes":
            return joined(phoneme_inventory(spec, mapping))
        if command == "tokens":
            return "|".join(tokenize(field[3], spec, mapping))
        return joined(phoneme_atoms(spec, mapping))
    if command == "stress":
        return str(detect_stress(field[1], get(field[2]).stress, lang=field[2],
                                 syllabifier=plugin(field[3])))
    sys.exit(f"unknown command {command}")


def expected(field):
    try:
        return f"ok\t{run(field)}"
    except (FileNotFoundError, LookupError) as error:
        return f"{type(error).__name__}\t{error.args[0]}"
    except MarkupError as error:
        return "MarkupError\t" + str(error).replace("\n", "|")
    except ValueError as error:
        return f"ValueError\t{error}"
    except registry.MissingPlugin as error:
        return "MissingPlugin\t" + str(error).replace("\n", "|")
    except AttributeError as error:
        # A command on the engine that a failed "fields" left unset.
        return "NoEngine\t" if engine is None else f"AttributeError\t{error}"


def write_tsv(path, rows):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("".join(f"{word}\t{ipa}\n" for word, ipa in rows), encoding="utf-8")
    return str(path)


def commands(root):
    process = {lang: write_tsv(root / "process" / f"{lang}.tsv", [(w.strip(",").lower(), f"{CLICK}ɐ")
                                                                 for w in words[::2]])
               for lang, words in WORDS.items()}
    own = {lang: write_tsv(root / "own" / f"{lang}.tsv", [(w.strip(",").lower(), "ɡaʃtɐ")
                                                          for w in words[1::2]] + [("x", f"{CLICK}a")])
           for lang, words in WORDS.items()}
    out = []
    for lang, words in WORDS.items():
        for lexicon in ("-", "{}", own[lang]):
            for mode in ("-", *MODES):
                for process_set in (False, True):
                    out.append(["reset"])
                    out.append(["discovered", lang, "whole"])
                    if process_set:
                        out.append(["register", lang, process[lang]])
                    out.append(["engine", lang, lexicon, mode])
                    for word in words:
                        out += [[surface, word] for surface in
                                ("word", "text", "detailed", "candidates", "confidence", "lattice", "path", "pre")]
                    out += [["text", text] for text in TEXTS.get(lang, [])]
                    out += [[surface, text] for text in FORCED
                            for surface in ("text", "detailed", "features", "sentence")]
                    out += [["word", word] for word in FORCED_WORDS]
        for lexicon in ("-", "{}", own[lang]):
            out.append(["reset"])
            out += [[kind, lang, lexicon] for kind in ("emission", "phonemes", "atoms")]
            out += [["tokens", lang, lexicon, ipa] for ipa in (f"{CLICK}a", "ˈɡaʃtɐ")]
        for mode in ("-", *MODES) if get(lang).stress else ():
            out.append(["reset"])
            out.append(["discovered", lang, "whole"])
            out += [["stress", word, lang, mode] for word in words]
    out.append(["reset"])
    out += [["load", own["pt-PT"]], ["load", str(root / "absent.tsv")]]
    return out


def field_commands():
    out = []
    for lang, texts in FIELD_TEXTS.items():
        for modes in FIELD_MODES:
            out.append(["fields", lang, *modes])
            out += [[surface, text] for text in texts for surface in ("text", "detailed", "features", "sentence")]
            out += [["word", word] for word in ("gato", "casa", "101")]
    return out


def compare(binary, root, label, env, lines):
    environ = {k: v for k, v in os.environ.items() if k != "ORTHOGRAPHY2IPA_LEXICON_DIR"}
    environ.update(env)
    text = "\n".join("\t".join(line) for line in lines) + "\n"
    got = subprocess.run([binary], input=text, capture_output=True, text=True, check=True,
                         env=environ).stdout.split("\n")[:-1]
    if len(got) != len(lines):
        sys.exit(f"pipeline parity {label}: {len(lines)} commands in, {len(got)} lines out")
    bad = 0
    for line, value in zip(lines, got):
        want = expected(line)
        if want != value:
            bad += 1
            if bad <= 20:
                print(f"DIFF {line}\n  reference {want!r}\n  port      {value!r}")
    print(f"pipeline parity {label}: {len(lines)} commands, {bad} differ")
    return bad


def packaged_case(binary, root):
    """A fixture provider: its entries, its patches, a failing one, and a code
    with none. An exception wins over a patch for the same word."""
    good = write_tsv(root / "packaged" / "pt-PT" / "fixture.tsv", [("gato", "ˈɡaʃtɐ")])
    write_tsv(root / "packaged" / "en-GB" / "one.tsv", [("the", "ðə")])
    write_tsv(root / "packaged" / "en-GB" / "two.tsv", [("the", "ðiː")])
    write_tsv(root / "packaged" / "es-ES" / "fixture.tsv", [("gato", "ˈɡato")])
    patch = write_tsv(root / "packaged" / "patches" / "pt-PT" / "fixture.tsv",
                      [("gato", "ˈɡatu"), ("rato", "ˈʁatu")])

    def broken(code):
        raise RuntimeError("en-GB has 2 lexicon files")

    points = [_EntryPoint("pt-PT", lambda code: good), _EntryPoint("en-GB", broken),
              _EntryPoint("es-ES", lambda code: str(root / "packaged" / "es-ES" / "fixture.tsv"))]
    patch_points = [_EntryPoint("pt-PT", lambda code: patch)]
    lexmod.entry_points = lambda group=None: (list(points) if group == lexmod.ENTRY_POINT_GROUP else
                                              list(patch_points) if group == lexmod.PATCH_ENTRY_POINT_GROUP
                                              else [])
    lexmod.clear_lexicons()
    lines = [["packaged", "pt-PT"], ["packaged", "pt"], ["packaged", "es-ES"],
             ["patched", "pt-PT"], ["patched", "pt"], ["patched", "es-ES"], ["patched", "fr-FR"]]
    bad = compare(binary, root, "packaged", {"ORTHOGRAPHY2IPA_PACKAGED_LEXICONS": str(root / "packaged")}, lines)
    # A failing provider serves nothing, so it raises on both sides. Its name
    # stays in the installed list of the reference and of the port.
    lines = [["packaged", "en-GB"]]
    bad += compare(binary, root, "failing provider",
                   {"ORTHOGRAPHY2IPA_PACKAGED_LEXICONS": str(root / "packaged")}, lines)
    return bad


def deprecation_case():
    """Each deprecated name carries the reference's warning text."""
    header = (HERE.parent / "include" / "orthography2ipa" / "orthography2ipa.hpp").read_text(encoding="utf-8")
    template = re.search(r'\[\[deprecated\(name " (.*?)" replacement "(.*?)"\)\]\]', header)
    text = template.group(1) + "{}" + template.group(2) if template else ""
    want = f"is deprecated and will be removed in {lexmod._REMOVAL_VERSION}; use {{}}."
    bad = 0 if text == want else 1
    calls = {
        "register_lexicon": lambda: lexmod.register_lexicon("pt-PT", "/nothing"),
        "set_lexicon_dir": lambda: lexmod.set_lexicon_dir(None),
        "clear_lexicons": lexmod.clear_lexicons,
        "use_packaged_lexicons": lambda: lexmod.use_packaged_lexicons([]),
        "get_lexicon": lambda: lexmod.get_lexicon("en-GB"),
    }
    for name, call in calls.items():
        with warnings.catch_warnings(record=True) as caught:
            warnings.simplefilter("always")
            call()
        message = str(caught[0].message)
        replacement = message.split("; use ", 1)[1].rstrip(".")
        if f'O2I_DEPRECATED_LEXICON_API("{name}", "{replacement}")' not in header:
            print(f"DIFF deprecation {name}: {message}")
            bad += 1
    lexmod.clear_lexicons()
    # get_syllabifier is no lexicon name, so its attribute holds the whole text.
    with warnings.catch_warnings(record=True) as caught:
        warnings.simplefilter("always")
        found = registry.get_syllabifier("pt-PT")
    message = str(caught[0].message)
    if found is not None or f'[[deprecated("{message}")]]' not in header:
        print(f"DIFF deprecation get_syllabifier: {found!r} {message}")
        bad += 1
    print(f"pipeline parity deprecation: {len(calls) + 1} names, template {text!r}, {bad} differ")
    return bad


def main():
    if len(sys.argv) not in (2, 4) or (len(sys.argv) == 4 and sys.argv[2] != "--only"):
        sys.exit(__doc__)
    binary = sys.argv[1]
    only = sys.argv[3] if len(sys.argv) == 4 else None
    bad = 0
    with tempfile.TemporaryDirectory() as tmp:
        root = pathlib.Path(tmp)
        if only in (None, "grid"):
            bad += compare(binary, root, "grid", {}, commands(root))
        if only in (None, "fields"):
            bad += compare(binary, root, "fields", {}, field_commands())
        if only in (None, "packaged"):
            bad += packaged_case(binary, root)
        if only in (None, "deprecation"):
            bad += deprecation_case()
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
