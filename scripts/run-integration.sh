#!/usr/bin/env bash
set -u

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
BUILD="$ROOT/build"
DIST="$ROOT/dist"
OUT="${OUT:-$ROOT/evidence/gate8d/runs/integration-$(date +%Y%m%d-%H%M%S)}"
SERVER_LOG="${SERVER_LOG:-$ROOT/evidence/gate8d/runs/local-server.log}"

mkdir -p "$OUT"
: >"$OUT/00-RESULTS.txt"

SERVER_LOG_START_BYTES=0
if [[ -f "$SERVER_LOG" ]]; then
    SERVER_LOG_START_BYTES="$(wc -c <"$SERVER_LOG" | tr -d '[:space:]')"
fi

COREX_REMOTE_HOST="${COREX_REMOTE_HOST:-127.0.0.1}" \
COREX_REMOTE_PORT="${COREX_REMOTE_PORT:-50051}" \
"$DIST/bin/device_identity_app" \
    >"$OUT/app.stdout.log" \
    2>"$OUT/app.stderr.log"
RC=$?

echo "device_identity_app_exit=$RC" | tee -a "$OUT/00-RESULTS.txt"

FAIL=0
[[ "$RC" -eq 0 ]] || FAIL=1

grep -q '^G8C_DEVICE_IDENTITY_RESULT=PASS$' \
    "$OUT/app.stdout.log" || FAIL=1

grep -q 'G8C_DEVICE_PROPERTIES device=0 name=Iluvatar MR-V100 .*result=PASS' \
    "$OUT/app.stdout.log" || FAIL=1

grep -Eq '^G8C_INVALID_DEVICE rc=[0-9]+ expected=[0-9]+$' \
    "$OUT/app.stdout.log" || FAIL=1

grep -Eq '^G8C_INVALID_DEVICE_LAST_ERROR rc=[0-9]+ expected=[0-9]+$' \
    "$OUT/app.stdout.log" || FAIL=1

grep -q '^G8C_LAST_ERROR_CLEARED rc=0 expected=0$' \
    "$OUT/app.stdout.log" || FAIL=1

MEM_INFO_COUNT="$(grep -c '^G8C_MEM_INFO ' "$OUT/app.stdout.log" || true)"
echo "client_mem_info_count=$MEM_INFO_COUNT" >>"$OUT/00-RESULTS.txt"
[[ "$MEM_INFO_COUNT" -eq 2 ]] || FAIL=1

grep -q '^G8C_NUMERICAL failures=0$' \
    "$OUT/app.stdout.log" || FAIL=1

cp -a "$BUILD/G8C-ABI-AUDIT.txt" "$OUT/"
cp -a "$BUILD/G8C-SCOPE-AUDIT.txt" "$OUT/"
cp -a "$BUILD/library-readelf-dynamic.txt" "$OUT/"
cp -a "$BUILD/library-version-info.txt" "$OUT/"
cp -a "$BUILD/library-dynamic-defined.txt" "$OUT/"
cp -a "$BUILD/app-readelf-dynamic.txt" "$OUT/"
cp -a "$ROOT/packaging/ABI-SURFACE-v1.1.txt" "$OUT/"

grep -q '^G8C_ABI_SURFACE=PASS$' \
    "$OUT/G8C-ABI-AUDIT.txt" || FAIL=1
grep -q '^G8C_SCOPE_AUDIT=PASS$' \
    "$OUT/G8C-SCOPE-AUDIT.txt" || FAIL=1

if [[ -f "$SERVER_LOG" ]]; then
    END="$(wc -c <"$SERVER_LOG" | tr -d '[:space:]')"
    if [[ "$END" -lt "$SERVER_LOG_START_BYTES" ]]; then
        SERVER_LOG_START_BYTES=0
    fi

    if [[ "$END" -gt "$SERVER_LOG_START_BYTES" ]]; then
        tail -c "+$((SERVER_LOG_START_BYTES + 1))" "$SERVER_LOG" \
            >"$OUT/G8C-SERVER-RUN.log"
    else
        : >"$OUT/G8C-SERVER-RUN.log"
    fi

    INFO_COUNT="$(grep -a -c '^GET_DEVICE_INFO request=' "$OUT/G8C-SERVER-RUN.log" || true)"
    GETS="$(grep -a -c '^GET_KERNEL request=' "$OUT/G8C-SERVER-RUN.log" || true)"
    LAUNCHES="$(grep -a -c '^LAUNCH_GENERIC request=' "$OUT/G8C-SERVER-RUN.log" || true)"
    ABI_PASSES="$(grep -a -c '^ABI_VALIDATE kernel=.* result=PASS$' "$OUT/G8C-SERVER-RUN.log" || true)"
    SESSIONS="$(grep -a -c '^SESSION_START$' "$OUT/G8C-SERVER-RUN.log" || true)"

    echo "server_device_info_count=$INFO_COUNT" >>"$OUT/00-RESULTS.txt"
    echo "server_get_kernel_count=$GETS" >>"$OUT/00-RESULTS.txt"
    echo "server_launch_generic_count=$LAUNCHES" >>"$OUT/00-RESULTS.txt"
    echo "server_abi_validate_pass_count=$ABI_PASSES" >>"$OUT/00-RESULTS.txt"
    echo "server_session_count=$SESSIONS" >>"$OUT/00-RESULTS.txt"

    # properties + mem-before + mem-after-alloc
    [[ "$INFO_COUNT" -eq 3 ]] || FAIL=1
    [[ "$GETS" -eq 1 ]] || FAIL=1
    [[ "$LAUNCHES" -eq 1 ]] || FAIL=1
    [[ "$ABI_PASSES" -eq 1 ]] || FAIL=1
    [[ "$SESSIONS" -eq 1 ]] || FAIL=1

    grep -q 'protocol=CRX9 version=3' "$SERVER_LOG" || FAIL=1
else
    echo "server_log_included=NO" >>"$OUT/00-RESULTS.txt"
fi

if [[ "$FAIL" -eq 0 ]]; then
    echo "G8C_B_RESULT=PASS" | tee -a "$OUT/00-RESULTS.txt"
else
    echo "G8C_B_RESULT=FAIL" | tee -a "$OUT/00-RESULTS.txt"
fi

ARCHIVE="${OUT}.tar.gz"
tar -C "$(dirname "$OUT")" -czf "$ARCHIVE" "$(basename "$OUT")"

echo "Evidence: $OUT"
echo "Archive : $ARCHIVE"

exit "$FAIL"
