"""
Regenerates src/script_registry.inc from the reference package's
orthography2ipa/script_distance.py: the SCRIPT_REGISTRY feature bundles and the
script-type distance matrix. Run it (with the interpreter that can import the
reference, e.g. /home/flavio/.venvs/piper/bin/python3) after the reference
changes a script, then rebuild:

    python3 tests/gen_script_registry.py [path-to-reference-repo]

The generated file is committed on purpose: it is data transcribed from the
reference, not compiled output, so a build never needs Python. Keeping it
generated rather than retyped is what guarantees the port's script names,
feature values, ancestries and matrix numbers are the reference's own.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
CPP_ROOT = os.path.dirname(HERE)
reference = sys.argv[1] if len(sys.argv) > 1 else (os.environ.get("O2I_PYTHON_ROOT") or os.path.join(os.path.dirname(CPP_ROOT), "orthography2ipa"))
if not os.path.isfile(os.path.join(reference, "orthography2ipa", "script_distance.py")):
    raise SystemExit("reference package not found at " + repr(reference))
sys.path.insert(0, reference)
import logging; logging.disable(logging.CRITICAL)
import importlib
# The package __init__ re-exports the FUNCTION script_distance, which
# shadows the module attribute, so the module is taken from sys.modules by
# importlib rather than by attribute lookup.
SD = importlib.import_module("orthography2ipa.script_distance")
from orthography2ipa.types import ScriptType

BS = chr(92)
NL = chr(10)


def esc(s):
    """A C++ string literal carrying exactly the reference's code points."""
    out = []
    for ch in s:
        o = ord(ch)
        if ch == '"': out.append(BS + '"')
        elif ch == BS: out.append(BS + BS)
        elif 0x20 <= o <= 0x7e: out.append(ch)
        elif o <= 0xffff: out.append(BS + "u%04x" % o)
        else: out.append(BS + "U%08x" % o)
    return '"' + "".join(out) + '"'


L = []
L.append("// Generated from the reference orthography2ipa/script_distance.py by")
L.append("// tests/gen_script_registry.py: the SCRIPT_REGISTRY feature bundles and the")
L.append("// script-type distance matrix, transcribed from the reference rather than")
L.append("// retyped. Do not edit by hand; regenerate.")
L.append("")
L.append("// script_distance.py SCRIPT_REGISTRY, in the reference's own declaration")
L.append("// order. script_type carries the ScriptType VALUE (types.py: alphabet,")
L.append("// abjad, ...), the same spelling LanguageSpec.script_type keeps.")
L.append("const std::vector<ScriptFeatures>& registry_table() {")
L.append("    static const std::vector<ScriptFeatures> table = {")
for name, feat in SD.SCRIPT_REGISTRY.items():
    L.append("        // %s: ScriptType.%s, %s, has_case=%s, vowel_marking=%s, block %s"
             % (esc(name), feat.script_type.name, feat.directionality,
                str(feat.has_case).lower(), feat.vowel_marking, feat.unicode_block))
    ancestors = ", ".join(esc(a) for a in feat.ancestor_scripts)
    L.append("        ScriptFeatures{%s, %s, %s, %s, %s, %s, {%s}}," % (
        esc(name), esc(feat.script_type.value), esc(feat.directionality),
        "true" if feat.has_case else "false", esc(feat.vowel_marking),
        esc(feat.unicode_block), ancestors))
L.append("    };")
L.append("    return table;")
L.append("}")
L.append("")
L.append("// script_distance.py _init_type_distances(): the `raw` table of base")
L.append("// distances. The reference fills BOTH halves of every pair plus a 0.0")
L.append("// diagonal; one entry per unordered pair is enough, and the C++")
L.append("// initialiser mirrors _init_type_distances in filling the rest.")
L.append("const std::vector<TypeDistance>& type_distance_table() {")
L.append("    static const std::vector<TypeDistance> table = {")
rank = {t: i for i, t in enumerate(list(ScriptType))}
seen = set()
for (a, b), d in SD._SCRIPT_TYPE_DISTANCE.items():
    if a == b:
        continue
    key = (a, b) if rank[a] < rank[b] else (b, a)
    if key in seen:
        continue
    seen.add(key)
    if SD._SCRIPT_TYPE_DISTANCE[(b, a)] != d:
        raise SystemExit("the reference matrix is not symmetric at " + repr(key))
    L.append("    {%s, %s, %s},  // ScriptType.%s x ScriptType.%s"
             % (esc(key[0].value), esc(key[1].value), repr(float(d)),
                key[0].name, key[1].name))
L.append("    };")
L.append("    return table;")
L.append("}")
L.append("")

missing = [t for t in rank if (t, t) not in SD._SCRIPT_TYPE_DISTANCE]
if missing:
    raise SystemExit("diagonal not filled by the reference for " + repr(missing))
expected = len(rank) * (len(rank) - 1) // 2
if len(seen) != expected:
    raise SystemExit("expected %d unordered type pairs, generated %d" % (expected, len(seen)))

out = os.path.join(CPP_ROOT, "src", "script_registry.inc")
open(out, "w").write(NL.join(L) + NL)
print("wrote", out, len(L), "lines;", "scripts", len(SD.SCRIPT_REGISTRY), "type pairs", len(seen))
