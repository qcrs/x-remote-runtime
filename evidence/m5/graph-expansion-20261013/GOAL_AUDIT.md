# Graph/Capture expansion requirement audit

Audit status: VALIDATION PASSED. Implementation commit and final clean-tree
inspection are the remaining closeout gates.

BASE_HEAD = `0383270696516c003743e4784f3c5795168052f6`. Frozen BASE_LEDGER matches the corresponding rows at
that exact commit. BASE_SUPPORTED=40. Existing support contributes zero.
NEW_SUPPORTED_DELTA=25 (8 IMPLEMENTED, 17 PARTIAL_IMPLEMENTED).
All 28 initially required practical API rows are terminal: 25 supported and
three GROUND_TRUTH_BLOCKED user-object APIs. Graphics Interop is excluded.

## Explicit new supported delta

- cudaDeviceGetGraphMemAttribute — PARTIAL_IMPLEMENTED
- cudaDeviceGraphMemTrim — IMPLEMENTED
- cudaDeviceSetGraphMemAttribute — IMPLEMENTED
- cudaGraphAddChildGraphNode — PARTIAL_IMPLEMENTED
- cudaGraphAddHostNode — PARTIAL_IMPLEMENTED
- cudaGraphAddMemAllocNode — PARTIAL_IMPLEMENTED
- cudaGraphAddMemFreeNode — PARTIAL_IMPLEMENTED
- cudaGraphChildGraphNodeGetGraph — PARTIAL_IMPLEMENTED
- cudaGraphDebugDotPrint — PARTIAL_IMPLEMENTED
- cudaGraphEventRecordNodeGetEvent — IMPLEMENTED
- cudaGraphEventRecordNodeSetEvent — IMPLEMENTED
- cudaGraphEventWaitNodeGetEvent — IMPLEMENTED
- cudaGraphEventWaitNodeSetEvent — IMPLEMENTED
- cudaGraphExecChildGraphNodeSetParams — PARTIAL_IMPLEMENTED
- cudaGraphExecEventRecordNodeSetEvent — IMPLEMENTED
- cudaGraphExecEventWaitNodeSetEvent — IMPLEMENTED
- cudaGraphExecHostNodeSetParams — PARTIAL_IMPLEMENTED
- cudaGraphHostNodeGetParams — PARTIAL_IMPLEMENTED
- cudaGraphHostNodeSetParams — PARTIAL_IMPLEMENTED
- cudaGraphMemAllocNodeGetParams — PARTIAL_IMPLEMENTED
- cudaGraphMemFreeNodeGetParams — PARTIAL_IMPLEMENTED
- cudaGraphUpload — PARTIAL_IMPLEMENTED
- cudaStreamBeginCaptureToGraph — PARTIAL_IMPLEMENTED
- cudaStreamUpdateCaptureDependencies — PARTIAL_IMPLEMENTED
- cudaThreadExchangeStreamCaptureMode — PARTIAL_IMPLEMENTED

## Requirements and authoritative evidence

| Requirement | Evidence / current state |
| --- | --- |
| Freeze exact starting HEAD and API rows before implementation | BASE_HEAD, BASE_LEDGER.csv, BASELINE.txt, BASELINE_COMPARISON.txt; exact row parity checked against git show |
| Baseline support counts as zero | Delta compares status sets; all 40 baseline supported names excluded |
| New implementation, rather than audit/revalidation | Commits 89cda0e/8fc5feb plus current allocation, host, upload and control source changes |
| >=18 newly supported APIs and major groups explored | 25 delta rows; grouped probes and implementations listed below |
| Child graphs and independent executable updates | Native cold setter defect/rebuilt first launch; public expansion kernel/copy/enable-state snapshots |
| Event query and mutation | Six new APIs; independent graph/exec EventID and frontier snapshots |
| Native graph memory and existing pool/async model | Native graph pool differs from default pool; unified allocation/fake-VA table, shared codecs/free/frontier, memory public test |
| Memory allocation identity, ordering, snapshots, stale rules | Reserved/active state, same-address replay, DAG checks, source destruction, unpaired external free and native exec retention |
| Accounting/control | Native granules, high-water reset, trim; generated opcodes 89–91 |
| Host client execution without client pointers on server | Universal host sink, native empty ordering placeholder; existing client worker after GPU/frontier completion; host integration PID/async/order/snapshots |
| Upload | native-remaining.stdout.log: official Driver entry, preparation without execution, premature memory activation/free repair, cached used counter cleared by trim; paired-free upload excluded |
| Capture-to-graph/dependencies | Native identity and frontier updates; public branching/empty dependency tests |
| Capture mode separately preserves thread semantics | Native Runtime TLS defaults and thread independence; native unsafe allocation guards weak; facade TLS plus one-shot server override tested |
| User-object safe ownership investigated | Native source/clone/exec ref lifetime and NoDestructorSync proved; current worker/notification limits terminally blocked |
| Practical remaining query/update | Client DOT, child updates, event setters and capture dependencies implemented |
| Exclude Graphics/IPC/Peer/External Interop | Selection excludes cudaGraphics names; no interop capability claims or changes |
| Reuse schema/codegen and existing ownership machinery | Existing OP_GRAPH envelope, ID types, native backend, allocation tables, callback worker and frontier reused; scalar memory controls generated |
| Independent public transport validation | Public clients -> localhost socket -> CRX9/V3 -> runtime_server -> CoreX; runner records server PID/port and topology |
| Proportionate build/ABI/protocol/schema/handler checks | build.stdout.log, schema.stdout.log, registry.stdout.log; all PASS |
| Ownership/concurrency coverage | memory/host/control-integration.stdout.log, lifecycle.stdout.log, session-isolation.stdout.log; all PASS; mutex/transport framework unchanged |
| One evidence set, no binaries/generated trees copied | This directory contains records/logs/hashes only; generated sources remain canonical |
| Ledger authoritative and investigated rows terminal | 25 support statuses + three blocked; no eligible required rows remain |
| Current documentation and high-level README | GRAPH_CAPTURE_MODEL/PROJECT_STATE/README describe actual additions and limits |
| Commit implementation and clean final worktree | PENDING final implementation commit and post-commit status inspection |

## Measured limits and truthful boundaries

The native composite child setter ignores updates before first launch. The
remote setter rebuilds the saved native definition and owns it within the
existing exec entry; it does not warm up by executing old work. Updates require
a quiescent previous launch stream. Child graphs are one level with bounded
GPU leaves and read-only borrowed identities. Parent cloning is unsupported.

Graph memory retains requested canonical size despite native 2 MiB query
rounding. Native graph allocation uses its own pool; it is not replaced with an
ordinary allocation or a separate software allocator. Retired native execs keep
active unpaired mapping ownership until external free and GPU completion.
Properties are bounded, free nodes belong to their allocating graph, and memory
node additions stop after initial instantiation. Cross-graph free, memory capture,
child combinations and whole memory-graph exec update remain unsupported.

A host sink runs in the client after real GPU work and transfer retirement.
The existing process serialization remains. GPU/host/GPU alternation, multiple
host nodes, disconnected host branches and callback CUDA calls are not claimed.

CoreX graph upload of memory alloc nodes activates backing prematurely and
causes the next allocation launch to fail. The native free repair is measured;
remote memory upload is bounded to inactive unpaired reservations and completed
streams. Paired allocation/free upload is rejected before native submission.
The measured native used counter persists after free/destruction until trim;
native and wire EOF checks verify zero after trim.
Global/Relaxed capture initiation and multistream capture remain unsupported.
Interaction-mode support has actual allocation effects but a conservative unsafe
call matrix. DOT accepts default flags and writes only on the client.

User-object native calls succeed, with no destructor after source destruction
or while clone/exec refs remain. The destructor runs after the last owner, and
context synchronization returns while a deliberately blocking destructor runs.
The existing client host worker gates Runtime calls and skips host functions on
errors; it cannot be reused unchanged for NoDestructorSync. Native reference
release has no client notification, including failures and retained native exec
mapping owners. A separate asynchronous release notification/non-gated destructor
lifecycle must be established. The three blocked rows are valid findings and
contribute zero. This audit uses completion condition 1, not an architecture
exhaustion claim for condition 2.

## Final validation records

- build.stdout.log: build, ABI/export surface, bounded protocol/golden fixtures,
  generated schema contracts and backend seam pass.
- schema.stdout.log and registry.stdout.log: schema determinism/invalid-input
  rejection and duplicate-handler rejection pass. These scripts overwrite the
  generic result/manifest files; the batch closeout restores them below.
- native-expansion, native-child-execution, native-memory,
  native-runtime-expansion and native-remaining stdout logs: grouped CoreX
  measurements, actual GPU data/replay, snapshot and callback/lifetime limits.
  native-memory's isolated early native ExecDestroy negative probe reports
  InvalidValue and an expected backend unreleased-backing warning; the public
  path retains the mapping owner until external free.
- integration, expansion-integration, memory-integration, host-integration,
  control-integration and protocol-integration stdout logs: real independent
  public/socket clients, all PASS. Direct-wire EOF cleanup plus trim reaches
  zero native graph used memory.
- lifecycle and session-isolation stdout logs: PASS for changed resource
  ownership; targeted host/TLS tests cover the new concurrency behavior.

Scope selection is Graph family plus capture-named APIs,
cudaThreadExchangeStreamCaptureMode and cudaUserObject APIs, excluding
cudaGraphics*. User-object create/release are in Other Runtime and capture
query/update/mode APIs also occur in Stream; family name alone is not the scope.
BASELINE_COMPARISON.txt verifies every frozen row against BASE_HEAD, counts only
previously unsupported-to-supported status transitions, verifies all 28 initially
required rows terminal, and verifies the excluded Graphics rows unchanged.
