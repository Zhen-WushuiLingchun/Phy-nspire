# CAS Foundation F4/F5 Implementation Plan

> **For Codex:** Execute this plan locally, task by task, without delegating to
> subagents or Claude Code. Commit only independently verified milestones.

**Goal:** Extend the exact F3/polynomial base into a bounded but coherent
general-purpose calculator CAS: formal series, exact limits, polynomial
equation solving, complex exact arithmetic, algebraic-number closure, and a
broader special-function/calculus pack, followed by physical CX II acceptance.

**Architecture:** Keep the existing interned typed IR and exact coefficient
facade as the only semantic path. Add a truncated formal-series ring whose
coefficients are exact IR atoms and whose valuation is explicit; drive finite
and infinite rational/analytic limits from that ring. Polynomial solving
returns exact rational values or certified algebraic-root descriptors rather
than decimal guesses. Complex and algebraic operations extend the exact value
domain under the same step/limb/byte ceilings.

**Tech Stack:** ISO C11, existing base-2^32 exact layer, typed IR, native CAS,
stateful evaluator, nMarkdown MathTree bridge, CTest, ASan/UBSan/leak checks,
Ndless r2022 ARM toolchain.

---

## Completion boundary

“Average CAS foundation” in this project means the following bounded,
reader-visible capabilities work coherently and exactly:

- arbitrary-precision integer/rational arithmetic;
- polynomial arithmetic, GCD, factorization and partial fractions;
- Taylor/Laurent series for rational functions and supported analytic heads;
- finite-point and infinity limits, including explicit one-sided pole signs;
- exact polynomial solving through the bounded degree, with certified
  `Root[...]` values when radicals are not appropriate;
- Gaussian-rational complex arithmetic and conservative `Re`/`Im`/
  `Conjugate`/`Abs`;
- exact differentiation and a useful table-driven integration/special-function
  layer;
- typed unsupported/resource results outside every documented class.

It does **not** mean feature-count parity with Mathematica, Maple, or SymPy.
Gröbner bases, Risch integration, general transcendental solving, arbitrary
analytic continuation, high-performance LLL, and numerical ball arithmetic are
separate later milestones.

## Non-negotiable contracts

1. No float sampling may decide a series coefficient, limit, root, factor, or
   branch.
2. A truncated series stores its center, valuation, truncation order and
   coefficient array explicitly. Missing coefficients mean exact zero, never
   “not computed”.
3. Division is allowed only when the divisor has a proven nonzero leading
   coefficient. Negative valuations produce Laurent series.
4. `Series` always exposes its order term. Dropping `O((x-a)^n)` requires an
   explicit `Normal` operation.
5. A two-sided limit exists only when both one-sided limits are proved equal.
   Pole signs require sign/side evidence; otherwise return `Indeterminate` or a
   typed unsupported result, not an arbitrary infinity.
6. `Solve` preserves multiplicity metadata internally and returns conditions
   needed for divisions or branch choices. No root is discarded because a
   denominator “probably” is nonzero.
7. Exact complex and algebraic outputs serialize, reopen and render without
   reparsing display text.
8. Every public operation is transactional, cancellable and charged to the CAS
   byte/step ceilings.

## Task 1: Freeze the F4/F5 capability matrix

**Files:**

- Modify: `docs/CAS_FOUNDATION.md`
- Modify: `docs/CAS_ACCEPTANCE.md`
- Modify: `docs/SOURCE_LANGUAGE.md`
- Create: `tests/corpus/cas_foundation_cases.inc`

**Steps:**

1. Add positive, negative, branch-sensitive and resource-limit rows for
   `Series`, `Normal`, `Limit`, `Solve`, complex heads and new special
   functions.
2. Give every row an exact reader input, canonical IR/result class and expected
   status.
3. Compile the corpus into host tests so documentation and command dispatch
   cannot drift.
4. Commit the matrix before enabling any reserved command.

## Task 2: Exact truncated formal-series ring

**Files:**

- Create: `src/cas/series_internal.h`
- Create: `src/cas/series.c`
- Modify: `src/cas/cas_internal.h`
- Modify: `CMakeLists.txt`
- Modify: `Makefile`
- Create: `tests/test_series.c`

**Steps:**

1. Write failing lifecycle and canonicalization tests for an explicit
   `(variable, center, valuation, order, coefficients[])` representation.
2. Implement bounded zero/constant/monomial construction and validation.
3. Implement exact add, subtract, Cauchy product, integer power, derivative,
   integral, reciprocal and division.
4. Add composition for a zero-constant inner series.
5. Prove ring identities exhaustively for small rational coefficient vectors.
6. Cover aliasing, negative Laurent valuations, zero divisors, cancellation,
   step exhaustion and allocation-failure rollback.
7. Commit the internal ring before adding reader semantics.

## Task 3: Analytic expansion and reader-facing `Series`

**Files:**

- Modify: `include/phy/cas.h`
- Modify: `include/phy/source.h`
- Modify: `src/notebook/source.c`
- Modify: `src/eval/display.c`
- Modify: `src/notebook/palette.c`
- Modify: `src/cas/series.c`
- Modify: `src/render/ir_math_tree.cpp`
- Modify: `tests/test_source.c`
- Modify: `tests/test_eval.c`
- Modify: `tests/test_formula.c`
- Modify: `src/tools/make_cas_tour.c`

**Reader contract:**

```text
Series[expr,{x,center,n}]
Normal[series]
```

`n` is a nonnegative exact machine-sized order bounded by the series ceiling.
The output retains `O((x-center)^(n+1))`.

**Steps:**

1. Extend top-level command parsing with a typed series specification rather
   than overloading the derivative variable array.
2. Expand exact rational functions at arbitrary exact rational centers.
3. Add zero-center recurrences for `Exp`, `Sin`, `Cos`, `Sinh`, `Cosh`,
   `Log[1+u]`, `(1+u)^q`, `ArcTan`, `ArcSin`, `Erf`, then compose with a
   supported inner series.
4. Translate centers through `u=x-center` without numeric approximation.
5. Build a typed `SeriesData`/order-term IR result and a MathTree path that
   renders the polynomial plus `O(...)`.
6. Make `Normal` strip only a well-formed series order term.
7. Add generator/reopen/replay coverage and an executable tour cell.
8. Run strict and sanitizer suites, then commit.

## Task 4: Exact finite and infinite limits

**Files:**

- Modify: `include/phy/cas.h`
- Modify: `include/phy/source.h`
- Modify: `src/notebook/source.c`
- Create: `src/cas/limit.c`
- Modify: `CMakeLists.txt`
- Modify: `Makefile`
- Extend: `tests/test_series.c`
- Extend: `tests/test_eval.c`

**Reader contract:**

```text
Limit[expr,{x,point}]
Limit[expr,{x,point,Direction->"FromAbove"|"FromBelow"}]
Limit[expr,{x,Infinity}]
Limit[expr,{x,-Infinity}]
```

The first implementation may encode direction as a canonical symbol in the
typed specification before the general `Rule` language exists.

**Steps:**

1. Decide polynomial/rational infinity limits from numerator/denominator
   degree and exact leading-coefficient sign.
2. At finite points, cancel removable polynomial factors and compare exact
   numerator/denominator valuations.
3. Use the formal-series leading term for supported analytic compositions.
4. Implement one-sided pole signs from parity, direction and exact coefficient
   sign.
5. Require left/right equality for two-sided results.
6. Add removable, finite, infinite, oscillatory/unknown and branch-sensitive
   negative controls.
7. Commit only when parser, evaluator and CAS agree.

## Task 5: Exact polynomial `Solve`

**Files:**

- Modify: `include/phy/cas.h`
- Modify: `include/phy/source.h`
- Modify: `src/notebook/source.c`
- Create: `src/cas/solve.c`
- Modify: `src/exact/algebraic.c`
- Modify: `include/phy/algebraic.h`
- Modify: `src/render/ir_math_tree.cpp`
- Create: `tests/test_solve.c`
- Extend: `tests/test_eval.c`

**Reader contract:**

```text
Solve[equation,x]
Solve[{equation,...},{x,...}]
```

The first completed class is one polynomial equation in one symbol. System
syntax is reserved until a real elimination algorithm exists.

**Steps:**

1. Normalize `lhs==rhs` to a primitive polynomial over `Q[x]`, rejecting
   denominator roots separately.
2. Return exact rational roots from the factorization workspace.
3. Return real certified `Root[p,k]` descriptors using Sturm isolation for
   higher irreducible factors.
4. Add Gaussian-rational roots for irreducible quadratics with negative
   discriminant after Task 6.
5. Preserve multiplicities in the internal solution object while returning the
   Mathematica-compatible distinct solution rules by default.
6. Verify every returned root by exact substitution/minimal-polynomial
   membership and every omitted interval by Sturm root counts.
7. Keep multivariate and general transcendental systems typed unsupported.

## Task 6: Gaussian rationals and exact complex heads

**Files:**

- Extend: `include/phy/exact.h`
- Create: `src/exact/gaussian.c`
- Modify: `src/cas/num.c`
- Modify: `src/cas/simplify.c`
- Modify: `src/notebook/source.c`
- Modify: `src/render/ir_math_tree.cpp`
- Extend: `tests/test_exact.c`
- Extend: `tests/test_cas.c`

**Steps:**

1. Represent `a+b I` with canonical arbitrary-precision rational parts.
2. Implement add/multiply/divide/conjugate/norm/integer power transactionally.
3. Give `I^2=-1` exact semantics without treating a free symbol named `I` as a
   polynomial generator.
4. Implement exact `Re`, `Im`, `Conjugate`; implement `Abs` only where its
   square root result is representable exactly/algebraically.
5. Add serialization, ordering, display and allocation-failure coverage.

## Task 7: Real algebraic arithmetic closure

**Files:**

- Modify: `include/phy/algebraic.h`
- Modify: `src/exact/algebraic.c`
- Create: `src/exact/algebraic_resultant.c`
- Extend: `tests/test_algebraic.c`

**Steps:**

1. Factor defining polynomials and canonicalize descriptors to irreducible
   minimal factors.
2. Implement rational translate/scale/inverse directly on defining
   polynomials and isolating intervals.
3. Implement algebraic add/multiply through bounded resultants, factor
   selection and interval certification.
4. Define equality by common canonical minimal polynomial plus certified root
   identity; comparison continues by interval refinement.
5. Add denominator rationalization and mixed rational/algebraic arithmetic.
6. Keep degree explosion bounded and transactional.

## Task 8: Broader calculus and special-function pack

**Files:**

- Modify: `src/cas/integrate.c`
- Modify: `src/cas/diff.c`
- Modify: `src/cas/simplify.c`
- Modify: `src/notebook/source.c`
- Modify: `src/notebook/palette.c`
- Extend: `tests/test_cas.c`
- Extend: `tests/test_eval.c`

**Steps:**

1. Add descriptor-driven `Factorial`, `Pochhammer`, `Binomial`,
   `PolyLog`, `BesselJ/Y/I/K`, `AiryAi/Bi`, `Zeta` heads with domain metadata.
2. Start each head with exact special values, parity/symmetry, derivative and
   recurrence rules; do not add fake numerical evaluation.
3. Integrate all rational functions through `Apart` plus logarithm/arctangent
   primitives.
4. Add bounded integration by parts for polynomial times exponential,
   trigonometric and logarithmic factors.
5. Differentiate every new antiderivative and require exact equivalence to its
   input.

## Task 9: Release and first physical-device acceptance

**Files:**

- Modify: `docs/CAS.md`
- Modify: `docs/CAS_ACCEPTANCE.md`
- Modify: `docs/BUILD.md`
- Modify: `README.md`
- Modify: `README.zh-CN.md`
- Regenerate: `examples/phy-nspire-cas-tour.tns`

**Steps:**

1. Run Windows strict complete CTest.
2. Run WSL warning-as-error and ASan/UBSan/leak complete CTest.
3. Run exact/CAS/IR/evaluator ARM link checks, symbol scan and product size.
4. Record assertion counts, product bytes and CAS text/probe bytes.
5. Push the verified commits.
6. Deploy the source-only tour and `dist/phy-nspire.tns` with
   `tools/nlinkctl deploy --verify sha256 --remove-backup`.
7. Read both files back and compare SHA-256.
8. On the physical CX II, record:
   - launch and clean exit;
   - `MemoryStatus[]` before/after the tour;
   - representative big integer, Factor, Apart, Series, Limit and Solve cells;
   - cancellation response;
   - peak observed heap and wall-clock time;
   - save/reopen/replay.
9. Treat upload/readback as transport evidence only; physical UI results must
   be reported separately.
