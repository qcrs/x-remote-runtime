#!/usr/bin/env bash
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
BUILD="$ROOT/build"
DIST="$ROOT/dist"
OUT="${OUT:-$ROOT/evidence/m1/s1/multithread-$(date +%Y%m%d-%H%M%S)}"

if [[ ! -x "$DIST/bin/multithread_runtime_app" ]]; then
    echo "Multithread integration binary is missing: $DIST/bin/multithread_runtime_app" >&2
    echo "Build first with COREX=/usr/local/corex-4.4.0 ./scripts/build-sdk.sh" >&2
    exit 1
fi

mkdir -p "$OUT"

COREX_REMOTE_HOST="${COREX_REMOTE_HOST:-127.0.0.1}" \
COREX_REMOTE_PORT="${COREX_REMOTE_PORT:-50051}" \
"$DIST/bin/multithread_runtime_app" \
    >"$OUT/client.stdout.log" \
    2>"$OUT/client.stderr.log"

cp -a "$BUILD/G8C-ABI-AUDIT.txt" "$OUT/"
cp -a "$BUILD/G8C-SCOPE-AUDIT.txt" "$OUT/"

grep -q '^M1_S1_MULTITHREAD_RESULT=PASS$' "$OUT/client.stdout.log"

echo "M1_S1_MULTITHREAD=PASS" | tee "$OUT/00-RESULTS.txt"
echo "M1_S1_RESULT=PASS" | tee -a "$OUT/00-RESULTS.txt"
echo "Evidence: $OUT"
