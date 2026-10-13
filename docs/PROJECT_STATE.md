# Project state

M9 partial semantics completion starts from clean baseline
`352a3f8360eb824a7a30639dac815caf6dff2e20` on 2026-10-13. All 75 baseline public
partial APIs have an individual reason classification. Five practical gaps are
completed: broader Runtime-backed Driver entry queries and non-stream PTDS flags,
library fatbin/data/file inputs and preserved-binary hints, native enumeration
output validation, and context-flag protection for graph/exec/transfer owners.
RC-009 through RC-011 record the three actual Runtime/lifetime defects.
Remaining full-API boundaries prevent any truthful whole-row upgrade: totals
remain 107 implemented, 75 partial, 63 blocked, 13 backend unsupported and eight
out of scope. See [PARTIAL_SEMANTICS_COMPLETION](PARTIAL_SEMANTICS_COMPLETION.md)
and `evidence/m9/partial-semantics-20261013/` for the audit and targeted native,
independent public CRX9, static and affected-workload validation.

CUDA Runtime compatibility closure is complete from clean baseline
`1f9502b05d12f46df21c95cd0aac78a88dd871e1` on 2026-10-13. The 35 previously required
public APIs now have terminal classifications; all 77 baseline partial APIs have
an individual review. Public totals are 107 implemented, 75 partial, 63 blocked,
13 backend unsupported and eight explicitly out of scope. Practical changes add
the single-device self-peer query, full installed error names/descriptions,
unaligned host registration with shared-page lock ownership, and native function
cache/dynamic-shared-memory queries. The remaining carveout query is unsupported.
See [API_COMPATIBILITY_CLOSURE](API_COMPATIBILITY_CLOSURE.md) and
`evidence/m8/compatibility-closure-20261013/` for the final baseline audit.
RC-008 records the corrected error-description defect. Build/static gates,
targeted independent public remote clients and the two affected real workloads
pass; unchanged historical suites were not repeated.

Engineering prototype release hardening is complete from clean baseline
`97907fe70a09fed21f1a8d28d52cb92acdb8fd0c` on 2026-10-13. API coverage and Runtime
semantics are unchanged. The release entrypoint is [README](../README.md);
readiness, qualification results and limitations are in
[RELEASE_READINESS](RELEASE_READINESS.md).

Current boundary: aarch64 client -> CRX9/TCP -> aarch64 server -> CoreX 4.4 /
MR-V100. The canonical remote correctness qualification and all twelve real
applications passed again, alongside installed/relocated SDK and external consumer
validation. Qualification is same-host
namespace/veth emulation using the accepted rootless server namespace variant
(RC-003), with independent clients/server and real GPU work. No physical two-host,
cross-ISA, performance or production-readiness claim is made.

Major capabilities and resource ownership are summarized in
[ARCHITECTURE](ARCHITECTURE.md). Per-API support remains solely authoritative in
[the compatibility ledger](../compat/cuda-api-ledger.csv). Detailed Graph bounds
remain in [GRAPH_CAPTURE_MODEL](GRAPH_CAPTURE_MODEL.md); qualification scopes are
[REMOTE_CORRECTNESS_MATRIX](REMOTE_CORRECTNESS_MATRIX.md) and
[REAL_WORKLOAD_COMPATIBILITY](REAL_WORKLOAD_COMPATIBILITY.md). Issues RC-001 through
RC-011 remain recorded in [REMOTE_CORRECTNESS_ISSUES](REMOTE_CORRECTNESS_ISSUES.md).

Release evidence: `evidence/m7/release-hardening-20261013/`.
Earlier milestones and their evidence are preserved in [HISTORY](HISTORY.md).
