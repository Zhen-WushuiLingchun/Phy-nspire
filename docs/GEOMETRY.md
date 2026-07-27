# Manifolds and differential forms

The exterior-calculus half of [`docs/SCIENTIFIC_SCOPE.md`](SCIENTIFIC_SCOPE.md)
§3. It is defined by `include/phy/geom.h` and implemented in `src/geom`, on top
of the component tensor core ([`docs/TENSOR.md`](TENSOR.md)) and the scalar CAS
([`docs/CAS.md`](CAS.md)).

This document covers the design decisions and the conventions. The header is
the API reference and is not repeated here.

## What has landed, and what has not

| Landed | Deferred, with the blocking dependency named |
| --- | --- |
| manifold metadata: name, dimension ≤ 4, orientation, signature | pullback — needs a validated coordinate-map object |
| bounded list of borrowed, validated charts | transition maps between registered charts |
| canonical antisymmetric `C(n,p)` component storage | Lie derivative and pullback |
| exact wedge product | Lie derivative, Lie bracket |
| exact exterior derivative | connection, torsion, curvature 2-forms |
| exact interior product by a contravariant vector | integration, Stokes, cohomology |
| orthonormal and general coordinate-metric Hodge/volume | frames and tetrads other than a coordinate coframe |
| linear structure, copy, and the zero/equality decisions | |

Nothing here is a stub. Every entry in the left column is implemented, tested,
and bounded; every entry in the right column is absent rather than
half-present, and the paragraph that names its blocking dependency is in the
header next to the operation it would have extended.

## Storage: antisymmetry *is* the representation

`phy_tensor` stores a dense `n^r` table and maintains antisymmetry through a
declared symmetry group and a fill discipline. A form does not, because it does
not have to. A degree-`p` form is exactly its strictly increasing components in
lexicographic order:

```
alpha = sum over i_0 < ... < i_{p-1} of
        alpha_{i_0...i_{p-1}} dx^{i_0} ^ ... ^ dx^{i_{p-1}}
```

A component with a repeated index does not exist, and a component whose indices
are out of order is reached through `phy_form_get`, which applies the sort
parity. There is no orbit to fill, no sign table to keep consistent, and no
`phy_tensor_check_symmetries` equivalent, because there is no redundancy to
check.

The table is at most `max_p C(4,p) = C(4,2) = 6` handles, so it is an inline
member rather than an allocation:

| object | bytes (host, 64-bit) |
| --- | --- |
| `struct phy_manifold` | 72 |
| `struct phy_form`, any degree | 72 |

A form is one `phy_alloc` of a fixed size — `test_storage_is_bounded` asserts
both the single allocation and the ceiling — which is what makes it reasonable
for an operation to return a freshly allocated result rather than writing into
a caller-supplied one. A curvature loop pays a constant per intermediate.

### Why this layer forms expressions and `src/tensor` does not

`docs/TENSOR.md` draws its boundary at negation: an antisymmetric orbit is one
handle and two signs precisely so that nothing in `src/tensor` has to build
`-x`. That boundary was drawn because no scalar layer existed at the time.

One does now, so this layer sits on the other side of it. Folding a sort parity
into a component, summing the shuffles of a wedge, and differentiating a
component are all `phy_cas_*` calls. The consequence worth stating: every
guarantee the CAS makes applies here without this layer restating it. The step
budget bounds a wedge, the cancellation hook interrupts an exterior derivative,
exact arithmetic means `PHY_ERR_OVERFLOW` rather than a wrapped `int64`, and
the memo cache means a shared subterm is simplified once however many
components mention it. `test_cancellation_and_budget` checks the propagation
rather than assuming it.

## Charts are registered, not related

A manifold borrows up to `PHY_MANIFOLD_MAX_CHARTS` charts. It validates each
one — matching dimension, same IR context, not already registered — and then
does nothing further with it. There are no transition maps.

A form therefore names its chart, and every binary operation requires its
operands to agree on it. Mixing charts is `PHY_ERR_TYPE`.

That refusal is the honest form of the missing feature. Two components on two
charts are related by a transition map, and identifying them without one is
not a convenience, it is a wrong answer. `test_charts_do_not_mix` pins it.

### Why pullback is deferred

`F*(dy^a) = sum_j (d phi^a / d x^j) dx^j` is not hard to compute — the CAS has
`phy_cas_diff` and `phy_cas_substitute`, and the components are small. What is
missing is a *safe* object to compute it from. Two requirements make a
coordinate map a real design rather than a call to `phy_cas_substitute`:

- **Disjoint coordinate symbols.** Substituting the target chart's coordinates
  with expressions in the source chart's coordinates captures the source ones
  whenever the two charts share a name. Both `{"x","y"}` charts in this
  repository's own tests would collide. Nothing detects it; the result is
  simply wrong. A map object has to own the disjointness, either by
  construction or by rejecting an overlap.
- **Validation before differentiation.** A map component that mentions a symbol
  belonging to neither chart differentiates to an unevaluated
  `PHY_IR_DERIVATIVE`, which then propagates into every pulled-back component
  as something that *looks* like a legitimate deferred answer. A malformed map
  should be one typed error at construction, not a residue in the output.

Neither is difficult; both are design, not effort. Until a `phy_map` supplies
them, this layer has no pullback and says so, rather than shipping one that is
right for the cases its tests happen to use.

## Conventions

Fixed here so that the implementation, the test suite, and any oracle cannot
disagree silently. They are the conventions SageManifolds and Cadabra use, so a
disagreement with either is a real bug rather than a normalization mismatch.

Slots are ordered left to right from zero, matching
[`docs/references/TENSOR_GEOMETRY.md`](references/TENSOR_GEOMETRY.md).

### Wedge

On strictly increasing components,

```
(alpha ^ beta)_K = sum over the shuffles I ⊔ J = K of sgn(I,J) alpha_I beta_J
```

with `I` increasing of length `p`, `J` increasing of length `q`, and `sgn(I,J)`
the parity of the permutation carrying the concatenation `(I, J)` to `K`. This
is the determinant convention: for two 1-forms,

```
(alpha ^ beta)_{01} = alpha_0 beta_1 - alpha_1 beta_0
```

The shuffle sign is computed by sorting the concatenated *slot positions within
K* rather than the index values. The two have the same parity because `K` is
increasing, and the positions are what the enumeration already has in hand.

The convention is checked, not assumed: `test_wedge_graded_commutativity`
requires `alpha ^ beta = (-1)^{pq} beta ^ alpha` at every admissible degree pair
in dimensions 2, 3 and 4, and `test_wedge_associativity` requires
`(alpha ^ beta) ^ gamma = alpha ^ (beta ^ gamma)` at four degree splittings.
Both run on forms whose components are distinct symbols, so two of them
agreeing is evidence rather than coincidence.

### Exterior derivative

```
(d alpha)_{i_0...i_p} = sum_m (-1)^m d/dx^{i_m} alpha_{i_0...^i_m...i_p}
```

Components are differentiated with `phy_cas_diff` against the chart's
coordinate symbols. A component the CAS cannot see through — an unknown
function, a tensor, an operator — becomes an unevaluated `PHY_IR_DERIVATIVE`
rather than a wrong zero, which is `docs/CAS.md`'s distinction propagated
unchanged.

### Interior product

```
(iota_v alpha)_{j_1...j_{p-1}} = v^i alpha_{i j_1 ... j_{p-1}}
```

`v` is a rank-1 `phy_tensor` with an upper slot on the same chart. Reusing the
tensor core rather than inventing a vector type keeps its storage, its zero
handle and its validation shared; a vector field *is* a rank-1 contravariant
tensor and this layer has no reason to disagree. A covariant slot, a rank other
than 1, or another chart is `PHY_ERR_TYPE`.

It is an antiderivation of degree −1, and the suite requires both halves of
that: `iota_v iota_v = 0`, and

```
iota_v(alpha ^ beta) = (iota_v alpha) ^ beta + (-1)^p alpha ^ (iota_v beta)
```

### Hodge dual

```
(*alpha)_J = orientation * sgn(I,J) * (prod_{i in I} sigma_i) * alpha_I
```

with `J` a strictly increasing `(n-p)`-tuple, `I` its increasing complement in
`0..n-1`, `sgn(I,J)` the parity of `(I, J)` as a permutation of `(0,...,n-1)`
— so `epsilon_{01...n-1} = +1` — and `sigma_i` the signature entry on axis `i`.

The `sigma` factors are index raising. With a diagonal metric,
`alpha^{i_1...i_p} = sigma_{i_1} ... sigma_{i_p} alpha_{i_1...i_p}` with no sum,
so the whole of raising is a per-index sign. `sqrt(|det g|)` is 1 in an
orthonormal coframe and does not appear.

The identity that pins it is

```
** = (-1)^{p(n-p)} sign(det g)      on degree-p forms,
```

with `sign(det g) = (-1)^q = phy_manifold_metric_sign`. `test_hodge_star_square`
checks it at every degree across eight configurations: Euclidean and Lorentzian
2D, Euclidean 3D, Euclidean and Minkowski 4D, in both orientations. An epsilon
with the wrong sign, or an index raised with the wrong signature entry, survives
every other test in the file and fails this one. Orientation cancels in `**`,
as it must, which is why both orientations are run.

The familiar special cases fall out and are checked directly: `** = -1` on
1-forms in Euclidean 2D, `** = +1` on 1-forms in Lorentzian 2D, and `** = -1`
on 2-forms in Minkowski 4D — the last being why `*F` and `**F = -F` behave as
they do for the electromagnetic field.

## Two Hodge paths

`phy_form_hodge` is the cheap path for the diagonal orthonormal metric declared
by the manifold:

```
g = sum_a sigma_a (dx^a ⊗ dx^a),    sigma_a in {-1, +1}
```

The orthonormal assertion is a caller contract. Curvilinear and general GR
coordinates use `phy_form_hodge_metric`, which accepts a symmetric covariant
rank-2 `phy_tensor` on the same chart and evaluates

```text
(*alpha)_J = orientation sqrt(|det g|) epsilon_{I J} alpha^I
alpha^I = g^(i_1 k_1) ... g^(i_p k_p) alpha_K .
```

The tensor layer supplies the exact inverse and determinant. The manifold's
declared inertia supplies `sign(det g)`, so `|det g|` is formed without numeric
sampling. Its square root stays an exact rational power if the scalar CAS
cannot reduce it. `phy_form_volume_metric` is the corresponding general volume
form. Wedge, exterior derivative, and interior product remain
metric-independent.

When the determinant is an exact integer or rational, the implementation also
checks its sign against the manifold's declared inertia and rejects a
contradiction with `PHY_ERR_ASSUMPTION`. For a symbolic determinant the
inertia remains an explicit caller assumption: the bounded scalar CAS does not
pretend to be a general real-domain sign solver.

## Degrees outside `0..n`

`Lambda^p` is the zero vector space for `p > n`, and this layer has no object
for it. So:

- creating a form of degree greater than the dimension is
  `PHY_ERR_INVALID_ARGUMENT`;
- `alpha ^ beta` with `p + q > n` is `PHY_ERR_DOMAIN`;
- `d alpha` of an `n`-form is `PHY_ERR_DOMAIN`;
- `iota_v alpha` of a 0-form is `PHY_ERR_DOMAIN`.

In all three operation cases the value is *zero*, not undefined, and the header
says so at each one. The alternative — a form object with zero components — was
rejected because it makes every position accessor fail on an object that is
otherwise well formed, and because it pushes the ceiling question outward
rather than answering it: two 4-forms would wedge to degree 8. A caller for
which these cases are reachable compares degrees against
`phy_form_dimension` first.

## The normal form, and where `d^2 = 0` is structural

Components are stored **simplified but not expanded**, which is
`phy_cas_simplify`'s contract: structure is preserved, so a product of sums
stays a product of sums.

That has a visible consequence for nilpotence. `d(d alpha)` is componentwise
`S + (-1) * S` for each mixed partial `S`. When `S` is a single monomial the two
orders reach the same interned node, the sum collects, and the component is the
canonical zero handle on the nose — `test_d_squared_is_structural`. When `S` is
itself a sum, the IR flattens `S`'s terms into the enclosing sum while the
negation stays a product, so the terms no longer share a non-numeric part and
the collection does not fire. The component is then a syntactically nonzero
expression whose value is zero.

`phy_form_is_zero` decides it correctly, because `phy_cas_is_zero` expands
first and its answer is a proof on this class rather than an estimate.
`test_d_squared_is_zero` uses that decision at every degree from `n = 2` to
`n = 4`, on components built from monomials and sines of the coordinates.

This is a property of the normal form, not of this layer, and expanding here
was rejected: `phy_cas_expand` is bounded by the IR's term limit and can fail on
expressions a curvature pass would legitimately produce, and it would discard
the structure `docs/CAS.md` deliberately preserves for the reader.

### Where `d^2 = 0` genuinely does not hold

For a component the CAS must defer on, `phy_cas_diff` appends variables to a
`PHY_IR_DERIVATIVE` in the order they are applied, and the IR preserves that
order by design — mixed partials commute only for sufficiently smooth
expressions, and `include/phy/ir.h` puts that assumption in the rewriter rather
than in construction. So `d^2` of an opaque component is a difference of two
derivative nodes that differ only in variable order, and `phy_form_is_zero`
reports `PHY_CAS_UNKNOWN`.

That is the honest answer. `test_d_squared_defers_on_opaque_components` pins
it, so the boundary is characterized rather than discovered later. Closing it
needs a declared smoothness assumption and a canonical variable order in the
CAS; neither exists yet and neither belongs in this layer.

## The decisions

`phy_form_is_zero` and `phy_form_equivalent` combine per-component decisions
conservatively: `PHY_CAS_ZERO` only when every component was proved,
`PHY_CAS_NONZERO` as soon as one is proved otherwise, and `PHY_CAS_UNKNOWN` in
between. A form is never reported as vanishing because a component could not be
decided.

`phy_form_equivalent` follows `phy_cas_equivalent`'s spelling, where
`PHY_CAS_ZERO` is the affirmative answer, because it decides `left - right`.

One consequence surprises callers and is therefore tested: two forms whose
components are distinct symbols come back `PHY_CAS_UNKNOWN`, not
`PHY_CAS_NONZERO`. `phy_cas_is_zero` proves *nonzero* only for an exact number
or a product of factors declared nonzero, and `a0 - b0` is neither.

## Errors

Typed values, uniform across the API, and no new `phy_status` value was needed:

| status | when |
| --- | --- |
| `PHY_ERR_INVALID_ARGUMENT` | null pointer, axis or position out of range, signature entry other than ±1, unknown orientation, chart already registered, chart or CAS from another IR context, degree greater than the dimension |
| `PHY_ERR_UNSUPPORTED` | dimension or chart count beyond the compiled ceilings — `n = 5` is not wrong, it is not implemented |
| `PHY_ERR_TYPE` | operands disagree on manifold, chart, or degree; a vector field is not a rank-1 contravariant tensor |
| `PHY_ERR_DOMAIN` | the result's degree leaves `0..n` |
| `PHY_ERR_ASSUMPTION` | the Hodge dual or volume form on an unoriented manifold; a nonzero assignment through a repeated index |
| `PHY_ERR_OUT_OF_MEMORY` | `phy_alloc` failed |
| `PHY_ERR_TIMEOUT`, `PHY_ERR_INTERRUPTED`, `PHY_ERR_OVERFLOW`, … | propagated unchanged from the CAS |

Every failing call leaves its output parameter untouched, destroys any
partially built result, and modifies no operand. Operations create the result
first and fill it afterwards, so the unwind is one `phy_form_destroy` on a path
that has already decided to fail.

Two of these are worth a sentence each because the choice was not forced.

**The unoriented Hodge dual is `PHY_ERR_ASSUMPTION`, not
`PHY_ERR_UNSUPPORTED`.** `*alpha` is not merely uncomputed on an unoriented
manifold; it does not exist until a volume form is chosen. The caller has to
declare something, not wait for a later release.

**A nonzero assignment through a repeated index is `PHY_ERR_ASSUMPTION`, not
`PHY_ERR_DOMAIN`.** `alpha_{aab}` vanishes by antisymmetry, so an assignment
saying otherwise contradicts the storage. A value that merely cannot be
*decided* zero is refused too, because accepting it would silently discard it.

## Testing

`tests/test_geom.c` has 4,581 checks in 31 cases. The separate
`tests/test_geom_metric.c` adds diagonal, non-diagonal, volume, singular, and
orientation cases for the general-metric path.

The identity tests are the substance. A wedge product with the wrong
normalization still produces plausible components and a Hodge dual with the
wrong epsilon still produces a form of the right degree; what neither survives
is graded commutativity, associativity, nilpotence, the two Leibniz rules and
`**` simultaneously, in more than one dimension and both signatures. The worked
R² and R³ examples exist to catch a convention that is self-consistently wrong
— one that satisfies every identity but disagrees with a textbook — and they
are checked as serialized normal forms, which pins the exact shape.

Structural coverage: manifold metadata and every rejection path; chart
registration to the ceiling and past it; form shapes at every degree in
dimensions 1 through 4; exhaustive position round trips; the sort parity and
landing position of *every* index tuple, not only the increasing ones, checked
against an independently computed inversion count; component access through
reversed and repeated tuples; the linear structure and both decisions; the
transactional rejection paths of all four operations; cancellation and step
budget propagation; and the allocation-failure sweep.

Dimension independence is honoured throughout, for the reason
`docs/TENSOR.md` gives: every test that can run at more than one dimension
does.

The allocation sweep walks an injected failure across every allocation the
workload makes, and after each one requires a typed status, no object handed
back, `phy_ir_validate` and `phy_cas_validate` both passing, and
`phy_telemetry` back at its baseline.

## Device build

In the device source list, alongside the IR, the CAS and the tensor core. As
with all three, **none of it will reach `dist/phy-nspire.tns`**: nothing in the
current application calls it, so `--gc-sections` discards every symbol.

The ARM compile and link check were run on the pinned Ndless r2022 toolchain on
2026-07-27.

`make geom-link-check` closes the gap the same way `make cas-link-check` does.
It links `tests/device/geom_link_probe.c`, which touches every public entry
point, with `--gc-sections` included — the point being that these symbols
survive collection because they are genuinely referenced — derives the expected
symbol set from `include/phy/geom.h` rather than listing it, and checks that all
entry points are retained, that the image packages to a real `.tns`, and
that no float formatter, libm call or soft-float helper was dragged in.

That last check is held to the CAS's stricter standard rather than the IR's,
and deliberately: this is the first physics module to call the CAS, and a form
operation that reached `libm` would defeat the reason the CAS computes only in
exact rationals.

The probe retained 44/44 public APIs, compiled the geometry translation units
to 8,577 bytes of ARM text, packaged a 48,400-byte dependency-complete `.tns`,
and retained no float formatter, libm call, or ARM soft-float helper.

## Not in this layer

Pullback and transition maps, for the reason above. Lie derivatives and Lie
brackets. Connections, torsion, and curvature 2-forms — the Cartan structure
equations are the natural next step and need only the wedge and the exterior
derivative, both of which are here. Integration, Stokes' theorem, and anything
cohomological. Frames and tetrads other than a chart's coordinate coframe.
Dimensions above 4.
