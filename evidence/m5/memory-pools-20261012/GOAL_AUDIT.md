# Core Runtime and Memory Pool goal audit

Baseline: `cf5f8a8`. Core checkpoint: `d2e8c7a`. Hardware: CoreX 4.4.0,
MR-V100, aarch64. This closeout follows the authoritative compatibility ledger
and the repository's AGENTS.md rules.

## Scope and implementation result

`SCOPE_AUDIT.csv` enumerates the requested Device, Stream, Event, Execution,
Memcpy, Linear/Host Memory, Function and Other Runtime families, together with
Error and Memory Pool / Async Allocation. Name-based exclusions catch
Graph/Capture, IPC, Peer/Multi-device and External Interop declarations that
the header census places inside those families. Other API families are not
expanded by this goal.

There are 113 eligible core records and 13 eligible pool records. All 126 are
terminal: 62 IMPLEMENTED, 41 PARTIAL_IMPLEMENTED, five BACKEND_UNSUPPORTED and
18 GROUND_TRUTH_BLOCKED. None is GROUND_TRUTH_REQUIRED or DEPRECATED. The 17
explicitly excluded records are not counted. Four pool sharing/import/export
APIs are marked OUT_OF_SCOPE_CURRENT; they were not probed or implemented.

`SUPPORTED_DELTA.csv` compares public APIs with the goal baseline: **21 newly
supported APIs**, comprising nine IMPLEMENTED and 12 PARTIAL_IMPLEMENTED.
Eight are core Runtime APIs and 13 are pool APIs. Unsupported/blocked results
do not count as successful progress. cudaMemcpyAsync's pre-existing async D2D
path was separately verified and its stale partial ledger status corrected.

## Requirement and evidence mapping

| Requirement | Implementation or evidence |
| --- | --- |
| Exhaust practical remaining core APIs | The core checkpoint adds reset/thread-exit, pre-resource flags, basic extended launch, legacy configuration, host functions and callbacks. This closeout additionally implements bounded Driver-entry queries. See the ledger, scope/delta CSVs, the core evidence under `evidence/m4/remaining-core-20261012/`, and `entry-{native,integration}.stdout.log`. |
| Implement native pool lifecycle and controls | Default/current pool query/selection, create/destroy/trim, attributes and owning-device access; seven IMPLEMENTED, six PARTIAL_IMPLEMENTED. `driver-probe.stdout.log`, `runtime-probe.stdout.log` and `integration.stdout.log`. |
| Stream-ordered allocation and async free | cudaMallocAsync, cudaMallocFromPoolAsync and cudaFreeAsync use the existing allocation table and fake-VA allocator. Public tests include cross-stream event dependencies, H2D/D2D/D2H, kernel use and both ordinary/async free paths. `integration.stdout.log`. |
| Reuse ownership and ordering mechanisms | RuntimeContext generation/tombstones; ServerSession PoolID registry; existing AllocationID, StreamID, event/frontier and TransferID state. Async free invalidates allocation identity immediately without discarding independently owned transfer staging. No alternate allocator or deferred-free state machine. |
| Prefer schema/codegen | Pool RPCs 75–87 use generated bounded codecs, typed calls and handler metadata; flags use generated opcode 74. Added pool-ID type and capability 8 in the existing generator. Handwritten code handles native lifetime, pointer translation, stream ordering and measured limits. `schema.stdout.log` reports 44 schema APIs and deterministic/rejection checks. |
| Independent public transport validation | The runner starts a server with Popen and runs separately linked public clients through 127.0.0.1 sockets, CRX9/V3 and the backend seam. `COMMANDS.txt`, `ENVIRONMENT.txt`, `integration.stdout.log`, `entry-integration.stdout.log` and `server.stdout.log`. |
| Protocol trust boundary and capability checks | Malformed bounded requests, invalid IDs/enums, deferred destruction, immediate free-ID invalidation and EOF/default-pool isolation. `protocol-integration.stdout.log`. Legacy pool capability rejection is separately tested with a mock negative peer in `callback-errors.stdout.log`; it is not positive CoreX support evidence. |
| Shared-subsystem regressions only when justified | Context teardown/callback locking changed in the core checkpoint: lifecycle, isolation and 8×100 multithread passed. Pool allocation ownership/default-pool persistence changed here: lifecycle, isolation and failure/reconnect passed, including stale pool/async-pointer rejection and fresh identities. Results and selected full-output hashes are retained in `*-details.txt` and core multithread evidence. |
| Build, ABI and backend discipline | `build.stdout.log`: protocol fixtures, generated tests, ABI/export checks and no native libcudart client dependency pass. `backend-seam.stdout.log`: zero direct Driver calls in the server dispatcher. `registry.stdout.log`: duplicate opcode rejected. |
| Aligned support documentation | Ledger is authoritative; PROJECT_STATE records the completed slice and limits; README remains a high-level capability/build/run summary; HELLO_CAPABILITIES documents pool negotiation. Source hashes are in `SOURCE_MANIFEST.sha256`. |
| Committed, clean completion | The closeout is accepted only after the final commit and an empty `git status --porcelain`. Final artifact/source hashes and whitespace/contracts checks precede that commit; the terminal verification follows it. |

The full ownership run completed against the final pool implementation,
including the zero-target trim fallback. The later client-only queried Driver
adapters reuse public Runtime functions without altering shared lifetime or
ordering. Their native/public family checks and the pool/wire family were run
against the final rebuilt library; ownership regressions were not repeated for
that adapter-only change.

## Measured partial support and blockers

Native reuse setters succeed but read back disabled; enabling those policies
returns NotSupported. Release thresholds and high-water resets work. Pool
metrics count allocation granules (2 MiB for the small tested allocation).
Owning-device permissions remain ReadWrite; unsupported permission changes
return NotSupported. Pools are pinned, device zero and non-exportable.

Native TrimTo alone did not reclaim unused reservations. Temporarily setting
the release threshold to zero and synchronizing did: the grouped native probe
released 10 MiB, and the public test verifies zero reserved bytes, threshold
restoration and continued live-allocation validity. Zero-target trim uses that
measured mechanism and a conservative context barrier. Nonzero targets retain
native best-effort behavior without a reclamation claim. This preserves the
minimum-reservation contract documented by the
[CUDA stream-ordered allocator reference](https://docs.nvidia.com/cuda/archive/11.4.3/cuda-runtime-api/group__CUDART__MEMORY__POOLS.html).

Native Runtime cudaMallocAsync(0) returns null success and cudaFreeAsync(NULL)
succeeds, whereas the corresponding Driver calls reject zero. The separate
native cudaMallocFromPoolAsync(0) returns InvalidValue; the remote facade
preserves all three measured Runtime results.

Queried Driver support is restricted to cuMemAlloc/cuMemFree, synchronous
H2D/D2H/D2D copies and cuCtxSynchronize, using the implicit RuntimeContext and
fake pointers. Default/legacy flags work; PTDS and other symbols are rejected
with null output. Native zero-free and zero-allocation behavior is measured.
There is no general Driver context or native-pointer interoperability claim.

The remaining core blockers have concrete evidence: unsupported managed/array
allocation prevents valid dependent objects; cooperative launch and device
kernel attribute setters return native NotSupported; persisting L2 reset has
no successful valid native path; obsolete double converters lack proven
native FP64 equivalence. Client host pages cannot become GPU-mapped server
pages, and opaque UUID export tables lack a documented remote ABI. These
limitations are individually recorded in the ledger and are not counted as
implementation progress.

Callback workers preserve requested-stream completion and host visibility,
but conservatively serialize subsequent Runtime calls, including unrelated
streams. Basic extended launch accepts no attributes or Ignore. Device flags
may change only before native session resources exist. These core limitations
remain explicit in the ledger.

Evidence contains logs, hashes and audit records only; no build trees,
binaries or copied generated source are retained.
