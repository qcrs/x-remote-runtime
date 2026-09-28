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
sed 's/"opcode": 4,/"opcode": 0,/' "$ROOT/schema/corex_api_schema.json" >"$TMP/invalid.json"
if python3 "$ROOT/scripts/generate-api-schema.py" --schema "$TMP/invalid.json" \
    --output "$TMP/invalid.h" --test-output "$TMP/invalid.c" \
    >"$TMP/invalid.stdout" 2>"$TMP/invalid.stderr"; then
    echo "unknown schema was accepted" >&2
    exit 1
fi
grep -q 'opcode must be an explicit positive u32' "$TMP/invalid.stderr"
sed '0,/"name": "cudaDeviceSynchronize"/s//"name": "cudaStreamQuery"/' "$ROOT/schema/corex_api_schema.json" >"$TMP/duplicate-api.json"
if python3 "$ROOT/scripts/generate-api-schema.py" --schema "$TMP/duplicate-api.json" \
    --output "$TMP/duplicate-api.h" --test-output "$TMP/duplicate-api.c" \
    >"$TMP/duplicate-api.stdout" 2>"$TMP/duplicate-api.stderr"; then
    echo "duplicate API schema was accepted" >&2
    exit 1
fi
grep -q 'duplicate API cudaStreamQuery' "$TMP/duplicate-api.stderr"
sed '0,/"opcode": 4,/s//"opcode": 14,/' "$ROOT/schema/corex_api_schema.json" >"$TMP/duplicate-opcode.json"
if python3 "$ROOT/scripts/generate-api-schema.py" --schema "$TMP/duplicate-opcode.json" \
    --output "$TMP/duplicate-opcode.h" --test-output "$TMP/duplicate-opcode.c" \
    >"$TMP/duplicate-opcode.stdout" 2>"$TMP/duplicate-opcode.stderr"; then
    echo "duplicate opcode schema was accepted" >&2
    exit 1
fi
grep -q 'duplicate opcode 14' "$TMP/duplicate-opcode.stderr"
sed '0,/"type": "i32"/s//"type": "unknown_type"/' "$ROOT/schema/corex_api_schema.json" >"$TMP/unknown-type.json"
if python3 "$ROOT/scripts/generate-api-schema.py" --schema "$TMP/unknown-type.json" \
    --output "$TMP/unknown-type.h" --test-output "$TMP/unknown-type.c" \
    >"$TMP/unknown-type.stdout" 2>"$TMP/unknown-type.stderr"; then
    echo "unknown field type schema was accepted" >&2
    exit 1
fi
grep -q 'unknown request field type' "$TMP/unknown-type.stderr"
PYTHONDONTWRITEBYTECODE=1 python3 "$ROOT/scripts/verify-api-contracts.py" "$ROOT"
cp "$TMP/one.h" "$OUT/generated.h"
cp "$TMP/one.c" "$OUT/generated-test.c"
printf 'REGENERATE_TWICE_ZERO_DIFF=PASS\nINVALID_SCHEMA_REJECTED=PASS\nDUPLICATE_API_REJECTED=PASS\nDUPLICATE_OPCODE_REJECTED=PASS\nUNKNOWN_FIELD_TYPE_REJECTED=PASS\nAPI_CONTRACTS=PASS\nRESULT=PASS\n' >"$OUT/00-RESULTS.txt"
echo "M3_S6_SCHEMA_REPRODUCIBLE=PASS"
