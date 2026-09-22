#!/usr/bin/env python3
from pathlib import Path

p = Path("runtime_server_g8c.c")
s = p.read_text()

old_decl = """    uint32_t async_engine_count = 0;
    uint32_t unified_addressing = 0;

#define ATTR(dst, attr) \\
"""
new_decl = """    uint32_t async_engine_count = 0;
    uint32_t unified_addressing = 0;
    uint32_t compute_capability_major = 0;
    uint32_t compute_capability_minor = 0;

#define ATTR(dst, attr) \\
"""

old_tail = """    ATTR(async_engine_count,
         CU_DEVICE_ATTRIBUTE_ASYNC_ENGINE_COUNT);
    ATTR(unified_addressing,
         CU_DEVICE_ATTRIBUTE_UNIFIED_ADDRESSING);

#undef ATTR

    int major_i = 0;
    int minor_i = 0;
    r = cuDeviceComputeCapability(
        &major_i,
        &minor_i,
        g_device);

    if (r != CUDA_SUCCESS ||
        major_i < 0 ||
        minor_i < 0)
        return send_response(
            fd,
            OP_GET_DEVICE_INFO,
            req_id,
            ST_CUDA_ERROR,
            NULL,
            0);

    uint32_t device_overlap =
"""
new_tail = """    ATTR(async_engine_count,
         CU_DEVICE_ATTRIBUTE_ASYNC_ENGINE_COUNT);
    ATTR(unified_addressing,
         CU_DEVICE_ATTRIBUTE_UNIFIED_ADDRESSING);
    ATTR(compute_capability_major,
         CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR);
    ATTR(compute_capability_minor,
         CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR);

#undef ATTR

    int major_i = (int)compute_capability_major;
    int minor_i = (int)compute_capability_minor;

    uint32_t device_overlap =
"""

if old_decl not in s:
    raise SystemExit("declaration marker not found; file may already be patched")
if old_tail not in s:
    raise SystemExit("deprecated block not found; file may already be patched")

s = s.replace(old_decl, new_decl, 1)
s = s.replace(old_tail, new_tail, 1)
p.write_text(s)

print("G8C_B_V2_PATCH=PASS")
