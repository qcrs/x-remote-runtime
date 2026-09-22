# Architecture

The current Runtime keeps the validated Gate 8D execution model.

```text
CUDA/CoreX application
        |
        v
Client Compatibility Runtime
        |
        |  libcorex_remote_cudart.so.1
        |  COREX_REMOTE_CUDART_1.0 / 1.1
        |  COREX_REMOTE_EXT_1.0
        v
CRX9 / VERSION 3
        |
        v
Remote Runtime Server
        |
        |  session-owned allocations, modules, kernels,
        |  streams, events, and transfers
        v
CoreX Driver API
        |
        v
Iluvatar MR-V100
```

The client owns CUDA-style handles and fake device virtual addresses. The
server owns the corresponding CoreX objects and allocation identities. The
wire protocol carries explicit request and response fields, including
`AllocationID`, `StreamID`, `EventID`, and hidden `TransferID` state.

Compiler-generated registration calls enter the client runtime automatically.
Fatbinary extraction supplies the loadable image; function registration
obtains kernel metadata and ABI information before a launch. Kernel arguments
use the existing `DEVICE_PTR` and `BY_VALUE` model, with metadata remaining
the ABI authority.

Stream, event, default-stream, and asynchronous-copy behavior follows the
validated causal-frontier model. D2H completion is made host-visible at the
existing Runtime boundary. Allocation, module, kernel, stream, event, and
session lifetimes remain explicit across the client/server boundary.

The public SDK boundary contains the public extension header, the versioned
Runtime DSO, the pkg-config file, ABI surface description, and the server
binary. Internal headers and implementation objects are not exported through
that boundary.
