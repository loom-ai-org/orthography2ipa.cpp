"""
Regenerates src/transform_tables.inc from the reference package's
orthography2ipa/transforms.py: the DIALECT_PROFILES rule tables, the character
sets and the regex sources the context predicates compile. Run it (with the
interpreter that can import the reference, e.g.
/home/flavio/.venvs/piper/bin/python3) after the reference changes a profile,
then rebuild:

    python3 tests/gen_transform_tables.py [path-to-reference-repo]

The generated file is committed on purpose: it is data transcribed from the
reference, not compiled output, so a build never needs Python. Keeping it
generated rather than retyped is what guarantees the port's rules, ids,
ordering and context names are the reference's own.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
CPP_ROOT = os.path.dirname(HERE)
reference = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(CPP_ROOT), "orthography2ipa")
if not os.path.isfile(os.path.join(reference, "orthography2ipa", "transforms.py")):
    raise SystemExit(f"reference package not found at {reference!r}")
sys.path.insert(0, reference)
import logging; logging.disable(logging.CRITICAL)
from orthography2ipa import transforms as T

def esc(s):
    out = []
    for ch in s:
        o = ord(ch)
        if ch == '"': out.append('\\"')
        elif ch == '\\\\': out.append('\\\\\\\\')
        elif 0x20 <= o <= 0x7e: out.append(ch)
        elif o <= 0xffff: out.append('\\u%04x' % o)
        else: out.append('\\U%08x' % o)
    return '"' + ''.join(out) + '"'

def cps(s):
    return ", ".join("0x%x" % ord(c) for c in s) + ", 0"

# The regex sources, taken from the reference's own text so the character
# classes carry exactly the code points it uses.
src = open(os.path.join(reference, "orthography2ipa", "transforms.py")).read()
def literal(pat):
    m = re.search(re.escape(pat), src)
    if not m: raise SystemExit("pattern not found: " + pat)
    return pat
DB1 = r"(?<=[^aeɛɐiɨoɔuɑæøœyãẽĩõũ\s])ɐj"
DB2 = r"o(?=[tdszʃnlɾ])"
RC  = r"[ɾr][bcdfɡhjklmnpqrstvwxzʃʒŋɲʎ]"
NC  = r"[mn][bcdfɡhjklmnpqrstvwxzʃʒŋɲʎ]"
CE  = r"c[ei]"
IVS = r"[aeiouáéíóúâêôãõ]s[aeiouáéíóúâêôãõ]"
for p in (DB1, DB2, RC, NC, CE, IVS):
    literal(p)

L = []
L.append("// Generated from the reference orthography2ipa/transforms.py by "
         "tests/_scratch/gen_tables.py:")
L.append("// the profile rule tables, the character sets and the regex sources of the")
L.append("// context predicates, transcribed from the reference rather than retyped.")
L.append("")
L.append("namespace {")
L.append("")
L.append("// transforms.py VOWEL_SET / PALATAL_SET / STRESS.")
L.append("const char32_t VOWEL_SET[] = {%s};" % cps("".join(sorted(T.VOWEL_SET))))
L.append("const char32_t PALATAL_SET[] = {%s};" % cps("".join(sorted(T.PALATAL_SET))))
L.append("const char32_t STRESS_MARK = 0x%x;" % ord(T.STRESS))
L.append("")
L.append("// The regex sources the predicates and the de-biasing steps compile.")
L.append("const char DB1_RE[] = %s;" % esc(DB1))
L.append("const char DB2_RE[] = %s;" % esc(DB2))
L.append("const char BEFORE_R_C[] = %s;" % esc(RC))
L.append("const char BEFORE_NASAL_C[] = %s;" % esc(NC))
L.append("const char ORTHO_C_EI[] = %s;" % esc(CE))
L.append("const char ORTHO_INTERVOCALIC_S[] = %s;" % esc(IVS))
L.append("")
L.append("const std::vector<Profile>& profiles_table() {")
L.append("    static const std::vector<Profile> table = {")
for code, dt in T.DIALECT_PROFILES.items():
    L.append("        // %s: %s (%s)" % (code, dt.name, dt.cintra_zone))
    L.append("        Profile{%s, %s, %s, %s, {" % (esc(code), esc(dt.name), esc(dt.cintra_zone),
                                                    "true" if dt.requires_debiasing else "false"))
    for r in dt.rules:
        if isinstance(r, T.IPARule):
            ctx = "nullptr" if r.context is None else esc(r.context)
            L.append("            Rule::plain(%s, %s, %s, %s, %s, %s)," % (
                esc(r.id), esc(r.name), esc(r.find), esc(r.replace), ctx,
                "true" if r.requires_ortho else "false"))
        elif isinstance(r, T.IPAChainShift):
            items = ", ".join("{%s, %s}" % (esc(k), esc(v)) for k, v in r.mapping.items())
            ctx = "nullptr" if r.context is None else esc(r.context)
            L.append("            Rule::chain(%s, %s, Mapping{%s}, %s)," % (
                esc(r.id), esc(r.name), items, ctx))
        elif isinstance(r, T.IPALexicalRule):
            L.append("            Rule::lexical(%s, %s, %s, %s)," % (
                esc(r.id), esc(r.word), esc(r.find), esc(r.replace)))
        else:
            raise SystemExit("unknown rule type " + type(r).__name__)
    L.append("        }},")
L.append("    };")
L.append("    return table;")
L.append("}")
L.append("")
L.append("} // namespace")
out = os.path.join(CPP_ROOT, "src", "transform_tables.inc")
open(out, 'w').write("\n".join(L) + "\n")
print('wrote', out, len(L), 'lines')
print('generated', len(L), 'lines; profiles', len(T.DIALECT_PROFILES))
