#!/usr/bin/env python3
from pathlib import Path

p = Path("corex_device_api_g8c.c")
s = p.read_text()

old = """    strncpy(prop->name, info.name, sizeof(prop->name) - 1);
    prop->name[sizeof(prop->name) - 1] = '\\0';
"""

new = """    size_t name_bytes = sizeof(info.name);
    if (name_bytes >= sizeof(prop->name))
        name_bytes = sizeof(prop->name) - 1;

    memcpy(prop->name, info.name, name_bytes);
    prop->name[name_bytes] = '\\0';
"""

if old not in s:
    raise SystemExit("strncpy block not found; file may already be patched")

p.write_text(s.replace(old, new, 1))
print("G8C_B_V3_PATCH=PASS")
