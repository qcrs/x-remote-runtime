# CUDA API Implementation Priority

This document is the implementation queue for the APIs currently classified as
`GROUND_TRUTH_REQUIRED` in `compat/cuda-api-ledger.csv`. It is a planning and
probe order, not a support claim. An API moves to `IMPLEMENTED`,
`PARTIAL_IMPLEMENTED`, or `BACKEND_UNSUPPORTED` only after the CoreX 4.4 probe
and the relevant semantic tests pass.

The census contains 266 public Runtime declarations. The current implementation
contains 43 fully implemented public APIs plus three partial APIs (`cudaMemcpy`,
`cudaMemcpyAsync`, and `cudaFuncGetAttributes`); the remaining declarations are
deliberately not forwarded based on a CUDA signature alone.

## Priority Model

| Priority | Meaning | Default difficulty |
| --- | --- | --- |
| P0 | Already implemented; preserve with regression tests | completed |
| P1 | Common scalar/query behavior with a small, stable DTO | low to medium |
| P2 | Common memory/function behavior requiring new state or layout metadata | medium |
| P3 | Stateful object families with descriptors and lifetime rules | high |
| P4 | Rare, platform-specific, multi-device, callback, IPC, or interop behavior | very high / defer |

Difficulty is independent of API popularity. A popular API with host-pointer
ownership or stream-ordered lifetime is still hard.

## Recommended Execution Order

### Wave 0: close the existing semantic gaps

**Priority: P1/P2. Difficulty: low-medium.** These should be probed first
because applications commonly use them and they build on objects already in the
runtime.

1. Complete the `cudaMemcpy` and `cudaMemcpyAsync` direction matrix and update
   their ledger notes. Verify H2H, H2D, D2H, D2D, `Default`, zero-byte,
   overlap, bounds, explicit/default stream ordering, and async D2D behavior.
2. M3-S7 probed and implemented the device/context query subset below,
   including PCI identity through bounded-string DTOs:

   - `cudaDeviceGetAttribute`
   - `cudaGetDeviceFlags`
   - `cudaDeviceGetStreamPriorityRange`
   - `cudaDeviceGetLimit`
   - `cudaDeviceGetCacheConfig`
   - `cudaDeviceGetSharedMemConfig`
   - `cudaDeviceGetPCIBusId`
   - `cudaDeviceGetByPCIBusId`
3. Probe function configuration APIs using the existing registered-kernel token
   (the two proven setters were completed in M3-S7):

   - `cudaFuncSetAttribute`
   - `cudaFuncSetCacheConfig`

   `cudaFuncSetSharedMemConfig` remains lower priority unless CoreX exposes an
   equivalent and the device semantics are unambiguous.

**Exit evidence:** CoreX header/API probe, fixed scalar DTO tests, invalid enum
and device tests, ABI/protocol audit, numerical baseline, lifecycle, and
multithread regression.

### Wave 1: pitched and two-dimensional memory

**Priority: P2. Difficulty: medium-high. Planned milestone: M4-S1.** This is
the next practical memory family after linear memory and is useful for image,
tensor, and row-major workloads.

- `cudaMallocPitch`
- `cudaMemcpy2D`
- `cudaMemcpy2DAsync`
- `cudaMemset2D`
- `cudaMemset2DAsync`

Required design: allocation metadata must carry pitch/layout; wire payloads
must use explicit width/height/pitch/offset DTOs; native `cudaMemcpy2DParms`
or other host structs must never cross the wire.

**Exit evidence:** row width versus pitch, subregion offsets, host/device
directions, zero-size rows, out-of-bounds rejection, async ordering, stale
allocation rejection, and numerical row-by-row comparison.

### Wave 2: host memory ownership

**Priority: P2. Difficulty: high. Planned milestone: M4-S3.** These APIs are
common in high-throughput applications but cannot be implemented by forwarding a
client virtual address to the server.

- `cudaHostAlloc`
- `cudaMallocHost`
- `cudaHostRegister`
- `cudaHostUnregister`
- `cudaFreeHost`
- `cudaHostGetDevicePointer`
- `cudaHostGetFlags`

First probe whether CoreX supplies compatible pin/register/map primitives. If
not, classify the affected APIs as `BACKEND_UNSUPPORTED`; do not emulate
zero-copy or claim pinned-memory semantics.

**Exit evidence:** ownership/lifetime matrix, mapped-pointer behavior, flags,
server-kill cleanup, stale host registration, concurrent use, and explicit
unsupported behavior where CoreX lacks an equivalent.

### Wave 3: 3D pitched memory

**Priority: P2. Difficulty: high. Planned milestone: M4-S2.** Depends on the
pitch/layout model from Wave 1.

- `cudaMalloc3D`
- `cudaMemcpy3D`
- `cudaMemcpy3DAsync`

Use explicit extent, position, pitch, slice-pitch, and endpoint DTOs. Do not
serialize native `cudaExtent`, `cudaPitchedPtr`, or `cudaMemcpy3DParms` layouts.

**Exit evidence:** depth/slice pitch, mixed host/device endpoints, subvolumes,
zero extents, bounds, async ordering, and cross-generation stale-object tests.

### Wave 4: CUDA arrays

**Priority: P3. Difficulty: high. Planned milestone: M4-S4.** Arrays are a
new typed remote object family and should precede texture/surface objects.

- `cudaMallocArray`
- `cudaFreeArray`
- `cudaArrayGetInfo`
- `cudaGetChannelDesc`
- `cudaMemcpyToArray`
- `cudaMemcpyFromArray`
- `cudaMemcpy2DToArray`
- `cudaMemcpy2DFromArray`
- `cudaMemcpyArrayToArray`
- `cudaGetMipmappedArrayLevel`
- `cudaMalloc3DArray`
- `cudaFreeMipmappedArray`

The first probe should establish supported channel formats, extents, mip levels,
copy directions, and object lifetime. Unsupported formats must return a stable
error rather than silently reinterpret data.

### Wave 5: texture and surface objects

**Priority: P3. Difficulty: very high. Planned milestone: M4-S5.** Depends on
Wave 4 and compiler/kernel evidence showing actual consumption.

- `cudaCreateTextureObject`
- `cudaDestroyTextureObject`
- `cudaGetTextureObjectResourceDesc`
- `cudaGetTextureObjectTextureDesc`
- `cudaGetTextureObjectResourceViewDesc`
- `cudaCreateSurfaceObject`
- `cudaDestroySurfaceObject`
- `cudaGetSurfaceObjectResourceDesc`
- `cudaBindTexture`
- `cudaBindTexture2D`
- `cudaBindTextureToArray`
- `cudaBindSurfaceToArray`
- `cudaUnbindTexture`

Descriptor DTOs, object lifetime, array binding, and actual kernel access must
all be validated. Signature-compatible stubs are not sufficient.

### Wave 6: memory pools and stream-ordered allocation

**Priority: P3. Difficulty: very high. Planned milestone: M5-S1/M5-S2.**

Probe the pool object model first:

- `cudaMemPoolCreate`
- `cudaMemPoolDestroy`
- `cudaDeviceGetDefaultMemPool`
- `cudaDeviceGetMemPool`
- `cudaDeviceSetMemPool`
- `cudaMemPoolGetAttribute`
- `cudaMemPoolSetAttribute`
- `cudaMemPoolTrimTo`
- `cudaMemPoolGetAccess`
- `cudaMemPoolSetAccess`

Only after pool lifetime and attributes are proven:

- `cudaMallocAsync`
- `cudaFreeAsync`
- `cudaMallocFromPoolAsync`

The implementation must integrate fake VA allocation identity, stream ordering,
deferred free, stale-pointer rejection, and cross-stream synchronization.

### Wave 7: explicit CUDA Graphs

**Priority: P3/P4. Difficulty: very high. Planned milestone: M5-S3.** Start
with a deliberately small graph subset built from already supported operations:

- `cudaGraphCreate`
- `cudaGraphDestroy`
- `cudaGraphAddEmptyNode`
- `cudaGraphAddDependencies`
- `cudaGraphAddKernelNode`
- `cudaGraphAddMemcpyNode`
- `cudaGraphAddMemsetNode`
- `cudaGraphInstantiate`
- `cudaGraphExecDestroy`
- `cudaGraphLaunch`

Then consider getters/setters, update, upload, host nodes, child graphs, and
graph memory APIs. Graph state is handwritten infrastructure; it should not be
generated from C signatures.

### Wave 8: stream capture

**Priority: P3/P4. Difficulty: very high. Planned milestone: M5-S4.** This
must follow a working explicit graph implementation.

- `cudaStreamBeginCapture`
- `cudaStreamEndCapture`
- `cudaStreamIsCapturing`
- `cudaStreamGetCaptureInfo`
- `cudaStreamGetCaptureInfo_v2`
- `cudaStreamBeginCaptureToGraph`
- `cudaStreamUpdateCaptureDependencies`
- `cudaThreadExchangeStreamCaptureMode`

The probe and tests must define unsupported-call behavior, invalidation,
per-thread state, default-stream interaction, and capture lifetime.

### Wave 9: multi-device and peer APIs

**Priority: P4. Difficulty: very high. Planned milestone: M5-S5.** Do not
implement or simulate this family until the real CoreX host exposes at least two
devices and peer topology.

- `cudaDeviceCanAccessPeer`
- `cudaDeviceEnablePeerAccess`
- `cudaDeviceDisablePeerAccess`
- `cudaDeviceGetP2PAttribute`
- `cudaMemcpyPeer`
- `cudaMemcpyPeerAsync`
- `cudaMemcpy3DPeer`
- `cudaMemcpy3DPeerAsync`

If the hardware remains single-device, classify these as
`GROUND_TRUTH_BLOCKED` or `BACKEND_UNSUPPORTED` with the exact probe output.

### Wave 10: IPC and external interop

**Priority: P4. Difficulty: very high / environment-dependent.** These are
last because they depend on OS handles, process sharing, graphics APIs, or
driver-specific contracts.

- IPC: `cudaIpcGetMemHandle`, `cudaIpcOpenMemHandle`,
  `cudaIpcCloseMemHandle`, `cudaIpcGetEventHandle`, `cudaIpcOpenEventHandle`
- External memory/semaphores: `cudaImportExternalMemory`,
  `cudaDestroyExternalMemory`, `cudaExternalMemoryGetMappedBuffer`,
  `cudaImportExternalSemaphore`, `cudaDestroyExternalSemaphore`,
  `cudaWaitExternalSemaphoresAsync`, `cudaSignalExternalSemaphoresAsync`
- Graphics interop: `cudaGraphics*` APIs in the ledger

Do not start this wave without exact CoreX and OS-handle ground truth. An honest
`BACKEND_UNSUPPORTED` result is preferable to a local-only fake.

## Probe and Acceptance Template

Every wave follows the same sequence:

1. Inspect installed CoreX headers and symbols under `/usr/local/corex-4.4.0`.
2. Add a focused probe for positive behavior, invalid arguments, and resource
   lifetime.
3. Record exact CoreX return codes and relevant device/profile information.
4. Define explicit network-order scalar/DTO payloads before implementation.
5. Implement the smallest dependency-ready API slice.
6. Run build, ABI/protocol, numerical, lifecycle, negative, session-isolation,
   reconnect, and multithread gates as applicable.
7. Update the API ledger and add compact evidence under
   `evidence/<milestone>/<slice>/<timestamp>/`.

The completion status is decided by evidence, not by the existence of a client
symbol or a successful compile.

## Roadmap Tracks

The compatibility track is at M3-S7; its next dependency-ready slice is M4-S1
pitched and two-dimensional memory. M6 is a separate productization/operations
track and does not imply that M4 or M5 API families are complete.

## Current Next Step

The repository has passed M1-M3 locally. The next dependency-ready work is:

1. Finish M6-S1 endpoint/bind configuration and diagnostics.
2. If no second same-architecture host is available, record M6-S2 as
   environment-blocked and continue with Wave 1.
3. Probe Wave 0 gaps, then begin M4-S1 pitched/2D memory.
