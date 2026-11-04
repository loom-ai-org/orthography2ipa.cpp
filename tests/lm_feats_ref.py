#!/usr/bin/env python3
"""Reference side of the lm.py / feats.py parity harness (tests/lm_parity.py).

It writes the battery and the word-list cases that the C++ check program
(tests/check_lm_feats_driver.cpp) reads, and the reference values for them, to
tests/_scratch/:

  _lm_feats_battery.txt   the phone battery: the count, then one phone per line
  _lm_feats_cases.txt     the build_ngram_lm / perplexity cases
  _lm_feats_ref_vec.txt   feats.vectorize_phones per phone ("E" for ValueError)
  _lm_feats_ref_vowel.txt feats.is_vowel_phone per phone (T, F, or N for None)
  _lm_feats_ref_fdist.bin feats.phonetic_distance over every ordered pair, as
                          native float64
  _lm_feats_ref_embed.txt lm.phoneme_embeddings for the driver's 18 specs
  _lm_feats_ref_lm.txt    lm.build_ngram_lm and lm.perplexity per case

Every file has the same format as the driver's _lm_feats_cpp_* file, so
tests/lm_feats_compare.py compares them byte for byte.

The battery is the IPA readings of a seeded 300-spec catalog sample, every key
of the feature and modifier tables, each base phone with each modifier, two
modifiers on a few bases, the NFD forms of the table keys, and phones the
tables do not know. The cases are 6 word lists for each of 11 specs at
n = 1 to 4: words drawn from the spec's own graphemes, words with characters
the spec does not map, and empty lists.

The reference package is imported from $O2I_PYTHON_ROOT, else from the sibling
directory of this repository. The interpreter must have the reference's
requirements (numpy, linguatag).
"""
import logging
import os
import pathlib
import random
import struct
import sys
import unicodedata

HERE = pathlib.Path(__file__).resolve().parent
CPP_ROOT = HERE.parent
SCRATCH = HERE / "_scratch"

reference = os.environ.get("O2I_PYTHON_ROOT") or str(CPP_ROOT.parent / "orthography2ipa")
if not (pathlib.Path(reference) / "orthography2ipa" / "lm.py").is_file():
    sys.exit(f"reference package not found at {reference!r} (set O2I_PYTHON_ROOT)")
sys.path.insert(0, reference)
logging.disable(logging.CRITICAL)

from orthography2ipa import feats, lm  # noqa: E402
from orthography2ipa.json_loader import available_json_codes  # noqa: E402
from orthography2ipa.phonetok import PhonetokTokenizer  # noqa: E402
from orthography2ipa.registry import get  # noqa: E402

SEED = 20261103
SPEC_SAMPLE = 300
# The driver's kEmbLangs, in its order.
EMBED_LANGS = ["en-US", "fr-FR", "pt-PT", "de-DE", "hi", "ar", "th", "my", "ja", "ru", "tr",
               "ta", "bn", "kw", "dz", "ags", "am", "bbl"]
LM_LANGS = ["pt-PT", "es-ES", "en-GB", "fr-FR", "de-DE", "ru", "ar", "hi", "th", "kw", "ja"]
UNKNOWN_PHONES = ["", "Q", "1", "€", "ᵐ", "aQ", "Qa", "tQʼ", "́", "ː", "ːː", " ", "  "]
UNMAPPED_WORDS = ["", "123", "@#", "zz9", "☃", "a1b2"]
# The driver writes a context as its phones joined by this separator.
CONTEXT_SEPARATOR = "\x1f"


def number(value: float) -> str:
    return "%.17g" % value


def usable(text: str) -> bool:
    """A line of the battery or case file: no line break, tab or separator."""
    return not any(ch in text for ch in "\n\r\t\x1f\x0b\x0c\x1c\x1d\x1e\x85  ")


def build_battery() -> list:
    phones = {}
    codes = sorted(available_json_codes())
    for code in random.Random(SEED).sample(codes, SPEC_SAMPLE):
        for readings in get(code).graphemes.values():
            for reading in readings:
                phones[reading] = None
    for key in list(feats.phone_features) + list(feats.modifiers) + list(feats._bad_phones):
        phones[key] = None
    bases = [key for key in feats.phone_features if len(key) == 1]
    for base in bases:
        for modifier in feats.modifiers:
            phones[base + modifier] = None
    for base in ["a", "i", "u", "t", "k", "n", "s", "ɾ"]:
        for first in feats.modifiers:
            for second in feats.modifiers:
                phones[base + first + second] = None
    for key in list(feats.phone_features) + list(feats.modifiers):
        phones[unicodedata.normalize("NFD", key)] = None
    for phone in UNKNOWN_PHONES:
        phones[phone] = None
    return [phone for phone in phones if usable(phone)]


def words_for(spec, rng: random.Random, count: int) -> list:
    letters = [key for key in spec.graphemes if usable(key) and key.strip() == key and key]
    words = []
    for _ in range(count):
        words.append("".join(rng.choice(letters) for _ in range(rng.randint(1, 6))))
    return words


def build_cases() -> list:
    rng = random.Random(SEED)
    cases = []
    for lang in LM_LANGS:
        spec = get(lang)
        for n in (1, 2, 3, 4):
            train = words_for(spec, rng, 30)
            test = words_for(spec, rng, 12)
            lists = [
                (train, test),
                (train, test[:6] + UNMAPPED_WORDS),
                ([], test),
                (train, []),
                (UNMAPPED_WORDS + train[:10], test),
                (train[:1], train[:1]),
            ]
            for words, held_out in lists:
                cases.append((lang, n, words, held_out))
    return cases


def write_lines(name: str, lines) -> None:
    with open(SCRATCH / name, "w", encoding="utf-8", newline="\n") as out:
        for line in lines:
            out.write(line + "\n")


def vector_line(phone: str) -> str:
    try:
        vec = feats.vectorize_phones(phone)
    except ValueError:
        return "E"
    return " ".join("-1" if v is None else "1" if v else "0" for v in vec)


def vowel_line(phone: str) -> str:
    flag = feats.is_vowel_phone(phone)
    return "N" if flag is None else "T" if flag else "F"


def embed_lines():
    for lang in EMBED_LANGS:
        embeddings = lm.phoneme_embeddings(get(lang))
        yield f"EMB {lang} {len(embeddings)}"
        for phone in sorted(embeddings):
            yield phone + "\t" + "\t".join(number(float(v)) for v in embeddings[phone])


def lm_lines(cases):
    for lang, n, words, held_out in cases:
        spec = get(lang)
        table = lm.build_ngram_lm(words, spec, n)
        ppl = lm.perplexity(table, held_out, spec, n)
        seen = sum(sum(counter.values()) for counter in table.values())
        yield f"CASE\t{lang}/{n}\t{len(table)}\t{seen}\t{number(ppl)}"
        for context, counter in sorted(table.items(), key=lambda item: CONTEXT_SEPARATOR.join(item[0])):
            yield "C\t" + CONTEXT_SEPARATOR.join(context)
            yield f"K\t{len(counter)}"
            for phone, count in sorted(counter.items()):
                yield f"S\t{phone}\t{count}"
        yield f"P\t{len(held_out)}"
        tokenizer = PhonetokTokenizer(spec)
        for index, word in enumerate(held_out):
            paths = tokenizer.ipa_beam(word, beam_width=1)
            yield f"W\t{index}\t{paths[0].ipa if paths else '-'}"
        yield "END"


def main() -> None:
    SCRATCH.mkdir(exist_ok=True)
    phones = build_battery()
    write_lines("_lm_feats_battery.txt", [str(len(phones))] + phones)
    cases = build_cases()
    case_lines = []
    for lang, n, words, held_out in cases:
        case_lines += ["CASE", f"lang\t{lang}", f"n\t{n}", f"train\t{len(words)}", *words,
                       f"test\t{len(held_out)}", *held_out, "END"]
    write_lines("_lm_feats_cases.txt", case_lines)

    write_lines("_lm_feats_ref_vec.txt", (vector_line(phone) for phone in phones))
    write_lines("_lm_feats_ref_vowel.txt", (vowel_line(phone) for phone in phones))
    with open(SCRATCH / "_lm_feats_ref_fdist.bin", "wb") as out:
        for phone_a in phones:
            row = [feats.phonetic_distance(phone_a, phone_b) for phone_b in phones]
            out.write(struct.pack(f"={len(row)}d", *row))
    write_lines("_lm_feats_ref_embed.txt", embed_lines())
    write_lines("_lm_feats_ref_lm.txt", lm_lines(cases))
    print(f"battery phones: {len(phones)}, pairs: {len(phones) ** 2}, "
          f"embedding specs: {len(EMBED_LANGS)}, lm cases: {len(cases)}")


if __name__ == "__main__":
    main()
