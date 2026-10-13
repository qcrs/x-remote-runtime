# Native Graph and Stream Capture model

CoreX 4.4 / MR-V100 runs the grouped native graph/capture probes in
`tests/probes/`. The remote subsystem uses real native graphs and executable
DAGs. The authoritative classifications and limits are in
`compat/cuda-api-ledger.csv`. The expansion from `0383270` adds 25 NEW supported
APIs (8 IMPLEMENTED, 17 PARTIAL_IMPLEMENTED); the 40 previously supported APIs
are baseline only. Three user-object APIs remain terminally blocked by the
remote destructor model. Evidence and the explicit baseline audit are in
`evidence/m5/graph-expansion-20261013/`.

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
event record/wait nodes, graph allocation/free nodes and bounded client host tails. Device copies include 1D, 2D and 3D regions,
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
do not extend regular allocation, module or event lifetimes. Graph-created
reservations and their executable owners use the lifetime rules below. ServerSession cleanup ends active native
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

## Child graphs

Child nodes use real native CoreX child graphs and the existing graph/node
registries. The embedded graph receives borrowed GraphID/NodeID identities and
copied client parameter storage. Parent node destruction tombstones those
identities and releases their server metadata; native ownership stays with the
parent node. Session cleanup never independently destroys borrowed native
graphs. The current slice permits one child level with empty, registered kernel,
device-copy and memset leaves; the complete parameter/topology owner DTO must
fit 4096 bytes. Borrowed graphs permit inspection and leaf-only cloning, with
mutation, direct destruction and direct instantiation unsupported. Parent graphs
containing child nodes cannot yet be cloned or used with BeginCaptureToGraph.

Each executable retains canonical node parameters, resource IDs, edge topology
and enable states independently. Native CoreX reports successful composite
child exec updates before first launch but ignores the new parameters; a fresh
clone or two consecutive setters do not fix that cold-exec behavior. The remote
setter therefore builds a native graph definition from the saved executable
snapshot with the new child, instantiates it and replaces the native exec under
the same GraphExecID. CoreX executes the resulting DAG. The existing exec entry
owns the new native definition and node correspondence; ordinary exec setters
and enable controls use that correspondence. Other executable parameters and
source graph mutations remain independent. No extra scheduler or registry is
introduced, and no warm-up launch executes unwanted work.

Replacement is allowed before first launch or after the previous launch's live
stream queries complete. In-flight or unresolvable previous streams return
NotSupported without replacing the old exec. This bound preserves serial launch
ordering when the underlying native exec changes. Client frontiers survive the
replacement. Repeated updates, disabled node restoration, other exec parameter
updates, source/parent destruction and stale module rejection pass through the
independent public CRX9 test. Lifecycle and EOF tests exercise private definition
and borrowed child cleanup.

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

Host transfers, host callbacks, multi-stream submission, Global/Relaxed capture initiation and
other unsupported operations are rejected. Unsupported execution invalidates
capture before performing that operation. End-capture aborts the native capture,
discards its graph and returns cudaErrorStreamCaptureInvalidated; the stream is
then usable again. Wrong-thread end returns cudaErrorStreamCaptureWrongThread
without consuming capture. This is a bounded capture slice, including a
conservative guard on other GPU operations while capture is active. It does not
claim CUDA's complete multi-thread, multi-stream or host-staging capture model.
The interaction-mode allocation exception is described below; it does not
capture allocations or permit submission from another stream or thread.

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
and free nodes now use the native graph allocator and the unified allocation
registries described below.

## Native graph allocation and free nodes

Graph memory uses native cuGraphAddMemAllocNode/cuGraphAddMemFreeNode, with the
same AllocationID table, fake-VA identity construction, checked pointer/region
helpers, kernel codecs and free/frontier paths as regular and stream-ordered
allocations. The measured graph pool is independent of the default/current
Memory Pool; no ordinary allocation is made at definition time. Properties are
bounded to device-zero pinned, non-exportable storage with no access descriptors.
Queries retain requested canonical parameters rather than the native 2 MiB
rounding. Only owning-graph free nodes are currently supported.

A graph reservation is an identity that graph parameter serialization may use.
It is not ordinary live storage before allocation-node execution. Self-contained
allocation/use/free graphs remain reserved outside graph execution. An unpaired
allocation becomes available to normal stream operations after launch; free or
async free makes it reserved again. Replaying its owner activates the same fake
address and AllocationID. Regular freed allocations stay stale and are never
revived. Launching an unpaired allocation while it is still active is rejected.
The server validates allocation-before-use and use-before-free DAG ordering,
including registered kernel pointer arguments, before instantiate, launch and
parameter mutation. Graphs with memory nodes cannot be cloned or have nodes or
edges deleted, and only one native instantiation may exist. Memory-node additions
stop after first instantiation; capture, whole exec update and child combinations
with memory nodes are outside this slice.

The source graph may be destroyed while an executable still owns the native
reservation. CoreX additionally unmaps active unpaired memory at physical exec
destruction. Logical exec destruction therefore tombstones the public handle and
retains its native entry while such allocations remain active. External free,
completed free/launch streams, device synchronization, trim and session cleanup
retire those native owners. Async free invalidates ordinary access immediately;
existing TransferID staging survives and retires through the usual frontier.
EOF cleanup synchronizes and frees active graph storage before destroying native
exec mapping owners, then clears their reservation identities. No separate
allocator, resource registry or graph scheduler was added.

## Client host tails, upload and DOT output

A supported graph contains at most one host node, a sink reachable from every
other node. Its function and userData remain in client parameter storage and
independent executable snapshots. The native DAG has an empty ordering node at
that position. After native launch, the existing client callback worker waits
remote stream completion, retires the launch frontier's hidden host transfers,
and invokes the client function outside the Runtime mutex. Runtime entries are
gated until it finishes, so subsequent GPU work cannot overtake it. Worker
startup is checked before submitting GPU work. Launch returns asynchronously;
source/clone/exec parameter changes stay independent. Upload does not invoke the
callback. GPU errors skip host functions as in the existing host callback model.

Host nodes with successors, disconnected branches or multiple host nodes are
rejected at instantiation. This remains a process-serialized, host-tail subset;
it requires preceding GPU work to finish autonomously and callbacks to obey the
CUDA prohibition on CUDA calls. It does not claim arbitrary GPU/host alternation,
parallel remote callbacks, host-staging capture or server-side client execution.
No client function pointer or client host path crosses CRX9.

The Driver ELF export for cuGraphUpload is absent on CoreX, but the official
cuGetProcAddress lookup at API version 11010 returns its native primitive. The
backend invokes that entry with the actual exec and stream, after validating the
saved resource owners. The grouped probe and public test prove preparation does
not execute the graph or its callback, followed by correct launch. Default DOT
output uses bounded graph inspection and writes node types/edges on the client;
verbose parameter flags and native addresses remain unsupported.

Uploading native allocation nodes activates backing prematurely. For inactive
unpaired allocations and quiescent upload/previous-launch streams, measured
native free restores the reservation before launch without changing its fake
address or AllocationID. CoreX retains the used counter after later free and
graph/exec destruction until GraphMemTrim; the native and EOF probes verify
that trim brings it to zero. Active, in-flight and paired allocation/free graph
upload is rejected before execution. The paired native graph permits upload but
forbids the external free needed by this repair.

## Capture interaction and user-object limits

The capture interaction mode is client TLS, initially Global, and swaps valid
enums independently across threads. For active ThreadLocal capture, owner
cudaMalloc is allowed in Relaxed interaction and another client thread may
allocate outside capture. An atomic client transaction grants a one-shot native
relaxed allocation on the server's single request thread, then restores its
previous native mode. The permission is consumed by the next request and is
limited to allocation. Default/ThreadLocal owner allocation still invalidates
capture before executing. Other unsafe calls remain conservative. This extends
interaction behavior without claiming Global/Relaxed capture initiation,
multi-stream submission or CUDA's complete unsafe-call matrix.

Native user-object create/retain/release, clone and exec ownership work: the
native destructor stays pending until the last owner is released. A blocking
native destructor remains running when context synchronization returns, proving
NoDestructorSync behavior. The current client host worker instead gates every
Runtime entry and skips host functions on errors. It cannot truthfully provide
that independent destruction contract. Native graph/clone/exec reference release
also has no client notification channel, including lost sessions and retained
native exec mapping owners. User-object support requires an asynchronous release
notification and non-gated client destructor lifecycle with a proven failure
policy. The three API rows are GROUND_TRUTH_BLOCKED; no CPU-only reference facade
or server execution of client destructor pointers is claimed.

## Validation

Run `scripts/build.sh`, then:

```bash
python3 scripts/run-graphs.py --expansion --out evidence/m5/graph-expansion-20261013
```

Grouped native probes measure data, lifetime, cold child update behavior, upload,
TLS and destructor limits. Independent public clients and direct-wire tests run
through localhost → CRX9/V3 → runtime_server → CoreX. They cover graph/exec
snapshots, enable state, memory reservation/replay/free, client callback process
identity and ordering, capture interaction, malformed DTOs and EOF cleanup.
Build/ABI/protocol, deterministic schema generation/rejection, handler uniqueness
and backend seam checks accompany lifecycle/session isolation for changed
ownership. Callback locking itself is reused; the targeted host and capture
thread tests cover concurrency without repeating exhaustive historical suites.
The closeout compares the frozen baseline directly and excludes prior support
and blocked APIs from NEW_SUPPORTED_DELTA.

CUDA semantic references: [CUDA Graphs](https://docs.nvidia.com/cuda/cuda-programming-guide/04-special-topics/cuda-graphs.html),
[Graph API](https://docs.nvidia.com/cuda/cuda-runtime-api/group__CUDART__GRAPH.html),
and the installed CoreX Runtime/Driver contracts.
