# Continuing active Goal; do not reset the baseline

BASE_HEAD remains 0383270696516c003743e4784f3c5795168052f6. BASE_LEDGER contains
40 already supported APIs and 28 practical required APIs, excluding Graphics
Interop. Delta is 14; the Goal is not complete. Native probes have investigated
prerequisites for remaining groups, but public implementation and terminal
classification are still pending. Do not count prior supported APIs or native
probe-only success.

1. Implement graph memory nodes next. Native allocation/fill/copy/free data and
   replay pass, with fixed native addresses and 2 MiB query granules. Graph pool
   counters are separate from default/current Memory Pools. Use native
   cuGraphAddMemAllocNode and cuGraphAddMemFreeNode, and the existing AllocationID
   and fake-VA records. Do not allocate ordinary storage at definition time.
   Start with a truthful self-contained allocation/use/free DAG if broader
   unpaired ownership needs more state. Record requested canonical parameters,
   not the rounded native query. Graph-reserved pointers must be usable for
   graph parameter serialization but must not become ordinary live allocations
   before allocation-node execution. Source graph destruction must preserve an
   executable's reservation; retire fake identities only after their real owner
   lifetimes end. Investigate DAG ordering and the CUDA one-instantiation/no-clone
   restrictions for memory graphs. Normal malloc/free stale identity stays
   unchanged. Native probe also proves unpaired graph memory can be ordinarily/
   asynchronously freed and reactivated at the same native address by replay;
   any support for that requires explicit lifetime states in existing tables.

2. A bounded host-tail model appears possible using the EXISTING callback
   worker, not a new server callback framework. Inspect
   submit_host_callback_locked / host_callback_worker in corex_remote_cuda.c.
   It waits remote stream completion, retires the host-transfer frontier,
   invokes client code outside the mutex, and gates subsequent Runtime entries.
   A single host sink reachable from all GPU nodes could use a native empty
   placeholder plus a client-owned fn/userData snapshot, then queue the existing
   worker after graph launch. Validate true async return, client process identity,
   preceding GPU/D2H retirement, subsequent GPU ordering, graph/exec snapshots,
   clone, destroy and reset. Reject host nodes with GPU successors/independent
   GPU branches unless a truthful finer bridge is established. Pre-start the
   existing worker before GPU submission so allocation failure does not silently
   omit a callback. Never put a client function pointer on CRX9 or execute it on
   the server. This is a candidate to prove, not supported behavior yet.

3. User objects need client destructor ownership across graph/clone/exec and
   in-flight work; do not infer support from successful native create/retain/
   release. Explore whether the same callback/lifetime machinery can safely
   retain and retire them. Upload succeeds in native Runtime while its Driver
   symbol is absent; inspect Driver entry-point availability or a faithful
   preparation equivalent. Native DebugDotPrint succeeds; implement local
   bounded dot output from canonical inspection if suitable. Capture mode
   exchange initially reports ThreadLocal on CoreX. Probe real thread/process
   effects and preserve client TLS plus server guard semantics; storing an enum
   alone is not support for capture interaction modes.

Validation is proportional: grouped probes as needed, build/ABI/protocol/schema,
public independent localhost CRX9 graph-family integration. Native graph owners
and exec node correspondence changed in the child checkpoint, so lifecycle and
session isolation were justified and pass. Callback locking itself is unchanged;
multithread/reconnect history was not repeated. Keep this ONE evidence set.
Continue after checkpoint commits. Final closeout requires >=18 new supported
APIs and exploration of major groups (or genuine exhaustion), terminal statuses
for investigated APIs, recomputed delta, passing target checks, committed code
and a clean worktree. CHECKPOINT.md is not a completion audit.
