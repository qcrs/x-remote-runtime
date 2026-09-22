#!/usr/bin/env python3
from pathlib import Path
import re
import sys

root = Path(sys.argv[1] if len(sys.argv) > 1 else ".")

client = (root / "corex_remote_cuda_g8c.c").read_text()
server = (root / "runtime_server_g8c.c").read_text()
api = (root / "corex_device_api_g8c.c").read_text()
vmap = (root / "corex_remote_cudart.map").read_text()

checks = [
    ("client_protocol_v3",
     "#define VERSION 3u" in client),
    ("server_protocol_v3",
     "#define VERSION 3u" in server),
    ("client_opcode_26",
     "OP_GET_DEVICE_INFO     = 26" in client),
    ("server_opcode_26",
     "OP_GET_DEVICE_INFO  = 26" in server),
    ("server_no_cudaDeviceProp",
     "cudaDeviceProp" not in server),
    ("server_driver_only",
     "#include <cuda.h>" in server and
     "cuda_runtime" not in server),
    ("client_vendor_abi_adapter",
     "#include <cuda_runtime_api.h>" in api and
     "sizeof(*prop)" in api),
    ("client_fieldwise_prop_population",
     "memset(prop, 0, sizeof(*prop));" in api and
     "memcpy(prop->name, info.name, name_bytes);" in api and
     "prop->totalGlobalMem = (size_t)info.total_global_mem;" in api and
     "prop->warpSize = (int)info.warp_size;" in api and
     "prop->multiProcessorCount = (int)info.multi_processor_count;" in api),
    ("client_no_raw_dto_memcpy_to_prop",
     re.search(r"\bmemcpy\s*\(\s*prop\s*,", api) is None),
    ("meminfo_uses_remote_dto",
     "corexRemoteGetDeviceInfoInternal(0" in api),
    ("abi_1_1",
     "COREX_REMOTE_CUDART_1.1" in vmap and
     "cudaGetDeviceProperties;" in vmap and
     "cudaMemGetInfo;" in vmap),
]

failed = False
for name, ok in checks:
    print(f"{name}={'PASS' if ok else 'FAIL'}")
    failed |= not ok

print(f"G8C_SCOPE_AUDIT={'PASS' if not failed else 'FAIL'}")
raise SystemExit(1 if failed else 0)
