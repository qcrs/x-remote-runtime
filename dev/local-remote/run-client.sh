#!/usr/bin/env bash
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"

if [[ ! -x "$ROOT/dist/bin/device_identity_app" ]]; then
    echo "Client integration binary is missing: $ROOT/dist/bin/device_identity_app" >&2
    echo "Build first with COREX=/usr/local/corex-4.4.0 ./scripts/build-sdk.sh" >&2
    exit 1
fi

export COREX_REMOTE_HOST="${COREX_REMOTE_HOST:-127.0.0.1}"
export COREX_REMOTE_PORT="${COREX_REMOTE_PORT:-50051}"
exec "$ROOT/scripts/run-integration.sh"
