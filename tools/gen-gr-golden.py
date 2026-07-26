#!/usr/bin/env python3
"""Generate research/corpus/gr_golden.json: curvature golden values for the
Phase 3 general-relativity slice.

Every entry is computed from the metric alone, then cross-checked against a
closed form taken from the literature. The closed forms are not used to produce
the values; they only have to agree with them. A metric whose cross-check fails
aborts the run, so the committed corpus cannot drift away from the references.

SymPy is a generator-side dependency only. Nothing here ships to the calculator;
the output JSON is the artifact the native tests consume.

Scope is deliberately bounded. The committed corpus covers metrics whose
curvature closes symbolically in seconds, which is what the MVP in
docs/agent-tasks/GR_CURVATURE.md needs. Kerr is defined but marked deferred: it
is opt-in behind --include-deferred and its values are not committed.

Usage:
    python tools/gen-gr-golden.py [--out PATH] [--only NAME[,NAME...]]
                                  [--include-deferred]
"""

import argparse
import json
import sys

import sympy as sp

# --------------------------------------------------------------------------
# Curvature from a metric. Conventions are stated in research/TENSOR_GR_SOURCES.md
# and repeated in the emitted JSON so a reader never has to guess a sign.
#
#   Gamma^a_bc  = 1/2 g^ad (d_b g_dc + d_c g_db - d_d g_bc)
#   R^a_bcd     = d_c Gamma^a_bd - d_d Gamma^a_bc
#                 + Gamma^a_ce Gamma^e_bd - Gamma^a_de Gamma^e_bc
#   R_bd        = R^a_bad                      (Ricci)
#   R           = g^bd R_bd                    (Ricci scalar)
#   G_ab        = R_ab - 1/2 R g_ab            (Einstein)
#   K           = R_abcd R^abcd                (Kretschmann)
#
# Signature is mostly-plus (-,+,+,+) for the Lorentzian metrics.
# --------------------------------------------------------------------------


def _simp(e, aggressive):
    """Rational-function-first simplification.

    cancel() alone handles the Schwarzschild/RN family; the rotating and
    cosmological metrics need trig folding before the rational structure
    becomes visible.
    """
    e = sp.together(sp.expand(e))
    e = sp.cancel(e)
    if aggressive:
        e = sp.simplify(sp.trigsimp(e))
    return e


def christoffel(g, ginv, x, aggressive):
    n = len(x)
    G = [[[sp.S.Zero] * n for _ in range(n)] for _ in range(n)]
    for a in range(n):
        for b in range(n):
            for c in range(b, n):
                s = sum(ginv[a, d] * (sp.diff(g[d, b], x[c])
                                      + sp.diff(g[d, c], x[b])
                                      - sp.diff(g[b, c], x[d])) for d in range(n))
                v = _simp(s / 2, aggressive)
                G[a][b][c] = v
                G[a][c][b] = v
    return G


def riemann_mixed(G, x, aggressive):
    """R^a_{bcd}"""
    n = len(x)
    R = [[[[sp.S.Zero] * n for _ in range(n)] for _ in range(n)] for _ in range(n)]
    for a in range(n):
        for b in range(n):
            for c in range(n):
                for d in range(c + 1, n):
                    e = sp.diff(G[a][b][d], x[c]) - sp.diff(G[a][b][c], x[d])
                    e += sum(G[a][c][k] * G[k][b][d] - G[a][d][k] * G[k][b][c]
                             for k in range(n))
                    v = _simp(e, aggressive)
                    R[a][b][c][d] = v
                    R[a][b][d][c] = -v
    return R


def curvature(g, x, aggressive):
    n = len(x)
    ginv = _matsimp(g.inv(), aggressive)
    G = christoffel(g, ginv, x, aggressive)
    Rm = riemann_mixed(G, x, aggressive)

    Ric = sp.Matrix(n, n, lambda b, d: _simp(sum(Rm[a][b][a][d] for a in range(n)),
                                             aggressive))
    Rs = _simp(sum(ginv[b, d] * Ric[b, d] for b in range(n) for d in range(n)),
               aggressive)
    Ein = sp.Matrix(n, n, lambda a, b: _simp(Ric[a, b] - Rs * g[a, b] / 2, aggressive))

    # Fully covariant and fully contravariant Riemann, for the Kretschmann trace.
    Rdn = [[[[_simp(sum(g[a, e] * Rm[e][b][c][d] for e in range(n)), aggressive)
              for d in range(n)] for c in range(n)] for b in range(n)] for a in range(n)]
    Rup = [[[[_simp(sum(ginv[b, j] * ginv[c, k] * ginv[d, l] * Rm[a][j][k][l]
                        for j in range(n) for k in range(n) for l in range(n)),
                    aggressive)
              for d in range(n)] for c in range(n)] for b in range(n)] for a in range(n)]
    K = _simp(sum(Rdn[a][b][c][d] * Rup[a][b][c][d]
                  for a in range(n) for b in range(n)
                  for c in range(n) for d in range(n)), aggressive)
    return {"christoffel": G, "riemann_mixed": Rm, "riemann_down": Rdn,
            "ricci": Ric, "ricci_scalar": Rs, "einstein": Ein, "kretschmann": K}


def _matsimp(m, aggressive):
    return sp.Matrix(m.rows, m.cols, lambda i, j: _simp(m[i, j], aggressive))


# --------------------------------------------------------------------------
# Metric catalogue
# --------------------------------------------------------------------------

t, r, th, ph = sp.symbols('t r theta phi', real=True)
M, Q, a, L, aa, k = sp.symbols('M Q a L a_0 k', real=True, positive=True)
tau = sp.Function('a')(t)


def _metrics():
    S2 = sp.sin(th) ** 2
    cs = sp.cos(th) ** 2

    f_s = 1 - 2 * M / r
    f_rn = 1 - 2 * M / r + Q ** 2 / r ** 2
    f_ds = 1 - r ** 2 / L ** 2

    sigma = r ** 2 + a ** 2 * cs
    delta = r ** 2 - 2 * M * r + a ** 2

    kerr = sp.zeros(4, 4)
    kerr[0, 0] = -(1 - 2 * M * r / sigma)
    kerr[0, 3] = kerr[3, 0] = -2 * M * r * a * S2 / sigma
    kerr[1, 1] = sigma / delta
    kerr[2, 2] = sigma
    kerr[3, 3] = (r ** 2 + a ** 2 + 2 * M * r * a ** 2 * S2 / sigma) * S2

    return [
        dict(name="minkowski_cartesian",
             description="Flat spacetime in Cartesian coordinates. The trivial "
                         "floor: every curvature object vanishes identically.",
             coords=['t', 'x', 'y', 'z'],
             syms=sp.symbols('t x y z', real=True),
             params=[], signature="(-,+,+,+)", dim=4, aggressive=False,
             build=lambda X: sp.diag(-1, 1, 1, 1),
             expect=dict(ricci_scalar=sp.S.Zero, kretschmann=sp.S.Zero,
                         ricci_zero=True, riemann_zero=True),
             source="Any GR text; definitional."),

        dict(name="minkowski_spherical",
             description="Flat spacetime in spherical coordinates. Christoffel "
                         "symbols are non-zero while every curvature tensor "
                         "still vanishes, so this separates a genuine curvature "
                         "bug from a coordinate artifact.",
             coords=['t', 'r', 'theta', 'phi'],
             syms=(t, r, th, ph), params=[], signature="(-,+,+,+)", dim=4,
             aggressive=False,
             build=lambda X: sp.diag(-1, 1, r ** 2, r ** 2 * S2),
             expect=dict(ricci_scalar=sp.S.Zero, kretschmann=sp.S.Zero,
                         ricci_zero=True, riemann_zero=True),
             source="Any GR text; flat metric in curvilinear coordinates."),

        dict(name="sphere_2d",
             description="Round 2-sphere of radius a_0. Maximally symmetric "
                         "Riemannian check with R = 2/a_0^2 and K = 4/a_0^4.",
             coords=['theta', 'phi'], syms=(th, ph), params=['a_0'],
             signature="(+,+)", dim=2, aggressive=True,
             build=lambda X: sp.diag(aa ** 2, aa ** 2 * S2),
             expect=dict(ricci_scalar=2 / aa ** 2, kretschmann=4 / aa ** 4,
                         ricci_zero=False, riemann_zero=False),
             source="Standard: R = 2/a^2 for S^2 of radius a; "
                    "K = R_abcd R^abcd = 4/a^4 in 2D."),

        dict(name="schwarzschild",
             description="Schwarzschild vacuum in Schwarzschild coordinates. "
                         "Ricci-flat with a non-zero Kretschmann scalar, which "
                         "is the standard demonstration that r = 2M is a "
                         "coordinate singularity and r = 0 is not.",
             coords=['t', 'r', 'theta', 'phi'], syms=(t, r, th, ph), params=['M'],
             signature="(-,+,+,+)", dim=4, aggressive=False,
             build=lambda X: sp.diag(-f_s, 1 / f_s, r ** 2, r ** 2 * S2),
             expect=dict(ricci_scalar=sp.S.Zero, kretschmann=48 * M ** 2 / r ** 6,
                         ricci_zero=True, riemann_zero=False),
             source="MTW Gravitation Box 31.2; Wald General Relativity ch. 6. "
                    "K = 48 M^2 / r^6."),

        dict(name="reissner_nordstrom",
             description="Charged non-rotating black hole. Ricci scalar still "
                         "vanishes because the Maxwell stress tensor is "
                         "trace-free in four dimensions, but the Ricci tensor "
                         "itself does not, which distinguishes 'R = 0' from "
                         "'Ricci-flat'.",
             coords=['t', 'r', 'theta', 'phi'], syms=(t, r, th, ph),
             params=['M', 'Q'], signature="(-,+,+,+)", dim=4, aggressive=False,
             build=lambda X: sp.diag(-f_rn, 1 / f_rn, r ** 2, r ** 2 * S2),
             expect=dict(ricci_scalar=sp.S.Zero,
                         kretschmann=(48 * M ** 2 / r ** 6 - 96 * M * Q ** 2 / r ** 7
                                      + 56 * Q ** 4 / r ** 8),
                         ricci_zero=False, riemann_zero=False),
             source="Henry, 'Kretschmann scalar for a Kerr-Newman black hole', "
                    "ApJ 535 (2000) 350; reduces to "
                    "48M^2/r^6 - 96MQ^2/r^7 + 56Q^4/r^8 at a = 0."),

        dict(name="de_sitter_static",
             description="de Sitter space in the static patch. Maximally "
                         "symmetric Lorentzian vacuum with a cosmological "
                         "constant: R = 12/L^2 and K = 24/L^4.",
             coords=['t', 'r', 'theta', 'phi'], syms=(t, r, th, ph), params=['L'],
             signature="(-,+,+,+)", dim=4, aggressive=False,
             build=lambda X: sp.diag(-f_ds, 1 / f_ds, r ** 2, r ** 2 * S2),
             expect=dict(ricci_scalar=12 / L ** 2, kretschmann=24 / L ** 4,
                         ricci_zero=False, riemann_zero=False),
             source="Maximally symmetric space with Lambda = 3/L^2: "
                    "R = 4 Lambda = 12/L^2, K = 8 Lambda^2/3 = 24/L^4."),

        dict(name="kerr_boyer_lindquist",
             description="Kerr vacuum in Boyer-Lindquist coordinates. The first "
                         "metric in this corpus that is not diagonal; it "
                         "exercises off-diagonal g_tphi handling. DEFERRED: not "
                         "part of the bounded corpus and not an MVP target. Its "
                         "symbolic curvature does not close in a bounded time "
                         "budget with the simplification strategy used here, so "
                         "generating it is opt-in via --include-deferred and its "
                         "values are not committed. See "
                         "docs/agent-tasks/GR_CURVATURE.md.",
             deferred=True,
             coords=['t', 'r', 'theta', 'phi'], syms=(t, r, th, ph),
             params=['M', 'a'], signature="(-,+,+,+)", dim=4, aggressive=True,
             build=lambda X: kerr,
             expect=dict(ricci_scalar=sp.S.Zero,
                         kretschmann=(48 * M ** 2 * (r ** 2 - a ** 2 * cs)
                                      * ((r ** 2 + a ** 2 * cs) ** 2
                                         - 16 * r ** 2 * a ** 2 * cs)
                                      / (r ** 2 + a ** 2 * cs) ** 6),
                         ricci_zero=True, riemann_zero=False),
             source="Henry, ApJ 535 (2000) 350, eq. for the Kerr Kretschmann "
                    "scalar; also Cherubini et al., Int. J. Mod. Phys. D 11 "
                    "(2002) 827."),
    ]


# --------------------------------------------------------------------------
# Verification
# --------------------------------------------------------------------------

def _is_zero(e, aggressive):
    e = _simp(e, aggressive)
    if e == 0:
        return True
    return sp.simplify(e) == 0


def _agrees(got, want, aggressive):
    """Symbolic equality, falling back to exact rational sampling.

    The fallback substitutes random rationals into the difference and evaluates
    with exact arithmetic. For the rational-in-(r, cos theta) expressions in this
    corpus, agreement at many independent generic points is strong evidence; the
    method actually used is recorded in the emitted JSON.
    """
    d = sp.expand(got - want)
    if _is_zero(d, aggressive):
        return True, "symbolic"

    free = sorted(d.free_symbols, key=lambda s: s.name)
    if not free:
        return False, "symbolic"
    rng = sp.Rational
    for seed in range(1, 41):
        sub = {}
        for i, s in enumerate(free):
            # Generic, small, and away from the horizons/poles of the family.
            sub[s] = rng(7 + 3 * i + 5 * seed, 4 + i + seed)
        try:
            v = sp.nsimplify(d.subs(sub))
            v = sp.simplify(v)
        except Exception:
            return False, "sampled"
        if v != 0:
            return False, "sampled"
    return True, "sampled-exact-rational-40pts"


def _ser(e):
    return sp.sstr(sp.nsimplify(e)) if e != 0 else "0"


def _nonzero_map(obj, rank, n, coords):
    """Emit only the independent non-zero components, keyed by index name."""
    out = {}
    if rank == 3:  # Gamma[a][b][c], symmetric in b c
        for a in range(n):
            for b in range(n):
                for c in range(b, n):
                    v = obj[a][b][c]
                    if v != 0:
                        out[f"{coords[a]};{coords[b]},{coords[c]}"] = _ser(v)
    elif rank == 4:  # R_{abcd}, emit a<b, c<d, (ab) <= (cd)
        for a in range(n):
            for b in range(n):
                for c in range(n):
                    for d in range(c + 1, n):
                        if a >= b:
                            continue
                        if (a, b) > (c, d):
                            continue
                        v = obj[a][b][c][d]
                        if v != 0:
                            out[f"{coords[a]},{coords[b]},{coords[c]},{coords[d]}"] = _ser(v)
    return out


def _mat_map(m, n, coords):
    out = {}
    for i in range(n):
        for j in range(i, n):
            if m[i, j] != 0:
                out[f"{coords[i]},{coords[j]}"] = _ser(m[i, j])
    return out


def build(spec):
    n = spec["dim"]
    X = list(spec["syms"])
    g = spec["build"](X)
    aggressive = spec["aggressive"]

    cur = curvature(g, X, aggressive)
    exp = spec["expect"]
    checks = {}

    ok, how = _agrees(cur["ricci_scalar"], exp["ricci_scalar"], aggressive)
    checks["ricci_scalar"] = {"agrees": ok, "method": how,
                              "expected": _ser(exp["ricci_scalar"])}
    if not ok:
        raise SystemExit(f"{spec['name']}: Ricci scalar {cur['ricci_scalar']} "
                         f"!= expected {exp['ricci_scalar']}")

    ric_zero = all(_is_zero(cur["ricci"][i, j], aggressive)
                   for i in range(n) for j in range(i, n))
    if ric_zero != exp["ricci_zero"]:
        raise SystemExit(f"{spec['name']}: Ricci-flat = {ric_zero}, "
                         f"expected {exp['ricci_zero']}")
    checks["ricci_flat"] = {"agrees": True, "value": ric_zero}

    riem_zero = all(_is_zero(cur["riemann_down"][i][j][kk][l], aggressive)
                    for i in range(n) for j in range(n)
                    for kk in range(n) for l in range(n))
    if riem_zero != exp["riemann_zero"]:
        raise SystemExit(f"{spec['name']}: Riemann-zero = {riem_zero}, "
                         f"expected {exp['riemann_zero']}")
    checks["riemann_zero"] = {"agrees": True, "value": riem_zero}

    ok, how = _agrees(cur["kretschmann"], exp["kretschmann"], aggressive)
    checks["kretschmann"] = {"agrees": ok, "method": how,
                             "expected": _ser(exp["kretschmann"])}
    if not ok:
        raise SystemExit(f"{spec['name']}: Kretschmann {cur['kretschmann']} "
                         f"!= expected {exp['kretschmann']}")

    coords = spec["coords"]
    return {
        "name": spec["name"],
        "description": spec["description"],
        "source": spec["source"],
        "dimension": n,
        "signature": spec["signature"],
        "coordinates": coords,
        "parameters": spec["params"],
        "metric": _mat_map(_matsimp(g, aggressive), n, coords),
        "christoffel": _nonzero_map(cur["christoffel"], 3, n, coords),
        "riemann_covariant": _nonzero_map(cur["riemann_down"], 4, n, coords),
        "ricci": _mat_map(cur["ricci"], n, coords),
        "ricci_scalar": _ser(cur["ricci_scalar"]),
        "einstein": _mat_map(cur["einstein"], n, coords),
        "kretschmann": _ser(cur["kretschmann"]),
        "cross_checks": checks,
    }


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--out", default="research/corpus/gr_golden.json")
    p.add_argument("--only", default="")
    p.add_argument("--include-deferred", action="store_true",
                   help="Also generate metrics marked deferred. These are not "
                        "part of the committed bounded corpus and may not "
                        "terminate in a useful time budget.")
    args = p.parse_args()

    want = set(x for x in args.only.split(",") if x)
    specs = []
    for s in _metrics():
        if want:
            if s["name"] not in want:
                continue
        elif s.get("deferred") and not args.include_deferred:
            print(f"  skipping deferred metric {s['name']}", file=sys.stderr)
            continue
        specs.append(s)

    entries = []
    for s in specs:
        print(f"  computing {s['name']} ...", file=sys.stderr, flush=True)
        entries.append(build(s))
        print(f"    ok", file=sys.stderr, flush=True)

    doc = {
        "schema": 1,
        "generator": "tools/gen-gr-golden.py",
        "generated_with": f"sympy {sp.__version__}",
        "purpose": ("Golden curvature values for the Phase 3 general-relativity "
                    "slice. Values are computed from each metric and "
                    "cross-checked against an independent closed form from the "
                    "literature; see cross_checks on every entry."),
        "scope": {
            "mvp_fields": ["metric", "christoffel", "riemann_covariant", "ricci",
                           "ricci_scalar", "einstein"],
            "post_mvp_fields": ["kretschmann"],
            "max_dimension": 4,
            "deferred_metrics": ["kerr_boyer_lindquist"],
            "note": ("kretschmann is emitted because the generator computes it "
                     "anyway, but it is not an MVP acceptance target: it needs "
                     "three index raisings on the full Riemann tensor. Kerr is "
                     "excluded entirely; see docs/agent-tasks/GR_CURVATURE.md."),
        },
        "conventions": {
            "christoffel": "Gamma^a_bc = 1/2 g^ad (d_b g_dc + d_c g_db - d_d g_bc)",
            "riemann": ("R^a_bcd = d_c Gamma^a_bd - d_d Gamma^a_bc "
                        "+ Gamma^a_ce Gamma^e_bd - Gamma^a_de Gamma^e_bc"),
            "ricci": "R_bd = R^a_bad",
            "ricci_scalar": "R = g^bd R_bd",
            "einstein": "G_ab = R_ab - 1/2 R g_ab",
            "kretschmann": "K = R_abcd R^abcd",
            "units": "Geometrized, G = c = 1.",
            "component_keys": ("christoffel 'a;b,c' is Gamma^a_bc with b <= c; "
                               "riemann_covariant 'a,b,c,d' is R_abcd restricted "
                               "to a<b, c<d, (a,b) <= (c,d); symmetric rank-2 "
                               "maps list i <= j. Omitted components are zero, "
                               "up to the stated symmetries."),
        },
        "metrics": entries,
    }
    with open(args.out, "w", encoding="utf-8", newline="\n") as fh:
        json.dump(doc, fh, indent=2, ensure_ascii=False)
        fh.write("\n")
    print(f"wrote {args.out} with {len(entries)} metrics", file=sys.stderr)


if __name__ == "__main__":
    main()
