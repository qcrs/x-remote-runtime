# Core Runtime and Memory Pool completion revalidation

Inspected clean HEAD: `6d30df8`. Implementation baseline: `cf5f8a8`; core
checkpoint: `d2e8c7a`; pool closeout: `154dde1`. This continuation preserves the
original objective and validates the existing implementation. The later
Graph/Capture commit belongs to the separate goal recorded in its own audit;
this continuation changes only completion evidence and PROJECT_STATE.

The current 276-row census is classified in `SCOPE_AUDIT.csv`. Scope includes
Device, Device/Memory, Device/Sync, Stream, Stream/Event, Event, Execution,
Memcpy, Linear Memory, Host Memory, Function, Other Runtime and Memory Pool /
Async Allocation. Error, Version, Occupancy, 2D Memory and 3D Memory are also
included so header census labels cannot hide practical core work. Name-based
exclusions cover Graph/Capture/UserObject, Ipc, Peer/P2P/MultiDevice, NvSci,
External and MemPool Export/Import APIs. Other families remain outside scope.

There are **139 eligible records**: 75 IMPLEMENTED, 41 PARTIAL_IMPLEMENTED,
five BACKEND_UNSUPPORTED and 18 GROUND_TRUTH_BLOCKED. No eligible API remains
GROUND_TRUTH_REQUIRED or DEPRECATED. This extends the older 126-record audit
with 13 already implemented APIs in adjacent census families; it does not
claim 13 new implementations. All 116 supported eligible APIs appear in the
expected public exports, and the rebuilt library passes the actual ABI check.
The 21 newly supported public APIs relative to `cf5f8a8` exactly match the
original pool closeout delta. Unsupported and blocked APIs are excluded from
that count.

| Original requirement | Current evidence and conclusion |
| --- | --- |
| Exhaust practical core APIs before pool work | Current ledger and full census audit have no eligible unprocessed entries. The eight additional core APIs and 13 pool APIs are committed in the core/pool checkpoints. `core-integration.stdout.log` checks reset, flags, legacy configuration, extended launch, async D2D and both callback forms through the public library. Earlier library, symbol, memory and execution family evidence remains linked by the ledger. |
| Implement or classify cudaMemPool*, device pool selection and all three async allocation APIs | All 13 practical APIs are supported: seven implemented and six partial. Public `integration.stdout.log` checks selection, attributes/access, allocation/free, trim, native reuse limitations, zero sizes and invalid arguments. Four pool import/export APIs remain excluded as IPC. |
| Preserve pointer identity, session ownership and stream ordering | Source inspection confirms shared `allocate_linear_locked`, fake-VA and AllocationID tables, RuntimeContext pool generation/tombstones, ServerSession native pool ownership, existing StreamID/event frontiers and independent TransferID staging. Public checks cover cross-stream events, kernel/copy use, immediate free invalidation, deferred host visibility, pool destruction with live allocations, ordinary/async free interoperability and reset. |
| Prefer schema/codegen for repetitive RPCs | Schema pool opcodes 75–87 use generated bounded codecs, typed client calls and registry metadata. Ownership, pointer translation and ordering remain handwritten. Current rebuild regenerates the committed output without a diff and runs generated/golden protocol tests. Existing deterministic generation/rejection evidence is preserved. |
| Measured backend limits and blockers | The original Driver and native Runtime probe logs show actual return values and readbacks: native pools/async allocation succeed; reuse enables and permission changes do not take effect; zero-target trim requires the measured threshold/sync path; usage counters have backend granularity. Ledger notes identify unsupported cooperative launch, library kernel setters, arrays and managed memory, and blocked dependent/mapping/export/double/cache operations. These results are terminal classifications, never counted as implementations. |
| Independent public client → localhost → CRX9 → server → CoreX | Current pool and core checks use separately linked client processes and independently started Runtime Servers. COMMANDS and ENVIRONMENT record server PIDs/ports. Tests assert returned data, host visibility and identities; export presence alone is not support evidence. |
| Bounded protocol, capability and failure behavior | `protocol-integration.stdout.log` checks malformed pool DTOs, stale IDs, async free, native deferred pool destruction and EOF isolation against CoreX. `callback-errors.stdout.log` uses controlled negative peers for callback errors and legacy capability rejection; it supplies no positive backend claim. `entry-native.stdout.log` and `entry-integration.stdout.log` verify the six bounded Driver adapters natively and through Runtime RPCs. |
| Appropriate grouped validation and justified regressions | Current rebuild and targeted pool/core/entry/protocol checks pass. No Runtime ownership or locking code changed in this continuation. Previous lifecycle, session isolation, failure/reconnect and callback mutex multithread evidence is retained rather than rerun. Pool tests themselves exercise reset and EOF ownership. |
| Ledger, PROJECT_STATE, README and evidence aligned | The ledger and high-level README already accurately describe the implemented core/pool slice. PROJECT_STATE now links this broader current audit. The previous 62 evidence hashes match preserved artifacts, and 43 original source hashes match commit `154dde1`. Current source/artifact hashes are in MANIFEST.sha256. |
| Excluded families and evidence discipline | This continuation adds no Graph/Capture, IPC, peer/multidevice or external API behavior. Evidence contains only logs, hashes and audits; historical evidence remains untouched. |
| Committed changes and clean worktree | Implementation is already committed. This supplemental audit and documentation are committed after contracts, evidence links, manifest and whitespace checks; post-commit HEAD/status verification is required before marking the goal complete. |

All partial support retains the existing explicit limits: device-zero,
non-exportable pinned pools; granule-based counters; unsupported reuse enables
and permission changes; best-effort nonzero trim and a conservative context
barrier for zero-target trim. Existing callback serialization, launch-attribute,
host staging, library and symbol limits remain in the ledger. No broader CUDA
compatibility claim is inferred from this completion.
