# Canonical Complex Algebraic Numbers Implementation Plan

**Goal:** Deliver canonical exact complex algebraic values, resultant-closed
arithmetic, complete bounded complex algebraic univariate `Solve`, and exact
characteristic-polynomial/eigenvalue support.

**Architecture:** Extend the existing algebraic context with a separate
complex value type sharing the exact integer/rational kernel and bounded
factorizer. Place exact complex-root certification below the CAS layer. Keep
minimal polynomial plus canonical all-complex root ordinal as identity and use
isolating rectangles only as refinable certificates.

**Tech Stack:** C11 exact arithmetic, Sturm real-root isolation, exact
Pellet--Rouche complex rectangles, bounded polynomial factorization and
resultants, typed IR/evaluator, CTest, ASan/UBSan/leak checks, Wolfram Language
development oracle, Ndless r2022 ARM toolchain.

---

## Task 1: Freeze identity and complex-root certification

**Files:**

- Modify: `include/phy/algebraic.h`
- Modify: `src/exact/algebraic.c`
- Move/refactor: `src/exact/complex_roots.c`, `src/exact/complex_roots.h`
- Modify: `CMakeLists.txt`, `Makefile`, ARM link scripts
- Add/extend: algebraic and complex-root tests

**Steps:**

1. Add `phy_complex_algebraic` ownership, validation, accessors, destruction,
   equality, hash, and exact rectangle accessors.
2. Isolate and canonically order all roots: exact Sturm real block followed by
   exactly ordered non-real rectangles.
3. Canonicalize reducible input to an irreducible minimal polynomial and root
   ordinal before publication.
4. Add explicit real-to-complex lifting and exact conjugation.
5. Prove deterministic identity across different defining polynomials,
   rectangles, isolation precisions, and allocation/cancellation retries.

## Task 2: Close complex algebraic arithmetic

**Files:**

- Modify: `src/exact/algebraic.c`
- Modify: `include/phy/algebraic.h`
- Extend: algebraic tests and ARM algebraic probe

**Steps:**

1. Generalize the existing sum/product resultant constructors for complex
   certificate selection.
2. Implement add, subtract, multiply, divide, reciprocal, and bounded integer
   power.
3. Refine operand/result rectangles until exactly one irreducible result root
   is selected, or return a typed resource error.
4. Cover rational, Gaussian, real-algebraic, conjugate, cancellation, degree
   growth, coefficient growth, and allocation-failure cases.
5. Check reconstruction identities exactly; never accept a numeric residual.

## Task 3: Migrate `Root` and complete exact univariate `Solve`

**Files:**

- Modify: `src/cas/reduce.c`, `src/cas/solve.c`
- Modify: `include/phy/cas.h`
- Extend: `tests/test_cas.c`, `tests/test_eval.c`
- Add: `tests/oracle/wolfram_complex_algebraic.wlt`

**Steps:**

1. Change reader-facing `Root` generation from real-root ordinal to the
   canonical all-complex ordinal while preserving existing real ordinals.
2. Publish every certified root of each irreducible rational factor.
3. Keep `Solve` distinct-root semantics and exact denominator validation.
4. Add exact tests for irreducible quadratics, cubics, quartics and bounded
   higher degree polynomials containing mixed real/non-real roots.
5. Compare polynomial identities, ordering, conjugation and root counts with
   Wolfram oracle fixtures; host tests remain independently runnable.

## Task 4: Add characteristic polynomials and eigenvalues

**Files:**

- Modify: `include/phy/linear.h`, `src/linear/*`
- Modify: `src/eval/env.c`, `src/eval/dispatch.c`, `src/notebook/source.c`
- Modify: `src/notebook/palette.c`
- Extend: `tests/test_linear.c`, `tests/test_eval.c`

**Steps:**

1. Implement exact `CharacteristicPolynomial[matrix, variable]` as
   `det(variable I - matrix)` with the matrix's existing step and memory
   budgets.
2. Factor the characteristic polynomial and publish exact eigenvalues with
   algebraic multiplicity.
3. Register `CharacteristicPolynomial` and `Eigenvalues` in evaluator,
   source-validation, display, and command palette paths.
4. Test rational, repeated, irreducible real, irreducible complex, triangular,
   and resource-limited matrices.

## Task 5: Verify and publish the milestone

**Files:**

- Modify: `docs/ALGEBRAIC.md`, `docs/CAS.md`, `docs/ROADMAP.md`
- Modify: `docs/CAS_ACCEPTANCE.md`

**Steps:**

1. Run focused algebraic, CAS, linear, evaluator, renderer, persistence, and
   device-link tests.
2. Run the Wolfram oracle in one evaluator session and record the failed
   context-helper attempt separately from successful evaluator evidence.
3. Run the complete Release suite.
4. Run the complete ASan/UBSan/leak suite.
5. Run clean Ndless algebraic/CAS/evaluator/product probes and reject libm,
   float formatting, and soft-float dependencies.
6. Commit and push only after every software gate passes. Do not upload to a
   calculator without a separate explicit request.
