# Notebook shell and two-dimensional layout

## Current artifact

The production entry point opens a new, empty 320 × 240 notebook rather than
the Phase 0 hardware diagnostic or a seeded demo. `FILE`/`MENU` exposes New,
Save, Open, and Run all cells. New documents are `Untitled`; saved documents
default to `/documents/phy-nspire/notebooks` and use the versioned,
CRC32-protected `PHYNB001` codec. Save is atomic and Open replaces the active
workspace only after the complete file validates.

## Cell model

`include/phy/notebook.h` exposes a bounded model with twelve cell slots and
fixed source buffers. A source cell stores both reader-facing source and, after
a successful parse, the backend-neutral serialized IR produced from that exact
source. Evaluation inserts or updates a separate output/error cell, so a failed
calculation cannot replace or destroy its source.

Implemented cell kinds:

- Markdown heading/body;
- symbolic input;
- typed-IR symbolic output;
- typed physics-object output, shown as a descriptor line;
- typed error output.

### Cells share state

The notebook owns one evaluator environment, documented in
[`EVALUATOR.md`](EVALUATOR.md), and a cell is evaluated against it. A cell can
bind a name — `M = Manifold[{x,y}, Euclidean]` — that later cells read. Three
consequences are visible in the shell:

- running a cell marks every result *after* it stale, because a cell that binds
  a name changes what the cells below it mean;
- an output whose value is a manifold, a Lie group, or a curvature bundle has no
  expansion in the typed IR, so the card shows a descriptor line
  (`Manifold M dim 2 Riemannian +oriented (x,y)`) instead. Objects that do have
  an expansion — forms, algebra-valued forms, Lie elements, tensors up to rank
  two — are drawn by the ordinary typed-IR renderer;
- the document codec stores cells, never objects, so a reopened notebook starts
  with an empty environment. `FILE` > `Run all cells` replays it in order, which
  is `phy_notebook_evaluate_all`.

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

Inside edit mode, `MENU` opens a context-sensitive insertion palette. Math
cells expose only reader commands and functions already accepted by the
current evaluator — `test_palette` parses every one of them — grouped as
Algebra, Functions, Calculus/Syntax, Tensor/Indices, Differential Geometry, and
Lie/QFT Objects.
Markdown bodies expose nMarkdown-backed LaTeX templates for layout, calculus,
Greek letters, accents/styles, and matrices. Left/right changes category,
up/down selects, and Enter or a touch on a row inserts the template with the
cursor in its first argument slot. Outside edit mode, `MENU` remains the file
menu.

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

## Verification

- `test_notebook`: 162 checks over exact results, editing, insertion, stale
  results, source/IR agreement, bounds, memory return, selection, `RUN` hit
  testing, Markdown LaTeX integration, 2D metrics, and deterministic pixels;
- `test_eval`: 918 checks over the stateful evaluator, including the notebook
  integration — state flowing between cells, descriptor outputs, forward
  staleness, and a save/reopen that restores descriptors but not objects;
- `test_palette`: 561 checks over every category, entry, snippet, and cursor
  bound, and over every CAS snippet actually parsing;
- `test_formula`: 33 checks over lifecycle, metrics, matrices, RGB565 drawing,
  and malformed-formula recovery;
- `test_source`: 217 checks over the permanent reader-facing grammar, the
  command registry, assignment, and reserved-head canonicalization;
- `test_pointer`: 29 checks over relative contact/motion behavior;
- `test_modifier`: 8 checks over tapped and held Shift/Ctrl behavior;
- `tests/fixtures/notebook_frame.digest`: bit-exact 320 × 240 host fixture;
- strict Windows suite: 28/28; WSL ASan/UBSan/leak suite: 30/30; 89,504
  explicit checks;
- Ndless r2022 ARM build: 1,095,275 bytes. The evaluator probe retains 15/15
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
