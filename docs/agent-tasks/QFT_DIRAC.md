# Agent task pack — Dirac algebra, contraction, Mandelstam, SU(N)

Date: 2026-07-26

Executable contracts for the Phase 5 MVP defined in
[`docs/references/QFT_GAUGE.md`](../references/QFT_GAUGE.md). Each contract
states its inputs, its deliverable paths, its acceptance test, and what it is
forbidden from doing. An agent should be able to pick up one contract, work it
to completion, and have the acceptance test decide whether it is done.

**Read the reference document first.** The conventions in its §4 and §5 are
binding, and roughly half of the failure modes in this area are convention
errors rather than algorithm errors.

---

## 0. Ground rules for every contract

1. **Scope is closed.** `docs/references/QFT_GAUGE.md` §2 lists what is
   deferred. A contract here never widens it. If a contract appears to require
   `γ⁵`, Fierz, spinors, loops or Yang-Mills connections, the contract is
   wrong — stop and say so rather than implementing the deferred item.
2. **Write from the specification, not from upstream source.** Every contract
   below is designed to be implementable from the identities and algorithm
   descriptions in the reference document alone. Do not port upstream
   implementation code. If a contract seems to require it, stop and escalate:
   incorporating and distributing someone else's implementation raises
   licensing questions that need project licensing and legal review, and that
   review has not happened. See the reference §10.
3. **Warnings are errors.** The host build runs `-Wall -Wextra -Wshadow
   -Wconversion -Wsign-conversion -Wstrict-prototypes -Wmissing-prototypes
   -Wpointer-arith -Wcast-qual -Werror`. Code that needs a warning suppressed
   is code that needs rewriting.
4. **Nothing here ships to the device yet.** These contracts build host-testable
   core. The device binary must not grow a QFT dependency until Q-7.
5. **Every identity needs a test.** An identity added to reference §7 without a
   check in `tests/oracle/test_qo_golden.c` is not verified and must be
   labelled so.

**Dependency order.** Q-1 → Q-2 → Q-3 → Q-4 are strictly sequential. Q-5 and
Q-6 depend only on Q-1, and are independent of each other and of Q-2…Q-4, so
all three can proceed in parallel once Q-1 lands. Q-7 is last. Q-0 is already
done and is listed for provenance.

**Current state.** The typed IR is merged on `main`, so **Q-1 is ready to
start** and is the only thing standing between the pack and three parallel
tracks.

---

## Q-0 — Numeric oracle and golden cases *(complete)*

**Status:** complete. Landed together with this document.

**Deliverables.**

- `tests/oracle/qo_complex.h` — complex scalar arithmetic, MSVC-safe.
- `tests/oracle/qo_dirac.{h,c}` — 4-vectors, 4×4 complex matrices, gamma
  matrices in the Dirac and Weyl representations, slashes, the metric trace
  recursion.
- `tests/oracle/qo_color.{h,c}` — generalised Gell-Mann su(N) generators,
  structure constants, Casimirs.
- `tests/oracle/test_qo_golden.c` — the golden cases G-1…G-12 and C-1…C-7.
- `CMakeLists.txt` — `test_qo_golden` target and ctest registration.

**Acceptance, met.** `ctest` reports 5/5 passing; `test_qo_golden` reports
**44,295 checks, 0 failures**.

**Why it exists.** The oracle is not a component of the product. It is the
evidence that the identities in the reference document are correct, so the
later contracts can be tested against something better than a transcription.
It links against nothing from `phy_core` on purpose, so that a core regression
cannot mask a physics regression.

---

## Q-1 — Lorentz index and scalar-product objects in the typed IR

**Depends on** the canonical typed expression IR — the interface in
[`include/phy/ir.h`](../../include/phy/ir.h) and its specification in
[`docs/IR.md`](../IR.md). **Both are merged on `main`, so Q-1 is ready to
start.** Write against those as merged; do not reintroduce a parallel index or
scalar-product representation alongside them.

Note what the IR does *not* carry, per `docs/IR.md`: no simplification, no
evaluation, no arithmetic, and no dummy-index canonicalization — that is Phase
2. Q-1 therefore adds the index and scalar-product *objects* on top of the IR's
node model, and stops there. Rewriting belongs to Q-2 onward.

**Deliver.**

- Index objects carrying: name, upper/lower position, and the
  **signature-bearing metric they belong to**. Reference §4.1 — the signature
  must be a property of the metric object, never a global constant, because
  the GR slice uses the opposite one.
- Scalar product `p·q` as a typed node, not a function call on opaque scalars.
- Momentum conservation as a declarable relation over a momentum set.
- On-shell substitution `pᵢ² → mᵢ²`.
- Free/dummy index classification and dummy renaming.

**Acceptance.**

1. A dummy index repeated in one term is detected; a free index is not.
2. Renaming a dummy index leaves the expression structurally equal under the
   IR's canonical form.
3. `p·q = q·p` holds structurally, not only after simplification.
4. Declaring `p₁ + p₂ = p₃ + p₄` lets `(p₁+p₂)²` and `(p₃+p₄)²` compare equal.
5. An index belonging to a `(+,−,−,−)` metric never silently contracts with one
   belonging to a `(−,+,+,+)` metric; the attempt is a typed error.

Acceptance 5 is the cross-phase guard. It is cheap now and expensive later.

**Do not.** Introduce `γ⁵`, Levi-Civita, or any `D ≠ 4` dimension tracking.

---

## Q-2 — Gamma matrix objects and Clifford normalisation

**Depends on** Q-1.

**Deliver.**

- A `gamma` node carrying a Lorentz index **and a spin-line identifier**.
- A slash node `p̸`, or `gamma` accepting a momentum in place of an index —
  FORM does the latter (`g_(1,p1)`) and it keeps one code path.
- An explicit identity element on each spin line (FORM's `gi_(j)`).
- Clifford normalisation: rewrite `γ^μ γ^ν → 2g^{μν}𝟙 − γ^ν γ^μ` under a
  canonical index order, so that equal expressions reach equal normal forms.
- `Tr[1]` as a **configurable parameter defaulting to 4**, not a literal.

**Acceptance.**

1. `{γ^μ, γ^ν} = 2g^{μν}𝟙` reproduces oracle case **G-1**.
2. `p̸q̸ + q̸p̸ = 2(p·q)𝟙` reproduces **G-10**.
3. Gamma matrices on **different spin lines commute**; on the same spin line
   they do not. This is FORM's second defining relation and a single flat
   noncommutative product cannot express it.
4. A trace node whose argument contains no gamma matrix is well defined and
   evaluates to the scalar times `Tr[1]`. This is FORM's `(g_(1,p)+m)` trap,
   reference §9.
5. Two expressions differing only by Clifford reordering have the same
   canonical form.

**Do not.** Implement traces yet. Implement `γ⁵`, `γ⁶`, `γ⁷` or chiral
projectors at all.

---

## Q-3 — Lorentz contraction of gamma strings

**Depends on** Q-2. **Reference** §6.1, §6.2 rules 1–4.

**Deliver.** Contraction of a repeated Lorentz index within one spin line,
implementing FORM's rules 1–4:

- rule 1, adjacent identical index or momentum collapses;
- rule 2, odd number of matrices between the contracted pair → `−2 ×` reversed
  string;
- rule 3, even number between → the two-term Chisholm-style form, with the
  `γ^μ γ^{m₁} γ^{m₂} γ_μ = 4 g^{m₁m₂} 𝟙` special case;
- rule 4, anticommute a repeated index into adjacency, then rule 1.

**Acceptance.**

1. Oracle cases **G-2, G-3, G-4, G-5** reproduce exactly.
2. **Mandatory regression, reference §9.** Both

   ```
   γ^μ γ_μ γ^ρ γ^σ      and      γ^ρ γ^σ γ^μ γ_μ
   ```

   must give `4 γ^ρ γ^σ` with `ρ` before `σ`. SymPy shipped this defect as
   issue #23823, where the second form returned `4 γ^σ γ^ρ`. A test that only
   checks the scalar factor `4` does **not** discharge this requirement.
3. Contraction is order-preserving for all free indices in every position, not
   only leading ones. Test with free indices before, between and after the
   contracted pair.
4. Contracting `n` nested pairs terminates and does not revisit a state.

**Do not.** Use FORM's rule 5 — it introduces `γ⁵` and Levi-Civita. The trace
recursion in Q-4 replaces it.

---

## Q-4 — Dirac traces without γ⁵

**Depends on** Q-3. **Reference** §6.2, §7.1, §8.

**Deliver.** Trace evaluation for a gamma string on one spin line, with no
`γ⁵`, via the metric recursion

```
Tr[γ^{m₁} ⋯ γ^{m_{2n}}] = Σ_{j=2}^{2n} (−1)^j g^{m₁ m_j} Tr[γ^{m₂} ⋯ ĝ^{m_j} ⋯ γ^{m_{2n}}]
```

terminating at `Tr[1] = Tr[1]-parameter`, with odd-length strings zero. Run
Q-3 contraction **before** expanding — that is the entire performance argument
of reference §8.

**Acceptance.**

1. **G-6** odd traces vanish; **G-8**, **G-9** closed forms; **G-11** the
   four-slash contracted form.
2. **G-7 is the real test.** For every assignment of Lorentz indices, exhaustively
   for `n = 2, 4, 6`, the engine's result must equal
   `qo_trace_recursive` from the oracle. Reuse the oracle directly as the
   comparison; do not re-derive expected values by hand.
3. Term counts follow `(2n−1)!!` for all-distinct indices: 1, 3, 15, 105 for
   `n = 1..4`. Assert the count, not only the value — a wrong count with a
   right value means terms are cancelling that should not exist.
4. Contraction-before-expansion is measurably cheaper: a string with two
   contracted pairs must generate strictly fewer intermediate terms than the
   same string traced without contracting first.
5. Exceeding the term ceiling produces a **typed resource-limit error**, per
   `docs/ARCHITECTURE.md`, and leaves the document saveable. It must not
   abort, and it must not return a partial expression as if it were complete.

**Resource limits.** Start from the proposals in reference §8 — 12 matrices per
chain, 4,096 generated terms — and treat them as provisional until measured on
hardware. They are flagged UNVERIFIED in reference §11.

**Do not.** Add `γ⁵`, even "just for the 4-dimensional case". Reference §4.5
gives the reason: the scheme apparatus cannot be retrofitted, and the MVP IR
has no dimension tracking to hang it on.

---

## Q-5 — Mandelstam variables

**Depends on** Q-1. Independent of Q-2…Q-4.

**Deliver.**

- Declaration of `s`, `t`, `u` for a four-momentum set, **with the routing
  recorded as part of the declaration**.
- On-shell substitution for the four masses.
- The sum rule `s + t + u = Σmᵢ²`.
- Elimination of one invariant using the sum rule.

**Acceptance.**

1. Oracle case **G-12**: both the Peskin routing (`p₁+p₂ → p₃+p₄`,
   `t = (p₁−p₃)²`) and the FeynCalc `SetMandelstam` routing (all-incoming,
   `p₁+p₂+p₃+p₄ = 0`, `t = (p₁+p₃)²`) are supported, and both produce
   `s = 100`, `t = −17`, `u = −65`, `s+t+u = 18` on the reference kinematics.
2. A Mandelstam declaration without a routing is **rejected**. Reference §4.4 —
   a bare `t = (p1+p3)^2` is not self-describing.
3. Eliminating `u` from an expression containing all three leaves no `u`.
4. The sum rule holds symbolically for arbitrary masses, not only for the
   numeric fixture.

**Optional, defer if it costs anything.** FeynCalc's `TrickMandelstam` chooses
whichever of `s`, `t`, `u` to eliminate so the result has fewest terms. Useful,
not required.

---

## Q-6 — SU(N) colour algebra

**Depends on** Q-1. Independent of Q-2…Q-5.

**Deliver.**

- Fundamental generators `T^a` with `Tr[T^aT^b] = δ^{ab}/2` (`T_F = 1/2`).
- Structure constants `f^{abc}`, real and totally antisymmetric.
- `δ^{ab}` contraction, and the commutator `[T^a,T^b] = i f^{abc} T^c`.
- Casimirs `C_F = (N²−1)/(2N)` and `C_A = N`.
- Presentation in terms of `C_F`/`C_A` rather than raw `N`, following
  FeynCalc's `SUNNToCACF -> True` default.

**Acceptance.**

1. Oracle cases **C-1…C-7** reproduce, including the exact textbook values
   `f^{123} = 1`, `f^{147} = 1/2`, `f^{458} = √3/2` at `N = 3`, and
   `C_F(3) = 4/3`.
2. `N` is symbolic: `C_F` simplifies to `(N²−1)/(2N)` without `N` being bound.
3. Colour and Dirac algebra do not interfere — a colour index never contracts
   with a Lorentz index, and the attempt is a typed error.
4. Colour traces longer than 3 generators are left unevaluated by default,
   matching FeynCalc's `SUNTraceEvaluate -> Automatic`.

**Do not.** Implement the Fierz/completeness identity
`T^a_{ij}T^a_{kl} = ½(δ_{il}δ_{kj} − δ_{ij}δ_{kl}/N)`. It is deferred: it needs
`1/N` singlet bookkeeping and a canonical spinor-chain ordering, neither of
which exists yet.

---

## Q-7 — Device build and resource measurement

**Depends on** Q-4 and Q-6. **This is the contract that settles reference §11.**

**Deliver.**

- The QFT module compiled into the ARM device binary.
- `make size-report` delta for the QFT module, against the 6 MB ceiling.
- `make symbol-report` confirming no host-only symbols leaked, per the Phase 0
  rule.
- Measured peak RSS and wall time on a real CX II for: a 4-matrix trace, an
  8-matrix trace, a 12-matrix trace, and one deliberately over-limit case.

**Acceptance.**

1. Size delta recorded in `research/upstreams.lock.json` or a successor
   measurement file.
2. The provisional limits in reference §8 are either confirmed or replaced with
   measured values, and reference §11's first row is discharged.
3. The over-limit case returns a typed resource-limit error on device, and the
   notebook remains saveable afterwards.
4. No `phy_host_*` symbol appears in the device binary.

**Note.** The core has now passed a real Ndless ARM link/package check, and
earlier notebook/CAS artifacts ran on the target calculator. The current
Dirac/QFT evaluator has not yet been timed or stress-tested on the CX II.
Everything in reference §8 therefore remains a provisional resource policy
until this contract's hardware measurements are recorded.

---

## Summary table

| ID | Contract | Depends on | Status |
| --- | --- | --- | --- |
| Q-0 | Numeric oracle and golden cases | — | **done** |
| Q-1 | Lorentz index and scalar-product objects | `include/phy/ir.h`, `docs/IR.md` | **done** |
| Q-2 | Gamma objects, Clifford normalisation | Q-1 | **done** |
| Q-3 | Lorentz contraction | Q-2 | **done** |
| Q-4 | Dirac traces without `γ⁵` | Q-3 | **done** |
| Q-5 | Mandelstam variables | Q-1 | **done** |
| Q-6 | SU(N) colour algebra | Q-1 | pending |
| Q-7 | Device build and measurement | Q-4, Q-6 | **ARM link done; CX II timing pending** |

Deferred, with the blocking dependency named, not to be picked up as part of
this pack: `γ⁵` and chiral projectors; Levi-Civita contraction; Fierz
rearrangement; spinors and spin/polarization sums; squared amplitudes; loop
integrals; diagram generation; covariant derivatives, field strengths and
Bianchi identities.
