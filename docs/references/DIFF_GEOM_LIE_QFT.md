# Differential geometry, Lie theory, and scalar/gauge QFT reference pack

This pack fixes the dependency order and comparison oracles for the native
geometry-to-QFT path. It is not evidence that every item is implemented.
Algorithms are implemented against Phy-nspire's bounded C/typed-IR contracts;
upstream projects are semantic and golden-test references, not calculator
dependencies.

## Dependency graph

1. `phy_manifold` and coordinate charts;
2. exterior algebra and differential forms;
3. metrics, volume forms, Hodge dual, vector fields, and Lie derivatives;
4. finite-dimensional Lie algebras from structure constants;
5. Lie-algebra-valued forms and gauge connections;
6. scalar `phi^4` fields, Wick/diagram objects, and bounded loop masters;
7. Yang--Mills field strength, color algebra, amplitudes, and Ward/Bianchi
   checks.

The order is deliberate. General relativity consumes 1--3. Yang--Mills
consumes 1--5. Feynman diagrams consume typed field, vertex, propagator, and
integral objects from 6 rather than being a disconnected drawing feature.

## Implemented bounded slice

As of 2026-07-27, the native backend implements:

- oriented manifolds and bounded chart registration;
- canonical differential forms, wedge, exterior derivative, interior product,
  orthonormal Hodge, and general symmetric coordinate-metric Hodge/volume;
- exact finite Lie algebras, Lie brackets, adjoint/Killing operations, and the
  built-in `U(1)`, `SU(2)`, `SO(3)`, `SU(3)`, and `SO(1,3)` group metadata;
- Lie-algebra-valued forms, gauge connections, `D_A`,
  `F=dA+(g/2)[A,A]`, infinitesimal variations, Bianchi residuals, and
  quadratic Yang--Mills densities;
- the bounded real `phi^4` Lagrangian, propagator/vertex, tadpole, and
  `s/t/u` one-loop master objects.

Lie derivative/pullback/transition maps, automatic matrix-generator
recognition, roots/weights, perturbative Yang--Mills vertices, ghosts/BRST,
Ward identities, general diagram generation, and loop reduction remain
deferred. Typed notebook heads and palettes exist, but the stateful notebook
environment is not yet wired to these backend objects.

## Primary software oracles

### SageManifolds / SageMath

- Documentation:
  <https://doc.sagemath.org/html/en/reference/manifolds/index.html>
- Differential forms:
  <https://doc.sagemath.org/html/en/reference/manifolds/sage/manifolds/differentiable/diff_form.html>
- Lie algebras by structure coefficients:
  <https://doc.sagemath.org/html/en/reference/algebras/sage/algebras/lie_algebras/structure_coefficients.html>
- Classical matrix Lie algebras:
  <https://doc.sagemath.org/html/en/reference/algebras/sage/algebras/lie_algebras/classical_lie_algebra.html>
- Source: <https://github.com/sagemath/sage>

Use as the main coordinate/differential-form and finite Lie-algebra oracle:
exterior derivative, wedge, interior product, Lie derivative, Hodge dual,
structure coefficients, Jacobi identity, adjoint maps, and Killing forms.
Sage is GPLv2+ and compatible with this GPLv3 project, but its Python/Cython
runtime is much too large for the CX II. No runtime port is planned.

### Cadabra

- Project/source documentation:
  <https://kpeeters.github.io/cadabra2/index.html>
- Manual: <https://cadabra.science/the_cadabra_book.pdf>
- License: <https://cadabra.science/license.html>

Use as a field-theory notation and tensor-property oracle. Cadabra is
particularly useful for differential-form degrees, anticommutation, multiple
index families, Clifford algebra, and canonicalization tests. Its C++ tree,
Python binding, and general algorithm framework are not suitable for a direct
calculator port. It is GPLv3, so small derived tests or code would be legally
compatible, but independent bounded implementations remain easier to audit.

### xAct/xTerior

- Project: <https://www.xact.es/>
- xTerior documentation:
  <http://www.xact.es/Documentation/English/xTeriorDoc.nb>

Use for Wolfram-language-facing notation, form-valued tensors, covariant
exterior derivatives, and GR/gauge examples. It is an oracle for expected
heads and notebook syntax, not a portable backend.

### FeynCalc

- Project/manual: <https://feyncalc.github.io/>
- Example gallery: <https://feyncalc.github.io/examples>
- `phi^4` one-loop renormalization:
  <https://feyncalc.github.io/FeynCalcExamples/Phi4/OneLoop/Renormalization>
- Loop representation:
  <https://feyncalc.github.io/FeynCalcBookDev/Loops.html>

Use as the main QFT object-model and golden-workflow oracle: Lorentz and Dirac
algebra, SU(N) color, propagator denominators, topology objects, scalar
one-loop functions, and `phi^4` renormalization examples. FeynCalc is GPLv3
but depends on the Wolfram evaluator, so the target is a documented native
subset, not source-level transplantation.

### FORM

- Current reference manual:
  <https://form-dev.github.io/form-docs/stable/manual/>

Use for large-expression ordering, dummy-index and term-streaming semantics,
Dirac/color identities, and host-side stress oracles. FORM's workstation-scale
resource figures are not calculator performance evidence.

## Native geometry contract

### Manifold and chart

The bounded first model stores:

- a manifold name and dimension `1..4`;
- orientation `+1` or `-1`;
- pseudo-Riemannian signature `(n_minus, n_plus)`;
- one or more borrowed coordinate charts with matching dimension;
- later, explicit transition maps with domains and invertibility assumptions.

A chart is not itself a manifold. Existing `phy_chart` remains the coordinate
symbol owner. The manifold layer supplies shared identity, orientation, and
signature metadata and must not silently identify charts by dimension alone.

### Differential forms

A degree-`p` form in dimension `n` stores only the `binomial(n,p)`
independent components with strictly increasing coordinate indices. Repeated
indices are zero and a permutation contributes its parity. Coefficients are
typed-IR handles simplified through `phy_cas`.

First operations:

- addition and scalar multiplication;
- wedge with canonical graded signs;
- exterior derivative;
- interior product by a contravariant vector field;
- Hodge dual from a non-degenerate metric, orientation, and signature;
- Lie derivative through Cartan's formula `L_X = i_X d + d i_X`.

Acceptance identities:

- `d(d(alpha)) = 0`;
- `alpha wedge beta = (-1)^(pq) beta wedge alpha`;
- `d(alpha wedge beta) = d(alpha) wedge beta
  + (-1)^p alpha wedge d(beta)`;
- `i_X(alpha wedge beta) = i_X(alpha) wedge beta
  + (-1)^p alpha wedge i_X(beta)`;
- on a pseudo-Riemannian `n`-manifold with `s` negative directions,
  `star(star(alpha_p)) = (-1)^(p(n-p)+s) alpha_p`.

Pullback is deferred until transition maps can represent substitutions and
Jacobians without flattening chart identity.

## Native Lie-algebra contract

The first implementation is finite-dimensional and basis based:

- algebra name, dimension, basis symbols, and exact
  `f[a,b,c]` coefficients for `[T_a,T_b] = f[a,b,c] T_c`;
- sparse algebra elements as coefficient vectors over the shared CAS;
- bilinear antisymmetric bracket and generic noncommutative commutator head;
- construction-time antisymmetry and Jacobi validation;
- adjoint matrices and Killing form;
- structure-constant extraction from an explicitly supplied matrix
  representation when exact coefficient solving is decidable.

Built-in catalog:

- `u(1)` with zero bracket;
- `su(2)` / `so(3)` in a real epsilon basis;
- `su(3)` in a fixed Gell-Mann convention;
- Lorentz `so(1,3)` in a fixed `J_i,K_i` basis.

The catalog records normalization and real/complex convention in metadata.
“Define Lie group” initially means group identity plus its finite-dimensional
Lie algebra and standard representation metadata. Global topology,
exponential-map simplification, root/weight systems, and arbitrary matrix-group
recognition are later work.

## Scalar `phi^4` contract

The minimum usable scalar-field workflow contains typed heads for:

- `ScalarField`, `Lagrangian`, `Propagator`, `Vertex`;
- external legs and momentum-conserving delta functions;
- `WickContract`, `Diagram`, `SymmetryFactor`;
- `LoopIntegral`, `FeynmanParameter`, and named scalar masters.

The first golden workflows are:

1. derive the free inverse propagator and quartic vertex from
   `L = 1/2 (d phi)^2 - 1/2 m^2 phi^2 - lambda/4! phi^4`;
2. generate the connected tree four-point vertex;
3. enumerate the one-loop two-point tadpole and the three one-loop four-point
   channels with correct combinatorial/symmetry factors;
4. reduce their integrals to named dimensionally regulated scalar masters;
5. compare normalization, UV poles, and channel multiplicities with the
   pinned FeynCalc examples.

The calculator will not attempt unrestricted multiloop IBP reduction. It keeps
unevaluated typed integrals when outside the bounded master table.

## Yang--Mills contract

For a Lie-algebra-valued connection one-form
`A = A_mu^a T_a dx^mu`, implement:

- covariant derivative `D = d + g [A, .]` in a declared convention;
- curvature `F = dA + g A wedge A`, equivalently components
  `F^a_mn = d_m A^a_n - d_n A^a_m + g f^a_bc A^b_m A^c_n`;
- infinitesimal gauge transformations of `A` and `F`;
- Bianchi identity `D F = 0`;
- invariant trace/Killing contraction and
  `L_YM = -1/2 Tr(F wedge star(F))`;
- later, gauge fixing, ghosts, propagators, three-/four-gauge-boson vertices,
  and Ward identities.

The first tests use `u(1)` to prove the nonlinear term vanishes and `su(2)` to
prove the non-Abelian term, gauge covariance, Jacobi-dependent Bianchi
identity, and color contractions. A full QCD model is not the first device
milestone.

## Resource boundary

All public algorithms must:

- return a typed status on unsupported dimension, degree, rank, term count, or
  undecidable assumption;
- preserve exact coefficients and never substitute floating-point sampling for
  an identity;
- publish no partially built object on failure;
- expose cancellation at loops whose cost grows as `n^p`, basis dimension
  cubed, graph count, or term count;
- retain a typed unevaluated head when a mathematically valid operation lies
  outside the implemented decision class.
