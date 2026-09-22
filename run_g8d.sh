#!/usr/bin/env bash
set -u

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PREFIX="${PREFIX:-$HERE/sdk-install}"
WORK="${WORK:-$HERE/clean-consumer}"
OUT="${OUT:-$HERE/evidence-$(date +%Y%m%d-%H%M%S)}"
SERVER_LOG="${SERVER_LOG:-$HERE/g8d-server.log}"

mkdir -p "$OUT"
: >"$OUT/00-RESULTS.txt"

SERVER_LOG_START_BYTES=0
if [[ -f "$SERVER_LOG" ]]; then
    SERVER_LOG_START_BYTES="$(wc -c <"$SERVER_LOG" | tr -d '[:space:]')"
fi

"$WORK/app" >"$OUT/app.stdout.log" 2>"$OUT/app.stderr.log"
RC=$?

echo "clean_consumer_exit=$RC" | tee -a "$OUT/00-RESULTS.txt"

FAIL=0
[[ "$RC" -eq 0 ]] || FAIL=1

grep -q '^G8D_CLEAN_CONSUMER_RESULT=PASS$' \
    "$OUT/app.stdout.log" || FAIL=1
grep -q '^G8D_NUMERICAL failures=0$' \
    "$OUT/app.stdout.log" || FAIL=1

cp -a "$WORK/pkg-version.txt" "$OUT/"
cp -a "$WORK/pkg-cflags.txt" "$OUT/"
cp -a "$WORK/pkg-libs.txt" "$OUT/"
cp -a "$WORK/app-readelf-dynamic.txt" "$OUT/"
cp -a "$WORK/app-undefined.txt" "$OUT/"
cp -a "$PREFIX/share/corex-remote-cudart/manifest.txt" "$OUT/"
cp -a "$PREFIX/share/corex-remote-cudart/install-files.txt" "$OUT/"
cp -a "$PREFIX/share/corex-remote-cudart/ABI-SURFACE-v1.1.txt" "$OUT/"

if grep -qi 'libcudart' "$OUT/app-readelf-dynamic.txt"; then
    echo "consumer_libcudart_dependency=PRESENT" >>"$OUT/00-RESULTS.txt"
    FAIL=1
else
    echo "consumer_libcudart_dependency=ABSENT" >>"$OUT/00-RESULTS.txt"
fi

if grep -q 'NEEDED.*libcorex_remote_cudart.so.1' \
       "$OUT/app-readelf-dynamic.txt"; then
    echo "consumer_remote_runtime_dependency=PASS" >>"$OUT/00-RESULTS.txt"
else
    echo "consumer_remote_runtime_dependency=FAIL" >>"$OUT/00-RESULTS.txt"
    FAIL=1
fi

if [[ -f "$SERVER_LOG" ]]; then
    END="$(wc -c <"$SERVER_LOG" | tr -d '[:space:]')"
    if [[ "$END" -lt "$SERVER_LOG_START_BYTES" ]]; then
        SERVER_LOG_START_BYTES=0
    fi

    if [[ "$END" -gt "$SERVER_LOG_START_BYTES" ]]; then
        tail -c "+$((SERVER_LOG_START_BYTES + 1))" "$SERVER_LOG" \
            >"$OUT/G8D-SERVER-RUN.log"
    else
        : >"$OUT/G8D-SERVER-RUN.log"
    fi

    INFO="$(grep -a -c '^GET_DEVICE_INFO request=' "$OUT/G8D-SERVER-RUN.log" || true)"
    GETS="$(grep -a -c '^GET_KERNEL request=' "$OUT/G8D-SERVER-RUN.log" || true)"
    LAUNCHES="$(grep -a -c '^LAUNCH_GENERIC request=' "$OUT/G8D-SERVER-RUN.log" || true)"
    ABI="$(grep -a -c '^ABI_VALIDATE kernel=.* result=PASS$' "$OUT/G8D-SERVER-RUN.log" || true)"
    SESSIONS="$(grep -a -c '^SESSION_START$' "$OUT/G8D-SERVER-RUN.log" || true)"

    echo "server_device_info_count=$INFO" >>"$OUT/00-RESULTS.txt"
    echo "server_get_kernel_count=$GETS" >>"$OUT/00-RESULTS.txt"
    echo "server_launch_generic_count=$LAUNCHES" >>"$OUT/00-RESULTS.txt"
    echo "server_abi_validate_pass_count=$ABI" >>"$OUT/00-RESULTS.txt"
    echo "server_session_count=$SESSIONS" >>"$OUT/00-RESULTS.txt"

    [[ "$INFO" -eq 2 ]] || FAIL=1
    [[ "$GETS" -eq 1 ]] || FAIL=1
    [[ "$LAUNCHES" -eq 1 ]] || FAIL=1
    [[ "$ABI" -eq 1 ]] || FAIL=1
    [[ "$SESSIONS" -eq 1 ]] || FAIL=1
else
    echo "server_log_included=NO" >>"$OUT/00-RESULTS.txt"
    FAIL=1
fi

if [[ "$FAIL" -eq 0 ]]; then
    echo "G8D_RESULT=PASS" | tee -a "$OUT/00-RESULTS.txt"
else
    echo "G8D_RESULT=FAIL" | tee -a "$OUT/00-RESULTS.txt"
fi

ARCHIVE="${OUT}.tar.gz"
tar -C "$(dirname "$OUT")" -czf "$ARCHIVE" "$(basename "$OUT")"

echo "Evidence: $OUT"
echo "Archive : $ARCHIVE"

exit "$FAIL"
