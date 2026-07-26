# Independent audit of `REFERENCE_CORPUS.md` and `TASK_CONTRACTS.md`

Date: 2026-07-26
Auditor: independent correction and physics-reference pass
Subject: branch `worktree-reference-corpus`, commit `b8ad096`
Method: re-derivation from primary artifacts, not review of the draft's prose

---

## 0. Verdict

The draft is **unusually sound**. Every quantitative claim I could re-derive
reproduced exactly, including all Zehn header fields across three binaries, all
derived RAM figures, five of five sampled file-group counts, every cited source
line, and every `ti-ce-giac` macro. I found **no arithmetic error** and **no
fabricated citation**. The `883,674 → 882,929` correction it makes against
`feasibility-2026-07-26.md` is itself correct.

It should be merged. The findings below are corrections and additions, not a
rejection. They fall into three classes:

- **One material misdiagnosis** (F1) that would send an implementation agent
  down the wrong path.
- **Four physics-notation corrections** (F2–F5) that change the Phase 2
  priority order. These are the substance of this pass: the draft's §6.1 is
  accurate about *what strings are absent* but wrong about *what that costs a
  physicist*.
- **Nine precision, scope, and presentation issues** (F6–F14).

A separate deliverable, [`PHYSICS_REFERENCE.md`](PHYSICS_REFERENCE.md),
addresses the corpus's largest omission: it contains no physics.

---

## 1. What I re-derived, and how

I did not read the draft's formulas and check them for self-consistency; that
only detects typos. I parsed the artifacts independently and compared at the
end. Scripts: [`audit/`](audit/).

**Zehn parsing.** I located the image by scanning for the `Zehn` signature and
selecting the candidate satisfying an invariant the draft never uses —
`zehn_offset + file_size == container_bytes`. This matters: in all three
binaries examined, the *first* `Zehn` occurrence is a decoy inside the PRG stub
(offset 712 in `khicas.tns`, 528 in `nmarkdown.tns`), and parsing it yields
garbage. Any tool written for **P0-4** must select by invariant, not by first
match. I then established the header field
order empirically from three binaries before consulting `zehn.h`, and it agrees:

```c
signature, version, file_size, reloc_count, flag_count, extra_size,
alloc_size, entry_offset      // 8 x u32, packed
```

**Confirmed exactly** — §3, all three binaries: `file_size`, `alloc_size`,
`reloc_count`, `flag_count`, `extra_size`, `EXECUTABLE_NAME`, zlib compression,
`NDLESS_VERSION_MIN`/`NDLESS_REVISION_MIN` absent on `khicas`/`luagiac` and
`31`/`2004` on `nmarkdown`, `USES_LCD_BLIT`/`RUNS_ON_HWW` = 1/1.

**Confirmed exactly** — §4: every cell of the budget table, both sums, the
1.98× / 2.07× / 1.68× expansions, the 33.7 % / 50.6 % shares, and the
882,929-byte headroom.

**Confirmed exactly** — SHA-256 of all three binaries, including the
"newly measured" `khicas.tns` digest `1d6f9dd8…b1f880`.

**Confirmed exactly** — §5.2 groups A (7/483/14,631), E (8/57,113/5,174,700),
R1 (4/7,844/351,925), R5 (1/7,097/545,418), H (3/129/3,622), and
`core.fpk` = 761,040 B.

**Confirmed exactly** — §6.1: 514 symbol entries, KaTeX commit
`2c6143a…fcc19`, `math_atoms.h:117-119` limits, `math_macros.cpp:190`,
`math_parser.cpp:796`. §6.2: all eleven macros, `#define std ustl` (line 60),
`SIZEOF_INT 3` (line 26), `PACKAGE_VERSION "1.4.9"` / `VERSION "1.4.9-57"`.
§7: exactly 28 `InputEventType` enumerators, the `InputEvent` struct as quoted,
`TouchSample` genuinely `private`.

**Confirmed exactly** — `zehn_loader.cpp` lines 20, 122, 138, 146 against
current `master`, and `remaining_mem = alloc_size − tables` at line 120.

---

## 2. Material finding

### F1 — §8 misdiagnoses a local egress block as an upstream outage

The draft's reachability *measurements* reproduce perfectly: all three Parisse
URLs fail with curl code 000, all three alternates return 200. But the
**diagnosis drawn from them is wrong.**

DNS resolution in this environment:

| Hostname | Resolves to | Actually reachable |
| --- | --- | ---: |
| `www-fourier.univ-grenoble-alpes.fr` | 198.18.1.220 | no |
| `wfourier.u-ga.fr` | 198.18.2.15 | no |
| **`github.com`** | **198.18.0.187** | **yes** |

`198.18.0.0/15` is the RFC 2544 benchmarking range. No real web host lives
there. Every hostname — including one that works perfectly — is being mapped to
a synthetic address by a local proxy/VPN resolver. The Parisse hosts fail
because this environment's egress policy has no route for them, **not** because
the upstream is down.

The draft concludes "Both hostnames recorded in the project documents are
currently unusable", "the upstream has no mirror of its own", and "A project
whose only CAS supply line is a single unreachable university web host is one
outage away from being unable to reproduce its own release." That reasoning
treats a sandbox restriction as evidence about the internet.

**Why this matters:** P0-3 is BLOCKING. An agent reading §8 may conclude the
Giac upstream is dead and commit to a Debian `+dfsg` repack — a source that has
had components *removed* — when a developer on an unrestricted network can very
likely download the canonical tarball directly.

**Correction.** Keep the mirroring action; it is good practice regardless.
Replace the diagnosis with: *"Not reachable from the audit environment, whose
resolver maps all external hostnames into 198.18.0.0/15. Upstream availability
is therefore untested. Re-probe from an unrestricted network before treating
the canonical source as lost."* P0-3 should require that re-probe as step one,
and prefer the canonical tarball over a `+dfsg` repack when it is reachable.

---

## 3. Physics-notation corrections (§6.1, P2-1)

The draft's absent/present lists are factually correct as string lookups. The
*physics consequences* attached to them are not. Corrected priority order is in
§3.5.

### F2 — "the single most important gap" is already supported

The draft calls `\tensor`/`\prescript` "**the single most important gap for
Phase 2**" and makes it P2-1 item 1, asserting "Phase 2 output is unreadable
without it."

The standard idiom physicists actually write — and that xAct emits — is not
`\tensor`. It is an empty group as a script carrier:

```latex
R^a{}_{bcd}        \Gamma^\lambda{}_{\mu\nu}        F^{\mu}{}_{\nu}
```

That already parses in the pinned engine. The path is three hops, all verified:

1. `parse_primary` dispatches `BeginGroup` to `parse_group`
   (`math_parser.cpp:811-812`).
2. `parse_group` on `{}` runs `parse_row`, whose loop stops immediately on
   `EndGroup` and still emits a valid `Row` node with no children
   (`math_parser.cpp:295-314`); `parse_group` then reclasses it as
   `AtomClass::Ordinary`.
3. `parse_atom` attaches sub/superscripts to *any* primary, including that
   empty row (`math_parser.cpp:377-412`).

So `R^a{}_{bcd}` yields `Scripts(R, sup=a)` followed by
`Scripts(∅, sub=bcd)` — staggered indices, structurally correct, today.

What is unverified is **layout quality**: whether an empty `Row` base gets zero
advance width and whether the second cluster's scripts sit at the right
vertical offsets. That is a tuning-and-fixture problem, not a parser feature.

**Correction.** Reclassify P2-1 item 1 from *"implement `\tensor`/`\prescript`"*
to *"add a golden fixture for `R^a{}_{bcd}`; verify and tune empty-base script
layout."* Keep `\tensor` as optional input ergonomics, not a blocker. This is
the difference between a layout regression test and a new parser subsystem.

### F3 — the `\overset` rationale is refuted by the accent table

The draft justifies `\overset \underset \stackrel` with the QFT bidirectional
derivative, `\overset{\leftrightarrow}{\partial}`.

`\overleftrightarrow` is present as a first-class accent. The parser's accent
array holds **21** entries, including six over/under arrows the draft's
"present" inventory omits entirely:

```
overleftarrow  overrightarrow  overleftrightarrow
underleftarrow underrightarrow underleftrightarrow
```

`\overleftrightarrow{\partial}` therefore renders today, and it is the more
conventional spelling anyway.

**Correction.** Add the six arrow accents to the "present" list. Demote
`\overset` from item 2 to the general-mechanism tier, justified as *generic
over/under annotation* — not as a blocker for the bidirectional derivative.

### F4 — a real gap the draft missed: no math spacing macros exist at all

Absent from **every** file in `src/math/` and `include/nmarkdown/math/`, and
from the symbol table (which contains no punctuation-named entry):

```
\,   \!   \;   \:   \thinspace  \negthinspace  \hspace  \mspace  \phantom
```

The draft lists `\quad \qquad \space` as present and never notices the thin,
medium, thick, and negative spacing family is missing.

This is not cosmetic:

- `\,` is among the highest-frequency commands in all physics LaTeX —
  `\int f(x)\,dx`, `\d^4x\,\mathcal{L}`. Any imported or exported expression
  will contain it. Today it is silently unrecognized.
- `\!` is the conventional manual tool for tightening index spacing — i.e. it
  partially substitutes for the very staggered-index problem F2 is about.

**Correction.** Add the spacing family to §6.1's absent table and to P2-1,
ranked **above** `\substack` and `\binom`. `\,` should arguably be the first
thing implemented in P2-1: it is a one-line lexer/symbol addition with the
broadest blast radius of anything on the list.

### F5 — `\binom` is justified by the wrong physics

The draft motivates `\binom \genfrac \atop` with "combinatorial and
**Clebsch-Gordan** coefficients."

Clebsch–Gordan coefficients are not binomials. They are written either as
brackets, `\langle j_1 m_1 j_2 m_2 | J M \rangle` — `\langle`/`\rangle` are
present — or as Wigner symbols, which are matrix environments:

| Symbol | Delimiter | Environment | Status |
| --- | --- | --- | --- |
| Wigner 3j | round | `pmatrix` | **present** |
| Wigner 6j | curly | `Bmatrix` | **present** |
| Wigner 9j | curly | `Bmatrix` | **present** |

So the entire Clebsch–Gordan / Wigner notation set is **already supported**.
`\binom` is for genuine binomial coefficients and belongs at low priority.

**Correction.** Fix the rationale; drop the CG claim; keep `\binom` at the
bottom of P2-1.

### F5b — `\ket`/`\bra`/`\braket` absent, unremarked

`physics.sty`'s bra-ket macros are absent from both surfaces. Mitigated —
`|\psi\rangle` and `\left\langle\phi\right|` work — so priority is low, but
`SCIENTIFIC_SCOPE.md` §5 promises "bra-ket notation" without saying which
spelling is canonical. Phase 4 should fix the spelling before writing fixtures.

### 3.5 Corrected P2-1 priority order

| # | Item | Change from draft |
| ---: | --- | --- |
| 1 | `\,` `\!` `\;` `\:` spacing family | **new** (F4) |
| 2 | Golden fixture + layout tuning for `R^a{}_{bcd}` empty-base scripts | was "implement `\tensor`" (F2) |
| 3 | `\limits`/`\nolimits` | unchanged |
| 4 | `\slashed` (or adopt `\not` as canonical) | unchanged |
| 5 | `\overset`/`\underset`/`\stackrel` as generic annotation | demoted (F3) |
| 6 | `\phantom`, `\substack`, `\binom` | unchanged |
| 7 | `\tensor`/`\prescript` as input sugar | demoted from #1 (F2) |
| 8 | Parameterized macros | unchanged |

---

## 4. Precision, scope, and presentation findings

### F6 — the peak-RAM formula omits two live allocations

`zehn_load` allocates **five** buffers that are simultaneously live during
`uncompress()`, not three. `Storage<Zehn_flag> flags` and
`Storage<uint8_t> extra_data` are constructed at `zehn_loader.cpp:102-104` and
destroyed only at function exit — they outlive the decompression.

True peak = `resident + zlib_buf + 4·reloc_count + 4·flag_count + extra_size`.

| Artifact | Draft peak | Corrected peak | Δ |
| --- | ---: | ---: | ---: |
| `khicas.tns` | 12,700,111 | 12,700,151 | +40 |
| `nmarkdown.tns` | 3,386,780 | 3,386,860 | +80 |

Numerically irrelevant. It matters only because **P0-4's acceptance test
demands the tool "reproduce these exact values"** — so the spec and the tool
must agree on one definition. Pick the five-term form; it is the true one.

### F7 — new evidence: the data/BSS split (supports the draft)

The draft asserts `resident = alloc_size − tables` but never decompresses to
check. I did:

| Artifact | Claimed resident | Inflated payload | BSS tail |
| --- | ---: | ---: | ---: |
| `khicas.tns` | 8,574,520 | 8,038,944 | 535,576 |
| `nmarkdown.tns` | 2,130,052 | 2,127,296 | 2,756 |

The gap is the zero-filled region from
`std::fill(base + dest_len, base + remaining_mem, 0)` (`zehn_loader.cpp:152`).
This **confirms** `alloc_size − tables` as the correct RAM *reservation*, and
hands the project a free extra metric. P0-4's tool should emit the data/BSS
split — `khicas.tns` carrying 535 KB of BSS is a real datum for a build that
will link that same library.

### F8 — summing `execmem_alloc` and `malloc` into one budget is unverified

`execmem_alloc` (the image, line 122) and `malloc` (relocs, flags, extra,
compressed buffer) draw from **different Ndless pools**. The draft adds them
into a single "peak" and compares against the single free-RAM figure of
31,805,820 B from `feasibility-2026-07-26.md`, which was measured as one
number.

That is probably fine in total-RAM terms, but it is an assumption, and the
≤ 12 MiB / ≤ 20 MiB gates in P0-4 are built on it. Add it to §10 as a new
unverified claim to be settled by the P0-5 device baseline.

### F9 — §2.2 and §3 contradict each other on HW-W

§2.2 quotes `README_52_53`: *"It will not work on TI Nspire CX HW-W."*
§3 reports `RUNS_ON_HWW = 1` for `khicas.tns`. I confirm the flag is 1.

Both are true and they are about different things — the Zehn flag declares the
binary handles the rotated 240×320 LCD (`zehn.h`: "Whether the executable
support the 90° rotated 240x320 LCD on HW-W"), while the README is about
OS/hardware support for that build. The document presents both without
reconciling them.

Not blocking — Phy-nspire targets CX II only — but it should be stated, or a
reader will mis-scope device testing.

### F10 — "does not provide editable text input" is imprecise

`InputEventType::TextInput` and `Backspace` exist, and `TextInput` carries the
codepoint in `amount` (`input_ndless.cpp:287`). The *transport* for text entry
already exists; the editing model does not. This slightly narrows P1-2 and
should be stated so the contract is not overscoped.

### F11 — §5.2's byte column is source bytes, unlabelled, next to a RAM budget

§5.2's "Bytes" column is **source text**, and it sits immediately after §4, a
table of flash and RAM bytes. Group E's 5,174,700 is the size of generated C++
arrays; its actual embedded payload is dominated by `core.fpk` at 761,040 B
(`core_font_pack.cpp` is a ~6.3×-inflated hexdump of exactly that file).

Nothing in §5.2 may be added to anything in §4. Label the column
**"source bytes"**.

### F12 — the "70,495 lines liftable" headline contradicts its own row

The total includes group E (57,113 lines), which the same row classifies
**"Regenerate, do not copy"**. The draft's own following sentence gives the
honest figure. Lead with **13,382**; report 70,495 only as "lines under
`src/`+`include/` in lifted groups, of which 57,113 are regenerated."

### F13 — `reloc_count` is not purely relocations

Reloc type mix (relevant to P0-4's launch-latency metric):

| Artifact | ADD_BASE | ADD_BASE_GOT | SET_ZERO | FILE_COMPRESSED |
| --- | ---: | ---: | ---: | ---: |
| `khicas.tns` | 83,420 | 1 | 2 | 1 |
| `nmarkdown.tns` | 7,483 | 1 | 0 | 1 |

Entry 0 is the `FILE_COMPRESSED` marker, not a relocation — compression is
signalled through the reloc table, not a flag. Cosmetic, but a size report that
prints "83,424 relocations" is off by one and should say so.

### F14 — MB/MiB is used inconsistently across documents

`README.md` says "5–6 MB"; `feasibility` says "below 6 MiB" and "a strict
all-files 6 MB budget"; both compute against 6,291,456 = 6 MiB. The draft
inherits the ambiguity. State once, in `README.md`: **the ceiling is 6 MiB =
6,291,456 bytes of compressed flash.**

---

## 5. Recommended amendments

To `REFERENCE_CORPUS.md`:

1. Rewrite §8's conclusion per **F1**; add the DNS evidence.
2. Add F4's spacing family to §6.1's absent table; add the six arrow accents to
   the present list (**F3**); fix the `\binom` rationale (**F5**).
3. Rewrite the `\tensor` entry per **F2**, and re-rank P2-1 per §3.5.
4. Adopt the five-term peak formula (**F6**) and add the data/BSS split
   (**F7**) to §3.
5. Add **F8** to §10 as U8; add **F9** as a reconciliation note in §2.2.
6. Relabel §5.2's byte column (**F11**); fix the liftable headline (**F12**).

To `TASK_CONTRACTS.md`:

7. P0-3: require re-probing from an unrestricted network before choosing a
   `+dfsg` repack (**F1**).
8. P0-4: specify invariant-based Zehn header location (§1 of this audit),
   the five-term peak, and data/BSS output (**F6**, **F7**).
9. P2-1: replace the priority list with §3.5.
10. P1-2: note that `TextInput`/`Backspace` transport already exists (**F10**).

New, and in my view the highest-value follow-up: the corpus contains **no
physics**. `ROADMAP.md` requires "a comparison corpus derived from xAct
examples" (Phase 2), "known Schwarzschild/Kerr identities" (Phase 3), and "a
small FeynCalc-compatible golden corpus" (Phase 5). None of that exists, and
none of it is blocked on a toolchain, a device, or network access — it can be
written and reviewed today. See [`PHYSICS_REFERENCE.md`](PHYSICS_REFERENCE.md).
