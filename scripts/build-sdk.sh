#!/usr/bin/env bash
set -euo pipefail

COREX="${COREX:-/usr/local/corex-4.4.0}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PREFIX="${PREFIX:-$(cd "$HERE/.." && pwd)/sdk-install}"

COREX="$COREX" "$HERE/build.sh"
PREFIX="$PREFIX" "$HERE/install-sdk.sh"

echo "G8D_BUILD_AND_INSTALL=PASS"
