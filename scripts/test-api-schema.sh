#!/usr/bin/env bash
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
OUT="${OUT:-$ROOT/evidence/m3/s8/schema-$(date +%Y%m%d-%H%M%S)}"
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
sed '0,/"opcode_name": "OP_SYNC"/s//"opcode_name": "OP_STREAM_QUERY"/' "$ROOT/schema/corex_api_schema.json" >"$TMP/duplicate-opcode-name.json"
if python3 "$ROOT/scripts/generate-api-schema.py" --schema "$TMP/duplicate-opcode-name.json" \
    --output "$TMP/duplicate-opcode-name.h" --test-output "$TMP/duplicate-opcode-name.c" \
    >"$TMP/duplicate-opcode-name.stdout" 2>"$TMP/duplicate-opcode-name.stderr"; then
    echo "duplicate opcode name schema was accepted" >&2
    exit 1
fi
grep -q 'duplicate opcode_name OP_STREAM_QUERY' "$TMP/duplicate-opcode-name.stderr"
sed '0,/"opcode_name": "OP_SYNC"/s//"opcode_name": "bad"/' "$ROOT/schema/corex_api_schema.json" >"$TMP/invalid-opcode-name.json"
if python3 "$ROOT/scripts/generate-api-schema.py" --schema "$TMP/invalid-opcode-name.json" \
    --output "$TMP/invalid-opcode-name.h" --test-output "$TMP/invalid-opcode-name.c" \
    >"$TMP/invalid-opcode-name.stdout" 2>"$TMP/invalid-opcode-name.stderr"; then
    echo "invalid opcode name schema was accepted" >&2
    exit 1
fi
grep -q 'opcode_name must match OP_' "$TMP/invalid-opcode-name.stderr"
sed '0,/"type": "i32"/s//"type": "unknown_type"/' "$ROOT/schema/corex_api_schema.json" >"$TMP/unknown-type.json"
if python3 "$ROOT/scripts/generate-api-schema.py" --schema "$TMP/unknown-type.json" \
    --output "$TMP/unknown-type.h" --test-output "$TMP/unknown-type.c" \
    >"$TMP/unknown-type.stdout" 2>"$TMP/unknown-type.stderr"; then
    echo "unknown field type schema was accepted" >&2
    exit 1
fi
grep -q 'unknown request field type' "$TMP/unknown-type.stderr"
mkdir -p "$TMP/bounded"
python3 "$ROOT/scripts/generate-api-schema.py" \
    --schema "$ROOT/tests/schema/bounded_types.json" \
    --output "$TMP/bounded/corex_api_schema.h" \
    --test-output "$TMP/bounded/corex_api_schema_test.c"
gcc -O2 -Wall -Wextra -Werror -std=gnu11 \
    -I"$ROOT/include/internal" -I"$TMP/bounded" \
    "$TMP/bounded/corex_api_schema_test.c" -o "$TMP/bounded/test"
"$TMP/bounded/test" >"$TMP/bounded.stdout"
grep -q '^M3_S6_GENERATED_SCHEMA_TEST=PASS$' "$TMP/bounded.stdout"
PYTHONDONTWRITEBYTECODE=1 python3 "$ROOT/scripts/verify-api-contracts.py" "$ROOT"
PYTHONDONTWRITEBYTECODE=1 python3 "$ROOT/scripts/report-api-coverage.py" \
    "$ROOT/compat/cuda-api-ledger.csv" >"$TMP/coverage.txt"
for status in IMPLEMENTED PARTIAL_IMPLEMENTED GROUND_TRUTH_REQUIRED BACKEND_UNSUPPORTED DEPRECATED OUT_OF_SCOPE_CURRENT; do
    grep -Eq "^$status=[0-9]+$" "$TMP/coverage.txt"
done
grep -q '^BACKEND_UNSUPPORTED=0$' "$TMP/coverage.txt"
grep -q '^GROUND_TRUTH_BLOCKED=0$' "$TMP/coverage.txt"
cmp "$TMP/one.h" "$ROOT/include/generated/corex_api_schema.h"
cmp "$TMP/one.c" "$ROOT/tests/generated/corex_api_schema_test.c"
PYTHONDONTWRITEBYTECODE=1 python3 "$ROOT/scripts/verify-evidence-links.py" "$ROOT"
if find "$ROOT/evidence/m3/s8" -type f \( -name generated.h -o -name generated-test.c \) -print -quit | grep -q .; then
    echo "new evidence contains copied generated artifacts" >&2
    exit 1
fi
cp "$TMP/coverage.txt" "$OUT/API-COVERAGE.txt"
(
    cd "$ROOT"
    sha256sum schema/corex_api_schema.json include/generated/corex_api_schema.h \
        tests/generated/corex_api_schema_test.c
) >"$OUT/MANIFEST.sha256"
printf '%s\n' \
    'python3 scripts/generate-api-schema.py (twice)' \
    'python3 scripts/verify-api-contracts.py' \
    'python3 scripts/report-api-coverage.py' \
    'python3 scripts/verify-evidence-links.py' >"$OUT/COMMANDS.txt"
printf 'SCHEMA_VERSION=2\nABI=COREX_REMOTE_CUDART_1.1\n' >"$OUT/ENVIRONMENT.txt"
printf 'REGENERATE_TWICE_ZERO_DIFF=PASS\nGENERATED_TREE_CLEAN=PASS\nINVALID_SCHEMA_REJECTED=PASS\nDUPLICATE_API_REJECTED=PASS\nDUPLICATE_OPCODE_REJECTED=PASS\nUNKNOWN_FIELD_TYPE_REJECTED=PASS\nBOUNDED_BYTES_STRING_CODEC=PASS\nAPI_CONTRACTS=PASS\nCOVERAGE_STATUS_ZEROES=PASS\nRESULT=PASS\n' >"$OUT/00-RESULTS.txt"
echo "M3_S8_SCHEMA_REPRODUCIBLE=PASS"
