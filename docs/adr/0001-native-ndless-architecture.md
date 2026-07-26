# ADR-0001: use an Ndless-native production architecture

Status: accepted

Date: 2026-07-26

## Context

Phy-nspire needs a responsive notebook UI and computationally expensive
symbolic tensor, differential-geometry, and QFT operations on a constrained
TI-Nspire CX II CAS.

TI Lua can expose Giac through `luagiac` and is useful for behavioral
experiments. It also adds interpreter, document-runtime, and event-loop
overheads and gives the project less control over allocation, rendering, and
hot loops.

nMarkdown and KhiCAS separately prove that a rich native UI and a substantial
native CAS can run under Ndless on the target device.

## Decision

The production application will be one native ARM C/C++ program built with the
Ndless SDK.

- Reuse the relevant native nMarkdown platform, input, text, Markdown, and math
  layout concepts under compatible licensing.
- Build a feature-trimmed native Giac backend rather than loading it through TI
  Lua.
- Keep physics semantics in a typed, backend-neutral native layer.
- Use LuaGiAC/KhiCAS only as reference implementations and comparison oracles.
- Optimize and profile at the native level; do not require overclocking.

## Consequences

Benefits:

- direct control of CPU-heavy loops, memory layout, framebuffer updates, and
  cancellation;
- one coherent pointer-driven UI and Markdown/LaTeX rendering pipeline;
- a path to replace general Giac operations with compact specialized kernels.

Costs:

- a harder cross-build and integration task;
- an extremely tight 5–6 MB binary budget;
- GPL-3 compatibility and third-party notice obligations if upstream code is
  reused;
- device-specific performance, allocator, and display verification from the
  earliest milestones.
