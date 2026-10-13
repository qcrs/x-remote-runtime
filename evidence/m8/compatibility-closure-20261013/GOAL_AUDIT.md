# Compatibility closure completion audit

BASE_HEAD: `1f9502b05d12f46df21c95cd0aac78a88dd871e1`, initially clean.
The baseline inventory is checked against `git show BASE_HEAD:compat/cuda-api-ledger.csv`,
not inferred from current counts. Implementation and all evidence belong to the
closure implementation commit; final commit metadata is recorded in COMMIT.txt.

| Requirement | Authoritative evidence and result |
| --- | --- |
| Read objective and starting architecture/status/ledger documents; record baseline | BASE_HEAD.txt, ENVIRONMENT.txt and baseline-inventory.json; clean starting HEAD captured before edits. |
| Eliminate all required public declarations | FINAL_LEDGER_COMPARISON.json and terminal-review.csv enumerate all 35 original names: one IMPLEMENTED, 28 GROUND_TRUTH_BLOCKED, six BACKEND_UNSUPPORTED; zero required remain among 266 public APIs. |
| Review all 77 baseline partial APIs | partial-review.csv has exactly the original 77 names, individual limitations, source paths and decisions; audit script checks current status/note agreement. |
| Implement practical missing semantics | Unaligned/disjoint shared-page host registration and last-owner unlock; real cache-mode/dynamic-shared attributes via generated opcode 92; all 112 installed public error names and native descriptions; self-peer query through existing Runtime initialization. Public and native logs prove these behaviors. |
| Upgrade complete behavior without fake semantics | cudaChooseDevice and cudaGetErrorName become IMPLEMENTED. cudaFuncGetAttributes and cudaHostRegister retain remaining limits. No advanced Graph API is forced complete. |
| Review prior 35 blocked/seven unsupported rows | terminal-review.csv references inspected native operation results or explicit remote ownership/prerequisite limits. Prior proof paths in PRIOR_EVIDENCE.txt are hashed, not copied. |
| Group peer/IPC/legacy/graphics/external/cooperative investigations | peer-ipc.stdout.log proves one device, no self peer, cooperative-multi attribute zero and actual two-process memory/event sharing. Legacy logs plus compiler registration symbols prove native 801/31 on real references. Array/managed probes prove failing prerequisites. Interop environment records absent SDK/exporter/registration entrypoints; fabricated resources/FDs were not used. |
| Preserve architecture and protocol boundaries | Source diff uses RuntimeContext, existing host/fake-VA/module/kernel/transfer registries and backend seam. Function DTO carries KernelID/scalars, with capability 10 negotiation and the original opcode 37 preserved. Golden codecs, generated contracts, malformed lengths and stale owners pass. |
| Record only actual new Runtime defects | RC-008 records enum-name-as-description behavior; ordinary compatibility bounds and known backend limitations receive no new issue IDs. |
| Proportionate native/public/real-workload validation | Grouped native prerequisites and function queries; independent localhost public clients and direct-wire tests; host_interaction and numerical_pipeline through namespace/veth, real GPU work and remote DSO binding. Build/static ABI, protocol, deterministic schema, duplicate handler rejection and backend seam gates pass. |
| Authoritative ledger, concise boundary summary, one evidence root | compat/cuda-api-ledger.csv, docs/API_COMPATIBILITY_CLOSURE.md, PROJECT_STATE and RELEASE_READINESS agree with API-COVERAGE.txt and final JSON comparison. This root contains logs/audits/hashes, no binaries, build trees or generated source. |
| Commit implementation/evidence; clean final worktree | COMMIT.txt identifies the implementation commit. Final commit/tree cleanliness is checked after the audit commit; no unrelated pre-existing work was present. |

Baseline → final public counts: IMPLEMENTED 104 → 107; PARTIAL_IMPLEMENTED
77 → 75; REQUIRED 35 → 0; BLOCKED 35 → 63; BACKEND_UNSUPPORTED 7 → 13;
OUT_OF_SCOPE_CURRENT stays 8. The new supported API is cudaDeviceCanAccessPeer.
Existing supported APIs were not downgraded. See FINAL_LEDGER_COMPARISON.json for
the exact 35 name-to-terminal mappings and the two upgrades.

Remaining partial counts by ledger family: Graph 32, Other Runtime 12, Stream 8,
Host Memory 7, Memory Pool / Async Allocation 6, Memcpy 4, Execution 3, Device 2,
Function 1. The one private partial compiler hook does not count toward the public
75. Main retained partial reasons are explicit bounded Graph/Capture/library ABI,
cross-process host mapping/callback scheduling, unavailable native controls and
measured pool accounting/reuse behavior.

Remaining blocked: 63, mainly failed array/managed/texture prerequisites,
single-device peer/cooperative qualification, absent graphics/external/NvSci
owners, and IPC/user-destructor/shared-address ownership across sessions/processes.
Remaining backend unsupported: 13, all backed by actual native CoreX operations
(801 or obsolete Runtime 31), including the six legacy reference APIs.
The final JSON includes blocked/unsupported counts by ledger family.

Validation scope is this aarch64/CoreX 4.4/MR-V100 engineering prototype. Current
evidence does not qualify physical two-host deployment, general Driver contexts,
multiple GPUs, arbitrary launch attributes or full Graph/host-memory semantics.
Unchanged historical multithread/reconnect/lifecycle suites were not rerun.

Reproduce after build with `python3 scripts/run-compatibility-closure.py --phase required`,
`--phase core`, then `--phase remote`; invoke the audit script afterward. Other
exact build/static/workload commands and nested commands are recorded in COMMANDS.txt.
