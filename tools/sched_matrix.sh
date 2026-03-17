#!/usr/bin/env bash
set -u

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
ROUNDS="${1:-5}"
MIN_CPUS="${MIN_CPUS:-2}"
MAX_CPUS="${MAX_CPUS:-8}"
TIMEOUT_SECS="${TIMEOUT_SECS:-20}"

if ! [[ "$ROUNDS" =~ ^[0-9]+$ ]] || [ "$ROUNDS" -le 0 ]; then
    echo "invalid rounds: $ROUNDS" >&2
    exit 2
fi

if ! [[ "$MIN_CPUS" =~ ^[0-9]+$ ]] || ! [[ "$MAX_CPUS" =~ ^[0-9]+$ ]] || [ "$MIN_CPUS" -le 0 ] ||
    [ "$MAX_CPUS" -lt "$MIN_CPUS" ]; then
    echo "invalid cpu range: MIN_CPUS=$MIN_CPUS MAX_CPUS=$MAX_CPUS" >&2
    exit 2
fi

if ! [[ "$TIMEOUT_SECS" =~ ^[0-9]+$ ]] || [ "$TIMEOUT_SECS" -le 0 ]; then
    echo "invalid timeout: $TIMEOUT_SECS" >&2
    exit 2
fi

cd "$ROOT_DIR" || exit 1

echo "[matrix] building fs image once before matrix"
if ! make fsimg >/tmp/sched_matrix_fsimg.log 2>&1; then
    echo "[matrix] fsimg build failed"
    cat /tmp/sched_matrix_fsimg.log
    exit 1
fi

total=0
pass=0
fail=0

for cpus in $(seq "$MIN_CPUS" "$MAX_CPUS"); do
    echo "[matrix] cpus=$cpus rounds=$ROUNDS"
    for run in $(seq 1 "$ROUNDS"); do
        total=$((total + 1))
        out="$(timeout "${TIMEOUT_SECS}s" make qemu CPUS="$cpus" 2>&1)"
        ec=$?

        panic=0
        hello_ok=0
        stressio_ok=0
        stsched_ok=0
        pair_ok=0

        if echo "$out" | grep -q "panic"; then
            panic=1
        fi
        if echo "$out" | grep -q "\\[init\\] hello exited"; then
            hello_ok=1
        fi
        if echo "$out" | grep -q "\\[stressio\\] done"; then
            stressio_ok=1
        fi
        if echo "$out" | grep -q "\\[stresssched\\] done"; then
            stsched_ok=1
        fi
        if echo "$out" | grep -q "\\[init\\] stress pair done"; then
            pair_ok=1
        fi

        ok=0
        if [ "$ec" -eq 124 ] && [ "$panic" -eq 0 ] && [ "$hello_ok" -eq 1 ] &&
            [ "$stressio_ok" -eq 1 ] && [ "$stsched_ok" -eq 1 ] && [ "$pair_ok" -eq 1 ]; then
            ok=1
        fi

        if [ "$ok" -eq 1 ]; then
            pass=$((pass + 1))
            echo "[result] cpus=$cpus run=$run PASS exit=$ec panic=$panic hello=$hello_ok stressio=$stressio_ok stsched=$stsched_ok pair=$pair_ok"
        else
            fail=$((fail + 1))
            echo "[result] cpus=$cpus run=$run FAIL exit=$ec panic=$panic hello=$hello_ok stressio=$stressio_ok stsched=$stsched_ok pair=$pair_ok"
            echo "$out" | grep -E "\\[init\\]|\\[stressio\\]|\\[stresssched\\]|panic|pop_off|terminating on signal" || true
        fi
    done
done

echo "[summary] total=$total pass=$pass fail=$fail cpus=${MIN_CPUS}-${MAX_CPUS} rounds=$ROUNDS timeout=${TIMEOUT_SECS}s"

if [ "$fail" -ne 0 ]; then
    exit 1
fi

exit 0
