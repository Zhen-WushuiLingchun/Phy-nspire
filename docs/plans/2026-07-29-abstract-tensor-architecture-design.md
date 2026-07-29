# Abstract Tensor and Differential Geometry Architecture

**Status:** accepted for implementation on 2026-07-29

## Goal

Add a real abstract-index tensor algebra to Phy-nspire without regressing the
validated component GR engine.  The finished architecture must distinguish
abstract tensor identities from coordinate components, remove semantic
rank/dimension-four limits, canonicalize free and dummy indices under declared
slot symmetries, support multi-term Young relations, and connect the result to
matrices, atlases, transition maps, pullbacks, GR, Lie algebra, and QFT.

“Arbitrary” means that rank, dimension, matrix shape, and slot count are
runtime values rather than compile-time constants.  Every operation remains
bounded by explicit byte, term, permutation, and step budgets on Ndless.

## The two tensor layers

### Abstract layer

An abstract tensor is independent of coordinates and component arrays:

```text
IndexSpace[M, Dimension -> 4, Metric -> g]
TensorHead[R, {Down[M], Down[M], Down[M], Down[M]}, RiemannSymmetry]
R[Down[a,M], Down[b,M], Down[c,M], Down[d,M]]
```

Its representation records:

- typed index spaces and optional known or symbolic dimensions;
- free and dummy index occurrences, variance, and metric policy;
- tensor heads, slot types, commutation grade, and signed slot symmetry groups;
- tensor monomials with an exact scalar coefficient and ordered factor records;
- sums of canonical monomials.

It never allocates `dimension^rank` components.  Abstract rank is bounded only
by the canonicalizer's configured slot budget.

### Component layer

A component object has an explicit basis or chart and concrete shape:

```text
Vector[{v0, v1, ...}]
Matrix[{{a11, a12, ...}, ...}]
ComponentTensor[M, {Down, Up, ...}, components]
```

The component layer owns exact scalar entries and performs linear algebra,
contraction, basis changes, and coordinate differentiation.  It supports dense
storage for small arrays and sparse canonical-component storage when the dense
product is unsuitable.  The existing `phy_tensor` API remains operational
during migration and becomes a compatibility facade over the dynamic
component representation.

## Shared foundations

Both layers use the existing typed IR, arbitrary-precision exact numbers,
algebraic numbers, scalar simplifier, evaluator budgets, cancellation, and
allocation telemetry.  Neither Lorentz, Dirac, colour, Lie algebra, nor GR may
keep an independent definition of free/dummy index validity after migration.

The linear-algebra foundation is a dynamic rectangular `phy_matrix`; a vector
is a one-column matrix view with vector-specific convenience functions.  Its
first exact operations are construction, indexing, addition, scalar
multiplication, matrix multiplication, transpose, determinant, fraction-free
RREF, rank, null space, exact linear solve, and inverse.  Characteristic
polynomials and exact eigen data follow without changing the storage contract.

## Abstract data model

The public API is split into four opaque object families:

```c
typedef struct phy_index_space phy_index_space;
typedef struct phy_abstract_tensor_head phy_abstract_tensor_head;
typedef struct phy_tensor_monomial phy_tensor_monomial;
typedef struct phy_tensor_expression phy_tensor_expression;
```

An index occurrence contains an interned name, an index-space identity, and
variance.  Free/dummy status is derived from a complete monomial census; it is
not stored on the occurrence.  A tensor head contains a runtime slot count,
one index-space requirement per slot, and a signed permutation group.  A
monomial contains a scalar coefficient plus factors; each factor points to a
head and owns its ordered index occurrences.

Construction rejects:

- a factor whose index space does not match its head's slot type;
- a repeated free index;
- a dummy appearing other than exactly twice;
- a non-metric dummy with equal variance;
- an ambiguous metric contraction without an explicit metric policy;
- incompatible free-index signatures across terms of a sum.

## Monoterm canonicalization

Canonicalization follows the Butler–Portugal double-coset formulation:

1. validate and classify the complete index census;
2. encode factor slots and the current index assignment as a permutation;
3. construct the signed slot group from tensor-head symmetries and exchange of
   identical commuting factors;
4. construct the index group from dummy-pair renaming, allowed pair flips,
   repeated component labels, and metric symmetry;
5. build a base and strong generating set with bounded Schreier–Sims;
6. find the lexicographically minimal double-coset representative;
7. detect a representative reachable with both signs and return exact zero;
8. rename dummy pairs deterministically and rebuild the typed IR.

Permutation, BSGS, orbit, transversal, and double-coset working memory comes
from a caller-owned bounded arena.  Resource exhaustion returns a typed limit
status and never produces a partially canonical answer.

The official xPerm 1.3.0 `xperm.c`, verified at SHA-256
`7a6c5f600868a3922668b020a15c0692f76574ff2a559808c62d460cef1b07be`,
is retained outside the repository as a read-only oracle.  Its heap-driven
MathLink implementation is not linked into the device binary.  SymPy
`tensor_can` provides a second independent host oracle.

## Multi-term identities

Signed slot permutations handle monoterm identities only.  First Bianchi,
cyclic Jacobi-like relations, dimension-dependent Schouten identities, and
general representation constraints are linear relations between distinct
monomials.

The second canonical layer therefore stores Young shapes/tableaux and builds
bounded projection or relation matrices over exact rationals.  It reduces
canonical monoterms against a deterministic row-echelon basis.  Built-in
Riemann symmetry combines the monoterm signed slot group with the
`R[a,b,c,d] + R[a,c,d,b] + R[a,d,b,c] = 0` multi-term relation.  Young
reduction is opt-in per tensor head and reports a resource-limit status when
the representation is too large for the configured arena.

## Abstract/component bridge

`ComponentValue[expression, basis]` is the only general downward conversion.
It resolves every index space to a basis, expands free slots, contracts dummy
slots, and returns a component tensor or scalar.  Conversion is lazy and
iterates independent components when symmetry data permits.

Component expressions are not automatically “lifted” back to abstract
tensors.  Reconstruction requires an explicitly supplied tensor head and a
successful transformation-law check; otherwise it remains a component object.

## Manifolds and atlases

A manifold owns dimension, orientation, signature metadata, index spaces, and
an atlas.  A chart owns coordinate symbols and a declared domain.  A
transition map owns source/target chart identities and one exact target
coordinate expression per target axis.

Registration validates shape, symbol scope, and—where possible—the two-sided
composition of inverse maps.  Jacobians are exact matrices.  Pullback,
pushforward, tensor basis transformation, exterior derivative, Hodge dual,
connection, curvature, and covariant derivative consume this shared map
object.  Mixing charts without a registered transition remains a typed error.

## Evaluator surface

The Mathematica-style surface will expose:

```text
Vector, Matrix, Transpose, Det, Inverse, RowReduce, Rank, NullSpace, LinearSolve
IndexSpace, Index, TensorHead, Symmetry, YoungSymmetry
TensorCanonicalize, ContractMetric, RaiseIndex, LowerIndex
ComponentValue, ChangeBasis
Manifold, Chart, TransitionMap, Pullback, Pushforward
```

Existing `LorentzIndex`, Dirac, colour, Lie, GR, and Yang–Mills heads will
lower into the same abstract objects.  Reader-facing aliases may remain, but
there will be one index census and one canonicalizer.

## Verification gates

The work is accepted only when all of the following hold:

- dynamic exact matrix/vector tests cover rectangular and dimensions above 4;
- abstract tensors support runtime rank above 4 without component allocation;
- dummy alpha-renaming, factor order, and construction order do not change the
  canonical serialization;
- symmetric, antisymmetric, Riemann, metric, multiple-index-space, and
  identical-factor cases match xPerm and SymPy oracle fixtures;
- sign collisions return exact zero;
- Young reduction proves first Bianchi identities unavailable to the monoterm
  canonicalizer;
- transition maps and pullbacks pass identity, composition, and coordinate
  change fixtures;
- existing scalar, tensor, GR, geometry, QFT, notebook, sanitizer, ARM symbol,
  size, and device smoke gates continue to pass.

## Delivery order

The dependency order is:

1. dynamic exact matrices and vectors;
2. abstract index spaces, heads, factors, and monomial validation;
3. signed permutation groups and BSGS;
4. monoterm Butler–Portugal canonicalization;
5. expression collection and Young multi-term reduction;
6. dynamic component tensors and the abstract/component bridge;
7. transition maps, pullback/pushforward, and multi-chart geometry;
8. migration of GR, differential forms, Lorentz, Dirac, Lie, colour, and QFT;
9. notebook/evaluator command surface and device acceptance.
