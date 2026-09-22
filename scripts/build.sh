#!/usr/bin/env bash
set -euo pipefail

COREX="${COREX:-/usr/local/corex-4.4.0}"
CXX="${CXX:-$COREX/bin/clang++}"
CC="${CC:-gcc}"
GXX="${GXX:-g++}"

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
BUILD="$ROOT/build"
DIST="$ROOT/dist"
OBJ="$BUILD/obj"
SRC="$ROOT/src"
INTERNAL="$ROOT/include/internal"
SERVER="$ROOT/server"
TESTS="$ROOT/tests/integration"
PACKAGING="$ROOT/packaging"

VERSION="1.1.0"
SONAME_MAJOR="1"
REAL_LIB="libcorex_remote_cudart.so.${VERSION}"
SONAME="libcorex_remote_cudart.so.${SONAME_MAJOR}"

rm -rf "$BUILD" "$DIST"
mkdir -p \
    "$OBJ" \
    "$DIST/bin" \
    "$DIST/include" \
    "$DIST/lib" \
    "$DIST/share/corex-remote-cudart"

GCC_MAJOR="$("$GXX" -dumpfullversion -dumpversion | cut -d. -f1)"
EXTRA=()
[[ -d "/usr/include/c++/$GCC_MAJOR" ]] &&
    EXTRA+=(-isystem "/usr/include/c++/$GCC_MAJOR")
[[ -d "/usr/include/aarch64-linux-gnu/c++/$GCC_MAJOR" ]] &&
    EXTRA+=(-isystem "/usr/include/aarch64-linux-gnu/c++/$GCC_MAJOR")
[[ -d "/usr/include/c++/$GCC_MAJOR/backward" ]] &&
    EXTRA+=(-isystem "/usr/include/c++/$GCC_MAJOR/backward")

echo "COREX=$COREX"
echo "CXX=$CXX"
echo "CC=$CC"
echo "GXX=$GXX"

"$CC" \
    -O2 -Wall -Wextra -Werror -std=gnu11 \
    -I"$COREX/include" \
    -I"$INTERNAL" \
    "$SERVER/runtime_server.c" \
    "$SRC/corex_metadata.c" \
    -L"$COREX/lib64" \
    -Wl,-rpath,"$COREX/lib64" \
    -lcuda \
    -o "$DIST/bin/runtime_server"

"$CC" -O2 -Wall -Wextra -Werror -std=gnu11 -fPIC \
    -I"$INTERNAL" \
    -c "$SRC/corex_remote_cuda.c" \
    -o "$OBJ/corex_remote_cuda.o" \
    -pthread

"$CC" -O2 -Wall -Wextra -Werror -std=gnu11 -fPIC \
    -I"$INTERNAL" \
    -c "$SRC/corex_launch_config.c" \
    -o "$OBJ/corex_launch_config.o"

"$CC" -O2 -Wall -Wextra -Werror -std=gnu11 -fPIC -fvisibility=hidden \
    -I"$INTERNAL" \
    -c "$SRC/corex_fatbin_runtime.c" \
    -o "$OBJ/corex_fatbin_runtime.o"

"$CC" -O2 -Wall -Wextra -Werror -std=gnu11 -fPIC -fvisibility=hidden \
    -I"$INTERNAL" \
    -c "$SRC/corex_metadata.c" \
    -o "$OBJ/corex_metadata.o"

"$CC" -O2 -Wall -Wextra -Werror -std=gnu11 -fPIC \
    -I"$COREX/include" \
    -I"$INTERNAL" \
    -c "$SRC/corex_device_api.c" \
    -o "$OBJ/corex_device_api.o"

"$CC" -shared \
    -Wl,-z,defs \
    -Wl,-soname,"$SONAME" \
    -Wl,--version-script="$PACKAGING/corex_remote_cudart.map" \
    "$OBJ/corex_remote_cuda.o" \
    "$OBJ/corex_launch_config.o" \
    "$OBJ/corex_device_api.o" \
    "$OBJ/corex_fatbin_runtime.o" \
    "$OBJ/corex_metadata.o" \
    -ldl -lm -pthread \
    -o "$DIST/lib/$REAL_LIB"

ln -s "$REAL_LIB" "$DIST/lib/$SONAME"
ln -s "$SONAME" "$DIST/lib/libcorex_remote_cudart.so"

cp "$ROOT/include/corex_remote_cudart_ext.h" \
   "$DIST/include/corex_remote_cudart_ext.h"
cp "$PACKAGING/ABI-SURFACE-v1.1.txt" \
   "$DIST/share/corex-remote-cudart/ABI-SURFACE-v1.1.txt"

readelf -dW "$DIST/lib/$REAL_LIB" >"$BUILD/library-readelf-dynamic.txt"
readelf --version-info "$DIST/lib/$REAL_LIB" >"$BUILD/library-version-info.txt"
nm -D --defined-only "$DIST/lib/$REAL_LIB" >"$BUILD/library-dynamic-defined.txt"

python3 "$HERE/verify-abi.py" \
    "$DIST/lib/$REAL_LIB" \
    "$PACKAGING/EXPECTED-EXPORTED-FUNCTIONS.txt" \
    | tee "$BUILD/G8C-ABI-AUDIT.txt"

python3 "$HERE/verify-scope.py" "$ROOT" \
    | tee "$BUILD/G8C-SCOPE-AUDIT.txt"

if grep -qi 'libcudart' "$BUILD/library-readelf-dynamic.txt"; then
    echo "G8C_LIBRARY_LIBCUDART_DEPENDENCY=FAIL"
    exit 1
fi

"$CXX" -x ivcore \
    --cuda-path="$COREX" \
    -I"$COREX/include" \
    "${EXTRA[@]}" \
    -c "$TESTS/device_identity.cu" \
    -o "$OBJ/device_identity.o"

"$GXX" \
    "$OBJ/device_identity.o" \
    -L"$DIST/lib" \
    -lcorex_remote_cudart \
    -Wl,-rpath,'$ORIGIN/../lib' \
    -o "$DIST/bin/device_identity_app"

readelf -dW "$DIST/bin/device_identity_app" >"$BUILD/app-readelf-dynamic.txt"

if grep -qi 'libcudart' "$BUILD/app-readelf-dynamic.txt"; then
    echo "G8C_APP_LIBCUDART_DEPENDENCY=FAIL"
    exit 1
fi

echo "G8C_PROTOCOL=CRX9/V3"
echo "G8C_ABI_VERSION=COREX_REMOTE_CUDART_1.1"
echo "G8C_LIBRARY_LIBCUDART_DEPENDENCY=ABSENT"
echo "G8C_APP_LIBCUDART_DEPENDENCY=ABSENT"
echo "G8C_BUILD=PASS"
