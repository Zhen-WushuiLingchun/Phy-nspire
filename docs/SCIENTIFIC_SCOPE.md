# Scientific calculation scope

This document defines the calculations Phy-nspire should eventually support.
It orders them by mathematical dependency so that each layer has a usable,
testable stopping point.

The calculator is not intended to replace a workstation for large multiloop
integrals or high-rank perturbation expansions. It should be a compact,
auditable notebook for derivation, inspection, teaching, and moderate symbolic
work.

## 0. Symbolic foundation

The common expression layer must represent:

- exact integers, rational numbers, arbitrary-precision real and complex
  values;
- symbols, assumptions, equations, functions, derivatives, sums, products,
  powers, matrices, tensors, and noncommutative products;
- substitutions, delayed rewrite rules, canonical ordering, collection,
  expansion, factoring, cancellation, and simplification;
- explicit error values for undefined expressions, resource limits, and
  interrupted calculations.

Giac initially supplies general scalar algebra, calculus, polynomial,
matrix, and numeric operations. Physics objects remain typed nodes rather than
being flattened into ambiguous scalar function calls.

## 1. Advanced calculus

Required scalar and multivariable operations:

- symbolic differentiation, integration, limits, series, and asymptotics;
- gradients, Jacobians, Hessians, divergence, curl, and Laplacians;
- constrained differentiation and chain-rule-aware substitutions;
- ordinary differential equations and first-order systems within Giac's
  practical limits;
- coordinate transformations and change-of-variables Jacobians.

## 2. Linear and multilinear algebra

- exact and symbolic vectors and matrices;
- determinant, inverse, rank, null space, eigenvalues, and eigenvectors;
- bilinear and sesquilinear forms;
- tensor/Kronecker products, direct sums, commutators, and anticommutators;
- basis changes and indexed components.

## 3. Tensor calculus and manifolds

This is the first native physics milestone.

- typed manifolds, charts, coordinates, dimensions, signatures, and index
  families;
- upper/lower, free/dummy, coordinate/frame, and user-named indices;
- declared tensor symmetries, dummy-index renaming, canonicalization,
  symmetrization, antisymmetrization, contraction, and trace;
- metrics, inverse metrics, raising/lowering, volume forms, and
  Levi-Civita symbols;
- partial and covariant derivatives, affine connections, torsion, Lie
  derivatives, Lie brackets, and commutators of covariant derivatives;
- differential forms, wedge products, exterior derivatives, pullbacks,
  interior products, and Hodge duals.

Where practical, notation and canonical forms should resemble xAct while
remaining independent of the Wolfram Language evaluator.

## 4. General relativity and black-hole physics

The first GR slice computes from a coordinate metric:

- line element, metric and inverse metric;
- Christoffel symbols;
- Riemann and Ricci tensors;
- Ricci scalar and Einstein tensor;
- geodesic equations and conserved quantities from declared symmetries;
- Kretschmann and selected curvature invariants.

Later extensions:

- tetrads, spin connections, Newman-Penrose quantities, and Weyl scalars;
- Killing vectors/tensors and hypersurface geometry;
- ADM/3+1 quantities and simple perturbations;
- curated Schwarzschild, Kerr, Kerr-Schild, Reissner-Nordström, and
  (anti-)de Sitter templates;
- horizons, ergosurfaces, circular orbits, and effective potentials.

## 5. Quantum mechanics

- bra-ket and operator notation;
- noncommutative products, commutators, and anticommutators;
- Pauli matrices, angular-momentum algebras, ladder operators, and spin
  coupling;
- tensor-product Hilbert spaces, density matrices, traces, expectation values,
  and simple time evolution;
- symbolic two-level and finite-dimensional systems.

## 6. Relativistic QFT

- Lorentz vectors, metrics, scalar products, and momentum conservation;
- gamma matrices, slash notation, gamma-five, Dirac simplification and traces;
- spin and polarization sums;
- Mandelstam variables and on-shell substitutions;
- propagator, vertex, external-state, and amplitude objects;
- compact tree-level amplitude squaring and contraction workflows;
- selected color algebra for SU(N), including generators, structure constants,
  traces, and Casimirs.

The command vocabulary may resemble a small, documented subset of FeynCalc,
but semantics will be implemented natively.

## 7. Gauge theory

- Lie algebra declarations, representations, generators, roots, and structure
  constants for small standard groups;
- gauge-covariant derivatives, connections, curvature/field-strength forms,
  gauge transformations, and Bianchi identities;
- Abelian and Yang-Mills Lagrangian templates;
- compact Euler-Lagrange variation for fields;
- later BRST/ghost notation only after the core rewrite system is mature.

## 8. Feynman diagrams

The first diagram layer is deliberately modest:

- touchpad placement of particles, vertices, and labeled edges;
- straight, wavy, curly, dashed, and fermion-arrow propagator styles;
- model-independent graph serialization;
- rule-linked tree-level amplitudes;
- LaTeX/TikZ-like textual export where practical.

Automatic topology generation, full model-file import, and loop-integral
reduction are later projects.

## Cross-cutting notebook behavior

Every scientific module must work through notebook cells:

- Markdown explanation cells;
- editable two-dimensional expression cells;
- rendered result cells with expandable derivation steps;
- touchpad palettes for indices, tensors, operators, particles, and diagrams;
- plain-text and LaTeX export;
- deterministic save files that preserve source expressions separately from
  cached display output.
