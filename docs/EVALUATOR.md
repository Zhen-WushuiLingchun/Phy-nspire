# The stateful notebook evaluator

`include/phy/eval.h`, `src/eval`.

## What changed, and why it mattered

Before this layer the notebook parsed `ExteriorD[omega]`, `FieldStrength[A,g]`
and a dozen more physics spellings into `PHY_IR_OPERATOR` nodes and handed them
to the scalar CAS. The CAS does exactly what
[`CAS.md`](CAS.md) says it does with an operator: it simplifies the operands and
leaves the node alone. So the head survived the round trip, the cell rendered
something plausible, and nothing had been computed.

That is the worst failure mode a symbolic system has, because a reader cannot
tell it apart from a working evaluator without already knowing the answer. The
differential-geometry, Lie-algebra and Yang--Mills backends were fully
implemented and fully tested, and completely unreachable from the product.

This layer replaces head preservation with dispatch. Two rules make that
verifiable rather than aspirational:

- every reserved head the evaluator knows is either evaluated or a typed error;
- nothing is rebuilt as an inert operator with the same head.

`tests/test_eval.c` pins the second rule directly: called with no arguments,
every evaluated head must return `PHY_ERR_PARSE`. A head that fell out of either
the parser's table or the evaluator's would instead become an ordinary function
application and return a value, and the test would fail.

## Why it has to be stateful

The objects those backends operate on cannot be written as one expression. A
manifold is a dimension, an orientation, a signature and a chart of named
coordinates; an `SU(2)` connection on a 3-manifold is 3 algebra components each
holding 3 form components. Nothing in the typed IR represents them and nothing
should — the IR is a graph of values, and these are objects with identity that
several cells share.

So the notebook owns an environment, and a cell is evaluated *against* it:

```text
M = Manifold[{x, y, z}, Euclidean]
su = LieAlgebra[LieGroup[SU2]]
A = GaugeConnection[su, M, {{1,0,0},{0,1,0},{0,0,1}}]
F = FieldStrength[A, g]
ZeroQ[Bianchi[A, g]]                       (* True *)
```

Each line is one cell. The last cannot be written at all without the first
four, which is what "stateful" buys.

## Values

| Kind | Produced by | Displays as |
| --- | --- | --- |
| `Scalar` | arithmetic, components, decisions | its own typed IR |
| `Manifold` | `Manifold[...]` | descriptor line |
| `Tensor` | `Metric`, `VectorField`, curvature parts | components, `List` of rows at rank <= 2 |
| `Form` | `DifferentialForm`, `Wedge`, `ExteriorD`, `HodgeStar`, `Volume`, `InteriorProduct`, `YangMillsLagrangian`, `ColorComponent` | coordinate-coframe expansion |
| `LieGroup` | `LieGroup[...]` | descriptor line |
| `LieAlgebra` | `LieAlgebra[G]` | descriptor line |
| `LieElement` | `Generator`, `LieElement`, `LieBracket` | `sum_a c_a T_a` |
| `LieForm` | `LieForm`, `GaugeConnection`, `FieldStrength`, `CovariantD`, `GaugeVariation`, `Bianchi` | `sum_a T_a . (coframe expansion)` |
| `Curvature` | `Curvature[g]` | descriptor line |

A form's expansion is real mathematics rather than a label: the coframe symbol
of a coordinate is its name with a `d` in front, so a chart on `(r, theta)`
spans its 1-forms with `dr` and `dtheta`, and `ExteriorD[y dx]` renders as
`-dx ^ dy`. Those are ordinary interned symbols, so a document that also uses
`dr` as a scalar will see them collide; the alternative was a coframe node kind
that the scalar CAS would then have to be taught to ignore.

An algebra-valued form uses `PHY_IR_NCMUL` between the generator and its
component, because generators do not commute and an ordinary product would
invite the CAS to collect terms it has no right to collect. A Lie element uses
ordinary multiplication, because its coefficients are scalars and that product
really is commutative.

## Surface

Assignment is `name = value`, distinguished from the equation `name == value` by
one character of lookahead. `Set[name, value]` is the FullForm spelling.
`Clear[name]` unbinds one name; `ClearAll[]` clears the environment.

### Differential geometry

| Spelling | Backend |
| --- | --- |
| `Manifold[{coords...}, signature, orientation?]` | `phy_chart_create` + `phy_manifold_create` |
| `DifferentialForm[M, degree, {components}?]` | `phy_form_create` |
| `Metric[M, {{...},...}]` | rank-2 covariant `phy_tensor` |
| `VectorField[M, {...}]` | rank-1 contravariant `phy_tensor` |
| `Wedge[a, b, ...]` | `phy_form_wedge` |
| `ExteriorD[a]` | `phy_form_exterior_derivative` / `phy_lie_form_exterior_derivative` |
| `InteriorProduct[a, v]` | `phy_form_interior_product` |
| `HodgeStar[a]`, `HodgeStar[a, g]` | `phy_form_hodge` / `phy_form_hodge_metric` |
| `Volume[M]`, `Volume[M, g]` | `phy_form_volume` / `phy_form_volume_metric` |

`signature` is the keyword `Euclidean`/`Riemannian`, the keyword
`Lorentzian`/`Minkowski` — mostly-plus, per
[`references/GENERAL_RELATIVITY.md`](references/GENERAL_RELATIVITY.md) — or an
explicit list of `+1`/`-1`. `orientation` is `Positive` (the default),
`Negative`, or `Unoriented`/`None`. A manifold carries exactly one chart:
`geom.h` registers charts but does not relate them, so a second one buys nothing
until a validated `phy_map` exists.

### Lie algebra

| Spelling | Backend |
| --- | --- |
| `LieGroup[U1\|SU2\|SO3\|SU3\|SO13]` | `phy_lie_group_builtin` |
| `LieAlgebra[G]` | `phy_lie_group_algebra`, borrowed |
| `Generator[alg, k]` | `phy_lie_basis_element` |
| `LieElement[alg, {coefficients}]` | `phy_lie_element_create` |
| `LieBracket[X, Y]` | `phy_lie_bracket`, `phy_lie_form_bracket_wedge`, or `phy_lie_commutator_ir` by operand kind |
| `StructureConstant[alg, a, b, c]` | `phy_lie_structure_constant` |
| `Killing[alg, a, b]` | `phy_lie_killing_component` |

A group outside the built-in catalogue is `PHY_ERR_UNSUPPORTED`, not a label:
returning an object that cannot bracket would be worse than failing.

### Yang--Mills

| Spelling | Backend |
| --- | --- |
| `LieForm[alg, M, degree, {{...},...}]` | `phy_lie_form_create` |
| `GaugeConnection[alg, M, {{...},...}]` | the same, at degree 1 |
| `FieldStrength[A, g]` | `phy_yang_mills_field_strength` |
| `CovariantD[A, omega, g]` | `phy_yang_mills_covariant_derivative` |
| `GaugeVariation[X, alpha, g]` | connection or curvature branch, chosen by `X`'s degree |
| `Bianchi[A, g]` | `phy_yang_mills_bianchi` |
| `YangMillsLagrangian[F, metric]`, `[F, metric, {{h...}}]` | `phy_yang_mills_lagrangian` |
| `ColorComponent[F, a]` | one colour component as an ordinary form |

`GaugeVariation` reads the degree because `delta A = D_A alpha` and
`delta F = g [F, alpha]` are different formulas, a connection is a 1-form and a
curvature a 2-form. Asking a reader to spell which one they meant would be
asking them to restate what the object already knows, and getting it wrong would
return a well-formed wrong answer.

Omitting the bilinear form of `YangMillsLagrangian` selects the algebra's
Killing form, which is identically zero for `U(1)`; Abelian callers supply their
representation trace form explicitly.

### General relativity

`Curvature[g]` runs the whole coordinate-metric pipeline; `InverseMetric`,
`Christoffel`, `Riemann`, `RiemannMixed`, `Ricci`, `RicciScalar` and `Einstein`
read parts of the result.

### Queries

`Component[obj, indices...]`, `Degree[form]`, `Rank[tensor]`, `Dimension[obj]`,
`ZeroQ[obj]`, `EquivalentQ[a, b]`.

`Component` of a Lie form takes the colour index first, then the form indices;
a degree-0 form takes none. `Dimension` reports the underlying space where there
is one, and the algebra or representation dimension otherwise.

The two decisions return the symbols `True`, `False` and `Unknown`, following
`phy_cas_is_zero`: an undecided question stays visibly undecided instead of
collapsing to `False`.

### Structural algebra

`alpha + beta` and `s * alpha` work on forms, algebra-valued forms and Lie
elements, so `(g/2)*LieBracket[A, A]` reads as the formula it is. Subtraction
and division need no cases — the parser already writes `a - b` as `a + (-1)*b`
and `a/2` as `a * 2^-1`. Sums are homogeneous, and a product admits at most one
object factor, because the product of two forms is the wedge and has its own
spelling.

## Not evaluated yet

`ScalarField`, `Propagator`, `Vertex`, `TadpoleIntegral` and `BubbleIntegral`
are reserved, parse to typed IR, and have **no evaluator**. The bounded real
`phi^4` layer of [`QFT_SCALAR.md`](QFT_SCALAR.md) is implemented and is the next
increment; until it is wired, those heads are display and persistence only, and
`tests/test_eval.c` pins that so changing it is a deliberate act rather than a
side effect.

## Ownership

The environment owns every object it creates and destroys them in reverse
creation order, which is the order the layers below require: forms before
manifolds, manifolds before charts, everything before the IR context.

Evaluating a cell creates intermediates — `HodgeStar[Wedge[a, b]]` builds a
wedge nobody names. After each command the environment sweeps: everything
reachable from a binding or from the command's own result survives, the rest are
destroyed newest-first. Reachability follows recorded dependencies, so binding a
form keeps its manifold alive even when the manifold's own name was overwritten
in the same cell. Survivors are then compacted with their order preserved, which
is what keeps "created later" and "destroyed first" the same statement.

The sweep runs on the failure path too. A command that failed half-way through a
gauge expression has already registered intermediates, and they are exactly what
nothing will ever reach again.

`phy_env_validate` checks the three invariants that make this sound — every
dependency points at a lower live slot, every binding names a live object or a
scalar, no object appears twice — and `tests/test_eval.c` calls it after every
single command, including the ones that fail.

## Coordinate capture

Binding a name that a live chart uses as a coordinate is `PHY_ERR_ASSUMPTION`,
and so is creating a chart whose coordinate is already bound. Without that rule
`x = 2` silently rewrites every component of every form on a chart with an `x`
axis, and the wrong answer is indistinguishable from the right one. Scalar
substitution is otherwise unrestricted, which is why this one case is closed
rather than documented as a hazard.

Differentiating with respect to a bound name is `PHY_ERR_TYPE` for the same
reason: `D[x^2, x]` after `x = 2` is not a derivative.

## Persistence

The environment is **not** serialized. A saved document stores cell sources and
results; reopening one starts with an empty environment, so a cell that reads a
binding fails until `phy_notebook_evaluate_all` replays the notebook in order —
`FILE` > `Run all cells` in the shell.

Objects hold pointers into an IR context and into each other. Writing them to a
file would mean inventing a second, weaker serialization of every physics layer,
and reading one back would mean trusting it. Replaying is cheap, exact, and
cannot disagree with the source.

The consequence for the cell model is that evaluation is now forward-dependent:
running one cell marks every result after it stale, because a cell that binds a
name changes what the cells below it mean.

## Limits

`PHY_EVAL_MAX_OBJECTS` is 96 and `PHY_EVAL_MAX_BINDINGS` is 32; exceeding either
is `PHY_ERR_TERM_LIMIT`. The notebook's own ceilings were raised with this phase
— 65,536 IR nodes, 2 MiB of IR pools, 200,000 CAS steps, 512 KiB of CAS memo and
scratch — because a 4D curvature pass interns several thousand nodes and the IR
has no collection, so a document that computes one and then edits a cell has to
be able to compute it again. They stay limits: an intentionally explosive
expression still fails as a typed `PHY_ERR_NODE_LIMIT`.

## Verification

`tests/test_eval.c`, 847 checks. The physics cases deliberately reproduce,
through reader-facing source, results the backend suites already certify
directly:

- the `U(1)` connection `A = x dy` giving `F = dx ^ dy`, its vanishing Bianchi
  residual, and the `-1/2` quadratic density of `tests/test_yang_mills.c`;
- the nine `SU(2)` curvature components of the same file, from constant
  `A^a = dx^a`, and `D_A F_A = 0`;
- `delta F = g[F, alpha]` and `delta A = D_A alpha` agreeing with the branch the
  evaluator selects from the operand degree;
- the round two-sphere of `tests/test_gr.c`: `R = 2/a^2`, the two Christoffel
  symbols, `R_{theta phi theta phi}`, and a vanishing Einstein tensor;
- graded commutativity of the wedge, `d^2 = 0`, the graded Leibniz rule, and
  `iota_v iota_v = 0` from `tests/test_geom.c`;
- `[T1,T2] = T3`, structure-constant antisymmetry, and `K_ab = -2 delta_ab` from
  `tests/test_lie.c`;
- the general coordinate-metric volume form and Hodge dual of
  `tests/test_geom_metric.c`.

If the evaluator merely preserved operator heads, none of them would hold.

The remaining cases cover state flow between cells, `Clear`/`ClearAll`, the
capture rules, every typed-error path, the sweep's object accounting under
rebinding and failure, the binding ceiling, and the notebook integration
including save/reopen with descriptors.

`tests/test_palette.c` additionally parses every CAS palette snippet, because a
palette that inserts something the evaluator rejects is worse than no palette.

The ARM link check is `make eval-link-check` and
`tests/device/eval_link_probe.c`: 15 declared entry points, the whole physics
stack behind one dispatcher, and the same no-float/no-libm/no-soft-float
standard the CAS and geometry layers are held to. **It has not been run**: the
Ndless SDK is not installed on the machine this phase was developed on. The
probe compiles clean under the same warning set with a host GCC; the device
figures are unmeasured and are not quoted anywhere.

One consequence of this phase that the earlier link-check reports called out as
future work has now happened: the application genuinely calls the geometry, Lie
and Yang--Mills layers, so `--gc-sections` no longer drops them and
`dist/phy-nspire.tns` will contain them. The size effect is likewise unmeasured
here.
