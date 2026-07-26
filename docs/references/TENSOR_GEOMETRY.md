# Reference pack: tensor algebra and differential geometry

Source pack for the Phase 2 tensor and manifold layer (`docs/ROADMAP.md`). It
records where the algorithms come from, what each upstream may and may not be
used for, and what the published complexity means for a device with a 5–6 MB
binary budget and no virtual memory.

Every file path, size, symbol and license quoted here was read from the named
release, not from documentation about it. Facts that were not verified are
marked as such.

## How each upstream may be used

Project licensing is **not settled**, and nothing in this pack should be read as
assuming otherwise. Two facts from the current tree:

- `README.md` records that the selected upstream references include GPL-3
  software, and that "the final project license and retained notices will be
  fixed before any upstream code is copied".
- The application does not link Giac today. `docs/ARCHITECTURE.md` plans a
  size-trimmed native Giac backend; that work has not landed, so no GPL-3
  obligation has attached to the binary yet.

Reference-only is therefore load-bearing in two directions at once. It is an
engineering judgement — see the assessment of `xperm.c` below — and it is also
what keeps the licensing question open, because reading a source imposes nothing
while copying one would force the decision `README.md` defers. Neither
justification depends on the other, and neither should be weakened without the
licensing question being answered first.

| Upstream | Role | Vendored? |
| --- | --- | --- |
| xAct / xPerm | Canonicalization algorithm reference | No |
| Cadabra 2 | Second reading of the same algorithm, C-callable API shape | No |
| SymPy | Permissive reference implementation and host oracle | No |
| EinsteinPy | Secondary host oracle | No |

No third-party tensor code is vendored into this repository. The native core is
written from the published papers, with the implementations below used only to
read, to compare against, and to generate golden values on the host. Nothing in
this pack ships to the calculator.

## Canonicalization: the algorithm lineage

Index canonicalization is the one genuinely hard algorithm in Phase 2. Deciding
whether two tensor monomials are equal after renaming dummy indices and applying
declared slot symmetries is a double-coset problem in a permutation group.

Primary sources, in dependency order:

1. G. Butler, *Fundamental Algorithms for Permutation Groups*, Lecture Notes in
   Computer Science 559, Springer (1991). Base and strong generating sets,
   Schreier–Sims.
2. R. Portugal, "Algorithmic simplification of tensor expressions",
   *J. Phys. A: Math. Gen.* **32** (1999) 7779–7789.
3. R. Portugal, B. F. Svaiter, "Group-theoretic Approach for Symbolic Tensor
   Manipulation: I. Free Indices", arXiv:math-ph/0107031.
4. L. R. U. Manssur, R. Portugal, "Group-theoretic Approach for Symbolic Tensor
   Manipulation: II. Dummy Indices", arXiv:math-ph/0107032.
5. J. M. Martín-García, "xPerm: fast index canonicalization for tensor computer
   algebra", *Comput. Phys. Commun.* **179** (2008) 597–603, arXiv:0803.0862.
6. B. E. Niehoff, "Faster Tensor Canonicalization", *Comput. Phys. Commun.*
   **228** (2018) 123, arXiv:1702.08114. Improves the common totally
   (anti)symmetric cases that the base algorithm handles poorly.

Items 1–4 are the algorithm. Items 5 and 6 are the engineering that makes it
fast. This ordering matters: the native core only needs 1–4 to be correct, and
can adopt 5–6 later as an optimization without changing observable behaviour.

## xAct / xPerm — algorithm reference only

- Release: xAct 1.3.0, dated 29 December 2025.
- Archive: `https://xact.es/download/xAct_1.3.0.tgz`
- Archive size: 15,613,945 bytes.
- Archive SHA-256: `7a6c5f600868a3922668b020a15c0692f76574ff2a559808c62d460cef1b07be`
- File of interest: `xAct/xPerm/mathlink/xperm.c`
- File size: 98,905 bytes, 3,181 lines, mtime 2014-09-26.
- Copyright: José M. Martín-García, 2003–2011.
- License: the file header states "This is free software, distributed under the
  GNU GPL license" without naming a version. Treat the version as unresolved
  until it matters; it does not matter while the file is only read.

Properties measured by reading the file, all of which bear on a possible port:

- **No `#include` directives at all.** The translation unit expects its host to
  have already included the C library headers it needs. It is not a drop-in
  compilation unit.
- **Heap-driven.** 132 `malloc`, 13 `realloc` and 138 `free` calls. The header
  records this as deliberate: the 6 May 2006 entry notes "All arrays declared
  dynamically to avoid stack limits", crediting Kasper Peeters. A device port
  would need every one of these redirected to a bounded arena.
- **Log output is mostly compiled out.** The bulk of the 140 `printf` and 30
  `fprintf` calls sit behind `#ifdef VERBOSE_LISTS` and `#ifdef VERBOSE_SCHREIER`,
  off by default.
- **But the printing helpers are unconditional definitions.** `print_perm`,
  `print_list`, `print_array`, `print_array_perm` and the `fprint_*` family are
  defined outside the guards, and the `fprint_*` ones take a `FILE *`. With
  verbosity off they are unreachable, so `-ffunction-sections` plus
  `-Wl,--gc-sections` should drop them, but this is an assumption that must be
  checked against a symbol report rather than believed. Phase 0 already found
  that two `snprintf` calls pulled 12.7 KB of newlib float formatting into a
  53.8 KB binary, so an accidental `stdio` edge here is a real and precedented
  risk, not a hypothetical one.
- Entry points relevant to a reimplementation: `canonical_perm`,
  `canonical_perm_ext`, `schreier_sims`, `schreier_vector`, `trace_schreier`,
  `perm_member`, `stabilizer`, `one_orbit`, `all_orbits`.

Why it is read and not ported: the file is 3,181 lines shaped around a
Mathematica MathLink caller, allocates on every internal step, and carries no
test suite of its own. Reimplementing items 1–4 above against our own arena and
our own tests is more predictable than adapting it, and the paper describes the
algorithm completely enough that the source is a cross-check rather than a
requirement.

## Cadabra 2 — second reading

- Repository: `https://github.com/kpeeters/cadabra2`
- Tag: `2.5.14`, published 2025-07-31.
- Tag object SHA: `a4ab1c93912f1724f2f3ef26e7deba5a1b694af6`
- License: GPL-3.0-or-later (verified in the file header).
- Files: `core/modules/xperm_new.cc`, `core/modules/xperm_new.h`; caller in
  `core/algorithms/canonicalise.cc`.

Cadabra carries a maintained C++ descendant of `xperm.c` by Kasper Peeters. Its
header exposes a plain C-callable surface:

```c
void canonical_perm(int *perm, int SGSQ, int *base, int bl, int *GS,
                    int m, int n, int *freeps, int fl, int *dummyps, int dl,
                    int ob, int metricQ, int *cperm);

void canonical_perm_ext(int *perm, int n, int SGSQ, int *base, int bl,
                        int *GS, int m, int *frees, int fl,
                        int *vds, int vdsl, int *dummies, int dl, int *mQ,
                        int *vrs, int vrsl, int *repes, int rl, int *cperm);
```

Value to this project: it shows which parts of the original survived twenty
years of use, and its flat `int *` interface is a good model for the native
signature — no allocation in the contract, caller supplies `cperm`. Read for
that shape; do not copy the body.

## SymPy — permissive reference implementation and host oracle

- Repository: `https://github.com/sympy/sympy`
- Tag: `1.14.0`, published 2025-04-27.
- Tag object SHA: `fe935ceb303891d1f8bea4c03b19fd9ec9464b02`
- License: 3-clause BSD (verified by reading `LICENSE` at that tag; the GitHub
  API classifier reports `NOASSERTION` only because the file carries a custom
  author list ahead of the standard clauses).

Files, with line counts read from the installed 1.14.0:

| File | Lines | Contents |
| --- | --- | --- |
| `sympy/combinatorics/tensor_can.py` | 1,189 | Butler–Portugal canonicalization |
| `sympy/combinatorics/perm_groups.py` | 5,459 | Schreier–Sims, orbits, transversals |
| `sympy/tensor/tensor.py` | 5,265 | Abstract index tensors, `canon_bp` |
| `sympy/diffgeom/diffgeom.py` | 2,270 | Manifolds, charts, curvature helpers |

Verified public API of `tensor_can`: `canonicalize`, `double_coset_can_rep`,
`canonical_free`, `riemann_bsgs`, `get_symmetric_group_sgs`, `dummy_sgs`,
`bsgs_direct_product`, `get_transversals`, `get_minimal_bsgs`, `tensor_gens`,
`perm_af_direct_product`, `transversal2coset`.

This is the single most useful item in the pack. It is an independent
implementation of the same algorithm under a permissive license, and its own
docstring cites exactly references 2, 3, 4 above plus `xperm.c`. Concretely,
`riemann_bsgs` returns the base and strong generating set for the Riemann
symmetries:

```
([0, 2], [Permutation(0, 1)(4, 5), Permutation(2, 3)(4, 5),
          Permutation(5)(0, 2)(1, 3)])
```

which is a directly checkable fixture for the native BSGS construction —
antisymmetry in the first pair, antisymmetry in the second pair, and pair
exchange. A native implementation that reproduces this triple for the Riemann
slot group is very likely correct.

## Conventions this project adopts

Fixed here so that the native core, the corpus, and the oracles cannot disagree
silently:

- Index slots are ordered left to right, counting from zero.
- A dummy pair occupies two slots and is stored in consecutive positions in the
  canonical form, matching the convention `tensor_can` documents.
- Permutations are stored in images notation: `p[i]` is the image of slot `i`.
  This is what both `xperm.c` and `tensor_can` use, so a disagreement with
  either oracle is a real bug rather than a notation mismatch.
- Slot symmetry (how a tensor behaves under permuting its own slots) is
  declared per tensor head and is separate from dummy-index symmetry (freedom to
  rename contracted pairs). Conflating the two is the standard first bug.
- Metric-contracted dummies may be raised or lowered freely; this is the
  `metricQ` flag in the xPerm interface and must be explicit in ours.

## Complexity and memory on Ndless

The published complexity is the reason to be careful. Butler–Portugal
double-coset canonicalization is worst-case exponential in the number of index
slots. It is fast in practice because physics monomials are small and their
symmetry groups are highly structured, not because the bound is good.

What that means concretely for a device with no virtual memory:

- Cost is driven by the slot count `n`, not the manifold dimension. A rank-4
  Riemann monomial with all indices contracted has `n = 8`; a product of three
  such tensors has `n = 24`. Dimension 4 never enters the permutation problem.
- Working storage is dominated by the strong generating set: `m` generators of
  `n` ints each, so `4 m n` bytes. At `n = 24` and a pessimistic `m = 200` that
  is 19.2 KB — small, but it must come from a bounded arena, because the
  quantity is data-dependent and an adversarial declaration can inflate `m`.
- The correct device posture is therefore a fixed arena plus a hard cap on both
  `n` and `m`, returning a clean resource-limit error instead of attempting the
  computation. `docs/SCIENTIFIC_SCOPE.md` already requires explicit error values
  for resource limits; canonicalization is the first place that requirement
  earns its keep.
- Do not port the upstream allocation pattern. 132 `malloc` sites in `xperm.c`
  is roughly 132 opportunities to fragment a heap that cannot be compacted.

None of these numbers are measured on hardware. They are budgets to design
against and to check once the core exists.

## Out of scope for this pack

Abstract-index canonicalization is **not** in the first milestone. The Phase 3
curvature MVP in `docs/agent-tasks/GR_CURVATURE.md` works entirely with concrete
components on a coordinate chart, where two expressions are compared by
simplifying scalars, not by canonicalizing index structure. This pack exists so
that the canonicalization work, when it starts, begins from the papers and the
BSGS fixtures above rather than from a blank page.

Also out of scope here: differential forms, Lie derivatives, torsion, and frame
or tetrad formalisms. They are listed in `docs/SCIENTIFIC_SCOPE.md` §3 and will
need their own references.
