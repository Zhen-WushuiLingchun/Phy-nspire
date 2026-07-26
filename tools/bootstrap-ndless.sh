#!/usr/bin/env bash
#
# Bootstraps the pinned Ndless SDK so that `make` can build a .tns.
#
# Two toolchain modes:
#
#   --toolchain=source     Build the ARM cross toolchain with Ndless's own
#                          build_toolchain.sh. This is the upstream-supported
#                          path. It needs root once, to install build
#                          dependencies (gmp, mpfr, mpc, texinfo, flex, bison),
#                          and takes roughly half an hour.
#
#   --toolchain=prebuilt   Download the pinned Arm GNU Toolchain release and
#                          point the SDK at it via _NDLESS_TOOLCHAIN_PATH.
#                          Needs no root and no distro packages. This is the
#                          mode the Phase 0 ARM build was verified with.
#
# Everything lands under $PHY_SDK_ROOT (default ~/.phy-nspire) and nothing is
# written into the repository.
#
# Usage:
#   tools/bootstrap-ndless.sh [--toolchain=prebuilt|source] [--jobs=N]
#   eval "$(tools/bootstrap-ndless.sh --env-only)"

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LOCK_FILE="$REPO_ROOT/research/upstreams.lock.json"
SDK_ROOT="${PHY_SDK_ROOT:-$HOME/.phy-nspire}"

TOOLCHAIN_MODE=prebuilt
JOBS="$(nproc 2>/dev/null || echo 4)"
ENV_ONLY=0

for arg in "$@"; do
    case "$arg" in
    --toolchain=source) TOOLCHAIN_MODE=source ;;
    --toolchain=prebuilt) TOOLCHAIN_MODE=prebuilt ;;
    --jobs=*) JOBS="${arg#*=}" ;;
    --env-only) ENV_ONLY=1 ;;
    -h | --help)
        sed -n '2,26p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
        exit 0
        ;;
    *)
        echo "unknown argument: $arg" >&2
        exit 2
        ;;
    esac
done

# Reads a string field from one object in the lock file. Deliberately avoids a
# JSON dependency: the lock file is generated and checked in, not hand-edited.
lock_field() {
    local object="$1" field="$2"
    sed -n "/\"$object\"[[:space:]]*:/,/^    }/p" "$LOCK_FILE" |
        grep -m1 "\"$field\"[[:space:]]*:" |
        sed -e 's/.*:[[:space:]]*"//' -e 's/".*//'
}

NDLESS_REPO="$(lock_field ndless_sdk repository)"
NDLESS_COMMIT="$(lock_field ndless_sdk commit)"
NDLESS_TAG="$(lock_field ndless_sdk tag)"
ARM_ARCHIVE_URL="$(lock_field arm_gnu_toolchain archive)"
ARM_ARCHIVE_SHA="$(lock_field arm_gnu_toolchain archive_sha256)"
ARM_DIRNAME="$(lock_field arm_gnu_toolchain extracted_directory)"

if [ -z "$NDLESS_COMMIT" ]; then
    echo "could not read the pinned Ndless commit from $LOCK_FILE" >&2
    exit 1
fi

NDLESS_DIR="$SDK_ROOT/Ndless"
SDK_DIR="$NDLESS_DIR/ndless-sdk"
ARM_DIR="$SDK_ROOT/$ARM_DIRNAME"

emit_env() {
    echo "export NDLESS_SDK=\"$SDK_DIR\""
    if [ "$TOOLCHAIN_MODE" = prebuilt ]; then
        echo "export _NDLESS_TOOLCHAIN_PATH=\"$ARM_DIR/bin\""
        echo "export PATH=\"$SDK_DIR/bin:$ARM_DIR/bin:\$PATH\""
    else
        echo "export PATH=\"$SDK_DIR/bin:$SDK_DIR/toolchain/install/bin:\$PATH\""
    fi
}

if [ "$ENV_ONLY" -eq 1 ]; then
    emit_env
    exit 0
fi

mkdir -p "$SDK_ROOT"

echo "==> Ndless $NDLESS_TAG ($NDLESS_COMMIT)"
if [ ! -d "$NDLESS_DIR/.git" ]; then
    git clone --recursive "$NDLESS_REPO" "$NDLESS_DIR"
fi
git -C "$NDLESS_DIR" fetch --tags origin
git -C "$NDLESS_DIR" checkout --recurse-submodules "$NDLESS_COMMIT"

actual_commit="$(git -C "$NDLESS_DIR" rev-parse HEAD)"
if [ "$actual_commit" != "$NDLESS_COMMIT" ]; then
    echo "checkout mismatch: expected $NDLESS_COMMIT, got $actual_commit" >&2
    exit 1
fi

if [ "$TOOLCHAIN_MODE" = prebuilt ]; then
    echo "==> Arm GNU Toolchain (prebuilt)"
    archive="$SDK_ROOT/$(basename "$ARM_ARCHIVE_URL")"
    if [ ! -d "$ARM_DIR" ]; then
        if [ ! -f "$archive" ]; then
            curl -fL --retry 3 -o "$archive.part" "$ARM_ARCHIVE_URL"
            mv "$archive.part" "$archive"
        fi
        echo "$ARM_ARCHIVE_SHA  $archive" | sha256sum -c -
        tar -xf "$archive" -C "$SDK_ROOT"
    fi
    export _NDLESS_TOOLCHAIN_PATH="$ARM_DIR/bin"
    export PATH="$SDK_DIR/bin:$ARM_DIR/bin:$PATH"
else
    echo "==> Arm cross toolchain (built from source)"
    export PATH="$SDK_DIR/bin:$SDK_DIR/toolchain/install/bin:$PATH"
    if ! command -v arm-none-eabi-gcc >/dev/null 2>&1; then
        echo "    this needs libgmp-dev libmpfr-dev libmpc-dev texinfo flex bison"
        (cd "$SDK_DIR/toolchain" && ./build_toolchain.sh)
    fi
fi

if ! command -v arm-none-eabi-gcc >/dev/null 2>&1; then
    echo "arm-none-eabi-gcc is still not on PATH; bootstrap failed" >&2
    exit 1
fi

# The SDK's genzehn is a host tool that needs boost::program_options. When the
# distro package is missing and we have no way to install it, unpack it into a
# local prefix instead. The link directory deliberately holds only the static
# archive, so genzehn links statically and needs no LD_LIBRARY_PATH later.
ensure_boost() {
    if printf '#include <boost/program_options.hpp>\nint main(void){return 0;}\n' |
        g++ -x c++ -std=c++11 -fsyntax-only - >/dev/null 2>&1; then
        return 0
    fi

    echo "==> boost::program_options not installed; staging it locally"
    if ! command -v apt-get >/dev/null 2>&1 || ! command -v dpkg >/dev/null 2>&1; then
        echo "install your distribution's boost program_options development" >&2
        echo "package (Debian/Ubuntu: libboost-program-options-dev)" >&2
        exit 1
    fi

    local deps="$SDK_ROOT/deps"
    local archive="$deps/usr/lib/x86_64-linux-gnu/libboost_program_options.a"
    if [ ! -f "$archive" ]; then
        mkdir -p "$deps/dl"
        (cd "$deps/dl" &&
            apt-get download libboost1.74-dev libboost-program-options1.74-dev &&
            for deb in ./*.deb; do dpkg -x "$deb" "$deps"; done)
    fi
    if [ ! -f "$archive" ]; then
        echo "failed to stage boost::program_options" >&2
        exit 1
    fi

    mkdir -p "$deps/link"
    cp -f "$archive" "$deps/link/"
    export CPLUS_INCLUDE_PATH="$deps/usr/include${CPLUS_INCLUDE_PATH:+:$CPLUS_INCLUDE_PATH}"
    export LIBRARY_PATH="$deps/link${LIBRARY_PATH:+:$LIBRARY_PATH}"
}

ensure_boost

echo "==> Building the Ndless SDK"
make -C "$SDK_DIR" -j"$JOBS"

echo
echo "Ndless SDK ready. Add it to your environment with:"
echo
emit_env
