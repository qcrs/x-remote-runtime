#!/usr/bin/env bash
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
OUT="${OUT:-$ROOT/evidence/m3/s4/version-$(date +%Y%m%d-%H%M%S)}"
mkdir -p "$OUT"
OUT="$OUT" PYTHONDONTWRITEBYTECODE=1 python3 "$ROOT/tests/integration/version_queries.py" >"$OUT/client.stdout.log" 2>"$OUT/client.stderr.log"
grep -q '^M3_S4_VERSION_QUERIES=PASS ' "$OUT/client.stdout.log"
printf 'DRIVER_VERSION=PASS\nRUNTIME_VERSION=PASS\nSCALAR_DTO=PASS\nRESULT=PASS\n' >"$OUT/00-RESULTS.txt"
echo "M3_S4_VERSION_INTEGRATION=PASS"
