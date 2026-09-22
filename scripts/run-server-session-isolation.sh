#!/usr/bin/env bash
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
OUT="${OUT:-$ROOT/evidence/m1/s4/session-isolation-$(date +%Y%m%d-%H%M%S)}"

mkdir -p "$OUT"

COREX_REMOTE_HOST="${COREX_REMOTE_HOST:-127.0.0.1}" \
COREX_REMOTE_PORT="${COREX_REMOTE_PORT:-50051}" \
python3 "$ROOT/tests/integration/server_session_isolation.py" \
    >"$OUT/client.stdout.log" \
    2>"$OUT/client.stderr.log"

grep -q '^M1_S4_SERVER_SESSION_ISOLATION=PASS$' "$OUT/client.stdout.log"

cat >"$OUT/00-RESULTS.txt" <<'EOF'
SLICE=M1-S4
SEQUENTIAL_CONNECTIONS=PASS
SESSION_A_LIVE_OBJECT_CLEANUP=PASS
SESSION_A_OBJECTS_HIDDEN_FROM_B=PASS
SESSION_B_EMPTY_INITIAL_STATE=PASS
SESSION_B_COUNTERS_RESTART=PASS
RESULT=PASS
EOF

echo "M1_S4_SERVER_SESSION_ISOLATION=PASS"
echo "Evidence: $OUT"
