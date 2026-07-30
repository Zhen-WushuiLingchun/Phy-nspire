# Notebook shell and two-dimensional layout

## Current artifact

The production entry point opens a new, empty 320 × 240 notebook rather than
the Phase 0 hardware diagnostic or a seeded demo. `FILE`/`MENU` exposes New,
Save, Open, and Run all cells. New documents are `Untitled`; saved documents
default to `/documents/phy-nspire/notebooks` and use the versioned,
CRC32-protected `PHYNB001` codec. Save is atomic and Open replaces the active
workspace only after the complete file validates.

## Cell model

`include/phy/notebook.h` exposes a bounded model with 192 cell slots and
fixed source buffers. A source cell stores both reader-facing source and, after
a successful parse, the backend-neutral serialized IR produced from that exact
source. Evaluation inserts or updates a separate output/error cell, so a failed
calculation cannot replace or destroy its source.

Implemented cell kinds:

- Markdown heading/body;
- symbolic input;
- typed-IR symbolic output;
- typed physics-object output, shown as a structured constructor or
  abstract/component-index signature;
- typed error output.

### Cells share state

The notebook owns one evaluator environment, documented in
[`EVALUATOR.md`](EVALUATOR.md), and a cell is evaluated against it. A cell can
bind a name — `M = Manifold[{x,y}, Euclidean]` — that later cells read. Three
consequences are visible in the shell:

- running a cell marks every result *after* it stale, because a cell that binds
  a name changes what the cells below it mean;
- handles such as manifolds, groups, bases, atlases, and curvature bundles keep
  their typed constructor in the output; tensor heads and component tensors
  show abstract/component-index signatures. QFT systems additionally display
  their SU(N), typed spaces, tensor heads, and currently exact component
  tables. All of them use the same two-dimensional renderer as scalar CAS
  output; old descriptor-only documents remain readable;
- the document codec stores cells, never objects, so a reopened notebook starts
  with an empty environment. `FILE` > `Run all cells` replays it in order, which
  is `phy_notebook_evaluate_all`.

An evaluation heavy enough to fill the permanent interning IR — a Kerr
curvature attempt is the measured example — used to wedge the whole session:
every later cell failed on its first interned node until the document was
reopened. The notebook now probes for that state after any failed evaluation
(one canary intern), rebuilds its IR/CAS/environment with every cell's
expression re-read from its stored text, and quarantines the offending input
so `Run all cells` converges instead of refilling the fresh context. Bindings
do not survive the rebuild, so every output goes stale — which is exactly
what stale is for. Running the quarantined cell directly retries it.

Every executable input has an independent `RUN` badge in its upper-right
corner. The badge has a tested hit rectangle: clicking the card body only
selects and edits it, while clicking `RUN` reevaluates that one input. The
footer has functional `+MD` and `+Math` insertion buttons. Source edits mark an
existing output `stale` until the cell is rerun. Selection automatically
scrolls the bounded viewport.

`ENTER` runs the selected input; left/right move the edit cursor; `DEL`
backspaces; `TAB` switches between Markdown heading and body. The first `ESC`
leaves edit mode, and a second `ESC` exits through the normal display-restore
path.

A result wider than its card first steps down through smaller formula sizes;
what still does not fit pans: when such an output is selected, left/right
scroll the formula horizontally and `<`/`>` corner markers show which side has
hidden content. Up/down still change the selection, so a wide result never
traps navigation.

A Markdown body that mixes prose with inline math flows like a paper
paragraph: words and `$...$` formulas are unbreakable tokens laid left to
right, wrapping at the card edge, and each line takes the height its tallest
token needs, so a fraction or an integral opens its line up instead of
colliding with neighbors. The card grows to hold the wrapped lines up to a
viewport-sized cap; content past the cap is dropped and marked with a `+` in
the card's bottom-right corner — the same marker a pure-prose body shows past
its twelve-line cap. A body that is entirely one `$$...$$` display formula
instead shrinks and then pans like a wide output card.

Inside edit mode, `MENU` opens a context-sensitive insertion palette. Math
cells expose only reader commands and functions already accepted by the
current evaluator. They are grouped as Algebra, Functions, Calculus/Syntax,
Linear Algebra, Tensor/Indices, Differential Geometry, Lie/Yang-Mills, General
Relativity, QFT/Colour, and Queries/State. `test_palette` checks that all 109
registered evaluator heads and all 18 supported source commands occur in at
least one insertion snippet, and parses every snippet.
Markdown bodies expose nMarkdown-backed LaTeX templates for layout, calculus,
Greek letters, accents/styles, and matrices. Left/right changes category and
up/down selects through a seven-row viewport; `^` and `v` show that more rows
exist. Enter or a touch on a visible row inserts the template with the cursor
in its first argument slot. Keyboard selection and pointer hit testing share
the same scroll window. Outside edit mode, `MENU` remains the file menu.

The touchpad is relative. A new finger contact establishes a motion origin and
does not teleport the cursor; movement continues from the last screen
position, retains fractional pixel deltas, and clears overshoot at screen
edges.

## Two-dimensional math

`src/render/math_layout.c` measures and draws the typed IR directly. It does
not serialize a result to LaTeX and parse it again. The first implementation is
allocation-free, clips through the common RGB565 primitives, saturates layout
dimensions, and bounds recursion at 64 levels.

Implemented layout forms:

- integers, symbols, and typed errors;
- exact rationals with a horizontal fraction bar;
- powers and upper/lower indices;
- sums, commutative and noncommutative products, wedge products, and equations;
- functions, tensors, operators, and derivative nodes.

CAS results still use the direct typed-IR renderer, so exact types are never
flattened into a string and reparsed. Markdown bodies additionally use the
pinned nMarkdown math subsystem through `include/phy/formula.h` and
`src/render/formula_bridge.cpp`. The bridge wraps the existing RGB565 surface
without a second framebuffer and owns one long-lived `TextSystem` and
`MathSystem`.

Supported Markdown-cell math delimiters:

- inline: `$...$` and `\(...\)`;
- display: `$$...$$` and `\[...\]`.

Edit mode always shows the raw source. Leaving edit mode renders Latin Modern
Math with OpenType MATH metrics. The upstream language supplies at least 580
commands and eleven matrix/alignment environments; the exact audited boundary
is recorded in
[`plans/2026-07-27-nmarkdown-adaptation.md`](plans/2026-07-27-nmarkdown-adaptation.md).

Calculator input aliases make the delimiters and structural characters
reachable without an on-screen keyboard: `Ctrl+.` types `$`, `Ctrl+/` types
`\`, and the Shift/Ctrl variants of the parenthesis keys type brackets and
braces. Both held modifiers and tap-then-key modifiers are accepted.

## Comprehensive CAS tour

[`examples/phy-nspire-cas-tour.tns`](../examples/phy-nspire-cas-tour.tns) is a
generated, executable notebook rather than a screenshot fixture. Its 192
source cards contain sixteen Markdown/LaTeX explanations and 176 Math inputs
covering the implemented scalar CAS and calculus, exact dynamic linear
algebra, abstract/component bridging, verified chart transitions and atlases,
generic component tensors, manifolds, forms and Hodge operations, coordinate
GR, Lie algebra and Yang--Mills, phi4 graph/renormalization operations,
Dirac/Mandelstam/SU(N) colour, the shared QFT abstract/component view, and
`MemoryStatus[]`.

`phy-make-cas-tour` first evaluates every input in a validation copy, serializes
that fully evaluated notebook, deserializes it into a fresh empty environment,
and replays all cells. It then writes a separately round-tripped source-only
document. This keeps the CX II's `FILE > Open` path free of eager cached-tree
reconstruction while preserving full generation-time CAS coverage. Running all
176 Math cells top-to-bottom grows the document to 368 cards. The 400-card
bound leaves 32 slots for small reader experiments; start a new notebook for
extended work rather than appending a long calculation to the acceptance tour.

## Verification

- `test_notebook`: exact checks over results, editing, insertion, stale
  results, source/IR agreement, bounds, memory return, selection, `RUN` hit
  testing, Markdown LaTeX integration, 2D metrics, and deterministic pixels;
- `test_eval`: exact checks over the stateful evaluator, including the notebook
  integration — state flowing between cells, structured object output, forward
  staleness, `ClearAll[]`, and save/reopen without persisting live objects;
- `test_palette`: 29,408 checks over every category, entry, snippet, cursor
  bound, registry-completeness rule, and scrolling window, and over every CAS
  snippet actually parsing;
- `test_formula`: 103 checks over lifecycle, metrics, matrices, RGB565 drawing,
  and malformed-formula recovery;
- `test_source`: 443 checks over the permanent reader-facing grammar, the
  command registry, assignment, and reserved-head canonicalization;
- `test_pointer`: 29 checks over relative contact/motion behavior;
- `test_modifier`: 8 checks over tapped and held Shift/Ctrl behavior;
- `tests/fixtures/notebook_frame.digest`: bit-exact 320 × 240 host fixture;
- last strict Windows baseline: 45/45; current WSL GCC, ASan/leak, and UBSan
  suites: 47/47 each; 337,894 explicit checks;
- Ndless r2022 ARM build: 1,224,221 bytes. The evaluator probe retains 17/17
  public APIs behind the complete physics stack and imports no forbidden
  float/libm/soft-float helper.

Directional keys on the CX II touchpad are filtered at the platform boundary:
while one is down, the overlapping touch contact/click report is suppressed.
This prevents both notebook selection and text cursors from jumping to the
on-screen pointer. Shift and Ctrl support both held chords and the calculator's
usual tap-then-key one-shot behavior.

The 2D renderer applies precedence-aware grouping. Compound power bases and
sum factors therefore remain visibly parenthesized: the IR `(^ (+ m x) 3)` is
shown as `(m+x)^3`, never as the algebraically different `m+x^3`.

Physical-device acceptance of this shell is tracked separately from the
already-complete seven-case CAS smoke.

## Explicitly not implemented yet

- arbitrary Markdown parsing, inline emphasis, links, and code blocks;
- two-dimensional visual formula editing and direct typed-IR-to-nMarkdown
  layout;
- LaTeX export and a larger optional CJK asset stack;
- cell deletion and reordering;
- a bindings inspector: the environment is queryable through
  `phy_env_binding`, but the shell does not show it;
- tables, matrices, plots, and diagram cells.

These remain Phase 1 work. The current artifact establishes their cell,
evaluation, hit-testing, and two-dimensional rendering boundaries.
