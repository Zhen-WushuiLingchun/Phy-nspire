#!/usr/bin/env bash
#
# Proves the expression IR links on the device, which the ordinary ARM build
# does not.
#
# `make` compiles the IR but --gc-sections discards all of it, because nothing
# in the application calls it yet. So the shipped .tns contains zero phy_ir_*
# symbols and the build never exercises the linker on this code: a reference
# to a libc function Ndless newlib lacks would stay hidden until Phase 1.
#
# This links tests/device/ir_link_probe.c, which touches every public entry
# point, and then checks three things:
#
#   1. every function declared in include/phy/ir.h survived the link;
#   2. no floating-point formatter or parser was dragged in -- Phase 0
#      measured that dependency at 12.7 KB of a 53.8 KB image, and the IR's
#      hand-rolled integer formatting and bit-pattern reals exist to avoid it;
#   3. the result packages to a real .tns.
#
# Nothing here touches dist/. The probe is built into its own directory and
# is never linked into the product.
#
# Usage: tools/ir-link-check.sh   (after eval "$(tools/bootstrap-ndless.sh --env-only)")

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

BUILD_DIR="build/arm-linkcheck"
PROBE="tests/device/ir_link_probe.c"
HEADER="include/phy/ir.h"

# Sources the probe needs: the IR, the status table it reports through, and
# the device platform backend that supplies phy_alloc.
SOURCES=(
    src/ir/ir.c
    src/ir/order.c
    src/ir/text.c
    src/core/status.c
    src/platform/ndless/platform_ndless.c
    src/platform/ndless/crt_compat.c
)

for tool in nspire-gcc nspire-ld genzehn make-prg; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "error: $tool not found." >&2
        echo "       run tools/bootstrap-ndless.sh, then" >&2
        echo "       eval \"\$(tools/bootstrap-ndless.sh --env-only)\"" >&2
        exit 1
    fi
done

# Binutils come from the toolchain the bootstrap selected, not from a fixed
# path, so this works for both the prebuilt and source toolchain modes.
find_binutil() {
    local name="$1"
    if [ -n "${_NDLESS_TOOLCHAIN_PATH:-}" ] && [ -x "$_NDLESS_TOOLCHAIN_PATH/$name" ]; then
        echo "$_NDLESS_TOOLCHAIN_PATH/$name"
        return 0
    fi
    if command -v "$name" >/dev/null 2>&1; then
        command -v "$name"
        return 0
    fi
    echo "error: $name not found; is the ARM toolchain on PATH?" >&2
    return 1
}

NM="$(find_binutil arm-none-eabi-nm)"
SIZE="$(find_binutil arm-none-eabi-size)"

# Same flags as the product build, --gc-sections included: the point is that
# these symbols survive collection because the probe genuinely references
# them, not because collection was disabled.
GCCFLAGS=(-Wall -Wextra -Wshadow -Wpointer-arith -std=c11 -marm -Os -DNDEBUG
          -ffunction-sections -fdata-sections -Iinclude -Isrc/ir)
LDFLAGS=(-Wl,--gc-sections -Wl,--no-warn-rwx-segments)

rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"

printf '== IR device link check ==\n\n'
printf '  compiling %d sources + probe\n' "${#SOURCES[@]}"
objects=()
for source in "${SOURCES[@]}" "$PROBE"; do
    object="$BUILD_DIR/$(echo "$source" | tr '/' '_' | sed 's/\.c$/.o/')"
    nspire-gcc "${GCCFLAGS[@]}" -c "$source" -o "$object"
    objects+=("$object")
done

printf '  linking\n'
nspire-ld "${objects[@]}" -o "$BUILD_DIR/ir_link_probe.elf" "${LDFLAGS[@]}"

# ---- 1. every declared entry point survived --------------------------------
#
# The expected set is derived from the header rather than listed here, so
# adding a public function without extending the probe fails this check
# instead of silently going unlinked. phy_ir_equal is excluded: it is
# static inline and has no external definition by design.
mapfile -t declared < <(
    grep -oE '\bphy_ir_[a-z0-9_]+[[:space:]]*\(' "$HEADER" |
        sed -E 's/[[:space:]]*\($//' |
        grep -v '^phy_ir_equal$' |
        sort -u
)

# Intersect with what the IR actually defines, so a name mentioned only in
# prose cannot fail the check.
mapfile -t defined < <(
    for object in "$BUILD_DIR"/src_ir_*.o "$BUILD_DIR"/src_core_status.o; do
        "$NM" --defined-only "$object"
    done | awk '$2 == "T" { print $3 }' | sort -u
)

mapfile -t expected < <(
    comm -12 <(printf '%s\n' "${declared[@]}") <(printf '%s\n' "${defined[@]}")
)

mapfile -t retained < <(
    "$NM" --defined-only "$BUILD_DIR/ir_link_probe.elf" |
        awk '$2 == "T" { print $3 }' | sort -u
)

missing=()
for symbol in "${expected[@]}"; do
    if ! printf '%s\n' "${retained[@]}" | grep -qx "$symbol"; then
        missing+=("$symbol")
    fi
done

if [ "${#expected[@]}" -lt 30 ]; then
    echo "  FAIL: only ${#expected[@]} entry points derived from $HEADER;" >&2
    echo "        the extraction is probably broken, not the link." >&2
    exit 1
fi

if [ "${#missing[@]}" -ne 0 ]; then
    echo >&2
    echo "  FAIL: ${#missing[@]} public entry point(s) did not survive the link." >&2
    echo "        Add them to $PROBE:" >&2
    printf '          %s\n' "${missing[@]}" >&2
    exit 1
fi
printf '  ok    %d/%d public entry points retained\n' \
    "${#expected[@]}" "${#expected[@]}"

# ---- 2. no floating-point formatter ----------------------------------------
banned=$("$NM" "$BUILD_DIR/ir_link_probe.elf" |
    grep -Ei '(^|[[:space:]_])(_dtoa|_strtod|_printf_float|_scanf_float|_vfprintf|__sf_fake)' || true)
if [ -n "$banned" ]; then
    echo >&2
    echo "  FAIL: a float formatter or parser reached the image." >&2
    echo "        The IR formats integers by hand and serializes reals as bit" >&2
    echo "        patterns precisely to keep this out. Offending symbols:" >&2
    printf '%s\n' "$banned" | sed 's/^/          /' >&2
    exit 1
fi
printf '  ok    no _dtoa / _strtod / _printf_float in the image\n'

# ---- 3. it packages ---------------------------------------------------------
genzehn --input "$BUILD_DIR/ir_link_probe.elf" \
    --output "$BUILD_DIR/ir_link_probe.zehn" \
    --name "phy-ir-link-probe" --version 1 >/dev/null
make-prg "$BUILD_DIR/ir_link_probe.zehn" "$BUILD_DIR/ir_link_probe.tns" >/dev/null
rm -f "$BUILD_DIR/ir_link_probe.zehn"
printf '  ok    packaged to a .tns\n'

# ---- report ------------------------------------------------------------------
ir_text=$("$SIZE" "$BUILD_DIR"/src_ir_*.o | awk 'NR > 1 { total += $1 } END { print total }')
probe_tns=$(wc -c <"$BUILD_DIR/ir_link_probe.tns")

printf '\n  IR text as compiled       %8d bytes\n' "$ir_text"
printf '  probe .tns (IR + platform)%8d bytes\n' "$probe_tns"
printf '\n  Note: dist/phy-nspire.tns still contains none of this. The\n'
printf '        application does not call the IR yet, so --gc-sections drops\n'
printf '        it; the figure above is what Phase 1 will pay when it does.\n\n'
printf '  OK\n'
