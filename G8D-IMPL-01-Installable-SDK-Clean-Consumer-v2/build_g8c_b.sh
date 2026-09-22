#!/usr/bin/env bash
set -euo pipefail

COREX="${COREX:-/usr/local/corex-4.4.0}"
CXX="${CXX:-$COREX/bin/clang++}"
CC="${CC:-gcc}"
GXX="${GXX:-g++}"

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="$HERE/build"
DIST="$HERE/dist"
OBJ="$BUILD/obj"

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

# Server: new CRX9/V3 binary, existing CoreX Driver backend.
"$CC" \
    -O2 \
    -Wall \
    -Wextra \
    -Werror \
    -std=gnu11 \
    -I"$COREX/include" \
    -I"$HERE" \
    "$HERE/runtime_server_g8c.c" \
    "$HERE/corex_metadata.c" \
    -L"$COREX/lib64" \
    -Wl,-rpath,"$COREX/lib64" \
    -lcuda \
    -o "$DIST/bin/runtime_server_g8c"

# Client runtime internals.
"$CC" -O2 -Wall -Wextra -Werror -std=gnu11 -fPIC \
    -I"$HERE" \
    -c "$HERE/corex_remote_cuda_g8c.c" \
    -o "$OBJ/corex_remote_cuda_g8c.o" \
    -pthread

"$CC" -O2 -Wall -Wextra -Werror -std=gnu11 -fPIC \
    -I"$HERE" \
    -c "$HERE/corex_launch_config_g8c.c" \
    -o "$OBJ/corex_launch_config_g8c.o"

"$CC" -O2 -Wall -Wextra -Werror -std=gnu11 -fPIC -fvisibility=hidden \
    -I"$HERE" \
    -c "$HERE/g7c_corex_fatbin_runtime.c" \
    -o "$OBJ/g7c_corex_fatbin_runtime.o"

"$CC" -O2 -Wall -Wextra -Werror -std=gnu11 -fPIC -fvisibility=hidden \
    -I"$HERE" \
    -c "$HERE/corex_metadata.c" \
    -o "$OBJ/corex_metadata.o"

# Vendor-facing ABI adapter: this is deliberately compiled against the real
# pinned CoreX runtime header so cudaDeviceProp layout is not redefined locally.
"$CC" -O2 -Wall -Wextra -Werror -std=gnu11 -fPIC \
    -I"$COREX/include" \
    -I"$HERE" \
    -c "$HERE/corex_device_api_g8c.c" \
    -o "$OBJ/corex_device_api_g8c.o"

"$CC" -shared \
    -Wl,-z,defs \
    -Wl,-soname,"$SONAME" \
    -Wl,--version-script="$HERE/corex_remote_cudart.map" \
    "$OBJ/corex_remote_cuda_g8c.o" \
    "$OBJ/corex_launch_config_g8c.o" \
    "$OBJ/corex_device_api_g8c.o" \
    "$OBJ/g7c_corex_fatbin_runtime.o" \
    "$OBJ/corex_metadata.o" \
    -ldl -lm -pthread \
    -o "$DIST/lib/$REAL_LIB"

ln -s "$REAL_LIB" "$DIST/lib/$SONAME"
ln -s "$SONAME" "$DIST/lib/libcorex_remote_cudart.so"

cp "$HERE/corex_remote_cudart_ext.h" \
   "$DIST/include/corex_remote_cudart_ext.h"
cp "$HERE/ABI-SURFACE-v1.1.txt" \
   "$DIST/share/corex-remote-cudart/ABI-SURFACE-v1.1.txt"

readelf -dW "$DIST/lib/$REAL_LIB" \
    >"$BUILD/library-readelf-dynamic.txt"
readelf --version-info "$DIST/lib/$REAL_LIB" \
    >"$BUILD/library-version-info.txt"
nm -D --defined-only "$DIST/lib/$REAL_LIB" \
    >"$BUILD/library-dynamic-defined.txt"

python3 "$HERE/verify_g8c_abi.py" \
    "$DIST/lib/$REAL_LIB" \
    "$HERE/EXPECTED-EXPORTED-FUNCTIONS.txt" \
    | tee "$BUILD/G8C-ABI-AUDIT.txt"

python3 "$HERE/verify_g8c_scope.py" "$HERE" \
    | tee "$BUILD/G8C-SCOPE-AUDIT.txt"

if grep -qi 'libcudart' "$BUILD/library-readelf-dynamic.txt"; then
    echo "G8C_LIBRARY_LIBCUDART_DEPENDENCY=FAIL"
    exit 1
fi

# Ordinary compiler-generated CUDA translation unit.
"$CXX" -x ivcore \
    --cuda-path="$COREX" \
    -I"$COREX/include" \
    "${EXTRA[@]}" \
    -c "$HERE/g8c_device_identity_app.cu" \
    -o "$OBJ/g8c_device_identity_app.o"

"$GXX" \
    "$OBJ/g8c_device_identity_app.o" \
    -L"$DIST/lib" \
    -lcorex_remote_cudart \
    -Wl,-rpath,'$ORIGIN/../lib' \
    -o "$DIST/bin/g8c_device_identity_app"

readelf -dW "$DIST/bin/g8c_device_identity_app" \
    >"$BUILD/app-readelf-dynamic.txt"

if grep -qi 'libcudart' "$BUILD/app-readelf-dynamic.txt"; then
    echo "G8C_APP_LIBCUDART_DEPENDENCY=FAIL"
    exit 1
fi

echo "G8C_PROTOCOL=CRX9/V3"
echo "G8C_ABI_VERSION=COREX_REMOTE_CUDART_1.1"
echo "G8C_LIBRARY_LIBCUDART_DEPENDENCY=ABSENT"
echo "G8C_APP_LIBCUDART_DEPENDENCY=ABSENT"
echo "G8C_BUILD=PASS"
