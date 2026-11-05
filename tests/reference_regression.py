#!/usr/bin/env python3
"""Section-8 reference regression gate: compare the INTERMEDIATES, not only the
final strings.

The TODO's last open item is "compare intermediate token spans, grapheme slots,
segments, costs, stress, tone, and final IPA against the reference, not only
final strings". Every surface below is a reference producer matched to the C++
debug surface that exposes the same object, so a divergence names the STAGE that
produced it instead of only the word that came out wrong.

Usage:
    reference_regression.py <path-to-cli> [--battery B] [--surface S] [--sweep]
                            [--tolerance T] [--report PATH] [-q]

  --battery required     the six named languages: every word_exceptions entry
                         plus the pinned confirmed words. MUST be 100%.
  --battery categories   the ten requirement categories (reference_battery.py).
  --battery sentence     multi-word cases: the pinned spine + two-word and
                         comma-separated sentences from every sandhi spec.
  --battery all          everything (default).
  --surface S            one surface, or `all` (default). See SURFACES.
  --sweep                add the catalog sweep: every spec's word_exceptions.

Rules, from the TODO ("do not skip them silently"):
  * a missing reference package, a missing CLI surface, or a non-zero CLI exit
    is a FAILURE, never a skip;
  * a case whose REFERENCE call raises is counted as an error and fails, unless
    --allow-errors, which prints them in a named block;
  * parsed JSON is compared, never bytes; doubles within --tolerance;
  * the two bases are pinned per case. Reference `transcribe_word` performs no
    word splitting while `transcribe` re-tokenizes the utterance, and the
    reference itself does not equate them (ar `لَكِنَّ`: word basis `laːˈkinna`,
    sentence basis `laˈkinna`). Per-word surfaces therefore use the C++ `word`
    and `lattice` commands, and multi-word cases use `transcribe`.
"""
import argparse
import json
import math
import os
import pathlib
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
CPP_ROOT = HERE.parent
sys.path.insert(0, str(CPP_ROOT / "tests"))

reference = os.environ.get("O2I_PYTHON_ROOT") or str(CPP_ROOT.parent / "orthography2ipa")
if not (pathlib.Path(reference) / "orthography2ipa" / "phonetok.py").is_file():
    print(f"FAIL: reference package not found at {reference!r} (set O2I_PYTHON_ROOT)")
    sys.exit(1)
sys.path.insert(0, reference)

import logging  # noqa: E402
logging.disable(logging.CRITICAL)

import reference_battery as BATTERY  # noqa: E402
from orthography2ipa import tone as RT  # noqa: E402
from orthography2ipa import stress as RS  # noqa: E402
from orthography2ipa.g2p import G2P, _CLITIC_NO_STRESS  # noqa: E402
from orthography2ipa.phonetok import (  # noqa: E402
    PhonetokTokenizer, TokenKind, flat_contexts, lattice_confidence)
from orthography2ipa.positional import resolve_branches  # noqa: E402
from orthography2ipa.registry import available_codes, get  # noqa: E402
from orthography2ipa.rescorer import apply_rescorers  # noqa: E402

SURFACES = ["tokens", "grapheme_tokens", "beam", "lattice", "lattice_pre_rescore",
            "word", "word_candidates", "stress", "stress_ipa", "tone", "detailed",
            "sentence"]
WORD_SURFACES = ["lattice", "lattice_pre_rescore", "word", "word_candidates",
                 "stress_ipa", "tone"]
ALL_SURFACES = SURFACES

CLI = None
TOL = 1e-12
ERRORS = []


def cli(*args):
    """Run the C++ CLI. A non-zero exit is always a harness failure."""
    result = subprocess.run([str(CLI), *args], capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"CLI {args[0]!r} rc={result.returncode}: "
                           f"{result.stderr.strip()[:200]}")
    text = result.stdout.strip()
    # NOT `text[:1] in "[{"` — the empty string is a substring of everything,
    # so that test is True for an empty result and json.loads then explodes.
    if text[:1] in ("[", "{"):
        return json.loads(text)
    return text


def close(a, b):
    return math.isclose(float(a), float(b), rel_tol=TOL, abs_tol=TOL)


def eq(left, right):
    """Structural equality; doubles within tolerance; lists order-sensitive."""
    if isinstance(left, bool) or isinstance(right, bool):
        return left == right
    if isinstance(left, (int, float)) and isinstance(right, (int, float)):
        return close(left, right)
    if isinstance(left, list) and isinstance(right, list):
        return len(left) == len(right) and all(eq(x, y) for x, y in zip(left, right))
    if isinstance(left, dict) and isinstance(right, dict):
        return left.keys() == right.keys() and all(eq(left[k], right[k]) for k in left)
    return left == right


# ─────────────────────────────── engines ────────────────────────────────────
_ENGINES = {}


def engine(lang):
    if lang not in _ENGINES:
        try:
            _ENGINES[lang] = G2P(lang)
        except Exception as error:      # an untranslatable case is a hard error
            _ENGINES[lang] = error
    value = _ENGINES[lang]
    if isinstance(value, Exception):
        raise value
    return value


def engine_path(g, word):
    """The beam path `transcribe_word` finalizes — g2p.py's own choice of
    the positional beam or the plain tokenizer beam, then the grammatical
    ending rewrite. Mirrors `G2P::engine_path` on the C++ side."""
    if g._needs_context_beam:
        paths = g._positional_beam(word, 1)
    else:
        paths = g._tokenizer.ipa_beam(word, beam_width=1,
                                      expand_allophones=g.expand_allophones,
                                      rescorer=g._rescorers or None)
    paths = g._apply_grammatical_ending(word, paths)
    return paths[0] if paths else None


def slots_of(g, word, width=8):
    return [{"grapheme": s.grapheme, "span": [s.span[0], s.span[1]],
             "candidates": [{"ipa": c.ipa, "cost": c.cost} for c in s.candidates]}
            for s in g.ipa_lattice(word, beam_width=width)]


def pre_rescore_slots(g, word, width=8):
    """The `resolve_branches` output BEFORE the rescorer chain — the state
    g2p.py computes internally and never exposes, rebuilt here exactly the way
    `G2P.ipa_lattice` does it, minus `apply_rescorers`."""
    spec = g.spec
    g_tokens = g._tokenizer.grapheme_tokens(word)
    if not g_tokens:
        return []
    contexts = flat_contexts(g_tokens, spec.vowel_graphemes)
    sylls = (g._syllables_cached(word)
             if spec.stress is not None or g._uses_aperture else [])
    stressed = None
    secondary = frozenset()
    if spec.stress is not None:
        stressed = (RS.detect_stress(word, spec.stress, syllables=sylls)
                    if len(sylls) > 1 else 0)
        if g._is_cliticless(word):
            stressed = _CLITIC_NO_STRESS
        secondary = RS.secondary_stress_positions(len(sylls), stressed, spec.stress)
    from orthography2ipa.g2p import _ApertureView
    syll_for_token = g._map_tokens_to_syllables(g_tokens, sylls)
    aperture = _ApertureView(sylls, spec, enabled=g._uses_aperture)
    allophone_map = spec.allophones if g.expand_allophones else None
    out = []
    for index, ctx in enumerate(contexts):
        branches = resolve_branches(
            spec, ctx, weights_for=g._tokenizer.weights_for,
            allophone_map=allophone_map, syll_idx=syll_for_token[index],
            stressed_syll_idx=stressed, secondary_syll_idxs=secondary,
            syllable=aperture.syllable(syll_for_token[index]),
            syllable_final=aperture.is_final(syll_for_token[index]))
        if not branches:
            continue
        out.append({"grapheme": g_tokens[index].grapheme,
                    "span": [g_tokens[index].position,
                             g_tokens[index].position + g_tokens[index].length],
                    "candidates": [{"ipa": ipa, "cost": cost} for ipa, cost in branches[:width]]})
    return out


def token_rows(tokens):
    return [{"kind": t.kind.name, "grapheme": t.grapheme, "ipa": list(t.ipa),
             "position": t.position, "length": t.length} for t in tokens]


# ─────────────────────────────── surfaces ───────────────────────────────────
def surface_tokens(lang, text):
    return token_rows(PhonetokTokenizer(get(lang)).tokenize(text))


def surface_grapheme_tokens(lang, text):
    return token_rows(PhonetokTokenizer(get(lang)).grapheme_tokens(text))


def surface_beam(lang, text):
    tokenizer = PhonetokTokenizer(get(lang))
    return [{"ipa": p.ipa, "score": p.score, "graphemes": list(p.graphemes),
             "segments": list(p.segments)}
            for p in tokenizer.ipa_beam(text, beam_width=8)]


_LATTICE_CACHE = {}


def surface_lattice(lang, word):
    key = (lang, word, 8)
    if key not in _LATTICE_CACHE:
        _LATTICE_CACHE[key] = slots_of(engine(lang), word)
    return _LATTICE_CACHE[key]


def surface_lattice_pre_rescore(lang, word):
    return pre_rescore_slots(engine(lang), word)


def surface_word(lang, word):
    return engine(lang).transcribe_word(word)


_WC_CACHE = {}


def surface_word_candidates(lang, word):
    key = (lang, word)
    if key not in _WC_CACHE:
        _WC_CACHE[key] = list(engine(lang).word_candidates(word, k=5))
    return _WC_CACHE[key]


def surface_stress(lang, word):
    spec = get(lang)
    sylls = RS._syllables_for(word, lang, spec.stress.diphthongs if spec.stress else (),
                              spec=spec)
    row = {"syllables": list(sylls)}
    if spec.stress is not None:
        index = RS.detect_stress(word, spec.stress, syllables=sylls)
        row["stress"] = index
        row["secondary"] = sorted(RS.secondary_stress_positions(len(sylls), index, spec.stress))
        row["marked"] = RS.apply_stress_mark(word, spec.stress, index, syllables=sylls)
    return row


_RAW = {}


def _raw_reading(lang, word):
    """The pre-stress reading both sides of `stress --ipa` must agree on: the
    concatenation of the lattice's top candidates."""
    key = (lang, word)
    if key not in _RAW:
        _RAW[key] = "".join(slot["candidates"][0]["ipa"]
                            for slot in surface_lattice(lang, word))
    return _RAW[key]


def surface_stress_ipa(lang, word):
    spec = get(lang)
    ipa = _raw_reading(lang, word)
    atoms = list(spec.phonemes or ())
    sylls = RS.syllabify_ipa(ipa, spec.stress.max_onset if spec.stress else 1, atoms)
    row = {"lang": spec.code, "input": ipa, "mode": "ipa", "syllables": sylls,
           "weights": [RS.syllable_weight(s, atoms) for s in sylls]}
    if spec.stress is not None:
        index = RS.detect_stress_by_weight(ipa, spec.stress, atoms)
        row["stress"] = index
        row["secondary"] = sorted(RS.secondary_stress_positions(len(sylls), index, spec.stress))
        row["iambic"] = sorted(RS.iambic_length_positions(sylls, atoms))
        row["marked"] = RS.apply_stress_mark(ipa, spec.stress, index, ipa_syllables=sylls)
    return row


def surface_tone(lang, word):
    g = engine(lang)
    spec = g.spec
    path = engine_path(g, word)
    if path is None:
        graphemes, segments = [word], [word]
    else:
        graphemes, segments = list(path.graphemes), list(path.segments)
    atoms = list(spec.phonemes or ())
    rules = spec.tone_rules
    slots = RT._syllable_slots(segments)
    syllables = []
    for indices in slots:
        syl_g = [graphemes[i] for i in indices if i < len(graphemes)]
        ipa = "".join(segments[i] for i in indices if i < len(segments))
        rime = RT._rime(ipa, atoms, rules.dead_codas) if rules else None
        row = {"indices": list(indices), "graphemes": syl_g, "ipa": ipa,
               "rime": None if rime is None else {"nucleus_end": rime[0],
                                                  "long_vowel": rime[1], "dead": rime[2]}}
        if rules is not None:
            cls, mark = "", rules.no_mark
            for grapheme in syl_g:
                for ch in grapheme:
                    if ch in rules.classes:
                        cls = rules.classes[ch]
                        break
                if cls:
                    break
            for grapheme in syl_g:
                for ch in grapheme:
                    if ch in rules.marks:
                        mark = rules.marks[ch]
            row["class"] = cls
            row["mark"] = mark
            row["shape"] = None if rime is None else (
                "dead_long" if rime[2] and rime[1] else "dead_short" if rime[2] else "live")
            row["tone"] = RT._syllable_tone(syl_g, ipa, rules, atoms)
        else:
            row["class"] = row["mark"] = row["shape"] = row["tone"] = None
        syllables.append(row)
    computed = (RT.assign_computed_tones(graphemes, segments, rules, atoms)
                if rules is not None else "".join(segments))
    out = {"lang": spec.code, "word": word, "atoms": atoms, "graphemes": graphemes,
           "segments": segments, "slots": [list(s) for s in slots],
           "syllables": syllables, "computed": computed,
           "docked": RT.dock_tone_marks(computed, atoms),
           "tone_marks_syllable_final": bool(spec.tone_marks_syllable_final),
           "declares_tone_rules": rules is not None}
    if path is not None:
        out["final"] = g.transcribe_word(word)
    return out


def surface_detailed(lang, text):
    g = engine(lang)
    result = g.transcribe_detailed(text, search="beam", beam_width=8)
    return {"lang": result.lang, "ipa": result.ipa,
            "words": [{"word": w.word, "ipa": w.ipa, "coverage": w.coverage,
                       "confidence": w.confidence, "unmapped": list(w.unmapped),
                       "candidates": [{"ipa": p.ipa, "score": p.score,
                                       "graphemes": list(p.graphemes),
                                       "segments": list(p.segments)} for p in w.candidates]}
                      for w in result.words]}


def surface_sentence(lang, text):
    return engine(lang).transcribe(text)


REFERENCE = {
    "tokens": surface_tokens,
    "grapheme_tokens": surface_grapheme_tokens,
    "beam": surface_beam,
    "lattice": surface_lattice,
    "lattice_pre_rescore": surface_lattice_pre_rescore,
    "word": surface_word,
    "word_candidates": surface_word_candidates,
    "stress": surface_stress,
    "stress_ipa": surface_stress_ipa,
    "tone": surface_tone,
    "detailed": surface_detailed,
    "sentence": surface_sentence,
}


def cpp_surface(surface, lang, text):
    if surface == "tokens":
        return cli("tokens", lang, text)
    if surface == "grapheme_tokens":
        return cli("tokens", lang, text, "--grapheme-only")
    if surface == "beam":
        return cli("beam", lang, text)
    if surface == "lattice":
        return cli("lattice", lang, text)
    if surface == "lattice_pre_rescore":
        return cli("lattice", lang, text, "--pre-rescore")
    if surface == "word":
        return cli("word", lang, text)
    if surface == "word_candidates":
        return cli("word-candidates", lang, text, "-k", "5")["candidates"]
    if surface == "stress":
        row = cli("stress", lang, text)
        return row
    if surface == "stress_ipa":
        return cli("stress", lang, "--ipa", _raw_reading(lang, text))
    if surface == "tone":
        return cli("tone", lang, text)
    if surface == "detailed":
        return cli("transcribe", lang, text, "--beam", "8", "--detailed")
    if surface == "sentence":
        return cli("transcribe", lang, text)
    raise KeyError(surface)


def strip_none(row):
    """The reference emits None where the C++ surface omits the key entirely
    (no tone rules on the spec): drop None-valued keys before comparing."""
    if isinstance(row, dict):
        return {k: strip_none(v) for k, v in row.items() if v is not None}
    if isinstance(row, list):
        return [strip_none(v) for v in row]
    return row


def surfaces_for(text, multi):
    """Which surfaces a case runs. A spaced or punctuated text is a SENTENCE
    case: the per-word surfaces would compare `transcribe_word` against the
    sentence pipeline, which the reference itself does not equate."""
    single = not any(ch.isspace() for ch in text) and "," not in text and ";" not in text
    if multi:
        return ["sentence", "detailed", "tokens", "beam"]
    if single:
        return ALL_SURFACES
    return ["tokens", "grapheme_tokens", "beam", "detailed", "sentence"]


def sentence_cases():
    """Multi-word cases: the confirmed spine, then two-word and comma-separated
    sentences built from every spec that declares sandhi rules."""
    spine = [
        ("fr-FR", "j'aime l'homme"), ("fr-FR", "je l'ai vu"), ("fr-FR", "n'est-ce pas"),
        ("fr-FR", "s'il vous plaît"), ("fr-FR", "les amis"),
        ("en-GB", "don't stop"), ("en-GB", "it's over"), ("en-GB", "the water is a lot"),
        ("ru", "он не пришёл"), ("ru", "он же не знал"),
        ("pt-PT", "as aves"), ("pt-PT", "os dois"), ("pt-PT", "estás a ver"),
        ("pt-PT", "os irmãos"), ("pt-PT", "não me digas"), ("pt-PT", "ele deu-me o livro"),
        ("pt-PT", "os jogos"), ("pt-PT", "três jovens"),
        ("pt-PT-x-lisbon", "os jogos"), ("pt-MO", "os jogos"), ("mzs", "os jogos"),
        ("ext-PT-x-barranco", "dez jovens"),
        ("es-ES", "un buen baile"), ("es-ES", "en casa"), ("es-ES", "sin par"),
        ("es-MX", "un coche"), ("es-CU", "con son"), ("es-CU", "canción vieja"),
        ("es-VE", "con música"), ("ext", "cantan pola"),
        ("ca", "els nens i les nenes"), ("ca", "la porta és"), ("ca", "el'"),
        ("it-IT", "l'amico"), ("it-IT", "un'amica"),
        ("ar", "في البيت"), ("ar", "مِنَ الكتابِ"), ("ar-LB", "بُكْرَا الْيَوم"),
        ("af", "'n man is 'n mens"), ("af", "hy's hier hier."),
        ("ga", "a d'thuisiú"), ("hi", "राम की"), ("th", "ผม ไม่ ค่อย สบาย"),
    ]
    cases = list(spine)
    for code in available_codes():
        try:
            spec = get(code)
        except Exception:
            continue
        if not spec.sandhi_rules:
            continue
        words = [w for w in (spec.word_exceptions or {}) if " " not in w.strip()]
        if len(words) >= 2:
            cases.append((code, f"{words[0]} {words[1]}"))
            cases.append((code, f"{words[0]}, {words[1]}"))
    return cases


def catalog_sweep():
    out = []
    for code in available_codes():
        try:
            spec = get(code)
        except Exception:
            continue
        for word in (spec.word_exceptions or {}):
            if word.strip():
                out.append((code, word))
    return out


def main():
    global CLI, TOL, ERRORS
    parser = argparse.ArgumentParser()
    parser.add_argument("cli")
    parser.add_argument("--battery", default="required",
                        choices=["required", "categories", "sentence", "all"])
    parser.add_argument("--surface", default="all")
    parser.add_argument("--sweep", action="store_true")
    parser.add_argument("--tolerance", type=float, default=1e-12)
    parser.add_argument("--report")
    parser.add_argument("--allow-errors", action="store_true")
    parser.add_argument("--category", action="append", default=[],
                        help="limit to these battery categories (repeatable)")
    parser.add_argument("--limit", type=int, default=0, help="cap the case count")
    parser.add_argument("-q", "--quiet", action="store_true")
    args = parser.parse_args()
    CLI = pathlib.Path(os.path.abspath(args.cli))
    TOL = args.tolerance

    for probe, argv in [("lattice", ["lattice", "pt-PT", "chuva"]),
                        ("tone", ["tone", "th", "ก"]),
                        ("word-candidates", ["word-candidates", "sv", "sysko", "-k", "2"]),
                        ("resolve", ["resolve", "pt"]),
                        ("stress --ipa", ["stress", "ar", "--ipa", "ktaːb"]),
                        ("tokens --grapheme-only", ["tokens", "pt-PT", "chuva", "--grapheme-only"])]:
        result = subprocess.run([str(CLI)] + argv, capture_output=True, text=True)
        if result.returncode != 0:
            print(f"MISSING SURFACE: {probe} (rc={result.returncode}) — "
                  f"{result.stderr.strip()[:160]}")
            return 1

    wanted = SURFACES if args.surface == "all" else [args.surface]
    battery = BATTERY.build()
    cases = []
    if args.battery in ("required", "all"):
        cases += [("required", lang, text) for lang, text in battery["six"]]
    if args.battery in ("categories", "all"):
        for tag, pairs in battery.items():
            if tag == "six":
                continue
            cases += [(tag, lang, text) for lang, text in pairs]
    if args.battery in ("sentence", "all"):
        cases += [("sentence", lang, text) for lang, text in sentence_cases()]
    if args.sweep:
        cases += [("sweep", lang, text) for lang, text in catalog_sweep()]
    if args.category:
        cases = [c for c in cases if c[0] in args.category]
    if args.limit:
        cases = cases[:args.limit]

    totals, failures, errors = {}, [], []
    processed = 0
    for tag, lang, text in cases:
        processed += 1
        if not args.quiet and processed % 200 == 0:
            print(f"... {processed}/{len(cases)} cases", file=sys.stderr, flush=True)
        multi = tag == "sentence"
        for surface in surfaces_for(text, multi):
            if surface not in wanted:
                continue
            totals.setdefault(surface, [0, 0])
            try:
                want = REFERENCE[surface](lang, text)
            except Exception as error:
                errors.append((surface, tag, lang, text, repr(error)[:200]))
                continue
            try:
                got = cpp_surface(surface, lang, text)
            except Exception as error:
                failures.append((surface, tag, lang, text, "<cli failed>", repr(error)[:200]))
                totals[surface][1] += 1
                continue
            ok = eq(strip_none(want), strip_none(got))
            totals[surface][0] += 1
            if not ok:
                failures.append((surface, tag, lang, text,
                                 json.dumps(want, ensure_ascii=False)[:400],
                                 json.dumps(got, ensure_ascii=False)[:400]))
                totals[surface][1] += 1

    if not args.quiet:
        for surface in sorted(totals):
            passed, failed = totals[surface]
            flag = "OK " if failed == 0 else "FAIL"
            print(f"{flag} {surface:22s} {passed - failed}/{passed}")
    if failures:
        print(f"\n{len(failures)} divergences (first 25):")
        for surface, tag, lang, text, want, got in failures[:25]:
            print(f"  [{surface}] {tag} {lang} {text!r}\n     ref {want}\n     cpp {got}")
    if errors:
        label = "reference errors" if args.allow_errors else "ERRORS (fail the gate)"
        print(f"\n{len(errors)} {label} (first 10):")
        for row in errors[:10]:
            print(f"  [{row[0]}] {row[1]} {row[2]} {row[3]!r}: {row[4]}")
    if args.report:
        pathlib.Path(args.report).write_text(json.dumps(
            {"totals": {k: v for k, v in totals.items()},
             "failures": [[s, t, l, x, w, g] for s, t, l, x, w, g in failures],
             "errors": errors}, ensure_ascii=False, indent=1), encoding="utf-8")
    bad = bool(failures) or (bool(errors) and not args.allow_errors)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
