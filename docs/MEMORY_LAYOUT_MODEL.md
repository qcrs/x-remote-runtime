# Remote Memory Layout Model

The client owns one `VirtualAllocation` registry for all remote allocations. A
record contains the fake VA span, AllocationID, physical byte size, layout kind,
logical extent, pitch, and slice pitch. `cudaMalloc` records a linear layout;
`cudaMallocPitch` records the backend pitch and the physical `pitch * height`
size in the same registry, so `cudaFree` and stale-pointer checks are shared.

2D validation checks the supplied pitch and the final row expression
`offset + (height - 1) * pitch + width` with checked arithmetic against the
physical allocation size. Linear allocations are accepted by 2D APIs when the
caller supplied pitch fits the allocation; pitched metadata provides additional
extent information but is not a second ownership system.

The CRX9 wire format uses explicit fixed-width fields. `OP_ALLOC_PITCHED`
returns AllocationID, pitch, and physical bytes. `OP_MEMCPY_2D` carries a mode,
allocation IDs and offsets, pitches, width, height, and a bounded host payload.
The server first uses `cuMemcpy2D`, setting host pitch to the packed row width.
The existing row loop is a fallback after native errors, so one public 2D call
creates one client RPC. Caller host pitch is used only for client pack/scatter.
`OP_MEMSET_2D` uses the backend D2D8 primitive and changes only logical row
bytes, leaving pitch padding untouched.

`OP_D2D_ASYNC` maps `cudaMemcpyAsync` device-to-device operations to
`cuMemcpyDtoDAsync` and the existing stream frontier. 2D async entry points
reuse the same stream validation and frontier rather than creating a second
scheduler.
Async D2D uses `OP_MEMCPY_2D_ASYNC=51`; H2D/D2H use submit opcodes 57/58 and
the existing pinned staging, TransferID, and retirement paths, including the
legacy default stream. Retirement query/wait RPCs are separate from submission.

`cudaExtent`, `cudaPitchedPtr`, and `cudaMemcpy3DParms` are public ABI types,
but are normalized at the API boundary. Array-backed 3D transfers remain
unsupported because this subsystem owns only pointer memory. Pointer-backed
3D transfers use explicit slice pitches and CoreX `cuMemcpy3D`; the client
resolver validates the final slice, row, and byte boundary before the single
`OP_MEMCPY_3D=55` RPC. The public struct layout matches CoreX 4.4, but nonzero
`srcPos`/`dstPos` are explicitly unsupported; byte offsets in the pointers work.
`cudaMemcpy3DAsync` currently shares this synchronous RPC and ignores the
backend stream. `OP_MEMCPY_3D_ASYNC=56` exists only as a server handler; its
host staging lifetime/retirement is not implemented for public Async use.
Both 3D APIs are `PARTIAL_IMPLEMENTED`, despite native sync/Async Driver probes
passing all three directions. Target evidence is recorded in
`evidence/m4/memory-layout-20261012/`.
