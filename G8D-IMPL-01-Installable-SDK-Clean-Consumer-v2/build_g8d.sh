#!/usr/bin/env bash
set -euo pipefail

COREX="${COREX:-/usr/local/corex-4.4.0}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PREFIX="${PREFIX:-$HERE/sdk-install}"

COREX="$COREX" "$HERE/build_g8c_b.sh"
PREFIX="$PREFIX" "$HERE/install_g8d_sdk.sh"

echo "G8D_BUILD_AND_INSTALL=PASS"
