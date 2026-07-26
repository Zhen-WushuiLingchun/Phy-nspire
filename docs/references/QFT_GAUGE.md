# Relativistic QFT and gauge theory — MVP source reference

Date: 2026-07-26

This is the source-backed reference for the Phase 5 QFT/gauge slice
(`docs/ROADMAP.md`). It fixes the conventions, names the upstream projects and
the exact files that implement each capability, records the identities the
native engine must reproduce, and states which of those identities have been
machine-checked in this repository.

It is deliberately an **MVP** reference. Section 2 lists what is in scope and
what is deferred; a deferred item is not a gap in this document, it is a
decision. Nothing outside section 2's "in scope" column should be implemented
against this reference.

Every upstream fact below was read from the project's own repository or from a
metadata API in this environment on the date above, and the evidence is cited
inline. Claims that could not be verified are collected in
[§11](#11-unverified-claims). Do not promote an UNVERIFIED claim to a design
assumption without executing the verification step named beside it.

---

## 1. Verification environment

| Property | Value |
| --- | --- |
| Base commit | `7e24783`, "Typed expression IR core (Phase 1) (#4)" — the tip of `main` at the time of writing |
| Host | Windows 11 Pro 10.0.26300; build and test executed under WSL |
| Build/test toolchain | cmake 3.22.1, gcc 11.4.0 (Ubuntu 11.4.0-1ubuntu1~22.04.3) |
| Upstream access | GitHub REST API and `gh` (authenticated), arXiv API, INSPIRE-HEP API |
| Upstream source obtained | Metadata and individual files via API. **No upstream repository was cloned and no upstream code was copied into this repository.** |

The oracle in `tests/oracle/` was compiled and run in this environment. Its
result at the time of writing:

```
test_qo_golden: 44295 checks, 0 failures
```

That figure is what makes the identity tables in [§7](#7-golden-identities)
evidence rather than transcription.

---

## 2. MVP scope

The Phase 5 roadmap entry lists more than one release can carry. This is the
cut.

| Capability | Status | Rationale |
| --- | --- | --- |
| Clifford algebra, `{γ^μ, γ^ν} = 2g^{μν}` | **In scope** | The axiom everything else reduces to. |
| Lorentz index contraction of gamma strings | **In scope** | Required before any trace is affordable; see [§8](#8-resource-behaviour). |
| Dirac traces of gamma strings, no `γ⁵` | **In scope** | The core deliverable. Closed-form, well-conditioned, testable. |
| Scalar products, momentum conservation, on-shell substitution | **In scope** | Needed for traces to collapse to invariants. |
| Mandelstam variables and the `s + t + u` sum rule | **In scope** | Small, high value, and a convention trap worth pinning early ([§4.4](#44-mandelstam-routing)). |
| SU(N) colour: generators, `f^{abc}`, `Tr[T^aT^b]`, `C_F`, `C_A` | **In scope** | Self-contained, no Dirac coupling, independently testable. |
| `γ⁵`, chiral projectors, Levi-Civita contraction | **Deferred** | Scheme-dependent in `D ≠ 4` (BMHV vs Larin vs naive). Requires a dimension-tracking IR that Phase 5 MVP does not have. See [§4.5](#45-why-gamma-5-is-deferred). |
| Fierz / SU(N) completeness rearrangement | **Deferred** | Needs the `1/N` colour-singlet bookkeeping and a canonical spinor-chain ordering; neither exists yet. |
| Spinors, spin and polarization sums | **Deferred** | Depends on external-state objects, which depend on a particle/model layer. |
| Squared amplitudes, tree-level processes | **Deferred** | Depends on all of the above. |
| Loop integrals, tensor reduction | **Deferred** | Deferred from this MVP pending a later feasibility and budget design based on measured device data. Not foreclosed: small analytic cases may well be tractable, but nothing here has been measured, so the question stays open rather than being decided by assumption. See [§8](#8-resource-behaviour). |
| Diagram generation / topology enumeration | **Deferred** | Phase 6, and a separate problem. |
| Full Yang-Mills algebra: covariant derivatives, field strength, Bianchi, gauge transformations | **Deferred** | Requires the tensor/manifold core from Phase 2. The colour algebra in scope here is the flat, purely algebraic part that does **not** need it. |

The gauge-theory content of this MVP is therefore the **SU(N) algebra only**.
`docs/SCIENTIFIC_SCOPE.md` §7 (connections, curvature forms, Bianchi
identities, Lagrangian templates) is Phase 2-dependent and stays deferred.

---

## 3. Upstream register

Four projects solve this problem in production. Each is consulted as a
*specification and cross-check*, never as a code donor — see
[§10](#10-licence-obligations).

### 3.1 FeynCalc — closest match to the target vocabulary

| Field | Value |
| --- | --- |
| Repository | <https://github.com/FeynCalc/feyncalc> (`full_name` `FeynCalc/feyncalc`) |
| Description | "Mathematica package for algebraic calculations in elementary particle physics." |
| Licence | **GPL-3.0** |
| Default branch | `master`; homepage <https://feyncalc.github.io> |
| Latest release | tag `Release-10_2_1`, name "FeynCalc 10.2.1", published 2026-06-11T14:05:52Z |
| Last push observed | 2026-07-09T10:56:09Z |
| Copyright header | "Copyright (C) 1990-2026 Rolf Mertig / 1997-2026 Frederik Orellana / 2014-2026 Vladyslav Shtabovenko", read from `FeynCalc/Dirac/DiracTrace.m` |

Files that implement the MVP capabilities, with sizes read from the GitHub
contents API:

| Capability | File | Bytes |
| --- | --- | ---: |
| Trace driver and options | `FeynCalc/Dirac/DiracTrace.m` | 32,216 |
| Gamma-chain rewriting engine | `FeynCalc/Dirac/DiracTrick.m` | 109,405 |
| User-facing simplifier | `FeynCalc/Dirac/DiracSimplify.m` | 23,936 |
| Canonical ordering of gammas | `FeynCalc/Dirac/DiracOrder.m` | 11,253 |
| Chisholm identity | `FeynCalc/Dirac/Chisholm.m` | 11,843 |
| Lorentz contraction | `FeynCalc/Lorentz/Contract.m` | 28,789 |
| Pairwise contraction core | `FeynCalc/Lorentz/PairContract.m` | 14,539 |
| Scalar-product expansion | `FeynCalc/Lorentz/ExpandScalarProduct.m` | 3,363 |
| Mandelstam definition | `FeynCalc/Lorentz/SetMandelstam.m` | 9,405 |
| Mandelstam elimination | `FeynCalc/Lorentz/TrickMandelstam.m` | 5,101 |
| Dummy-index canonicalization | `FeynCalc/Lorentz/FCCanonicalizeDummyIndices.m` | 14,830 |
| Colour trace | `FeynCalc/SUN/SUNTrace.m` | 8,822 |
| Colour simplifier | `FeynCalc/SUN/SUNSimplify.m` | 37,731 |

Deferred-capability files, listed so a future agent can find them without
re-deriving the map: `Anti5.m` (14,931), `ToLarin.m` (1,671),
`EpsChisholm.m` (5,651), `SUNFierz.m` (3,768).

`Options[DiracTrace]`, read verbatim from `DiracTrace.m`:

```
Contract -> True,     DiracTraceEvaluate -> False,  EpsContract -> False,
EpsExpand -> True,    EpsEvaluate -> True,          Expand -> True,
Factoring -> Automatic, FCDiracIsolate -> True,     LarinMVV -> True,
Mandelstam -> {},     PairCollect -> False,         Sort -> True,
TraceOfOne -> 4,      West -> True
```

Two things follow directly. `TraceOfOne -> 4` is an *option*, not a constant —
`Tr[1]` is configurable, and our engine should expose the same knob rather than
hard-coding 4. And `Mandelstam -> {}` on the trace function itself shows that
Mandelstam substitution is applied *inside* the trace routine, not as a
post-pass; that is a design signal for [§6](#6-algorithm-register).

`Options[SUNSimplify]` includes `SUNNToCACF -> True`, `SUNTraceEvaluate ->
Automatic`, `SUNFJacobi -> False`, `TimeConstrained -> 3`. `SUNTrace::usage`
states that `Automatic` "implies evaluation of traces with 2 or 3 color
matrices" — i.e. even the reference implementation declines to evaluate longer
colour traces by default.

### 3.2 FORM — the performance and algorithm reference

| Field | Value |
| --- | --- |
| Repository | <https://github.com/form-dev/form> |
| **Redirect note** | The widely-cited `github.com/vermaseren/form` now resolves to `form-dev/form`. Pin the new path. |
| Description | "The FORM project for symbolic manipulation of very big expressions" |
| Licence | **GPL-3.0**; default branch `master` |
| Latest release | `v5.0.1`, published 2026-06-24T20:41:33Z |
| Last push observed | 2026-07-22T08:43:55Z |
| Gamma algebra implementation | `sources/opera.c`; compiler side in `sources/compcomm.c`, `sources/compiler.c`; declarations in `sources/declare.h`, `sources/ftypes.h` |
| Documentation | `doc/manual/gamma.tex` (chapter "Dirac algebra"), `doc/manual/statements.tex`, `doc/devref/indepth.tex` |
| Regression material | `check/examples.frm`, `check/formunit/fu.frm`, `check/extra/color.frm` |

FORM's surface vocabulary, quoted from `doc/manual/gamma.tex`:

- `g_(j,mu)` — gamma matrix on spin line `j`; `gi_(j)` — unit matrix;
  `d_(mu,nu)` — metric; `e_(...)` — Levi-Civita.
- `g_(j,mu,nu,...)` is shorthand for the product `g_(j,mu)*g_(j,nu)*...`.
- `trace4,j;` — trace in exactly 4 dimensions, using 4-dimensional tricks.
- `tracen,j;` — trace in an unspecified even number of dimensions, "evaluated
  by only using the anticommutation properties"; a `g5_` present is "a fatal
  error".
- `unittrace value;` — changes `Tr[1]` from its default of 4. Value must be a
  positive short number or a single symbol other than `i_`.

The defining relations, verbatim:

```
{g_(j1,mu),g_(j1,nu)} = 2 * d_(mu,nu)
[g_(j1,mu),g_(j2,nu)] = 0    j1 not equal to j2.
```

The second relation is the part a naive implementation forgets: gamma matrices
on **different spin lines commute**. Any engine that models a gamma chain as a
single flat noncommutative product cannot express a two-fermion-line amplitude
correctly.

### 3.3 SymPy — the only permissively licensed implementation

| Field | Value |
| --- | --- |
| Repository | <https://github.com/sympy/sympy> |
| Licence | GitHub reports `NOASSERTION`; the `LICENSE` file is a **3-clause BSD** licence, "Copyright (c) 2006-2023 SymPy Development Team". Note `sympy/parsing/latex` is MIT, and other subtrees carry their own BSD grants. |
| Module | `sympy/physics/hep/gamma_matrices.py`, 24,288 bytes |

Public API, read in definition order from the module:

```python
LorentzIndex = TensorIndexType('LorentzIndex', dim=4, dummy_name="L")
GammaMatrix  = TensorHead("GammaMatrix", [LorentzIndex],
                          TensorSymmetry.no_symmetry(1), comm=None)
```

then `extract_type_tens`, `simplify_gamma_expression`, `simplify_gpgp`,
`gamma_trace`, `kahane_simplify` (with `_simplify_single_line`,
`_trace_single_line`, `_gamma_trace1` private).

Three observations that matter to us:

1. `dim=4` is baked into `LorentzIndex`. SymPy's HEP gamma module is
   **strictly four-dimensional**; it has no `D`-dimensional mode at all. This
   is precisely the MVP boundary we chose independently in
   [§2](#2-mvp-scope), which is corroborating evidence that the cut is a
   natural one.
2. The trace of the identity is the bare literal `gctr = 4` inside
   `_gamma_trace1`. It is not documented in any docstring and not
   configurable — the opposite of FeynCalc's `TraceOfOne` option.
3. `kahane_simplify(G(i0)*G(-i0))` returns a 4×4 SymPy `Matrix`, not the
   scalar `4`. The return type depends on whether any free index survives.
   That is an API wart to avoid reproducing, not to imitate.

**This is the only one of the four upstreams whose licence would permit
deriving an implementation without imposing GPL terms on Phy-nspire.** See
[§10](#10-licence-obligations).

### 3.4 Cadabra — the structurally different approach

| Field | Value |
| --- | --- |
| Repository | <https://github.com/kpeeters/cadabra2> |
| Description | "A field-theory motivated approach to computer algebra." |
| Licence | **GPL-3.0**; homepage <https://cadabra.science/> |
| Last push observed | 2026-03-22T22:00:14Z |
| Relevant algorithms | `core/algorithms/join_gamma.{cc,hh}`, `split_gamma.*`, `expand_diracbar.*`, `sort_spinors.*`, `epsilon_to_delta.*` |
| Relevant properties | `core/properties/GammaMatrix.{cc,hh}`, `GammaTraceless.*`, `Spinor.*`, `DiracBar.*` |

The `join_gamma` interface, read from `join_gamma.hh`:

```cpp
join_gamma(const Kernel&, Ex&, bool expand, bool use_gendelta);
...
bool expand;  std::vector<int> only_expand;
const GammaMatrix *gm1, *gm2;
private: bool use_generalised_delta_;
```

Cadabra has **no dedicated Dirac trace algorithm** in its algorithm set. It
works by joining and splitting products into the antisymmetrised
(generalised) gamma basis, optionally through generalised Kronecker deltas.
Traces then fall out of the basis decomposition. This is a genuinely different
strategy from FeynCalc's and FORM's, and it is the one that generalises
cleanly to arbitrary dimension and to supergravity-style index gymnastics —
at the cost of being far less direct for the 4-dimensional trace that the MVP
actually needs.

---

## 4. Pinned conventions

A convention that is merely "understood" is a convention that will be
violated. These are binding for all Phase 5 work.

### 4.1 Metric signature

**Pinned: `g^{μν} = diag(+1, −1, −1, −1)`.**

This is the particle-physics ("mostly-minus", Bjorken–Drell) signature used by
FeynCalc and by Peskin & Schroeder. In this signature `g^{μν}` and `g_{μν}`
are numerically equal, which is why the oracle exposes a single accessor
(`qo_metric`, `tests/oracle/qo_dirac.c`).

> **Cross-phase hazard.** The general-relativity slice (Phases 3–4,
> `docs/SCIENTIFIC_SCOPE.md` §4) conventionally uses the **opposite**
> signature `(−, +, +, +)` (MTW, Wald). Phy-nspire will therefore host both
> conventions in one notebook. Two consequences: the signature must be a
> property carried by the typed IR's index/metric objects, **not** a global
> constant; and any golden value shared between the GR and QFT corpora must
> record which signature it was computed in. This is an integration risk that
> belongs to the Phase 2 tensor core, and it should be raised there before the
> metric object is designed.

### 4.2 Trace of the identity

**Pinned: `Tr[1] = 4`, but as a configurable parameter, not a literal.**

FORM exposes `unittrace`; FeynCalc exposes `TraceOfOne -> 4`. SymPy hard-codes
`gctr = 4`. Two of the three mature implementations made it configurable;
follow them. The parameter matters for dimensional regularisation and for
non-4-dimensional Clifford algebras, both deferred but both foreseeable.

### 4.3 Gamma matrix representation

**Pinned: no representation is fixed, and no result may depend on one.**

Every MVP quantity — Clifford algebra, contraction identities, traces — is
representation-independent. The oracle enforces this by computing every result
in **both** the Dirac and the Weyl representation and requiring agreement
(`kBases` in `tests/oracle/test_qo_golden.c`). A symbolic engine that never
instantiates matrices cannot accidentally depend on a representation, but the
oracle that validates it can, so the oracle checks itself.

### 4.4 Mandelstam routing

This is the sharpest convention trap in the MVP, because two mutually
inconsistent-looking definitions are both standard and both correct.

`SetMandelstam::usage`, verbatim from FeynCalc:

> `SetMandelstam[s, t, u, p1, p2, p3, p4, m1, m2, m3, m4]` defines the
> Mandelstam variables `s=(p_1+p_2)^2`, `t=(p_1+p_3)^2`, `u=(p_1+p_4)^2` and
> sets the momenta on-shell: `p_1^2=m_1^2`, … Notice that
> `p_1+p_2+p_3+p_4=0` is assumed.

FeynCalc routes **all four momenta as incoming**, so `t` and `u` carry a plus
sign. Peskin routes `p₁ + p₂ → p₃ + p₄` with `p₃`, `p₄` outgoing, giving
`t = (p₁ − p₃)²` and `u = (p₁ − p₄)²`. The formulas differ in sign; the
invariants are identical once the momenta are converted. Both routings, and
their agreement, are checked in `test_mandelstam_sum_rule` using an exactly
representable configuration:

| Quantity | Value |
| --- | ---: |
| `p₁ = (5, 0, 0, 5)`, `p₂ = (5, 0, 0, −5)` | massless |
| `p₃ = (5, 3.2, 0, 2.4)`, `p₄ = (5, −3.2, 0, −2.4)` | `m² = 9` |
| `s` | 100 |
| `t` | −17 |
| `u` | −65 |
| `s + t + u` | 18 `= 0 + 0 + 9 + 9` |

**Binding rule:** every stored Mandelstam golden value must record its routing.
A bare `t = (p1+p3)^2` in a corpus entry is not self-describing and must be
rejected in review.

`TrickMandelstam::usage` documents the elimination step: it rewrites sums so
that one of `s`, `t`, `u` is removed using `s + t + u = m₁² + m₂² + m₃² + m₄²`,
choosing the substitution that yields "the most short number of terms". The
MVP should implement the sum rule; the shortest-result search is an
optimisation and may be deferred.

### 4.5 Why gamma-5 is deferred

`γ⁵` is not deferred because it is hard to define in 4 dimensions — it is
trivial there. It is deferred because every mature implementation carries an
entire *scheme* apparatus around it, and a scheme choice cannot be retrofitted.
Evidence from `DiracTrace.m`'s own error strings:

> Expressions that mix D-, 4- and D-4-dimensional quantities are forbidden in
> Dirac matrix chains unless you are using the t'Hooft-Veltman scheme. […] If
> you explicitly intend to use the t'Hooft-Veltman scheme, please activate it
> via `FCSetDiracGammaScheme["BMHV"]`.

and

> Detected string of Dirac matrices containing more than two g^5 matrices in
> the Larin scheme. Since the pairing of the resulting Eps-tensors depends on
> their physical origin, FeynCalc will disable all such contractions […] The
> correct order of contractions cannot be determined automatically and must be
> specified by the user.

FORM reaches the same conclusion from the other direction: `tracen` treats a
`g5_` as a fatal error, and the manual warns that mixing 4-dimensional and
`n`-dimensional indices in one chain gives an "unpredictable" result.

The MVP has no dimension-tracking in its IR. Adding `γ⁵` without it would
produce results that are silently wrong in exactly the cases users care about.

---

## 5. Colour conventions

**Pinned:**

| Quantity | Value | Note |
| --- | --- | --- |
| Normalisation | `Tr[T^a T^b] = T_F δ^{ab}`, `T_F = 1/2` | The universal QCD convention. |
| Commutator | `[T^a, T^b] = i f^{abc} T^c` | Fixes the sign and reality of `f`. |
| Structure constants | `f^{abc} = −2i Tr([T^a,T^b] T^c)` | Follows from the two above; used directly by the oracle. |
| Fundamental Casimir | `C_F = (N²−1)/(2N)` | `= 4/3` at `N = 3`. |
| Adjoint Casimir | `C_A = N` | via `f^{acd} f^{bcd} = C_A δ^{ab}`. |
| Generator ordering | generalised Gell-Mann | See below. |

The oracle builds fundamental generators in **generalised Gell-Mann order**: for
each column `j = 1..N−1`, the symmetric and antisymmetric off-diagonal pairs
`(i, j)` for `i < j`, then the `j`-th diagonal generator
`diag(1,…,1,−j,0,…)/√(2j(j+1))`. This ordering has a property worth preserving:
at `N = 2` it reproduces the Pauli matrices over two exactly, and at `N = 3` it
reproduces the Gell-Mann matrices over two in the standard `λ₁…λ₈` order. That
makes published structure constants directly comparable rather than comparable
only up to a basis rotation — `test_su_matches_textbook_bases` asserts
`f^{123} = 1`, `f^{147} = 1/2`, `f^{458} = √3/2` against the literature values.

FeynCalc's `SUNNToCACF -> True` default converts `N` into `C_A`/`C_F` in
output. That is a presentation choice we should copy: `C_F` and `C_A` are what
a physicist reads, `N` is what the engine computes with.

---

## 6. Algorithm register

The four upstreams use three distinct strategies. The MVP should implement the
FORM/Kahane family, for the reasons given below.

### 6.1 Contraction identities (`D = 4`)

The identities that remove contracted index pairs, quoted verbatim from
SymPy's `kahane_simplify` docstring:

> for contractions enclosing an even number of `γ` matrices
>
> `γ^μ γ_{a_1} ⋯ γ_{a_{2N}} γ_μ = 2 (γ_{a_{2N}} γ_{a_1} ⋯ γ_{a_{2N−1}} + γ_{a_{2N−1}} ⋯ γ_{a_1} γ_{a_{2N}})`
>
> for an odd number of `γ` matrices
>
> `γ^μ γ_{a_1} ⋯ γ_{a_{2N+1}} γ_μ = −2 γ_{a_{2N+1}} γ_{a_{2N}} ⋯ γ_{a_1}`

FORM's `doc/manual/gamma.tex` states the same rules in its own notation
(rules 2 and 3, quoted in §6.2). Two independent implementations agreeing on
the identities is the cross-check; the low-`N` cases are then machine-verified
in [§7](#7-golden-identities).

Note the **order reversal** in both identities. This is the single most likely
place for an implementation to be subtly wrong, and it is exactly where SymPy
shipped a real defect — see [§9](#9-known-defects-in-the-upstreams).

### 6.2 FORM's trace rule set

Quoted and condensed from `doc/manual/gamma.tex`. This is the most complete
public statement of a production trace algorithm, and the MVP specification
should be written against it.

| Rule | Content | MVP |
| --- | --- | --- |
| 0 | Odd-length strings have zero trace (`γ⁵` counts as even). | Implement |
| 1 | Adjacent identical index or vector collapses: `g_(1,mu,mu) = gi_(1)*d_(mu,mu)`, `g_(1,p1,p1) = gi_(1)*p1.p1`. | Implement |
| 2 | Contracted pair with an **odd** number of matrices between: `g_(1,mu,m1,…,mn,mu) = -2*g_(1,mn,…,m2,m1)`. 4-dimensional only. | Implement |
| 3 | Contracted pair with an **even** number between: `g_(1,mu,m1,m2,mu) = 4*gi_(1)*d_(m1,m2)`, and in general `2*g_(1,mn,m1,…,mj) + 2*g_(1,mj,…,m1,mn)`. 4-dimensional only. | Implement |
| 4 | Anticommute a repeated index/vector into adjacency, then apply rule 1. Works in any dimension. | Implement |
| 5 | All indices distinct: reduce via the basis formula. | **Substitute** — see below |

FORM's rule 5 reads:

```
g_(1,mu,nu,ro) = g_(1,5_,si)*e_(mu,nu,ro,si)
               + d_(mu,nu)*g_(1,ro) - d_(mu,ro)*g_(1,nu) + d_(nu,ro)*g_(1,mu)
```

It introduces `g5_` and the Levi-Civita tensor, both deferred. **The MVP must
therefore not use FORM's rule 5.** It should instead close the recursion with
the standard metric expansion

```
Tr[γ^{m₁} ⋯ γ^{m_{2n}}] = Σ_{j=2}^{2n} (−1)^j g^{m₁ m_j} Tr[γ^{m₂} ⋯ ĝ^{m_j} ⋯ γ^{m_{2n}}]
```

which needs no `γ⁵`, terminates at `Tr[1] = 4`, and is fully verified against
explicit matrices in `test_trace_recursion_matches_matrices`. This substitution
is the one place where the MVP deliberately diverges from FORM, and it is a
direct consequence of the §2 scope cut.

FORM's own caveats, which apply to us as soon as `γ⁵` is un-deferred:

- Rule 5's two algorithms differ only when at least **10** gamma matrices
  remain after rules 1–4, with `γ⁵` counting as 4. Below that threshold the
  distinction is invisible — so a test suite that never exceeds 10 matrices
  cannot detect getting this wrong.
- "The result is unpredictable, when both indices in four dimensions and
  indices in n dimensions occur in the same string."

### 6.3 The Chisholm identity

FORM states it as

```
γ_μ Tr[γ_μ S] = 2 (S + S^R)
```

for `S` a string with an odd number of matrices (`γ⁵` counting as even), `S^R`
the reversed string. It is used to *combine* traces sharing an index, and it is
on by default for `trace4` (disable with `trace4,nocontract,j;`). FeynCalc
exposes the same thing as `Chisholm[exp]`, which "substitutes products of three
Dirac matrices or slashes by the Chisholm identity".

**MVP position: not required.** It is an optimisation for multi-trace
expressions, and the MVP has no amplitude layer to produce them. Recorded here
so it is not rediscovered from scratch.

### 6.4 Choosing between the strategies

| Strategy | Upstream | Verdict for MVP |
| --- | --- | --- |
| Rule-driven rewriting on flat gamma strings | FORM, FeynCalc | **Adopt.** Direct, well-documented, matches the 4-dimensional MVP exactly. |
| Kahane link-graph contraction | SymPy | **Adopt for contraction.** Same identities, but avoids repeated rewriting by recognising the link structure once. Also the only BSD-licensed prior art. |
| Antisymmetrised basis decomposition | Cadabra | **Reject for MVP.** More general than needed, and pays for generality in dimension-agnostic machinery the MVP does not have. |

SymPy's docstring describes the Kahane payoff precisely:

> Instead of repeatedly applying these identities to cancel out all contracted
> indices, it is possible to recognize the links that would result from such an
> operation, the problem is thus reduced to a simple rearrangement of free
> gamma matrices.

---

## 7. Golden identities

Every identity below is checked in `tests/oracle/test_qo_golden.c` against
explicit 4×4 matrices, in **both** the Dirac and Weyl representations, at
tolerance `1e-9`. The suite reports **44,295 checks, 0 failures**. The named
test is the evidence for each row; an identity added to this table without a
corresponding check is not verified and must be marked so.

### 7.1 Dirac algebra

| ID | Identity | Test |
| --- | --- | --- |
| G-1 | `{γ^μ, γ^ν} = 2 g^{μν} 𝟙` | `test_clifford_algebra` |
| G-2 | `γ^μ γ_μ = 4·𝟙` | `test_contraction_identities` |
| G-3 | `γ^μ γ^ν γ_μ = −2 γ^ν` | `test_contraction_identities` |
| G-4 | `γ^μ γ^ν γ^ρ γ_μ = 4 g^{νρ} 𝟙` | `test_contraction_identities` |
| G-5 | `γ^μ γ^ν γ^ρ γ^σ γ_μ = −2 γ^σ γ^ρ γ^ν` | `test_contraction_identities` |
| G-6 | Odd-length gamma strings are traceless | `test_odd_traces_vanish` |
| G-7 | The metric recursion equals the explicit matrix trace, exhaustively for `n = 2, 4, 6` and on a fixed stride for `n = 8` | `test_trace_recursion_matches_matrices` |
| G-8 | `Tr[γ^μ γ^ν] = 4 g^{μν}` | `test_trace_two_gammas` |
| G-9 | `Tr[γ^μ γ^ν γ^ρ γ^σ] = 4(g^{μν}g^{ρσ} − g^{μρ}g^{νσ} + g^{μσ}g^{νρ})` | `test_trace_four_gammas` |
| G-10 | `p̸ q̸ + q̸ p̸ = 2 (p·q) 𝟙` | `test_slash_anticommutator` |
| G-11 | `Tr[p̸₁p̸₂p̸₃p̸₄] = 4[(p₁·p₂)(p₃·p₄) − (p₁·p₃)(p₂·p₄) + (p₁·p₄)(p₂·p₃)]` | `test_trace_four_slashes` |
| G-12 | `s + t + u = Σmᵢ²`, in both the Peskin and FeynCalc routings | `test_mandelstam_sum_rule` |

G-7 deserves emphasis: it validates the **algorithm**, not a table of answers.
For every assignment of Lorentz indices the recursion of §6.2 is run and
compared against a matrix product, so a sign error in any branch of the
recursion fails the suite regardless of whether a textbook case happens to
cover it.

G-5 is the order-reversal case. `test_contraction_preserves_leading_order`
additionally pins the ordering trap from [§9](#9-known-defects-in-the-upstreams).

### 7.2 SU(N) colour

Checked for `N = 2..6` where the cost allows, `N = 2..4` for the quartic
contractions, `N = 3` for Jacobi.

| ID | Identity | Test |
| --- | --- | --- |
| C-1 | Generators are hermitian and traceless | `test_su_generators_hermitian_traceless` |
| C-2 | `Tr[T^a T^b] = δ^{ab}/2` | `test_su_normalisation` |
| C-3 | `[T^a, T^b] = i f^{abc} T^c`; `f` real and totally antisymmetric | `test_su_structure_constants` |
| C-4 | `T^a T^a = C_F 𝟙`, `C_F = (N²−1)/(2N)`, `C_F(3) = 4/3` | `test_su_casimir_fundamental` |
| C-5 | `f^{acd} f^{bcd} = C_A δ^{ab}`, `C_A = N` | `test_su_casimir_adjoint` |
| C-6 | Jacobi: `f^{abe}f^{ecd} + f^{bce}f^{ead} + f^{cae}f^{ebd} = 0` | `test_su_jacobi` |
| C-7 | `N = 2` gives Pauli/2 (`f = ε`); `N = 3` gives `f^{123}=1`, `f^{147}=1/2`, `f^{458}=√3/2` | `test_su_matches_textbook_bases` |

---

## 8. Resource behaviour

The device budget is the binding constraint: the application target is
nominally 5–6 MB against a 6 MB ceiling, and `README.md` records the Phase 0
`.tns` at 12,676 bytes. Dirac traces are the classic place where a symbolic
engine detonates, so the growth law must be in the design, not discovered at
runtime.

**Combinatorics.** A trace of `2n` gamma matrices with all indices distinct
expands into `(2n−1)!!` metric terms before any simplification:

| Gamma matrices | Terms `(2n−1)!!` |
| ---: | ---: |
| 2 | 1 |
| 4 | 3 |
| 6 | 15 |
| 8 | 105 |
| 10 | 945 |
| 12 | 10,395 |
| 14 | 135,135 |

**Measured upstream data.** `doc/manual/gamma.tex` publishes timings for a
`γ⁵` plus 12 regular matrices trace, run as part of FORM's own test suite:

| Method | Generated terms | Output terms | Time | Bytes |
| --- | ---: | ---: | ---: | ---: |
| `tracen` (anticommutation only) | 51,975 | 51,975 | 1.07 s | 919,164 |
| `trace4` (4-dimensional tricks) | 1,053 | 1,029 | 0.02 s | 20,284 |

Two conclusions for Phy-nspire:

1. **The 4-dimensional rules are worth roughly a factor of 50 in both terms and
   time.** On a device with ~30 MB free RAM, the naive route is not merely
   slower, it is the difference between a result and an out-of-memory error.
   This is the quantitative justification for adopting rules 1–4 rather than
   pure anticommutation.
2. Even FORM's *good* path generates ~1,000 terms and ~20 KB for a
   13-matrix trace on a workstation. A 320×240 calculator should therefore
   impose a term ceiling well below that and surface it as a typed
   resource-limit error, per `docs/ARCHITECTURE.md` "Error handling".

**Proposed MVP limits**, to be confirmed by measurement on device:

| Limit | Proposed value | Basis |
| --- | ---: | --- |
| Gamma matrices per chain | 12 | `(2n−1)!! = 10,395` is already at the edge |
| Generated trace terms | 4,096 | Below FORM's 13-matrix figure, above every MVP golden case |
| Colour trace length | 3 | Matches FeynCalc's `SUNTraceEvaluate -> Automatic` |

The colour ceiling is not a guess: FeynCalc's own default declines to evaluate
colour traces longer than 3 matrices.

These are proposals. They are **UNVERIFIED** on hardware; see
[§11](#11-unverified-claims).

---

## 9. Known defects in the upstreams

Recorded because they are free regression tests, and because they show where
this class of code actually breaks.

**SymPy issue #23823** — `physics.hep.kahane_simplify() incorrectly reverses
order of leading uncontracted gamma matrices`. Opened 2022-07-23, closed
2022-07-30, label `physics`. The reporter's case:

```python
t = G(mu)*G(-mu)*G(rho)*G(sigma)   # -> 4*GammaMatrix(rho)*GammaMatrix(sigma)  correct
t = G(rho)*G(sigma)*G(mu)*G(-mu)   # -> 4*GammaMatrix(sigma)*GammaMatrix(rho)  WRONG
```

Root cause, in the reporter's words: "the leading matrices are removed at the
beginning of the function and then inserted at the start of the product at the
end of the function, and the insertion loop is just backward."

This is the highest-value regression case in the whole MVP. Both orderings must
give `4 γ^ρ γ^σ` with the free matrices in their original order. It is pinned
in `test_contraction_preserves_leading_order`, and it is a **mandatory**
acceptance test for the Phase 5 engine — a mature, widely-used package shipped
this bug, so an assertion that merely checks the scalar factor `4` is not
sufficient.

**FORM's `(g_(1,p)+m)` trap** — the manual warns that writing a projector as
`(g_(1,p)+m)` instead of `(g_(1,p)+m*gi_(1))` is "technically not correct",
because a term may end up traced with no gamma matrix present at all. It notes
the sloppy form "will almost always give the correct result. Almost always…".
The lesson for our IR: **the identity element must be explicit**, never implied
by the absence of a factor. A trace node whose argument is a bare scalar must
be well-defined.

---

## 10. Licence obligations

This section is engineering guidance for scoping work, **not legal advice**.
Phy-nspire's own licence is not yet settled — `README.md` records that "the
final project license and retained notices will be fixed before any upstream
code is copied or linked". Any actual incorporation of upstream code needs
project licensing and legal review before it happens, not a judgement call by
an implementing agent.

| Upstream | Licence | Use relied on in this document |
| --- | --- | --- |
| FeynCalc | GPL-3.0 | Read as specification; behaviour, option names and documented identities cited. |
| FORM | GPL-3.0 | Same, plus paraphrase and citation of the manual's algorithm descriptions. |
| Cadabra | GPL-3.0 | Same. |
| SymPy | 3-clause BSD | Same. Its permissive terms make it the least encumbered starting point *if* code is ever reused. |

**No upstream code has been copied into this repository.** Everything in
`tests/oracle/` was written for this project from the identities themselves.
Nothing in this document depends on copying any upstream implementation.

Two points a future agent should keep straight:

1. **Reading a specification is not the same as incorporating an
   implementation.** A mathematical identity such as `Tr[γ^μγ^ν] = 4g^{μν}` is
   a fact, and recording it — or an option name, or a documented algorithm
   rule — as this document does, is ordinary referencing. Lifting
   `DiracTrick.m`'s rewrite rules into C is a different act: that is
   incorporating someone else's implementation, and distributing the result
   would generally require complying with that project's licence terms.
   Whether and how those terms can be satisfied here is a question for
   licensing review, which has not happened.
2. **The MVP is designed so this question never has to be answered.** Every
   contract in the task pack is written to be implemented from the identities
   and the algorithm description, not from upstream source. If an agent finds
   itself wanting to port code rather than write it, that is the moment to
   stop and escalate — and SymPy's BSD terms are the least encumbered place to
   start that conversation, not a licence to skip it.

---

## 11. UNVERIFIED claims

| Claim | How to settle it |
| --- | --- |
| The proposed resource limits in §8 are appropriate for the CX II | Build a device binary that runs a 12-matrix trace and measure peak RSS and wall time. No hardware has been used in this work. |
| FORM's published timings are representative of an ARM device | They are workstation figures quoted from the manual, given without a CPU model. Treat the ~50× ratio between `tracen` and `trace4` as meaningful and the absolute times as not. |
| Kahane's link-graph method outperforms naive rewriting *at MVP sizes* | Asserted by SymPy's docstring and by Kahane (1968); not measured here. At ≤12 matrices the constant factors may dominate. Measure before optimising. |
| `sympy/physics/hep` has no `D`-dimensional path | Inferred from `LorentzIndex` being constructed with `dim=4` and from the absence of any other index type in the module. Not confirmed by exhausting the tests directory. |
| Golz (arXiv:1710.05164) is published in Ann. Inst. H. Poincaré D | A search result pointed at `10.4171/aihpd/89`; the arXiv metadata carries no journal reference. Cite the arXiv ID until confirmed. |

---

## 12. Primary references

Verified against the arXiv API, the INSPIRE-HEP API, or the publisher, as noted.

**Algorithms**

- J. Kahane, "Algorithm for Reducing Contracted Products of γ Matrices",
  *J. Math. Phys.* **9**(10), 1732–1738 (1968).
  DOI [10.1063/1.1664506](https://doi.org/10.1063/1.1664506).
- J. S. R. Chisholm, "Relativistic Scalar Products of Gamma Matrices",
  *Nuovo Cimento* **30**, 426 (1963).
- M. Golz, "Contraction of Dirac matrices via chord diagrams",
  [arXiv:1710.05164](https://arxiv.org/abs/1710.05164) — "A simple formula for
  the result of arbitrary contractions is derived, simplifying and extending an
  old contraction algorithm due to Kahane."

**Colour and group theory**

- P. Cvitanović, "Group theory for Feynman diagrams in non-Abelian gauge
  theories", *Phys. Rev. D* **14**, 1536 (1976).
  DOI [10.1103/PhysRevD.14.1536](https://doi.org/10.1103/PhysRevD.14.1536).
- T. van Ritbergen, A. N. Schellekens, J. A. M. Vermaseren, "Group theory
  factors for Feynman diagrams", *Int. J. Mod. Phys.* **A14**, 41–96 (1999).
  [arXiv:hep-ph/9802376](https://arxiv.org/abs/hep-ph/9802376),
  DOI [10.1142/S0217751X99000038](https://doi.org/10.1142/S0217751X99000038).

**Tools**

- R. Mertig, M. Böhm, A. Denner, "FEYN CALC: Computer algebraic calculation of
  Feynman amplitudes", *Comput. Phys. Commun.* **64**, 345–359 (1991).
  DOI [10.1016/0010-4655(91)90130-D](https://doi.org/10.1016/0010-4655%2891%2990130-D).
- V. Shtabovenko, R. Mertig, F. Orellana, "New Developments in FeynCalc 9.0",
  *Comput. Phys. Commun.* **207**, 432–444 (2016).
  [arXiv:1601.01167](https://arxiv.org/abs/1601.01167),
  DOI [10.1016/j.cpc.2016.06.008](https://doi.org/10.1016/j.cpc.2016.06.008).
  *(The arXiv record's `journal_ref` field for this entry incorrectly carries
  the 1991 paper's citation; the volume above is from INSPIRE-HEP.)*
- V. Shtabovenko, R. Mertig, F. Orellana, "FeynCalc 9.3: New features and
  improvements", *Comput. Phys. Commun.* **256**, 107478 (2020).
  [arXiv:2001.04407](https://arxiv.org/abs/2001.04407),
  DOI [10.1016/j.cpc.2020.107478](https://doi.org/10.1016/j.cpc.2020.107478).
- V. Shtabovenko, R. Mertig, F. Orellana, "FeynCalc 10: Do multiloop integrals
  dream of computer codes?", *Comput. Phys. Commun.* 109357 (2024).
  [arXiv:2312.14089](https://arxiv.org/abs/2312.14089),
  DOI [10.1016/j.cpc.2024.109357](https://doi.org/10.1016/j.cpc.2024.109357).
- J. A. M. Vermaseren, "New features of FORM",
  [arXiv:math-ph/0010025](https://arxiv.org/abs/math-ph/0010025).
- J. Kuipers, T. Ueda, J. A. M. Vermaseren, J. Vollinga, "FORM version 4.0",
  [arXiv:1203.6543](https://arxiv.org/abs/1203.6543),
  DOI [10.1016/j.cpc.2012.12.028](https://doi.org/10.1016/j.cpc.2012.12.028).
- K. Peeters, "A field-theory motivated approach to symbolic computer algebra",
  *Comput. Phys. Commun.* **176**, 550–558 (2007).
  [arXiv:cs/0608005](https://arxiv.org/abs/cs/0608005),
  DOI [10.1016/j.cpc.2007.01.003](https://doi.org/10.1016/j.cpc.2007.01.003).
- K. Peeters, "Introducing Cadabra: a symbolic computer algebra system for
  field theory problems",
  [arXiv:hep-th/0701238](https://arxiv.org/abs/hep-th/0701238).

**Textbook conventions**

- M. E. Peskin, D. V. Schroeder, *An Introduction to Quantum Field Theory*
  (1995) — signature `(+,−,−,−)`, spinor normalisation, Mandelstam routing for
  `2 → 2`. Used here for the convention cross-map in §4 only.

---

## 13. How to use this document

- Implementing Phase 5? Read §2 for the boundary, §4 and §5 for the
  conventions, §6 for the algorithm, then work the contracts in
  [`docs/agent-tasks/QFT_DIRAC.md`](../agent-tasks/QFT_DIRAC.md).
- Adding a golden case? It goes in `tests/oracle/test_qo_golden.c` with a new
  ID in §7. An identity in this document without a test is not verified.
- Un-deferring `γ⁵`, Fierz, spinors or Yang-Mills? Start by re-reading §4.5 and
  §2 — each of those items has a stated blocking dependency, and the dependency
  is the work, not the identity.
