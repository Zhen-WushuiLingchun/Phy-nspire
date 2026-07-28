# Component tensor core

The substrate the Phase 3 curvature pipeline computes on. It is defined by
`include/phy/tensor.h` and implemented in `src/tensor`. The task contract is
`docs/agent-tasks/TENSOR_CORE.md`; the reference pack is
`docs/references/TENSOR_GEOMETRY.md`.

This document covers the design decisions. The header is the API reference and
is not repeated here.

## Notebook construction surface

The reader-facing evaluator can construct every supported dense component
shape:

```text
scalar = ComponentTensor[M, {}, s]
vector = ComponentTensor[M, {Up}, {v0,v1}]
mixed  = ComponentTensor[M, {Down,Up}, {{a,b},{c,d}}]
rank4  = ComponentTensor[M, {Down,Down,Up,Up}, components]
```

The component list has one nesting level per slot and every extent equals the
manifold dimension. Rank and variance are independent: any `Up`/`Down` pattern
is valid at ranks 0 through 4. The compiled scope remains dimensions 1 through
4 and ranks 0 through 4 (at most 256 dense components), so “arbitrary” means
every combination inside that explicit device bound, not unbounded tensor rank.

## What has landed, and what has not

The original storage slice and its scalar-dependent operations now share one
native tensor API.

| Landed | Deliberately deferred |
| --- | --- |
| charts, coordinate symbols, rank, valence, head metadata | dimensions above 4 or ranks above 4 |
| dense `n^r` storage, encode/decode, signed slot symmetries | abstract dummy-index canonicalization |
| exact contraction, inverse metric, raise/lower, component derivatives | first-Bianchi orbit canonicalization |
| canonical lookup, fill validation, allocation-failure unwind | optional xPerm integration |

The scalar-dependent entries use the native exact CAS and its three-valued zero
decision; they are not numerical fallbacks. The deferred abstract-index work is
different from dense component arithmetic and is left absent rather than
represented by a misleading no-op head.

## The scalar boundary, and why it falls on negation

The natural place to draw the line would have been "storage is here,
arithmetic is elsewhere", but that line does not survive contact with
antisymmetry. Filling an antisymmetric orbit means writing `-x` into the
mirrored component, and forming `-x` is arithmetic.

So the boundary is drawn one step further out. A stored component is a pair:

```c
typedef struct {
    phy_ir_ref ref;
    int8_t sign;      /* -1, 0, or +1 */
} phy_tensor_component;
```

read as `sign * ref`. An antisymmetric orbit is one handle and two signs, not
two expressions. Nothing in `src/tensor` calls an IR builder except
`phy_ir_intern` and `phy_ir_integer(ctx, 0)` for the canonical zero.

Three things fall out of this that are worth stating:

- **The layer is exactly testable as it stands.** Every guarantee it makes is
  structural, so every guarantee is checked, and there is no half-implemented
  arithmetic waiting for a dependency.
- **A sign of 0 is meaningful.** It marks a component the declared symmetries
  force to vanish — `R_aacd` when the first pair is antisymmetric. That is a
  fact about the group, not about any assignment, so it survives clearing.
- **The scalar layer's job here is small and well defined.** Folding a sign
  into a handle is one multiplication by `-1` followed by a normalization. The
  index operations sit on top of that, not on top of a rewritten storage
  layer.

## Storage

Dense `n^r`, as the task contract requires, and deliberately not packed. At
`n = 4, r = 4` that is 256 components in a single allocation:

| table | bytes at `n = 4, r = 4` |
| --- | --- |
| `values` — one `phy_ir_ref` per component | 1,024 |
| `signs` — one `int8_t` per component | 256 |
| `assigned` — one flag per component | 256 |
| **total** | **1,536** |

The contract budgets "about 1 KB at 4 bytes per handle" for the handles alone.
The extra 512 bytes are the sign table, which is what buys a fill discipline
that never forms a negated expression, and the assigned table, which is what
makes assignment *validation* possible rather than just assignment. Both are
measured by `test_storage_is_bounded`.

Every vanishing component shares one interned zero handle, so a mostly-zero
tensor costs one node however large the table is —
`phy_ir_node_count` does not move when a second rank-4 tensor is created.

The tables are sized from the chart and the rank at construction and never
grow. No loop in this layer allocates.

## Symmetry is a group, not a list of pairs

The Riemann tensor forced this. Its three slot symmetries are antisymmetry in
`(0,1)`, antisymmetry in `(2,3)`, and *exchange of the two pairs* — and the
third is not a transposition, so an API that only took slot pairs could not
express it. The IR's own `phy_ir_declare_symmetry` has exactly that shape and
is therefore not what this layer uses.

A declaration is a permutation `p` of the slots and a sign `s`, meaning

```
T[p . a] = s * T[a]        for every index tuple a
```

with the action placing the index at slot `i` into slot `p[i]`, i.e.
`(p . a)[p[i]] = a[i]`. Images notation, matching the convention fixed in
`docs/references/TENSOR_GEOMETRY.md`, so a disagreement with `xperm.c` or
SymPy's `tensor_can` would be a real bug rather than a notation mismatch.

That is a left action — `p . (q . a) = (p ∘ q) . a` with
`(p ∘ q)[i] = p[q[i]]` — so declarations generate a group and the signs are a
homomorphism onto `{+1, -1}`. The group is closed at declaration time, which
is what makes a contradiction a declaration-time error rather than a fill-time
surprise. Rank is at most 4, so the group is at most `4! = 24` elements and
lives inline in the tensor.

Two consequences are load-bearing and neither is obvious from the formula:

- **An odd stabilizer forces a component to vanish.** If some `g` fixes a
  tuple and carries sign `-1`, then `T[a] = -T[a]`. This is why the diagonal
  of an antisymmetric tensor is zero, and why it is zero *by construction*
  rather than by anyone remembering to write zero there.
- **Reaching the identity with sign `-1` makes the whole tensor vanish.**
  Declaring a pair both symmetric and antisymmetric does this. It is
  consistent, not malformed — the caller may have derived both facts
  legitimately — so it is reported through
  `phy_tensor_is_identically_zero` rather than rejected.

### Independent component counts

`phy_tensor_independent_count` counts orbits that are not forced to vanish.
For the Riemann slot group that is `N(N+1)/2` with `N = n(n-1)/2`:

| `n` | slot symmetries | `n^2(n^2-1)/12` |
| --- | --- | --- |
| 2 | 1 | 1 |
| 3 | 6 | 6 |
| 4 | 21 | 20 |

The familiar `n^2(n^2-1)/12` additionally uses the **first Bianchi identity**,
which is a cyclic relation among three *different* components rather than a
permutation of one component's slots. It is not a slot symmetry and does not
belong in this group. The two agree at `n = 2` and `n = 3` and differ at
`n = 4`, which is exactly the kind of coincidence that would let a wrong
implementation look right on `sphere_2d`; the test suite checks all three.

### Variance is enforced

A declared permutation may only exchange slots of equal variance. Swapping an
upper slot with a lower one is not a relation between components at all, and
declaring it is precisely how `R^a_bcd` would acquire the symmetries that
belong to `R_abcd`. `phy_tensor_declare_riemann_symmetry` on the mixed form is
therefore `PHY_ERR_TYPE`, not silently accepted.

## Fill and validation

`phy_tensor_set` writes the whole orbit: the same handle into every component,
each with its group element's sign. One call populates all the dependents, so
"fill" is not a separate phase a caller can forget.

The ownership rule is deliberately simple: a tensor borrows its chart, and a
chart borrows its IR context. Destroy tensors first, then charts, then the IR
context. Likewise, a `phy_ir_ref` is valid only in the context that issued it;
the compact handle carries no runtime context tag, so passing a ref from
another context is a caller-contract violation rather than an error this layer
can reliably diagnose.

The orbit is validated in full before anything is written, so a rejected
assignment leaves the tensor exactly as it was. Two things are rejected:

- assigning a non-zero handle to a component the symmetries force to vanish;
- assigning into an orbit that already holds a *different* assignment.

The second is what makes this validation rather than overwriting. It is
**structural**: the IR interns, so equal handles are exact structural
equality, but two spellings of the same value are currently a conflict.
Equality up to simplification needs the scalar layer. `phy_tensor_clear` and
`phy_tensor_clear_component` release an orbit for reassignment.

Declaring a symmetry after any component is assigned is `PHY_ERR_ASSUMPTION`.
The existing assignments were validated against a different group, and
silently reinterpreting them is how a fill discipline turns into a fill bug.

`phy_tensor_check_symmetries` verifies the whole dense table against every
declared symmetry — at most `24 * 256` comparisons. It is public rather than
test-local because a caller that populates components by hand needs the same
gate.

## Errors

Typed values, uniform across the API, and no new `phy_status` value was
needed:

| status | when |
| --- | --- |
| `PHY_ERR_INVALID_ARGUMENT` | null pointer, index `>=` dimension, slot `>=` rank, repeated slot, malformed permutation, sign other than `±1` |
| `PHY_ERR_UNSUPPORTED` | dimension or rank beyond the compiled ceilings — `n = 5` is not wrong, it is not implemented |
| `PHY_ERR_TYPE` | a slot's variance rejects the operation |
| `PHY_ERR_ASSUMPTION` | declared symmetries cannot all hold, or an assignment contradicts one |
| `PHY_ERR_OUT_OF_MEMORY` | `phy_alloc` failed |

Every failing call leaves its output parameter untouched and its tensor
unmodified.

The IR has no rollback for interning, by design, so a chart whose fourth
coordinate fails to intern leaves the first three symbols in the context.
That is the IR's documented behaviour and a memory concern rather than a
correctness one — the symbols are well-formed, `phy_ir_validate` still passes,
and destroying the context reclaims them. What this layer guarantees is that
no chart or tensor is leaked and no half-built one is ever returned.

## Testing

`tests/test_tensor.c`, 12,462 checks.

Structural coverage: chart construction and rejection; rank 0 through 4 at
dimensions 1 through 4; exhaustive flat-index round trips over every component
of every shape; group orders and independent counts at `n = 2, 3, 4`; every
rejection path; the Riemann fill agreement, with a distinct symbol per
independent component so that two components agreeing is evidence rather than
coincidence; canonical lookup over all 256 components; and the
allocation-failure sweep.

The scalar-dependent slice now covers metric inversion, raise/lower
involution, contraction against known traces, signed-component extraction, and
component partial derivatives. The first Bianchi identity and abstract-index
canonicalization remain separate work because they need algebra over index
orbits rather than only dense component arithmetic.

Test 6, dimension independence, is honoured throughout: every structural test
that can run at more than one dimension does. A corpus that is almost entirely
4-dimensional would otherwise hide a hard-coded `n = 4`.

Test 7, the resource limit, is split. The allocation half is here: an injected
failure is walked across every allocation the workload makes, and after each
one the status is typed, no object is handed back, `phy_ir_validate` passes,
and `phy_telemetry` returns to its baseline. The IR live-node-cap half belongs
with the scalar layer, since only expression construction can reach it.

Sanitizers: the suite is clean under trapping UBSan (GCC 15.2, `-fsanitize`
with `undefined,bounds,alignment,object-size,enum,bool,signed-integer-overflow`
and `-fsanitize-undefined-trap-on-error`) and under AddressSanitizer
(clang-cl 22.1).

## Device build

In the device source list and compiles clean for ARM under
`-Os -marm -ffunction-sections -fdata-sections`. The current isolated link
check measures **8,834 bytes** of tensor-layer text and a **58,512-byte**
dependency-complete probe package.

The application now reaches the tensor core through the stateful evaluator.
`make tensor-link-check` still closes an independent API-retention gap the same
way `make ir-link-check` does: it links `tests/device/tensor_link_probe.c`,
which touches every public entry point, with the production flags —
`--gc-sections` included, since the point is that these symbols survive
collection because they are genuinely referenced. It then checks that every
function declared in `include/phy/tensor.h` is present, that no float
formatter was dragged in, and that the result packages to a real `.tns`.

The expected symbol set is **derived from the header**, not listed in the
script, so adding a public function without extending the probe fails the
check instead of quietly going unlinked. All 45 public entry points are
retained; no `_dtoa`, `_strtod`, or `_printf_float` reaches the image.

The target is not a dependency of `all` and the probe is not in the Makefile's
`SOURCES`. It builds into `build/arm-tensor-linkcheck/` and never touches
`dist/`, so it cannot inflate the shipped `.tns`.

## Not in this layer

Index canonicalization, abstract indices, and dummy-index renaming — Phase 2
work that starts from the papers and BSGS fixtures in
`docs/references/TENSOR_GEOMETRY.md`, and which nothing here anticipates. Lie
derivatives, torsion, tetrads, and frames. Symmetrization and
antisymmetrization operators. Dimensions above 4 and ranks above 4. Scalar
functions beyond the native CAS contract remain in the CAS layer rather than
being reimplemented here.

Differential forms and wedge products have since landed, but not here and not
as tensors. `include/phy/geom.h` stores a form as its `C(n,p)` strictly
increasing components, so antisymmetry is the representation rather than a
declared symmetry this layer maintains; see [`docs/GEOMETRY.md`](GEOMETRY.md).
What that layer does reuse is `phy_chart` and, for the interior product, a
rank-1 contravariant `phy_tensor` — a vector field is a rank-1 tensor, and
there was no reason to invent a second one.
