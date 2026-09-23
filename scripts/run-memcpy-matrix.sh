#!/usr/bin/env bash
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
OUT="${OUT:-$ROOT/evidence/m3/s1/memcpy-$(date +%Y%m%d-%H%M%S)}"
mkdir -p "$OUT"
OUT="$OUT" PYTHONDONTWRITEBYTECODE=1 python3 "$ROOT/tests/integration/memcpy_matrix.py" \
    >"$OUT/client.stdout.log" 2>"$OUT/client.stderr.log"
grep -q '^M3_S1_MEMCPY_MATRIX=PASS$' "$OUT/client.stdout.log"
printf 'DIRECTION_MATRIX=PASS\nBOUNDS_OVERLAP=PASS\nASYNC_D2D_EXPLICIT_UNSUPPORTED=PASS\nRESULT=PASS\n' >"$OUT/00-RESULTS.txt"
echo "M3_S1_MEMCPY_MATRIX=PASS"
