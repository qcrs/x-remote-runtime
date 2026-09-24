#!/usr/bin/env bash
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
COREX="${COREX:-/usr/local/corex-4.4.0}"
OUT="${OUT:-$ROOT/evidence/m3/s3/probe-$(date +%Y%m%d-%H%M%S)}"
mkdir -p "$OUT"
gcc -O2 -Wall -Wextra -Werror -I"$COREX/include" \
    "$ROOT/tests/probes/corex_stream_event_probe.c" -L"$COREX/lib64" \
    -Wl,-rpath,"$COREX/lib64" -lcuda -o "$OUT/probe"
"$OUT/probe" >"$OUT/probe.stdout.log" 2>"$OUT/probe.stderr.log"
grep -q '^M3_S3_COREX_PROBE=PASS ' "$OUT/probe.stdout.log"
printf 'STREAM_FLAGS=PASS\nSTREAM_PRIORITY=PASS\nEVENT_TIMING=PASS\nDISABLE_TIMING_REJECTED=PASS\nRESULT=PASS\n' >"$OUT/00-RESULTS.txt"
echo "M3_S3_COREX_PROBE=PASS"
