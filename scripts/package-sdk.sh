#!/usr/bin/env bash
# Archive an installed SDK. The pkg-config prefix follows the extracted location.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
PREFIX="${PREFIX:-$ROOT/sdk-install}"
ARCHIVE="${ARCHIVE:-$ROOT/dist/corex-remote-sdk-1.1.0-aarch64.tar.gz}"
[[ -f "$PREFIX/share/corex-remote-cudart/manifest.txt" ]]
mkdir -p "$(dirname "$ARCHIVE")"
tar -C "$PREFIX" -czf "$ARCHIVE" bin include lib share
echo "SDK_ARCHIVE=$ARCHIVE"
echo "SDK_PACKAGE=PASS"
