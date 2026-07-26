"""Physics-notation coverage probe against the pinned nMarkdown math engine.

Checks each command against two independent surfaces:
  1. the no-argument symbol catalog  (src/math/math_symbol_table.inc)
  2. structural commands handled in  (src/math/math_parser.cpp)
Reports which physics domain each gap blocks.
"""
import re
import sys

ROOT = sys.argv[1]
TBL = ROOT + "/src/math/math_symbol_table.inc"
PARSER = ROOT + "/src/math/math_parser.cpp"

sym = set(re.findall(r'^\s*\{"([A-Za-z*]+)"', open(TBL, encoding="utf-8").read(),
                     re.M))
psrc = open(PARSER, encoding="utf-8").read()
parser_cmds = set(re.findall(r'"([A-Za-z*]{2,24})"', psrc))

# domain -> [(command, what it is used for)]
PROBE = {
    "Tensor calculus / GR (Phase 2-3)": [
        ("partial", "partial derivative"),
        ("nabla", "covariant derivative / gradient"),
        ("Gamma", "Christoffel symbol"),
        ("otimes", "tensor product"),
        ("wedge", "exterior product"),
        ("star", "Hodge dual"),
        ("mathcal", "script curvature symbols"),
        ("tensor", "staggered index placement (tensor pkg)"),
        ("prescript", "left/pre-indices"),
        ("indices", "tensor pkg alias"),
        ("dd", "upright differential (physics pkg)"),
        ("mathrm", "upright d in line element"),
    ],
    "Quantum mechanics (Phase 4)": [
        ("dagger", "Hermitian conjugate"),
        ("langle", "bra"),
        ("rangle", "ket"),
        ("hbar", "reduced Planck constant"),
        ("ket", "physics pkg ket"),
        ("bra", "physics pkg bra"),
        ("braket", "physics pkg braket"),
        ("Vert", "norm"),
        ("otimes", "tensor-product Hilbert space"),
        ("binom", "binomial"),
        ("pmatrix", "Wigner 3j symbol"),
        ("Bmatrix", "Wigner 6j symbol"),
    ],
    "QFT / gauge (Phase 5)": [
        ("slashed", "Feynman slash"),
        ("not", "slash fallback"),
        ("bar", "Dirac adjoint"),
        ("gamma", "gamma matrices"),
        ("overset", "annotated operators"),
        ("underset", "annotated operators"),
        ("stackrel", "annotated operators"),
        ("overleftrightarrow", "bidirectional derivative"),
        ("overrightarrow", "right-acting derivative"),
        ("overleftarrow", "left-acting derivative"),
        ("substack", "multi-line sum limits"),
        ("limits", "explicit limit placement"),
        ("nolimits", "explicit limit placement"),
        ("epsilon", "Levi-Civita"),
        ("varepsilon", "Levi-Civita variant"),
        ("sum", "summation"),
        ("int", "integral"),
        ("oint", "contour integral"),
    ],
    "Layout / derivation steps": [
        ("phantom", "alignment in derivations"),
        ("hphantom", "alignment"),
        ("vphantom", "alignment"),
        ("quad", "spacing"),
        ("qquad", "spacing"),
        ("thinspace", "thin space"),
        ("negthinspace", "negative thin space (index tightening)"),
        ("mspace", "explicit space"),
        ("hspace", "explicit space"),
        ("text", "prose in math"),
        ("operatorname", "named operators (Tr, det)"),
        ("tag", "equation numbering"),
        ("align", "alignment env"),
        ("aligned", "alignment env"),
        ("cases", "case analysis"),
        ("array", "generic array"),
    ],
}

# Single-char / spacing commands are not identifiers; check them literally.
LITERAL = {"thinspace": r'"\\,"', "negthinspace": r'"\\!"'}

print("legend: [S]=symbol table  [P]=parser  [--]=ABSENT\n")
gaps = []
for domain, items in PROBE.items():
    print("## %s" % domain)
    for cmd, why in items:
        in_s = cmd in sym
        in_p = cmd in parser_cmds
        mark = ("S" if in_s else "-") + ("P" if in_p else "-")
        if not (in_s or in_p):
            gaps.append((domain, cmd, why))
        print("  [%s] \\%-20s %s" % (mark, cmd, why))
    print()

print("=" * 68)
print("ABSENT from both surfaces:")
for domain, cmd, why in gaps:
    print("  \\%-20s %-46s (%s)" % (cmd, why, domain.split(" (")[0]))

# Spacing macros are punctuation-named; probe raw.
print()
print("raw probes in parser source:")
for pat in [r'\\\\,', r'\\\\!', r'\\\\;', r'\\\\:']:
    print("  %-8s occurrences: %d" % (pat, len(re.findall(pat, psrc))))
print("  'movable_limits' in parser: %d" % psrc.count("movable_limits"))
