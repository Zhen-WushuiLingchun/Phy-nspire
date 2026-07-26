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
- anything that requires a CAS backend. The planned trimmed native Giac backend
  in `docs/ARCHITECTURE.md` has not landed; nothing in this task may depend on
  it, and the operations below come from the typed IR instead.

## Dependency on the typed IR and the scalar CAS layer

> **Correction, recorded when the storage half landed.** This section
> originally attributed every capability in the table below to the typed IR.
> Three of them are not there and were never going to be:
> `include/phy/ir.h` builds and interns expressions, it does not evaluate
> them. `phy_ir_add` produces `(+ 2 3)`, not `5`; `phy_ir_derivative` produces
> an unevaluated `(d f x)` node; and there is no simplify and no zero decision
> anywhere in the header. `docs/IR.md` agrees — "simplification, evaluation,
> and arithmetic" are listed there as *not in that layer* — so it is this
> document that was wrong, not the IR.
>
> Those three capabilities come from the **canonical scalar CAS layer**, which
> is a separate dependency landing in parallel. The table below now says which
> layer each capability comes from.
>
> **Component-independent tensor storage is implemented first**, against the
> IR alone: charts, rank and per-slot valence, dense `n^r` storage,
> flat-index encoding, the declared symmetry group, canonical component
> lookup, fill, assignment validation, and the allocation-failure unwind. That
> is the half of this task that never forms an expression, and it is complete
> and tested. See `docs/TENSOR.md`. The index operations — raise, lower,
> contract, componentwise derivative — bind to the scalar header when it
> lands.

The handle-based typed expression IR is the substrate for this task. Bind
against the real `include/phy/ir.h`.

Do not define an expression type here. Do not wrap the IR in an adapter layer,
and do not introduce a second set of construction, lifetime, or ownership rules:
`include/phy/ir.h` is the single authority on the handle type, on how handles are
created and released, and on which arena they come from. Where this document and
that header disagree about anything the header covers, the header wins — raise
the conflict rather than working around it. The correction above is what that
rule produced.

What follows is an operation-level contract: the capabilities the component
tensor core requires, stated in terms of what each one must decide or produce.
Bind each to whatever the owning header actually names it.

| Capability | From | Required behaviour | Used by |
| --- | --- | --- | --- |
| Integer and rational literals | typed IR | Exact, no floating point anywhere in the pipeline | Metric entries, the `1/2` in the Christoffel formula |
| Coordinate symbol reference | typed IR | Resolve a chart axis to its IR symbol | Every derivative |
| Bounded-arena allocation with a live-node cap | typed IR | Allocation failure surfaces as a recoverable status, never an abort | Whole pipeline |
| Add, subtract, negate | scalar CAS | — | All four curvature stages; also the sign fold described below |
| Multiply, divide | scalar CAS | Division only by an expression established non-zero, i.e. `det(g)` | Inverse metric, Christoffel |
| Partial derivative w.r.t. a coordinate | scalar CAS | Componentwise, exact | Christoffel, Riemann |
| Simplify | scalar CAS | Must reach a normal form on which the zero decision below is exact | End of each stage |
| **Zero decision** | scalar CAS | Must *decide*, not estimate — see below | Symmetry fill, corpus comparison, resource unwinding |

Note that negation is on the scalar side, and that this is what fixes the
boundary between the two halves of this task. Filling an antisymmetric orbit
means writing `-x` into the mirrored component, so the storage layer stores a
handle and a sign rather than a negated expression, and folding the sign back
into a single expression is the scalar layer's first job here.

### The zero decision

This is the load-bearing capability and the one to confirm first. It must decide
whether an expression is zero, not guess. The entire corpus turns on
distinguishing "this component is genuinely zero" from "this component did not
simplify well enough to tell" — `minkowski_spherical` is exactly the case where
a non-zero connection must yield provably zero curvature, and a heuristic answer
there converts a passing test into a meaningless one.

For the MVP, a rational-function normal form over the coordinate symbols is
sufficient and decidable. That is why the corpus is restricted to metrics whose
components are rational in the coordinates and in `sin`/`cos` of the angles: the
bound was chosen so that this decision stays exact. If the IR's normal form does
not cover that class, that is a blocking finding about the corpus scope, not
something for this task to paper over locally.

### Bounded arena

Treat the arena as an IR-provided capability rather than something this layer
implements. What the tensor core is responsible for is behaving correctly when
the cap is hit: return a clean resource-limit status, leave no tensor partially
written, and release everything it acquired. Sizing and instrumentation are in
the next section.

### Sequencing

If a dependency is not yet available, the correct move is to implement and test
the parts of this task that do not touch expressions — chart and tensor
construction, valence and rank bookkeeping, index arithmetic, symmetry fill and
validation on placeholder components, and the resource-limit unwind path — and
to land the expression-dependent operations once it is. Do not unblock yourself
by inventing a stand-in expression type; that is precisely the competing
ownership model this section exists to prevent.

**This is what happened.** The typed IR landed; the scalar CAS layer had not,
so the storage half was built and tested against the IR alone and the index
operations were left for the scalar header. `docs/TENSOR.md` records the split
and the exact point the boundary falls on.

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

Required posture. Expression storage and its cap belong to the IR; the first two
items are the budget this task supplies as input to that sizing, and the rest are
this task's own obligations:

- report ~30 KB steady state and an unmeasured peak as the dimension-4 sizing
  input, and take the arena and live-node cap from the IR rather than declaring
  a private one;
- the tensor core's own allocations — the dense component tables, about 1.5 KB
  per curvature pass — are bounded at construction from the chart's dimension
  and rank, with no allocation in the component loops;
- when the IR reports the cap exceeded, return a clean resource-limit status and
  unwind: no tensor left partially written, every handle acquired in the failed
  operation released. `docs/SCIENTIFIC_SCOPE.md` requires explicit error values
  for resource limits, and this is where that starts.

Instrument the peak and record it. Replacing the estimate above with a measured
number is part of the deliverable, and it is the number the IR's cap should
ultimately be set from.

## Complexity

At `n <= 4` every operation here is small; the table is to establish that op
count is not the bottleneck.

| Operation | Elementary ops | At `n = 4` |
| --- | --- | --- |
| raise or lower one slot of rank `r` | `n^(r+1)` | 1,024 at `r = 3` |
| contract two slots of rank `r` | `n^(r-1)` | 64 at `r = 3` |
| componentwise derivative, rank `r` | `n^r` | 256 |

The cost is dominated by expression simplification, which is superlinear in tree
size, not by these counts. Optimize how many times the IR's simplify capability
is invoked and where those calls sit, not the loops.

## Deterministic tests

All tests must be exact and reproducible; no floating point, no tolerances.

Status after the storage half. Tests 3 and 6 are done, and 7 is done for the
allocation path; 1, 2, 4 and 5 all compare expressions and so wait on the zero
decision. `tests/test_tensor.c` carries the four that landed, 12,377 checks.

| test | state |
| --- | --- |
| 1 Kronecker round trip | waits on scalar CAS (needs the inverse metric and the zero decision) |
| 2 raise/lower involution | waits on scalar CAS |
| 3 symmetry fill agreement | **done**, at `n = 2` and `n = 4` |
| 4 first Bianchi identity | waits on scalar CAS |
| 5 contraction against known traces | waits on scalar CAS |
| 6 dimension independence | **done**, every structural test runs at `n = 2, 3, 4` |
| 7 resource limit | **allocation half done** — injected failure swept across every allocation; the IR node-cap half waits on scalar CAS, since only expression construction can reach it |

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

### Measured after the storage half

- The tensor core's own dense tables, which were budgeted at about 1 KB for a
  rank-4 tensor at `n = 4`, measure **1,536 bytes**: 1,024 of handles plus 256
  of signs plus 256 of assignment flags. The two extra tables are what a fill
  discipline that never forms a negated expression costs. Pinned by
  `test_storage_is_bounded`.
- ARM text for the layer, `-Os -marm`: **5,168 bytes** (`chart.o` 552,
  `symmetry.o` 1,116, `tensor.o` 3,500). None of it reaches
  `dist/phy-nspire.tns` yet — nothing calls it, so `--gc-sections` drops it,
  exactly as with the IR.
- Peak *arena* usage still cannot be measured. It is dominated by expression
  storage during a curvature pass, and no expression is formed until the
  scalar CAS layer lands. The estimate above stands until then.

## Dependencies

Depends on the handle-based typed IR, and on the canonical scalar CAS layer for
the capabilities marked "scalar CAS" in the table above. Bind to
`include/phy/ir.h` as described; if a dependency is not yet available, follow the
sequencing note in that section rather than substituting a stand-in.

`docs/agent-tasks/GR_CURVATURE.md` depends on this task and should not start
before tests 1, 2 and 5 pass.
