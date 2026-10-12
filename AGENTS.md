# x-remote-runtime Development Rules

- `compat/cuda-api-ledger.csv` is the authoritative API compatibility record.
  Any API actively processed in a compatibility expansion must end as
  IMPLEMENTED, PARTIAL_IMPLEMENTED, BACKEND_UNSUPPORTED, or
  GROUND_TRUTH_BLOCKED; do not leave investigated APIs as
  GROUND_TRUTH_REQUIRED.

- Prefer existing schema/codegen for repetitive scalar, enum, bounded DTO,
  object-ID, codec, registry, export, and simple RPC plumbing.
  Keep pointer ownership, resource lifetime, async ordering, staging,
  allocation semantics, and state machines handwritten.

- Reuse existing RuntimeContext, ServerSession, backend seam, fake-VA,
  object registries, stream/frontier, TransferID, and evidence machinery.
  Do not introduce parallel mechanisms without a concrete semantic need.

- Avoid speculative abstraction and excessive defensive programming.
  Add validation and fallback only for CUDA semantics, measured CoreX
  behavior, protocol trust boundaries, or bugs demonstrated by evidence.

- Test by capability family, not by individual API.
  Use only representative positive cases and necessary negative cases.
  Do not rerun historical multithread/reconnect/lifecycle suites unless the
  corresponding shared subsystem changed.

- Remote Runtime validation must use independent client and server processes:
  client -> localhost socket -> CRX9 -> server -> CoreX.
  Do not bypass transport when claiming Remote Runtime support.

- Use a small number of CoreX probes per capability family rather than one
  probe per API.

- Keep `README.md` high level: project purpose, architecture, major supported
  capabilities, build/run instructions, and links.
  Detailed support belongs in `compat/cuda-api-ledger.csv`;
  current development status belongs in `docs/PROJECT_STATE.md`.

- Store one evidence set per substantial compatibility batch, not per API.
  Do not copy build trees, binaries, or generated source into evidence.

- A blocked API does not block the batch. Record the limitation and continue.