# Reference corpus

Date: 2026-07-26

This document is the source-backed register of every external artifact
Phy-nspire depends on, what each one actually provides, and what it does not.
It supersedes the informal upstream descriptions in
[`feasibility-2026-07-26.md`](feasibility-2026-07-26.md) wherever the two
disagree; that note remains a dated record of the decision, this one is the
live engineering reference.

Every quantitative claim below was produced by reading a file or parsing a
binary in this environment. Claims that could not be verified are marked
**UNVERIFIED** and are listed together in
[§10](#10-unverified-claims-and-open-questions). Implementation agents must not
promote an UNVERIFIED claim to a design assumption without first executing the
verification step named beside it.

---

## 1. Verification environment

| Property | Value |
| --- | --- |
| Host | Windows 11 Pro 10.0.26300, MSYS2/Git-Bash shell |
| Repository worktree | `.claude/worktrees/reference-corpus`, branch `worktree-reference-corpus` |
| Upstream checkout used | `research/upstream/nMarkdown` (gitignored, main checkout) |
| Binaries used | `research/nmarkdown-v0.2.0.tns`, `../downloads/apps/khicas52-extracted/ndless/*.tns` |
| Network | GitHub and `sources.debian.org` reachable; `*.univ-grenoble-alpes.fr` **not** reachable (see §8) |
| Toolchain present | `git`, `gh`, `curl`, `python 3.13` |
| Toolchain absent | `nspire-gcc`, `arm-none-eabi-gcc`, `make`, `cmake` |

The absence of `make` and any ARM toolchain means **no claim in this document
was produced by compiling anything**. All size figures are read from shipped
binaries or from source file lengths, never from a build.

---

## 2. Upstream register

### 2.1 nMarkdown — UI, text, and math rendering donor

| Field | Value |
| --- | --- |
| Repository | <https://github.com/KaraRyougi/nMarkdown> |
| Pin | tag `v0.2.0`, commit `936b04854fc0838de9986b4bfee66a4da9db6166` |
| Licence | **GPL-3.0** (`LICENSE` line 1: "GNU GENERAL PUBLIC LICENSE Version 3") |
| Release asset | `nmarkdown.tns`, 1,269,572 bytes |
| SHA-256 | `55c71c31fe432382a44a67128eb8dddd05e4ea8983bd7ef9fad9795b9db2c933` — **verified in this environment** against `research/nmarkdown-v0.2.0.tns` |

Provides, at usable quality: a 320×240 RGB565 surface and primitives, a
FreeType + HarfBuzz text stack with glyph cache and A8 compositing, a bounded
LaTeX parser and math layout engine, CommonMark parsing via MD4C, Ndless
display/input/clock adapters, a desktop replay harness, and a Firebird
emulator verification suite.

Does **not** provide: absolute pointer coordinates (§7), editable text input,
any notion of a cell, any CAS binding, a size-report build target, or measured
on-device timing (`README.md` §"Compatibility boundaries": "peak memory and
the provisional CX timing targets still require measurement on each supported
calculator model").

### 2.2 KhiCAS / Giac — CAS backend donor

| Field | Value |
| --- | --- |
| Distribution | `khicas52.zip`, 11,947,330 bytes, SHA-256 `4ee6b97b51a4c46b5ea8a6c48b0fac6e79f732b797003cd6e5314e5664a2cb55` — **verified** |
| Native binary | `ndless/khicas.tns`, 4,138,955 bytes, SHA-256 `1d6f9dd8a543c136da99b8307d3540cd25f66838df8c72b9fcb65ec2c1b1f880` — **newly measured; was absent from `upstreams.lock.json`** |
| Lua module | `ndless/luagiac.luax.tns`, 4,138,210 bytes, SHA-256 `794f65c0b9851c23a14f1eb1ce2a0e1bfe12e5053acc4525951ce86554484052` — **verified** |
| Licence | GPL-3.0. `khicas52-extracted/LICENSE`: "Khicas.tns and luagiac.luax.tns and xcasnws.tns are covered by the GPL license. Source code is included in the giac distribution" |
| Target confirmation | `README_52_53`: "This version is for TI Nspire CX II and some CX. It will not work on TI Nspire CX HW-W." |

**The shipped archive contains no source.** The GPL offer points at the giac
distribution on Bernard Parisse's university page, which is unreachable from
this environment (§8). Acquiring buildable Giac source is a prerequisite task,
not an assumption.

Note that `khicas52-extracted/ndless/` also ships `ptt.tns` (302,320 bytes,
**closed-source** per `LICENSE`) and `upsilon.tns` (CC BY-NC-SA 4.0). Neither
is usable by this project; do not copy anything from that directory wholesale.

### 2.3 KhiCAS/ti-ce-giac — the only public calculator Giac configuration

| Field | Value |
| --- | --- |
| Repository | <https://github.com/KhiCAS/ti-ce-giac> (default branch `main`) |
| Content | 144 blobs, 6,630,453 bytes total: 99 headers (1,853,687 B), 40 `.cc` (4,359,633 B), 3 `.bak`, 2 extensionless |
| Declared version | `config.h`: `PACKAGE_VERSION "1.4.9"`, `VERSION "1.4.9-57"` |
| GitHub licence detection | `NOASSERTION` (giac itself is GPL) |

This is a **TI-84 Plus CE (eZ80)** port, not an Nspire port — `config.h` sets
`SIZEOF_INT 3`, which is eZ80, not ARM. It is nonetheless the only publicly
readable evidence of how KhiCAS trims Giac for a calculator, and
`docs/ARCHITECTURE.md`'s phrase "the working KhiCAS target configuration"
currently has no other referent. Its trimming macros are transcribed in §6.

### 2.4 Ndless SDK

| Field | Value |
| --- | --- |
| Repository | <https://github.com/ndless-nspire/Ndless> (default branch `master`) |
| `master` HEAD | `9484d8da7c7a4dde9766138c2e42e1d1e3acfcd4`, committed 2026-07-06T19:02:24Z |
| Published tags | `v4.5`, `v4.4`, `v4.2`, `v4.0`, `v3.9`, `v3.6` |
| Local artifacts | `../downloads/ndless-r2022/` — **installer `.tns` files only, no SDK** |

**There is no `r2022` git tag.** "r2022" is an Ndless *revision* number of the
kind consumed by the Zehn `NDLESS_REVISION_MIN` flag, not a release tag. The
pin in `README.md` must therefore be expressed as a commit SHA plus a minimum
revision number, not as a tag. nMarkdown itself declares
`--ndless-min 31 --ndless-rev-min 2004` (`Makefile.ndless:89`).

Authoritative format documentation used in §4:
`ndless-sdk/include/zehn.h` and `ndless/src/resources/zehn_loader.cpp`.

### 2.5 Specification-only upstreams

xAct/xTensor/xPerm, FeynCalc, FeynArts, FORM. The reasoning in
`feasibility-2026-07-26.md` §"CAS and physics-library portability" stands and
is not re-litigated here. For this corpus they contribute **notation, canonical
forms, and regression corpora only**; no build dependency is planned on any of
them except a possible bounded port of the xPerm C core (Phase 2).

---

## 3. Measured artifact facts

Both shipped `.tns` files are `make-prg` containers: a small ARM stub beginning
`50 52 47 00` (`"PRG\0"`), followed by a Zehn image. Parsing the Zehn headers
(`zehn.h`, `struct Zehn_header`) gives:

| Field | `khicas.tns` | `luagiac.luax.tns` | `nmarkdown.tns` |
| --- | ---: | ---: | ---: |
| Container bytes on flash | 4,138,955 | 4,138,210 | 1,269,572 |
| Zehn header offset | 13,292 | 13,292 | 12,732 |
| `file_size` | 4,125,663 | 4,124,918 | 1,256,840 |
| `alloc_size` | 8,908,288 | 8,908,156 | 2,160,104 |
| `reloc_count` | 83,424 | 83,453 | 7,485 |
| `flag_count` | 8 | 8 | 11 |
| Compression | zlib | zlib | zlib |
| `EXECUTABLE_NAME` | `giac` | `luagiac` | `nMarkdown` |
| `NDLESS_VERSION_MIN` | *absent* | *absent* | 31 |
| `NDLESS_REVISION_MIN` | *absent* | *absent* | 2004 |
| `USES_LCD_BLIT` / `RUNS_ON_HWW` | 1 / 1 | 1 / 1 | 1 / 1 |

Two consequences that no current project document accounts for.

**Every `.tns` is zlib-compressed.** The 5–6 MB "installed application size"
target in `README.md` is therefore a *compressed flash* figure. It says nothing
about RAM.

**The loader holds the compressed source and the decompressed image live at the
same time.** From `zehn_loader.cpp`: the reloc table is `malloc`ed first
(`Storage<T>`, line 20), then `execmem_alloc(remaining_mem)` reserves the full
image (line 122), then `Storage<uint8_t> compressed(remaining_file)` allocates a
second buffer for the whole compressed payload (line 138) before
`uncompress()` runs (line 146). Peak is the sum of all three.

Direct confirmation of the layout, from `khicas.tns`: the computed metadata
region ends at offset 347,060, and the bytes immediately before it are
`… "giac\0" 00 00 00 00 | 78 9c …`. `78 9c` is the zlib header. The
`EXECUTABLE_NAME` string sits in uncompressed extra data and the compressed
payload begins directly after it.

---

## 4. RAM and flash budget

Derived from §3, with `tables = 32 + 4·reloc_count + 4·flag_count + extra_size`
matching the loader's `nuc_ftell(file) - file_start`:

| Artifact | Flash `.tns` | Resident image | zlib buffer | Reloc table | Peak at load |
| --- | ---: | ---: | ---: | ---: | ---: |
| `khicas.tns` | 4,138,955 | 8,574,520 | 3,791,895 | 333,696 | 12,700,111 |
| `nmarkdown.tns` | 1,269,572 | 2,130,052 | 1,226,788 | 29,940 | 3,386,780 |
| **Sum** | **5,408,527** | **10,704,572** | | | **16,086,891** |

Against the device budget measured in `feasibility-2026-07-26.md`
(free RAM 31,805,820 bytes; free flash 91,090,944 bytes):

| Quantity | Bytes | MiB | % of free RAM |
| --- | ---: | ---: | ---: |
| Resident image, both components | 10,704,572 | 10.21 | 33.7 % |
| Peak during load | 16,086,891 | 15.34 | 50.6 % |
| RAM remaining after load | 21,101,248 | 20.12 | — |

Flash-to-RAM expansion is **1.98×** overall (2.07× for `khicas.tns`, 1.68× for
`nmarkdown.tns`).

Flash headroom under a strict 6 MiB ceiling is **882,929 bytes**
(6,291,456 − 5,408,527). `feasibility-2026-07-26.md` states 883,674 bytes
because it used `luagiac.luax.tns` (the TI-Lua module) as the baseline. For a
native plan the correct baseline is `khicas.tns`; the corrected figure is
882,929.

Both sums are upper bounds for a *merged* binary, not predictions of one. A
single link unit drops one Zehn header, one reloc table, the KhiCAS shell UI,
and duplicated runtime, but adds the physics layer. The number that matters and
that nobody has measured is the resident image of a Phy-nspire binary linking
trimmed Giac; producing it is Task **P0-6**.

Planning rules that follow directly:

- The RAM ceiling, not the flash ceiling, is the binding constraint once Giac
  is linked in. Budget **≤ 12 MiB resident** and **≤ 20 MiB peak** at load.
- Relocation count is a launch-latency driver processed linearly by the loader.
  `khicas.tns` carries 83,424 relocations against nMarkdown's 7,485. Track
  `reloc_count` in the size report as a first-class metric.
- Any decision to embed a CJK font must be evaluated against RAM, not only
  flash: `SarasaFixedSC-Regular-CX.ttf` alone is 6,105,504 bytes.

---

## 5. nMarkdown reuse classification

### 5.1 Layer dependency graph

Include-edge analysis over `src/` and `include/` (`#include "nmarkdown/<layer>/`):

| Layer | Depends on |
| --- | --- |
| `render` | *(none)* |
| `io` | *(none)* |
| `text` | document, generated, io, layout, platform, render, text |
| `math` | document, generated, layout, math, render, text |
| `layout` | document, io, layout, math, text |
| `document` | document, generated, layout, text |
| `platform` | **app**, document, io, platform, render |
| `app` | app, document, io, layout, math, platform, render, text |

`render` and `io` are leaves and lift verbatim. The apparent `text → document`
and `math → document` edges are narrow and benign — they resolve to exactly
three headers:

- `src/math/{math_layout,math_lexer,math_parser}.cpp` → `document/utf8.h`
- `src/text/font.cpp` → `document/utf8.h`
- `src/text/harfbuzz_shaper.cpp` → `document/{unicode,utf8}.h`

`utf8.h` and `unicode.h` are generic text utilities misfiled under `document/`;
relocating them to a `text/` or `unicode/` module severs the edge.

The one real layering violation is `platform → app`, and it is confined to the
two entry points `src/platform/desktop/main.cpp:8` and
`src/platform/ndless/main.cpp:9`, both of which include
`nmarkdown/app/application.h` solely to call `run_reader()`. Replacing those
two files with a notebook entry point cuts the reader shell away cleanly.

`layout/block_layout.h:12` includes `document/markdown.h` and
`layout/plain_text_layout.h:13-14` include `document/{search,utf8}.h`. That
coupling is genuine — those two files *are* the Markdown block layout — and is
why they are classified **replace**, not lift.

### 5.2 Classification table

| Group | Contents | Files | Lines | Bytes | Disposition |
| --- | --- | ---: | ---: | ---: | --- |
| A | `render/` + `io/` | 7 | 483 | 14,631 | **Lift verbatim** |
| B | `text/` (font, shaper, cache, compositor, renderer, system, catalog, pack) | 18 | 3,912 | 148,518 | **Lift**, drop external-font manager hooks |
| C | `math/` (lexer, parser, atoms, layout, macros, system, symbol table) | 13 | 3,801 | 169,398 | **Lift**, then extend (§6) |
| D | `document/` utilities + Markdown (`utf8`, `unicode`, `entity`, `text_encoding`, `markdown`, `document_ir`) | 12 | 2,583 | 96,324 | **Lift**, re-home `utf8`/`unicode` |
| E | `generated/` (core font pack, unicode tables, HTML entities, math font) | 8 | 57,113 | 5,174,700 | **Regenerate**, do not copy |
| F | Ndless platform adapters + `stdio_files` + `platform.h` + `display_sync.h` | 10 | 2,053 | 75,994 | **Lift**, extend input (§7) |
| G | Desktop harness (`src/platform/desktop/`) | 7 | 421 | 14,432 | **Lift**, retarget to notebook |
| H | `layout/fenwick` + `layout/fixed` | 3 | 129 | 3,622 | **Lift verbatim** |
| R1 | `app/` reader shell (`application.cpp`, `viewer.cpp`, headers) | 4 | 7,844 | 351,925 | **Replace** |
| R2 | `layout/{block_layout,plain_text_layout}` | 4 | 3,980 | 159,987 | **Replace** with cell layout |
| R3 | `document/{state,search}` reader state | 4 | 632 | 25,160 | **Replace** with notebook document |
| R4 | Legacy CJK codec tables (GBK, Shift-JIS, JIS0212) | 3 | 3,684 | 415,453 | **Drop** — reader-only import codecs |
| R5 | `platform/ndless/firebird_font_fixture.h` | 1 | 7,097 | 545,418 | **Drop** — test fixture |

Totals: **70,495 lines liftable** (13,382 hand-written once group E's generated
tables are excluded) against **23,237 lines to replace or drop**, of which the
genuinely reimplementable reader logic is R1+R2+R3 = **12,456 lines**.

The headline for planning: the notebook shell is a ~12.5k-line rewrite on top
of a ~13.4k-line hand-written foundation that already works on the target
device. Group E is 5.17 MB of *source* that compiles into the embedded
`assets/core.fpk` (761,040 bytes on disk) — it must be regenerated from the
manifest (`assets/core-font-pack.json`, `make fontpack`), never copied, or the
physics build will inherit reader-sized font coverage it does not need.

---

## 6. Capability matrices

### 6.1 Math rendering versus physics notation

`src/math/math_symbol_table.inc` holds **514** entries, derived from KaTeX
`src/symbols.ts` at commit `2c6143a6dd7c168cef602c1e29f8add66f7fcc19` and
filtered to the embedded Latin Modern Math face
(`THIRD_PARTY_NOTICES.md` §"KaTeX symbol registry").

Probed and **present**: `\partial \nabla \otimes \oplus \wedge \dagger
\langle \rangle \int \oint \sum \prod \infty \hbar \ell \propto \approx \equiv
\simeq \times \cdot \bullet \star \dag \ddag \mp \pm`, the full lower- and
upper-case Greek set used in physics (`\alpha`…`\omega`, `\Gamma \Delta \Theta
\Lambda \Xi \Pi \Sigma \Phi \Psi \Omega`), and `\varepsilon`.

Structural features **present** (`src/math/math_parser.cpp`):

| Feature | Evidence |
| --- | --- |
| Accents `\hat \bar \vec \dot \ddot \tilde \check \acute \grave \breve \mathring` | `math_parser.cpp:680-685`, `MathAccent` enum |
| `\overline \underline \overbrace \underbrace` | parser command list |
| Variants `\mathrm \mathbf \mathbb \mathcal \mathfrak \mathsf \mathtt \mathit \mathscr \mathnormal \boldsymbol` | parser command list, `MathVariant` enum |
| `\frac \cfrac \sqrt` (with optional index) | parser command list |
| `\left … \right` with `\lVert \rVert \lceil \lfloor \langle` etc. | parser command list |
| Environments `matrix pmatrix bmatrix vmatrix Vmatrix Bmatrix cases array align align* aligned` | parser environment list |
| `\tag` equation numbers, `\hline` | `MathNodeKind::Tag`, parser |
| Style overrides `\displaystyle \textstyle \scriptstyle \scriptscriptstyle` | parser command list |
| `\operatorname`, `\text`, `\not`, `\quad \qquad \space` | parser command list |
| Zero-argument macros `\newcommand \renewcommand \def` | `math_macros.cpp`; bounded to 16 macros, 32-byte names, 256-byte replacements, depth 8 |

Structural features **absent**, verified by exhaustive grep over `src/math/`
and `include/nmarkdown/math/`:

| Missing | Why physics needs it |
| --- | --- |
| `\overset \underset \stackrel` | operator annotations, `\overset{\leftrightarrow}{\partial}` |
| `\substack` | multi-line summation limits |
| `\binom \genfrac \atop` | combinatorial and Clebsch-Gordan coefficients |
| `\slashed` | Feynman slash notation (Dirac algebra) |
| `\tensor` / `\prescript` | **staggered multi-level tensor indices — the single most important gap for Phase 2** |
| `\limits \nolimits` | explicit limit placement; currently automatic only, driven by `MathStyle::Display` plus the symbol table's `movable_limits` flag (`math_parser.cpp:796`, `math_layout.cpp:838-845`) |
| `\phantom` | alignment in derivation steps |
| Parameterized macros | rejected explicitly: `math_macros.cpp:190` "replacement is too large or has parameters" |

Bounds already enforced (`include/nmarkdown/math/math_atoms.h:117-119`):
`kMaximumMathNesting = 64`, `kMaximumMathBoxes = 16384`,
`kMaximumMatrixDimension = 32`. These are the natural starting values for the
notebook's expression-limit policy.

Conclusion for `docs/SCIENTIFIC_SCOPE.md` §"Cross-cutting notebook behavior":
the glyph and layout vocabulary for physics is essentially already present. The
work is **structural**, concentrated in staggered index placement, and is far
smaller than a from-scratch math renderer.

### 6.2 Giac calculator trimming configuration

Transcribed from `KhiCAS/ti-ce-giac/config.h`. Read this as the shape of the
size-reduction surface, not as a drop-in Nspire configuration (§2.3).

| Macro | Effect |
| --- | --- |
| `USE_GMP_REPLACEMENTS`, `HAVE_LIBTOMMATH` | replaces GMP with libtommath + `gmp_replacements.h` |
| `USTL`, `#define std ustl` | replaces the standard library with a micro-STL |
| `NO_RTTI` | no run-time type information |
| `NO_STDEXCEPT` | no C++ exceptions |
| `NO_PHYSICAL_CONSTANTS` | drops the physical-constants table |
| `STATIC_BUILTIN_LEXER_FUNCTIONS` | static lexer dispatch table |
| `NO_UNARY_FUNCTION_COMPOSE`, `NO_TEMPLATE_MULTGCD`, `NOTURTLE` | drops composition, templated multi-GCD, turtle graphics |
| `GIAC_NO_OPTIMIZATIONS` | disables selected optimization paths |
| `HAVE_NO_{SIGNAL_H,PWD_H,CWD,HOME_DIRECTORY,SYS_TIMES_H,SYS_RESOURCE_WAIT_H}` | bare-metal host adaptation |

**This is a hard integration hazard.** nMarkdown compiles
`-std=c++17 -fexceptions -fno-rtti` (`Makefile.ndless:82-83`) and its core is
built on `std::vector`, `std::string`, `std::shared_ptr`, and
`std::unique_ptr`. A Giac configured with `USTL`, `#define std ustl`,
`NO_RTTI`, and `NO_STDEXCEPT` cannot share a translation unit — and arguably
not a link unit — with that code without an explicit reconciliation decision.
The options are: (a) build Giac against the real libstdc++ and pay the size,
(b) keep the `ustl` Giac behind a strict C ABI shim with no C++ types crossing
the boundary, or (c) namespace-isolate. This decision gates all of Phase 1 and
is Task **P1-1**.

Note also that nMarkdown *requires* exceptions (`-fexceptions`), so a global
`-fno-exceptions` build is not available as an escape.

---

## 7. Platform boundary gap analysis

`docs/ARCHITECTURE.md` §"Platform layer" states the initial source reference is
"nMarkdown's already-tested Ndless platform adapter", and `README.md` requires
"touchpad pointer interaction". These are not the same capability.

What the boundary actually exposes
(`include/nmarkdown/platform/platform.h:53-83`):

```cpp
struct InputEvent {
    InputEventType type = InputEventType::None;
    int amount = 0;
    InputEventOrigin origin = InputEventOrigin::Semantic;
};
class Input {
    virtual bool poll(InputEvent& event) = 0;
    virtual bool interaction_active() const { return false; }
};
```

`InputEventType` is a closed set of 28 **reader-semantic** verbs —
`ScrollLineUp`, `PageDown`, `SwipeLeft`, `ToggleBookmark`, `OpenBookmarks`,
`PointerScroll`, `PointerPan`, … . The richest pointer events, `PointerScroll`
and `PointerPan`, carry a scalar `amount` and no position.

Absolute coordinates exist but are private. `src/platform/ndless/input_ndless.h`
declares `struct TouchSample { bool scanned, valid, contact; TouchKey key;
int x, y; }` as a **private** member type of `InputNdless`, consumed internally
by `sample_touchpad`, `poll_touchpad`, and the axis-lock state machine
(`touch_axis_`, `touch_origin_x_`, `touch_origin_y_`, `touch_tap_candidate_`).
Nothing reaches the portable interface.

Therefore: a two-dimensional notebook with cell hit-testing, palettes, and
selection **cannot be built on the existing `Input` interface**. The platform
boundary must be extended with a pointer-state accessor, and the desktop
adapter and Firebird probes extended in step. This is Task **P1-2** and it is
on the critical path for every UI item in Phases 1 and 6.

The display side, by contrast, is directly usable. `DisplayNdless` presents a
stable landscape 320×240 RGB565 surface;
`include/nmarkdown/platform/ndless/display_sync.h` supplies
`stage_landscape_rgb565_for_native()` for the HW-W/CX II 240×320 rotation plus
a `VerticalCompareEdge` state machine that rejects a stale latched
vertical-compare bit before calling `lcd_blit`. Both are pure functions with
existing host tests (`tests/test_display_sync.cpp`) and lift unchanged.

`FileSystem` already provides `write_atomic()`, which satisfies the
`docs/ARCHITECTURE.md` requirement that "the notebook saves source,
declarations, and compact results atomically". `open_random_access()`
degrades explicitly — false with an empty `error` means unsupported, false
with a non-empty `error` is a real failure — so notebook loading can reuse the
same convention.

`AllocationStats` (`include/nmarkdown/platform/allocation_stats.h`) is
compiled out unless `NMARKDOWN_ALLOCATION_PROBE` is defined, and nMarkdown
ships a separate `ndless-memory-profile` target for it. Phy-nspire needs the
same split: allocation telemetry is a diagnostic build, not a production cost.

---

## 8. Source acquisition and supply risk

`khicas52.zip` ships binaries only; its GPL source offer points at
Bernard Parisse's page. Measured reachability from this environment on
2026-07-26:

| Endpoint | Result |
| --- | --- |
| `https://www-fourier.univ-grenoble-alpes.fr/~parisse/giac.html` | connect failure (curl code 000) |
| `https://wfourier.u-ga.fr/~parisse/install_en.html` | connect failure (curl code 000) |
| `https://www-fourier.univ-grenoble-alpes.fr/~parisse/ti/khicas52.zip` | connect failure (curl code 000) |

Both hostnames recorded in the project documents are currently unusable, and
the upstream has no mirror of its own. Verified reachable alternates:

| Channel | Status | Notes |
| --- | --- | --- |
| `https://sources.debian.org/api/src/giac/` | HTTP 200 | versions `1.9.0.93+dfsg2-4`, `1.9.0.93+dfsg2-3`, `1.9.0.35+dfsg2-1.1`, `1.6.0.41+dfsg1-1`, `1.4.9.69+dfsg1-2`. `+dfsg` means repackaged — non-free components removed; verify nothing needed was stripped |
| `https://sourceforge.net/projects/xcas/files/` | HTTP 200 | upstream Xcas project files |
| `https://github.com/KhiCAS/ti-ce-giac` | reachable | calculator-trimmed giac 1.4.9, eZ80 target |

The Nspire `khicas.tns` is dated 2024-07-06 and its Zehn payload is
zlib-compressed, so the giac version it was built from **cannot be read out of
the binary** (§10). Matching the exact upstream version is Task **P0-3**.

Action: mirror the chosen Giac source tarball into a pinned, hashed artifact
before any build work depends on it. A project whose only CAS supply line is a
single unreachable university web host is one outage away from being unable to
reproduce its own release.

---

## 9. Licence obligation matrix

| Component | Licence | Obligation |
| --- | --- | --- |
| nMarkdown | GPL-3.0 | Derivative work must be GPL-3.0; retain notices |
| Giac / KhiCAS | GPL-3.0 | Same; source offer must accompany distribution |
| MD4C `65c6c9d…` | MIT | Retain `third_party/md4c/LICENSE.md` |
| FreeType 2.14.3 | FreeType Project License | Retain `LICENSE.TXT`, credit in documentation |
| HarfBuzz 14.2.1 | "Old MIT" | Retain `third_party/harfbuzz/LICENSE` |
| KaTeX symbol table `2c6143a…` | MIT | Retain `third_party/KATEX_LICENSE` |
| DejaVu Sans / Mono | Bitstream Vera / Arev | Retain `assets/fonts/LICENSE_DEJAVU` |
| Latin Modern Math | GUST Font License | Retain `LICENSE_LATIN_MODERN_MATH` |
| Sarasa Fixed SC 1.0.40 | SIL OFL 1.1 | Retain `LICENSE_SARASA`; optional asset |
| Unicode CLDR/UCD 17.0.0 | Unicode-3.0 | Retain attribution for generated tables |
| xPerm C core | GPL | Compatible; retain notices if ported |
| `ptt.tns` | **closed source** | **Unusable — must not be copied or linked** |
| `upsilon.tns` | CC BY-NC-SA 4.0 | **Unusable — non-commercial clause is GPL-incompatible** |

The combination is coherent: **GPL-3.0 is the only viable licence for
Phy-nspire**, because both nMarkdown and Giac are GPL-3.0 and everything else
in the corpus is GPL-compatible. `README.md` §"Licensing" defers this decision;
the evidence says there is no decision left to make. Two files that happen to
sit in the same KhiCAS directory (`ptt.tns`, `upsilon.tns`) are incompatible and
must be explicitly excluded from any bulk copy.

`docs/adr/0001-native-ndless-architecture.md` should be amended, or a follow-up
ADR recorded, to state GPL-3.0 as the project licence.

---

## 10. UNVERIFIED claims and open questions

Each entry names the step that would settle it.

| # | Claim | Status | Verification step |
| --- | --- | --- | --- |
| U1 | "KhiCAS proves that a useful general CAS, **including GMP/MPFR/MPFI support**, fits in about 4.14 MB" (`feasibility-2026-07-26.md`) | **UNVERIFIED, and contradicted for the one readable configuration.** The Zehn payload is zlib-compressed, so no symbol or string scan of `khicas.tns` can confirm it: a byte scan finds zero occurrences of `mpfr`, `gmp`, `mpfi`, or `tommath`, and the single hit for `giac` lies at offset 347,052 — inside the *uncompressed* Zehn metadata, immediately before the `78 9c` zlib header. The scan can only see metadata, never the payload. Meanwhile `KhiCAS/ti-ce-giac/config.h` sets `USE_GMP_REPLACEMENTS` and `HAVE_LIBTOMMATH`, i.e. **no GMP** | Obtain Giac source (§8), locate the Nspire target configuration, read its arbitrary-precision backend |
| U2 | A trimmed native Giac *library* is materially smaller than the 4,138,955-byte `khicas.tns` *application* | UNVERIFIED — plausible, since the KhiCAS shell UI is dropped, but unmeasured | Task P0-6 size report |
| U3 | Giac version used by `khicas.tns` (2024-07-06) | UNKNOWN — compressed payload | Compare against Debian/SourceForge release dates, or decompress the Zehn payload offline |
| U4 | nMarkdown's on-device timing | **Not measured by upstream either.** `README.md`: "peak memory and the provisional CX timing targets still require measurement on each supported calculator model" | Task P0-5 device baseline |
| U5 | LTO compatibility with the Ndless toolchain (`docs/ARCHITECTURE.md` §"Performance policy" says "measured LTO where compatible") | UNVERIFIED — nMarkdown does not use LTO; `Makefile.ndless:87` uses only `-Wl,--gc-sections` | Task P0-2 toolchain bring-up |
| U6 | Whether xPerm's C core compiles under the Ndless toolchain | UNVERIFIED — no xPerm source retrieved in this pass | Phase 2 spike |
| U7 | Firebird emulator verification is reusable | UNVERIFIED — nMarkdown's harness requires a local PocketJS-NSpire checkout and a calculator dump, neither present; `docs/ARCHITECTURE.md` already hedges with "where legally available" | Phase 0 spike, or drop from the verification plan |

---

## 11. Corrections to existing project documents

| Document | Statement | Correction |
| --- | --- | --- |
| `research/feasibility-2026-07-26.md` | Size headroom 883,674 bytes | 882,929 bytes — the native baseline is `khicas.tns` (4,138,955), not `luagiac.luax.tns` (4,138,210) |
| `research/feasibility-2026-07-26.md` | Giac "including GMP/MPFR/MPFI support" | Unverified; contradicted for `ti-ce` (§10 U1) |
| `research/upstreams.lock.json` | `khicas_cx2.native_binary_bytes` present, no hash | Add `native_binary_sha256: 1d6f9dd8a543c136da99b8307d3540cd25f66838df8c72b9fcb65ec2c1b1f880` |
| `README.md` | "application target is nominally 5–6 MB" | Flash-compressed only. Add a RAM budget: ≤ 12 MiB resident, ≤ 20 MiB peak (§4) |
| `README.md` | Ndless "r2022" | Not a git tag; pin by commit + `NDLESS_REVISION_MIN` (§2.4) |
| `README.md` | Licensing "to be fixed" | GPL-3.0 is forced by nMarkdown + Giac (§9) |
| `docs/ARCHITECTURE.md` | Platform layer provides "touchpad" pointer interaction | Provides gesture semantics only; absolute coordinates are private (§7) |
| `docs/ARCHITECTURE.md` | "trimmed native Giac library derived from the working KhiCAS target configuration" | No Nspire configuration is publicly available; the only readable one targets eZ80 (§2.3, §6.2) |
| `docs/ARCHITECTURE.md` | "Every module has … device timing counters" | Upstream has none; inherited aspiration (§10 U4) |
| `docs/ROADMAP.md` | Phase 0 "size-report and symbol-report targets" | Must be written from scratch; nMarkdown has no such target |
| `docs/ROADMAP.md` | Phase 0 "pinned Ndless SDK" | No SDK is present locally; only calculator-side installers (§1, §2.4) |

---

## 12. How implementation agents should use this document

1. Treat §4 (RAM budget), §5.2 (classification table), §6.1 (math gaps), and
   §7 (pointer gap) as **binding inputs**. They are measured.
2. Treat §10 as **blocking unknowns**. Do not design against U1–U7 until the
   named verification step has run.
3. Every task in [`../docs/TASK_CONTRACTS.md`](../docs/TASK_CONTRACTS.md)
   cites a section here. If a contract and this document disagree, this
   document is the evidence and the contract is the error.
4. When a claim here is falsified by a real build or a real device, amend this
   file in the same commit as the finding, and move the entry out of §10.
