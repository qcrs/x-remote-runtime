#!/usr/bin/env bash
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
PREFIX="${PREFIX:-$ROOT/sdk-install}"

if [[ ! -f "$ROOT/dist/lib/libcorex_remote_cudart.so.1.1.0" ]]; then
    echo "G8D_INSTALL=FAIL reason=BUILD_FIRST"
    exit 1
fi

rm -rf "$PREFIX"
mkdir -p \
    "$PREFIX/bin" \
    "$PREFIX/include" \
    "$PREFIX/lib/pkgconfig" \
    "$PREFIX/share/corex-remote-cudart"

cp -a "$ROOT/dist/lib/libcorex_remote_cudart.so.1.1.0" "$PREFIX/lib/"
ln -s "libcorex_remote_cudart.so.1.1.0" "$PREFIX/lib/libcorex_remote_cudart.so.1"
ln -s "libcorex_remote_cudart.so.1" "$PREFIX/lib/libcorex_remote_cudart.so"

cp -a "$ROOT/dist/bin/runtime_server" "$PREFIX/bin/corex-remote-runtime-server"
cp -a "$ROOT/include/corex_remote_cudart_ext.h" "$PREFIX/include/"
cp -a "$ROOT/packaging/ABI-SURFACE-v1.1.txt" \
   "$PREFIX/share/corex-remote-cudart/"

cat >"$PREFIX/lib/pkgconfig/corex-remote-cudart.pc" <<EOF
prefix=$PREFIX
exec_prefix=\${prefix}
libdir=\${prefix}/lib
includedir=\${prefix}/include

Name: corex-remote-cudart
Description: Remote CUDA Runtime compatibility layer for CoreX
Version: 1.1.0
Libs: -L\${libdir} -lcorex_remote_cudart
Cflags: -I\${includedir}
EOF

cat >"$PREFIX/share/corex-remote-cudart/manifest.txt" <<EOF
runtime_name=corex-remote-cudart
runtime_version=1.1.0
library_soname=libcorex_remote_cudart.so.1
protocol_magic=CRX9
protocol_version=3
protocol_opcode_device_info=26
cuda_abi_base=COREX_REMOTE_CUDART_1.0
cuda_abi_device_identity=COREX_REMOTE_CUDART_1.1
extension_abi=COREX_REMOTE_EXT_1.0
server_binary=corex-remote-runtime-server
EOF

find "$PREFIX" \( -type f -o -type l \) |
    sed "s#^$PREFIX/##" |
    sort >"$PREFIX/share/corex-remote-cudart/install-files.txt"

echo "G8D_PREFIX=$PREFIX"
echo "G8D_INSTALL=PASS"
