#!/usr/bin/env bash
# Run after build-sdk.sh; build.sh already checks ABI, scope and protocol fixtures.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
OUT="${OUT:-$ROOT/build/qualification/static}"
OUT="$OUT/schema" "$HERE/test-api-schema.sh"
OUT="$OUT/registry" "$HERE/test-handler-registry.sh"
OUT="$OUT/backend" "$HERE/test-backend-seam.sh"
echo "STATIC_QUALIFICATION=PASS"
