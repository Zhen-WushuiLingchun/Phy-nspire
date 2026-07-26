#!/usr/bin/env bash
#
# Lists the largest symbols in an ARM ELF, so that size regressions can be
# attributed to something specific rather than argued about.
#
# Usage: tools/symbol-report.sh <program.elf> [count]

set -euo pipefail

ELF="${1:-}"
COUNT="${2:-30}"

if [ -z "$ELF" ] || [ ! -f "$ELF" ]; then
    echo "usage: $0 <program.elf> [count]" >&2
    exit 2
fi

NM="${NM:-arm-none-eabi-nm}"
if ! command -v "$NM" >/dev/null 2>&1; then
    echo "error: $NM not found; run tools/bootstrap-ndless.sh first" >&2
    exit 1
fi

printf '== Phy-nspire symbol report: %s ==\n\n' "$(basename "$ELF")"
printf '  %10s  %-6s %s\n' "BYTES" "TYPE" "SYMBOL"

# -S prints sizes, -C demangles. Symbols with no size are skipped: they carry
# no allocation and would only pad the list.
#
# awk reads the stream to the end and stops printing rather than exiting at
# the count. Exiting early closes the pipe, nm dies of SIGPIPE, and with
# pipefail set the whole script exits 141 -- which it did for any binary with
# more than COUNT sized symbols, so the target broke precisely when the report
# became worth reading. Draining costs one pass over a symbol table.
"$NM" -S -C --size-sort --reverse-sort "$ELF" 2>/dev/null |
    awk -v count="$COUNT" '
        NF >= 4 {
            size = strtonum("0x" $2)
            if (size == 0) next
            if (shown >= count) next
            name = $4
            for (i = 5; i <= NF; i++) name = name " " $i
            printf "  %10d  %-6s %s\n", size, $3, name
            shown++
        }'

printf '\n  totals by section:\n'
if command -v arm-none-eabi-size >/dev/null 2>&1; then
    arm-none-eabi-size -A "$ELF" | sed 's/^/    /'
fi
