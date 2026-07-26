# Task: component tensor core

Substrate for the Phase 3 curvature pipeline. Reference material is
`docs/references/TENSOR_GEOMETRY.md`.

## Goal

A native C layer that represents tensors as concrete components on a coordinate
chart of dimension `n <= 4`, and supports the index operations the curvature
pipeline needs: raising, lowering, contraction, and componentwise partial
differentiation.

## Scope

In scope:

- charts: dimension `n <= 4` and a coordinate symbol per axis;
- component tensors of rank `r <= 4` with per-slot valence (up or down);
- get and set of a single component;
- raise and lower a single slot against a metric;
- contract two slots of one tensor;
- partial derivative of every component with respect to a coordinate;
- declared component symmetries, used to fill dependent components and to
  validate assignments.

Explicitly **not** in scope, and not to be designed around:

- abstract index notation and index canonicalization. The curvature MVP compares
  expressions by simplifying scalars, never by canonicalizing index structure.
  When canonicalization lands it starts from the papers and BSGS fixtures in
  `docs/references/TENSOR_GEOMETRY.md`; nothing in this task should anticipate
  its data model.
- differential forms, wedge products, Lie derivatives, torsion, tetrads, frames;
- dimensions above 4, and rank above 4;
- symmetrization and antisymmetrization operators;
- anything that requires Giac.

## The expression seam

The typed expression IR (Phase 1) does not exist yet. Do not block on it and do
not assume its shape. Define a narrow interface and build against a stub:

```c
typedef struct phy_expr phy_expr;   /* opaque, owned by the expression layer */

phy_expr *phy_expr_zero(void);
phy_expr *phy_expr_int(long v);
phy_expr *phy_expr_symbol(const char *name);
phy_expr *phy_expr_add(phy_expr *a, phy_expr *b);
phy_expr *phy_expr_sub(phy_expr *a, phy_expr *b);
phy_expr *phy_expr_mul(phy_expr *a, phy_expr *b);
phy_expr *phy_expr_div(phy_expr *a, phy_expr *b);
phy_expr *phy_expr_neg(phy_expr *a);
phy_expr *phy_expr_diff(phy_expr *a, const char *coord);
phy_expr *phy_expr_simplify(phy_expr *a);
int       phy_expr_is_zero(phy_expr *a);   /* must be decisive, see below */
```

`phy_expr_is_zero` is the load-bearing one. It must decide zero, not guess: the
whole corpus turns on distinguishing "this component is zero" from "this
component simplified badly". For the MVP a rational-function normal form over
the coordinate symbols is sufficient and decidable, which is why the corpus was
restricted to metrics whose components are rational in the coordinates and
`sin`/`cos` of the angles.

Deliver a stub implementation adequate for the corpus. The seam is what makes
this task independently testable, as `docs/ROADMAP.md` requires of every phase.

## Storage

Use **dense** `n^r` storage. Do not build packed symmetry-aware storage.

At `n = 4, r = 4` a dense tensor is 256 component handles, which is 1 KB at 4
bytes per handle. The full working set for one curvature pass — metric, inverse
metric, Christoffel, Riemann, Ricci, Einstein — is 384 handles, about 1.5 KB of
tables. Packed storage would save under a kilobyte and cost an index-mapping
layer that is a rich source of bugs. Spend the kilobyte.

Symmetry is therefore a *fill and check* discipline, not a storage layout:
compute the independent components, populate the dependents from the declared
symmetry, and have the test suite assert the redundant entries agree.

Zero components must share a single canonical zero handle so that the dense
tables stay cheap.

## Memory budget

Measured from the committed corpus (`docs/references/GENERAL_RELATIVITY.md`),
worst case is Reissner–Nordström: 27 distinct non-zero expressions across the
stored fields, plus roughly 4 more for the inverse metric, which the corpus does
not store. Worst single component is 58 expression nodes. Taking that worst case
for every component gives an upper bound of about 1,800 nodes for steady-state
expression storage, roughly 30 KB at 16 bytes per node.

That is the *settled* figure and it is comfortable. The number that is not known
is the intermediate peak: the Riemann recurrence forms `2 + 2n` product terms per
component before anything is collected, so transient swell of one to two orders
of magnitude over steady state is expected and has not been measured.

Required posture:

- allocate from a bounded arena, sized 512 KB for the MVP and adjustable by a
  single constant;
- enforce a hard cap on total live expression nodes;
- on exceeding either, return a clean resource-limit status and unwind, never
  abort and never partially mutate a tensor. `docs/SCIENTIFIC_SCOPE.md` requires
  explicit error values for resource limits; this is where that starts.
- no `malloc` in the component loops.

Instrument the peak and record it. Replacing the estimate above with a measured
number is part of the deliverable.

## Complexity

At `n <= 4` every operation here is small; the table is to establish that op
count is not the bottleneck.

| Operation | Elementary ops | At `n = 4` |
| --- | --- | --- |
| raise or lower one slot of rank `r` | `n^(r+1)` | 1,024 at `r = 3` |
| contract two slots of rank `r` | `n^(r-1)` | 64 at `r = 3` |
| componentwise derivative, rank `r` | `n^r` | 256 |

The cost is dominated by expression simplification, which is superlinear in tree
size, not by these counts. Optimize the number of `phy_expr_simplify` calls and
where they sit, not the loops.

## Deterministic tests

All tests must be exact and reproducible; no floating point, no tolerances.

1. **Kronecker round trip.** `g^ac g_cb` equals `delta^a_b` for every metric in
   the corpus. Trace is `n`.
2. **Raise/lower involution.** Lowering then raising any slot of a randomly
   populated tensor returns componentwise-equal expressions, for each corpus
   metric. Use a fixed seed; the test must be reproducible.
3. **Symmetry fill agreement.** For a tensor declared with Riemann symmetries,
   every component reachable by more than one symmetry path holds the same
   expression. Covers antisymmetry in the first pair, antisymmetry in the second
   pair, and pair exchange.
4. **First Bianchi identity.** `R_a[bcd] = 0`, i.e.
   `R_abcd + R_acdb + R_adbc = 0`, checked componentwise on the Riemann tensors
   in the corpus. This is a component-level identity and needs no
   canonicalization.
5. **Contraction against known traces.** Contracting the corpus Riemann tensors
   on the first and third slots reproduces the corpus Ricci entries exactly.
6. **Dimension independence.** Every test that can run at `n = 2` also runs at
   `n = 2` against `sphere_2d`. The corpus is otherwise 4-dimensional and would
   hide a hard-coded `n = 4`.
7. **Resource limit.** With the node cap set artificially low, a curvature-sized
   computation returns the resource-limit status, leaves no tensor partially
   written, and leaks nothing from the arena.

## Acceptance

- All seven tests above pass under `ctest`, warnings-as-errors including
  `-Wconversion`, matching the Phase 0 build posture.
- No `phy_host_*` symbol reaches a device binary; the existing
  `tools/symbol-report.sh` check continues to pass.
- `tools/size-report.sh` is run and the delta from the Phase 0 baseline of
  12,676 bytes is recorded in the commit message. Watch for `stdio` creeping in:
  Phase 0 found two `snprintf` calls worth 12.7 KB.
- Peak arena usage for a dimension-4 curvature pass is measured and written into
  this document, replacing the estimate above.

## Dependencies

Depends on nothing that does not yet exist — that is the point of the expression
seam. `docs/agent-tasks/GR_CURVATURE.md` depends on this task and should not
start before tests 1, 2 and 5 pass.
