# Graph/Capture completion revalidation

Objective: the complete user-provided goal-objective.md, starting at `154dde1`.
Implementation is committed at `6d30df8`; this audit starts from clean `c24f07b`.
The original objective closes at condition 4: 40 supported additional APIs with
explicit graphs and practical capture implemented. No smaller scope was substituted.

| Requirement | Current evidence and result |
| --- | --- |
| Rules, project state, API census and ownership inspected | AGENTS.md, PROJECT_STATE.md, all 276 ledger rows; client graph/capture includes, server graph include and RuntimeContext/ServerSession integration inspected |
| Grouped CoreX ground truth before architectural choice | Original implementation probes and fresh driver/runtime/execution probe logs; real native creation, dependencies, instantiate, launch, update and capture all pass |
| Coherent explicit graphs beyond create/destroy | Fresh integration log: empty/kernel/memset/device-copy composition; DAG inspection/mutation; clone/correspondence; enable/disable; instantiate and repeated launch with numerical assertions |
| Independent graph and executable lifetime, parameters and updates | Fresh integration: original graph mutation/destruction preserves exec; graph/exec setters; successful original-graph update; rejected topology update retains executable data |
| Practical capture explored and implemented in same goal | Fresh integration: owner-thread ThreadLocal begin/end, status/info/v2, capture dependency query, deferred work, repeat replay, 3D copy capture, wrong-thread rejection, unsupported-operation invalidation and recovery |
| Native ownership, no client pointers/handles across CRX9 | Generated opcode 88 bounded codecs; handwritten AllocationID/KernelID/EventID translation and owner checks; native backend seam; fresh public stale allocation/module/event and reset assertions; fresh malformed/direct-wire stale-ID tests |
| Ordering and cleanup reuse | Exec frontiers and event frontier joins inspected; fresh cross-stream repeat launch and event retirement checks; direct-wire EOF during capture followed by usable independent session |
| Justified broader lifetime validation | Original lifecycle/session-isolation and numerical baseline logs match their manifest; implementation and test sources are byte-identical to `6d30df8`. Shared subsystems have not changed, so these suites were not rerun |
| 25–40 supported APIs, investigated rows terminal | ledger-manifest-audit.stdout.log: exact baseline/selected/current set equality; 40 changed statuses, 22 IMPLEMENTED and 18 PARTIAL_IMPLEMENTED; all selected rows terminal |
| Schema/codegen for bounded plumbing, handwritten semantics | Generated graph envelope/codecs/typed call and registry inspected; fresh deterministic generation, schema rejection and API/evidence contract checks pass |
| Independent public client → socket → CRX9/V3 → server → CoreX | run-graphs.py launches separate server and public client processes; COMMANDS.txt and ENVIRONMENT.txt retain server PID/port; fresh public and direct-protocol logs pass |
| Final build, ABI and protocol checks | build.stdout.log: build, exports, ABI 1.1, protocol golden, schema and memory-layout ABI checks pass; public SDK and internal graph ABI compiles also pass |
| Ledger, high-level README, project state, evidence aligned | Current ledger/README/model match implementation; PROJECT_STATE links this revalidation; new manifest binds current sources and new logs without copying canonical source or binaries |
| Excluded scope respected | Exact baseline ledger comparison changes only the 40 selected Graph/Capture APIs; no IPC, Peer/Multi-device or External Interop expansion |
| Committed changes and clean worktree | Implementation already committed; this revalidation is committed separately and post-commit clean status is checked before marking the goal complete |

The original 101-file manifest matches every file at implementation commit
`6d30df8`. Before this revalidation, its sole current-tree mismatch was
PROJECT_STATE.md: `c24f07b` updated the preceding Memory Pool goal's status.
Historical evidence and hashes remain unchanged. This directory's manifest
binds the current project state and the unchanged implementation and tests.

Measured limits remain documented in the ledger and GRAPH_CAPTURE_MODEL.md:
64-node graphs, bounded object/token tables, device-only copy endpoints,
registered kernelParams, zero instantiate flags, limited instantiate diagnostics,
original-graph exec update and one explicit owner-thread ThreadLocal capture
stream. Child graphs and broader capture modes are not claimed. The goal's
40-API condition permits closure without claiming all CUDA Graph APIs exhausted.
