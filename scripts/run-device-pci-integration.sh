#!/usr/bin/env bash
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
OUT="${OUT:-$ROOT/evidence/m3/s7/device-pci-$(date +%Y%m%d-%H%M%S)}"
mkdir -p "$OUT"
OUT="$OUT" PYTHONDONTWRITEBYTECODE=1 python3 "$ROOT/tests/integration/device_pci_protocol.py" \
    >"$OUT/gate.stdout.log" 2>"$OUT/gate.stderr.log"
grep -q '^M3_S7_DEVICE_PCI_INTEGRATION=PASS$' "$OUT/gate.stdout.log"
grep -q '^RESULT=PASS$' "$OUT/00-RESULTS.txt"
echo "M3_S7_DEVICE_PCI_GATE=PASS"
