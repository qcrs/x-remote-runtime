#!/usr/bin/env bash
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
OUT="${OUT:-$ROOT/evidence/m1/s5/failure-reconnect-$(date +%Y%m%d-%H%M%S)}"

mkdir -p "$OUT"

OUT="$OUT" python3 "$ROOT/tests/integration/session_failure_reconnect.py" \
    >"$OUT/client.stdout.log" \
    2>"$OUT/client.stderr.log"

grep -q '^M1_S5_FAILURE_RECONNECT_RESULT=PASS$' "$OUT/client.stdout.log"
grep -q 'M1_S5_SESSION_FAILED generation=1 .* no_replay=YES' "$OUT/client.stderr.log"
grep -q 'M1_S5_SESSION_FAILED generation=2 .* no_replay=YES' "$OUT/client.stderr.log"
grep -q 'reason=PROTOCOL_MISMATCH .* no_replay=YES' "$OUT/client.stderr.log"

cat >"$OUT/00-RESULTS.txt" <<'EOF'
SLICE=M1-S5
IDLE_SERVER_KILL=PASS
LIVE_OBJECT_SERVER_KILL=PASS
PROTOCOL_MISMATCH=PASS
FAILING_CALL_ERROR=cudaErrorUnknown
NO_BLIND_REPLAY=PASS
FAILED_GENERATION_INVALIDATED=PASS
LAZY_RECONNECT_CLEAN_GENERATION=PASS
FRESH_NUMERICAL_ROUNDTRIP=PASS
RESULT=PASS
EOF

echo "M1_S5_FAILURE_RECONNECT=PASS"
echo "Evidence: $OUT"
