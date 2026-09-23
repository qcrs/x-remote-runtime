#!/usr/bin/env bash
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
OUT="${OUT:-$ROOT/evidence/m2/s4/backend-$(date +%Y%m%d-%H%M%S)}"
mkdir -p "$OUT"

if rg -n 'cu[A-Z][A-Za-z0-9_]+\(' "$ROOT/server/runtime_server.c" >"$OUT/runtime-direct-calls.txt"; then
    echo "M2_S4_RUNTIME_DIRECT_CALLS=FAIL" >&2
    exit 1
fi
WRAPPERS="$(rg -c '^CUresult corex_backend_' "$ROOT/server/corex_backend.c")"
[[ "$WRAPPERS" -ge 30 ]]
printf 'RUNTIME_DIRECT_COREX_CALLS=0\nBACKEND_WRAPPERS=%s\nRESULT=PASS\n' "$WRAPPERS" >"$OUT/00-RESULTS.txt"
echo "M2_S4_BACKEND_SEAM=PASS wrappers=$WRAPPERS"
