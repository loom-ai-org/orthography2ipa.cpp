#!/usr/bin/env python3
"""Compare the reference and C++ dumps of the lm.py / feats.py parity harness.

tests/lm_feats_ref.py writes tests/_scratch/_lm_feats_ref_*; the C++ check
program writes tests/_scratch/_lm_feats_cpp_*. Each pair must be byte-identical.
For each pair that differs, this script prints the first differing rows, with
the phone or case they belong to. Exit 0 only when every pair is identical.
"""
import pathlib
import struct
import sys

SCRATCH = pathlib.Path(__file__).resolve().parent / "_scratch"
TEXT_DUMPS = ["vec", "vowel", "embed", "lm"]
SHOWN = 20


def read(name: str) -> bytes:
    path = SCRATCH / name
    if not path.is_file():
        sys.exit(f"missing dump: {path}")
    return path.read_bytes()


def main() -> int:
    battery = read("_lm_feats_battery.txt").decode("utf-8").split("\n")
    phones = battery[1:-1]
    if not phones or battery[0] != str(len(phones)):
        print(f"battery: the header says {battery[0]!r}, and {len(phones)} phones follow")
        return 1
    failures = 0

    for dump in TEXT_DUMPS:
        ref = read(f"_lm_feats_ref_{dump}.txt").decode("utf-8").split("\n")
        cpp = read(f"_lm_feats_cpp_{dump}.txt").decode("utf-8").split("\n")
        differ = [i for i in range(max(len(ref), len(cpp)))
                  if i >= len(ref) or i >= len(cpp) or ref[i] != cpp[i]]
        print(f"{dump}: {len(ref)} reference lines, {len(cpp)} C++ lines, {len(differ)} differ")
        if dump in ("vec", "vowel") and not len(ref) == len(cpp) == len(phones) + 1:
            print(f"  {dump}: {len(phones)} phones, but {len(ref) - 1} reference and"
                  f" {len(cpp) - 1} C++ lines")
            failures += 1
        for i in differ[:SHOWN]:
            label = f" phone {phones[i]!r}" if dump in ("vec", "vowel") and i < len(phones) else ""
            print(f"  line {i + 1}{label}: reference {ref[i] if i < len(ref) else None!r}"
                  f" vs C++ {cpp[i] if i < len(cpp) else None!r}")
        failures += len(differ)

    ref = read("_lm_feats_ref_fdist.bin")
    cpp = read("_lm_feats_cpp_fdist.bin")
    n = len(phones)
    if len(ref) != n * n * 8 or len(cpp) != n * n * 8:
        print(f"fdist: expected {n * n * 8} bytes, reference {len(ref)}, C++ {len(cpp)}")
        return 1
    differ = 0
    if ref != cpp:
        for index in range(n * n):
            a, b = ref[index * 8:index * 8 + 8], cpp[index * 8:index * 8 + 8]
            if a != b:
                if differ < SHOWN:
                    print(f"  phonetic_distance({phones[index // n]!r}, {phones[index % n]!r}):"
                          f" reference {struct.unpack('=d', a)[0]!r}"
                          f" vs C++ {struct.unpack('=d', b)[0]!r}")
                differ += 1
    print(f"fdist: {n * n} pairs, {differ} differ")
    failures += differ
    print("IDENTICAL" if failures == 0 else f"FAIL: {failures} differing values")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
