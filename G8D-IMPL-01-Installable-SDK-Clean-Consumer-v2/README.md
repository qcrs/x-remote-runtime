# Gate 8C-B — Remote Device Discovery / Runtime Identity

## Prerequisite

```text
Gate 8C-A
Device Identity / cudaDeviceProp Ground Truth
= PASS
```

Measured pinned CoreX 4.4 ABI:

```text
sizeof(cudaDeviceProp)=712
alignof(cudaDeviceProp)=8
```

MR-V100 ground truth includes:

```text
name                    Iluvatar MR-V100
totalGlobalMem          34359738368
sharedMemPerBlock       131072
regsPerBlock            262144
warpSize                64
maxThreadsPerBlock      4096
maxThreadsDim           4096,4096,256
maxGridSize             2147483647,65535,65535
clockRate               1500000
totalConstMem           16384
compute capability      7.1
multiProcessorCount     16
memoryClockRate         1600000
memoryBusWidth          2048
l2CacheSize             16777216
maxThreadsPerMP         8192
asyncEngineCount        1
unifiedAddressing       1
```

## What Gate 8C-B adds

```text
cudaGetDeviceProperties
cudaMemGetInfo
```

through:

```text
OP_GET_DEVICE_INFO = 26
CRX9 / VERSION 3
```

No raw host struct is sent over the wire.

## Build

```bash
cd /home/lvtong/corex-feasibility

unzip G8C-B-IMPL-01-Remote-Device-Identity.zip
cd G8C-B-IMPL-01-Remote-Device-Identity

chmod +x build_g8c_b.sh run_g8c_b.sh verify_g8c_scope.py verify_g8c_abi.py

COREX=/usr/local/corex-4.4.0 \
./build_g8c_b.sh
```

Expected:

```text
G8C_ABI_SURFACE=PASS
G8C_SCOPE_AUDIT=PASS

G8C_PROTOCOL=CRX9/V3
G8C_ABI_VERSION=COREX_REMOTE_CUDART_1.1
G8C_LIBRARY_LIBCUDART_DEPENDENCY=ABSENT
G8C_APP_LIBCUDART_DEPENDENCY=ABSENT
G8C_BUILD=PASS
```

## Start the new V3 server

Stop the older `runtime_server_g6e` first.

```bash
cd /home/lvtong/corex-feasibility/G8C-B-IMPL-01-Remote-Device-Identity

stdbuf -oL ./dist/bin/runtime_server_g8c \
  | tee g8c-server.log
```

Expected startup:

```text
GPU=Iluvatar MR-V100
protocol=CRX9 version=3
SERVER_READY
```

## Run

In another terminal:

```bash
cd /home/lvtong/corex-feasibility/G8C-B-IMPL-01-Remote-Device-Identity

./run_g8c_b.sh
```

Expected final:

```text
device_identity_app_exit=0
client_mem_info_count=2

server_device_info_count=3
server_get_kernel_count=1
server_launch_generic_count=1
server_abi_validate_pass_count=1
server_session_count=1

G8C_B_RESULT=PASS
```

Why three device-info RPCs:

```text
cudaGetDeviceProperties
cudaMemGetInfo before allocations
cudaMemGetInfo after allocations
```

The invalid-device properties request is rejected locally and must not create a
fourth RPC.

Upload the generated evidence archive for closure review.
