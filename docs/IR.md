# Typed expression IR

The IR is the layer every physics module computes on. It is defined by
`include/phy/ir.h` and implemented in `src/ir`. Nothing in it knows that a CAS
backend exists, which is what lets a Giac call be replaced by a native rewrite
one operation at a time.

This document covers the design decisions and the text format. The header is
the API reference and is not repeated here.

## What the IR guarantees

**Interning.** Structurally identical subtrees are one node. Equality is
`a == b` on a `phy_ir_ref`, not a tree walk, and a shared subterm costs its
nodes once no matter how many expressions mention it.

**Canonical order.** Operands of a commutative node are stored sorted, so
`x*y` and `y*x` are the same node rather than two nodes a simplifier has to
reconcile later.

**Explicit noncommutativity.** `PHY_IR_NCMUL` and `PHY_IR_WEDGE` preserve
operand order. Nothing commutes by accident: a product either is declared
commutative by its kind or it is not.

**Bounded construction.** Node count, depth, operands per node, and total
bytes are all checked, and exceeding any of them is a typed status, not a
crash or a silent truncation.

**Backend-neutral serialization.** An expression round-trips through text
into a different context as the same structure.

## Refs are context-local

**A `phy_ir_ref` means nothing outside the context that issued it.** It is a
pool index assigned in creation order. Two contexts holding the same
expression will generally give it different refs, and the same number will
generally name unrelated nodes in each. The same applies to `phy_ir_symbol`.

So comparing refs across contexts is not a weak test, it is a meaningless one
— and it can silently pass, because small integers collide readily. `x` in one
context and `y` in another are both quite likely to be ref 1.

Within one context, interning makes ref equality exact structural equality;
that is what `phy_ir_equal` is for. Across contexts, use one of:

| route            | strength                                        |
| ---------------- | ----------------------------------------------- |
| `phy_ir_hash`    | equal structure ⇒ equal hash, in any context. 32-bit, so equality is strong evidence, not proof. |
| `phy_ir_write`   | compare the serialized text. Exact.              |
| `phy_ir_read`    | rebuild into the target context, then compare refs there. Exact, and gives you a usable node. |

`tests/test_ir.c` pins the hazard directly: it builds two contexts where equal
structure lands on *different* refs and different structure lands on the
*same* ref, then shows the three supported routes agreeing with the structure
rather than the numbers.

## Representation

A `phy_ir_ref` is an index into context-owned pools, not a pointer. Pools are
reallocated as they grow, so a ref stays valid for the life of the context
while a node pointer does not. No API hands out node pointers.

A node is 32 bytes, asserted at compile time in `src/ir/ir.c`:

| field         | width | meaning                                         |
| ------------- | ----- | ----------------------------------------------- |
| `kind`        | u8    | `phy_ir_kind`                                    |
| `aux`         | u8    | index variance, or a `phy_status` for error nodes |
| `depth`       | u16   | 1 for atoms; bounded by `max_depth`              |
| `child_count` | u16   | bounded by `max_children`                        |
| `hash`        | u32   | structural, portable across contexts             |
| `intern_next` | u32   | hash bucket chain                                |
| `head`        | u32   | symbol id, or `PHY_IR_NO_SYMBOL`                 |
| union         | 8     | children offset, rational index, index-space symbol, `int64`, `double` |

All memory is taken through `phy_alloc`/`phy_free`, so IR usage shows up in
`phy_telemetry` alongside everything else, and is additionally capped by
`limits.max_bytes`.

The byte ceiling is tested against the *transient peak* rather than the final
size. There is no `phy_realloc`, so growing a pool holds the old and new
buffers at once; a budget that only accounted for the final size would let a
context that nominally fits still fail to allocate.

## Node kinds

Behaviour is driven by a descriptor table, `kKindInfo` in `src/ir/ir.c`, not by
switch statements spread across the implementation. Ordering, hashing,
interning, and serialization all read it. Adding a kind is one table row, plus
a builder where the shape is unusual.

Atoms are `INTEGER`, `RATIONAL`, `REAL`, `SYMBOL`, `INDEX`, and `ERROR`.
Scalar structure is `ADD`, `MUL`, `NCMUL`, `POW`, `EQUATION`, and `FUNCTION`.
Physics structure is `TENSOR`, `OPERATOR`, `DERIVATIVE`, and `WEDGE`.

Gamma matrices and SU(N) generators are deliberately **not** separate kinds.
Both are headed noncommutative objects carrying indices, which is exactly
`PHY_IR_OPERATOR`; a kind per physics object would grow the table without
adding structure. The distinction between a gamma matrix and a generator lives
in the head symbol and its declared properties, where the rewriter can reach
it.

Tensor slots are **not** sorted. Slot position carries meaning, so
`g[mu,nu]` and `g[nu,mu]` are different expressions until a declared symmetry
says otherwise — and applying that symmetry is canonicalization, which is
Phase 2 work in the rewriter, not construction.

## Numbers

Integers are exact `int64`. Rationals are exact `int64` pairs, reduced, with
the sign on the numerator and a denominator greater than one; a unit
denominator yields an integer instead, so no rational ever equals an integer.
That normalization is canonical form, not simplification — the IR performs no
arithmetic beyond it.

Reals are IEEE-754 `binary64`, for numeric fallback only. Infinities and NaN
are rejected as `PHY_ERR_DOMAIN`, and negative zero is folded to positive zero
so the two cannot intern apart. The checks read the bit pattern rather than
calling `isfinite`, which keeps libm out of the device binary.

Arbitrary-precision integers, which `docs/SCIENTIFIC_SCOPE.md` calls for, are
**not** implemented. Construction that would leave the `int64` range reports
`PHY_ERR_OVERFLOW` rather than wrapping. Adding a bignum is a new atom kind and
a new payload arm; nothing in the ordering or interning machinery assumes 64
bits beyond the two comparison paths in `src/ir/order.c`.

## Canonical order

`phy_ir_compare` is a total order over every node in a context. It depends
only on structure — kinds, symbol name bytes, numeric values, children — and
never on node refs or symbol ids, both of which record creation order. Two
contexts that reached the same expression by different routes agree.

Ranks group kinds: exact numbers, then reals, then symbols, indices, error
values, and finally compounds in table order. Exact and inexact numbers are
deliberately separated rather than compared numerically. Comparing an `int64`
against a `double` exactly would need arithmetic wider than the device offers,
and an approximate comparison would be an *intransitive* comparator — which
does not fail loudly, it just silently produces a non-canonical order.

Exact numbers are compared by cross-multiplication in 128 bits, assembled from
32-bit pieces in `src/ir/order.c` because the device is 32-bit and has no
`__int128`.

Sorting is heapsort: `O(n log n)` unconditionally, iterative, and needs no
scratch buffer. The term limit permits several thousand operands, which rules
out insertion sort's quadratic worst case, and quicksort's worst case is
reachable from user input.

## Assumptions and symmetries

Assumptions are declared properties of a symbol, accumulated as a bit set.
The IR rejects only what is self-contradictory on its face — positive and
negative together. Deciding whether an assumption *holds* of an expression is
the rewriter's job.

Slot symmetries are declared per head symbol as normalized `(low, high)`
pairs. The IR stores and reports them; it does not yet rewrite with them.
Canonicalizing indices under a symmetry group, including the sign bookkeeping
antisymmetry needs, is Phase 2 work.

## Errors

Errors are typed values. `phy_status` gained the expression-layer categories
from `docs/ARCHITECTURE.md`: parse, type, domain, assumption, unsupported,
overflow, timeout, the three limit categories, backend, and corrupt-document.
They are appended and never reordered, because the serializer writes status
names into saved documents.

Builders return `PHY_IR_NULL` on failure and record a sticky error, so a
caller may build a whole expression and check `phy_ir_last_error` once. A
`PHY_IR_NULL` operand propagates as `PHY_ERR_INVALID_ARGUMENT` rather than
producing a node with a hole in it.

`PHY_IR_ERROR` additionally carries a status *as a value*, which is how a
failed cell survives a save and reopen without invalidating the document.

### Construction unwinds; parsing does not

These are two different guarantees and it is worth being precise about which
is which.

**A failed construction unwinds.** Building a node appends its payload — a
name, a rational, a run of child slots — to a pool *before* pushing the entry
that owns it. If that push is the allocation that hits the ceiling, the
payload is rolled back. Only the counts are rewound; capacity keeps its peak,
which is correct, since that memory is already charged and reusable.

`phy_ir_validate` is how this is checked rather than asserted. It verifies
that every byte, child slot, and rational the pools hold is claimed by exactly
one live symbol or node, and that everything published is reachable from its
intern table — no orphans, nothing lost. The tests sweep an injected
allocation failure across every allocation a workload makes and validate after
each, which is the only way to reach these paths: pool growth is geometric, so
squeezing a real budget always breaks at the same first allocation and never
part-way through a construction.

**A failed parse does not unwind.** Reading builds as it goes, so a parse that
fails part-way leaves everything it already interned — symbols, and the nodes
of any complete subexpression — in the context. The same is true of a
declaration stream whose fifth entry is malformed: the first four are applied
and stay applied, and assumptions cannot be un-declared.

Those entries are well-formed and reachable; nothing is corrupted and
`phy_ir_validate` still passes. But nothing references them, and the IR has no
reference counting or collection, so they persist until the context is
destroyed. **This is a memory concern, not a correctness one:** a caller that
retries a malformed document repeatedly against one long-lived context
accumulates dead nodes and can eventually reach `PHY_ERR_MEMORY_LIMIT`.

The safe pattern for loading a document is therefore:

1. create a fresh context for the load;
2. read declarations and cells into it;
3. on success, publish that context and retire the previous one;
4. on failure, destroy it — which reclaims the partial parse in full.

The test suite pins both halves: that the prefix really does survive, and that
destroying a disposable context returns every byte to `phy_telemetry`.

Making a failed read self-cleaning would need either a transaction mark across
every pool and both intern tables, or collection. Both are real designs;
neither is in this layer, and choosing between them is not a decision to make
as a side effect of a bug fix.

## Text format

A small S-expression grammar. It carries structure only; symbol declarations
are written separately, because a symbol is shared by every expression that
mentions it.

```
expression  := integer | symbol | list
integer     := "-"? digit+
symbol      := bare | "|" escaped "|"
list        := "(" form ")"

form        := "rat" integer integer         exact rational
             | "real" hex64                  IEEE-754 bit pattern
             | "idx" symbol ("up" | "dn") [symbol]
                                               index with variance and optional space
             | "err" status-name             error value
             | "+"     expression+           commutative sum
             | "*"     expression+           commutative product
             | "nc*"   expression+           noncommutative product
             | "wedge" expression+           exterior product
             | "^"     expression expression base, exponent
             | "="     expression expression lhs, rhs
             | "fn"     symbol expression*   function application
             | "tensor" symbol index*        tensor
             | "op"     symbol expression*   operator
             | "d"      expression variable+ derivative
```

Names stay bare when they read back as one token; bytes above `0x7f` count as
bare, so Greek index names survive unquoted and saved documents stay readable.
Anything else is `|`-quoted with backslash escapes.

Declarations are a separate stream:

```
declaration := "(" "declare" symbol assumption* symmetry* ")"
assumption  := "real" | "positive" | "negative" | "integer"
             | "nonzero" | "constant" | "noncommutative"
symmetry    := "(" ("sym" | "asym") slot slot ")"
```

Symmetries are emitted in slot order rather than storage order, so
`write(read(text))` reproduces `text`. A document format that drifted every
time it was opened and saved would not be usable.

### Why reals are bit patterns

`(real 0x3ff0000000000000)` is not readable, and that is a deliberate trade.
A save format has to round-trip exactly. Decimal round-tripping and hex floats
both reach for `strtod` and the float formatter, and Phase 0 measured exactly
that dependency costing 12.7 KB of a 53.8 KB binary — two `snprintf` calls
formatting integers pulled in newlib's float formatter, and replacing them cut
the `.tns` from 55,108 to 12,676 bytes.

Integers here are formatted by hand for the same reason. The human-facing form
of an expression is the notebook's two-dimensional rendering; this format is
for storage and for tests.

## Recursion and the depth ceiling

Comparison, serialization, and parsing all recurse once per level. The CX II
gives the application a small stack, so `limits.max_depth` is what keeps that
recursion inside it, and it is clamped to `PHY_IR_MAX_DEPTH_CEILING` (1024)
regardless of what a caller asks for. A caller must not be able to raise the
limit to the point where a legal expression overflows the stack.

Depth is computed at construction and stored, so the check is O(1) and the
builders do not recurse.

## Testing

`tests/test_ir.c`, 2,586 checks. The suite leans throughout on interning:
building the same expression by two different routes and comparing refs tests
canonicalization, not just construction.

Verified: interning and shared subterms; commutative canonicalization and
noncommutative order preservation; associative flattening; the ordering being
total, antisymmetric, and transitive; structural hashes agreeing across
independent contexts; rational normalization at the `int64` extremes; type
rules on tensor slots and derivative variables; every limit; assumption and
symmetry contradictions; round-trip of all sixteen kinds through text into a
fresh context; awkward and UTF-8 names; buffer sizing; and parse diagnostics.

Memory is checked by comparing `phy_telemetry` before and after a context's
life, so a leaked pool fails the suite.

## Device build

The IR is in the device source list and compiles clean for ARM under the
pinned toolchain. Measured with `-Os -marm`:

| object            | text bytes |
| ----------------- | ---------- |
| `src/ir/ir.o`     | 8,008      |
| `src/ir/order.o`  | 1,442      |
| `src/ir/text.o`   | 5,442      |
| total             | **14,892** |

At the original IR landing, none of it was in `dist/phy-nspire.tns`: the
13,440-byte Phase 0 shell referenced no IR symbol and `--gc-sections` removed
it. That historical link-risk is why the probe below exists. The current
notebook does call the IR, CAS, reader-facing parser, and 2D layout; the full
product is now 1,048,076 bytes after persistence and the nMarkdown math slice
were added.

Because link-time garbage collection hides the IR, the device build alone does
not prove it *links*. A reference to a libc function Ndless newlib lacks would
go unnoticed until Phase 1 wired the notebook up.

`make ir-link-check` closes that gap:

```
$ make ir-link-check
== IR device link check ==

  compiling 6 sources + probe
  linking
  ok    50/50 public entry points retained
  ok    no _dtoa / _strtod / _printf_float in the image
  ok    packaged to a .tns
```

It links `tests/device/ir_link_probe.c`, which touches every public entry
point, with the production flags — `--gc-sections` included, since the point
is that these symbols survive collection because they are genuinely
referenced, not because collection was disabled. It then checks that every
function declared in `include/phy/ir.h` is present, that no float formatter
was dragged in, and that the result packages to a real `.tns`.

The expected symbol set is **derived from the header**, not listed in the
script, so adding a public function without extending the probe fails the
check instead of quietly going unlinked. Both guards are mutation-verified:
dropping three leaf entry points from the probe reports exactly those three,
and a single `snprintf("%f")` anywhere in the probe surfaces `_dtoa_r` and
fails.

The target is not a dependency of `all`, and the probe is not in the
Makefile's `SOURCES`. It builds into `build/arm-linkcheck/` and never touches
`dist/`, so it cannot inflate the shipped `.tns`.

## Not in this layer

Simplification, evaluation, and arithmetic — these are the scalar CAS, which
landed on top of this one and is documented in [`docs/CAS.md`](CAS.md). Nothing
in the IR changed to accommodate it: it reaches everything it needs through the
public header, which is the check that the substrate was the right shape.

Dummy-index canonicalization and contraction. Anything that consumes declared
symmetries. Arbitrary-precision numbers. The Giac backend boundary. These are
later phases; the IR is the substrate they operate on.
