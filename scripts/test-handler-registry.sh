#!/usr/bin/env bash
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
OUT="${OUT:-$ROOT/evidence/m2/s3/registry-$(date +%Y%m%d-%H%M%S)}"
COREX="${COREX:-/usr/local/corex-4.4.0}"
mkdir -p "$OUT"

if gcc -std=gnu11 -Wall -Wextra -Werror -DCOREX_TEST_DUPLICATE_OPCODE \
    -I"$COREX/include" -I"$ROOT/include/internal" -I"$ROOT/include/generated" -fsyntax-only \
    "$ROOT/server/runtime_server.c" "$ROOT/server/corex_backend.c" \
    >"$OUT/duplicate.stdout.log" 2>"$OUT/duplicate.stderr.log"; then
    echo "M2_S3_DUPLICATE_OPCODE=UNDETECTED" >&2
    exit 1
fi
grep -q 'duplicate case value' "$OUT/duplicate.stderr.log"
printf 'DUPLICATE_OPCODE_COMPILE=REJECTED\nRESULT=PASS\n' >"$OUT/00-RESULTS.txt"
echo "M2_S3_DUPLICATE_OPCODE=REJECTED"
