#!/usr/bin/env bash
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
OUT="${OUT:-$ROOT/evidence/m2/s2/hello-$(date +%Y%m%d-%H%M%S)}"
mkdir -p "$OUT"

OUT="$OUT" PYTHONDONTWRITEBYTECODE=1 python3 "$ROOT/tests/integration/hello_capabilities.py" \
    >"$OUT/client.stdout.log" 2>"$OUT/client.stderr.log"
grep -q '^M2_S2_NEW_PEER=PASS$' "$OUT/client.stdout.log"
grep -q '^M2_S3_UNKNOWN_OPCODE=PASS$' "$OUT/client.stdout.log"
grep -q '^M2_S2_LEGACY_V3=PASS$' "$OUT/client.stdout.log"
grep -q '^M2_S2_MALFORMED_PAYLOAD=REJECTED$' "$OUT/client.stdout.log"
grep -q '^M2_S2_HELLO=NEGOTIATED ' "$OUT/client.stdout.log"
grep -q '^M2_S2_HELLO=LEGACY_V3 ' "$OUT/client.stdout.log"
grep -q 'reason=MALFORMED_HELLO_PAYLOAD' "$OUT/client.stderr.log"

printf 'SLICE=M2-S2\nNEW_CLIENT_NEW_SERVER=PASS\nLEGACY_V3_FALLBACK=PASS\nMALFORMED_CAPABILITY_PAYLOAD=REJECTED\nRESULT=PASS\n' \
    >"$OUT/00-RESULTS.txt"
echo "M2_S2_HELLO_INTEGRATION=PASS"
echo "Evidence: $OUT"
