# Certified Numeric Precision Contract Implementation Plan

**Status:** Completed and verified on 2026-08-12. The current artifact has not
been uploaded to a calculator; physical acceptance remains a separate gate.

**Goal:** Make `N` and `NSolve` honor one checked significant-digit contract end to end, preserve the requested precision in typed IR, render certified balls without a fixed ten-digit truncation, and certify clustered complex roots through bounded precision escalation.

**Architecture:** Exact symbolic values remain exact and all numerical decisions remain in rational real/complex balls. Published `Around` values gain a private, backward-compatible third integer child carrying requested display precision; old two-child notebook values remain readable. `N` and every `NSolve` path must prove a mixed significant-digit tolerance before publication. The complex root candidate search may increase dyadic precision a bounded number of times, but the final Pellet--Rouche proof and all-or-nothing publication remain unchanged.

**Tech Stack:** C11 exact/ball/CAS kernels, C++17 nMarkdown `MathTree` renderer, CTest, Wolfram Language development oracle, Ndless ARM toolchain.

---

### Task 1: Pin the numeric accuracy contract with failing tests

**Files:**
- Modify: `tests/test_cas.c`
- Modify: `tests/test_eval.c`
- Modify: `tests/test_ir_math_tree.cpp`
- Modify: `tests/oracle/wolfram_complex_numeric.wlt`

**Steps:**

1. Add helpers that decode `Around[mid,radius,digits]` and `ComplexAround` from typed IR without converting through `double`.
2. Add failing tests proving requested digits are preserved for `N[Pi,30]`, `N[1/3,12]`, composed elementary functions, all three `NSolve` publication paths, and save/reopen.
3. Add a clustered-root regression such as `NSolve[x^2+1/10^40==0,x,16]`; require two certified disjoint branches rather than `PHY_ERR_TERM_LIMIT`.
4. Add renderer tests for legacy two-child `Around`, new precision-bearing `Around`, negative imaginary parts, very small/large values, and outward containment after decimal formatting.
5. Extend the Wolfram oracle with precision, clustered-root and residual checks. Host tests remain independently reproducible without Wolfram.

Run the focused tests and verify that the new assertions fail for the documented reason before implementation.

### Task 2: Carry precision in the published certified IR

**Files:**
- Modify: `src/cas/ball_eval.c`
- Modify: `include/phy/cas.h`
- Modify: `docs/CAS.md`
- Modify: `docs/BALL_ARITHMETIC.md`

**Steps:**

1. Change private ball publication helpers to emit `Around[midpoint,radius,digits]`; keep `ComplexAround[realAround,imagAround]` unchanged structurally.
2. Thread normalized requested digits through `N`, the quadratic `NSolve` path, the all-real Sturm path and the general complex path.
3. Keep the renderer and validator compatible with existing two-child `Around` objects so saved notebooks do not require migration.
4. Define the checked contract as `radius <= 10^-d * max(1, |midpoint|)` for each rectangular component. Exact inputs may retain radius zero.
5. Before publishing, verify the contract using exact rational comparison. If a bounded retry cannot satisfy it, return `PHY_ERR_TERM_LIMIT` and publish nothing.

### Task 3: Add bounded complex-root precision escalation

**Files:**
- Modify: `src/cas/complex_roots.c`
- Modify: `src/cas/complex_roots.h`
- Modify: `src/cas/ball_eval.c`
- Modify: `tests/test_cas.c`

**Steps:**

1. Retain the requested bit count as the minimum output accuracy.
2. When fixed-radius boxes cannot be made disjoint or cannot all pass `pellet_one`, increase the candidate precision and decrease the radius geometrically.
3. Cap escalation levels and Durand--Kerner iterations per level. Charge all work to the existing exact/CAS step and cancellation budgets.
4. Restart candidate centres once on the full fallback grid after the economical first level; retain those high-precision centres while later certificate boxes shrink. Never treat approximate iteration convergence as a certificate.
5. Publish only after the existing exact Pellet--Rouche one-root tests pass for every pairwise-disjoint box and root count equals the square-free degree.
6. On exhaustion return `PHY_ERR_TERM_LIMIT`, leave `*out_ref == PHY_IR_NULL`, and keep contexts valid.

### Task 4: Render requested significant digits with an outward certificate

**Files:**
- Modify: `src/render/ir_math_tree.cpp`
- Modify: `tests/test_ir_math_tree.cpp`
- Modify: `tests/test_notebook.c`

**Steps:**

1. Read precision from the optional third `Around` child and clamp it to the public 1--36 digit range; use the legacy display default only for old two-child values.
2. Replace fixed fractional-place output with exact decimal/scientific formatting at the requested significant-digit count.
3. Truncate the displayed midpoint and round the displayed radius outward, then add one midpoint ulp so the visible interval encloses the exact rational certificate.
4. Use scientific notation when fixed notation would erase the leading significant digit or overflow the calculator row.
5. Keep lowercase upright `i` and the existing negative-imaginary layout.
6. Generate and visually inspect 320x240 framebuffer artifacts for ordinary, tiny, large and complex results.

### Task 5: Verify all mathematical and platform gates

**Files:**
- Modify: `docs/CAS_ACCEPTANCE.md`
- Modify: `docs/ROADMAP.md`

**Steps:**

1. Run focused `test_ball`, `test_cas`, `test_eval`, `test_ir_math_tree` and `test_notebook` tests.
2. Attempt `WolframLanguageContext`, then run the extended `.wlt` through one Wolfram evaluator/TestReport session and record the evidence boundary.
3. Run the complete Release suite.
4. Run the complete ASan/UBSan/leak suite.
5. Run clean Ndless ball/CAS/evaluator link probes and product build; reject float formatters, libm and soft-float helpers.
6. Record artifact size but do not upload to the calculator without a separate explicit user request.

### Next dependency-ordered tranche

After this plan passes, build a canonical `phy_complex_algebraic` domain: primitive irreducible minimal polynomial, deterministic all-complex-root identity, certified isolating rectangle, exact equality/hash/conjugation, resultant-closed arithmetic, reader-facing `Root` arithmetic and exact `Solve` completeness. That work must also define and migrate the current real-only `Root[...,k]` index convention before more persisted exact roots are published.
