# Project state

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
RC-007 remain recorded in [REMOTE_CORRECTNESS_ISSUES](REMOTE_CORRECTNESS_ISSUES.md).

Release evidence: `evidence/m7/release-hardening-20261013/`.
Earlier milestones and their evidence are preserved in [HISTORY](HISTORY.md).
