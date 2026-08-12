#!/usr/bin/env bash
#
# Retain and resolve every public dynamic-component/bridge entry point on
# Ndless ARM. The product currently compiles this layer but --gc-sections
# discards it until the evaluator starts calling ComponentValue.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
cd "$ROOT"

for tool in nspire-gcc arm-none-eabi-nm genzehn make-prg; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "error: $tool not found" >&2
        exit 1
    }
done

BUILD_PARENT="$ROOT/build"
mkdir -p "$BUILD_PARENT"
RESOLVED_PARENT="$(cd "$BUILD_PARENT" && pwd -P)"
case "$RESOLVED_PARENT" in
    "$ROOT"|"$ROOT"/*) ;;
    *) echo "error: build directory escapes the repository" >&2; exit 1 ;;
esac
BUILD="$RESOLVED_PARENT/arm-component-bridge-linkcheck"
if [ -e "$BUILD" ]; then
    RESOLVED_BUILD="$(cd "$BUILD" && pwd -P)"
    if [ "$RESOLVED_BUILD" != "$BUILD" ]; then
        echo "error: refusing to remove unexpected target: $RESOLVED_BUILD" >&2
        exit 1
    fi
fi
rm -rf -- "$BUILD"
mkdir -p "$BUILD"

FLAGS=(-Wall -Wextra -Wshadow -Wpointer-arith -std=c11 -marm -Os
       -DNDEBUG -ffunction-sections -fdata-sections
       -Iinclude -Isrc/abstract -Isrc/component -Isrc/permutation
       -Isrc/cas -Isrc/ir -Isrc/input)
PROBE="$BUILD/component_bridge_link_probe.o"
nspire-gcc "${FLAGS[@]}" -c \
    tests/device/component_bridge_link_probe.c -o "$PROBE"

objects=(
    build/arm/src/core/status.o
    build/arm/src/input/modifier.o
    build/arm/src/input/pointer.o
    build/arm/src/ir/ir.o
    build/arm/src/ir/order.o
    build/arm/src/ir/text.o
    build/arm/src/tensor/chart.o
    build/arm/src/tensor/symmetry.o
    build/arm/src/tensor/tensor.o
    build/arm/src/tensor/ops.o
    build/arm/src/abstract/index.o
    build/arm/src/abstract/head.o
    build/arm/src/abstract/monomial.o
    build/arm/src/abstract/canonical.o
    build/arm/src/abstract/dgs.o
    build/arm/src/abstract/young.o
    build/arm/src/abstract/garnir.o
    build/arm/src/component/basis.o
    build/arm/src/component/component.o
    build/arm/src/component/bridge.o
    build/arm/src/permutation/perm.o
    build/arm/src/permutation/bsgs.o
    build/arm/src/permutation/orbit.o
    build/arm/src/exact/context.o
    build/arm/src/exact/integer.o
    build/arm/src/exact/rational.o
    build/arm/src/exact/gaussian.o
    build/arm/src/exact/algebraic.o
    build/arm/src/exact/ball.o
    build/arm/src/exact/complex_ball.o
    build/arm/src/exact/complex_special.o
    build/arm/src/cas/num.o
    build/arm/src/cas/big_num.o
    build/arm/src/cas/complex.o
    build/arm/src/cas/algebraic.o
    build/arm/src/cas/finite_poly.o
    build/arm/src/cas/series.o
    build/arm/src/cas/limit.o
    build/arm/src/cas/solve.o
    build/arm/src/cas/linear_solve.o
    build/arm/src/cas/sparse_poly.o
    build/arm/src/cas/ball_eval.o
    build/arm/src/cas/complex_roots.o
    build/arm/src/cas/special.o
    build/arm/src/cas/engine.o
    build/arm/src/cas/simplify.o
    build/arm/src/cas/diff.o
    build/arm/src/cas/integrate.o
    build/arm/src/cas/normal.o
    build/arm/src/cas/reduce.o
    build/arm/src/platform/ndless/platform_ndless.o
    build/arm/src/platform/ndless/crt_compat.o
    "$PROBE"
)
for object in "${objects[@]}"; do
    test -f "$object" || {
        echo "error: missing ARM object: $object" >&2
        echo "       run make first" >&2
        exit 1
    }
done

ELF="$BUILD/component_bridge_link_probe.elf"
nspire-gcc "${objects[@]}" -o "$ELF" \
    -Wl,--gc-sections -Wl,--no-warn-rwx-segments

HEADER=include/phy/component_tensor.h
mapfile -t declared < <(
    grep -oE '\bphy_(basis|bridge|component)_[a-z0-9_]+[[:space:]]*\(' \
        "$HEADER" |
        sed -E 's/[[:space:]]*\($//' |
        sort -u
)
mapfile -t retained < <(
    arm-none-eabi-nm --defined-only "$ELF" |
        awk '$2 == "T" { print $3 }' |
        sort -u
)
missing=()
for symbol in "${declared[@]}"; do
    found=false
    for actual in "${retained[@]}"; do
        if [ "$actual" = "$symbol" ]; then
            found=true
            break
        fi
    done
    if [ "$found" != true ]; then
        missing+=("$symbol")
    fi
done
if [ "${#declared[@]}" -lt 33 ]; then
    echo "FAIL: header extraction found only ${#declared[@]} symbols" >&2
    exit 1
fi
if [ "${#missing[@]}" -ne 0 ]; then
    echo "FAIL: ${#missing[@]} public symbols were not retained:" >&2
    printf '  %s\n' "${missing[@]}" >&2
    exit 1
fi

banned="$(arm-none-eabi-nm "$ELF" |
    grep -Ei '(_dtoa|_strtod|_printf_float|_scanf_float|_vfprintf)' ||
    true)"
if [ -n "$banned" ]; then
    echo "FAIL: floating-point formatter/parser reached the probe" >&2
    printf '%s\n' "$banned" >&2
    exit 1
fi

genzehn --input "$ELF" --output "$BUILD/probe.zehn" \
    --name "phy-component-bridge-probe" --version 1 >/dev/null
make-prg "$BUILD/probe.zehn" "$BUILD/probe.tns" >/dev/null
rm -f "$BUILD/probe.zehn"

printf '== component bridge device link check ==\n\n'
printf '  ok    %d/%d public entry points retained\n' \
    "${#declared[@]}" "${#declared[@]}"
printf '  ok    no floating-point formatter/parser linked\n'
printf '  ok    packaged probe: %d bytes\n\n' \
    "$(wc -c <"$BUILD/probe.tns")"
printf '  OK\n'
