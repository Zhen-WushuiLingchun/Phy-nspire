# Exact SU(N) colour algebra

Implementation: `include/phy/color.h`, `src/qft/color.c`.

This is the native implementation of contract Q-6 in
[`agent-tasks/QFT_DIRAC.md`](agent-tasks/QFT_DIRAC.md). It is an exact,
symbolic layer over the shared typed IR and scalar CAS. It instantiates no
floating-point matrices on the calculator.

## Conventions

The fundamental generators use the particle-physics normalization

\[
\operatorname{Tr}(T^aT^b)=T_F\delta^{ab},\qquad T_F=\frac12,
\]

and

\[
[T^a,T^b]=i f^{abc}T^c.
\]

The structure constants are real and totally antisymmetric. The symmetric
tensor needed by a trace of three generators is defined by the compatible
identity

\[
\operatorname{Tr}(T^aT^bT^c)
 =\frac14\left(d^{abc}+i f^{abc}\right).
\]

The two quadratic Casimirs are

\[
C_F=\frac{N^2-1}{2N},\qquad C_A=N.
\]

Results whose conventional form is a Casimir identity use the presentation
atoms `C_F` and `C_A`; `SUNExpandCasimirs[expr,N]` replaces them by their raw
expressions in `N`. This follows the useful part of FeynCalc's
`SUNNToCACF -> True` default without importing FeynCalc code.

`N` is an exact IR expression. It may remain an unbound symbol. If it is an
exact number, this layer accepts only an integer `N >= 2`; a rational
non-integer is a domain error rather than a fictitious group.

## Typed indices

Every colour index is a `PHY_IR_INDEX` in the `ColorAdjoint` space. Bare
reader-facing names such as `a` are converted to upper adjoint indices by the
evaluator. Explicit indices are also accepted:

```text
Up[a,ColorAdjoint]
```

A `Lorentz`, `LorentzGR`, spinor, fundamental-colour, or generic index is not
convertible to this space. Passing one to `SUNDelta`, `SUNF`, `SUND`, `SUNT`,
or a colour trace is `PHY_ERR_TYPE`. This is what prevents colour contraction
from interfering with Dirac/Lorentz contraction.

The native invariant tensors have canonical order:

- `SUNDelta[a,b]` is symmetric;
- `SUNF[a,b,c]` is totally antisymmetric, including its exact permutation
  sign, and is zero when two slots coincide;
- `SUND[a,b,c]` is totally symmetric.

`SUNDelta[a,a,N]` evaluates to the adjoint dimension `N^2-1`.
`SUNDeltaContract[a,b,expr,N]` substitutes the one occurrence of either
contracted index in `expr`. If neither occurs, the explicit product remains.
If the contraction is ambiguous because both occur or one occurs more than
once, it returns `PHY_ERR_UNSUPPORTED`; it does not guess a dummy-index
ordering. Powers are counted semantically, so `T_b^2` is correctly recognized
as two occurrences even though the scalar collector stores one base node.

## Traces and generators

The evaluator spellings are:

| Input | Exact result |
| --- | --- |
| `SUNT[a,N]` | fundamental `SUNGenerator[a]` |
| `SUNCommutator[a,b,N]` | `I SUNF[a,b,c] SUNGenerator[c]` with a fresh typed dummy |
| `SUNTrace[{},N]` | `N` |
| `SUNTrace[{a},N]` | `0` |
| `SUNTrace[{a,b},N]` | `SUNDelta[a,b]/2` |
| `SUNTrace[{a,b,c},N]` | `(SUND[a,b,c] + I SUNF[a,b,c])/4` |
| `SUNTrace[{a,b,c,d,...},N]` | held `SUNTrace[N,a,b,c,d,...]` |

The held spelling carries `N` explicitly, so it stays meaningful after the
temporary colour context is destroyed. It is also idempotent when pasted back
into a cell. The default trace ceiling is 16 generators; a longer input is a
typed term-limit error.

Traces longer than three are deliberately not expanded. This matches
FeynCalc's automatic trace boundary and prevents uncontrolled expression
growth on a 64 MiB device.

## Casimir and component commands

| Input | Result |
| --- | --- |
| `SUNCF[N]` | `(N^2-1)/(2N)` |
| `SUNCA[N]` | `N` |
| `SUNFundamentalCasimir[N]` | `C_F IdentityFundamental` |
| `SUNAdjointCasimir[a,b,N]` | `C_A SUNDelta[a,b]` |
| `SUNExpandCasimirs[expr,N]` | replaces `C_F`, `C_A` by their definitions |
| `SUNFComponent[N,a,b,c]` | exact one-based textbook component for `N=2,3` |

`SUNFComponent` reuses the exact built-in Lie-group tables. In the public
command the component labels are one-based, matching the literature:

```text
SUNFComponent[3,1,2,3]   -> 1
SUNFComponent[3,1,4,7]   -> 1/2
SUNFComponent[3,4,5,8]   -> Sqrt[3]/2
SUNCF[3]                 -> 4/3
```

For symbolic `N` and for concrete `N > 3`, `SUNFComponent` is unsupported:
the abstract `SUNF` tensor remains available, but this module does not pretend
to have constructed a general matrix basis.

All commands are present in the CAS insertion palette. Their results use the
same typed-IR-to-nMarkdown MathTree path as scalar, tensor, GR, and Dirac
results, so colour indices, fractions, powers, and roots share one two-
dimensional layout backend.

## Deliberate boundary

This Q-6 layer does not implement:

- the Fierz/completeness identity;
- explicit general-`N` generator matrices;
- automatic traces longer than three;
- a general colour dummy-index canonicalizer;
- colour-flow bases, representation decomposition, or birdtracks;
- QCD amplitude generation, gauge fixing, ghosts, or renormalization.

The Fierz identity is specifically deferred by the Q-6 contract because it
needs `1/N` singlet bookkeeping and a canonical spinor-chain ordering. Returning
it without those prerequisites would make mixed colour/Dirac expressions
order-dependent.

## Verification

- `tests/test_color.c`: 216 exact checks over symbolic `N`, invariant-tensor
  symmetry, typed index isolation, traces, commutators, delta contraction,
  Casimir presentation/expansion, and exact SU(2)/SU(3) components;
- `tests/test_eval.c`: reader-facing `SUN*` commands, held-trace round trip,
  the textbook values above, and Lorentz/colour rejection;
- `tests/oracle/test_qo_golden.c`: the independent generalized Gell-Mann
  matrix oracle, including C-1 through C-7 and `N=2...6` Casimir checks;
- `make color-link-check`: 23/23 public APIs retained by the Ndless linker,
  4,924 bytes of colour-layer text, a 52,764-byte isolated probe, and no float
  formatter, libm call, or ARM soft-float helper;
- the clean product build is 1,173,026 bytes, 18.6% of the 6 MiB ceiling.

The numeric oracle is host-only and independent of the native implementation:
it certifies the conventions from explicit matrices, while the calculator
module remains exact and representation-independent.
