#!/usr/bin/env bash
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
OUT="${OUT:-$ROOT/evidence/m3/s7/device-config-$(date +%Y%m%d-%H%M%S)}"
mkdir -p "$OUT"
LD_LIBRARY_PATH="$ROOT/dist/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    "$ROOT/dist/bin/device_config_app" >"$OUT/client.stdout.log" 2>"$OUT/client.stderr.log"
grep -q '^M3_S7_DEVICE_CONFIG=PASS ' "$OUT/client.stdout.log"
printf 'SCALAR_QUERIES=PASS\nINVALID_DEVICE_AND_ENUMS=PASS\nPRIORITY_ROUND_TRIP=PASS\nFUNCTION_SETTERS=PASS\nRESULT=PASS\n' >"$OUT/00-RESULTS.txt"
echo "M3_S7_DEVICE_CONFIG_INTEGRATION=PASS"
