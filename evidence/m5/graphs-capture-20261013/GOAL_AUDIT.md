# Graph/Capture goal audit

Objective source: the user-provided goal-objective.md attachment. Baseline is
clean tracked HEAD `154dde1`. This batch closes at objective condition 4:
40 newly supported APIs, with explicit instantiate/launch and practical capture
both complete. It does not claim exhaustion of all CUDA graph APIs.

| Requirement | Authoritative evidence |
| --- | --- |
| Read rules, project state, ledger; inspect ownership/census | AGENTS.md; current ledger; RuntimeContext/ServerSession integration in the changed source |
| Establish native ground truth in grouped probes | driver-probe, runtime-probe and execution-probe logs; their canonical tests/probes sources |
| Strongest measured architecture; no fabricated handles/no-op support | Native CoreX graph/capture backend seam; native graph data, repeat-launch, capture deferral/replay results; independent public integration |
| Coherent graph/node DAG lifecycle and inspection/mutation | graphs.c construction, roots/edges/dependencies, cycle/duplicate rejection, node destruction, clone/correspondence and enable/disable cases |
| Instantiation, independent executable lifetime, repeated launch | Graph mutation/destruction preserves executable data; cross-stream repeats; empty capture/graph and reset cases in integration log |
| Proven Runtime node reuse | Kernel launch shared codecs/metadata; allocation/3D helpers; composed memset/kernel/device copy results; native and public disjoint rows in one allocation; event node host-frontier retirement |
| Parameter and update operations | Graph/exec setters and getters; old snapshot isolation; native same-graph update; topology failure normalized from measured 1000/result 2 to Runtime 910 |
| Capture beyond explicit graph checkpoint | Native begin/end and captured submissions; state/info/v2 queries; unchanged data before replay; repeated replay; wrong-thread end and unsupported-operation abort |
| No native pointers/handles cross CRX9; generation/session ownership | Generated typed graph envelope; client tombstones; server ID-to-owner validation; stale allocation/module/event rejection; malformed request tests |
| Cleanup and lifetime/order interactions | Reset, source graph destruction, cross-stream exec frontier, event visibility, EOF during capture, lifecycle and session-isolation logs |
| Approximately 25–40 new supported APIs | SELECTED_APIS.csv and authoritative ledger: 40 total, 22 IMPLEMENTED, 18 PARTIAL_IMPLEMENTED; no unsupported/blocked APIs counted |
| Every investigated Graph/Capture API terminal | All 40 selected ledger rows terminal; native probes' related public graph/capture primitives are covered by those rows; no selected GROUND_TRUTH_REQUIRED |
| Schema/codegen for bounded RPC plumbing; semantics handwritten | OP_GRAPH=88 generated request/response codecs, metadata, registry and typed call; deterministic schema/rejection check logs; graph/capture handwritten includes |
| Independent client/socket/CRX9/server/CoreX validation | run-graphs.py Popen topology; ENVIRONMENT.txt server PID/port; public integration and direct protocol logs |
| Justified regressions and final build/ABI/protocol checks | Build/golden/ABI/scope logs; schema/registry/backend logs; lifecycle/isolation for cleanup, numerical baseline for shared launch decoder; unchanged mutex needs no historical multithread rerun |
| Ledger/docs/evidence aligned | compat/cuda-api-ledger.csv, README.md, PROJECT_STATE.md, GRAPH_CAPTURE_MODEL.md, manifest and results |
| No IPC, Peer/Multi-device or External Interop expansion | Selected APIs are Graph/Capture only; changes outside that family share launch/cleanup/capture guards, not new compatibility surfaces |
| Committed changes and clean worktree | Final checkpoint commit and post-commit git status verification, recorded in final answer |

Limits are explicit: device endpoints; registered kernelParams; zero instantiate
flags; limited instantiate diagnostics; 64-node graphs and 64-edge mutation
batches; finite monotonic client token budgets; original-graph whole-exec update;
single-stream owner-thread ThreadLocal capture. Host/staging, child graphs,
multi-stream capture and other uninvestigated node families are not claimed.

The manifest binds source and logs. Validation results are asserted by the
native/public/protocol family tests and the recorded command exits, rather than
by API export counts alone.
