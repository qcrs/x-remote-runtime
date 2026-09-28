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
The server performs the row loop, so one public 2D call creates one client RPC.
`OP_MEMSET_2D` uses the backend D2D8 primitive and changes only logical row
bytes, leaving pitch padding untouched.

`OP_D2D_ASYNC` maps `cudaMemcpyAsync` device-to-device operations to
`cuMemcpyDtoDAsync` and the existing stream frontier. 2D async entry points
reuse the same stream validation and frontier rather than creating a second
scheduler.

`cudaExtent`, `cudaPitchedPtr`, and `cudaMemcpy3DParms` are public ABI types,
but are normalized at the API boundary. Array-backed 3D transfers remain
unsupported because this subsystem owns only pointer memory. Pointer-backed
3D transfers use explicit slice pitches and CoreX `cuMemcpy3D`/`Async`; the
client resolver validates the final slice, row, and byte boundary before the
single RPC.
