# Native Graph and Stream Capture model

CoreX 4.4 / MR-V100 passes the grouped native graph and capture probes in
`tests/probes/corex_graph*_probe.c`. The remote subsystem uses those real native
graphs, nodes and executable graphs. The authoritative API classifications and
per-API limits are in `compat/cuda-api-ledger.csv`. This batch adds 40 supported
public APIs: 22 IMPLEMENTED and 18 PARTIAL_IMPLEMENTED. Those 40 APIs are the
baseline for the subsequent expansion at `0383270`; the active expansion's
first checkpoint adds 11 APIs and is not goal completion.

## Objects and execution

RuntimeContext owns generation-tagged client graph/node/exec tokens. Tokens are
monotonic tombstones, sharing the existing reset and failed-session boundary.
ServerSession owns native graph, node and executable handles. CRX9 carries only
GraphID, NodeID, GraphExecID, StreamID, EventID, AllocationID and KernelID, scalar
parameters and bounded copied kernel argument bytes. Native handles and client
addresses never cross the socket. Graph operations require HELLO capability 9;
legacy and non-advertising peers are rejected.

The generated opcode 88 envelope encodes graph commands and responses, bounds
their lengths, registers the handler and supplies the typed client call. The
action values are pinned in `include/internal/corex_graph_protocol.h`. Handwritten
code owns DAG policy, native operation translation, snapshots, cleanup and
capture state. The existing kernel launch serializer/decoder is shared with
immediate launches; graph kernels use the same registered ABI metadata and
pointer-to-AllocationID conversion. Pitched/3D copies reuse the existing checked
region helpers. All GPU execution occurs on the real server/CoreX path.

Graphs support empty, registered kernel, pointer-backed device memcpy, memset,
event record and event wait nodes. Device copies include 1D, 2D and 3D regions,
nonzero positions and pitches. Host and array endpoints are rejected. Kernel
nodes accept kernelParams with copied pointer/value arguments; the extra buffer
ABI is rejected. Memsets support element sizes 1, 2 and 4 through the native node
primitive. Graph inspection covers nodes, roots, edges, node types, dependencies
and dependents. Edge mutations validate ownership, duplicate/missing edges and
cycles before applying the batch. Clone creates independent node identities and
parameter storage; immediate clone lookup retains correspondence.

Instantiation retains separate canonical parameter and resource-ID snapshots.
Graph or node mutation/destruction leaves existing executable behavior intact.
Every instantiate, launch and parameter update validates live allocation,
module/kernel and event owners before submitting native work. Freed allocations,
unloaded modules and destroyed events prevent future launches; graph snapshots
do not extend those resource lifetimes. ServerSession cleanup ends active native
capture before synchronization and destroys graph/executable objects before
releasing resource owners still live in the session. Individual resource
retirement invalidates future graph use.

Native CoreX serializes repeated launches of the same executable. Client exec
frontiers merge that ordering with existing hidden-transfer retirement across
launch streams. Event wait node frontiers join the launch frontier; event record
nodes publish the launch frontier so normal event synchronization retires prior
host transfers. No alternate allocator, transfer queue or scheduler was added.

Event record/wait queries and graph setters now share the canonical event DTO.
Exec event setters call native CoreX and replace only that executable's EventID
and client frontier owner. Each exec event snapshot records its source NodeID,
so updates reject unrelated nodes and keep graph parameters and other execs
independent. Destroyed events still invalidate future launches; setters do not
extend an event's lifetime.

Node parameters can change on a graph or executable. Executable kernel setters
retain function identity. Whole-executable update supports the original live
graph and existing node correspondence; clone/recapture update is rejected.
CoreX reports topology rejection as Driver result 1000 with update result 2;
the measured pair is translated to Runtime cudaErrorGraphExecUpdateFailure (910)
while preserving the old executable. Other unexpected backend errors are not
presented as successful updates. Zero instantiate flags work; nonzero modes and
legacy instantiate error-node/log diagnostics remain unsupported.

Each graph has at most 64 nodes; edge-mutation batches contain at most 64 edges.
Server tables bound live graph/exec counts at 128 each and node records at 4096.
Client tombstone budgets are 128 graph tokens, 128 exec tokens and 4096 node
tokens for the process lifetime, including resets. Node parameter DTOs are
bounded at 4096 bytes. Exhaustion is explicit. No resource slot can turn an old
client token into a new live object.

## Capture

The practical capture slice supports one explicit stream at a time in
ThreadLocal mode, with submissions and end-capture on the initiating client
thread. Begin/end invoke native CoreX capture. Captured kernel, device-copy and
byte-fill submissions become native nodes, and native capture-info supplies the
new node identity and dependency frontier. The returned graph uses the same
object model, inspection, mutation, instantiate and replay paths as explicit
graphs. Capture queries report status, a process-unique capture ID, the graph
token and a borrowed current dependency array. Captured work does not execute
until graph launch; repeated replay uses the retained parameters.

Host transfers, host callbacks, multi-stream submission, Global/Relaxed modes and
other unsupported operations are rejected. Unsupported execution invalidates
capture before performing that operation. End-capture aborts the native capture,
discards its graph and returns cudaErrorStreamCaptureInvalidated; the stream is
then usable again. Wrong-thread end returns cudaErrorStreamCaptureWrongThread
without consuming capture. This is a bounded capture slice, including a
conservative guard on other GPU operations while capture is active. It does not
claim CUDA's complete multi-thread, multi-stream or host-staging capture model.

BeginCaptureToGraph now starts native capture into an existing graph, with
bounded dependencies and zero edge data. End returns the same graph token.
Explicit node additions to the capture graph permit branch construction;
UpdateCaptureDependencies adds to or replaces the current native frontier.
Capture info returns its bounded borrowed dependency array, including multiple
or zero dependencies. Updates retain initiating-thread ownership. Software
invalidation of capture into a caller-supplied graph ends native capture and
leaves that graph alive for inspection and destruction. Global/Relaxed modes,
nonzero edge data and multi-stream capture remain unsupported.

## Graph memory controls

Generated opcodes 89–91 expose four native uint64 graph memory attributes,
zero-only high-water resets and native trim on device zero. The grouped native
allocation/fill/copy/free probe measures actual data and replay: CoreX reports
2 MiB allocation granules, holds used counts through same-graph free until graph
destruction, and then trim releases reservations. High-water reset reads back
zero after destruction. Default Memory Pool counters remain independent.
Accounting queries are therefore partial CUDA accounting; reset and trim use
the native semantics without synthetic counters. Public graph memory allocation
and free nodes remain implementation work for this active expansion.

## Validation and continuation boundary

Run `scripts/build.sh`, then:

```bash
python3 scripts/run-graphs.py --out evidence/m5/graphs-capture-20261013
```

The runner executes grouped native Driver/Runtime probes and independent public
client and direct-wire tests against a localhost runtime_server. It exercises
composition, repeat launch, native clone, graph/exec parameter separation,
updates, enable/disable, pitched 3D data/padding, event/frontier retirement,
capture replay/invalidation/thread ownership, stale resources, reset, malformed
wire requests and EOF during capture. Lifecycle/session isolation and the
numerical baseline cover the changed cleanup and shared launch codec. Existing
locking and scheduling mechanisms are reused, so historical multithread and
reconnect suites are not rerun for this batch.

The goal closes at its 40-supported-API target with explicit graphs and practical
capture both implemented. It does not claim that every remaining graph API is
exhausted: child graphs, host-node/staging capture, multi-stream capture and
other uninvestigated node families remain outside this measured slice.

Evidence: `evidence/m5/graphs-capture-20261013/`. CUDA semantic references:
[CUDA Graphs](https://docs.nvidia.com/cuda/cuda-programming-guide/04-special-topics/cuda-graphs.html)
and the installed CoreX Runtime/Driver header contracts.
