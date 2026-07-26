#!/usr/bin/env bash
#
# Reports the installed size of a .tns against the project budget.
#
# The number that matters is the size of the file that lands in calculator
# flash, which is the .tns, not the ELF. The ELF is reported separately only to
# show where the bytes come from.
#
# Usage: tools/size-report.sh <program.tns> [program.elf]

set -euo pipefail

TNS="${1:-}"
ELF="${2:-}"

if [ -z "$TNS" ] || [ ! -f "$TNS" ]; then
    echo "usage: $0 <program.tns> [program.elf]" >&2
    exit 2
fi

# From docs/ARCHITECTURE.md and the feasibility note: the whole installed
# application targets 5-6 MB, and the 6 MB figure is a hard ceiling.
BUDGET_TARGET=$((5 * 1024 * 1024))
BUDGET_CEILING=$((6 * 1024 * 1024))

file_size() { stat -c %s "$1" 2>/dev/null || stat -f %z "$1"; }

human() {
    awk -v bytes="$1" 'BEGIN {
        if (bytes >= 1048576) printf "%.2f MiB", bytes / 1048576;
        else if (bytes >= 1024) printf "%.1f KiB", bytes / 1024;
        else printf "%d B", bytes;
    }'
}

tns_bytes="$(file_size "$TNS")"

printf '== Phy-nspire size report ==\n\n'
printf '  %-28s %10s bytes  (%s)\n' "$(basename "$TNS")" "$tns_bytes" "$(human "$tns_bytes")"

if [ -n "$ELF" ] && [ -f "$ELF" ]; then
    elf_bytes="$(file_size "$ELF")"
    printf '  %-28s %10s bytes  (%s)\n' "$(basename "$ELF")" "$elf_bytes" "$(human "$elf_bytes")"
    if command -v arm-none-eabi-size >/dev/null 2>&1; then
        printf '\n  sections:\n'
        arm-none-eabi-size -A "$ELF" | sed 's/^/    /'
    fi
fi

printf '\n  budget target  %10s bytes  (%s)\n' "$BUDGET_TARGET" "$(human "$BUDGET_TARGET")"
printf '  budget ceiling %10s bytes  (%s)\n' "$BUDGET_CEILING" "$(human "$BUDGET_CEILING")"

remaining=$((BUDGET_CEILING - tns_bytes))
percent="$(awk -v a="$tns_bytes" -v b="$BUDGET_CEILING" 'BEGIN { printf "%.1f", 100 * a / b }')"

printf '\n  used %s%% of the ceiling, %s remaining\n' "$percent" "$(human "$remaining")"

if [ "$tns_bytes" -gt "$BUDGET_CEILING" ]; then
    printf '\n  FAIL: over the 6 MB ceiling by %s\n' "$(human $((tns_bytes - BUDGET_CEILING)))"
    exit 1
fi
if [ "$tns_bytes" -gt "$BUDGET_TARGET" ]; then
    printf '\n  WARN: over the 5 MB target, under the 6 MB ceiling\n'
fi
printf '\n  OK\n'
