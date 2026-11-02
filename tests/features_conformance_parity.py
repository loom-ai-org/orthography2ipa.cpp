#!/usr/bin/env python3
"""features.py / conformance.py parity harness.

Validates src/features.cpp and src/conformance.cpp (the C++ ports of
orthography2ipa/features.py and orthography2ipa/conformance.py) against the
reference package.

  features   — `build_word_features` is a pure read over a word's lattice slots
               and grapheme contexts, so the harness feeds BOTH implementations
               the same inputs: the reference's own `G2P.ipa_lattice` slots and
               confidence for real catalog words, plus constructed slots for the
               shapes real data rarely produces (a deleted zero-candidate slot,
               a one-candidate slot, a tied margin, a slot whose context is gone,
               a one-grapheme word, an empty lattice). Contexts come either from
               the C++ tokenizer over the same word (`native`) or from the same
               token list on both sides (`tokens`), and the GraphemeContext view
               the record builder actually read is compared too, so a diff is
               attributable to features.py's own logic rather than to phonetok.

  conformance— the kit runs over the reference's own good / bad plugin fixtures
               and the same C++ classes, and the verdict, the rule and the
               message are compared.

Usage: features_conformance_parity.py <path-to-check-binary> [--sweep N]
                                      [--only features|conformance]
"""
import json
import os
import pathlib
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
CPP_ROOT = HERE.parent
SCRATCH = HERE / "_scratch"

reference = os.environ.get("O2I_PYTHON_ROOT") or str(CPP_ROOT.parent / "orthography2ipa")
if not (pathlib.Path(reference) / "orthography2ipa" / "features.py").is_file():
    print(f"reference package not found at {reference!r} (set O2I_PYTHON_ROOT)")
    sys.exit(1)
sys.path.insert(0, reference)

import logging  # noqa: E402
logging.disable(logging.CRITICAL)

from orthography2ipa import available_codes, get  # noqa: E402
from orthography2ipa.features import build_word_features  # noqa: E402
from orthography2ipa.g2p import G2P  # noqa: E402
from orthography2ipa.phonetok import (Candidate, SegmentSlot, Token, TokenKind,  # noqa: E402
                                      flat_contexts)

# Categories the catalog sweep silently misses (TODO section 8: hi, lo, th, bn,
# ta, te, kn, ml, mr, ne, my, bo, km, ko … declare no word_exceptions at all),
# so they are pinned here rather than left to a data-derived battery.
PINNED = [
    ("pt-PT", ["coelho", "viagem", "mãe", "pão", "água", "paralelepípedo"]),
    ("en-GB", ["hello", "through", "colonel", "queue"]),
    ("fr-FR", ["terre", "femme", "verre", "oiseau", "château"]),
    ("es-ES", ["seguro", "agua", "gato"]),
    ("ru", ["молоко", "гости", "щука"]),
    ("ar", ["مدرسة", "كتاب", "عمّ", "لَكِنَّ", "الشمس"]),
    ("he", ["שלום", "בית"]),
    ("hi", ["कृष्ण", "राम", "क्या"]),
    ("ta", ["கக", "க்க", "தமிழ்"]),
    ("bn", ["রাম"]),
    ("ml", ["കൃഷ്ണൻ"]),
    ("km", ["ម្ល៉េះ"]),
    ("my", ["မြန်မာ"]),
    ("th", ["เบิ่ง", "ເບິ່ງ", "ไทย"]),
    ("bo", ["བོད་"]),
    ("ko", ["한국"]),
    ("it", ["ghiaccio"]),
    ("sv", ["kväll", "hjärt"]),
    ("hu", ["szieszt"]),
    ("vi", ["quảng"]),
    ("tr", ["şadırvan"]),
    ("pl", ["źrebak"]),
    ("fi", ["äly"]),
    ("cs", ["đáp"]),
]


def spec_vowels(code):
    try:
        return frozenset(get(code).vowel_graphemes or ())
    except Exception:
        return frozenset()


def battery(limit):
    """(lang, word) pairs: the pinned list plus up to `limit` word_exceptions
    from every spec that declares them."""
    cases = [(lang, word) for lang, words in PINNED for word in words]
    seen = set(cases)
    for code in available_codes():
        try:
            spec = get(code)
        except Exception:
            continue
        taken = 0
        for word in (spec.word_exceptions or {}):
            if limit and taken >= limit:
                break
            if not word.strip():
                continue
            taken += 1
            if (code, word) not in seen:
                seen.add((code, word))
                cases.append((code, word))
    return cases


ENGINES = {}


def engine_for(lang):
    """One reference engine per language: g2p.py's per-engine memos are pure,
    so reusing an engine is the same read as building a fresh one, and the
    battery would otherwise pay spec loading per word."""
    engine = ENGINES.get(lang)
    if engine is None:
        engine = G2P(lang)
        ENGINES[lang] = engine
    return engine


def reference_cases(lang, word):
    """Both context modes of one word, from a single reference read.

    Mirrors g2p.py `G2P.features`: ipa_lattice + confidence_breakdown +
    flat_contexts over the same word's grapheme tokens.
    """
    engine = engine_for(lang)
    spec = engine.spec
    slots = engine.ipa_lattice(word)
    confidence = engine.confidence_breakdown(word).value
    g_tokens = engine._tokenizer.grapheme_tokens(word)
    contexts = flat_contexts(g_tokens, spec.vowel_graphemes)
    token_rows = [{"grapheme": t.grapheme, "ipa": list(t.ipa),
                   "position": t.position, "length": t.length} for t in g_tokens]
    return [dump_case(f"{lang}:{word}:{mode}", lang, word, mode, slots, contexts,
                      confidence, spec.code, spec.script, tokens=token_rows,
                      vowel_graphemes=spec.vowel_graphemes)
            for mode in ("native", "tokens")]


def dump_case(name, lang, word, mode, slots, contexts, confidence, code, script,
              tokens=None, vowel_graphemes=None):
    """Build the record on both sides' shared inputs and serialise them.

    `vowel_graphemes` is the set the reference's flat_contexts was given: the
    grapheme→axis predicates read it, so the C++ side has to be handed the very
    same list rather than the spec's own.
    """
    built = build_word_features(word, slots, contexts, confidence, code, script)
    slot_data = [{"grapheme": s.grapheme, "start": s.span[0], "end": s.span[1],
                  "candidates": [{"ipa": c.ipa, "cost": repr(c.cost)} for c in s.candidates]}
                 for s in slots]
    context_data = [{
        "grapheme": c.grapheme, "start": c.span[0], "end": c.span[1],
        "is_vowel": bool(c.is_vowel), "is_consonant": bool(c.is_consonant),
        "is_front": bool(c.is_front), "is_back": bool(c.is_back),
        "prev": None if c.at(-1) is None else c.at(-1).grapheme,
        "next": None if c.at(1) is None else c.at(1).grapheme,
        "prev2": None if c.at(-2) is None else c.at(-2).grapheme,
        "next2": None if c.at(2) is None else c.at(2).grapheme,
    } for c in contexts]
    token_data = tokens
    if token_data is None:
        token_data = [{"grapheme": c.grapheme, "ipa": list(c.ipa),
                      "position": c.span[0], "length": c.span[1] - c.span[0]}
                     for c in contexts]
    return {
        "name": name, "lang": lang, "word": word, "mode": mode,
        "vowel_graphemes": sorted(vowel_graphemes if vowel_graphemes is not None
                                  else spec_vowels(lang)),
        "code": code, "script": script, "confidence": repr(confidence),
        "slots": slot_data, "tokens": token_data,
        "contexts": context_data, "expect": built.as_dicts(),
    }


def synthetic_cases():
    """The shapes the catalog battery does not produce.

    A deleted (zero-candidate) slot, a slot whose start no context carries, a
    tied margin, a one-candidate slot, a one-grapheme word, more contexts than
    slots, and an empty lattice.
    """
    out = []
    vowels = frozenset({"a"})
    token_rows = [{"grapheme": "k", "ipa": ["k"], "position": 0, "length": 1},
                  {"grapheme": "a", "ipa": ["a", "\u0251"], "position": 1, "length": 1},
                  {"grapheme": "t", "ipa": ["t"], "position": 2, "length": 1}]
    contexts = flat_contexts(
        [Token(TokenKind.GRAPHEME, t["grapheme"], tuple(t["ipa"]), t["position"], t["length"])
         for t in token_rows], vowels)
    slots = [
        SegmentSlot("k", (0, 1), (Candidate("k", 0.0),)),
        # A deleted slot: no candidates at all.
        SegmentSlot("a", (1, 2), ()),
        # A slot whose start no context carries: neighbours and every predicate
        # fall away, the lattice stays readable.
        SegmentSlot("?", (9, 10), (Candidate("\u0294", 1.0), Candidate("\u0294\u0259", 2.5))),
        # One candidate \u21d2 no margin.
        SegmentSlot("t", (2, 3), (Candidate("t", 0.0),)),
    ]
    out.append(dump_case("synthetic:deleted-and-orphan-slots", "en-GB", "kat", "tokens",
                         slots, contexts, 0.25, "en-GB", "Latin",
                         tokens=token_rows, vowel_graphemes=vowels))

    tied = [SegmentSlot("k", (0, 1), (Candidate("k", 0.75), Candidate("\u0261", 0.75))),
            SegmentSlot("a", (1, 2), (Candidate("a", 0.0), Candidate("\u0251", 0.0))),
            SegmentSlot("t", (2, 3), (Candidate("t", 0.0),))]
    out.append(dump_case("synthetic:tied-margin", "en-GB", "kat", "tokens",
                         tied, contexts, 0.0, "en-GB", "Latin",
                         tokens=token_rows, vowel_graphemes=vowels))
    # A one-grapheme word is "initial" (the reference tests index 0 first), and
    # its own run has no neighbours at all.
    single = flat_contexts([Token(TokenKind.GRAPHEME, "k", ("k",), 0, 1)], vowels)
    out.append(dump_case("synthetic:one-grapheme-word", "en-GB", "k", "tokens",
                         tied[:1], single, 1.0, "en-GB", "Latin",
                         tokens=[token_rows[0]], vowel_graphemes=vowels))
    # An empty lattice is an empty sequence; the contexts of the word it came
    # from are still there.
    out.append(dump_case("synthetic:empty-lattice", "en-GB", "", "tokens",
                         [], contexts, 1.0, "en-GB", "Latin",
                         tokens=token_rows, vowel_graphemes=vowels))
    # More contexts than slots: the positions are the SLOT count, the
    # neighbours are the run's.
    out.append(dump_case("synthetic:contexts-outnumber-slots", "en-GB", "kat", "tokens",
                         tied[:2], contexts, 0.5, "en-GB", "Latin",
                         tokens=token_rows, vowel_graphemes=vowels))
    return out


def run_features(binary):
    print("── features parity ──")
    cases = []
    skipped = 0
    for index, (lang, word) in enumerate(
            battery(int(os.environ.get("O2I_SWEEP_LIMIT", "2"))), 1):
        try:
            cases += reference_cases(lang, word)
        except Exception:
            skipped += 2
        if index % 100 == 0:
            print(f"  ... {index} words", flush=True)
    cases += synthetic_cases()

    fixture = SCRATCH / "_features_fixture.json"
    fixture.write_text(json.dumps({"cases": cases}, ensure_ascii=False), encoding="utf-8")
    proc = subprocess.run([str(binary), "features", str(fixture)],
                          capture_output=True, text=True)
    if proc.returncode != 0:
        print("check binary failed:", proc.stdout[-2000:], proc.stderr[-2000:])
        sys.exit(1)
    produced = {}
    for line in proc.stdout.splitlines():
        if line.strip():
            row = json.loads(line)
            if "error" in row:
                print("check binary error:", row["error"])
                sys.exit(1)
            produced[row["name"]] = row

    cases_checked = 0
    cases_exact = 0
    fields_checked = 0
    fields_bad = 0
    context_fields_checked = 0
    context_fields_bad = 0
    missing = 0
    samples = []
    for case in cases:
        row = produced.get(case["name"])
        if row is None:
            missing += 1
            continue
        cases_checked += 1
        clean = True
        if len(row["dicts"]) != len(case["expect"]):
            clean = False
            fields_bad += 1
            if len(samples) < 12:
                samples.append(f"{case['name']}: n_graphemes cpp={len(row['dicts'])} "
                               f"py={len(case['expect'])}")
        else:
            for index, (actual, want) in enumerate(zip(row["dicts"], case["expect"])):
                if set(actual) != set(want):
                    clean = False
                    fields_bad += 1
                    if len(samples) < 12:
                        samples.append(f"{case['name']}[{index}]: key sets differ: "
                                       f"cpp={sorted(set(actual) - set(want))} "
                                       f"py={sorted(set(want) - set(actual))}")
                    continue
                for key, want_value in want.items():
                    fields_checked += 1
                    got_value = actual[key]
                    if got_value != want_value or type(got_value) is not type(want_value) \
                            and not isinstance(got_value, bool):
                        fields_bad += 1
                        clean = False
                        if len(samples) < 12:
                            samples.append(f"{case['name']}[{index}].{key}: "
                                           f"cpp={got_value!r} py={want_value!r}")
        for index, want in enumerate(case["contexts"]):
            actual = row["contexts"][index] if index < len(row["contexts"]) else {}
            for key, want_value in want.items():
                context_fields_checked += 1
                if actual.get(key) != want_value:
                    context_fields_bad += 1
                    clean = False
                    if len(samples) < 12:
                        samples.append(f"{case['name']} ctx[{index}].{key}: "
                                       f"cpp={actual.get(key)!r} py={want_value!r}")
        if clean:
            cases_exact += 1
    for line in samples:
        print("  DIFF", line)
    print(f"  reference cases built: {len(cases)} (cases the reference could not answer: "
          f"{skipped}; cases with no C++ answer: {missing})")
    print(f"  cases fully identical: {cases_exact}/{cases_checked}")
    print(f"  as_dict fields equal: {fields_checked - fields_bad}/{fields_checked}")
    print(f"  GraphemeContext fields equal: "
          f"{context_fields_checked - context_fields_bad}/{context_fields_checked}")
    return cases_exact, cases_checked


# ─── conformance ──────────────────────────────────────────────────────────

from orthography2ipa.conformance import (ConformanceError,  # noqa: E402
                                         DEFAULT_PROBE_WORDS,
                                         assert_rescorer_conforms,
                                         assert_syllabifier_conforms)
from orthography2ipa.rescorer import LatticeRescorer  # noqa: E402
from orthography2ipa.rescorer_plugin import RescorerPlugin  # noqa: E402
from orthography2ipa.syllabifier_plugin import SyllabifierPlugin  # noqa: E402


class Rewriting(SyllabifierPlugin):
    """_Rewriting: splits AND strips the accent, so the word downstream is not
    the word in."""
    def syllabify(self, word, lang=None):
        return list(word.replace("\u00e3", "a"))
    @property
    def language_codes(self):
        return ["pt-PT"]


class NoClaim(SyllabifierPlugin):
    def syllabify(self, word, lang=None):
        return [word]
    @property
    def language_codes(self):
        return []


class MergesHiatus(SyllabifierPlugin):
    def syllabify(self, word, lang=None):
        if word == "coelho":
            return ["coe", "lho"]
        return [word]
    @property
    def language_codes(self):
        return ["pt-PT"]


class WholeWord(SyllabifierPlugin):
    def __init__(self, codes, priority=50):
        self._codes = list(codes)
        self._priority = priority
    def syllabify(self, word, lang=None):
        return [word]
    @property
    def language_codes(self):
        return list(self._codes)
    @property
    def priority(self):
        return self._priority


class BacksA(LatticeRescorer):
    def rescore(self, slot, context):
        if slot.candidates and slot.candidates[0].ipa == "a":
            return (Candidate(ipa="ɐ", cost=0.0),)
        return slot.candidates


class Invents(LatticeRescorer):
    def rescore(self, slot, context):
        if slot.candidates and slot.candidates[0].ipa == "a":
            return (Candidate(ipa="Q", cost=0.0),)
        return slot.candidates


class FirstSlotMark(LatticeRescorer):
    def rescore(self, slot, context):
        if context.index == 0 and slot.candidates:
            return (Candidate(ipa="\u02cc", cost=0.0),)
        return slot.candidates


class WordCounting(LatticeRescorer):
    """Answers differently for the same word depending on how many words it has
    already seen: the same input, read at a different point in a batch, gives a
    different transcription."""
    def __init__(self):
        self.words_seen = 0
        self.at_word_start = False
    def rescore(self, slot, context):
        if context.index == 0:
            if not self.at_word_start:
                self.words_seen += 1
            self.at_word_start = True
        else:
            self.at_word_start = False
        if (slot.candidates and slot.candidates[0].ipa == "a"
                and self.words_seen % 2 == 0):
            return (Candidate(ipa="ɐ", cost=0.0),)
        return slot.candidates


class PerEngine(LatticeRescorer):
    """Answers differently for the same word depending on WHICH engine built
    it — the shape of the state a plugin naturally carries, because
    `rescorers(lang)` is called once per engine."""
    built = 0
    def __init__(self):
        PerEngine.built += 1
        self.even = PerEngine.built % 2 == 0
    def rescore(self, slot, context):
        if slot.candidates and slot.candidates[0].ipa == "a" and self.even:
            return (Candidate(ipa="ɐ", cost=0.0),)
        return slot.candidates


class NoClaimRescorer(RescorerPlugin):
    def rescorers(self, lang):
        return [BacksA()]
    @property
    def language_codes(self):
        return []


class LatticePlugin(RescorerPlugin):
    """rescorer_plugin.py RescorerPlugin: the plugin CONTRIBUTES the
    rescorers, one per engine construction."""
    def __init__(self, factory, codes=("pt-PT",)):
        self._factory = factory
        self._codes = list(codes)
    def rescorers(self, lang):
        return [self._factory()]
    @property
    def language_codes(self):
        return list(self._codes)


FR = ["terre", "femme", "verre"]
THREE = ["casa", "kata", "mesa"]
# Words whose pt-PT transcription a whole-word syllabifier moves.
MOVED_BY_WHOLE_WORD = ["naquelas", "leque", "saque", "aguardem", "albergue",
                       "embarque", "inextinguível"]


def reference_scenarios():
    one = ["coelho"]
    mae = ["m\u00e3e"]
    rows = [
        ("syllabifier:claims nothing, lang given",
         lambda: assert_syllabifier_conforms(NoClaim(), one, "pt-PT")),
        ("syllabifier:claims nothing, no lang",
         lambda: assert_syllabifier_conforms(NoClaim())),
        ("syllabifier:strips the accent",
         lambda: assert_syllabifier_conforms(Rewriting(), mae, "pt-PT")),
        ("syllabifier:merges the hiatus",
         lambda: assert_syllabifier_conforms(MergesHiatus(), one, "pt-PT-x-lisbon")),
        ("syllabifier:whole word, language with no stress",
         lambda: assert_syllabifier_conforms(WholeWord(["fr-FR"]), FR, "fr-FR")),
        ("syllabifier:claims another language only",
         lambda: assert_syllabifier_conforms(WholeWord(["es-ES"]), FR, "fr-FR")),
        ("syllabifier:default words, pt-PT",
         lambda: assert_syllabifier_conforms(WholeWord(["pt-PT"]), None, "pt-PT")),
        ("syllabifier:whole word at priority 40, pt-PT",
         lambda: assert_syllabifier_conforms(WholeWord(["pt-PT"], 40),
                                             MOVED_BY_WHOLE_WORD, "pt-PT")),
        ("lattice:a to ɐ (declared)",
         lambda: assert_rescorer_conforms(LatticePlugin(BacksA), ["casa"], "pt-PT")),
        ("lattice:a to Q (invented)",
         lambda: assert_rescorer_conforms(LatticePlugin(Invents), ["casa"], "pt-PT")),
        ("lattice:first slot to ˌ in a spec with no stress",
         lambda: assert_rescorer_conforms(LatticePlugin(
             lambda: FirstSlotMark(), ("fr-FR",)), FR, "fr-FR")),
        ("lattice:answers depend on the run",
         lambda: assert_rescorer_conforms(LatticePlugin(PerEngine), THREE, "pt-PT")),
        ("rescorer:claims nothing, no lang",
         lambda: assert_rescorer_conforms(NoClaimRescorer())),
        ("rescorer:default words, no lang given",
         lambda: assert_rescorer_conforms(LatticePlugin(BacksA))),
    ]
    out = []
    for name, body in rows:
        try:
            body()
            out.append({"scenario": name, "verdict": "conforms", "message": ""})
        except ConformanceError as error:
            out.append({"scenario": name, "verdict": "refused", "message": str(error)})
        except Exception as error:            # noqa: BLE001
            out.append({"scenario": name, "verdict": "engine-refused",
                        "message": f"{type(error).__name__}: {error}"})
    return out


def rule_of(message):
    for rule in ("the plugin claims no language codes", "language claim", "round-trip",
                 "downstream neutrality", "determinism", "inventory"):
        if message.startswith(rule):
            return rule
    return message.split(":")[0][:40] if message else ""


def run_conformance(binary):
    print("\u2500\u2500 conformance parity \u2500\u2500")
    want = {row["scenario"]: row for row in reference_scenarios()}
    proc = subprocess.run([str(binary), "conformance"], capture_output=True, text=True)
    if proc.returncode != 0:
        print("check binary failed:", proc.stdout[-2000:], proc.stderr[-2000:])
        sys.exit(1)
    got = {}
    for line in proc.stdout.splitlines():
        if line.strip():
            row = json.loads(line)
            got[row["scenario"]] = row
    verdicts = agreed = rules = messages = compared = 0
    for name, expected in want.items():
        actual = got.get(name)
        if actual is None:
            print(f"  MISSING on the C++ side: {name}")
            continue
        compared += 1
        verdicts += 1
        same_decision = (actual["verdict"] == expected["verdict"]) or \
            (expected["verdict"] == "refused" and actual["verdict"] == "engine-refused")
        agreed += same_decision
        same_rule = rule_of(actual["message"]) == rule_of(expected["message"])
        rules += same_rule
        exact = actual["message"] == expected["message"]
        messages += exact
        if not same_decision or not same_rule:
            print(f"  DIFF {name}: cpp={actual['verdict']} py={expected['verdict']}")
            print(f"    cpp: {actual['message'][:220]}")
            print(f"    py : {expected['message'][:220]}")
        elif not exact:
            print(f"  same rule, different detail: {name}")
            print(f"    cpp: {actual['message'][:220]}")
            print(f"    py : {expected['message'][:220]}")
    cpp_only = sorted(set(got) - set(want))
    for name in cpp_only:
        print(f"  cpp-only scenario (no reference seam for it): {name} "
              f"-> {got[name]['verdict']}: {rule_of(got[name]['message'])}")
    print(f"  scenarios compared: {compared}; verdict agreed {agreed}/{compared}; "
          f"rule agreed {rules}/{compared}; message byte-identical "
          f"{messages}/{compared}")
    print(f"  C++-only scenarios (the whole-word RescorerPlugin seam): "
          f"{len(cpp_only)}")
    return agreed, len(want)


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(2)
    binary = pathlib.Path(os.path.abspath(sys.argv[1]))
    only = None
    if "--only" in sys.argv:
        only = sys.argv[sys.argv.index("--only") + 1]
    if "--sweep" in sys.argv:
        os.environ["O2I_SWEEP_LIMIT"] = sys.argv[sys.argv.index("--sweep") + 1]
    if only != "conformance":
        run_features(binary)
    if only != "features":
        # Every reference scenario must reach the same verdict on the C++ side.
        agreed, expected = run_conformance(binary)
        if agreed != expected:
            sys.exit(1)


if __name__ == "__main__":
    main()
