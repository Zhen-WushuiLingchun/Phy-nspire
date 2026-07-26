# NspirePhysics feasibility pass

Date: 2026-07-26

This note records measured artifacts and upstream capabilities before an
architecture is selected. It is not yet the implementation design.

## Device budget

The connected TI-Nspire CX II CAS reported:

| Resource | Total | Currently free |
| --- | ---: | ---: |
| Flash storage | 96,862,208 bytes (92.38 MiB) | 91,090,944 bytes (86.87 MiB) |
| RAM | 35,650,680 bytes (34.00 MiB) | 31,805,820 bytes (30.33 MiB) |

The project budget is much tighter than the available flash: the desired
installed application size is 5–6 MB.

## Measured candidate components

| Component | Measured size | Role | License / provenance |
| --- | ---: | --- | --- |
| `luagiac.luax.tns` | 4,138,210 bytes | Giac CAS exposed to TI Lua through `luagiac.caseval` | GPL-3, Bernard Parisse distribution |
| `khicas.tns` | 4,138,955 bytes | Existing native Giac/KhiCAS shell | GPL-3, Bernard Parisse distribution |
| `khicaslua.tns` | 10,895 bytes | Existing TI Lua two-dimensional input UI | GPL-3; source `khicas.lua` is included |
| `nmarkdown.tns` v0.2.0 | 1,269,572 bytes | Native Markdown, mathematical LaTeX, fonts, touchpad and RGB565 UI | GPL-3, GitHub release commit `936b048` |

`luagiac` plus the released nMarkdown binary totals 5,407,782 bytes
(5.16 MiB). This is below 6 MiB, but leaves only 883,674 bytes below a strict
6 MiB ceiling for the physics layer, integration code, and resources.

The nMarkdown v0.2.0 release was downloaded from its GitHub release and recorded
with SHA-256:

`55C71C31FE432382A44A67128EB8DDDD05E4EA8983BD7EF9FAD9795B9DB2C933`

The matching source tag is pinned read-only under
`research/upstream/nMarkdown` at commit
`936b04854fc0838de9986b4bfee66a4da9db6166`.

## What nMarkdown contributes

nMarkdown is more than a text viewer. Its portable C++ core already provides:

- a 320 × 240 RGB565 renderer and CX II-safe display adapter;
- direct touchpad motion, clicks, swipes, key repeat, menus, and dialogs;
- CommonMark parsing through MD4C;
- bounded mathematical LaTeX with fractions, radicals, scripts, matrices,
  aligned equations, stretchy delimiters, and a native math font;
- FreeType and HarfBuzz text rendering;
- a desktop replay harness and deterministic framebuffer tests.

Its core is deliberately separated from the Ndless platform layer, making it a
credible UI/rendering foundation. Reuse requires a GPL-3-compatible project.

Full CJK rendering is not free in the size budget. nMarkdown's optional
calculator-oriented Sarasa Fixed SC font is 6,105,504 bytes by itself. A strict
all-files 6 MB budget therefore cannot contain Giac, nMarkdown, and a full CJK
font simultaneously.

## CAS and physics-library portability

### Giac / KhiCAS: viable backend

KhiCAS proves that a useful general CAS, including GMP/MPFR/MPFI support, fits
in about 4.14 MB and runs on this exact calculator. The distributed Lua module
accepts Xcas commands through `luagiac.caseval`. This is the lowest-risk
general symbolic backend.

The existing `khicas.lua` also proves that TI Lua can provide pointer-aware
widgets and two-dimensional input. It is useful evidence and reusable GPL-3
code, but its shell/history layout is not the target UX.

### Mathematica packages: specifications, not drop-in libraries

The most relevant Wolfram Language ecosystems are:

- **xAct/xTensor/xCoba/xPerm** for abstract and component tensor algebra,
  differential geometry, perturbation theory, curvature invariants, and
  spinors;
- **FeynCalc** for Lorentz and Dirac algebra, contractions, traces, loop
  integrals, and amplitude manipulation;
- **FeynArts** for topology/diagram and amplitude generation;
- **FeynRules** and related model tooling for Lagrangians and interaction
  rules.

These packages depend deeply on the Wolfram Language evaluator, pattern
matcher, attributes, rule dispatch, and notebook/kernel runtime. There is no
Wolfram kernel for Ndless, so copying their package files is not a port.
FeynCalc's own FAQ explicitly says that porting its Mathematica-dependent
internal logic to another CAS would require substantial reimplementation.

They remain valuable as:

- feature and notation specifications;
- sources of compact regression examples;
- references for canonical forms and simplification semantics;
- GPL-compatible algorithms where a clean dependency boundary exists.

### xPerm: selective native reuse is plausible

xPerm's computationally demanding permutation-group and tensor-index
canonicalization routines include a GNU C implementation intended to be linked
from other computer algebra systems. A bounded port of this C core is a strong
candidate once the expression and index model is fixed.

### FORM: useful model, unsuitable as the first whole-engine port

FORM is an open C/C++ symbolic engine designed for high-energy physics,
noncommutative algebra, pattern transformations, and expressions larger than
RAM. Its batch/file-oriented architecture and broad runtime are a poor fit for
the first 320 × 240 interactive release. A FORM-like streaming term rewriter
or selected algorithms may become useful later; the whole system should not be
the first dependency.

### Heavy alternatives

Cadabra, SymPy, GiNaC/CLN, SymEngine, and full Python runtimes introduce
dependency, allocator, runtime, or binary-size costs that are not competitive
with the already working 4.14 MB Giac port for this target.

## Candidate architecture routes

### Route A: TI Lua notebook plus `luagiac` (fastest prototype)

- Keep Giac in `luagiac.luax.tns`.
- Build a new pointer-driven notebook UI in TI Lua.
- Implement tensor/QFT commands as a compact Lua/Giac domain layer.
- Use nMarkdown as a separate high-quality reader during the first release.

Advantages: smallest engineering risk, native TI widgets/input, immediate CAS.
Limit: Markdown/LaTeX is not initially in the same process as the notebook.

### Route B: native nMarkdown-derived notebook plus embedded Giac

- Fork the portable nMarkdown UI, text, math, and input layers.
- Replace the reader-only application shell with editable notebook cells.
- Link a size-trimmed Giac backend.
- Add compact tensor, GR, and QFT modules.

Advantages: best final UX and integrated Markdown/LaTeX.
Risks: difficult cross-build, tight 6 MB limit, and shared memory-pressure work.

### Route C: custom physics CAS and UI

- Build a compact expression DAG, exact arithmetic, rewrite engine, tensor
  canonicalizer, and renderer specifically for physics.

Advantages: maximum control and potentially the smallest final binary.
Risks: by far the largest correctness burden; general algebra, factorization,
calculus, assumptions, and numeric fallbacks would lag Giac for a long time.

## Architecture decision after review

The production architecture is Route B: an Ndless-native C/C++ application.
Route A is retained only as a source of behavioral comparisons for Giac and
two-dimensional input. TI Lua will not be part of the calculator runtime.

The native plan is:

1. Fork only the required nMarkdown platform, input, text, math, and layout
   components into a notebook-oriented UI.
2. Build a size-trimmed native Giac static backend using the proven KhiCAS
   target configuration.
3. Put all physics semantics behind a backend-neutral expression and command
   interface so that specialized algorithms can replace Giac operations
   incrementally.
4. Reuse xPerm's C core selectively for abstract-index canonicalization after
   the tensor representation is stable.
5. Profile on the real CX II from the first executable milestone. Native code
   removes interpreter overhead but does not make overclocking a requirement.

The first physics slice should stay deliberately small:

- typed tensor symbols and upper/lower indices;
- dummy-index renaming and Einstein contraction;
- declared symmetries;
- metric and inverse metric;
- component Christoffel, Riemann, Ricci, scalar curvature, and Einstein tensor;
- two-dimensional pretty output and LaTeX export.

Dirac traces, Lorentz contractions, Mandelstam substitutions, propagator and
vertex palettes, and simple Feynman diagrams should follow after the tensor
representation is stable.

## Primary sources

- nMarkdown: <https://github.com/KaraRyougi/nMarkdown>
- KhiCAS/Giac installer page: <https://wfourier.u-ga.fr/~parisse/install_en.html>
- xAct: <https://www.xact.es/>
- xPerm: <https://xact.es/xPerm/>
- FeynCalc porting FAQ:
  <https://feyncalc.github.io/FeynCalcBookDev/Extra/FrequentlyAskedQuestions.html>
- FORM: <https://github.com/form-dev/form>
