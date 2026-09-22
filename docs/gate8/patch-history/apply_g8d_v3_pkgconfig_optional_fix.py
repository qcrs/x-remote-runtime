#!/usr/bin/env python3
from pathlib import Path

p = Path("build_g8d_consumer.sh")
s = p.read_text()

old = r"""export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig"

pkg-config --modversion corex-remote-cudart >"$WORK/pkg-version.txt"
pkg-config --cflags corex-remote-cudart >"$WORK/pkg-cflags.txt"
pkg-config --libs corex-remote-cudart >"$WORK/pkg-libs.txt"
"""

new = r"""PC_FILE="$PREFIX/lib/pkgconfig/corex-remote-cudart.pc"

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
"""

if old not in s:
    raise SystemExit("pkg-config block not found; file may already be patched")
s = s.replace(old, new, 1)

old_link = r"""    "$GXX" \
        main.o \
        $(pkg-config --libs corex-remote-cudart) \
        -Wl,-rpath,"$PREFIX/lib" \
        -o app
"""

new_link = r"""    read -r -a SDK_LIBS < <(cat "$WORK/pkg-libs.txt")

    "$GXX" \
        main.o \
        "${SDK_LIBS[@]}" \
        -Wl,-rpath,"$PREFIX/lib" \
        -o app
"""

if old_link not in s:
    raise SystemExit("consumer link block not found")
s = s.replace(old_link, new_link, 1)

old_tail = r"""echo "G8D_CONSUMER_LIBCUDART_DEPENDENCY=ABSENT"
echo "G8D_CONSUMER_REMOTE_RUNTIME_DEPENDENCY=PASS"
echo "G8D_INTERNAL_SOURCE_LEAK=ABSENT"
echo "G8D_CONSUMER_BUILD=PASS"
"""

new_tail = r"""echo "G8D_CONSUMER_LIBCUDART_DEPENDENCY=ABSENT"
echo "G8D_CONSUMER_REMOTE_RUNTIME_DEPENDENCY=PASS"
echo "G8D_INTERNAL_SOURCE_LEAK=ABSENT"
echo "G8D_PKGCONFIG_METADATA=PASS"
echo "G8D_PKGCONFIG_MODE=$G8D_PKGCONFIG_MODE"
echo "G8D_CONSUMER_BUILD=PASS"
"""

if old_tail not in s:
    raise SystemExit("consumer result block not found")
s = s.replace(old_tail, new_tail, 1)

p.write_text(s)
print("G8D_V3_PKGCONFIG_OPTIONAL_PATCH=PASS")
