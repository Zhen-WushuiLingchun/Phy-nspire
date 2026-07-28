# F3 Exact Number Foundation Implementation Plan

> **For Codex:** Execute task-by-task in the existing isolated feature branch.
> Do not delegate this plan to subagents or Claude Code.

**Goal:** Replace the scalar CAS's `int64` overflow cliff with a native,
bounded-memory arbitrary-precision integer/rational domain, then build a
canonical real-algebraic representation that later polynomial, series, limit,
and equation algorithms can trust.

**Architecture:** Keep the existing inline `int64` IR atoms as the small-value
fast path. Add a portable base-2^32 limb engine with explicit byte, limb, and
work ceilings; operations are transactional and return typed errors rather
than wrapping, truncating, or falling back to floating point. Big IR atoms
reuse the existing decimal and `(rat n d)` text forms so old documents remain
readable. Real algebraic values are identified by a primitive square-free
minimal polynomial over `Z` and a rational isolating interval certified by a
Sturm sequence.

**Tech Stack:** ISO C11, `uint32_t` limbs with `uint64_t` products,
`phy_alloc`/`phy_free`, the existing typed IR/CAS/status system, CTest,
ASan/UBSan/leak detection, and the pinned Ndless r2022 ARM toolchain.

---

## Non-negotiable semantic contracts

1. “Arbitrary precision” means no fixed mathematical word size. A caller may
   raise the configured limb/byte ceiling; a finite calculator operation may
   still return `PHY_ERR_MEMORY_LIMIT` or `PHY_ERR_TIMEOUT`.
2. Zero has one representation. Integers are signed magnitude; rationals are
   reduced and have a strictly positive denominator.
3. Every public operation is alias-safe and transactional. On any error,
   destination values and live-byte accounting are unchanged.
4. Division has documented truncation semantics and satisfies
   `a = q*b + r`, `|r| < |b|`; rational normalization never depends on host
   signed-overflow behaviour.
5. No exact operation imports `libm`, a floating-point formatter, or an ARM
   soft-float helper.
6. Existing small integer/rational node kinds, source spelling, and saved
   notebooks remain valid. Promotion must not change ordinary small-result
   serialization.
7. An algebraic value is accepted only after its interval is proved to contain
   exactly one real root of the normalized minimal polynomial.

## Task 1: Native arbitrary-precision integer kernel

**Files:**

- Create: `include/phy/exact.h`
- Create: `src/exact/exact_internal.h`
- Create: `src/exact/context.c`
- Create: `src/exact/integer.c`
- Create: `tests/test_exact.c`
- Modify: `CMakeLists.txt`
- Modify: `Makefile`

**Steps and tests:**

1. Add failing lifecycle, limit, telemetry, and allocation-failure tests.
2. Implement a context that charges all limb storage against `max_bytes`,
   resets a per-operation work counter, and supports cancellation.
3. Add canonical parse/write, copy, comparison, addition, subtraction,
   multiplication, power, single- and multi-limb division, exact division,
   and GCD.
4. Check decimal round trips at 1, 2, 32, 64, 65, 256, 1024, and the configured
   maximum bit length.
5. Verify algebraic identities and division invariants against deterministic
   golden decimal values, including negative operands and aliasing.
6. Sweep every allocation in parse, multiplication, division, and GCD; after
   every injected failure, validate all surviving values and telemetry.

## Task 2: Canonical arbitrary-precision rationals

**Files:**

- Create: `src/exact/rational.c`
- Extend: `include/phy/exact.h`
- Extend: `tests/test_exact.c`

**Steps and tests:**

1. Add failing tests for sign normalization, GCD reduction, zero denominator,
   cross-cancelled multiplication, addition, comparison, reciprocal, and
   integer powers.
2. Implement the rational layer exclusively through the integer API.
3. Verify that intermediate cross-cancellation lets large reciprocal products
   complete within a tight limb ceiling.
4. Add round-trip text tests and allocation/timeout failure sweeps.

## Task 3: Promote exact values through the IR without breaking documents

**Files:**

- Modify: `include/phy/ir.h`
- Modify: `src/ir/ir_internal.h`
- Modify: `src/ir/ir.c`
- Modify: `src/ir/order.c`
- Modify: `src/ir/text.c`
- Modify: `src/notebook/source.c`
- Modify: `src/render/math_layout.c`
- Modify: `src/render/ir_math_tree.cpp`
- Extend: `tests/test_ir.c`
- Extend: `tests/test_source.c`
- Extend: `tests/test_formula.c`
- Extend: `tests/test_document.c`

**Steps and tests:**

1. Add big integer/rational atoms while preserving the small atom fast path.
2. Parse decimal literals beyond `int64` and exact decimals with large scales.
3. Preserve bare decimal integer and `(rat numerator denominator)` serialized
   spellings; prove old fixture reads and new fixture round trips.
4. Define total exact-number ordering by sign and cross-products without
   floating point.
5. Render large numerators/denominators through the existing MathTree backend.

## Task 4: Replace the CAS coefficient cliff with promotion

**Files:**

- Modify: `src/cas/cas_internal.h`
- Replace internals in: `src/cas/num.c`
- Modify consumers in: `src/cas/engine.c`, `src/cas/simplify.c`,
  `src/cas/normal.c`, `src/cas/reduce.c`, `src/cas/integrate.c`
- Extend: `tests/test_cas.c`
- Extend: `tests/test_eval.c`
- Extend: `tests/device/cas_link_probe.c`

**Steps and tests:**

1. Introduce an exact coefficient facade with small inline operands and big
   promoted operands.
2. Convert every arithmetic call site before allowing reader-facing promotion.
3. Turn former `PHY_ERR_OVERFLOW` cases into exact results where only numeric
   width was the blocker; preserve overflow/unsupported statuses for exponent,
   degree, and term-count bounds that are not numeric-width problems.
4. Repeat zero-decision, `Cancel`, and `Factor` round-trip tests with 100- to
   1000-digit coefficients.
5. Prove the small path remains byte-for-byte stable for the existing tour.

## Task 5: Certified real algebraic-number foundation

**Files:**

- Create: `include/phy/algebraic.h`
- Create: `src/exact/algebraic.c`
- Create: `src/exact/sturm.c`
- Create: `tests/test_algebraic.c`
- Modify: `CMakeLists.txt`
- Modify: `Makefile`

**Steps and tests:**

1. Normalize integer polynomials to primitive, square-free, positive-leading
   form.
2. Implement exact polynomial pseudo-remainder and Sturm sequences over the
   new integer/rational domain.
3. Certify a rational interval by endpoint non-root checks and a root-count
   difference of exactly one.
4. Canonicalize equal descriptors and provide exact comparison by interval
   refinement; return a typed resource error if configured refinement limits
   are exhausted.
5. Cover `sqrt(2)`, `sqrt(2)/2`, the real roots of `x^3-2`, reducible and
   repeated-polynomial rejection, overlapping-interval refinement, and
   allocation failure.

## Task 6: Release gates and downstream handoff

**Files:**

- Modify: `docs/CAS_FOUNDATION.md`
- Modify: `docs/CAS.md`
- Modify: `docs/IR.md`
- Modify: `docs/BUILD.md`
- Modify: `docs/ROADMAP.md`
- Create/update device link probe for the exact layer

**Verification commands:**

1. Windows MSVC strict build and complete CTest.
2. WSL `PHY_WERROR=ON` build and complete CTest.
3. WSL ASan/UBSan/leak complete CTest.
4. Ndless ARM product build, exact/CAS/IR/evaluator link checks, symbol report,
   and size report.
5. Confirm exact probes contain no `_dtoa`, `_strtod`, `_printf_float`, libm,
   or soft-float helper.
6. Record product size and measured transient heap before enabling later
   multivariate GCD, modular factorization, `Apart`, `Series`, `Limit`, or
   `Solve`.

## Downstream order after F3

1. Coefficient-domain-generic univariate algorithms and modular factorization.
2. Sparse multivariate polynomial representation, monomial order, content,
   subresultant/modular GCD.
3. `Apart` over exact polynomial factors.
4. Truncated formal series rings.
5. Limits driven by rational/series order.
6. Polynomial solving with rational and certified algebraic roots before any
   transcendental solver.
