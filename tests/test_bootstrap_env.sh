#!/usr/bin/env bash
set -euo pipefail

repo_root="${1:?repository root required}"
original_path="$PATH"
export PATH="/tmp/Program Files/phy regression:$PATH"

environment="$("$repo_root/tools/bootstrap-ndless.sh" --env-only)"
case "$environment" in
    *'export PATH="'*'$PATH"'*) ;;
    *)
        echo "bootstrap PATH export is not quoted" >&2
        exit 1
        ;;
esac

eval "$environment"
test -n "${NDLESS_SDK:-}"
test -n "${_NDLESS_TOOLCHAIN_PATH:-}"
case "$PATH" in
    "$NDLESS_SDK/bin:"*"Program Files/phy regression:"*) ;;
    *)
        echo "bootstrap PATH did not preserve a space-bearing inherited PATH" >&2
        exit 1
        ;;
esac

PATH="$original_path"
echo "test_bootstrap_env: 1 checks, 0 failures"
