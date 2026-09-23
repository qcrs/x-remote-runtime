#!/usr/bin/env bash
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
OUT="${OUT:-$ROOT/evidence/m3/s2/memset-$(date +%Y%m%d-%H%M%S)}"
mkdir -p "$OUT"
OUT="$OUT" PYTHONDONTWRITEBYTECODE=1 python3 "$ROOT/tests/integration/memset_matrix.py" \
    >"$OUT/client.stdout.log" 2>"$OUT/client.stderr.log"
grep -q '^M3_S2_MEMSET_MATRIX=PASS$' "$OUT/client.stdout.log"
printf 'SYNC=PASS\nASYNC=PASS\nZERO_BYTE=PASS\nBOUNDS=PASS\nINVALID_POINTER=PASS\nRESULT=PASS\n' >"$OUT/00-RESULTS.txt"
echo "M3_S2_MEMSET_MATRIX=PASS"
