#!/usr/bin/env bash
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
OUT="${OUT:-$ROOT/evidence/m2/s5/schema-$(date +%Y%m%d-%H%M%S)}"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$OUT"

python3 "$ROOT/scripts/generate-api-schema.py" --schema "$ROOT/schema/corex_api_schema.json" \
    --output "$TMP/one.h" --test-output "$TMP/one.c"
python3 "$ROOT/scripts/generate-api-schema.py" --schema "$ROOT/schema/corex_api_schema.json" \
    --output "$TMP/two.h" --test-output "$TMP/two.c"
cmp "$TMP/one.h" "$TMP/two.h"
cmp "$TMP/one.c" "$TMP/two.c"
sed 's/OP_SYNC/OP_UNKNOWN/' "$ROOT/schema/corex_api_schema.json" >"$TMP/invalid.json"
if python3 "$ROOT/scripts/generate-api-schema.py" --schema "$TMP/invalid.json" \
    --output "$TMP/invalid.h" --test-output "$TMP/invalid.c" \
    >"$TMP/invalid.stdout" 2>"$TMP/invalid.stderr"; then
    echo "unknown schema was accepted" >&2
    exit 1
fi
grep -q 'unsupported opcode/capability' "$TMP/invalid.stderr"
cp "$TMP/one.h" "$OUT/generated.h"
cp "$TMP/one.c" "$OUT/generated-test.c"
printf 'REGENERATE_TWICE_ZERO_DIFF=PASS\nUNKNOWN_SCHEMA_REJECTED=PASS\nRESULT=PASS\n' >"$OUT/00-RESULTS.txt"
echo "M2_S5_SCHEMA_REPRODUCIBLE=PASS"
