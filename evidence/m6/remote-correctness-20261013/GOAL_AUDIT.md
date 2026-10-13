# Remote correctness completion audit

BASE_HEAD: `6538a582fa4ff2266d00c5b59af20316238ff018` (clean).
Implementation: `cbcbc2636c5d186a0b1c0e0238c9e79c5e36117d`.
Fixing commit for RC-001, RC-002, RC-004: `dc28edbb2758381707be0fbc5f70360f69c47b45`.
Evidence packaging changes only documentation/evidence. The source hashes in
`MANIFEST.sha256` pin the implementation and every reused scenario; no build
outputs or copied sources are stored here. Run manifest verification from the
repository root. Earlier failures are retained and excluded from the final PASS.

| Requirement | Authoritative evidence / result |
| --- | --- |
| 1: reproducible real remote-emulated path | `qualification/ENVIRONMENT.txt`, `topology-{server,client,route}.stdout.log`, `server.stdout.log`, `negotiation-trust.stdout.log`; repeat in `repeat-topology/`. Client PID/netns/local/peer address differ from server PID/netns; veth route is 198.18.0.2 → 198.18.0.1; server identifies MR-V100. PASS |
| 2: core queries/controls | `device-identity`, `device-controls`, `compat_expansion` stdout: public installed-header reconstruction, context setter/readback and deterministic unsupported limits. PASS |
| 2: linear/sync/async/2D/3D/fill/staging | `memory_layout`, `memory_3d`, `execution_fill`, `compat_expansion`, `memory_pools` stdout: H2D/D2D/D2H, pitches/positions/padding, byte fills, source reuse, deferred visibility, explicit/default streams, cross-stream event dependency, stale streams and staging release. PASS |
| 2: kernel/module/symbol lifetime | `symbols`, `symbols-live-exit`, `symbols-wire`, `graphs`, `memory_pools` stdout: compiler registration, launch, global/constant offsets, non-owning storage, unload/stale aliases, module-backed graph rejection. PASS |
| 2: Memory Pools and all three async allocation/free entries | `memory_pools`, `memory_pools-wire` stdout: native default/current/custom identity, allocation-from-pool, event ordering, kernel/copies, destroyed pool/live storage, ordinary/async free, host retirement, reset/default restoration. PASS |
| 2: Graph/Capture and memory nodes | `graphs`, `graph_expansion`, `graph_memory`, `graph_host`, `graph_controls`, `graphs-wire` stdout: composed kernel/memcpy/memset/event DAGs, repeated launches, independent exec snapshots, child graphs, bounded capture/replay/invalidation, alloc/use/free/reservation replay, source/exec destruction, host tails/upload. PASS |
| 3: EOF and ownership cleanup | `abrupt-client-exit` uses os._exit with pending D2H and live allocation/stream/event/module/graph/exec/pool. `after-abrupt-exit` proves fresh work; server log shows transfer retirement before allocation/module/pool cleanup and SESSION_CLEANUP_EMPTY/CONTEXT_RESET rc=0. Wire tests leave custom pools and active graph reservations; pool thresholds restore and graph trim reads zero. PASS |
| 3: server termination/reconnect/reset | `failure.stdout.log`, `failure.stderr.log`, `server-reconnected.*`: parent terminates real server with live objects; client detects failed generation without replay; fresh generation rejects AllocationID/StreamID/EventID/GraphID/GraphExecID/PoolID tokens; fresh tokens differ. Failed pending D2H keeps sentinel unchanged; reset retires D2H then invalidates identities; fresh numerical roundtrip passes. PASS |
| 3: protocol/trust/capabilities | `negotiation-trust`, `post-failure-trust`, six family `*-wire` logs: HELLO matches Driver version and required capability IDs; unsupported requests repeat deterministic errors; unknown opcode/malformed DTO preserves same connection; wrong version, oversized length, truncated header/payload close boundedly and next session works. PASS |
| 4–6: findings and targeted regressions | `docs/REMOTE_CORRECTNESS_ISSUES.md`: RC-001 FIXED, RC-002 FIXED, RC-003 ACCEPTED_LIMITATION, RC-004 FIXED. Before logs in `rc001-baseline/`, `rc004-before/`, `rc004-status-before/`; final configured/invalid bind, five endpoint clients and negotiation assertions cover fixes through veth. No unexplained OPEN issue. PASS |
| 7: build/public ABI/exports/protocol | `static/build.stdout.log`, `.stderr.log`: all public export/ABI/scope checks, golden bounded codec fixtures, generated codecs and G8C_BUILD pass. PASS |
| 7: schema/registry/backend | `static/schema/00-RESULTS.txt`, `static/registry/00-RESULTS.txt`, `static/backend/00-RESULTS.txt`: two generations match each other and checked-in files; contracts/rejection/bounded types pass; duplicate handler rejected; zero direct Driver calls outside seam (130 wrappers). PASS |
| 8–9: documentation/boundary | PROJECT_STATE records actual topology, result and same-architecture/compatible-host-ABI boundary; README adds a short boundary and listener configuration; ledger/Graph model retain existing limits. PASS |
| 10–11: committed evidence/hygiene | source and artifact SHA-256 manifest; packaging commit contains final docs/evidence only; post-commit git diff of implementation paths against IMPLEMENTATION_HEAD must be empty, full worktree clean and manifest verification successful. Recorded at finalization in `FINALIZATION.txt`. PASS |

All qualification log names above without a directory prefix are under
`qualification/` and have `.stdout.log` / `.stderr.log` suffixes. Per-command
exit success appears in `qualification.stdout.log`; final result is
REMOTE_CORRECTNESS=PASS. `precommit-pass/` retains the earlier full passing run,
with its pre-final orchestration version; it does not substitute for final logs.

The ownership-order assessment combines actual EOF/reset outcomes with the
hashed existing cleanup implementation: cleanup_session calls cleanup_graphs
first; graph cleanup completes native work and retires active graph backing,
then destroys exec/graph definitions before regular allocation/module owners;
explicit-stream/transfer staging retirement precedes event/stream/allocation
and module/pool release. No ownership mechanism was changed for this goal.
Wire IDs are scoped to ServerSession and may restart there; generation-tagged
public client tokens prevent replay/resolution after reconnect or reset.

RC-003 is an explicit environment qualification limit: the measured server
namespace is rootless, not the initial host namespace. It accesses the real
host CoreX devices without a device/mount namespace. The independent isolated
client, non-loopback veth/TCP, same aarch64 ABI and real GPU execution requirements
are met. Privileged initial-host-server mode is available but unvalidated.
This is not physical two-host validation, performance/scaling measurement,
cross-ISA support, exhaustive API validation or an expansion of ledger support.
Existing backend/design limitations were neither fabricated nor duplicated as
new issues. No new data/ordering/lifetime/protocol implementation defect was
observed in the tested representative scenarios.
