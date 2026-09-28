# Project State

Current autonomous roadmap position: **M3-S7 complete; M4-S1 ready** (M1–M3 PASS locally).

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
| Last PASS commit | M3-S7 (`m3/s7: add probed device and function runtime APIs`, this state commit) |
| Current Slice | M4-S1 pitched and two-dimensional memory |
| Blockers | None |
| Next action | Begin Wave 1 pitched and two-dimensional memory probe |

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
for three simple query/control APIs. The deterministic generator emits checked
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
session-isolation, reconnect, and multithread gates passed. Evidence:
`evidence/m3/s5/20260924-XXXXXX/`.

M3-S6 upgrades the reviewed schema to Codegen V2 for fixed-width scalar and
object-ID APIs. Generated metadata, network-order codecs, exact payload
validation, registry coverage, ABI export coverage, and negative schema tests
are reproducible. The schema now covers opcodes 4, 14, 19, and 39–46 while
preserving opcodes 1–38. `scripts/verify-api-contracts.py` checks schema,
ledger, protocol, and packaging consistency.

M3-S7 adds CoreX-proven device/context queries and registered-kernel setters:
`cudaDeviceGetAttribute`, `cudaGetDeviceFlags`,
`cudaDeviceGetStreamPriorityRange`, `cudaDeviceGetLimit`,
`cudaDeviceGetCacheConfig`, `cudaDeviceGetSharedMemConfig`,
`cudaFuncSetAttribute`, and `cudaFuncSetCacheConfig`. CoreX 4.4 probe output
is preserved in `evidence/m3/s7/20260928-084000/`; supported and native
unsupported limits are tested without guessing. `cudaFuncGetAttributes` is
classified `PARTIAL_IMPLEMENTED` because three Runtime fields remain local
defaults without a proven CoreX query.
