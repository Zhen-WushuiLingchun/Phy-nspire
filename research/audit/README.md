# Audit scripts

Verification tooling for
[`../AUDIT-2026-07-26-reference-corpus.md`](../AUDIT-2026-07-26-reference-corpus.md).
Python 3 only — no build, no device, no network (except where noted).

These are *audit* instruments, deliberately written to re-derive results from
primary artifacts rather than to re-check another document's arithmetic. They
are not the deliverable of contract P0-4, but `zehn_audit.py` implements most
of its specified metric set and can seed it.

## `zehn_audit.py`

Parses `.tns` containers and reports Zehn header fields plus the derived load
budget. Locates the Zehn image by scanning for the signature and selecting the
candidate satisfying `zehn_offset + file_size == container_bytes` — **not** by
first match, because every `.tns` contains a decoy `Zehn` string inside its PRG
stub (offset 712 in `khicas.tns`). It also decompresses the payload to expose
the initialized-data / BSS split, which the flash and RAM figures alone hide.

```sh
python zehn_audit.py path/to/khicas.tns path/to/nmarkdown.tns
```

## `zehn_flags.py`

Decodes the Zehn flag and relocation tables: `EXECUTABLE_NAME`, the Ndless
minimum version/revision, `RUNS_ON_HWW`, `USES_LCD_BLIT`, and the relocation
type histogram (which shows that entry 0 is the `FILE_COMPRESSED` marker rather
than a relocation).

```sh
python zehn_flags.py path/to/khicas.tns
```

## `notation_probe.py`

Physics-notation coverage probe. Checks a curated command list, grouped by
physics domain, against two independent surfaces of the pinned nMarkdown math
engine — the no-argument symbol catalog and the parser's structural commands —
and reports which phase each gap blocks.

```sh
python notation_probe.py path/to/nMarkdown
```

Findings F2–F5 of the audit come from this probe combined with reading
`math_parser.cpp`; it is worth re-running against any updated upstream pin
before Phase 2 planning.
