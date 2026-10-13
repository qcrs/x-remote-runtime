# Real-workload qualification audit

Result: **FULL PASS** for the selected applications and documented Runtime bounds.
BASE_HEAD: `2e8528ffc29452c8783934d837e572b58095732e`, initially clean.
Qualified implementation: `e225f3f311875b06016ea5ab0278eb36ef10696d`.
Later commits change reports/evidence only; finalization verifies that comparison.

| Completion requirement | Authoritative evidence / conclusion |
| --- | --- |
| 1. Dedicated reproducible suite | `tests/workloads/README.md`, twelve `.cu` files, shared public-header helper, `scripts/build-workloads.py`; `build-qualified/COMMANDS.txt`, `WORKLOAD_BUILD_SUMMARY.txt` |
| 2. Approximately 8–12 compound applications | Twelve separately compiled applications, matrix in `docs/REAL_WORKLOAD_COMPATIBILITY.md`; per-application sources contain numerical/order assertions |
| 3. Substantial transparent compiler application | `numerical_pipeline.cu`: 4096 values, affine and combine normal launches. `compiler-established/` and `compiler-remote/` established this before remaining applications; final `build-qualified/numerical_pipeline-compiler-abi.stdout.log`, `qualification/numerical_pipeline.{stdout,stderr}.log` prove registration, ABI arguments, launch and data |
| 4. Major application behaviors | `qualification/{shared_reduction,pitched_image,volume_transform,stream_pipeline,pool_pipeline,explicit_graph,capture_pipeline,symbol_pipeline,mixed_iterations}.stdout.log`; all PASS. Two-stage shared reduction, real 2D/3D transforms/padding, six stream/pool rounds, five graph/capture/symbol rounds, twelve mixed rounds with two replays each |
| 5. Graph memory and host interaction | `qualification/{graph_memory,host_interaction}.stdout.log`: four native allocation/fill/kernel/copy/free replays after source graph destruction; stale reservation after exec destruction. Callback consumes retired D2H, produces next H2D input, subsequent kernel verifies callback order; graph host sink runs in client and preceding graph kernel data is checked |
| 6. Actual deterministic validation | All sources compare all output elements, including 2D/3D padding, exact reduction, changing graph parameters, deferred D2H and callback state; `WORKLOAD_RUN_SUMMARY.txt` confirms successful termination only after these checks |
| 7. Qualified actual remote topology | `qualification/ENVIRONMENT.txt`, topology address/route logs, each application's CLIENT_PID/CLIENT_NETNS/ENDPOINT. Twelve distinct client PIDs in net:[4026542023], server PID 2446349 in net:[4026541968], initial host net:[4026531840]. Veth/TCP 198.18.0.2 → 198.18.0.1:50991. Server log identifies real Iluvatar MR-V100 |
| 8. Real-call provenance | `SOURCE_AUDIT.txt`; all `build-qualified/*-{architecture,dependencies,resolved-libraries,compiler-abi}.stdout.log`. All public remote-library dependencies, no libcudart/Driver linkage. Numerical LD_DEBUG bindings resolve compiler hooks/config/launch/copies/allocation to the actual remote SDK library. Server receives registered modules, validates kernel ABI, executes copies/kernels and cleans all sessions |
| 9. Continuing issue ledger | `docs/REMOTE_CORRECTNESS_ISSUES.md`: RC-005 and RC-006 continue RC-001–004; no parallel numbering |
| 10. Terminal findings | RC-005 FIXED TEST_INFRA_BUG (macro shadowing); RC-006 FIXED TEST_INFRA_BUG (incorrect success check after expected stale-pointer error). Both fixed by e225f3f; no newly discovered Runtime defect or unexplained OPEN issue. Existing RC-003 topology limit applies |
| 11. Focused fixes and regression | RC-005 original `compiler-first/numerical_pipeline-compile.stderr.log`, fixed compiler/remote runs and full shared-helper suite. RC-006 original `run-initial/graph_memory.stderr.log`, fixed `rc006-fixed-build/`, `rc006-fixed-run/` graph-memory/mixed execution, final full suite. No Runtime infrastructure changed, so historical integration suites were unnecessary |
| 12. Required static gates | `build.stdout.log`: protocol golden/bounded decode/HELLO, generated codecs, public exports, ABI/scope/layout and build PASS. `schema.stdout.log` plus `static/schema/`: determinism/rejection/contracts PASS. `registry.stdout.log` plus `static/registry/`: deliberate duplicate handler rejected. `backend.stdout.log` plus `static/backend/`: zero direct server Driver calls, 130 wrappers |
| 13. Accurate docs | `docs/REAL_WORKLOAD_COMPATIBILITY.md`, `docs/PROJECT_STATE.md`, workload README and issue ledger record actual results and boundaries. Main README unchanged |
| 14. Committed implementation alignment | Final run ENVIRONMENT records e225f3f. MANIFEST hashes current canonical Runtime/server/public ABI/schema/packaging/workload/runners/docs and all evidence; finalization verifies code diff from qualified commit is empty |
| 15. Committed / clean | Finalization and post-commit git status / manifest audit prove all implementation, docs and evidence committed; worktree clean |

The source → public compiler/Runtime ABI → libcorex_remote_cudart → CRX9 →
runtime_server → CoreX Driver → MR-V100 path is qualified. Every workload uses
compiler-generated registration; ten use immediate normal launch syntax and
two use those naturally registered kernels as explicit Graph nodes. No controlled
module substitute, test-only transport, fabricated IDs or application Driver
calls were used. Existing integration tests do not count as workload evidence.

The build/static checks preceded the final committed run; their inputs match the
qualified implementation. Runtime, server, protocol, schema, API ledger and ABI
implementation are unchanged from BASE_HEAD. Only workload sources, runners and
reports changed. Required static gates still ran, without a broad historical suite.

Earlier logs are exploration/issue evidence, not substitutes for the authoritative
`qualification/` run. Expected `REGISTER_VAR ... result=0` diagnostics and the
numerical LD_DEBUG trace appear in per-application stderr; top-level qualification
stderr is empty. The original failures remain visible despite later passes.

Bounds: same-host emulation, aarch64/compatible host ABI, RC-003 rootless server
namespace variant. Initial-host server mode and physical two-host deployment
are not validated here. Existing bounded capture, host tail/staging, pointer
layout, pool and Graph memory limits remain authoritative. No claim that all
CUDA sources/applications work; no performance, scaling, netem or cross-ISA work.
All twelve server sessions report SESSION_CLEANUP_EMPTY=YES and reset result zero.
