#!/usr/bin/env python3
"""
Regenerates src/langcodes_tables.inc from the installed third-party `langcodes`
package that orthography2ipa/registry.py depends on: the grandfathered-tag set,
the language/territory replacement tables, CLDR's likely-subtags and
default-script tables, the macrolanguage normalization table, the language
distance matrix and the territory clusters the distance algorithm hard-codes.

Run it with the interpreter that has langcodes installed, e.g.

    /home/flavio/.venvs/piper/bin/python3 tests/gen_langcodes_tables.py

and rebuild. The generated file is committed on purpose: it is data transcribed
from the library the reference calls, not compiled output, so building the port
never needs Python and never needs `langcodes` itself. Keeping it generated
rather than retyped is what guarantees the port's tables are byte-identical to
the ones the reference resolves against.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
CPP_ROOT = os.path.dirname(HERE)
OUT = os.path.join(CPP_ROOT, "src", "langcodes_tables.inc")

try:
    import langcodes  # noqa: F401
    from langcodes import tag_parser as TP
    from langcodes import language_distance as LD
    from langcodes.data_dicts import (
        DEFAULT_SCRIPTS,
        LANGUAGE_DISTANCES,
        LANGUAGE_REPLACEMENTS,
        LIKELY_SUBTAGS,
        NORMALIZED_MACROLANGUAGES,
        TERRITORY_REPLACEMENTS,
    )
except ImportError as exc:  # pragma: no cover
    raise SystemExit(
        "this generator needs the langcodes package the reference uses;\n"
        "run it with /home/flavio/.venvs/piper/bin/python3 (%s)" % exc)


def esc(s):
    """A C++ string literal for an ASCII tag. Non-ASCII or a control character
    in the source data is a generator error, not something to paper over."""
    if not s.isascii():
        raise SystemExit("non-ASCII subtag in langcodes data: %r" % (s,))
    for ch in s:
        if ord(ch) < 0x20 or ord(ch) == 0x7f:
            raise SystemExit("control character in langcodes data: %r" % (s,))
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def kv_map(name, source, comment):
    """A sorted {key: value} table. Python dict lookup is by exact equality, so
    the order the entries are stored in carries no meaning."""
    pairs = sorted(source.items())
    for key, value in pairs:
        if not key or not value:
            raise SystemExit("%s has an empty key or value: %r" % (name, (key, value)))
    lines = ["// " + line for line in comment]
    lines.append("const Kv %s[] = {" % name)
    for key, value in pairs:
        lines.append("    {%s, %s}," % (esc(key), esc(value)))
    lines.append("};")
    lines.append("")
    return lines


def string_set(name, values, comment):
    lines = ["// %s" % comment,
             "const char* const %s[] = {" % name]
    for value in sorted(values):
        lines.append("    %s," % esc(value))
    lines.append("};")
    lines.append("")
    return lines


L = []
L.append("// Generated from the installed langcodes package by tests/gen_langcodes_tables.py.")
L.append("// Provenance: langcodes %s at %s" % (
    getattr(langcodes, "__version__", "unknown"),
    os.path.dirname(os.path.abspath(langcodes.__file__))))
L.append("// Every table below is a transcription of the data the reference resolves")
L.append("// language tags against; none of it is hand-typed.")
L.append("")
L.append("// tag_parser.py EXCEPTIONS: tags that bypass the ordinary parser because")
L.append("// they are grandfathered from RFC 3066 (lowercased, as the reference stores")
L.append("// them, so the comparison is case-insensitive).")
L.append("const char* const GRANDFATHERED[] = {")
for value in sorted(TP.EXCEPTIONS):
    L.append("    %s," % esc(value))
L.append("};")
L.append("")

L += kv_map("LANGUAGE_REPLACEMENTS", LANGUAGE_REPLACEMENTS, [
    "__init__.py Language.get / LANGUAGE_REPLACEMENTS: deprecated and malformed",
    "tags mapped to the tag they should be read as. Keys are already",
    "normalize_characters() form (lowercase, hyphens)."])
L += kv_map("TERRITORY_REPLACEMENTS", TERRITORY_REPLACEMENTS, [
    "__init__.py Language.get / TERRITORY_REPLACEMENTS: deprecated and numeric",
    "territory codes mapped to their current code. Keys are lowercase because the",
    "reference lowercases the subtag before looking it up."])
L += kv_map("DEFAULT_SCRIPTS", DEFAULT_SCRIPTS, [
    "data_dicts.py DEFAULT_SCRIPTS: the script a language is normally written in,",
    "used by simplify_script() and assume_script()."])
L += kv_map("NORMALIZED_MACROLANGUAGES", NORMALIZED_MACROLANGUAGES, [
    "data_dicts.py NORMALIZED_MACROLANGUAGES: the dominant individual language of",
    "each macrolanguage, used by prefer_macrolanguage()."])
L += kv_map("LIKELY_SUBTAGS", LIKELY_SUBTAGS, [
    "data_dicts.py LIKELY_SUBTAGS (CLDR likelySubtags): the fill-in values that",
    "maximize() reads along its broader-tag chain."])

distances = []
for desired, by_supported in LANGUAGE_DISTANCES.items():
    for supported, value in by_supported.items():
        distances.append((desired, supported, value))
distances.sort()
L.append("// language_distance.py LANGUAGE_DISTANCES: the (desired, supported) ->")
L.append("// distance matrix, flattened. '*'/'*_*' rows are the defaults the reference")
L.append("// reads DEFAULT_LANGUAGE_DISTANCE and DEFAULT_SCRIPT_DISTANCE from.")
L.append("const Dist LANGUAGE_DISTANCES[] = {")
for desired, supported, value in distances:
    L.append("    {%s, %s, %d}," % (esc(desired), esc(supported), int(value)))
L.append("};")
L.append("")

L.append("// language_distance.py: the territory clusters its matching rules use.")
L += string_set("MAGHREB", LD.MAGHREB, "MAGHREB: the western Arab world.")
L += string_set("US_TERRITORIES", LD.US, "US: the United States and its territories.")
L += string_set("CNSAR", LD.CNSAR, "CNSAR: the Special Autonomous Regions of China.")
L += string_set("LATIN_AMERICA", LD.LATIN_AMERICA, "LATIN_AMERICA: Central and South America.")
L += string_set("AMERICAS", LD.AMERICAS, "AMERICAS: North and South America, including LATIN_AMERICA.")
L.append("// language_distance.py DEFAULT_TERRITORY_DISTANCE.")
L.append("const int DEFAULT_TERRITORY_DISTANCE = %d;" % int(LD.DEFAULT_TERRITORY_DISTANCE))
L.append("")

os.makedirs(os.path.dirname(OUT), exist_ok=True)
with open(OUT, "w", encoding="utf-8") as handle:
    handle.write("\n".join(L) + "\n")
print("wrote %s: %d lines" % (OUT, len(L)))
print("tables: GRANDFATHERED %d, LANGUAGE_REPLACEMENTS %d, TERRITORY_REPLACEMENTS %d,"
      " DEFAULT_SCRIPTS %d, NORMALIZED_MACROLANGUAGES %d, LIKELY_SUBTAGS %d,"
      " LANGUAGE_DISTANCES %d pairs (%d outer keys), territory clusters %d/%d/%d/%d/%d"
      % (len(TP.EXCEPTIONS), len(LANGUAGE_REPLACEMENTS), len(TERRITORY_REPLACEMENTS),
         len(DEFAULT_SCRIPTS), len(NORMALIZED_MACROLANGUAGES), len(LIKELY_SUBTAGS),
         len(distances), len(LANGUAGE_DISTANCES),
         len(LD.MAGHREB), len(LD.US), len(LD.CNSAR), len(LD.LATIN_AMERICA), len(LD.AMERICAS)))
