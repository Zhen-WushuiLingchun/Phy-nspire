#!/usr/bin/env bash
#
# Proves that a layer links on the device, which the ordinary ARM build does
# not.
#
# `make` compiles each layer but --gc-sections discards all of it, because
# nothing in the application calls it yet. So the shipped .tns contains zero
# phy_ir_* or phy_cas_* symbols and the build never exercises the linker on
# this code: a reference to a libc function Ndless newlib lacks would stay
# hidden until the notebook wired it up.
#
# This links the layer's probe from tests/device/, which touches every public
# entry point, and then checks three things:
#
#   1. every function declared in the layer's public header survived the link;
#   2. no floating-point formatter or parser was dragged in -- Phase 0
#      measured that dependency at 12.7 KB of a 53.8 KB image, and the IR's
#      hand-rolled integer formatting and bit-pattern reals, along with the
#      CAS's exact-only arithmetic, exist to avoid it;
#   3. the result packages to a real .tns.
#
# Nothing here touches dist/. The probe is built into its own directory and
# is never linked into the product.
#
# Usage: tools/link-check.sh [ir|cas]   (default ir)
#        after eval "$(tools/bootstrap-ndless.sh --env-only)"

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

LAYER="${1:-ir}"

# The IR, the status table every layer reports through, and the device platform
# backend that supplies phy_alloc.
COMMON_SOURCES=(
    src/ir/ir.c
    src/ir/order.c
    src/ir/text.c
    src/core/status.c
    src/platform/ndless/platform_ndless.c
    src/platform/ndless/crt_compat.c
)

case "$LAYER" in
ir)
    LABEL="IR"
    PROBE="tests/device/ir_link_probe.c"
    HEADER="include/phy/ir.h"
    OBJECT_GLOB="src_ir_*.o"
    # phy_ir_equal is excluded: it is static inline and has no external
    # definition by design.
    EXCLUDE='^phy_ir_equal$'
    MIN_ENTRY_POINTS=30
    SOURCES=("${COMMON_SOURCES[@]}")
    ;;
cas)
    LABEL="CAS"
    PROBE="tests/device/cas_link_probe.c"
    HEADER="include/phy/cas.h"
    OBJECT_GLOB="src_cas_*.o"
    EXCLUDE='^$'
    MIN_ENTRY_POINTS=20
    SOURCES=("${COMMON_SOURCES[@]}"
             src/cas/num.c
             src/cas/engine.c
             src/cas/simplify.c
             src/cas/diff.c
             src/cas/normal.c)
    ;;
*)
    echo "usage: tools/link-check.sh [ir|cas]" >&2
    exit 2
    ;;
esac

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
          -ffunction-sections -fdata-sections -Iinclude -Isrc/ir -Isrc/cas)
LDFLAGS=(-Wl,--gc-sections -Wl,--no-warn-rwx-segments)

PROBE_NAME="$(basename "$PROBE" .c)"

# Resolve and validate the exact recursive-delete target. Refuse a build/
# symlink that escapes the checkout.
BUILD_PARENT="$REPO_ROOT/build/arm-linkcheck"
mkdir -p "$BUILD_PARENT"
RESOLVED_ROOT="$(cd "$REPO_ROOT" && pwd -P)"
RESOLVED_PARENT="$(cd "$BUILD_PARENT" && pwd -P)"
case "$RESOLVED_PARENT" in
    "$RESOLVED_ROOT"|"$RESOLVED_ROOT"/*) ;;
    *)
        echo "error: resolved build directory escapes the repository:" >&2
        echo "       $RESOLVED_PARENT" >&2
        exit 1
        ;;
esac
BUILD_DIR="$RESOLVED_PARENT/$LAYER"
ELF="$BUILD_DIR/$PROBE_NAME.elf"
TNS="$BUILD_DIR/$PROBE_NAME.tns"
if [ -e "$BUILD_DIR" ]; then
    RESOLVED_BUILD_DIR="$(cd "$BUILD_DIR" && pwd -P)"
    if [ "$RESOLVED_BUILD_DIR" != "$BUILD_DIR" ]; then
        echo "error: refusing to recursively remove unexpected target:" >&2
        echo "       $RESOLVED_BUILD_DIR" >&2
        exit 1
    fi
fi
rm -rf -- "$BUILD_DIR"
mkdir -p "$BUILD_DIR"

printf '== %s device link check ==\n\n' "$LABEL"
printf '  compiling %d sources + probe\n' "${#SOURCES[@]}"
objects=()
for source in "${SOURCES[@]}" "$PROBE"; do
    object="$BUILD_DIR/$(echo "$source" | tr '/' '_' | sed 's/\.c$/.o/')"
    nspire-gcc "${GCCFLAGS[@]}" -c "$source" -o "$object"
    objects+=("$object")
done

printf '  linking\n'
nspire-ld "${objects[@]}" -o "$ELF" "${LDFLAGS[@]}"

# ---- 1. every declared entry point survived --------------------------------
#
# The expected set is derived from the header rather than listed here, so
# adding a public function without extending the probe fails this check
# instead of silently going unlinked.
mapfile -t declared < <(
    grep -oE "\bphy_${LAYER}_[a-z0-9_]+[[:space:]]*\(" "$HEADER" |
        sed -E 's/[[:space:]]*\($//' |
        grep -Ev "$EXCLUDE" |
        sort -u
)

mapfile -t defined < <(
    for object in "$BUILD_DIR"/$OBJECT_GLOB "$BUILD_DIR"/src_core_status.o; do
        "$NM" --defined-only "$object"
    done | awk '$2 == "T" { print $3 }' | sort -u
)

mapfile -t missing_definitions < <(
    comm -23 <(printf '%s\n' "${declared[@]}") \
             <(printf '%s\n' "${defined[@]}")
)

mapfile -t retained < <(
    "$NM" --defined-only "$ELF" | awk '$2 == "T" { print $3 }' | sort -u
)

missing=()
for symbol in "${declared[@]}"; do
    if ! printf '%s\n' "${retained[@]}" | grep -qx "$symbol"; then
        missing+=("$symbol")
    fi
done

if [ "${#declared[@]}" -lt "$MIN_ENTRY_POINTS" ]; then
    echo "  FAIL: only ${#declared[@]} entry points derived from $HEADER;" >&2
    echo "        the extraction is probably broken, not the link." >&2
    exit 1
fi

if [ "${#missing_definitions[@]}" -ne 0 ]; then
    echo >&2
    echo "  FAIL: ${#missing_definitions[@]} public declaration(s) have no layer definition:" >&2
    printf '          %s\n' "${missing_definitions[@]}" >&2
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
    "${#declared[@]}" "${#declared[@]}"

# ---- 2. no accidental floating-point dependency -----------------------------
#
# The IR deliberately carries finite real literals, so its probe may retain
# comparison helpers. The scalar CAS is stricter: it carries those atoms
# without evaluating them, and therefore must retain neither libm nor any
# soft-float arithmetic/comparison helper.
BANNED_PATTERN='(^|[[:space:]_])(_dtoa|_strtod|_printf_float|_scanf_float|_vfprintf|__sf_fake)'
if [ "$LAYER" = "cas" ]; then
    BANNED_PATTERN+='|[[:space:]]_?(sin|cos|tan|exp|log|pow|sqrt|floor|ceil|fmod)$|__aeabi_[df]'
fi
banned=$("$NM" "$ELF" | grep -Ei "$BANNED_PATTERN" || true)
if [ -n "$banned" ]; then
    echo >&2
    echo "  FAIL: a forbidden floating-point dependency reached the image." >&2
    echo "        The IR formats integers by hand and serializes reals as bit" >&2
    echo "        patterns, and the CAS computes only in exact rationals," >&2
    echo "        precisely to keep this out. Offending symbols:" >&2
    printf '%s\n' "$banned" | sed 's/^/          /' >&2
    exit 1
fi
if [ "$LAYER" = "cas" ]; then
    printf '  ok    no float formatter, libm call, or soft-float helper\n'
else
    printf '  ok    no _dtoa / _strtod / _printf_float in the image\n'
fi

# ---- 3. it packages ---------------------------------------------------------
genzehn --input "$ELF" --output "$BUILD_DIR/$PROBE_NAME.zehn" \
    --name "phy-$LAYER-link-probe" --version 1 >/dev/null
make-prg "$BUILD_DIR/$PROBE_NAME.zehn" "$TNS" >/dev/null
rm -f "$BUILD_DIR/$PROBE_NAME.zehn"
printf '  ok    packaged to a .tns\n'

# ---- report ------------------------------------------------------------------
layer_text=$("$SIZE" "$BUILD_DIR"/$OBJECT_GLOB |
    awk 'NR > 1 { total += $1 } END { print total }')
probe_tns=$(wc -c <"$TNS")

printf '\n  %s text as compiled        %8d bytes\n' "$LABEL" "$layer_text"
printf '  probe .tns (layer + deps)  %8d bytes\n' "$probe_tns"
printf '\n  Note: dist/phy-nspire.tns still contains none of this. The\n'
printf '        application does not call the %s yet, so --gc-sections drops\n' \
    "$LABEL"
printf '        it; the figure above is what wiring it up will pay.\n\n'
printf '  OK\n'
