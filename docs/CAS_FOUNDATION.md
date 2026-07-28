# CAS foundation stabilization

This is the dependency-ordered plan for turning the existing narrow scalar
kernel into a dependable base for tensor calculus, GR, and QFT. New physics
domains are frozen while the foundation milestones below are active.

The goal is not to imitate every Mathematica command. It is to provide an exact,
typed, interruptible core whose supported class is large enough that upper
layers do not fail on routine algebra.

## Non-negotiable contracts

1. **Exact before approximate.** A rewrite either proves an exact result,
   returns an unevaluated typed expression, or returns a typed resource/domain
   error. It never samples floating-point values to guess an identity.
2. **One semantic path.** Reader syntax, evaluator dispatch, the public CAS API,
   and notebook display use the same typed IR. A command is not implemented
   until all four agree.
3. **Canonical internal names.** Mathematica-style spelling is accepted by the
   reader, while scalar function heads have one lowercase internal spelling.
   Mathematical constants have one protected spelling (`Pi`, `E`, `I`,
   `EulerGamma`).
4. **Principal branches, conservative rewrites.** Powers, roots, logarithms,
   inverse trigonometric functions, and inverse hyperbolic functions use their
   principal branches. Rewrites that need positivity, reality, or nonzero facts
   require corresponding assumptions; otherwise the expression stays explicit.
5. **Bounded algorithms.** Every walk charges the CAS step budget, every
   expansion charges the IR/CAS memory budgets, and potentially expensive
   integer or polynomial algorithms have documented degree/coefficient limits.
6. **Failure is transactional.** Resource exhaustion may invalidate one cell,
   but must not leave the notebook, memo cache, scratch arena, or bindings in a
   state that changes later mathematics.

## Milestone dependency order

### F0 — capability contract and regression matrix

- Freeze this document and a machine-readable/compiled test matrix.
- Pin positive and negative cases for every claimed command.
- Keep registered-but-unimplemented commands returning
  `PHY_ERR_UNSUPPORTED`; an inert head is not an implementation.

### F1 — exact elementary-function layer

Status: implemented in the current branch; physical-device timing/heap
acceptance remains required.

- Protected constants and exact elementary special values.
- Square-root/radical normalization inside the existing power IR.
- Inverse trigonometric and hyperbolic function heads.
- Chain-rule derivatives and the corresponding bounded elementary
  antiderivative rules.
- Source aliases, 2D display, palette entries, and notebook end-to-end tests.

This milestone deliberately does **not** introduce complex-number rewrites.
For example, `Sqrt[-1]` remains explicit until `I` has Gaussian-rational
semantics.

### F2 — polynomial and rational algebra

Status: the bounded univariate `Q[x]` GCD, reader-facing `Cancel`, Yun
square-free decomposition, and degree-48 modular
Berlekamp/Hensel/Zassenhaus `Factor` are implemented. Their coefficient
containers use the F3 arbitrary-precision exact domain. A bounded mixed-radix
multivariate cancellation subset is implemented with exact reconstruction
checks. Reader-facing univariate `Apart` is implemented over the same bounded
`Q[x]` domain; complete general multivariate GCD remains open.

- A bounded polynomial view with explicit variable order.
- Content/primitive-part extraction and exact coefficient division.
- Univariate polynomial GCD, then multivariate GCD by a separately validated
  algorithm.
- `Cancel`, robust `Together`, square-free decomposition, `Factor`, and
  `Apart`, in that order.

Polynomial Euclidean division, GCD, square-free decomposition, factor
reconstruction, denominator LCDs, and univariate cancellation use exact IR
coefficients with a checked `int64` fast path and arbitrary-precision fallback.
The rational-root candidate enumerator remains a bounded fast path. Inputs
outside it now continue through an exact monic-integer transform, good-prime
selection, deterministic Berlekamp factorization, pair Hensel lifting beyond a
Landau--Mignotte bound, and exact recombination. Modular derivative GCD plus CRT
prevents the initial square-free decomposition from falling back onto
coefficient-swelling rational Euclid for promoted inputs.

For expanded multivariate inputs whose Kronecker image has degree at most 48,
the reducer computes an image GCD, decodes candidate divisors, and accepts one
only if the decoded divisor times each decoded quotient exactly reconstructs
both original polynomials. This closes a useful cancellation subset, including
arbitrary-precision coefficients, without mistaking substitution artefacts for
real common factors. A sparse polynomial representation plus a complete
validated Brown/Zippel/subresultant-style algorithm is still required for the
general milestone.

The modular layer is now present end to end: a fixed-footprint exact `F_p[x]`
kernel with verified-prime contexts, Euclidean and extended GCD, modular
products/powers, square-free decisions, and deterministic Berlekamp
factorization; a non-truncating bigint-to-word residue bridge; CRT derivative
GCD reconstruction; bounded pair Hensel lifting; and exact Zassenhaus
recombination. See [`POLYNOMIAL.md`](POLYNOMIAL.md).

`Apart` first extracts the polynomial quotient, then factors the monic
denominator with that kernel. Numerators over every irreducible factor power
are obtained from an exact rational linear system. The solved coefficients are
substituted back into the original coefficient matrix before any reader-facing
sum is published. Multivariate and algebraic-extension partial fractions remain
typed unsupported cases.

### F3 — exact number domains

Status: native bounded-memory arbitrary-precision integers/rationals are
implemented and flow through IR, source, scalar folding, evaluator,
serialization, and MathTree display. The certified real-algebraic foundation
(primitive square-free defining polynomial, rational isolating interval, Sturm
count/refinement/comparison) is implemented and documented in
[`ALGEBRAIC.md`](ALGEBRAIC.md). Algebraic arithmetic, Gaussian rationals, and
canonical minimal-polynomial equality remain open. The univariate polynomial
coefficient containers and rational LCD path have been migrated.

- Native bounded-memory arbitrary-precision integers and rationals.
- Gaussian rationals and exact `I`, `Conjugate`, `Re`, `Im`, and `Abs`.
- Real algebraic numbers represented by a primitive square-free defining polynomial
  plus a certified rational isolating interval; canonical minimal-polynomial
  equality follows after modular factorization can certify irreducibility.
- Safe root comparison and denominator rationalization on that certified
  domain.

The native choice has host strict/ASan, serialization, allocation-failure, and
complete public-API ARM link/size evidence. Physical CX II timing/peak-heap
acceptance, Gaussian rationals, algebraic arithmetic, and canonical
minimal-polynomial equality are still required before F3 can be closed.

### F4 — series, limits, and equations

Status: the exact bounded Taylor/Laurent ring, reader-facing `Series` /
`Normal` path, and a proof-producing exact `Limit` subset are implemented.
Rational expressions expand about arbitrary
exact rational centers; exact Maclaurin recurrence/composition covers
`Exp`, `Sin`, `Cos`, `Tan`, `Sinh`, `Cosh`, `Tanh`, `ArcSin`, `ArcTan`,
`Log[1+u]`, and rational binomial powers. Results are typed `SeriesData`
operators, preserve the order term across save/open, and render through the
shared MathTree backend. Coefficients presently remain rational: an expansion
whose coefficients require an unsupported transcendental constant returns a
typed unsupported result. Finite limits use exact substitution at structurally
certified continuous points and the Laurent leading term at singular points.
`Infinity` and `-Infinity` use an exact `t=1/x` reduction. One-sided pole signs
come from coefficient sign and valuation parity; a two-sided pole with unequal
directions is `PHY_ERR_DOMAIN`. Oscillatory, branch-sensitive, or otherwise
undecidable cases remain typed unsupported rather than being sampled.

The remaining implementation is governed by
[`plans/2026-07-28-cas-foundation-f4-f5.md`](plans/2026-07-28-cas-foundation-f4-f5.md).
The compiled reader matrix is `tests/corpus/cas_foundation_cases.inc`.
`Solve`, `NSolve`, and `Reduce` remain typed unsupported until their
corresponding exact backend, evaluator, display, and negative controls land
together.

- Truncated formal power-series arithmetic before reader-facing `Series`.
- Extend the exact limit subset only alongside proof rules and negative
  controls; there is no numerical sampling fallback.
- Polynomial `Solve` before transcendental solving; solutions carry conditions
  rather than silently dropping branches.

### F5 — special-function kernel

Status: first bounded pack implemented (`Gamma`, `LogGamma`, `Erf`, `Erfc`);
Bessel families, polylogarithms, recurrence metadata, and a numerical layer
remain open.

- `Gamma`, factorial/rising factorial, `Erf`/`Erfc`, Bessel families, and
  polylogarithms are added by a descriptor table.
- Each function starts with exact special values, derivatives, symmetries,
  recurrences, and domain metadata. Numerical evaluation is a later,
  separately bounded interval/ball layer.
- An unsupported transform or integral remains explicit; table lookup never
  masquerades as a general integration algorithm.

## Acceptance matrix

Every milestone must supply all of the following evidence:

| Boundary | Required evidence |
| --- | --- |
| IR | stable serialize/read round trip and validator pass |
| scalar CAS | exact normal-form tests, negative controls, budget failures |
| calculus | differentiate every new antiderivative back to the input |
| source/evaluator | Mathematica-style input reaches the same CAS result |
| display | typed result renders without reparsing source text |
| notebook | save/open/replay and IR-saturation recovery |
| host | strict Windows suite and WSL ASan/UBSan/leak suite |
| device | ARM link/size/symbol report, then explicit CX II timing/heap run |

Host and byte-identical transfer evidence do not count as physical-device
runtime acceptance.
