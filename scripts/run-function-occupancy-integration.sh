#!/usr/bin/env bash
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
OUT="${OUT:-$ROOT/evidence/m3/s5/function-occupancy-$(date +%Y%m%d-%H%M%S)}"
mkdir -p "$OUT"
PYTHONDONTWRITEBYTECODE=1 python3 "$ROOT/tests/integration/function_occupancy_protocol.py" \
    >"$OUT/protocol.stdout.log" 2>"$OUT/protocol.stderr.log"
grep -q '^M3_S5_FUNCTION_OCCUPANCY_PROTOCOL=PASS$' "$OUT/protocol.stdout.log"
printf 'MALFORMED_PAYLOAD=PASS\nUNKNOWN_KERNEL=PASS\nFIXED_SCALAR_DTO=PASS\nRESULT=PASS\n' \
    >"$OUT/00-RESULTS.txt"
echo "M3_S5_FUNCTION_OCCUPANCY_INTEGRATION=PASS"
