# Third-party notices

Phy-nspire is distributed under the GNU General Public License version 3. See
[`LICENSE`](LICENSE).

## nMarkdown

The native mathematical typesetting subsystem is reused from
[KaraRyougi/nMarkdown](https://github.com/KaraRyougi/nMarkdown), pinned as the
`third_party/nmarkdown` Git submodule at commit
`936b04854fc0838de9986b4bfee66a4da9db6166`. nMarkdown is distributed under
GPL-3.0; its license is retained at
[`third_party/nmarkdown/LICENSE`](third_party/nmarkdown/LICENSE).

Phy-nspire compiles only nMarkdown's bounded formula parser, OpenType MATH
layout, embedded core font pack, RGB565 renderer, and required text stack. It
does not compile nMarkdown's reader, browser, search, or MD4C Markdown parser.

## Transitive notices

The selected nMarkdown math slice includes FreeType, HarfBuzz, a symbol table
derived from KaTeX, DejaVu Sans/Mono, Latin Modern Math, and Unicode data.
Their exact versions, provenance, and license texts are retained in
[`third_party/nmarkdown/THIRD_PARTY_NOTICES.md`](third_party/nmarkdown/THIRD_PARTY_NOTICES.md)
and the files linked from that notice.
