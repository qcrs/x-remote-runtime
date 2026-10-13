# M9 evidence

BASE_HEAD and clean initial worktree are recorded in `BASE_HEAD.txt`.
`baseline-inventory.json` freezes exactly the 75 public partial rows from that
revision. `partial-review.csv` records every row's initial classification,
completed practical semantics, remaining technical reason and evidence/source.
The private compiler registration partial is excluded from public totals.

Run `scripts/build.sh`, `scripts/run-partial-semantics.py` and
`OUT=evidence/m9/partial-semantics-20261013/static scripts/qualify-static.sh`.
The batch uses grouped native Driver/Runtime probes followed by independent
public clients -> localhost TCP -> CRX9/V3 -> runtime_server -> CoreX.
Commands are recorded in `COMMANDS.txt`; binaries and generated sources stay in
ignored build directories. Native probe success markers mean the probe finished;
individual operation results, readback and data establish actual capabilities.

- `native.stdout.log`: stream policy failures, ordinary extended launch data,
  invalid launch attribute acceptance, missing completion-event effects and
  direct cooperative failure. JIT option acceptance on compiled code does not
  establish PTX/JIT semantics.
- `library-{elf,fatbin,ptx}-native.stdout.log` and
  `library-runtime-native.stdout.log`: compiled input formats, preserved-binary
  hints, enumeration and native invalid-image/null-output results.
- `driver-query-native.stdout.log` and `driver-entries-native.stdout.log`:
  query flag semantics and effects of memory/stream/event primitives.
- `function-native.stdout.log`: available fields, carveout and occupancy limits.
- `*-remote.stdout.log`: public entry bridge, library inputs/enumeration/lifetime,
  context flags and existing bounded launch/callback behavior.
- `library-protocol.stdout.log` and `context-transfer-protocol.stdout.log`:
  malformed/stale IDs, EOF ownership and transfer-only context protection.
- `workloads/build/` and `workloads/run/`: affected explicit graph, capture,
  numerical and symbol applications in the existing namespace/veth topology.
- `build.stdout.log`, `static.stdout.log` and `final-*.stdout.log`: ABI, protocol,
  deterministic schema, handler registry, backend seam, contracts and links.
- `FINAL_AUDIT.json` and `audit.stdout.log`: baseline/current reconciliation,
  classification coverage, completed gap evidence and PARTIAL delta by family.
- `COMMIT.txt`: implementation revision; subsequent provenance commit records it.

The two `baseline-*-client/query` observations ran against the unchanged
pre-M9 DSO/server before rebuilding: non-stream PTDS queries were rejected, and
flags changed with a live graph. RC-011's pre-change null-output behavior is
already observed in `evidence/m4/library-expansion-20261012/integration.stdout.log`.
Native multi-kernel CUlibrary unload hangs were established in that earlier
batch's `native-unload-observations.txt`; this settled limitation is referenced
without repeating the hang. Valid native module unload and remote drain/stale/
EOF behavior are measured in this batch.

Completion check: `python3 scripts/audit-partial-semantics.py --require-clean`.
All five reviewed practical implementation gaps are addressed; all 75 rows retain
explicit backend, remote ownership, release scope or bounded-model reasons.
No status upgrade is justified. All families have PARTIAL delta zero.
