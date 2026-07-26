# Physics reference and golden-value corpus

Date: 2026-07-26
Status: original reference material, independently derived and cross-checked

`ROADMAP.md` requires "a comparison corpus derived from xAct examples"
(Phase 2), "known Schwarzschild/Kerr identities" (Phase 3), "canonical
commutators" and "spin identities" (Phase 4), and "a small FeynCalc-compatible
golden corpus" (Phase 5). None of that existed. This document supplies it.

Nothing here depends on a toolchain, a device, or network access, so it can be
reviewed and turned into host tests immediately — ahead of every BLOCKING
contract in `TASK_CONTRACTS.md`.

**Units.** Geometric units `G = c = 1` throughout §3. Restore SI by
`M → GM/c²`; a curvature invariant of mass-dimension `−4` such as the
Kretschmann scalar carries `G²M²/c⁴`.

---

## 1. Conventions must be declared before anything is computed

This is the single largest source of silent error in tensor CAS work. Two
correct implementations disagree on the sign of the Riemann tensor, and every
downstream test then "fails" against a reference that was never wrong. Fix
these in the IR as declared metadata, not as folklore.

| Choice | Options in the literature | Recommended default |
| --- | --- | --- |
| Metric signature | `(−,+,+,+)` (MTW, Wald) vs `(+,−,−,−)` (Peskin, most QFT) | `(−,+,+,+)` for GR modules; `(+,−,−,−)` for QFT modules — **declare per notebook, never global** |
| Riemann sign | `R^ρ{}_{σμν} = ∂_μΓ^ρ_{νσ} − ∂_νΓ^ρ_{μσ} + Γ^ρ_{μλ}Γ^λ_{νσ} − Γ^ρ_{νλ}Γ^λ_{μσ}` (MTW) vs overall minus | MTW as above |
| Ricci contraction | `R_{μν} = R^ρ{}_{μρν}` vs `R^ρ{}_{μνρ}` | first slot: `R_{μν} = R^ρ{}_{μρν}` |
| Levi-Civita | symbol `ε̃` (density, integer entries) vs tensor `ε = √|g| ε̃` | keep **both**, as distinct typed nodes |
| `ε^{0123}` | `+1` vs `−1` | `+1` |
| Gamma-5 | `γ⁵ = iγ⁰γ¹γ²γ³` | as given |

With the MTW choices, the Einstein equation is `G_{μν} = 8πT_{μν}`, and the
sphere has **positive** Ricci scalar. If an implementation reproduces §3.2's
2-sphere with `R = −2/a²`, it has the opposite Riemann sign convention — that
is a convention mismatch, not a bug, and the IR should be able to say so.

**Levi-Civita is two objects, not one.** `ε̃_{μνρσ}` has constant integer
entries and is a tensor *density*; `ε_{μνρσ} = √|g| ε̃_{μνρσ}` is a tensor.
Conflating them produces `√|g|` factor errors that survive every flat-space
test — because in Cartesian coordinates `√|g| = 1`. Test them in **polar**
coordinates.

---

## 2. Phase 2 — tensor and index core

### 2.1 Riemann symmetries (canonicalizer targets)

```
R_{abcd} = −R_{bacd}          antisymmetry, first pair
R_{abcd} = −R_{abdc}          antisymmetry, second pair
R_{abcd} =  R_{cdab}          pair exchange
R_{a[bcd]} = 0                first Bianchi:  R_{abcd} + R_{acdb} + R_{adbc} = 0
∇_{[e}R_{ab]cd} = 0           second Bianchi
R_{ab} = R_{ba}               Ricci symmetry
∇^a G_{ab} = 0                contracted Bianchi  (the conservation identity)
```

### 2.2 Component counts — cheap, sharp canonicalizer tests

Independent components of the Riemann tensor in `d` dimensions:

```
N(d) = d²(d² − 1)/12
```

| `d` | 2 | 3 | 4 | 5 |
| --- | ---: | ---: | ---: | ---: |
| `N(d)` | 1 | 6 | **20** | 50 |

A canonicalizer that reduces a general `R_{abcd}` to more than `N(d)`
independent components has an incomplete symmetry set; fewer means it is
over-identifying. This single count catches most dummy-index and
symmetry-application bugs, and it is far cheaper to test than a curvature
computation.

Related structural facts worth asserting:

- The Weyl tensor vanishes **identically** for `d ≤ 3`.
- In `d = 2`, `G_{μν} ≡ 0` identically, for *every* metric (see §3.2).
- `R_{abcd}` in `d = 2` has one independent component, so
  `R_{abcd} = (R/2)(g_{ac}g_{bd} − g_{ad}g_{bc})`.

### 2.3 Commutator of covariant derivatives

```
[∇_a, ∇_b] V^c =  R^c{}_{dab} V^d
[∇_a, ∇_b] ω_c = −R^d{}_{cab} ω_d
```

Sign-convention dependent — this is a good place to *detect* a convention
mismatch deliberately, by asserting consistency with §3.2.

### 2.4 Differential forms

```
d² = 0                                   nilpotency
d(α ∧ β) = dα ∧ β + (−1)^p α ∧ dβ        p = deg α
α ∧ β = (−1)^{pq} β ∧ α
**α = s(−1)^{p(d−p)} α                    s = sign det g  (s = −1 Lorentzian)
```

`d² = 0` and the graded Leibniz rule are the two cheapest exterior-derivative
regression tests, and they need no metric.

---

## 3. Phase 3 — general relativity golden values

### 3.0 A test-design trap, stated explicitly

**Schwarzschild is a vacuum solution, so `R_{μν} = 0` and `R = 0`.** An
implementation that returns zero for *everything* — a plausible failure mode
for a broken contraction or a Christoffel routine that silently drops terms —
**passes** the Ricci and scalar-curvature tests.

Every vacuum test must therefore be paired with a **non-vanishing** invariant.
Use the Kretschmann scalar `K = R_{abcd}R^{abcd}`. This applies to
Schwarzschild, Kerr, and any vacuum template. Do not accept a green vacuum
suite that never computes a non-zero curvature quantity.

### 3.1 Flat space in curvilinear coordinates

The point is that the Christoffel symbols are **non-zero** while the curvature
vanishes — this catches routines that confuse "connection" with "curvature".

2D polar, `ds² = dr² + r² dθ²`:

```
Γ^r{}_{θθ} = −r        Γ^θ{}_{rθ} = Γ^θ{}_{θr} = 1/r        all others 0
R^a{}_{bcd} = 0        R_{ab} = 0        R = 0        K = 0
```

3D spherical, `ds² = dr² + r²dθ² + r² sin²θ dφ²`:

```
Γ^r{}_{θθ} = −r                Γ^r{}_{φφ} = −r sin²θ
Γ^θ{}_{rθ} = 1/r               Γ^θ{}_{φφ} = −sinθ cosθ
Γ^φ{}_{rφ} = 1/r               Γ^φ{}_{θφ} = cotθ
R^a{}_{bcd} = 0
```

### 3.2 The 2-sphere of radius `a` — the sign-convention anchor

`ds² = a²(dθ² + sin²θ dφ²)`

```
Γ^θ{}_{φφ} = −sinθ cosθ        Γ^φ{}_{θφ} = Γ^φ{}_{φθ} = cotθ
R_{θφθφ} = a² sin²θ
R_{θθ} = 1        R_{φφ} = sin²θ
R = 2/a²                       Gaussian curvature K_G = 1/a² = R/2
G_{μν} = 0                     identically, because d = 2
```

Two independent checks in one small example: `R = +2/a²` pins the Riemann sign
convention (§1), and `G_{μν} = 0` with `R ≠ 0` verifies the Einstein-tensor
assembly — since here `R_{θθ} = 1` and `½Rg_{θθ} = ½(2/a²)(a²) = 1` must cancel
exactly.

### 3.3 Schwarzschild

`ds² = −f dt² + f⁻¹dr² + r²(dθ² + sin²θ dφ²)`, with `f = 1 − 2M/r`.

Christoffel symbols (all non-zero ones):

```
Γ^t{}_{tr} = M / (r(r − 2M))           Γ^r{}_{tt} = M(r − 2M)/r³
Γ^r{}_{rr} = −M / (r(r − 2M))          Γ^r{}_{θθ} = −(r − 2M)
Γ^r{}_{φφ} = −(r − 2M) sin²θ           Γ^θ{}_{rθ} = Γ^φ{}_{rφ} = 1/r
Γ^θ{}_{φφ} = −sinθ cosθ                Γ^φ{}_{θφ} = cotθ
```

Curvature — note the vacuum trap of §3.0:

```
R_{μν} = 0        R = 0        G_{μν} = 0        C_{abcd} ≠ 0
K = R_{abcd}R^{abcd} = 48 M² / r⁶            ← the test that must not be skipped
```

Fully covariant Riemann components:

```
R_{trtr} = −2M/r³              R_{θφθφ} = 2Mr sin²θ
R_{tθtθ} = (M/r) f             R_{rθrθ} = −M/(r f)
R_{tφtφ} = (M/r) f sin²θ       R_{rφrφ} = −M sin²θ/(r f)
```

*Consistency (verified during preparation of this document):* with a diagonal
metric, `K = 4 Σ_{a<b} (g^{aa}g^{bb})² (R_{abab})²`. The six components above
contribute `16 + 4 + 4 + 4 + 4 + 16` in units of `M²/r⁶`, giving exactly
`48M²/r⁶`. A tensor core that reproduces the components but not the invariant
(or the reverse) has an index-raising or contraction defect.

Landmark radii:

```
horizon r = 2M          photon sphere r = 3M          ISCO r = 6M
```

`K` is finite at `r = 2M` and divergent only at `r = 0` — the standard
coordinate-vs-curvature singularity test, and a good exercise for the
coordinate-transformation checks Phase 3 requires.

### 3.4 Reissner–Nordström

`f = 1 − 2M/r + Q²/r²`

```
R = 0                     ← trace of the Maxwell stress tensor vanishes in d = 4
R_{μν} ≠ 0                ← NOT a vacuum solution; distinguishes it from §3.3
K = 48M²/r⁶ − 96MQ²/r⁷ + 56Q⁴/r⁸
horizons r_± = M ± √(M² − Q²)
```

This is the sharpest available test of a Ricci implementation: `R = 0` but
`R_{μν} ≠ 0`. An implementation that computes `R` by an independent route
rather than by tracing `R_{μν}` will disagree here if either is wrong. Setting
`Q = 0` must recover §3.3 exactly.

### 3.5 Kerr

Boyer–Lindquist, `Σ = r² + a²cos²θ`, `Δ = r² − 2Mr + a²`:

```
R_{μν} = 0        R = 0                        (vacuum)
K = 48M² (r² − a²cos²θ) [ Σ² − 16 r² a² cos²θ ] / Σ⁶
horizons   r_± = M ± √(M² − a²)
ergosurface r_E = M + √(M² − a²cos²θ)
```

The `a → 0` limit must return `48M²/r⁶` exactly — a cheap symbolic-limit
regression that exercises simplification, not just differentiation.

### 3.6 Maximally symmetric spaces — (anti-)de Sitter

In `d = 4` with cosmological constant `Λ`:

```
R_{μνρσ} = (Λ/3)(g_{μρ}g_{νσ} − g_{μσ}g_{νρ})
R_{μν} = Λ g_{μν}          R = 4Λ          K = 8Λ²/3
```

With `Λ = 3/L²` (de Sitter radius `L`): `R = 12/L²`, `K = 24/L⁴`.
Anti-de Sitter is the same with `Λ < 0`; `K > 0` either way.

General `d`, curvature parameter `K₀`: `R = d(d−1)K₀`,
`K = 2d(d−1)K₀²`. Setting `d = 2`, `K₀ = 1/a²` reproduces §3.2's `R = 2/a²`.

---

## 4. Phase 4 — quantum mechanics

### 4.1 Canonical algebra

```
[x, p] = iħ            [x_i, p_j] = iħ δ_ij            [x_i, x_j] = [p_i, p_j] = 0
[J_i, J_j] = iħ ε_ijk J_k
[J², J_i] = 0
J_± |j,m⟩ = ħ √(j(j+1) − m(m±1)) |j,m±1⟩
J² |j,m⟩ = ħ² j(j+1) |j,m⟩          J_z |j,m⟩ = ħ m |j,m⟩
```

### 4.2 Pauli matrices

```
σ_i σ_j = δ_ij I + i ε_ijk σ_k          ← the master identity; the two below follow
[σ_i, σ_j] = 2i ε_ijk σ_k
{σ_i, σ_j} = 2 δ_ij I
σ_i² = I        Tr σ_i = 0        det σ_i = −1
Tr(σ_i σ_j) = 2δ_ij
Tr(σ_i σ_j σ_k) = 2i ε_ijk
(a·σ)(b·σ) = (a·b) I + i (a × b)·σ
exp(iθ n̂·σ) = cos θ I + i sin θ (n̂·σ)
```

Deriving `[σ_i,σ_j]` and `{σ_i,σ_j}` *from* the master identity, rather than
asserting all three, is a good test that the noncommutative product and the
commutator/anticommutator nodes are consistent with each other.

### 4.3 Traces and density matrices

```
Tr(AB) = Tr(BA)                     cyclicity
Tr(A ⊗ B) = Tr(A) Tr(B)
ρ† = ρ,  Tr ρ = 1,  ρ ≥ 0
Tr(ρ²) = 1  ⟺  pure state
⟨A⟩ = Tr(ρA)
```

`Tr(A ⊗ B) = Tr A · Tr B` is the cheapest check that tensor-product spaces and
partial traces are wired correctly.

---

## 5. Phase 5 — QFT and gauge theory

Signature `(+,−,−,−)`, `ε^{0123} = +1`, `d = 4` unless stated.

### 5.1 Dirac algebra

```
{γ^μ, γ^ν} = 2 g^{μν} I₄
γ⁵ = i γ⁰γ¹γ²γ³        (γ⁵)² = I        {γ⁵, γ^μ} = 0
a̸ b̸ + b̸ a̸ = 2 (a·b) I          a̸ a̸ = a² I
```

Contraction identities (`d = 4`):

```
γ^μ γ_μ = 4 I
γ^μ γ^ν γ_μ = −2 γ^ν
γ^μ γ^ν γ^ρ γ_μ = 4 g^{νρ} I
γ^μ γ^ν γ^ρ γ^σ γ_μ = −2 γ^σ γ^ρ γ^ν
```

Traces — the core of any FeynCalc-compatible corpus:

```
Tr I = 4
Tr(odd number of γ) = 0
Tr(γ^μ γ^ν) = 4 g^{μν}
Tr(γ^μ γ^ν γ^ρ γ^σ) = 4 (g^{μν}g^{ρσ} − g^{μρ}g^{νσ} + g^{μσ}g^{νρ})
Tr γ⁵ = 0
Tr(γ⁵ γ^μ γ^ν) = 0
Tr(γ⁵ γ^μ γ^ν γ^ρ γ^σ) = −4i ε^{μνρσ}          ← sign depends on ε^{0123} = +1
```

Keep general `d` available for the contraction identities (`γ^μγ_μ = d`,
`γ^μγ^νγ_μ = (2−d)γ^ν`): dimensional regularisation needs it, and it is a
useful check that `d` is a symbol rather than a hard-coded 4.

### 5.2 Mandelstam variables

For `1 + 2 → 3 + 4`:

```
s = (p₁+p₂)²        t = (p₁−p₃)²        u = (p₁−p₄)²
s + t + u = m₁² + m₂² + m₃² + m₄²
```

The sum rule is the single best on-shell-substitution regression test: it holds
only if momentum conservation and all four mass-shell conditions are applied
consistently. Massless case: `s + t + u = 0`.

### 5.3 SU(N) colour algebra

```
[T^a, T^b] = i f^{abc} T^c
Tr(T^a T^b) = T_F δ^{ab}            T_F = 1/2
T^a T^a = C_F I                     C_F = (N² − 1)/(2N)
f^{acd} f^{bcd} = C_A δ^{ab}        C_A = N
T^a T^b T^a = (C_F − C_A/2) T^b     = −T^b/(2N)
```

For `N = 3` (QCD): `C_F = 4/3`, `C_A = 3`, `T_F = 1/2`, and `T^aT^bT^a = −T^b/6`.
Number of generators: `N² − 1 = 8`.

### 5.4 Gauge theory

```
F_{μν} = ∂_μ A_ν − ∂_ν A_μ − i g [A_μ, A_ν]
D_μ = ∂_μ − i g A_μ
[D_μ, D_ν] = −i g F_{μν}
D_{[μ} F_{νρ]} = 0                  Bianchi identity
```

Abelian limit (`g → 0` in the commutator) must recover
`F_{μν} = ∂_μA_ν − ∂_νA_μ` and the Maxwell Bianchi identity
`∂_{[μ}F_{νρ]} = 0` — equivalently `dF = 0` in the language of §2.4, which
links the gauge module to the differential-forms tests.

---

## 6. Suggested minimal acceptance set

Ordered so each phase's first test is cheap and highly diagnostic.

| Phase | Test | Catches |
| --- | --- | --- |
| 2 | `N(4) = 20` independent Riemann components | symmetry/canonicalization defects |
| 2 | `d² = 0`, graded Leibniz | exterior derivative |
| 2 | `ε` vs `ε̃` in **polar** coordinates | missing `√\|g\|` |
| 3 | flat space in polar: `Γ ≠ 0`, `R = 0` | connection/curvature confusion |
| 3 | 2-sphere: `R = +2/a²`, `G_{μν} = 0` | Riemann sign; Einstein assembly |
| 3 | Schwarzschild: `K = 48M²/r⁶` | the all-zeros false pass (§3.0) |
| 3 | Reissner–Nordström: `R = 0` but `R_{μν} ≠ 0` | Ricci trace vs Ricci tensor |
| 3 | Kerr `a → 0` → Schwarzschild `K` | symbolic limit + simplification |
| 4 | `σ_iσ_j = δ_ij I + iε_ijk σ_k` ⟹ commutator and anticommutator | noncommutative product |
| 4 | `Tr(A ⊗ B) = Tr A · Tr B` | tensor-product spaces |
| 5 | `Tr(γ^μγ^νγ^ργ^σ)` four-term identity | Dirac trace engine |
| 5 | `s + t + u = Σ m_i²` | on-shell substitution |
| 5 | `C_F = 4/3`, `C_A = 3` for `N = 3` | SU(N) Casimirs |

Every entry is a closed-form identity checkable on the host, with no device, no
Giac, and no network. Recommend this become contract **P2-0**, runnable before
P0-1 unblocks anything else.
