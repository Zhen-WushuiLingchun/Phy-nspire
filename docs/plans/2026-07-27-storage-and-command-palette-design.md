# Native save repair and command palette

## Scope

This increment fixes the real-device `PHY_ERR_BACKEND` reported by the first
Save As attempt and adds a compact, context-sensitive insertion catalog for
the calculator keyboard.

## Storage diagnosis and repair

The device can create and enumerate
`/documents/phy-nspire/notebooks`; otherwise the workspace and Save As name
suggestion could not have opened. The failing path is therefore bounded to the
old replacement sequence:

1. create `.phy-save.tmp`;
2. explicitly `fflush`;
3. rename the extensionless hidden file to a `.tns` document.

Ndless exposes `fopen`, `fwrite`, `fclose`, `fflush`, and `rename`, but TI's
document store is most reliable when every created document has a visible
`.tns` leaf name. The repaired backend:

- writes a new destination directly and reads it back byte-for-byte;
- uses only `.tns` scratch names when replacing an existing document;
- tries the fast rename path first;
- falls back to a verified copy and restores the backup on failure;
- never includes internal scratch names in the Open catalog.

The fallback is important because the storage contract promises that a
reported replacement failure preserves the old notebook.

## Command-palette interaction

`MENU` remains the file menu outside edit mode. Inside an editable cell it
opens a modal insertion catalog:

- Math input: CAS Algebra, Functions, and Calculus/Syntax categories;
- Markdown body: LaTeX Layout, Calculus, Greek, Style, and Matrix categories.

Left/right changes category, up/down changes entry, Enter inserts, and Esc
cancels. Touching a row inserts it. Templates place the edit cursor inside the
first argument slot, for example `Simplify[]`, `D[, x]`, and `\frac{}{}`.

The catalog is data-driven and separate from rendering, so later Tensor/GR/QFT
heads can be added to the same registry without changing the modal controller.
Only commands already accepted by the current parser/evaluator are exposed as
CAS commands. LaTeX entries are chosen from the audited nMarkdown parser
surface.

## Bounds and verification

- no heap allocation is performed by catalog lookup or template insertion;
- insertion is transactional against the fixed cell buffer;
- the palette exposes no unsupported CAS command as runnable;
- unit tests cover category bounds, representative snippets, cursor placement,
  capacity failure, stale-result propagation, and Markdown insertion;
- host smoke tests cover opening the palette and inserting a template;
- the complete host suite and the pinned Ndless ARM build must pass before
  deployment.
