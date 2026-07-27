# Phy-nspire

Phy-nspire is an Ndless-native symbolic physics notebook for the TI-Nspire
CX II CAS.

The project targets a touchpad-driven, two-dimensional notebook rather than a
linear command shell. Its first scientific layer is tensor calculus and
differential geometry. The longer roadmap covers general relativity and black
holes, quantum mechanics, QFT and gauge theory, and compact Feynman-diagram
workflows.

## Non-negotiable design constraints

- The production application is native ARM C/C++ built with the Ndless SDK.
  TI Lua may be used only as a reference or host-side comparison, never as the
  production execution layer.
- The target device is a TI-Nspire CX II CAS running OS 6.4.0.74 and
  Ndless r2022.
- The application target is nominally 5–6 MB, with exact accounting for
  optional fonts and documentation still to be finalized.
- The UI must provide touchpad pointer interaction, palettes, notebook cells,
  two-dimensional mathematics, Markdown notes, and bounded LaTeX rendering.
- Long calculations must be cancellable and bounded by explicit memory and
  term-count limits.

## Current status

Phase 0, the reproducible native baseline, is implemented. The first Phase 1
notebook shell is now wired into the production application. The repository
builds two artifacts from one portable core:

- a host binary and test suite using a C11 core and C++17 formula bridge;
- `dist/phy-nspire.tns`, a native ARM program that brings up the CX II
  framebuffer, samples the keypad and touchpad, and exits cleanly.

The preserved Phase 0 diagnostic still renders one baseline frame for
framebuffer fixtures, but the production entry point now opens a native
notebook: editable Markdown and symbolic input cells, `+MD`/`+Math` insertion,
independent upper-right `RUN` badges, relative touchpad navigation with
persistent cursor position, atomic Save/Open under
`Documents/phy-nspire/notebooks/`, and direct two-dimensional IR layout for
exact fractions and powers. Markdown bodies now typeset inline `$...$` /
`\(...\)` and display `$$...$$` / `\[...\]` mathematics through the pinned
nMarkdown OpenType MATH engine.

Three native foundations are now connected through that shell: the typed
expression IR; a symbolic scalar computer algebra layer over it with
exact rational arithmetic, a normal form, expansion, substitution,
differentiation, and an exact zero decision; and the component-independent
tensor core with charts, dense storage, valence, and signed slot symmetries.
The CAS answers "unknown" rather than guessing outside its decidable class, so
the scalar operations needed by the tensor and GR phases no longer depend on
integrating Giac.

Measured on the pinned toolchain after the math-engine integration:
`dist/phy-nspire.tns` is 1,048,076 bytes, 16.7% of the 6 MiB ceiling. The
strict host suite passes 17/17, including 61,541 explicit checks. The prior
and current trees pass under AddressSanitizer, UndefinedBehaviorSanitizer, and
leak detection; the current run includes the C++ rendering slice.

The native CAS smoke artifact has run on the target CX II and shown all seven
exact symbolic checks passing. Returning from it restored Documents normally.
The earlier notebook shell also passed its input and touchpad acceptance. The
new 1.0 MiB build containing persistence and nMarkdown LaTeX rendering has
been transferred through the repository-owned CLI and verified byte-for-byte
on the calculator; its physical UI acceptance run is still pending.
Full Markdown blocks, palettes, and direct typed-IR-to-nMarkdown layout remain
open Phase 1 work. The separate baseline channel-order check remains tracked
in [docs/BUILD.md](docs/BUILD.md).

Start here:

- [Building](docs/BUILD.md)
- [Scientific calculation scope](docs/SCIENTIFIC_SCOPE.md)
- [Native architecture](docs/ARCHITECTURE.md)
- [Typed expression IR](docs/IR.md)
- [Component tensor core](docs/TENSOR.md)
- [Scalar computer algebra](docs/CAS.md)
- [Notebook shell and 2D layout](docs/NOTEBOOK.md)
- [Reader-facing symbolic source language](docs/SOURCE_LANGUAGE.md)
- [Roadmap](docs/ROADMAP.md)
- [ADR-0001: native Ndless architecture](docs/adr/0001-native-ndless-architecture.md)
- [Initial feasibility evidence](research/feasibility-2026-07-26.md)
- [QFT and gauge theory: MVP source reference](docs/references/QFT_GAUGE.md)
- [Agent task pack: Dirac algebra and SU(N)](docs/agent-tasks/QFT_DIRAC.md)

## Layout

```
include/phy/      public headers: platform boundary, drawing, app shell
src/core/         portable, backend-neutral core
src/ir/           typed expression IR: interning, ordering, serialization
src/cas/          scalar algebra: normal form, calculus, the zero decision
src/tensor/       component tensors: charts, storage, slot symmetries
src/gfx/          RGB565 primitives and the built-in debug font
src/render/       typed-IR layout and the narrow nMarkdown C++ bridge
src/input/        relative pointer tracking
src/notebook/     source parser, bounded cells, CAS dispatch, native renderer
src/app/          native notebook event loop and the two entry points
src/platform/     one subdirectory per backend: ndless (device), host (tests)
src/tools/        developer utilities
tests/            host test suite and framebuffer fixtures
tests/oracle/     host-only numeric oracle certifying the QFT golden cases
tools/            SDK bootstrap, size and symbol reports
third_party/      pinned nMarkdown submodule and retained license notices
docs/references/  source-backed capability references
docs/agent-tasks/ executable contracts derived from those references
```

## Licensing

Phy-nspire is licensed under
[GNU GPL version 3](LICENSE). The pinned nMarkdown mathematical typesetter is
GPL-3.0 and is linked into the product. Its transitive FreeType, HarfBuzz,
KaTeX-derived data, fonts, and Unicode notices are retained in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and in the submodule.
