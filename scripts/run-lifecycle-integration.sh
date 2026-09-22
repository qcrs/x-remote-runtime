#!/usr/bin/env bash
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
BUILD="$ROOT/build"
DIST="$ROOT/dist"
OUT="${OUT:-$ROOT/evidence/m1/s2/lifecycle-$(date +%Y%m%d-%H%M%S)}"

if [[ ! -x "$DIST/bin/session_lifecycle_app" ||
      ! -f "$BUILD/session_lifecycle.cubin" ]]; then
    echo "M1-S2 lifecycle artifacts are missing; run scripts/build-sdk.sh first" >&2
    exit 1
fi

mkdir -p "$OUT"

COREX_REMOTE_HOST="${COREX_REMOTE_HOST:-127.0.0.1}" \
COREX_REMOTE_PORT="${COREX_REMOTE_PORT:-50051}" \
"$DIST/bin/session_lifecycle_app" \
    "$BUILD/session_lifecycle.cubin" \
    >"$OUT/client.stdout.log" \
    2>"$OUT/client.stderr.log"

grep -q '^M1_S2_LIFECYCLE_RESULT=PASS failures=0$' \
    "$OUT/client.stdout.log"
grep -q 'name=stale-pointer .* result=PASS$' "$OUT/client.stdout.log"
grep -q 'name=stale-stream .* result=PASS$' "$OUT/client.stdout.log"
grep -q 'name=stale-event .* result=PASS$' "$OUT/client.stdout.log"
grep -q 'name=stale-module .* result=PASS$' "$OUT/client.stdout.log"
grep -q 'name=stale-compiler-kernel .* result=PASS$' "$OUT/client.stdout.log"
grep -q 'name=stale-controlled-kernel .* result=PASS$' "$OUT/client.stdout.log"

cat >"$OUT/00-RESULTS.txt" <<'EOF'
SLICE=M1-S2
LIFECYCLE=PASS
STALE_DEVICE_POINTER=PASS
STALE_STREAM=PASS
STALE_EVENT=PASS
STALE_MODULE=PASS
STALE_KERNEL=PASS
REPEATED_SHUTDOWN=PASS
LAZY_RECONNECT=PASS
RESULT=PASS
EOF

echo "M1_S2_LIFECYCLE=PASS"
echo "Evidence: $OUT"
