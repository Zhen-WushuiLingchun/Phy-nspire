# Reference pack: general relativity and curvature

Source pack for the Phase 3 general-relativity slice (`docs/ROADMAP.md`). It
fixes the sign conventions, records the golden corpus and how it was verified,
and names the host oracles and metric catalogues.

The companion pack for index machinery is `docs/references/TENSOR_GEOMETRY.md`.
The implementation contract is `docs/agent-tasks/GR_CURVATURE.md`.

## Sign conventions

More GR bugs come from convention drift than from algebra. Misner, Thorne and
Wheeler classify a convention by three independent signs; this project is
**(+, +, +)** in that scheme, and every definition below is written out so no
reader has to consult the table.

Signature, `[S1] = +1`:

```
g = diag(-1, +1, +1, +1)      mostly plus
```

Christoffel symbols of the second kind, torsion-free and metric-compatible:

```
Gamma^a_bc = 1/2 g^ad ( d_b g_dc + d_c g_db - d_d g_bc )
```

Riemann tensor, `[S2] = +1`:

```
R^a_bcd = d_c Gamma^a_bd - d_d Gamma^a_bc
        + Gamma^a_ce Gamma^e_bd - Gamma^a_de Gamma^e_bc
```

Ricci by contraction on the first and third slots, `[S3] = +1`:

```
R_bd = R^a_bad
R    = g^bd R_bd
G_ab = R_ab - 1/2 R g_ab
```

Kretschmann invariant, used as a post-MVP check:

```
K = R_abcd R^abcd
```

Covariant Weyl tensor in dimension `n >= 3`:

```
C_abcd = R_abcd
       - (g_ac R_db - g_ad R_cb - g_bc R_da + g_bd R_ca)/(n-2)
       + R (g_ac g_db - g_ad g_cb)/((n-1)(n-2))
```

The invariant is evaluated through the equivalent contraction identity

```
C_abcd C^abcd = R_abcd R^abcd
              - 4 R_ab R^ab/(n-2)
              + 2 R^2/((n-1)(n-2)).
```

The affine geodesic equation is exposed through its exact right-hand side:

```
d^2 x^mu/dlambda^2 = -Gamma^mu_nu_rho
                       (dx^nu/dlambda) (dx^rho/dlambda).
```

Units are geometrized, `G = c = 1`.

Two computed results in the corpus pin these signs down empirically, which is
worth more than a citation:

- the round 2-sphere gives `R = +2/a_0^2`, positive for positive curvature;
- static de Sitter gives `R = +12/L^2`, positive for a positive cosmological
  constant.

A convention slip in `[S2]` or `[S3]` flips one or both of these, so the corpus
detects it without any extra test.

## The golden corpus

`research/corpus/gr_golden.json`, produced by `tools/gen-gr-golden.py`.

Values are computed from the metric alone and then cross-checked against an
independent closed form from the literature. The closed forms never feed the
computation; they only have to agree with it, and a disagreement aborts
generation. Every entry carries its own `cross_checks` block recording the
outcome and the method used.

All 6 metrics and all cross-checks in the committed corpus were verified
**symbolically** — no numerical sampling was needed or used. Generated with
SymPy 1.14.0.

| Metric | Dim | `R` | `K` | Ricci-flat |
| --- | --- | --- | --- | --- |
| `minkowski_cartesian` | 4 | 0 | 0 | yes |
| `minkowski_spherical` | 4 | 0 | 0 | yes |
| `sphere_2d` | 2 | `2/a_0**2` | `4/a_0**4` | no |
| `schwarzschild` | 4 | 0 | `48*M**2/r**6` | yes |
| `reissner_nordstrom` | 4 | 0 | `(48*M**2*r**2 - 96*M*Q**2*r + 56*Q**4)/r**8` | no |
| `de_sitter_static` | 4 | `12/L**2` | `24/L**4` | no |

The native Weyl acceptance extends this table without changing the corpus:
Minkowski and de Sitter have `C_abcd = 0`, Schwarzschild has
`C_abcd C^abcd = K`, and Reissner--Nordstrom has
`C_abcd C^abcd = 48 (M r - Q^2)^2/r^8`. A separate non-vacuum
three-dimensional `S^2 x R` case checks the dimension-three cancellation,
where the Weyl tensor vanishes identically.

Independent non-zero components emitted per metric:

| Metric | Christoffel | Riemann | Ricci | Einstein |
| --- | --- | --- | --- | --- |
| `minkowski_cartesian` | 0 | 0 | 0 | 0 |
| `minkowski_spherical` | 6 | 0 | 0 | 0 |
| `sphere_2d` | 2 | 1 | 2 | 0 |
| `schwarzschild` | 9 | 6 | 0 | 0 |
| `reissner_nordstrom` | 9 | 6 | 4 | 4 |
| `de_sitter_static` | 9 | 6 | 4 | 4 |

### Why these six

Each one fails differently, which is the point of a corpus this small.

- **`minkowski_cartesian`** — the floor. Everything vanishes. Catches a core
  that does not run at all.
- **`minkowski_spherical`** — the most valuable cheap test in the set. Six
  non-zero Christoffel symbols and identically zero curvature, so it separates
  a genuine curvature bug from a coordinate artifact. An implementation that
  confuses "connection is non-zero" with "space is curved" passes the Cartesian
  case and fails here.
- **`sphere_2d`** — Riemannian rather than Lorentzian, and dimension 2. Two
  independent checks fall out: the single independent Riemann component matches
  `n^2(n^2-1)/12 = 1`, and the Einstein tensor vanishes identically because
  `G_ab = 0` in two dimensions. A sign or trace error in the Einstein assembly
  survives every 4-dimensional vacuum case in this corpus and dies here.
- **`schwarzschild`** — the canonical vacuum. Ricci-flat with non-zero
  Kretschmann, which is the standard demonstration that `r = 2M` is a coordinate
  singularity and `r = 0` is not.
- **`reissner_nordstrom`** — the discriminating case. `R = 0` because the
  Maxwell stress tensor is trace-free in four dimensions, yet the Ricci tensor
  is non-zero. An implementation that conflates "scalar curvature vanishes" with
  "Ricci-flat" passes Schwarzschild and fails here. It is also the algebraically
  heaviest entry, and sets the expression-size budget quoted below.
- **`de_sitter_static`** — maximally symmetric with non-zero `R`, so it is the
  only committed entry where the Ricci scalar itself is a non-trivial value to
  get right.

### Format notes for consumers

- Component keys: `christoffel` uses `"a;b,c"` for `Gamma^a_bc` with `b <= c`;
  `riemann_covariant` uses `"a,b,c,d"` for `R_abcd` restricted to `a<b`, `c<d`,
  `(a,b) <= (c,d)`; symmetric rank-2 maps list `i <= j`. Omitted components are
  zero up to the stated symmetries.
- Expressions are SymPy `sstr` output. All 100 non-zero expressions in the
  corpus round-trip through `sympy.sympify`, **but only when the parameter
  symbols are bound explicitly**. `Q` in particular collides with SymPy's
  assumptions object `sympy.Q` and silently fails to parse otherwise. Each entry
  lists its `parameters`, so build the `locals` dict from that field:

  ```python
  loc = {s: sympy.Symbol(s) for s in entry["coordinates"] + entry["parameters"]}
  expr = sympy.sympify(entry["kretschmann"], locals=loc)
  ```

- `kretschmann` is emitted because the generator computes it anyway, but it is
  **not** an MVP acceptance target. It requires raising three indices on the
  full Riemann tensor; see `docs/agent-tasks/GR_CURVATURE.md`.

### Measured expression sizes

Worst case over the committed corpus, in SymPy expression-tree nodes for a
single component. Reissner–Nordström dominates every row.

| Field | Worst nodes/component | Worst chars |
| --- | --- | --- |
| metric | 19 | 27 |
| Christoffel | 38 | 65 |
| Riemann (covariant) | 58 | 130 |
| Ricci | 24 | 37 |
| Einstein | 24 | 37 |
| Ricci scalar | 5 | 8 |
| Kretschmann | 25 | 43 |

These are **post-simplification** sizes. Intermediate swell before
simplification is the real memory risk and is not captured here; the Riemann
recurrence forms `2 + 2n` products per component before anything is collected.
Sizing guidance for the native core is in `docs/agent-tasks/GR_CURVATURE.md`.

## Deferred: Kerr

Kerr in Boyer–Lindquist coordinates is defined in `tools/gen-gr-golden.py` but
marked `deferred`, excluded from the default run, and **not committed**. Running
it requires `--include-deferred`.

An attempt at symbolic generation was made and abandoned: with the
simplification strategy the generator uses, it did not close within the time
budget allowed for this work. This is a known-hard case rather than a surprise —
Kerr is the first non-diagonal metric here, its `g_tphi` cross term makes the
inverse metric dense, and every downstream contraction inherits that density.

Kerr is therefore excluded from the MVP in both directions: no committed golden
values, and no live device computation. Reaching it needs a targeted
simplification strategy — the standard approach is to work in terms of
`Sigma = r^2 + a^2 cos^2(theta)` and `Delta = r^2 - 2Mr + a^2` rather than
expanding, and to substitute `u = cos(theta)` so the components become rational
— not merely more patience. Treat it as its own piece of work.

For when that work happens, the target closed form for the Kerr Kretschmann
scalar is:

```
K = 48 M^2 (r^2 - a^2 c^2) ((r^2 + a^2 c^2)^2 - 16 r^2 a^2 c^2)
    / (r^2 + a^2 c^2)^6                                where c = cos(theta)
```

from Henry (below). This expression is recorded here as an unverified target: it
is what the generator would check against, and it has not been confirmed by this
project's own computation.

## Host oracles

Both are host-side only. Neither is vendored, linked, or shipped.

### SymPy — primary oracle

- `https://github.com/sympy/sympy`, tag `1.14.0`, published 2025-04-27.
- Tag object SHA: `fe935ceb303891d1f8bea4c03b19fd9ec9464b02`.
- License: 3-clause BSD, verified by reading `LICENSE` at that tag.

Verified curvature helpers in `sympy.diffgeom`: `metric_to_Christoffel_1st`,
`metric_to_Christoffel_2nd`, `metric_to_Riemann_components`,
`metric_to_Ricci_components`, plus `Manifold`, `Patch`, `CoordSystem`,
`TensorProduct`.

This is the primary oracle: actively maintained, permissively licensed, and
already the engine behind `tools/gen-gr-golden.py`. Note that the generator does
**not** use the `diffgeom` helpers — it implements the curvature recurrences
directly from the definitions above. That is deliberate. It keeps the sign
conventions under this project's control and makes `diffgeom` available as a
genuinely independent second opinion rather than the same code path twice.

### EinsteinPy — secondary oracle

- `https://github.com/einsteinpy/einsteinpy`, tag `v0.4.0`, published
  2021-05-05.
- Tag object SHA: `555b51d63dcd9512aa7a04f445f5cb206e24e351`.
- License: MIT.
- Relevant modules: `einsteinpy.symbolic` — `christoffel`, `riemann`, `ricci`,
  `einstein`, `weyl`, `schouten`, `stress_energy_momentum`, and a
  `predefined` catalogue of standard metrics.

Useful mainly for its predefined metric catalogue and for Weyl and Schouten
tensors, which SymPy's `diffgeom` does not provide directly. Caveat worth
stating plainly: the last release is from May 2021 and the last commit to the
default branch was June 2024. It is not abandoned but it is not current either.
Treat a disagreement with SymPy as SymPy being right until shown otherwise, and
do not make it a dependency of any acceptance test.

## Metric catalogues and invariants

- H. Stephani, D. Kramer, M. MacCallum, C. Hoenselaers, E. Herlt, *Exact
  Solutions of Einstein's Field Equations*, 2nd ed., Cambridge University Press
  (2003). The standard catalogue; the reference for extending the corpus beyond
  the six entries above.
- C. W. Misner, K. S. Thorne, J. A. Wheeler, *Gravitation*, Freeman (1973).
  Sign-convention table on the inside front cover; Schwarzschild curvature in
  Box 31.2.
- R. M. Wald, *General Relativity*, University of Chicago Press (1984).
  Chapter 6 for the Schwarzschild solution.
- R. C. Henry, "Kretschmann Scalar for a Kerr–Newman Black Hole",
  *Astrophysical Journal* **535** (2000) 350. Source of the Reissner–Nordström
  closed form used in the corpus (the `a = 0` limit, confirmed by this
  project's own computation) and of the Kerr form quoted above (not confirmed).
- C. Cherubini, D. Bini, S. Capozziello, R. Ruffini, "Second order scalar
  invariants of the Riemann tensor: applications to black hole spacetimes",
  *Int. J. Mod. Phys. D* **11** (2002) 827.

## Extending the corpus

Add a metric to `_metrics()` in `tools/gen-gr-golden.py` with an `expect` block
holding an independently sourced closed form, then regenerate. Generation fails
loudly if the computed value and the cited value disagree, so a new entry cannot
be committed without either agreeing with the literature or forcing a decision
about which is wrong.

Prefer metrics that fail in a new way over metrics that are merely more famous.
The gaps in the current set, roughly in order of value: a non-vacuum
cosmological case such as flat FLRW, where `R` depends on an undetermined
function of time; a case with a non-diagonal metric cheaper than Kerr; and a
dimension-3 case, since every 4-dimensional entry here would survive a bug that
hard-codes `n = 4`.
