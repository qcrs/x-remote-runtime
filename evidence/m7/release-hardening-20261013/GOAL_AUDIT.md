# Release hardening audit

BASE_HEAD: `97907fe70a09fed21f1a8d28d52cb92acdb8fd0c`; initial worktree clean.
Date: 2026-10-13. Result: engineering prototype release qualification PASS within
[RELEASE_READINESS](../../../docs/RELEASE_READINESS.md)'s deployment/model boundary.
Canonical source identities are in `MANIFEST.sha256`; evidence identities are in
`EVIDENCE.sha256`. The final commit contains this audit and the hashed sources;
commit/worktree verification is performed after committing, avoiding a self-referential hash.

## Entrypoints inspected before editing

| Area | Competing baseline entrypoints | Canonical release choice |
| --- | --- | --- |
| Build | scripts/build.sh; scripts/build-sdk.sh | build-sdk.sh, which composes build.sh and install-sdk.sh |
| SDK/package/install | install-sdk.sh; no SDK archive runner; historical Gate 8 package records | installed prefix, verify-sdk.py, thin package-sdk.sh; archive extracts into a dedicated prefix |
| External consumer | build-consumer.sh defaults to repository clean-consumer; run-clean-consumer.sh requires historical server log counters | build-consumer.sh with external PREFIX/WORK; run WORK/app against installed server |
| Server | dist/bin/runtime_server; dev/local-remote/start-server.sh; installed corex-remote-runtime-server | PREFIX/bin/corex-remote-runtime-server |
| Client configuration | default loopback; environment variables; local helper | explicit numeric IPv4 COREX_REMOTE_HOST / COREX_REMOTE_PORT |
| Correctness | run-integration.sh; many capability-family runners; run-remote-correctness.py | run-remote-correctness.py --out; independent client/server over isolated namespace/veth TCP |
| Real applications | build-workloads.py; remote runner workload mode | build-workloads.py, then run-remote-correctness.py --workloads all |
| Static qualification | build checks; individual schema/registry/backend scripts | build-sdk.sh plus qualify-static.sh composing existing gates |

These choices were listed before editing. Existing entrypoints and historical
records remain available. No compatibility expansion, workload addition,
performance measurement, cross-ISA work or Runtime subsystem redesign occurred.

## Release findings and fixes

- Installation removed the entire custom prefix. It now creates/updates SDK files,
  preserves unrelated contents and permits reinstall. `install-preservation.txt`
  verifies a sentinel survived reinstall; SDK symlink/content checks pass.
- Absolute pkg-config prefix made archive relocation ambiguous. Metadata now derives
  prefix from pcfiledir. Both original and extracted packages pass actual pkg-config
  resolution. This host lacks pkg-config; the Ubuntu arm64 package was downloaded
  and extracted under /tmp solely for tool-mode validation. Existing explicit-prefix
  consumer fallback also builds/runs. No host system package was installed.
- ABI-SURFACE incorrectly placed later public symbols under Internal and misassigned
  some export versions. The document now follows the existing version map exactly;
  the map and expected export set are unchanged. SDK verifier compares ELF functions,
  expected exports, map versions and document versions. An initial document rewrite
  missed dotted version names; API-contract gate rejected it before compilation.
  Corrected rewrite passes all ABI/contracts checks; first failure logs are retained.
- Consumer helper ignored SDK Cflags and deleted its work directory. It now uses
  installed Cflags, preserves other files, normalizes prefix/work paths and checks
  native Driver dependencies as well as libcudart. Paths without spaces are the
  documented flag-parsing boundary.
- README mixed build/install/dev/integration entrypoints and omitted canonical
  workload qualification. It now owns practical instructions. Current architecture
  records actual RuntimeContext, ServerSession, backend and ID/lifetime behavior.
  PROJECT_STATE is concise; its prior content is preserved verbatim in HISTORY.
- Evidence verifier now checks README plus relative documentation links as well as
  evidence references. Local helper listener message/docs reflect configured bind.
- First external validation harness stopped the server immediately after client
  exit, before final EOF cleanup flushed; all consumers already passed. The harness
  now waits for three successful context resets before termination. Final empty
  session evidence passes. This was release harness timing, not a Runtime defect.

Raw loader/stdout logs retain their original whitespace; the source/document
whitespace gate excludes raw evidence logs.

No new Runtime/protocol/lifetime defect was found; RC-001 through RC-007 remain
unchanged and the next such finding would be RC-008. Packaging/doc findings stay
in this audit.

## Requirement-by-requirement evidence

| Requirement | Authoritative result/artifacts |
| --- | --- |
| Start at clean HEAD; record baseline; read required docs/ledger | ENVIRONMENT.txt; BASE_HEAD above; initial git status empty; authoritative ledger read/count and unchanged diff |
| One clear clean checkout/build/install/server/client path | README.md commands; scripts/build-sdk.sh and component reuse; final-build.stdout.log/stderr.log |
| SDK/package public boundary | sdk*.stdout.log; package.stdout.log; package-contents.txt; public extension header compiles using vendor public CUDA headers |
| SONAME/symlinks/export versions/install prefix | sdk*.stdout.log; installed ELF matches unchanged map/expected exports and corrected ABI document; install-preservation.txt |
| No source/build-tree or native client dependency | consumer-isolation.txt; consumer-isolated-build logs; app dynamic metadata; loader logs bind only installed DSO; original build/dist/install paths temporarily unavailable |
| Clean outside-repository consumer build/run and compiler transparency | consumer, consumer-pkgconfig, consumer-isolated stdout/stderr logs; three exact numerical PASS results; consumer-server log has three launches and three empty/reset sessions |
| Current architecture and every named identity/lifetime | docs/ARCHITECTURE.md; links to actual context, protocol, client, ServerSession/backend code; detailed Graph semantics retained separately |
| Derived compatibility summary; ledger authoritative | docs/RELEASE_READINESS.md; API-COVERAGE.txt; 276 total / 266 public, all status counts including 35 uninvestigated declarations; no percentage/full-CUDA claim |
| Canonical static/build/ABI/protocol/schema/registry/backend checks | build and final-build logs; static.stdout.log; static/schema, static/registry, static/backend result files; final links log |
| Canonical independent-process remote correctness | correctness.stdout.log REMOTE_CORRECTNESS=PASS; correctness/ENVIRONMENT.txt topology and real GPU server logs; numerical/ordering/failure/protocol family logs |
| Twelve real applications over transport | workload-build provenance; workloads.stdout.log REAL_WORKLOADS=PASS; twelve named app logs; qualification-audit.txt verifies exactly twelve empty sessions/context resets and actual remote bindings |
| Current readiness, known limitations and documentation roles | docs/RELEASE_READINESS.md; docs/PROJECT_STATE.md; README; updated architecture; historical docs/evidence retained |
| Documentation/evidence links and evidence policy | documentation-links.stdout.log; MANIFEST.sha256; EVIDENCE.sha256; no build trees, binaries, generated sources/headers copied into this evidence root |
| Scope discipline and final artifact correspondence | qualification-audit.txt and final-artifact-identity.txt; ledger, src, server, include, schema and tests byte-identical to BASE_HEAD |
| Clean committed worktree | final git commit and git status --porcelain audit after commit; reported final HEAD |

## Qualification scope and limitations

All execution uses separate client/server processes and CRX9/TCP to real CoreX.
Correctness and twelve applications use same-host rootless server/client network
namespaces and non-loopback veth (accepted RC-003), without relocating GPU devices
or mounting alternate filesystems. External package consumers use independent
localhost processes. No in-process transport shortcut supports a remote claim.

The release is READY as an engineering prototype within aarch64/CoreX 4.4/MR-V100,
matching host ABI, one server session at a time and the authoritative partial API
bounds. It is not production, PyTorch/arbitrary CUDA, performance, cross-ISA or
physical two-host qualification. IPC, peer/multidevice, graphics/external interop,
User Objects and pool sharing were not changed. Supported-row counts are not full
CUDA compatibility. Namespace privileges, vendor headers/compiler and server
Driver path remain explicit prerequisites, not hidden source-tree dependencies.
