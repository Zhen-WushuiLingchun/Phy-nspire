# Task: general-relativity curvature pipeline

First Phase 3 milestone. Reference material is
`docs/references/GENERAL_RELATIVITY.md`. Depends on
`docs/agent-tasks/TENSOR_CORE.md`.

## Goal

From a coordinate metric on a chart of dimension `n <= 4`, compute:

1. the inverse metric `g^ab`;
2. Christoffel symbols of the second kind `Gamma^a_bc`;
3. the Riemann tensor `R_abcd`;
4. the Ricci tensor `R_ab`;
5. the scalar curvature `R`;
6. the Einstein tensor `G_ab`.

That is the whole MVP. Conventions are fixed in
`docs/references/GENERAL_RELATIVITY.md` and must not be re-derived: this project
is `(+, +, +)` in the MTW classification, signature `(-, +, +, +)`.

## Deferred

Not in this task, and not to be partially implemented:

- **Kerr, in both directions.** No live device computation, and no golden values
  — the corpus deliberately does not contain them. Symbolic generation of Kerr
  curvature was attempted and abandoned as too expensive; see the deferral note
  in `docs/references/GENERAL_RELATIVITY.md` for why, and for the simplification
  strategy any future attempt should start from. Do not add a Kerr test.
- **Kretschmann `K = R_abcd R^abcd`.** The corpus carries values for it, and
  they are correct, but it is not an acceptance target. It needs three index
  raisings over the full Riemann tensor — `n^3` sums per component across `n^4`
  components — and it is the natural first extension once the six MVP quantities
  are solid, not part of the first cut.
- Geodesic equations, Killing vectors, conserved quantities.
- Weyl and Schouten tensors, tetrads, Newman–Penrose scalars.
- Any dimension above 4.

## Input

A chart and a metric, both supplied as component tensors from
`docs/agent-tasks/TENSOR_CORE.md`. The metric must be symmetric and its
determinant must be non-zero as an expression; reject anything else with a typed
error rather than producing nonsense.

## Algorithm and complexity

Written for `n <= 4`, where every count below is small. Formulae are in
`docs/references/GENERAL_RELATIVITY.md`.

**Inverse metric.** Use the cofactor/adjugate form, not Gaussian elimination.
At `n = 4` that is 16 cofactors, each a 3×3 determinant of 6 signed products, so
96 products plus one determinant, with a single division by `det(g)` at the end.
The reason is not speed: elimination needs a pivot choice, and choosing a pivot
among symbolic entries requires deciding whether an expression is zero, which
makes the result depend on how well the simplifier happened to do. Cofactor
expansion is branch-free and therefore deterministic, which the acceptance tests
below require.

**Remaining quantities.** Independent component counts and elementary operation
counts at `n = 4`:

| Quantity | Independent components at `n = 4` | Formula | Elementary ops |
| --- | --- | --- | --- |
| `g^ab` | 10 | `n(n+1)/2` | ~100 |
| `Gamma^a_bc` | 40 | `n^2(n+1)/2` | `O(n^4)` = 256 |
| `R_abcd` | 20 | `n^2(n^2-1)/12` | `O(n^5)` = 1,024 |
| `R_ab` | 10 | `n(n+1)/2` | `O(n^3)` = 64 |
| `R` | 1 | — | `O(n^2)` = 16 |
| `G_ab` | 10 | `n(n+1)/2` | `O(n^2)` = 16 |

Total is order `10^3` elementary symbolic operations. This is nothing. The
entire cost of the pipeline is expression simplification, whose cost is
superlinear in tree size, so the engineering question is *where* to simplify,
not how to shave loop iterations.

Guidance from building the generator, which does exactly this computation on the
host: simplify after each stage rather than at the end. Christoffel symbols left
unsimplified make the Riemann stage dramatically more expensive, because every
one of the `2 + 2n` terms per Riemann component inherits the bloat. Rational
normalisation is enough for the diagonal metrics; only Reissner–Nordström in the
committed corpus needs real effort, and it is the sizing case throughout.

## Output

For each computed quantity, the independent non-zero components, keyed as in the
corpus: `"a;b,c"` for `Gamma^a_bc` with `b <= c`, `"a,b,c,d"` for `R_abcd` with
`a<b`, `c<d`, `(a,b) <= (c,d)`, and `i <= j` for symmetric rank-2.

## Deterministic tests

The corpus is `research/corpus/gr_golden.json`, six metrics, every value
cross-checked symbolically against an independently sourced closed form. Read
`scope.mvp_fields` from the corpus and ignore the rest.

**Comparison rule.** Compare by computing `got - want` and deciding it is zero
through the expression layer. Never compare serialized strings: `2*M/r` and
`2*M*r**(-1)` are the same expression and a string test would reject one of
them. This rule is the difference between a corpus that catches bugs and a
corpus that manufactures them.

**Parsing rule.** Bind parameter symbols explicitly from each entry's
`coordinates` and `parameters` fields. Host-side consumers using SymPy must do
this or `Q` silently collides with `sympy.Q`; see
`docs/references/GENERAL_RELATIVITY.md`.

Required tests:

1. **Corpus reproduction.** For each of the six metrics, all six MVP quantities
   match the golden values componentwise.
2. **Flat in disguise.** `minkowski_spherical` yields six non-zero Christoffel
   symbols and identically zero Riemann, Ricci, `R`, and Einstein. The single
   most valuable test in the set — it is the one that separates a real curvature
   bug from a coordinate artifact, and an implementation that conflates the two
   passes `minkowski_cartesian` and fails here.
3. **Two-dimensional Einstein identity.** `sphere_2d` yields `R = 2/a_0^2`, one
   independent Riemann component, and an Einstein tensor that is identically
   zero. `G_ab = 0` holds identically in two dimensions, so a sign or trace error
   in the Einstein assembly that survives every 4-dimensional vacuum entry dies
   here.
4. **Vacuum.** `schwarzschild` is Ricci-flat with `R = 0` and non-zero Riemann.
5. **Trace-free but not Ricci-flat.** `reissner_nordstrom` gives `R = 0` with a
   non-zero Ricci tensor. Distinguishes "scalar curvature vanishes" from
   "Ricci-flat"; an implementation that conflates them passes test 4 and fails
   this one.
6. **Non-zero scalar curvature.** `de_sitter_static` gives `R = 12/L^2`. The only
   committed entry where `R` itself is a non-trivial value.
7. **Determinism.** Two runs over the whole corpus produce byte-identical
   serialized output. The host and device builds produce the same output for
   every metric.
8. **Resource limit.** With the node cap from `docs/agent-tasks/TENSOR_CORE.md`
   set low, the pipeline returns the resource-limit status cleanly, with no
   partial results presented as complete.

Tests 2, 3 and 5 are the discriminating ones. A pipeline that passes only 1, 4
and 6 is not yet known to work.

## Acceptance

- All eight tests pass under `ctest` with warnings-as-errors including
  `-Wconversion`.
- `tools/symbol-report.sh` still shows no `phy_host_*` symbols in the device
  binary.
- `tools/size-report.sh` delta from the Phase 0 baseline of 12,676 bytes is
  recorded in the commit message.
- Wall-clock time for a full dimension-4 curvature pass is measured on the host
  and, if hardware is available, on the CX II, and recorded here. No target is
  set: the first measurement establishes the baseline.
- Peak arena usage for the Reissner–Nordström pass is recorded here.

## Regenerating the corpus

```
python tools/gen-gr-golden.py
```

Requires SymPy on the host only; nothing from it ships to the calculator.
Generation aborts if any computed value disagrees with its cited closed form, so
the committed corpus cannot drift away from the literature. Adding a metric
means adding an `expect` block with an independently sourced closed form — see
the extension notes in `docs/references/GENERAL_RELATIVITY.md`.
