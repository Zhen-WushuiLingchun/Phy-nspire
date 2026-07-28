#!/usr/bin/env bash
#
# Proves the component tensor core links on the device, which the ordinary ARM
# build does not.
#
# `make` compiles src/tensor but --gc-sections discards all of it, because
# nothing in the application calls it yet. So the shipped .tns contains zero
# phy_tensor_* symbols and the build never exercises the linker on this code:
# a reference to a libc function Ndless newlib lacks would stay hidden until
# the curvature pipeline wired it up.
#
# This is tools/ir-link-check.sh applied to the tensor core, and it checks the
# same three things:
#
#   1. every function declared in include/phy/tensor.h survived the link;
#   2. no floating-point formatter or parser was dragged in. The tensor core
#      holds expression handles and never formats a number, so anything here
#      would be a genuine surprise -- Phase 0 measured that dependency at
#      12.7 KB of a 53.8 KB image;
#   3. the result packages to a real .tns.
#
# Nothing here touches dist/. The probe is built into its own directory and is
# never linked into the product.
#
# Usage: tools/tensor-link-check.sh
#        (after eval "$(tools/bootstrap-ndless.sh --env-only)")

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

PROBE="tests/device/tensor_link_probe.c"
HEADER="include/phy/tensor.h"

# The tensor core, the IR it holds handles from, the status table it reports
# through, and the device platform backend that supplies phy_alloc.
SOURCES=(
    src/tensor/chart.c
    src/tensor/symmetry.c
    src/tensor/tensor.c
    src/tensor/ops.c
    src/cas/num.c
    src/cas/engine.c
    src/cas/simplify.c
    src/cas/diff.c
    src/cas/integrate.c
    src/cas/normal.c
    src/cas/reduce.c
    src/ir/ir.c
    src/ir/order.c
    src/ir/text.c
    src/core/status.c
    src/input/modifier.c
    src/input/pointer.c
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
          -ffunction-sections -fdata-sections -Iinclude -Isrc/ir -Isrc/tensor
          -Isrc/cas -Isrc/input)
LDFLAGS=(-Wl,--gc-sections -Wl,--no-warn-rwx-segments)

# Resolve and validate the exact recursive-delete target. In particular, do
# not follow a user-created build/ symlink outside the checkout.
BUILD_PARENT="$REPO_ROOT/build"
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
BUILD_DIR="$RESOLVED_PARENT/arm-tensor-linkcheck"
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

printf '== tensor core device link check ==\n\n'
printf '  compiling %d sources + probe\n' "${#SOURCES[@]}"
objects=()
for source in "${SOURCES[@]}" "$PROBE"; do
    object="$BUILD_DIR/$(echo "$source" | tr '/' '_' | sed 's/\.c$/.o/')"
    nspire-gcc "${GCCFLAGS[@]}" -c "$source" -o "$object"
    objects+=("$object")
done

printf '  linking\n'
nspire-ld "${objects[@]}" -o "$BUILD_DIR/tensor_link_probe.elf" "${LDFLAGS[@]}"

# ---- 1. every declared entry point survived ---------------------------------
#
# Derived from the header rather than listed here, so adding a public function
# without extending the probe fails this check instead of silently going
# unlinked.
mapfile -t declared < <(
    grep -oE '\bphy_(tensor|chart)_[a-z0-9_]+[[:space:]]*\(' "$HEADER" |
        sed -E 's/[[:space:]]*\($//' |
        sort -u
)

mapfile -t defined < <(
    for object in "$BUILD_DIR"/src_tensor_*.o; do
        "$NM" --defined-only "$object"
    done | awk '$2 == "T" { print $3 }' | sort -u
)

mapfile -t missing_definitions < <(
    comm -23 <(printf '%s\n' "${declared[@]}") \
             <(printf '%s\n' "${defined[@]}")
)

mapfile -t retained < <(
    "$NM" --defined-only "$BUILD_DIR/tensor_link_probe.elf" |
        awk '$2 == "T" { print $3 }' | sort -u
)

missing=()
for symbol in "${declared[@]}"; do
    if ! printf '%s\n' "${retained[@]}" | grep -qx "$symbol"; then
        missing+=("$symbol")
    fi
done

if [ "${#declared[@]}" -lt 25 ]; then
    echo "  FAIL: only ${#declared[@]} entry points derived from $HEADER;" >&2
    echo "        the extraction is probably broken, not the link." >&2
    exit 1
fi

if [ "${#missing_definitions[@]}" -ne 0 ]; then
    echo >&2
    echo "  FAIL: ${#missing_definitions[@]} public declaration(s) have no tensor-core definition:" >&2
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

# ---- 2. no floating-point formatter -----------------------------------------
banned=$("$NM" "$BUILD_DIR/tensor_link_probe.elf" |
    grep -Ei '(^|[[:space:]_])(_dtoa|_strtod|_printf_float|_scanf_float|_vfprintf|__sf_fake)' || true)
if [ -n "$banned" ]; then
    echo >&2
    echo "  FAIL: a float formatter or parser reached the image." >&2
    echo "        The tensor core holds handles and never formats a number," >&2
    echo "        so this is a real dependency, not a rounding detail." >&2
    printf '%s\n' "$banned" | sed 's/^/          /' >&2
    exit 1
fi
printf '  ok    no _dtoa / _strtod / _printf_float in the image\n'

# ---- 3. it packages ---------------------------------------------------------
genzehn --input "$BUILD_DIR/tensor_link_probe.elf" \
    --output "$BUILD_DIR/tensor_link_probe.zehn" \
    --name "phy-tensor-link-probe" --version 1 >/dev/null
make-prg "$BUILD_DIR/tensor_link_probe.zehn" "$BUILD_DIR/tensor_link_probe.tns" >/dev/null
rm -f "$BUILD_DIR/tensor_link_probe.zehn"
printf '  ok    packaged to a .tns\n'

# ---- report ------------------------------------------------------------------
tensor_text=$("$SIZE" "$BUILD_DIR"/src_tensor_*.o | awk 'NR > 1 { total += $1 } END { print total }')
probe_tns=$(wc -c <"$BUILD_DIR/tensor_link_probe.tns")

printf '\n  tensor core text as compiled %8d bytes\n' "$tensor_text"
printf '  probe .tns (tensor + IR + platform) %6d bytes\n' "$probe_tns"
printf '\n  Note: the application now reaches the tensor core through the\n'
printf '        evaluator. The isolated figure above measures this API\n'
printf '        and its probe dependencies; it is not an incremental\n'
printf '        product-size measurement.\n\n'
printf '  OK\n'
