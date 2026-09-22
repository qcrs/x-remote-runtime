#!/usr/bin/env python3
from pathlib import Path

p = Path("build_g8d_consumer.sh")
s = p.read_text()

old = r"""if grep -Eq 'corex_remote_cuda|corex_metadata|g7c_corex_fatbin|corex_launch_config' \
        "$WORK/pkg-cflags.txt" "$WORK/pkg-libs.txt"; then
    echo "G8D_INTERNAL_SOURCE_LEAK=FAIL"
    exit 1
fi
"""

new = r"""INTERNAL_PATTERN='corex_remote_cuda_g8c|corex_metadata([.]c|[.]h|[.]o)?|g7c_corex_fatbin_runtime|corex_launch_config_g8c|corex_device_info_g8c|corex_device_api_g8c|runtime_server_g8c[.]c'

# The public library name libcorex_remote_cudart.so is allowed.
if grep -Eq "$INTERNAL_PATTERN" \
        "$WORK/pkg-cflags.txt" \
        "$WORK/pkg-libs.txt" \
        "$PREFIX/share/corex-remote-cudart/install-files.txt"; then
    echo "G8D_INTERNAL_SOURCE_LEAK=FAIL"
    exit 1
fi
"""

if old not in s:
    raise SystemExit("old internal-leak audit not found; file may already be patched")

p.write_text(s.replace(old, new, 1))
print("G8D_V4_INTERNAL_LEAK_AUDIT_PATCH=PASS")
