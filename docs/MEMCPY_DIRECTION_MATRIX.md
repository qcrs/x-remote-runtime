# M3-S1 memcpy direction matrix

| Runtime kind | Supported behavior | Validation |
| --- | --- | --- |
| Host → Host | local `memmove`, including overlap | both pointers must be outside fake-VA arena |
| Host → Device | existing chunked `OP_H2D` path | destination resolves to live allocation; source cannot be fake-VA |
| Device → Host | existing chunked `OP_D2H` path | source resolves to live allocation; destination cannot be fake-VA |
| Device → Device | `OP_D2D`, server calls `cuMemcpyDtoD` | both ranges live, in bounds, non-overlapping |
| Default | maps to H2H, H2D, D2H, or D2D from fake-VA classification | ambiguous/stale ranges fail; no native pointer guessing |
| Async Host → Host | immediate local `memmove` | stream is resolved, but no remote transfer is needed |
| Async Host → Device | hidden stream-ordered `OP_H2D_ASYNC_SUBMIT` | host data becomes remote-visible at transfer retirement |
| Async Device → Host | hidden stream-ordered `OP_D2H_ASYNC_SUBMIT` | host destination is written at transfer retirement |
| Async Device → Device | explicit `cudaErrorNotSupported` | capability is not advertised or emulated |

Zero-byte copies return success before dereference. Nonzero out-of-bounds ranges
return `cudaErrorInvalidValue`; stale/non-remote pointers return the existing
invalid-device-pointer mapping. The server repeats range and overlap checks
before calling CoreX.
