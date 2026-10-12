# Remote Memory Layout Model

The client owns one `VirtualAllocation` registry for all remote allocations. A
record contains the fake VA span, AllocationID, physical byte size, layout kind,
logical extent, pitch, and slice pitch. `cudaMalloc` records a linear layout;
`cudaMallocPitch` records the backend pitch and the physical `pitch * height`
size in the same registry, so `cudaFree` and stale-pointer checks are shared.
`cudaMalloc3D` reuses `OP_ALLOC_PITCHED=49` with `height * depth` rows and
records `PITCHED_3D`, the original width/height/depth, `slice_pitch=pitch*height`,
and `physical_bytes=slice_pitch*depth` in that same registry. The server owns
one contiguous Allocation; native pitch allocation and its existing linear
fallback are unchanged. The returned `cudaPitchedPtr` has `xsize=width` and
`ysize=height`.

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
3D transfers use explicit slice pitches. The checked region helper normalizes
`srcPos`/`dstPos` as `z*slice_pitch + y*pitch + x`, where x is bytes, then checks
the final slice/row/byte against allocation size. Host pack/scatter uses the
same position rules. Synchronous remote directions use `OP_MEMCPY_3D=55` and
`cuMemcpy3D`; H2H is local, and Default uses existing fake-VA classification.
Native public structs never cross the wire.

All remote `cudaMemcpy3DAsync` directions use `OP_MEMCPY_3D_ASYNC=56` and the
requested backend stream (StreamID zero selects the legacy default stream).
D2D uses mode 3; H2D/D2H use submit modes 4/5 and return TransferID. Host staging
is compact `width*height*depth`, with pitch=width and height=transfer height.
The server uses the existing pinned TransferEntry and event lifetime. The client
uses HiddenTransfer, the existing frontier, and query/wait retirement; D2H
scatters into the normalized destination with its caller row/slice pitches only
at retirement. No scheduler or separate lifetime model is added.

The 92-byte sync and 100-byte Async request headers retain their existing field
order. Opcode 56's old modes 1/2 keep their empty/data response shapes and
consume pinned staging after completion; mode 3 retains its empty response.
New modes 4/5 extend the existing mode field and return an eight-byte TransferID,
avoiding an incompatible response change for legacy clients. New submit modes
are rejected by older servers. CRX9/V3, opcode assignments, and payload fields
remain unchanged.

`cudaMalloc3D`, `cudaMemcpy3D`, and `cudaMemcpy3DAsync` are `IMPLEMENTED` for
pointer-backed memory. Array endpoints remain outside this subsystem. Current
target evidence is `evidence/m4/3d-memory-20261012-092736/`; the preceding native
probe and 2D closeout evidence is `evidence/m4/memory-layout-20261012/`.
