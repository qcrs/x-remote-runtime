# Project State

The Native Graph and practical Stream Capture goal is complete on CoreX 4.4 /
MR-V100, within the measured limits in the ledger: 40 additional public APIs
(22 IMPLEMENTED, 18 PARTIAL_IMPLEMENTED). The architecture and limits are in
[GRAPH_CAPTURE_MODEL](GRAPH_CAPTURE_MODEL.md); the requirements audit is in
`evidence/m5/graphs-capture-20261013/GOAL_AUDIT.md`. The preceding core Runtime
and Memory Pool goal is complete and was revalidated at `6d30df8`; its full
scope audit and current public transport checks are in
`evidence/m5/memory-pools-20261012/revalidation-20261013/`.

| Item | Current value |
| --- | --- |
| Hardware / CoreX | MR-V100 / CoreX 4.4.0, aarch64 |
| Protocol / ABI | CRX9/V3 / COREX_REMOTE_CUDART_1.0 and 1.1 |
| Goal baseline | `154dde1` |
| Last validated slice | Native Graph / bounded Stream Capture; 2026-10-13 |
| Scope boundary | IPC, Peer/Multi-device and External Interop excluded |
| Graph/Capture compatibility | 40-API target reached with native instantiate/launch and practical capture; broader node/capture families remain uninvestigated |

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
succeeded and identified the next family for the subsequent checkpoint below.
The implementation target and final completion audit were not achieved at
this first checkpoint.
Evidence for this checkpoint: `evidence/m4/continued-expansion-20261012/`.

### Library, identity and legacy-control checkpoint (2026-10-12)

This checkpoint adds **15 supported APIs**: five IMPLEMENTED deprecated control
aliases and ten PARTIAL_IMPLEMENTED APIs covering six library operations,
`cudaGetKernel`, `cudaGetFuncBySymbol`, `cudaChooseDevice`, and
`cudaSetValidDevices`. Together with `83cfba2`, this is **20 additional supported
APIs** relative to goal baseline `757e9ab`. The implementation target remains
active. Deprecated control names now carry support status; their deprecation
remains documented in the ledger notes.

CoreX native library load from file/data, count, enumeration, named kernel
lookup and kernel-to-function conversion succeed for a two-kernel image.
Native `cuLibraryUnload` does not return for that image: repeated live process
observations show entry into unload without return; deliberate SIGTERM ends the
probe. The installed library destructor contains a non-advancing branch
consistent with this observation. Native one-kernel unload previously succeeded.
The partial Runtime library slice therefore uses the **existing CUmodule
ownership and lifetime**, preserving two-kernel operation instead of limiting
inputs to one kernel. Count/enumeration come from the existing CoreX metadata
census and match the native two-kernel query. No opaque native handles cross
CRX9. Compiled CoreX ELF with metadata, one server context/device, no nonempty
JIT/library options and no PTX/fatbin inputs are the explicit limits.

Opcode 70 reuses the upload parser, allocation of module slots and unload
barrier. Generated opcodes 71–72 supply a count and bounded kernel name. Library
kernel tokens use the existing kernel registry and compiler ABI normalizer;
the retained client image is released on unload, shutdown and transport failure.
Identity queries return Runtime-facing tokens for compiler, controlled and
library kernels; they do not expose native Driver handles or context-independent
kernels. Missing library names return measured Runtime SymbolNotFound (500),
using appended protocol status 9; previous status/opcode values remain intact.

The one configured device is selected for all property preferences, including
oversize preferences, matching native CoreX behavior. Valid-device lists are
empty/default or [0]; other ordinals and oversized lists return the measured
errors. The five deprecated thread control APIs delegate to the existing
modern generated RPCs for limits, cache configuration and synchronization.
CoreX's obsolete query/setter Runtime entrypoints return 31; Driver-backed
modern controls provide the documented alias behavior. Kernel attribute setting
through native CUlibrary returns 801 for both mutable attributes and is recorded
BACKEND_UNSUPPORTED. The two obsolete double converters are GROUND_TRUTH_BLOCKED:
native Runtime returns 31, and a native FP64 kernel produces an unexpected value
for 1+2^-40. No unproven double conversion/no-op semantics were introduced.

Validation passes build, public exports, protocol fixtures, deterministic
schema generation/rejection, handler uniqueness and backend seam. Independent
public-library clients → localhost → CRX9 → server → CoreX validate two-kernel
library load from file/data, count/enumeration truncation and reuse, mixed
pointer/scalar argument launches, unload ordering, stale tokens, missing names,
compiler host-symbol identity, deprecated control readback and async retirement.
The existing lifecycle suite was rerun because module cleanup gained client
image ownership; it passes. EOF cleanup and normal exit with a live library
leave empty server session tables. Historical multithread/reconnect suites
were not rerun; no scheduling/reconnect policy changed.

A prerequisite probe for the next symbol family successfully resolves and
reads an initialized module global through native CoreX. Public Runtime symbol
registration and non-owning fake-VA lifetime work remain pending; the final
25–40 API target and completion audit are **not yet achieved**.
Evidence: `evidence/m4/library-expansion-20261012/`.

### Registered symbols and goal completion (2026-10-12)

Six public symbol APIs reach PARTIAL_IMPLEMENTED: address/size queries and
synchronous/asynchronous copies to/from a symbol. Together with the preceding
two checkpoints, **26 public APIs** newly reach IMPLEMENTED (9) or
PARTIAL_IMPLEMENTED (17) relative to `757e9ab`. The private `__cudaRegisterVar`
hook supports this family and is excluded from the count. All actively
investigated APIs have a terminal support classification; backend-unsupported
and ground-truth-blocked entries do not count toward the target.

CoreX resolves initialized global and constant storage through
`cuModuleGetGlobal`; grouped Driver probes verify offset writes, H2D/D2H/D2D
and stream ordering. Compiler variable registration resolves the existing
remote module through generated opcode 73 and represents its native storage
as a non-owning record in the existing allocation and fake-VA tables. Symbol
copies use existing copy RPCs, TransferIDs, staging and stream/event frontiers.
Native pointers never cross the protocol. `cudaFree` rejects module storage;
module unload drains its pending host transfers and invalidates its aliases.
Session cleanup lets the module own the native release.

The supported slice is compiler-registered, non-managed global/constant
variables in compiled CoreX modules with kernel metadata, one server context
and device, and device names at most 127 bytes. Public name-string symbol
lookup and managed-variable registration are excluded. Async behavior retains
the existing staged host-buffer semantics. Returned addresses work with normal
Runtime copies and pointer attribute queries, while remaining scoped to their
module and session generation.

Independent public shared-library clients → localhost socket → CRX9/V3 →
server → CoreX validate all six APIs, constant offsets, Default direction,
H2D/D2H/D2D, kernel reads, requested/default streams, event dependencies,
deferred D2H host visibility, unload with a pending D2H, stale addresses,
invalid bounds/directions/symbols/streams, and normal exit with live module
storage. Wire tests validate bounded DTOs, stable IDs, missing symbols,
module-owned free rejection, stale alias rejection and EOF cleanup. Build,
exports, ABI, protocol fixtures, deterministic generation/rejection, backend
seam and registry uniqueness pass. The existing lifecycle suite passes after
the shared allocation/module lifetime changes. Historical multithread and
failure/reconnect suites were not rerun; their shared policies were unchanged.

The final audit checks the actual ledger delta, the prior committed evidence
at its corresponding checkpoint, the current evidence, codegen reuse,
handwritten ownership/ordering, and excluded families. The 25–40 API target is
reached without expanding Graph/Capture, IPC, peer/multidevice or external
interop. Detailed limitations remain in the authoritative ledger; the README
keeps only the major capability summary. Evidence and final goal audit:
`evidence/m4/symbol-expansion-20261012/`.

## Remaining practical core checkpoint (2026-10-12)

Seven further public APIs are supported: reset and its deprecated thread-exit
alias, context flags, legacy configuration setup, basic extended launch, host
functions and stream callbacks. The existing async D2D implementation now has
an accurate IMPLEMENTED ledger classification, verified with host copies and
same-stream ordering. Basic extended launch accepts no attributes or Ignore
entries and reuses registered kernels and existing launch RPCs. Other launch
attributes remain outside this partial slice.

Host functions run asynchronously on a client worker, after the requested
remote stream completes and its existing host-transfer frontier retires. User
code executes outside the Runtime mutex. Subsequent Runtime entrypoints wait
for callback completion at the existing process-wide correctness boundary;
this preserves ordering but serializes unrelated streams. Callback functions
and userdata remain entirely client-side. The callback variant reports errors;
host functions are skipped on completion errors, matching the CUDA contract.

Flags are generated RPC opcode 74, applied through context creation before
session native resources exist. Equal flags succeed with live resources;
changing them returns NotSupported because the installed explicit-context
Driver has no cuCtxSetFlags. Reset drains pending host transfers, closes and
cleans the session and tombstones all remote identities. Each session teardown
now recreates the native context, clearing context controls for the next client.
Client page-locked staging remains process-owned. Old opcodes/statuses remain
unchanged; status 10 explicitly represents NotSupported.

Pageable-memory access is reported as supported natively, but stream memory
attachment cannot attach client pages to the independent server context;
managed allocations are already unsupported. At this checkpoint, Driver entry
queries were blocked because native pointers cannot serve remote clients; the
bounded client adapters described in the closeout below supersede that status.
Opaque export tables still lack a public remote ABI and remain blocked.
IPC/multidevice/interop APIs remain excluded even when census families label
some of them Device/Event/Memcpy. Graph user objects remain excluded.

Validation: build/ABI/protocol, deterministic generation/rejection, backend
seam and handler uniqueness, grouped Driver/native Runtime probes, independent
public-library localhost integration, lifecycle and session-isolation checks.
The latter two are justified by context teardown and callback ownership changes.
The 8×100 multithread suite also passes after the mutex condition gate changed;
only its result lines and full-output hash are retained.
The grouped pool prerequisite probe succeeds for native default/current/custom
pools, allocation, asynchronous free (including a normal linear allocation),
attributes, trim and access. Pool support is not yet claimed remotely.
Evidence: `evidence/m4/remaining-core-20261012/`.

## Native Memory Pools and stream-ordered allocation closeout (2026-10-12)

The practical family now supports **13 public APIs**: default/current device
pool queries and selection, pool creation/destruction/trim, attributes and
owning-device access, and cudaMallocAsync/cudaMallocFromPoolAsync/cudaFreeAsync.
Seven are IMPLEMENTED and six PARTIAL_IMPLEMENTED. Pool import/export and
pointer sharing are IPC, explicitly outside this goal; those four entries are
marked OUT_OF_SCOPE_CURRENT without a capability claim. cudaMallocFromPoolAsync
is now correctly grouped with this family in the ledger/census.

CoreX reports MEMORY_POOLS_SUPPORTED=1 and both Driver and native Runtime
allocation primitives succeed. Generated opcodes 75–87 provide bounded scalar,
PoolID, AllocationID and StreamID codecs, typed calls and handler registration.
HELLO advertises capability 8 only when the native device supports pools. The
client rejects pool operations on legacy/non-advertising peers. Pool property
and access-descriptor ABI layouts are checked against the installed header;
no native pointer or struct crosses CRX9.

Pool tokens use RuntimeContext generation ownership and monotonic tombstones;
ServerSession owns the native custom pools. Async allocation uses the same
linear allocation/fake-VA helper and server AllocationID table as cudaMalloc.
Native stream ordering and existing event/frontier dependencies determine when
an allocation may be used or freed. Successful async free invalidates its
client and server identity immediately, while independent TransferID staging
remains available for normal query/wait/frontier retirement. Ordinary linear
allocations can be asynchronously freed; synchronized pool allocations can be
ordinarily freed. Module storage cannot be freed through either path.

Native pool destruction with a live allocation is measured to defer backend
release. The pool token disappears, existing allocations stay usable and can
be freed, and destroying the selected pool restores default selection. No
alternate allocator, deferred-free queue or stream-lifetime mechanism was
introduced. Session cleanup synchronizes native work, retires staging, frees
live allocations, restores the native default-pool attributes/selection, trims
reservations and destroys custom pools. Default-pool thresholds survive native
context recreation on CoreX, so restoring their snapshot is necessary for
independent session isolation. Failure invalidation and reset also tombstone
client pool handles without reusing their storage.

Measured limitations are explicit: native reuse setters return success but
read back zero, so enabling reuse returns NotSupported; release thresholds and
high-water reset work. Native TrimTo alone retains cached memory even after
stream completion. Zero-target trim temporarily sets release threshold zero,
synchronizes the context through the existing backend seam, calls native trim
and restores the saved threshold; the probe and public client confirm actual
reservation release. Live allocations remain valid. Nonzero targets retain
native best-effort behavior and remain partial; no temporary threshold can
reclaim below their required minimum. Native usage/high-water counters account allocation
granules (2 MiB for a small allocation), so attribute queries are partial CUDA
accounting. Owning-device permissions stay ReadWrite even after native None/
Read setters; those changes are rejected. Empty access lists return measured InvalidValue. Zero-byte cudaMallocAsync
returns null and null async free succeeds locally, matching the measured native
Runtime; the underlying Driver rejects both with InvalidValue. The separate
cudaMallocFromPoolAsync entry returns InvalidValue for zero bytes in both
native Runtime and Driver, and the remote facade preserves that result. Only pinned,
device-zero, non-exportable pools are supported; peer access and IPC remain
excluded.

Independent public-library client → localhost → CRX9 → server → CoreX tests
cover all 13 APIs, native current/default identity, attributes and exact output
widths, event ordering across streams, H2D/D2D/D2H and kernel use, deferred host
visibility after async free and pool destruction, ordinary/async free
interoperability, default streams, fake-address uniqueness, stale tokens,
reset, malformed/invalid wire requests, normal exit and EOF session isolation.
Lifecycle/session isolation pass after allocation ownership changes. The
failure/reconnect suite now covers pool and async-pointer invalidation and fresh
pool identities after a server restart. A controlled negative protocol peer
proves callback error delivery, skipped host functions on errors and rejection
of pool RPCs on a legacy peer; it is not used as positive CoreX support evidence.
Build, public ABI/exports, protocol fixtures, deterministic schema generation/
rejection, handler uniqueness and backend seam pass. Historical multithread
validation was completed once for the core callback mutex change and is not
repeated for unchanged locking in this pool slice.

The closeout also adds partial cudaGetDriverEntryPoint support. Six queried
client functions (cuMemAlloc, cuMemFree, cuMemcpyHtoD, cuMemcpyDtoH,
cuMemcpyDtoD and cuCtxSynchronize) delegate to the existing Runtime allocation,
copy and synchronization paths. They accept the same fake pointers, share the
single implicit RuntimeContext and translate errors to CUresult. Default and
legacy query flags work; PTDS and other symbols return NotSupported with null
output. This is not a general Driver context or native-pointer compatibility
surface. A grouped native query/invocation probe and an independent public
localhost test verify mixed Runtime/Driver allocations, copies and errors.

The full audit covers the named core families, explicit name-based exclusions
for census misclassifications, all practical pool APIs, measured blockers,
codegen/ownership reuse, localhost evidence, documentation and commit hygiene.
No remaining eligible API is GROUND_TRUTH_REQUIRED or DEPRECATED. Relative to
cf5f8a8, 21 public APIs newly become supported, and cudaMemcpyAsync's previously
implemented D2D path is verified and accurately classified. Backend-unsupported
and blocked results are excluded from the implementation count.
Evidence and audit: `evidence/m5/memory-pools-20261012/`.
