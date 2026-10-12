# Project State

Current compatibility position: **47-API capability expansion measured and closed**,
including partial client Host Memory staging and generated context control RPCs
(CoreX 4.4 / MR-V100).

| Item | Current value |
| --- | --- |
| Hardware | Iluvatar MR-V100 / 智铠100 |
| CoreX | 4.4.0 |
| Architecture | aarch64 |
| Protocol | CRX9 / VERSION 3 |
| Public library | `libcorex_remote_cudart.so.1` |
| CUDA ABI | `COREX_REMOTE_CUDART_1.0`, `COREX_REMOTE_CUDART_1.1` |
| Extension ABI | `COREX_REMOTE_EXT_1.0` |
| Development repository | `/home/lvtong/corex-remote-runtime` |
| Roadmap baseline | `159e7f95057a7ff4b2b3d1ef24d579a2ab2b8d2c` |
| Last validated slice | 47-API compatibility expansion, 2026-10-12; public-ABI localhost CRX9 integration |
| Previous PASS baseline | `536b608` (pointer-backed 3D completion) |
| Current Slice | Host Memory, Array/Texture/Surface ground truth, context setters, stream attributes, pointer/value queries |
| Scope boundary | Graph/Capture, Pools, IPC, Peer/Multi-device and External Interop excluded |
| Next action | Batch closed; further compatibility work requires another selected family |

Compatibility and operations work are tracked independently. The compatibility
track has completed M3-S8 and has implemented the bounded Memory Layout slice;
its validation and remaining limitations are recorded below. The subsequent
47-API expansion is recorded in the final section and the authoritative ledger. M6-S1 network configuration
and diagnostics remain an independent productization track, not evidence that
M4/M5 API expansion is complete.

The repository migration preserves the validated Runtime behavior. Gate 8
history, patch scripts, and generated validation outputs are retained under
`docs/gate8/` and `evidence/gate8d/` for reference; they are not development
entrypoints.

M1-S1 is accepted and frozen. Its coarse, non-recursive RuntimeContext mutex
remains the correctness boundary. M1-S2 passed build, ABI/protocol audit,
baseline numerical, 8x100 multithread, lifecycle/stale-object, negative, and
local dual-process gates. Evidence: `evidence/m1/s2/20260922-133708/`.

M1-S3 moved all CRX9/V3 common headers, status/opcode definitions, stable
object-ID scalar types, endian helpers, and bounded decoders into
`include/internal/corex_protocol.h`. Golden byte fixtures and all M1 regression
gates passed without a version, opcode, payload, or public ABI change. Evidence:
`evidence/m1/s3/20260922-135005/`.

M1-S4 moved allocation, stream, event, transfer, module, and kernel tables plus
all next-ID counters into an explicit per-connection `ServerSession`. Cleanup
takes that session, retires live resources, and restores an empty structure.
Sequential-connection isolation and all common regressions passed. Evidence:
`evidence/m1/s4/20260922-140429/`.

M1-S5 promotes send, receive, and protocol-header failures to an explicit
failed-session state. The failed generation is invalidated without replay;
lazy reconnect creates a clean generation. Idle/live-object server kills and a
malformed peer were exercised, followed by fresh numerical work. M1 now passes
G0-G6 and G8. Evidence: `evidence/m1/s5/20260922-141618/`.

M2-S1 produced a deterministic census of 266 declarations in the installed
CoreX 4.4 `cuda_runtime_api.h` plus nine project/compiler ABI entries. All 33
existing public exports map to ledger rows. Human-reviewed annotations survive
regeneration, and unknown APIs retain `GROUND_TRUTH_REQUIRED`. Evidence:
`evidence/m2/s1/20260922-235848/`.

M2-S2 adds optional V3 `OP_HELLO=27` with network-order version/backend/device
metadata and six stable capability IDs. New client/server negotiation, legacy
V3 fallback, malformed-payload rejection, ABI/protocol audit, numerical,
lifecycle, failure/reconnect, session-isolation, and 8×100 multithread gates
passed. The wire contract is in `docs/HELLO_CAPABILITIES.md`.
Evidence: `evidence/m2/s2/20260923-001341/`.

M2-S3 replaces the server's growing central opcode switch with one static
opcode-to-handler/capability registry covering all 1–27 opcodes. A compile-time
duplicate-case guard and startup completeness check reject bad registration.
Unknown opcodes return `ST_BAD_REQUEST` without closing the connection.
Existing wire headers/payloads and handler implementations are unchanged;
HELLO/unknown-opcode, ABI/protocol, numerical, lifecycle, session-isolation,
reconnect, and 8×100 multithread gates passed. Evidence:
`evidence/m2/s3/20260923-004025/`.

M2-S4 extracts the proven CoreX Driver boundary into the statically linked
`server/corex_backend.{h,c}` seam. It covers device/context, memory and host
staging, synchronous/asynchronous copies, streams/events, module/function,
and launch primitives; `runtime_server.c` has zero direct `cu*` calls. Build,
ABI/protocol, numerical, lifecycle, session-isolation, reconnect, and 8×100
multithread gates passed. Evidence: `evidence/m2/s4/20260923-011245/`.

M2-S5 adds the human-reviewed JSON schema at `schema/corex_api_schema.json`
for the initial simple query/control API subset. The deterministic generator emits checked
in metadata, bounded wire-payload adapters, handler/capability metadata, and a
test skeleton. Unsupported schema values fail generation; two regenerations
are byte-identical. Generated sync/stream-query/event-query positive and
negative parity tests passed. Evidence: `evidence/m2/s5/20260923-013000/`.

M3-S1 completes synchronous memcpy direction coverage. H2H uses local
`memmove`; H2D and D2H retain the proven chunked paths; D2D uses a real
`cuMemcpyDtoD` backend opcode; `cudaMemcpyDefault` is enabled only when fake-VA
classification is unambiguous. Explicit remote-VA validation prevents stale or
remote pointers from being dereferenced as host memory. Bounds, overlap,
zero-byte, unsupported async-D2D, numerical, and 8×100 multithread tests
passed. Evidence: `evidence/m3/s1/20260923-020000/`.

M3-S2 implements `cudaMemset` and `cudaMemsetAsync` through real CoreX
`cuMemsetD8`/`cuMemsetD8Async` primitives using opcodes 29–30. Both paths use
fake-VA bounds validation; the async path resolves explicit/default streams and
participates in the client ordering frontier. Nonzero values, offsets,
zero-byte, explicit-stream ordering, stale/invalid and out-of-bounds cases,
ABI/protocol, numerical, and 8×100 multithread regressions passed. Evidence:
`evidence/m3/s2/20260923-024500/`.

M3-S3 probes CoreX stream nonblocking flags, priorities, event timing, and
disable-timing behavior, then exposes the verified Runtime extensions:
`cudaStreamCreateWithFlags`, `cudaStreamCreateWithPriority`, stream flag/
priority queries, `cudaEventCreateWithFlags`, and `cudaEventElapsedTime`.
Nonblocking streams bypass legacy-default frontier merging; event timing rejects
disable-timing events. Probe, extension, ABI/protocol, numerical, lifecycle,
isolation, reconnect, and 8×100 multithread gates passed. Evidence:
`evidence/m3/s3/20260924-030000/`.

M3-S4 adds scalar `cudaDriverGetVersion` and `cudaRuntimeGetVersion` queries
through opcodes 35–36. Driver version is read from CoreX
`cuDriverGetVersion`; runtime version is explicitly the compatibility ABI
1.1.0 (11000). No native device struct crosses the wire. Version integration,
ABI/protocol and numerical baseline gates passed. Evidence:
`evidence/m3/s4/20260924-034500/`.

M3-S5 adds function attributes and occupancy for already-registered remote
kernel identities through opcodes 37–38. The server resolves session-owned
kernel IDs and calls CoreX `cuFuncGetAttribute` for seven scalar attributes and
`cuOccupancyMaxActiveBlocksPerMultiprocessor`; no host function pointer crosses
the wire. Attributes use a fixed 28-byte network-order DTO and reconstruct the
ABI-sensitive `cudaFuncAttributes` fields locally. Positive registered-kernel,
null-argument, invalid-block-size, numerical, ABI/protocol, lifecycle,
session-isolation, reconnect, and multithread gates passed. Historical M3-S5
evidence path was not preserved in the repository; the implementation and test
claims remain represented by the committed source and later regression evidence.

M3-S6 upgrades the initial reviewed schema subset to Codegen V2 for fixed-width scalar,
object-ID, and bounded DTO APIs. Generated metadata, size-bound validation,
network-order codecs, server registry entries, typed client calls, ABI export
coverage, and negative schema tests are reproducible. The schema now covers
opcodes 4, 14, 19, 35, 36, and 38–48 while preserving the explicit legacy
protocol assignments, including handwritten opcode 37.
`scripts/verify-api-contracts.py` checks schema, ledger, protocol, and packaging
consistency.

M3-S7 adds CoreX-proven device/context queries and registered-kernel setters:
`cudaDeviceGetAttribute`, `cudaGetDeviceFlags`,
`cudaDeviceGetStreamPriorityRange`, `cudaDeviceGetLimit`,
`cudaDeviceGetCacheConfig`, `cudaDeviceGetSharedMemConfig`,
`cudaFuncSetAttribute`, and `cudaFuncSetCacheConfig`. It also adds
`cudaDeviceGetPCIBusId` and `cudaDeviceGetByPCIBusId` using generated
bounded-string codecs. CoreX 4.4 probe output is preserved under
`evidence/m3/s7/`; supported and native unsupported limits are tested without
guessing. `cudaFuncGetAttributes` is classified `PARTIAL_IMPLEMENTED` because
three Runtime fields remain local defaults without a proven CoreX query.

M3-S8 hardens the compatibility contract. Synchronous `cudaMemcpy` is now
classified `IMPLEMENTED` for H2H, H2D, D2H, D2D, and unambiguous Default;
`cudaMemcpyAsync` remains partial because D2D is explicitly unsupported, and
`cudaFuncGetAttributes` remains partial because three fields are local defaults.
Schema opcode symbols, protocol values, ledger entries, exports, coverage
statistics, and documented evidence links are checked from their authoritative
sources. New evidence follows `docs/EVIDENCE_POLICY.md` and is retained under
`evidence/m3/s8/`.
## Memory layout closeout (2026-10-12)

The runtime now has a unified linear/pitched allocation record, pitched
allocation (CRX9 opcode 49), one-RPC 2D copy and 2D memset operations (opcodes
50-53), and pointer-backed 3D transfer handlers (opcodes 55-56). CoreX
backend calls remain isolated in `server/corex_backend.c`. At that slice,
array-backed 3D transfers were excluded; the subsequent expansion measured
array allocation as unsupported by CoreX.

Direct Driver probes on CoreX 4.4 / MR-V100 passed H2D, D2H, and D2D for both
`cuMemcpy2D` and `cuMemcpy3D`: every call returned `CUDA_SUCCESS` (0) and data
and padding matched. Their Async variants also passed with pinned host staging.
The existing `cuMemAllocPitch(..., element_size=1)` call returned
`CUDA_ERROR_INVALID_VALUE` (1); linear allocation with `pitch=width` remains.
This result describes that invocation, not a blanket lack of backend support.

The historical `FAIL_D2H_2D_RUNTIME` was a Runtime bug: the client checked the
D2H response against an H2D-only length of zero, and native server copies used
caller host pitches for packed staging. Both are fixed. The existing server
row fallback is retained; no new fallback or client per-row RPC was added.
2D Async also now returns the correct opcode for native D2D and accepts default
stream host submissions, reusing the existing frontier and transfer retirement.
`cudaMemcpy2D` and `cudaMemcpy2DAsync` are `IMPLEMENTED` for this pointer slice.

The internal `cudaMemcpy3DParms` layout matches the installed public header,
and the build checks both layouts. The subsequent pointer-memory completion
normalizes nonzero positions in a shared checked 3D region helper, implements
`cudaMalloc3D` through opcode 49 and the unified allocation registry, and routes
all remote `cudaMemcpy3DAsync` directions through opcode 56 and the requested
backend stream. H2D/D2H use compact pinned staging, TransferID/event lifetime,
and existing HiddenTransfer/frontier/query/wait retirement; D2H host data stays
unchanged until retirement and scatters with destination row/slice pitches.

`cudaMalloc3D`, `cudaMemcpy3D`, and `cudaMemcpy3DAsync` are `IMPLEMENTED` for
the pointer-backed slice. Sync H2H/H2D/D2H/D2D and Default allocation roundtrip
pass, including nonzero positions and padding. The targeted Async chain passes
explicit H2D/D2D followed by default-stream D2H, including source reuse,
cross-stream frontier, and deferred host visibility. Old opcode 56 mode 1/2
response shapes are preserved; new submit modes 4/5 use the existing DTO.
Protocol query/wait retirement and invalid pitch checks pass. Array endpoints
remain outside this subsystem.

Minimal validation passed: build, API contracts, protocol/ABI checks, direct
CoreX probe, and one local two-process integration over localhost socket →
CRX9 → server → CoreX. Evidence: `evidence/m4/memory-layout-20261012/`.
The 3D completion passed build, API contracts, protocol/ABI, targeted public API
and wire compatibility integration, and evidence-link checks. Historical
regression suites were not rerun. Evidence: `evidence/m4/3d-memory-20261012-092736/`.

## Capability expansion closeout (2026-10-12)

Starting from clean tracked HEAD `536b608` (only user-supplied `AGENTS.md`
untracked), this batch processed **47 additional public APIs**: **4 IMPLEMENTED,
11 PARTIAL_IMPLEMENTED, 5 BACKEND_UNSUPPORTED, 27 GROUND_TRUTH_BLOCKED**.
All 47 previously required ground truth; none remains GROUND_TRUTH_REQUIRED.
The selected API names and measured prerequisite relationships are retained in
`evidence/m4/compat-expansion-20261012/SELECTED_APIS.csv`; the compatibility
contract remains `compat/cuda-api-ledger.csv`.

Host Memory supports default/portable client page-locked staging through
`cudaMallocHost`, `cudaHostAlloc`, registration, flag queries, and release.
The RuntimeContext owns the local host registry. Free/unregister retires pending
hidden transfers before releasing storage. OS lock failure returns an allocation
error. This is partial support: client pages are not registered for GPU DMA;
registration requires a page-aligned base; mapped, write-combined, IO and
read-only modes are rejected. Native CoreX host mapping works, but CRX9's
independent address spaces provide no shared GPU mapping. `cudaHostGetDevicePointer`
therefore remains blocked and returns NotSupported for tracked pages.

CoreX Driver valid array, surface-capable array, 3D array and mipmap allocation
return 801; native Runtime returns 31 (NotYetImplemented). Linear texture-object
creation returns 801 in both. Dependent array queries, frees, transfers and
texture/surface queries/destruction are blocked by unavailable valid handles;
no invalid-handle call is presented as a successful capability measurement.
Managed allocation likewise returns Driver 801 / Runtime 31; its four dependent
advice/prefetch/range APIs are blocked. These results classify the batch without
inventing software arrays or managed fake pointers.

Three context setters use schema/codegen codecs, typed client calls and registry
entries over new opcodes 59–61. All cache/shared-bank enums and supported limits
were measured; context setter/readback and unsupported-limit behavior pass through
CRX9. Opcode 62 copies attributes between real session-owned stream IDs. Opcodes
63–64 get/set the bounded attribute DTO, translating pointers through the existing
allocation registry. Empty access-policy window get/set/copy readback succeeds;
nonempty 32-byte normal and 4096-byte persisting windows return InvalidValue.
Synchronization-policy get/set returns 801. All three attribute APIs remain
partial; non-default propagation cannot be observed on this backend.

`cudaPointerGetAttributes` reconstructs the installed public ABI from live
remote fake-VA and local staging records, with explicit mapped/managed limitations.
The channel-description constructor returns the caller's fields locally;
the native CoreX helper produced unexpected fields in the installed header ABI,
so array probes used an independently constructed valid descriptor. Error names
cover Runtime-generated errors, including host registration errors; the wider
CUDA error enum remains partial. No native struct or host pointer crosses the wire.

Validation passed: build, public exports/ABI, protocol golden fixtures, API
contracts, deterministic generation and schema rejection checks, backend seam,
registry duplicate rejection, two grouped CoreX Driver/native Runtime probes,
and independent localhost public-library client → CRX9 → server → CoreX tests.
The targeted test covers host copies and async release, pointer/ABI layout,
context readback, stale stream handles, and malformed/bounded network DTOs.
Historical multithread/reconnect/lifecycle suites were not rerun: their shared
mechanisms were reused without changes. Six malformed pre-existing ledger CSV
rows were repaired and the API-contract check now rejects malformed rows.
Evidence: `evidence/m4/compat-expansion-20261012/`.

## Continued compatibility expansion (2026-10-12, active)

The active goal uses `757e9ab` as its additional-API baseline. The first
checkpoint adds **five supported APIs**: `cudaMemset3D`, `cudaMemset3DAsync`,
`cudaFuncSetSharedMemConfig`, and `cudaOccupancyMaxPotentialBlockSize` are
IMPLEMENTED; `cudaOccupancyMaxActiveBlocksPerMultiprocessorWithFlags` is
PARTIAL_IMPLEMENTED. Its default flag succeeds; the disable-caching-override
flag returns NotSupported as measured on CoreX. Unsupported results do not
count toward the 25–40 additional supported API target.

Generated scalar/object-ID codecs and registry entries use new opcodes 65–69.
3D fill ownership and bounds remain handwritten, reusing the live allocation
registry and checked 3D region helper. One RPC submits native per-slice fills;
Async uses the requested backend stream and existing frontier. Byte values
truncate to the low eight bits, padding stays untouched, and pitch is ignored
for single-row, single-slice extents. No host staging or alternate allocation
mechanism was introduced.

The grouped Driver probe measures fill data/padding, all function shared-bank
settings, and occupancy queries. Independent public-library client → localhost
socket → CRX9/V3 → server → CoreX validation passes sync and async fill,
same-stream deferred D2H retirement, default-stream fill, interior addresses,
invalid extents, stale streams/kernels, occupancy parity, and bounded/malformed
wire requests. Build, public exports, protocol golden fixtures, generated-code
determinism/rejection checks, backend seam and handler uniqueness pass.
Historical regression suites were not rerun because shared lifetime/transport
mechanisms were reused unchanged.

A second grouped execution probe measured cooperative launch as unsupported
(Driver 801, capability attribute 0), and persisting-cache reset as blocked
(Driver and native Runtime InvalidValue; no public Driver declaration).
Native library file loading, kernel lookup and kernel-to-function conversion
succeed and identify the next practical family, using the existing module and
kernel registry infrastructure. Library Runtime expansion remains pending;
the implementation target and final completion audit are **not yet achieved**.
Evidence for this checkpoint: `evidence/m4/continued-expansion-20261012/`.
