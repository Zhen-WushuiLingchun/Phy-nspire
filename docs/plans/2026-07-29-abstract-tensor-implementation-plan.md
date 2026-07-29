# Abstract Tensor Canonicalization Implementation Plan

> **For Codex:** REQUIRED SKILLS: use Code execution and verification workflows; implement task-by-task in this checkout without subagents, as requested by the user.

**Goal:** Build dynamic exact linear algebra, a resource-bounded abstract-index tensor kernel with Butler–Portugal and Young canonicalization, and a validated bridge to components and multi-chart differential geometry.

**Architecture:** Preserve the existing component GR backend while adding a separate abstract tensor representation above the typed IR.  Both layers share the exact CAS; an explicit bridge expands abstract objects into dynamic dense or sparse components only after a basis or chart is supplied.

**Tech Stack:** C11, CMake/CTest, typed Phy IR, native exact CAS, bounded `phy_alloc` telemetry, host oracle generation with official xPerm/SymPy, ARM Ndless toolchain.

---

### Task 1: Dynamic exact vectors and matrices

**Files:**
- Create: `include/phy/linear.h`
- Create: `src/linear/matrix.c`
- Create: `src/linear/elimination.c`
- Create: `src/linear/linear_internal.h`
- Create: `tests/test_linear.c`
- Modify: `CMakeLists.txt`

**Steps:**
1. Write failing tests for zero-sized rejection, rectangular construction,
   dimensions above four, exact get/set, addition, multiplication, transpose,
   determinant, inverse, fraction-free RREF, rank, null space, and exact solve.
2. Add opaque `phy_matrix`/`phy_vector` APIs with runtime rows/columns and
   caller-configurable entry/byte/step ceilings.
3. Implement overflow-checked shape allocation through `phy_alloc`.
4. Implement entry operations with `phy_cas_*`; keep partial outputs private
   until an operation succeeds.
5. Implement fraction-free elimination with deterministic pivot selection.
6. Run:
   `cmake --build build --config Debug --target test_linear &&
    ctest --test-dir build -C Debug -R test_linear --output-on-failure`.
7. Run the full host suite and commit `feat(linear): add dynamic exact matrices`.

### Task 2: Abstract index spaces and tensor heads

**Files:**
- Create: `include/phy/abstract_tensor.h`
- Create: `src/abstract/index.c`
- Create: `src/abstract/head.c`
- Create: `src/abstract/abstract_internal.h`
- Create: `tests/test_abstract_tensor.c`
- Modify: `CMakeLists.txt`

**Steps:**
1. Write failing tests for multiple spaces, symbolic/known dimensions,
   variance, metric policies, rank above four, and invalid slot-space matches.
2. Implement context-owned `phy_index_space` and
   `phy_abstract_tensor_head` registries.
3. Store runtime slot descriptors and signed symmetry generators without
   enumerating the full group.
4. Add deterministic serialization based on names and structure rather than
   context-local IDs.
5. Run `test_abstract_tensor`, the IR suite, and the full host suite.
6. Commit `feat(tensor): add abstract index spaces and tensor heads`.

### Task 3: Monomials, index census, and validation

**Files:**
- Create: `src/abstract/monomial.c`
- Create: `src/abstract/census.c`
- Modify: `include/phy/abstract_tensor.h`
- Modify: `src/abstract/abstract_internal.h`
- Modify: `tests/test_abstract_tensor.c`

**Steps:**
1. Write failing tests for free indices, valid dummies, repeated free indices,
   same-variance dummies, typed metric dummies, and incompatible term
   signatures.
2. Implement coefficient-plus-factor monomials with runtime factor/slot arrays.
3. Derive free/dummy classification from the complete monomial; never trust a
   caller-provided role flag.
4. Canonically sort only factors declared commuting; preserve noncommuting
   order.
5. Run focused and full host tests.
6. Commit `feat(tensor): validate typed abstract tensor monomials`.

### Task 4: Signed permutations and bounded BSGS

**Files:**
- Create: `include/phy/permutation.h`
- Create: `src/permutation/perm.c`
- Create: `src/permutation/bsgs.c`
- Create: `src/permutation/orbit.c`
- Create: `src/permutation/permutation_internal.h`
- Create: `tests/test_permutation.c`
- Modify: `CMakeLists.txt`

**Steps:**
1. Write failing fixtures for identity, composition, inverse, sign extension,
   orbit, stabilizer, membership, and the Riemann BSGS.
2. Implement image-notation permutations with a separate sign coordinate.
3. Implement deterministic Schreier–Sims using caller-owned bounded scratch.
4. Return typed resource errors for slot, generator, orbit, and step limits.
5. Cross-check the Riemann generators and randomized small groups against
   SymPy host fixtures.
6. Run sanitizer and full host suites.
7. Commit `feat(tensor): add bounded signed permutation groups`.

### Task 5: Butler–Portugal monoterm canonicalizer

> Status note (2026-07-29): this section is the intended implementation plan,
> not a completed-capability record. The repository currently has bounded
> signed slot-group traversal plus per-candidate dummy normalization. It still
> lacks an explicit dummy group \(D\), an explicit \(DgS\) double-coset
> search, and the committed xPerm/SymPy fixture files listed below.

**Files:**
- Create: `src/abstract/canonical.c`
- Create: `tests/test_tensor_canonical.c`
- Create: `tests/oracle/generate_tensor_can.py`
- Create: `tests/fixtures/tensor_can_cases.txt`
- Modify: `include/phy/abstract_tensor.h`
- Modify: `CMakeLists.txt`

**Steps:**
1. Generate committed oracle cases for symmetric/antisymmetric tensors,
   metrics of each symmetry, multiple dummy types, identical factors, and
   Riemann monomials.
2. Write failing tests for construction-order independence, alpha-renaming,
   factor-order independence, free-index preservation, and sign-zero cases.
3. Encode slot and dummy groups as a double-coset problem.
4. Implement bounded Butler–Portugal search using Task 4 BSGS/transversals.
5. Rebuild canonical factors and deterministic dummy names.
6. Compare every committed case against both expected serialization and sign.
7. Run host, sanitizer, and ARM symbol/size checks.
8. Commit `feat(tensor): canonicalize abstract indexed monomials`.

### Task 6: Abstract sums and Young multi-term reduction

> Status note (2026-07-29): normalized Young projection of one selected factor
> and exact collection of the generated terms have landed. General expression
> algebra, Garnir/relation-basis reduction, and automatic first-Bianchi
> reduction have not.

**Files:**
- Create: `src/abstract/expression.c`
- Create: `src/abstract/young.c`
- Create: `tests/test_young.c`
- Modify: `include/phy/abstract_tensor.h`
- Modify: `CMakeLists.txt`

**Steps:**
1. Write failing tests for collection of canonical monomials, first Bianchi,
   symmetric/antisymmetric projection, Jacobi-type cyclic relations, and
   configured resource exhaustion.
2. Implement tensor expressions as canonical monomial maps with exact scalar
   coefficients.
3. Implement tableaux validation, row symmetrizers, column antisymmetrizers,
   and exact normalization where defined.
4. Reduce multi-term relations through deterministic exact row-echelon bases.
5. Confirm monoterm-only canonicalization does not falsely prove Bianchi before
   Young reduction.
6. Run focused, full, and sanitizer suites.
7. Commit `feat(tensor): add Young multi-term canonicalization`.

### Task 7: Dynamic component tensors and bridge

**Status: frontend slice complete, migration partial.** Runtime-rank sparse
components, bases, the bounded one-monomial bridge, evaluator ownership,
`Component`/`Dimensions`, command-palette entries and reader-facing
`ComponentValue` are live. Dense/sparse policy selection, the legacy
compatibility facade, expression-wide `ComponentValue`, and
independent-component output iteration remain pending.

**Files:**
- Create: `include/phy/component_tensor.h`
- Create: `src/component/basis.c`
- Create: `src/component/component.c`
- Create: `src/component/bridge.c`
- Create: `tests/test_component_bridge.c`
- Modify: `include/phy/tensor.h`
- Modify: `src/tensor/*.c`
- Modify: `CMakeLists.txt`

**Steps:**
1. Write tests for runtime rank/dimension above four, heterogeneous slot
   dimensions, sparse storage, dense storage, and clean limit failures.
2. Implement runtime shapes and dense/sparse storage chosen by a documented
   threshold, with no fixed `PHY_TENSOR_MAX_RANK` semantic limit.
3. Adapt legacy `phy_tensor` calls through a compatibility facade.
4. Implement `ComponentValue` expansion, dummy contraction, independent
   component iteration, and basis validation.
5. Re-run all existing tensor/GR/geometry tests unchanged.
6. Commit `feat(tensor): bridge abstract and dynamic component tensors`.

### Task 8: Atlas transition maps and pullback/pushforward

**Status: native library and evaluator surface complete.** The implementation lives in
`include/phy/map.h` and `src/component/{map,atlas}.c`, beside the dynamic
component bases it transforms. `CoordinateMap`, verified `BasisTransition`,
Jacobian/scalar/covector/vector operations, sparse tensor pullback and bounded
atlas creation/edge verification/pullback are reachable from notebook cells.

**Files:**
- Create: `src/geom/transition.c`
- Create: `src/geom/pullback.c`
- Modify: `include/phy/geom.h`
- Modify: `src/geom/geom_internal.h`
- Create: `tests/test_atlas.c`
- Modify: `CMakeLists.txt`

**Steps:**
1. Write failing identity, polar/Cartesian, inverse-composition, invalid-symbol,
   singular-Jacobian, form-pullback, vector-pushforward, and tensor-change
   fixtures.
2. Implement typed transition maps with source/target chart ownership and exact
   coordinate lists.
3. Build exact Jacobians through `phy_cas_diff` and Task 1 matrices.
4. Implement pullback, pushforward, and tensor basis transformation using the
   shared component bridge.
5. Keep unregistered chart mixing a typed error.
6. Run geometry, GR, full host, and sanitizer suites.
7. Commit `feat(geom): add transition maps and tensor pullbacks`.

### Task 9: Unified evaluator and physics migration

**Status: frontend objects complete; physics migration pending.** Dynamic
vectors/matrices, abstract heads/expressions, component bases/tensors, maps,
transitions and atlases have value kinds, ownership, display, source commands,
palette entries and evaluator tests. GR, Dirac, colour, Lie and Yang--Mills
still require staged parity migration from their legacy/local index models.

**Files:**
- Modify: `include/phy/eval.h`
- Modify: `src/eval/eval_internal.h`
- Modify: `src/eval/dispatch.c`
- Modify: `src/lorentz/*.c`
- Modify: `src/dirac/*.c`
- Modify: `src/lie/*.c`
- Modify: `src/color/*.c`
- Modify: `src/gr/*.c`
- Modify: `src/yang_mills/*.c`
- Modify: `tests/test_eval.c`
- Modify: relevant physics tests

**Steps:**
1. Add parser/evaluator tests for the public heads listed in the design.
2. Add runtime value kinds and lifecycle tracking for matrices, abstract
   contexts, heads, expressions, bases, and maps.
3. Route Lorentz, Dirac, colour, Lie, GR, and Yang–Mills index construction
   through the shared index spaces and census.
4. Delete local dummy canonicalizers only after parity tests pass.
5. Add notebook command-palette entries and MathTree rendering for indexed
   canonical output.
6. Run all host tests, notebook round trips, sanitizer, ARM build, symbol
   report, binary-size report, and hardware smoke.
7. Commit `feat(physics): unify indexed algebra on abstract tensors`.

### Task 10: Documentation and acceptance corpus

**Files:**
- Modify: `docs/TENSOR.md`
- Modify: `docs/GEOMETRY.md`
- Modify: `docs/SCIENTIFIC_SCOPE.md`
- Create: `docs/ABSTRACT_TENSOR.md`
- Create: `examples/abstract-tensor-tour.tns`
- Modify: `README.md`

**Steps:**
1. Replace obsolete rank/dimension-four scope statements only after the dynamic
   APIs pass.
2. Document monoterm versus multi-term guarantees and every configured limit.
3. Add executable examples for matrices, abstract vectors, Riemann
   canonicalization, Bianchi, coordinate changes, pullbacks, GR, and QFT.
4. Build the calculator program and notebook, upload with the fixed CLI, verify
   byte-identical transport, and obtain calculator-side open/run evidence.
5. Commit `docs: publish abstract tensor and geometry acceptance guide`.
