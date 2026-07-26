#!/usr/bin/env bash
#
# Regression test for tools/symbol-report.sh.
#
# The report used to exit 141 whenever the ELF had more sized symbols than the
# requested count: awk exited at the limit, nm died of SIGPIPE, and pipefail
# propagated it. The target therefore failed exactly on the binaries worth
# reporting on, and passed on the trivial ones.
#
# Runs on any host: the script honours $NM, so a stub stands in for the ARM
# toolchain and no cross-compiler is needed.
#
# Usage: tests/test_symbol_report.sh <repo-root>

set -uo pipefail

REPO_ROOT="${1:-}"
if [ -z "$REPO_ROOT" ] || [ ! -x "$REPO_ROOT/tools/symbol-report.sh" ]; then
    echo "usage: $0 <repo-root>" >&2
    exit 2
fi

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

failures=0
check() {
    local label="$1" actual="$2" expected="$3"
    if [ "$actual" != "$expected" ]; then
        echo "FAIL $label: expected '$expected', got '$actual'" >&2
        failures=$((failures + 1))
    fi
}

# Stub nm in the `nm -S` output shape: value, size, type, name.
cat >"$WORK/fake-nm" <<'EOS'
#!/usr/bin/env bash
total="${FAKE_NM_SYMBOLS:-5000}"
for ((i = 1; i <= total; i++)); do
    printf '%08x %08x T symbol_%d\n' "$i" "$((100 + i))" "$i"
done
# A sizeless symbol, which the report is expected to skip.
printf '         U undefined_thing\n'
EOS
chmod +x "$WORK/fake-nm"
: >"$WORK/fake.elf"

run_report() {
    NM="$WORK/fake-nm" FAKE_NM_SYMBOLS="$1" \
        bash "$REPO_ROOT/tools/symbol-report.sh" "$WORK/fake.elf" "$2" \
        >"$WORK/out" 2>"$WORK/err"
    echo $?
}

# The regression itself: far more symbols than the count must still exit 0.
status="$(run_report 5000 30)"
check "exit status with 5000 symbols, count 30" "$status" "0"

# And the truncation must still truncate.
listed="$(grep -c '^  *[0-9]' "$WORK/out")"
check "symbols listed" "$listed" "30"

# Largest-first ordering is preserved: the stub's last symbol is its biggest,
# and nm is asked for a reverse size sort, so the stub order is what arrives.
first="$(grep -m1 '^  *[0-9]' "$WORK/out" | awk '{ print $3 }')"
check "first symbol" "$first" "symbol_1"

# Fewer symbols than the count: the path that always worked must keep working.
status="$(run_report 5 30)"
check "exit status with 5 symbols, count 30" "$status" "0"
listed="$(grep -c '^  *[0-9]' "$WORK/out")"
check "symbols listed when under the count" "$listed" "5"

# Exactly at the count is the boundary the old code tripped on first.
status="$(run_report 30 30)"
check "exit status with exactly 30 symbols" "$status" "0"
listed="$(grep -c '^  *[0-9]' "$WORK/out")"
check "symbols listed at exactly the count" "$listed" "30"

status="$(run_report 31 30)"
check "exit status with 31 symbols, count 30" "$status" "0"

# Nothing should reach stderr on a clean run.
if [ -s "$WORK/err" ]; then
    echo "FAIL unexpected stderr:" >&2
    cat "$WORK/err" >&2
    failures=$((failures + 1))
fi

# A missing ELF is still a usage error, not a crash.
set +e
bash "$REPO_ROOT/tools/symbol-report.sh" "$WORK/does-not-exist" >/dev/null 2>&1
missing_status=$?
set -e
check "missing ELF exit status" "$missing_status" "2"

if [ "$failures" -eq 0 ]; then
    echo "test_symbol_report: 8 checks, 0 failures"
    exit 0
fi
echo "test_symbol_report: $failures failures" >&2
exit 1
