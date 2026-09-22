#!/usr/bin/env bash
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
PORT="${COREX_REMOTE_PORT:-50051}"
SERVER="${SERVER_BINARY:-$ROOT/dist/bin/runtime_server}"
LOG="${SERVER_LOG:-$ROOT/evidence/gate8d/runs/local-server.log}"

if [[ ! -x "$SERVER" ]]; then
    echo "Runtime Server is missing: $SERVER" >&2
    echo "Build first with COREX=/usr/local/corex-4.4.0 ./scripts/build-sdk.sh" >&2
    exit 1
fi

mkdir -p "$(dirname "$LOG")"
echo "Starting CoreX Remote Runtime Server on 127.0.0.1:$PORT"
echo "Log: $LOG"
COREX_REMOTE_PORT="$PORT" stdbuf -oL "$SERVER" 2>&1 | tee -a "$LOG"
