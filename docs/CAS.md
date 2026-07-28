# Scalar computer algebra

The rewrite layer `docs/IR.md` defers to. The IR canonicalizes structure and
refuses to do arithmetic; everything that changes the *value* an expression
denotes lives here. It is defined by `include/phy/cas.h` and implemented in
`src/cas`.

This document covers the design decisions. The header is the API reference and
is not repeated here.

## What the layer guarantees

**Exact.** Integer and reduced-rational atoms use an `int64` fast path and
promote to the native bounded arbitrary-precision layer when necessary. They
never wrap and never silently promote to `double`. `PHY_IR_REAL` atoms are
carried but never folded. Polynomial coefficient containers use immutable exact
IR refs, so their checked `int64` fast path promotes to the same integer and
rational domain.

**Decidable.** `phy_cas_is_zero` *decides* zero on a documented class and
answers `PHY_CAS_UNKNOWN` outside it. It never estimates.

**Bounded.** Every operation runs against a step budget and an optional
cancellation hook, so a notebook cell is interruptible and a runaway rewrite
fails as `PHY_ERR_TIMEOUT`.

**Memoized.** Results are cached on the interned ref, so a shared subterm is
rewritten once however many times the expression mentions it.

## Every entry point returns a status

Failure is ordinary here — a budget runs out, a denominator is zero, or a
bounded algorithm leaves its supported coefficient class — and a caller that
must distinguish those cases should not have to consult a sticky flag to do
it. So unlike the IR's builders, which return `PHY_IR_NULL` and record the
reason, each function here returns a `phy_status` and writes its result through
an out parameter.

The two conventions meet at one place: when an IR builder fails inside this
layer, `phy_cas_ir_failure` reads the IR's sticky error and returns it as this
layer's status. It does not clear the flag, because that flag belongs to the
caller of the IR.

A `phy_cas` **borrows** its `phy_ir_context`; it does not own it. Destroy the
CAS first.

## The normal form

`phy_cas_simplify` preserves structure — a product of sums stays a product of
sums, because that is usually what a reader wants to see. What it guarantees:

- every subexpression of exact numbers is one exact number;
- a sum has at most one numeric term, no zero term, and no two terms sharing a
  non-numeric part: `2*x + 3*x` is `5*x`;
- a product has at most one numeric factor, never the factor 1, and no two
  factors sharing a base: `x * x^2` is `x^3`;
- a power has neither exponent 0 nor 1, and folds when the base is exact and the
  exponent an integer;
- a `PHY_IR_ERROR` operand anywhere propagates outward as the whole result.

### Collection is a sort, not a scan

Both collection routines split each operand into a `(key, weight)` pair, sort on
the key, and merge equal keys. A sum's terms split into (non-numeric part,
coefficient); a product's factors into (base, exponent). Sorting to bring equal
keys together is what keeps a sum of a thousand terms from costing a million
comparisons — and equal keys are found by ref comparison, because the IR
interns.

Heapsort, for the three reasons `src/ir/order.c` gives: unconditional
`O(n log n)` on operand counts the term limit allows, iterative, and no scratch
of its own, which matters because the array being sorted already lives in the
arena.

After the merge, the sum collector applies its one trigonometric identity:

```
c*sin(u)^2*K + c*cos(u)^2*K  ->  c*K
```

for a shared factor `K` — possibly none — and one exact coefficient `c`.
Restricted to an exactly matching pair because that move strictly removes a
term, so it cannot ping-pong, and a sum with no such pair is left exactly as
written: `1 - sin(u)^2` still reads back as entered, and
`3*sin(u)^2 + 2*cos(u)^2` is not creatively rebalanced. The pass repeats while
it fires, because a collapse can expose the next pair —
`sin(v)^2 sin(u)^2 + sin(v)^2 cos(u)^2 + cos(v)^2` reaches `1` in two steps —
and it terminates because every step removes a term. Anything subtler belongs
to the decision pipeline below, which owns the full basis.

### Nested operands must be flattened

A caller legitimately passes a nested operand of the same kind — negation is
`mul(-1, value)` whatever `value` is, and the rational form adds sums to sums —
and collection has to see through it. Treating a nested product as one opaque
factor is wrong twice over: `mul(-1, mul(-1, x))` keeps two numeric factors
instead of cancelling to `x`, and `add(add(x, y), x)` leaves `x` uncollected
instead of reaching `2*x`.

The first of those was a real bug during development, and its symptom is worth
recording: the parity rule for `sin` folds a negative sign out of its argument,
so an operand whose sign never resolved made `sin(-x)` negate forever until the
stack ran out. A wrong normal form does not always look like a wrong answer.

One level of flattening suffices, inductively — every operand has already been
simplified, and a normalized sum never contains a sum. That is the same argument
`make_nary` in `src/ir/ir.c` relies on.

### The generic-domain convention

Identities are applied where the expression is defined, so `x^0` is 1 and
`x - x` is 0 without demanding a nonzero declaration first. This is what every
mainstream CAS does, and the alternative — carrying `x^0` around until someone
declares `x` nonzero — makes ordinary algebra unusable.

What the convention never does is manufacture a value where there is none:
`1/0`, `0^0` and `0^(-1)` are `PHY_ERR_DOMAIN`.

### Exponent arithmetic

`(u^a)^b` combines to `u^(a*b)` **only when `b` is an integer**. The
restriction is load-bearing: `(x^2)^(1/2)` is `|x|`, and a layer that rewrites
it to `x` has quietly changed the function. `(u*v)^k` distributes on the same
condition.

`x^a * x^b` becomes `x^(a+b)` for any exponents, numeric or not. That is sound
for a single base on one principal branch: both sides are
`exp((a+b) log x)`.

### Exact-number promotion

Two numeric operands of a product or sum *must* fold — the normal form permits
only one numeric operand. If the checked `int64` fast path overflows, the
operation is repeated in the bounded arbitrary-precision domain and published
as a canonical exact atom. Integer powers use the same route, so `2^200`
evaluates exactly and `4^500 - 2^1000` is proved zero. A limb, byte, step, or
cancellation ceiling still fails transactionally with its typed status.

This promotion covers atom normalization, sum/product collection, integer
powers, source/evaluator flow, serialization, MathTree display, rational LCD
construction, and the univariate polynomial view used by reduction and
`Factor`. Polynomial coefficients are immutable exact IR refs: the checked
`int64` path remains the common fast path, while promoted coefficients use the
bounded bigint/rational bridge. Thus Euclidean division and GCD do not fail
merely because a coefficient exceeds 64 bits.

The rational-root enumerator inside `Factor` is only a fast path. It converts a
primitive polynomial to bounded `int64` coefficients before enumerating
divisors, with roots `0`, `-1`, and `1` tested directly in the exact
coefficient domain. Inputs outside that small enumerator continue through the
exact monic-integer transform and certified modular factorization below; a
failed fast path is never treated as proof of irreducibility.

## The zero decision

This is the load-bearing capability `docs/agent-tasks/TENSOR_CORE.md` names, and
the reason the rest of the layer is shaped as it is. Four steps:

1. **simplify** — collect and fold, so equal terms are equal refs;
2. **trigonometry** — put `tan` and multiple angles on one basis;
3. **rational form** — one expanded numerator over one expanded denominator;
4. **cos reduction** — `cos(u)^2 → 1 - sin(u)^2`, leaving degree at most one in
   each cosine.

What makes the answer exact is the IR, not the arithmetic. A fully expanded,
collected polynomial over a fixed set of generators is *canonical*, and the IR
interns it — so two equal polynomials are literally the same ref, and "is the
numerator zero" is `num == integer 0`. No tolerance, no sampling, no heuristic.

`PHY_CAS_ZERO` means identically zero as a rational function, wherever the
expression is defined. `PHY_CAS_NONZERO` means proved nonzero: the numerator
reduces to a nonzero exact number, or to a product of factors each known
nonzero, which is where `PHY_IR_ASSUME_NONZERO` and the sign assumptions are
read. `PHY_CAS_UNKNOWN` means the expression left the class.

A generator is anything the layer cannot see inside: a symbol, a function
application, a power with a non-integer exponent, a tensor. Denominators are
assumed non-vanishing, which is what "wherever it is defined" means; a
denominator that reduces to exactly zero is `PHY_ERR_DOMAIN`, because such an
expression is defined nowhere.

After exact division by denominator factors, a bounded Euclidean algorithm
computes a monic GCD in `Q[x]` through degree 48 with arbitrary-precision exact
coefficients. This closes cases such as
`(x^2-1)/(x^2-2x+1) -> (x+1)/(x-1)` even though the hidden factor is not the
whole denominator.

Expanded polynomials in two or more symbols also have a bounded, sound
cancellation path. Their exponent vectors are encoded by mixed-radix
Kronecker substitution only when the resulting degree is at most 48. Because
a univariate image can have factors that are not multivariate factors, every
decoded candidate and quotient pair is multiplied back against both original
polynomials. A complete-on-success divisor search is used on the currently
factorable image class, with a separate exact path for the universal image
roots `0`, `-1`, and `1`; otherwise the expression stays explicit. This is not
yet a complete sparse multivariate GCD implementation.

### Complete-on-success `Factor`

`phy_cas_factor` and reader-facing `Factor[...]` reuse the same degree-48
`Q[x]` view. They first extract the exact leading coefficient, make the
polynomial monic, and run Yun's characteristic-zero square-free decomposition.
The load-bearing derivative GCD is reconstructed from monic modular GCDs by
CRT; it is accepted only after exact division of both the integer transform and
its derivative. This avoids the coefficient swell of a direct Euclidean walk
on promoted rational coefficients and proves repeated irreducible factors such
as

```
x^4 + 2 x^2 + 1  ->  (x^2 + 1)^2.
```

Rational-root enumeration remains a fast path, including exact tests of
`0`, `-1`, and `1` before its small-integer divisor search. A residual beyond
that fast path is mapped exactly to a monic integer polynomial by
`F(y)=D^n P(y/D)`, where `D` is a positive common denominator. The complete
bounded path then:

1. selects a square-free prime image;
2. factors it deterministically with Berlekamp;
3. obtains Bézout corrections with finite-field extended GCD;
4. Hensel-lifts every candidate bipartition until the modulus exceeds twice a
   Landau--Mignotte coefficient bound;
5. centers the lifted coefficients, maps them back to `Q[x]`, and requires
   exact division before accepting a factor.

If every modular bipartition is lifted past the bound and rejected by exact
division, the residual is certified irreducible over `Q`; absence of rational
roots alone is never used for a degree above three. The recombination lattice
is bounded at 13 modular factors (4,096 bipartitions), the degree at 48, and
the modular derivative-GCD CRT at 256 primes. Exceeding a degree, work, memory,
or recombination ceiling is a typed resource error, never a partial
factorization reported as complete. This covers non-monic rational inputs,
repeated high-degree factors, and promoted coefficients such as `2^100`.

The factor-record workspace is charged to `phy_cas_limits.max_bytes` and
released on success and every failure path. The public result is not built
until the complete bounded factorization has succeeded.

### Exact univariate `Apart`

`phy_cas_apart` and reader-facing `Apart[...]` operate on the same bounded
univariate `Q[x]` class. The rational expression is reduced first and its
unique non-constant symbol is selected. The numerator and denominator are
expanded into exact polynomials, the denominator is made monic without
changing the quotient, and polynomial division separates the improper part.

The proper denominator is factored by the complete-on-success kernel above.
For each irreducible factor `f_i` and exponent `k`, the unknown numerator has
degree below `deg(f_i)`. Multiplying all proposed terms by the common
denominator gives a square coefficient system with exactly `deg(D)` unknowns.
Forward elimination and back substitution run entirely over exact rational IR
atoms; every pivot and row operation therefore remains symbolic.

Before constructing the display sum, the solution is substituted into the
original, unmodified coefficient matrix and every row is proved equal. This is
the partial-fraction analogue of Factor's exact product check. A singular
system, resource ceiling, factorization failure, second variable, or
algebraic-extension coefficient returns a typed error and publishes no partial
result. The result supports improper fractions, repeated factors, irreducible
quadratic and higher factors, rational leading coefficients, and promoted
integer coefficients.

`phy_cas_full_simplify` — the notebook's `FullSimplify` — is the one door from
the display normal form into this machinery. It runs the plain simplifier,
then the trig-basis rational form, and returns whichever of the two prints
shorter, so `sin(2x) - 2 sin(x) cos(x)` reaches `0` and `1/(1 - cos(u)^2)`
reaches `sin(u)^-2`, while `1/tan(q)` keeps the reader's spelling. A rational
pass that exhausts a resource budget falls back to the plain result; an
identically zero denominator is still `PHY_ERR_DOMAIN`.

### Exact bounded `Series`

`phy_cas_series` and reader-facing `Series[expr,{x,a,n}]` share one explicit
truncated Laurent representation:

```text
(variable, exact center, valuation, exclusive order, exact coefficients[])
```

The internal ring implements exact addition, subtraction, Cauchy product,
reciprocal/division, integer powers, differentiation, Laurent integration, and
zero-constant composition. Precision propagation uses valuations: multiplying
`O(u^p)` by a series of valuation `v` yields `O(u^(p+v))`; no missing
coefficient is treated as computed. The public result is
`SeriesData[var,center,valuation,order,List[coefficients...]]`, so every
coefficient and the order term are typed metadata rather than display text.
`Normal` returns the reconstructed finite expression only after validating
that metadata.

Rational functions expand at any exact rational center where the bounded
Laurent recurrence proves a nonzero leading denominator coefficient.
Maclaurin coefficient recurrences and exact composition currently cover
`Exp`, circular/hyperbolic sine, cosine and tangent, `ArcSin`, `ArcTan`,
`Log[1+u]`, and rational binomial powers. A nonzero-center analytic expansion
that would introduce an unavailable transcendental coefficient is explicitly
unsupported; it is never estimated with floating point.

### Exact bounded `Limit`

`phy_cas_limit` and reader-facing `Limit[expr,{x,a}]` reuse the formal-series
valuation and leading coefficient above. A structurally certified continuous
polynomial/rational or real-entire expression is substituted exactly.
Removable singularities and supported analytic compositions fall through to a
Laurent expansion. Positive and negative pole signs are proved from the exact
leading coefficient, valuation parity, and the requested `FromAbove` or
`FromBelow` direction. A two-sided request succeeds only when those directed
answers agree.

For `Infinity` and `-Infinity`, the implementation substitutes `x=1/t` or
`x=-1/t` and takes the exact `t -> 0` limit from above. This proves polynomial
and rational degree behavior without a separate floating-point heuristic.
`Direction->"FromAbove"`/`"FromBelow"` and the shorter bare direction symbols
are accepted at finite points. Oscillatory forms such as `Sin[1/x]`, unknown
branch behavior, or a coefficient outside the current exact rational series
domain return `PHY_ERR_UNSUPPORTED`; unequal finite directions return
`PHY_ERR_DOMAIN`.

### Why trigonometry is reduced, and to what

`research/corpus/gr_golden.json` forced this. Four `sphere_2d` entries — in the
**required** MVP fields, not the cross-checks — state their value in a different
trigonometric form from the one a curvature pass computes:

| field | corpus form | computed form |
| --- | --- | --- |
| `christoffel theta;phi,phi` | `-sin(2*theta)/2` | `-sin(theta)*cos(theta)` |
| `christoffel phi;theta,phi` | `1/tan(theta)` | `cos(theta)/sin(theta)` |
| `ricci phi,phi` | `sin(2*theta)/(2*tan(theta)) - cos(2*theta)` | `sin(theta)^2` |
| `riemann theta,phi,theta,phi` | `a_0^2*(sin(2*theta)/(2*tan(theta)) - cos(2*theta))` | `a_0^2*sin(theta)^2` |

If those were not decided equal, the Phase 3 acceptance tests could not use the
corpus at all — and `sphere_2d` is also the dimension-independence metric in
`docs/agent-tasks/TENSOR_CORE.md`, so the gap was not a corner case.

Three rewrites close it:

```
tan(u)              -> sin(u) / cos(u)
sin(k*u), cos(k*u)  -> polynomials in sin(u), cos(u), integer 2 <= |k| <= 64
cos(u)^k, k >= 2    -> (1 - sin(u)^2)^(k/2) * cos(u)^(k mod 2)
```

Multiple angles use the angle-addition recurrence, one step per unit of `k`:
Chebyshev polynomials in closed form would need binomial coefficients and an
alternating sign convention, and the recurrence needs neither. The cap exists
because the result has on the order of `k` terms, so an unbounded `k` would let
`sin(1000000*theta)` turn a decision into a term-limit failure; past the cap the
application stays an opaque generator, which costs completeness and never
soundness.

`tan` becomes a quotient so that the denominator it hides becomes a denominator
the rational form can cancel — which is exactly what `1/tan(theta)` needs.

The cosine reduction runs on the already expanded polynomial, and one pass is
enough: each monomial holds a single power of any one cosine because product
collection merged them, so after substitution no monomial has a cosine above the
first, and re-expanding `(1 - sin^2)^m` introduces sines only. There is no
fixpoint to iterate towards.

**These reductions belong to the decision procedure, not to display.**
`phy_cas_simplify` leaves `tan(theta)` and `sin(2*theta)` exactly as written,
because that is what the reader wrote. A notebook cell that silently rewrote
`1/tan(theta)` into `cos(theta)/sin(theta)` would be answering a question nobody
asked.

### What is still outside the class

Stated because a decision procedure's boundary is part of its contract. Each
answers `UNKNOWN`:

- `PHY_IR_REAL` atoms, non-integer powers, unknown functions, tensors and
  operators, exact powers too large to fold;
- a **non-integer** multiple of an argument — `sin(u/2)` is not a polynomial in
  `sin(u)` and `cos(u)`;
- a multiple beyond 64;
- a **sum** inside an argument — `sin(u + v)` is not rewritten through the
  addition formula. Nothing in the corpus needs it, and adding it doubles the
  surface of step 2.

## Differentiation

The rules are the ordinary ones. What is worth reading closely is what happens
when a rule does not apply.

A tensor component may depend on a coordinate, and nothing in the graph says
that it does: `PHY_IR_TENSOR` carries indices, not the functional form of its
entries. So "this subtree does not mention `theta`, therefore its derivative is
zero" is valid for a sum of symbols and invalid for a tensor, an operator, or an
unknown function. `phy_cas_may_depend` draws exactly that line — it answers
false only when the subtree is built entirely from kinds whose dependence is
visible — and everything else differentiates to an unevaluated
`PHY_IR_DERIVATIVE`.

An unevaluated derivative is a correct answer a later layer can refine. A wrong
zero is a curvature tensor that vanishes for a spacetime that curves.

The known table includes the elementary, inverse trigonometric, hyperbolic,
inverse hyperbolic, and first special-function pack (`Gamma`, `LogGamma`,
`Erf`, `Erfc`). `tan` differentiates to `1/cos(u)^2` rather than to
`1 + tan(u)^2` so that the result lands on the same basis the zero decision
reduces to; the other form would need the identity applied before anything
could cancel against it. `Gamma'` is represented exactly as
`Gamma(u) Digamma(u)`; Digamma remains an explicit special function outside
this first table.

`d(u^v)` uses the power rule when the exponent is constant, which avoids
introducing a logarithm of a base that may be negative, and the general
`u^v * (v' log u + v u'/u)` only when both parts vary.

## Integration and exact function values

`Integrate` is a bounded rule system, not a numerical or heuristic integrator.
It covers sums and constant factors, rational powers with linear inner
expressions, elementary and hyperbolic linear-inner rules, and the exact
kernels

```
1/(1+u^2)          -> atan(u)
1/(1-u^2)          -> atanh(u)
1/sqrt(1+u^2)      -> asinh(u)
1/sqrt(1-u^2)      -> asin(u)
exp(-u^2)          -> sqrt(Pi) erf(u) / 2
```

with the constant derivative of `u` divided out. `Erf` and `Erfc` themselves
have exact linear-inner antiderivatives. Every rule is tested by
differentiating its result back to the input. Outside this class the result is
the explicit typed head `Integrate[expr,var]`.

`Pi`, `E`, `I`, `EulerGamma`, and directed-limit `Infinity` are protected
constants. The first elementary
table includes exact trigonometric values at supported multiples of `Pi`,
positive exact square-factor extraction (`Sqrt[72] -> 6 Sqrt[2]`),
`Gamma[n]` while `(n-1)!` fits `int64`, `Gamma[1/2]`, and the zero values of
`Erf`/`Erfc`. `I` does not yet satisfy `I^2=-1`: complex arithmetic remains a
separate exact-number-domain milestone.

## Memory and budget

The reader-facing `MemoryStatus[]` command reports current IR node/byte usage,
CAS arena bytes, live evaluator objects, and bindings. The ownership and
notebook-lifetime boundary are documented in
[`EVALUATOR.md`](EVALUATOR.md#ownership).

**The memo cache is keyed on interned refs**, which is sound only because the IR
never mutates a published node. A ref names the same structure for the life of
the context, so a cached answer stays true — with one exception. A few rewrites
read *declared assumptions*, so a result cached before `phy_ir_assume` was
called may not be what the same input would produce now. Declare first, compute
second; `phy_cas_cache_clear` is the escape hatch and the header says so at the
call site.

**The cache is a cache.** When growing it would breach the byte ceiling, it is
dropped and rebuilt. That costs time and never correctness — reporting a failure
there would turn a memory ceiling into a wrong status on a computation perfectly
able to finish.

**The scratch arena is strictly LIFO and addressed by offset, never by
pointer.** A nested walk can grow it, and a pointer taken before that growth
would dangle. Every path out of a function that took a mark must release it, and
`phy_cas_validate` fails if one did not — which is how the tests demonstrate the
failure paths unwind rather than asserting that they do.

**Steps count nodes visited, not nodes created**, so a rewrite that revisits a
shared subterm pays once. It is the bound that makes an intentionally explosive
expression fail as `PHY_ERR_TIMEOUT` instead of running until the device is
power-cycled. Cancellation is a caller-supplied hook polled every 256 steps,
which keeps this layer free of any clock or platform dependency: the notebook
shell owns the wall-clock budget and the ESC key and answers with a `bool`. It
also keeps the test suite deterministic.

Recursive walks recurse once per expression level and are bounded by the same
thing the IR's own recursion is: `phy_ir_limits.max_depth`, itself clamped to
1024.

### Expansion collects between rounds

Distribution multiplies an accumulated term list by one factor at a time, and
collects after each round. That is not an optimization to skip: the cross
product of one round feeds the next, so an uncollected list grows
multiplicatively while the collected polynomial does not. `(a+b+c+d)^8` has 165
terms, but eight rounds without collecting form 4^8 = 65,536 of them — enough to
hit the term limit and fail an expansion that comfortably fits. This was also a
real bug.

Raising a sum to a power distributes copies of the base directly rather than
forming `result * base` and expanding that. Forming it does not terminate:
product collection merges equal factors, so `result * base` with `result` still
equal to `base` comes back as `base^2`, and expanding `base^2` arrives at the
same call with the same arguments.

## Errors are values

A `PHY_IR_ERROR` operand propagates outward as the whole result, so
`1 + err(domain)` is `err(domain)`. A failed cell stays a typed error rather than
becoming a well-formed expression that quietly means something else, which is
what lets it survive a save and reopen — the property `docs/IR.md` gives
`PHY_IR_ERROR` for. The zero decision treats an error as a generator, so it
answers `UNKNOWN` rather than deciding anything about it.

## Not in this layer

General integration, unrestricted asymptotic/branch limits, and solving.
The exact bounded `Series`/`Normal` ring and the finite/directed/rational-
infinity `Limit` subset live in `series.c` and `limit.c`; cases they cannot
prove return a typed error rather than sampling. Complete general
multivariate polynomial GCD and multivariate/algebraic-extension partial
fractions. Matrices.
Dummy-index canonicalization,
contraction, and anything
that consumes declared slot symmetries — this layer simplifies the operands of
`PHY_IR_NCMUL`, `PHY_IR_TENSOR`, `PHY_IR_OPERATOR`, `PHY_IR_WEDGE` and
`PHY_IR_DERIVATIVE` in place and otherwise leaves them alone, never reordering
them or reading their indices. The Giac backend boundary.

## Testing

`tests/test_cas.c` contains over one thousand checks. Expressions are written in the IR's text format
and parsed, so a case reads as the mathematics it is about; results are checked
both against a serialized normal form, which pins the exact shape, and through
the zero decision, which pins the value.

Verified: exact rational arithmetic at the `int64` edges and its overflow
statuses; the domain errors; sum and product collection, including gathering
terms that are not adjacent in operand order; every power rule, and the
non-integer exponent that must *not* combine; the known-function table and
parity; simplification being idempotent, checked by ref equality; error
propagation; noncommutative and tensor kinds surviving untouched; expansion,
including a negative power of a sum; substitution, including a rule set that
swaps rather than loops; differentiation, the deferral rules, and the type
rejection of an index variable; the zero decision on polynomials, rational
functions, assumptions, and each documented limit; the rational form and its
identically-zero denominator; the trigonometric identities; **the four
`sphere_2d` corpus entries above, with a negative control**; flat-space
curvature vanishing; `INT64_MIN` exponent handling without signed overflow;
Yun square-free layers, modular derivative GCD/CRT, deterministic Berlekamp,
Hensel lifting, repeated and zero roots, exact high-degree factorization round
trips, partial fractions with improper/repeated/irreducible denominators, and
explicit unsupported boundaries;
empty sum/product identities; error-value propagation through differentiation;
the step budget, cancellation, and the term limit;
memoization measured in steps; a byte ceiling too small for a cache; and an
injected allocation failure swept across a workload, validating both layers
after each.

Both layers are validated on the way out of every case, so an operation that
leaked scratch or corrupted the cache fails the suite wherever it happened.
The full strict host and sanitizer suites are the release gates; current exact
counts are recorded in `CAS_ACCEPTANCE.md` after each clean build.

## Device build

Built with the pinned Ndless r2022 SDK and ARM GNU 14.3 toolchain using
`-Os -marm`. The isolated link check compiles the complete scalar layer to
87,815 bytes of ARM text; its dependency-complete probe packages to 121,640
bytes. These figures are deliberately measured by the link-check target rather
than maintained as a hand-summed per-object table.

The application now calls the CAS and the typed physics backends through
editable notebook cells. The current product, including persistence,
nMarkdown's math typesetter, and the reachable evaluator stack, is 1,154,912
bytes (18.3% of the 6 MiB ceiling).

`make cas-link-check` closes the gap that leaves. It is the same guard as
`make ir-link-check`, and `tools/link-check.sh` now serves both layers from one
script with a layer argument. It links `tests/device/cas_link_probe.c`, which
touches every public entry point, with the production flags — `--gc-sections`
included, since the point is that these symbols survive collection because they
are genuinely referenced.

The specific risk it exists for: this layer does 64-bit division and modulo in
the gcd, which on a 32-bit target become libgcc calls (`__aeabi_uldivmod` and
relatives). Whether those resolve under Ndless's newlib and ldscript is a
link-time question compiling cannot answer. The IR already has the same
dependency through its own gcd and its check passes, which is good evidence but
not the check itself.

`make cas-link-check` has been run with the real Ndless linker and packager:
all **33/33** public entry points derived from `include/phy/cas.h` survive
`--gc-sections`; the CAS+IR+platform probe packages to a **121,640-byte `.tns`**;
and no `_dtoa`, `_strtod`, `_printf_float`, libm, `stdio` formatting, or ARM
soft-float helper reaches the image. Real IR atoms are ordered by their
IEEE-754 bit keys rather than by executing a floating-point comparison. The
corresponding IR check retains **53/53** entry points. Execution of the CAS
probe on the physical CX II is also complete: on 2026-07-26,
`phy-cas-smoke.tns` displayed **7/7 PASS** on the target OS 6.4.0.74 / Ndless
r2022 calculator and `ESC` returned normally to Documents. That test is an
observable backend acceptance screen, not a claim that the notebook UI is
implemented.
