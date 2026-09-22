#!/usr/bin/env bash
set -euo pipefail

COREX="${COREX:-/usr/local/corex-4.4.0}"
CXX="${CXX:-$COREX/bin/clang++}"
GXX="${GXX:-g++}"

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PREFIX="${PREFIX:-$HERE/sdk-install}"
WORK="${WORK:-$HERE/clean-consumer}"

rm -rf "$WORK"
mkdir -p "$WORK"

cp "$HERE/g8d_external_consumer.cu" "$WORK/main.cu"

PC_FILE="$PREFIX/lib/pkgconfig/corex-remote-cudart.pc"

if [[ ! -f "$PC_FILE" ]]; then
    echo "G8D_PKGCONFIG_METADATA=FAIL reason=MISSING_PC_FILE"
    exit 1
fi

grep -q '^Name: corex-remote-cudart$' "$PC_FILE"
grep -q '^Version: 1.1.0$' "$PC_FILE"
grep -q '^Libs: .* -lcorex_remote_cudart$' "$PC_FILE"

if command -v pkg-config >/dev/null 2>&1; then
    export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig"

    pkg-config --modversion corex-remote-cudart \
        >"$WORK/pkg-version.txt"
    pkg-config --cflags corex-remote-cudart \
        >"$WORK/pkg-cflags.txt"
    pkg-config --libs corex-remote-cudart \
        >"$WORK/pkg-libs.txt"

    G8D_PKGCONFIG_MODE="TOOL"
else
    printf '%s\n' '1.1.0' >"$WORK/pkg-version.txt"
    printf -- '-I%s/include\n' "$PREFIX" >"$WORK/pkg-cflags.txt"
    printf -- '-L%s/lib -lcorex_remote_cudart\n' "$PREFIX" \
        >"$WORK/pkg-libs.txt"

    G8D_PKGCONFIG_MODE="FALLBACK"
fi

echo "G8D_PKGCONFIG_METADATA=PASS"
echo "G8D_PKGCONFIG_MODE=$G8D_PKGCONFIG_MODE"

GCC_MAJOR="$("$GXX" -dumpfullversion -dumpversion | cut -d. -f1)"
EXTRA=()
[[ -d "/usr/include/c++/$GCC_MAJOR" ]] &&
    EXTRA+=(-isystem "/usr/include/c++/$GCC_MAJOR")
[[ -d "/usr/include/aarch64-linux-gnu/c++/$GCC_MAJOR" ]] &&
    EXTRA+=(-isystem "/usr/include/aarch64-linux-gnu/c++/$GCC_MAJOR")
[[ -d "/usr/include/c++/$GCC_MAJOR/backward" ]] &&
    EXTRA+=(-isystem "/usr/include/c++/$GCC_MAJOR/backward")

(
    cd "$WORK"

    "$CXX" -x ivcore \
        --cuda-path="$COREX" \
        -I"$COREX/include" \
        "${EXTRA[@]}" \
        -c main.cu \
        -o main.o

    read -r -a SDK_LIBS < <(cat "$WORK/pkg-libs.txt")

    "$GXX" \
        main.o \
        "${SDK_LIBS[@]}" \
        -Wl,-rpath,"$PREFIX/lib" \
        -o app
)

readelf -dW "$WORK/app" >"$WORK/app-readelf-dynamic.txt"
nm -uC "$WORK/app" >"$WORK/app-undefined.txt" || true

if grep -qi 'libcudart' "$WORK/app-readelf-dynamic.txt"; then
    echo "G8D_CONSUMER_LIBCUDART_DEPENDENCY=FAIL"
    exit 1
fi

if ! grep -q 'NEEDED.*libcorex_remote_cudart.so.1' \
        "$WORK/app-readelf-dynamic.txt"; then
    echo "G8D_CONSUMER_REMOTE_RUNTIME_DEPENDENCY=FAIL"
    exit 1
fi

INTERNAL_PATTERN='corex_remote_cuda_g8c|corex_metadata([.]c|[.]h|[.]o)?|g7c_corex_fatbin_runtime|corex_launch_config_g8c|corex_device_info_g8c|corex_device_api_g8c|runtime_server_g8c[.]c'

# The public library name libcorex_remote_cudart.so is allowed.
if grep -Eq "$INTERNAL_PATTERN" \
        "$WORK/pkg-cflags.txt" \
        "$WORK/pkg-libs.txt" \
        "$PREFIX/share/corex-remote-cudart/install-files.txt"; then
    echo "G8D_INTERNAL_SOURCE_LEAK=FAIL"
    exit 1
fi

echo "G8D_CONSUMER_LIBCUDART_DEPENDENCY=ABSENT"
echo "G8D_CONSUMER_REMOTE_RUNTIME_DEPENDENCY=PASS"
echo "G8D_INTERNAL_SOURCE_LEAK=ABSENT"
echo "G8D_PKGCONFIG_METADATA=PASS"
echo "G8D_PKGCONFIG_MODE=$G8D_PKGCONFIG_MODE"
echo "G8D_CONSUMER_BUILD=PASS"
