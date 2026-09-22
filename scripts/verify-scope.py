#!/usr/bin/env python3
from pathlib import Path
import re
import sys

root = Path(sys.argv[1] if len(sys.argv) > 1 else ".")

client = (root / "src/corex_remote_cuda.c").read_text()
server = (root / "server/runtime_server.c").read_text()
api = (root / "src/corex_device_api.c").read_text()
protocol = (root / "include/internal/corex_protocol.h").read_text()
vmap = (root / "packaging/corex_remote_cudart.map").read_text()

checks = [
    ("client_protocol_v3",
     '#include "corex_protocol.h"' in client and
     "#define CRX_PROTOCOL_VERSION 3u" in protocol),
    ("server_protocol_v3",
     '#include "corex_protocol.h"' in server and
     "#define CRX_PROTOCOL_VERSION 3u" in protocol),
    ("client_opcode_26",
     "OP_GET_DEVICE_INFO     = 26" in protocol),
    ("server_opcode_26",
     "OP_GET_DEVICE_INFO     = 26" in protocol),
    ("canonical_protocol_owner",
     "enum {\n    ST_OK" not in client and
     "enum {\n    ST_OK" not in server and
     "CorexProtocolRequestHeader" in protocol and
     "CorexProtocolResponseHeader" in protocol),
    ("server_no_cudaDeviceProp",
     "cudaDeviceProp" not in server),
    ("server_driver_only",
     "#include <cuda.h>" in server and
     "cuda_runtime" not in server),
    ("server_session_owns_objects",
     "typedef struct {\n    Allocation allocations[MAX_ALLOCS];" in server and
     "static void cleanup_session(ServerSession *session)" in server and
     "static Allocation allocations[MAX_ALLOCS]" not in server and
     "static ModuleEntry modules[MAX_MODULES]" not in server and
     "static KernelEntry kernels[MAX_KERNELS]" not in server),
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
