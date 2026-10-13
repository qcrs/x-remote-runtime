# Current architecture

Deployment boundary: aarch64 client -> CRX9/TCP -> aarch64 server -> CoreX 4.4 /
MR-V100, with compatible host ABI. The wire format does not provide cross-ISA
compiler ABI translation.

```text
application + compiler-generated CUDA ABI
  -> public libcorex_remote_cudart.so.1
  -> one process-owned RuntimeContext
  -> CRX9/V3 socket requests and responses
  -> connection-owned ServerSession
  -> corex_backend (statically linked Driver seam)
  -> CoreX Driver / MR-V100
```

## Execution boundary

[src/corex_remote_cuda.c](../src/corex_remote_cuda.c) exports Runtime calls and
compiler registration hooks. Fatbinary extraction in
[src/corex_fatbin_runtime.c](../src/corex_fatbin_runtime.c) supplies a loadable
CoreX image; registration and [metadata](../src/corex_metadata.c) associate host
function symbols with remote kernels and `DEVICE_PTR`/`BY_VALUE` argument layouts.
Launch serialization copies scalar arguments and resolves fake pointers into
AllocationID plus offset. Native handles and host addresses are never sent as
usable remote pointers. Global/constant compiler symbols use module-owned
allocation aliases.

[RuntimeContext](../include/internal/corex_runtime_context.h) owns one socket,
request sequence, negotiated capabilities, client registries and lifecycle.
Its coarse mutex serializes Runtime/RPC state. Last-error, device selection and
compiler launch configuration have thread-local state. The callback condition
gates subsequent Runtime entrypoints until the client worker finishes a queued
host function, which runs outside the mutex; this serializes unrelated streams.

[CRX9](../include/internal/corex_protocol.h) uses fixed-width network-order fields,
explicit IDs, bounded payloads and HELLO version/capability negotiation.
[Schema/codegen](API_SCHEMA_CODEGEN.md) supplies repetitive DTOs/codecs and
registry plumbing; ownership, bounds policy, ordering and state machines remain
handwritten. The registry dispatches into
[ServerSession handlers](../server/runtime_server.c), which validate requests and
resolve IDs before calling the [CoreX backend](../server/corex_backend.c).
The seam owns direct Driver calls; the client DSO has no native Driver/Runtime
link dependency. The server currently serves one connection at a time in one
native device/context, then cleans it and recreates the context for the next
session. This is not concurrent multi-client scheduling.

## Identity and lifetime

| Identity | Client representation | Server owner and retirement |
| --- | --- | --- |
| AllocationID | generation-tagged fake VA range with size/layout and offsets | session allocation table owns device storage; module globals and Graph reservations carry parent ownership |
| StreamID | monotonic token, flags/priority, transfer sequence and causal frontier; ID 0 is legacy default stream | session owns native stream; destroy synchronizes/retires related work |
| EventID | generation-tagged token and recorded frontier | session owns native event; record/wait propagates dependencies |
| ModuleID / KernelID | compiler or controlled/library registration tokens and ABI metadata | module owns native code/global storage; kernels refer to module; unload drains work and invalidates kernels/aliases |
| PoolID | generation-tagged default/current/custom token | native device-zero pool; destruction invalidates pool token while measured native semantics allow live allocations to survive |
| GraphID / NodeID / GraphExecID | generation-tagged monotonic graph tokens and parameter/resource snapshots | session owns native DAG/node/exec records; executable snapshots survive source graph destruction but do not retain ordinary resource owners |
| TransferID | hidden transfer record with host destination, generation and frontier | session owns staging and completion event independently of allocation retirement |

Fake VA comes from a reserved client arena and is never device-dereferenced on
the client. Address ranges and token storage are not reused across reset/session
failure; live registry records must also match the active generation. Server IDs
are monotonically allocated within a session and may restart in the next session;
client generation ownership prevents stale tokens from resolving to a new object.
Client page-locked staging records are process-owned and survive remote reset.
Unaligned registrations track byte ranges in that same host registry. Disjoint
ranges can share an OS page; unregister unlocks only pages with no remaining
registered owner, after retiring pending host transfers.

H2D submission copies host data into server staging. D2H bytes become visible at
client synchronization/query/frontier retirement boundaries, not simply when a
server submission returns. Stream/event/legacy-default-stream frontiers preserve
causal ordering across these hidden transfers. Transport failure invalidates
pending D2H without writing old results into host memory. Reset drains work and
closes the session; EOF also triggers server cleanup. Cleanup ends capture and
retires graphs/execs, stream work, transfer staging, events/streams, allocations,
modules/kernels and custom pools before context recreation. Default pool settings
are restored for session isolation.

Native Graphs and bounded capture reuse these registries, launch serializers and
frontiers. Graph allocation reservations, executable ownership, child graphs and
client host tails have additional rules in [GRAPH_CAPTURE_MODEL](GRAPH_CAPTURE_MODEL.md);
that document remains the detailed authority rather than duplicating them here.

## Document roles

[README](../README.md) owns the operational entrypoint;
[RELEASE_READINESS](RELEASE_READINESS.md) owns the release result and evidence map;
[PROJECT_STATE](PROJECT_STATE.md) owns current development status.
[The ledger](../compat/cuda-api-ledger.csv) owns per-API support and limits.
The Graph model, memory layout and other capability docs own detailed semantics;
remote correctness and real workload docs define qualification scope;
[the issue register](REMOTE_CORRECTNESS_ISSUES.md) owns RC findings.
[HISTORY](HISTORY.md), `docs/gate8/` and prior evidence preserve development history.
