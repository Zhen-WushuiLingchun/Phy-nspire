# nMarkdown math-engine adaptation audit

Date: 2026-07-27

Upstream: `KaraRyougi/nMarkdown`
Audited commit: `936b04854fc0838de9986b4bfee66a4da9db6166`

## Conclusion

Use the nMarkdown **math subsystem**, not the whole reader application, behind
a narrow C++ bridge. It is much more complete than a small new parser and
still fits the Phy-nspire package budget. Keep Phy-nspire's notebook model,
editor, storage, typed IR, CAS, and UI shell. Reuse nMarkdown's bounded formula
lexer/parser, OpenType MATH layout, Latin Modern Math face, shaping,
rasterization, and local recovery.

The owner selected GPL-3.0 on 2026-07-27. The legal gate is resolved:
Phy-nspire now has a top-level GPL-3.0 `LICENSE`, retains upstream and
transitive notices, and pins nMarkdown as a Git submodule.

## Reproduced device-size evidence

Both probes used the same local Ndless/ARM toolchain as Phy-nspire. The
upstream Makefile needed only the project's existing weak `_init`/`_fini`
compatibility shim for the current ARM GNU toolchain.

| Artifact | Contents | `.tns` bytes |
|---|---|---:|
| upstream `nmarkdown.tns` | complete reader | 1,270,598 |
| `math-adapter-probe.tns` | math/text/render dependency closure only | 1,017,885 |
| `phy-nspire.tns` before integration | notebook/CAS/persistence | 84,384 |
| integrated `phy-nspire.tns` | product plus selected math slice | 1,048,076 |

The math-only probe linked and laid out:

```latex
\frac{1}{2}g_{\mu\nu}+\sqrt{p^\alpha p_\alpha}
```

Its ELF sections were:

| Section | Bytes |
|---|---:|
| `.text` | 718,580 |
| `.data` | 987,084 |
| `.bss` | 156,300 |

The probe's `.bss` includes its own 153,600-byte RGB565 framebuffer.
Phy-nspire already owns that framebuffer, so integration does not need a
second one. Most static data is the embedded font pack. Glyph atlas pages are
lazy rather than allocated at startup.

## Supported formula language

The authoritative direct symbol table has exactly **514 entries**, with no
duplicates against the structural command set:

| TeX atom class | Entries |
|---|---:|
| Ordinary | 116 |
| Operator | 72 |
| Binary | 67 |
| Relation | 223 |
| Opening | 12 |
| Closing | 12 |
| Punctuation | 2 |
| Inner | 10 |

Source inspection finds another **66 distinct structural, style, accent,
spacing, and macro command names** outside that table. Therefore at least 580
`\command` names are accepted, plus eleven environments:

`matrix`, `pmatrix`, `bmatrix`, `Bmatrix`, `vmatrix`, `Vmatrix`, `cases`,
`array`, `aligned`, `align`, and `align*`.

Important supported structures include fractions and continued fractions,
indexed radicals, scripts, TeX atom spacing, scalable delimiters, 21 accent
forms, math variants/styles, display limits, annotations/tags, bounded local
macros, matrices, cases, arrays, and aligned equations. Unsupported or
malformed formulas become local error boxes rather than aborting the notebook.

The number 514 is a command-entry count, not a unique-glyph count or a claim
to implement all of TeX/KaTeX.

## Required dependency closure

The tested math-only closure consists of:

- `src/math/{math_atoms,math_lexer,math_macros,math_parser,math_layout,math_system}`;
- `src/generated/{core_font_pack,core_math_font,unicode_tables}`;
- the font-pack, font, glyph-cache, HarfBuzz shaper, renderer, compositor, and
  `TextSystem` implementation;
- RGB565 surface/primitives;
- UTF-8/Unicode helpers;
- the upstream minimal FreeType modules;
- upstream HarfBuzz built with `HB_TINY=1` and
  `NMARKDOWN_HARFBUZZ_MATH=1`.

MD4C, the nMarkdown viewer, document browser, search, legacy text encodings,
pagination, settings, and platform input/display adapters are not required by
the math bridge.

## Phy-nspire bridge

The implemented C++17 translation unit exposes this C ABI:

```text
phy_formula_initialize()
phy_formula_measure_latex(source, style, size, width, metrics)
phy_formula_draw_latex(surface, source, style, size, width, baseline, clip)
phy_formula_shutdown()
```

`phy_formula_measure_latex` and `phy_formula_draw_latex` use nMarkdown's parser
for raw Markdown formulas and its internal bounded layout cache.
The planned `phy_formula_layout_ir` will construct an nMarkdown `MathTree`
directly from Phy-nspire typed IR. That second adapter is not implemented yet
and remains the next renderer-unification task. It must not flatten CAS output
to LaTeX and parse it back.

The existing `phy_surface` is wrapped zero-copy as nMarkdown `Surface565`;
both use contiguous 320 x 240 RGB565 storage. The bridge owns one long-lived
`TextSystem` and `MathSystem`. Formula cache entries should be reduced from 64
to a notebook-appropriate bounded value after real-device profiling. The
glyph-cache logical ceiling should likewise be reduced from the reader's
CJK-oriented 128 pages because Phy-nspire embeds only the math use case.

The editor and document codec continue to store raw Markdown/LaTeX source.
Inline `$...$` / `\(...\)` and display `$$...$$` / `\[...\]` delimiters are
recognized at the Markdown-cell layer and select Text or Display math style.

## Build integration

- keep the CAS and notebook portable core in C11;
- compile the bridge and vendored nMarkdown slice as C++17 with exceptions and
  no RTTI, matching upstream;
- link the final product with `nspire-g++`;
- retain `-ffunction-sections -fdata-sections` and `--gc-sections`;
- keep the existing weak `_init`/`_fini` compatibility definitions;
- enable Zehn compression;
- retain upstream font and third-party notices.

## License decision

nMarkdown is GPL-3.0. Copying or modifying its math implementation in the
distributed Phy-nspire program requires the combined work to be distributed
under GPL-3.0-compatible terms with corresponding source and notices. Its
vendored MD4C, FreeType, HarfBuzz, KaTeX-derived symbol data, and fonts have
their own permissive/font notices, but those do not remove the top-level GPL
obligation.

The project adopted option 1: GPL-3.0 with a pinned, documented math slice.
`third_party/nmarkdown` retains the full corresponding upstream source and
notices, while the build compiles only the dependency closure listed above.
