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

Repository initialization, feasibility research, and architecture definition.
No calculator application has been implemented yet.

Start here:

- [Scientific calculation scope](docs/SCIENTIFIC_SCOPE.md)
- [Native architecture](docs/ARCHITECTURE.md)
- [Roadmap](docs/ROADMAP.md)
- [ADR-0001: native Ndless architecture](docs/adr/0001-native-ndless-architecture.md)
- [Initial feasibility evidence](research/feasibility-2026-07-26.md)

Before writing code, read these two. They are the measured evidence base and
the work breakdown derived from it:

- [Reference corpus](research/REFERENCE_CORPUS.md) — verified upstream
  inventory, flash **and RAM** budgets, nMarkdown reuse classification,
  capability gaps, licence obligations, and corrections to the documents above.
- [Task contracts](docs/TASK_CONTRACTS.md) — numbered work units with
  acceptance tests and dependency order.

Two contracts currently block all build work: the Ndless SDK toolchain is not
provisioned (P0-1) and no buildable Giac source has been obtained (P0-3).

## Licensing

The selected upstream references include GPL-3 software. The final project
license and retained notices will be fixed before any upstream code is copied
or linked. Until then, this repository contains planning and original research
only.
