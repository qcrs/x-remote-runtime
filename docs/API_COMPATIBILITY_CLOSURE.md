# CUDA Runtime compatibility closure

This document records the M8 closure snapshot. Subsequent practical semantics
and ownership repairs are in
[M9 partial semantics completion](PARTIAL_SEMANTICS_COMPLETION.md); the public
status counts are unchanged. In particular, M9 adds fatbin library inputs and
expands the Runtime-backed Driver entry bridge beyond the closure table below.

Baseline: `1f9502b05d12f46df21c95cd0aac78a88dd871e1` (clean, 2026-10-13).
Scope: the 266 public Runtime declarations in the installed CoreX 4.4 census.
[The ledger](../compat/cuda-api-ledger.csv) remains authoritative. Closure is an
evidence-backed classification of this environment, not full CUDA compatibility.

All 35 previously required declarations are terminal: one implemented, 28
ground-truth blocked, six backend unsupported. All 77 baseline partial APIs were
reviewed individually in [the partial audit](../evidence/m8/compatibility-closure-20261013/partial-review.csv).
`cudaChooseDevice` and `cudaGetErrorName` become implemented. The newly supported
`cudaDeviceCanAccessPeer` reports no self-peer access after remote initialization
and rejects ordinals other than zero. Distinct-device operations remain blocked.

Practical improvements also complete unaligned host registration and shared-page
lock ownership, query actual native cache-mode/dynamic-shared-memory attributes,
and repair human-readable error diagnostics (RC-008). Host registration and
function attributes retain their other documented partial limits. Function-info
opcode 92 uses generated KernelID/scalar codecs and negotiated capability 10;
the original seven-field opcode 37 remains available to older peers.

| Remaining reason | Families and compatibility boundary |
| --- | --- |
| Native operation unsupported | Valid array/mipmapped/managed allocations, linear textures, legacy texture/reference operations, cooperative launch and library kernel attribute setting return measured 801 or obsolete Runtime 31. Dependent array/surface/managed operations are blocked because their valid owners cannot be created. |
| Only one device | Peer enable/disable, P2P links, peer copies and cooperative multi-device launch lack a valid distinct-device positive case. No unavailable hardware is labeled a remote implementation failure. |
| Cross-process ownership | Client OS-locked staging has no native GPU alias, mapped/IO/read-only/write-combined policy or shared pageable attachment. Native two-process IPC memory/event sharing works, but the one-connection server destroys owners on disconnect; cross-client imports need concurrent contexts and shared lifetimes. |
| External prerequisites unavailable | No GPU Vulkan/EGL/NvSci exporter, NvSci SDK or native GL registration entrypoint is available. External mapping/signaling and graphics resource operations have no valid prerequisite objects or CRX9 descriptor-transfer ownership contract. Import/lifetime and pool sharing remain explicit release exclusions. |
| Backend behavior differs | Pool reuse/access setters may succeed without changing readback, counters use backend granules and trim needs the existing measured zero-threshold workaround. Stream nonempty policy windows fail; function carveout query and occupancy disable-caching flags return 801. These are preserved, not replaced by software counters or no-ops. |
| Bounded execution model | Library loading accepts metadata-bearing compiled ELF only; symbols exclude managed registration/string lookup and use bounded names. Driver entry queries expose six Runtime-backed functions. Extended launch attributes remain unimplemented beyond Ignore. Context flags cannot change with live owners. |
| Bounded Graph/Capture model | Single-stream ThreadLocal capture, device-only copies, kernelParams ABI, bounded DAG/DTOs, restricted children/memory nodes/update/flags/diagnostics and process-serialized host tails remain partial. User Objects need independent no-sync client destructors and native release notification. See [the detailed model](GRAPH_CAPTURE_MODEL.md). |

Final public counts: **107 implemented, 75 partial, 63 blocked, 13 backend
unsupported, 8 out of scope, 0 required**. Remaining partial rows by ledger
family: Graph 32; Other Runtime 12; Stream 8; Host Memory 7; Memory Pool / Async
Allocation 6; Memcpy 4; Execution 3; Device 2; Function 1. Private compiler ABI
hooks are excluded from these totals.

Evidence is one [M8 closure root](../evidence/m8/compatibility-closure-20261013/).
The terminal review includes the prior 35 blocked and seven unsupported rows;
older valid native evidence is referenced rather than copied. Validation covers
grouped native probes, public localhost CRX9 clients, malformed KernelID DTOs,
host/stream controls, affected host-interaction/numerical workloads, and
build/ABI/protocol/schema/registry/backend gates. Tests use independent client
and server processes; workload qualification also uses namespace/veth topology.
No new physical two-host or performance qualification is claimed.

Semantic references: installed CoreX Runtime/Driver headers and measured native
operations; [CUDA IPC](https://docs.nvidia.com/cuda/cuda-programming-guide/04-special-topics/inter-process-communication.html)
and [CUDA programming contracts](https://docs.nvidia.com/cuda/archive/12.5.1/cuda-c-programming-guide/index.html).
