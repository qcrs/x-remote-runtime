# Gate 8C-B — Remote Device Identity Design Freeze

## Protocol

Gate 8C-B introduces a real new wire semantic.

```text
MAGIC   = CRX9
VERSION = 3
```

New opcode:

```text
OP_GET_DEVICE_INFO = 26
```

Gate-8B CRX9/V2 binaries are not silently mixed with Gate-8C/V3 binaries.

## Logical-device model

V1 remains intentionally single-endpoint:

```text
cudaGetDeviceCount() = 1
cudaGetDevice()      = 0
cudaSetDevice(0)     = success
other device index   = cudaErrorInvalidDevice
```

Gate 8C does not introduce multi-GPU scheduling.

## Wire ABI

Rejected:

```text
raw cudaDeviceProp bytes
```

The server emits an endian-defined fixed-width DTO:

```text
dto_version
logical_device
name[256]

u64 total_global_mem
u64 free_mem
u64 shared_mem_per_block

u32/u64 capability fields...
```

The server never includes or constructs `cudaDeviceProp`.

## Client ABI adapter

A separate source file is compiled against the real pinned CoreX header:

```text
corex_device_api_g8c.c
→ #include <cuda_runtime_api.h>
```

It receives the decoded DTO and populates the caller's actual:

```c
struct cudaDeviceProp
```

using named fields.

The struct is zero-initialized before the validated V1 fields are filled.

## Dynamic memory information

`cudaMemGetInfo()` always performs a fresh `OP_GET_DEVICE_INFO`.

`free_mem` is not cached.

Gate 8C-A proved that free memory changes with context/runtime activity and must
be treated as dynamic state.

## Library ABI

Gate 8B froze:

```text
COREX_REMOTE_CUDART_1.0
```

Gate 8C adds:

```text
COREX_REMOTE_CUDART_1.1
    cudaGetDeviceProperties
    cudaMemGetInfo
```

SONAME remains:

```text
libcorex_remote_cudart.so.1
```

because the change is additive, not ABI-breaking.
