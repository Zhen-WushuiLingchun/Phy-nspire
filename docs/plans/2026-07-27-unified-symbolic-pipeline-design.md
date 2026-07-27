# Unified symbolic physics pipeline

## One formula tree

Phy-nspire will keep typed IR as the mathematical source of truth and use
nMarkdown's `MathTree` as the only display tree. Markdown LaTeX continues to
enter through nMarkdown's parser; CAS output enters through a new direct
`phy_ir_context`/`phy_ir_ref` to `MathTree` builder. Both paths then call the
same `layout_math_tree`, Latin Modern Math shaper, OpenType MATH metrics, and
RGB565 compositor.

The IR builder maps exact rationals to `Fraction`, powers and tensor indices to
`Scripts`, function calls to Roman heads plus `Delimited` arguments, and
structural operators to correctly classified row atoms. It maps common Greek
symbol names to Unicode math glyphs without serializing LaTeX. Precedence is
preserved with explicit `Delimited` nodes, so `(m+x)^3` remains unambiguous.
Invalid/deep input becomes a typed error node rather than malformed tree
memory. The existing compact C renderer remains only as a temporary isolated
test target and is removed from notebook output.

## One head and index vocabulary

Reader syntax is sugar; typed heads are the contract. A registry will describe
each public Mathematica-style head with arity, argument roles, evaluator,
algebraic attributes, palette category, and completion text. Parser,
evaluator, documentation, and command palette will consume that registry.
Unsupported reserved heads continue to return `PHY_ERR_UNSUPPORTED`.

The canonical index form is explicit:

```text
Up[mu, Lorentz]
Down[nu, Lorentz]
Tensor[g, Down[mu, Lorentz], Down[nu, Lorentz]]
```

Compact forms may later expand into these nodes, but are never stored as
string tricks. Index metadata distinguishes Generic, Lorentz, Spinor,
ColorFundamental, ColorAdjoint, and user bundles. Contraction validates bundle
and variance, recognizes dummy pairs, and alpha-renames them deterministically.
Tensor, GR, and QFT evaluators therefore share the same index rules.

## Staged evaluator

1. Scalar calculus: exact elementary derivatives, assumptions, a conservative
   rule-based indefinite integrator, substitution checks, limits and series
   only when implemented.
2. Tensor core: contraction, permutation symmetry, raising/lowering, metric
   inverse, covariant derivative.
3. GR: Christoffel, Riemann, Ricci, scalar curvature, Einstein tensor, Bianchi
   checks, and bounded component evaluation.
4. QFT: Lorentz and Dirac algebra, traces, polarization sums, Mandelstam
   relations, color algebra, graph objects, Wick contractions, and bounded
   one-loop integral reduction.

Every successful result remains symbolic typed IR. Numerical fallback must be
explicitly requested and visually identified. Each layer gets golden tests
against independent Mathematica/xAct/FeynCalc or FORM references, plus memory,
term, recursion, and real-device timing ceilings.

## Verification gates

- IR and LaTeX versions of representative formulas produce compatible
  nMarkdown metrics and visibly equivalent structures.
- Every IR kind has a direct-tree test, including mixed tensor indices and
  noncommutative products.
- Parser/head metadata and evaluator coverage are checked for drift.
- Integrals are differentiated back to the integrand before acceptance when
  the derivative is decidable.
- Tensor/GR identities and QFT traces use independent golden oracles.
- Host, ASan/UBSan, ARM link, size ceiling, and physical-device smoke tests
  remain mandatory at every deployment milestone.
