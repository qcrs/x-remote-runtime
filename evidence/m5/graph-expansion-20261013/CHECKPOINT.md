# Implementation checkpoint; Goal remains active

BASE_HEAD: `0383270696516c003743e4784f3c5795168052f6`. The saved BASE_LEDGER matches the rows at that commit.
BASE_SUPPORTED=40. Previously supported APIs contribute zero to this expansion.
NEW_SUPPORTED_DELTA=14 (8 IMPLEMENTED, 6 PARTIAL_IMPLEMENTED).

- cudaDeviceGetGraphMemAttribute — PARTIAL_IMPLEMENTED
- cudaDeviceGraphMemTrim — IMPLEMENTED
- cudaDeviceSetGraphMemAttribute — IMPLEMENTED
- cudaGraphAddChildGraphNode — PARTIAL_IMPLEMENTED
- cudaGraphChildGraphNodeGetGraph — PARTIAL_IMPLEMENTED
- cudaGraphEventRecordNodeGetEvent — IMPLEMENTED
- cudaGraphEventRecordNodeSetEvent — IMPLEMENTED
- cudaGraphEventWaitNodeGetEvent — IMPLEMENTED
- cudaGraphEventWaitNodeSetEvent — IMPLEMENTED
- cudaGraphExecChildGraphNodeSetParams — PARTIAL_IMPLEMENTED
- cudaGraphExecEventRecordNodeSetEvent — IMPLEMENTED
- cudaGraphExecEventWaitNodeSetEvent — IMPLEMENTED
- cudaStreamBeginCaptureToGraph — PARTIAL_IMPLEMENTED
- cudaStreamUpdateCaptureDependencies — PARTIAL_IMPLEMENTED

Validation: build/ABI/protocol golden fixtures, schema reproducibility/rejection,
handler duplicate rejection/backend seam, grouped native observations and memory
data/replay, independent public localhost CRX9/V3 graph integration, new public
expansion integration, direct-wire DTO/ownership checks, lifecycle and session
isolation pass. Evidence logs are in this directory. Historical multithread and
reconnect suites were not rerun; no locking or callback worker changes were made.

This is an implementation checkpoint, **not Goal completion**. No final
completion audit has passed. The 18-new-supported threshold is not reached,
and remaining practical groups are not exhausted. Native investigation of
remaining groups is ongoing; these rows have not yet received their final
remote semantic classification. `NEW_SUPPORTED_DELTA.csv` is the explicit
baseline comparison and must be recomputed at final closeout.

## Native findings and next implementation

- Child add/query/exec update: implemented with bounded read-only borrowed
  child identities and native executable rematerialization from canonical params,
  topology and enabled states. Native composite exec setters ignore changes
  before first launch even with a fresh clone or repeated setters. A native
  rebuilt definition executes the current value on its first launch. The public
  test verifies first-launch update, repeated quiescent update, other exec params
  and disabled state preservation, source/parent destruction and stale modules.
  Native direct instantiation of an embedded child returns InvalidValue; remote
  direct instantiation is explicitly unsupported. Existing leaf graph cloning
  remains supported; parent graphs with children now classify GraphClone partial.
- Graph memory nodes: real alloc/fill/copy/free repeated launch data passes.
  Allocation query rounds requested 32 bytes up to 2 MiB. Canonical requested
  parameters should be retained. Native graph allocation has its own pool:
  default/current pool counters do not include it. Reuse the existing AllocationID
  and fake-VA tables and stream/free machinery; do not substitute MallocAsync
  at graph definition time. Probe proves externally freed unpaired graph memory
  can be reallocated at the same native address by replay. Native graph/free-node
  ownership restrictions require a handwritten lifetime state in those tables.
- Host nodes: native server-process callback executes; existing remote host
  callbacks run on the client worker. A graph-ordered server/client handshake
  must be established before any support claim; client pointers cannot cross.
- User objects: native create/retain/release succeed. Client destructor lifetime
  and graph/clone/exec/in-flight reference ownership still need a safe model.
- Upload: Driver symbol is absent, but native Runtime upload succeeds for an
  empty graph. Investigate a Driver entry-point or truthful preparation equivalent
  before implementing or terminally classifying; no remote support is claimed.
- Capture mode: native exchange succeeds and initially reports ThreadLocal (1).
  Thread/process effects and remote guard behavior remain separate investigation.
- Dot query/output: native DebugDotPrint succeeds; client filesystem output should
  reconstruct bounded graph data rather than send a client path to the server.

Pending public implementation/classification (14):
- cudaGraphAddHostNode
- cudaGraphAddMemAllocNode
- cudaGraphAddMemFreeNode
- cudaGraphDebugDotPrint
- cudaGraphExecHostNodeSetParams
- cudaGraphHostNodeGetParams
- cudaGraphHostNodeSetParams
- cudaGraphMemAllocNodeGetParams
- cudaGraphMemFreeNodeGetParams
- cudaGraphRetainUserObject
- cudaGraphUpload
- cudaThreadExchangeStreamCaptureMode
- cudaUserObjectCreate
- cudaUserObjectRelease
