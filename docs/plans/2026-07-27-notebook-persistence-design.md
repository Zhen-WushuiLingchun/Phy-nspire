# Notebook persistence design

Date: 2026-07-27
Status: accepted implementation baseline

## User contract

- launching `phy-nspire.tns` creates a clean, empty notebook;
- `Save`, `Open`, and `New` are available through a pointer-driven file menu;
- the only user-facing notebook directory is
  `/documents/phy-nspire/notebooks`;
- first save proposes the first free `Notebook-NNN.tns` name;
- later saves replace the same file;
- Open lists only valid `.tns` names in that directory;
- a modified document is visibly marked and must not be discarded silently;
- saving is manual in this version, with a dirty-state prompt before New,
  Open, or Exit.

## Alternatives considered

1. **One autosave file.** Smallest UI, but it cannot organize examples or
   several physics calculations and makes accidental edits immediately
   destructive.
2. **One versioned `.tns` document per notebook — selected.** It fits the TI
   Documents model, gives a bounded file picker, and keeps the installed
   program independent of user data.
3. **One directory per notebook.** Useful for external assets, but excessive
   for the current 12-cell model. A future document version may add external
   asset references without changing the directory contract.

## Document format

The format is binary, explicitly little-endian, length-prefixed, and contains
no native C structs. Version 1 has a 32-byte header:

| Field | Bytes | Meaning |
| --- | ---: | --- |
| magic | 8 | `PHYNB001` |
| version | 2 | `1` |
| header size | 2 | `32` |
| payload bytes | 4 | bounded bytes after the header |
| cell count | 2 | at most `PHY_NOTEBOOK_MAX_CELLS` |
| reserved | 2 | zero |
| next execution | 4 | next `In[n]` label |
| payload CRC-32 | 4 | IEEE CRC-32 |
| flags | 4 | zero in version 1 |

Each cell record is length-prefixed and carries its kind, stale flag, typed
status, execution label, output owner, editable primary/secondary text, and an
optional serialized typed-IR expression. Input cells retain both reader-facing
source and the last successful IR. Output cells retain cached IR, so reopening
a future expensive curvature calculation does not require recomputation.

Loading is transactional: parse into a fresh notebook and IR context, validate
every length, enum, owner relation, IR expression, CRC, and trailing byte, then
publish the new object. Failure destroys the temporary object and leaves the
current notebook untouched.

## Storage boundary

Portable UI/model code calls a small storage contract:

- ensure the notebook directory exists;
- list bounded `.tns` names;
- read one bounded document;
- atomically write one document.

The Ndless backend uses `/documents/phy-nspire/notebooks`. It writes a
temporary file, closes it, moves the previous destination to a backup, promotes
the temporary file, restores the backup on failure, and removes the backup only
after success. Names are ASCII, traversal-free, and must end in `.tns`.

The host backend uses a deterministic in-memory directory with explicit test
reset/seed hooks. This lets lifecycle tests exercise Save/Open without touching
the developer's real files.

## UI state

The application owns the current filename and one of these views:

- notebook;
- file menu (`New`, `Save`, `Open`);
- Save As name editor;
- Open file list;
- dirty-document confirmation.

The notebook model owns content dirty state. Selection and cursor movement are
not modifications; adding/editing/evaluating cells is. A successful save or
load marks the model clean. The title bar shows `*` while dirty.

## Verification

- byte-exact serialize/load round trip for Markdown, input, output, error, stale
  output, and execution labels;
- rejection of truncated records, bad lengths, bad CRC, bad enum/status,
  invalid output owners, malformed IR, wrong versions, and trailing bytes;
- deterministic host storage listing, name filtering, capacity limits,
  replacement, and failed-read behavior;
- app flow tests for blank launch, first Save As, subsequent Save, Open,
  discard/cancel prompts, and corrupted-file errors;
- strict GCC and MSVC suites, ASan/UBSan/leak detection, ARM build, symbol
  audit, byte-for-byte deployment, and physical CX II acceptance.
