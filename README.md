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

Phase 0, the reproducible native baseline, is implemented. The repository
builds two artifacts from one portable core:

- a host binary and test suite that run anywhere with a C11 compiler;
- `dist/phy-nspire.tns`, a native ARM program that brings up the CX II
  framebuffer, samples the keypad and touchpad, and exits cleanly.

The Phase 0 application is not the notebook. It renders one baseline frame and
proves the platform boundary; cells, the expression IR, and the CAS backend
begin in Phase 1.

Measured on the pinned toolchain: the `.tns` is 12,676 bytes, 0.2% of the 6 MB
ceiling.

Not yet done: the roadmap's on-device check. Nothing here has been run on real
hardware, so "launch and exit without display corruption" is still unverified.
[docs/BUILD.md](docs/BUILD.md) has the procedure.

Start here:

- [Building](docs/BUILD.md)
- [Scientific calculation scope](docs/SCIENTIFIC_SCOPE.md)
- [Native architecture](docs/ARCHITECTURE.md)
- [Typed expression IR](docs/IR.md)
- [Roadmap](docs/ROADMAP.md)
- [ADR-0001: native Ndless architecture](docs/adr/0001-native-ndless-architecture.md)
- [Initial feasibility evidence](research/feasibility-2026-07-26.md)
- [QFT and gauge theory: MVP source reference](docs/references/QFT_GAUGE.md)
- [Agent task pack: Dirac algebra and SU(N)](docs/agent-tasks/QFT_DIRAC.md)

## Layout

```
include/phy/      public headers: platform boundary, drawing, app shell
src/core/         portable, backend-neutral core
src/gfx/          RGB565 primitives and the built-in debug font
src/app/          Phase 0 application and the two entry points
src/platform/     one subdirectory per backend: ndless (device), host (tests)
src/tools/        developer utilities
tests/            host test suite and framebuffer fixtures
tests/oracle/     host-only numeric oracle certifying the QFT golden cases
tools/            SDK bootstrap, size and symbol reports
docs/references/  source-backed capability references
docs/agent-tasks/ executable contracts derived from those references
```

## Licensing

The selected upstream references include GPL-3 software. The final project
license and retained notices will be fixed before any upstream code is copied
or linked. Until then, this repository contains planning and original research
only.
