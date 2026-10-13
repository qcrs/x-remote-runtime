# Engineering prototype release readiness

Release baseline: `97907fe70a09fed21f1a8d28d52cb92acdb8fd0c` (clean).
Scope: packaging, operations and documentation; no CUDA API or workload expansion.
Evidence: [release audit](../evidence/m7/release-hardening-20261013/GOAL_AUDIT.md).

## Environment and deployment

Verified on server310p, aarch64 Ubuntu 22.04 / Linux 5.15, GCC/G++ 11.4,
CoreX 4.4.0 (`clang++` 18.1.8), real Iluvatar MR-V100. Boundary:
aarch64 application/compiler ABI -> remote Runtime -> CRX9/V3 TCP -> aarch64
runtime_server -> CoreX backend/Driver -> GPU. Matching CPU architecture and
compatible host ABI are required. Remote qualification uses independent processes
and same-host rootless network namespaces/veth; RC-003 remains accepted.
Physical two-host and initial-host server-netns qualification are not claimed.

## Reproducible path

[README](../README.md) owns the copyable build/server/client instructions.
One recommended path is `build-sdk.sh` -> `verify-sdk.py "$PREFIX"` ->
`package-sdk.sh` -> extract/install into a dedicated prefix -> start
`$PREFIX/bin/corex-remote-runtime-server` -> `build-consumer.sh` with external
`WORK` and installed `PREFIX` -> `$WORK/app`.
`build.sh` and `install-sdk.sh` remain component entrypoints;
older local/integration helpers remain historical development conveniences.

The package supplies `libcorex_remote_cudart.so.1.1.0`, relative SONAME/linker
symlinks, the extension header, server, relocatable pkg-config metadata,
export/version map and ABI descriptions. Standard CUDA headers/device compiler
are supplied by the CoreX SDK. The client links the remote DSO without native
Driver/libcudart; only the server links the CoreX Driver. Server native-library
lookup retains the configured build-time `COREX/lib64` path. The compiler helper
supports pkg-config or explicit-prefix fallback; paths with spaces are outside
the helper's documented shell flag parsing contract.

Server defaults: `COREX_REMOTE_BIND=127.0.0.1`, `COREX_REMOTE_PORT=50051`.
Client defaults: `COREX_REMOTE_HOST=127.0.0.1`, same port. Both addresses are
numeric IPv4; set them explicitly for another reachable interface. Wait for
`SERVER_READY` before starting the client.

## Canonical qualification

Run after the clean build from the repository:

```bash
OUT=build/qualification/static ./scripts/qualify-static.sh
python3 scripts/verify-sdk.py /your/dedicated/sdk-prefix
python3 scripts/run-remote-correctness.py --out build/qualification/correctness
python3 scripts/build-workloads.py --out build/qualification/workload-build
python3 scripts/run-remote-correctness.py --workloads all --out build/qualification/workloads
python3 scripts/verify-evidence-links.py .
```

Build performs ABI/export/scope, golden protocol and generated DTO checks.
Static qualification composes deterministic schema/contracts, duplicate-handler
rejection and backend seam checks. The correctness runner owns isolated topology,
server/restart/cleanup and the [representative matrix](REMOTE_CORRECTNESS_MATRIX.md).
The workload mode reuses that runner for exactly the
[twelve real applications](REAL_WORKLOAD_COMPATIBILITY.md), including compiler
registration and normal launches. It requires iproute2/util-linux and usable
user/network namespaces. The SDK consumer run must also prove exact numerical
results, installed-library binding and no source/build-tree dependency.

## Compatibility summary derived from the ledger

The current snapshot includes the subsequent
[compatibility closure](API_COMPATIBILITY_CLOSURE.md); release packaging results
below retain their original M7 validation scope.

Snapshot: 276 total records, including 266 public Runtime APIs, seven private
compiler ABI hooks and three project extensions. Recompute with
`python3 scripts/report-api-coverage.py`; the
[CSV ledger](../compat/cuda-api-ledger.csv) remains authoritative.

| Summary label | Exact ledger status | All records | Public Runtime APIs |
| --- | --- | ---: | ---: |
| IMPLEMENTED | IMPLEMENTED | 116 | 107 |
| PARTIAL_IMPLEMENTED | PARTIAL_IMPLEMENTED | 76 | 75 |
| BLOCKED | GROUND_TRUTH_BLOCKED | 63 | 63 |
| BACKEND_UNSUPPORTED | BACKEND_UNSUPPORTED | 13 | 13 |
| OUT_OF_SCOPE | OUT_OF_SCOPE_CURRENT | 8 | 8 |
| Uninvestigated declarations | GROUND_TRUTH_REQUIRED | 0 | 0 |

Counts describe ledger rows, not full CUDA compatibility. The M8 closure
investigates the remaining declarations and preserves specific technical limits.
OUT_OF_SCOPE_CURRENT is a literal ledger count, not the entire excluded release
surface.

Supported families: linear/pitched/3D pointer memory and fills, streams/events,
staged host transfers, compiler kernel/symbol registration, device/context/function
queries, pools/stream-ordered allocation and bounded native Graph/Capture.
Excluded families: IPC, peer/multi-device, graphics/external interop, pool sharing,
User Objects and cross-ISA support. Arrays/texture objects/managed allocation and
cooperative launch have measured backend limitations. Detailed partial support
includes single-stream ThreadLocal capture, device-only Graph copies, bounded
resource tables and process-serialized client host functions/Graph host tails;
see [architecture](ARCHITECTURE.md), [Graph model](GRAPH_CAPTURE_MODEL.md) and ledger.

## Result

**READY as a scoped engineering prototype**, qualified on 2026-10-13. Clean
build, ABI/export and protocol/schema/registry/backend gates passed. Installed and
archive-relocated SDK checks passed; external consumers built and ran with the
repository build/dist and original install paths unavailable. Actual loader
bindings resolved compiler hooks, launches and memory calls to the installed
remote DSO. Canonical remote correctness and all twelve real applications passed;
all twelve workload sessions cleaned up empty and reset their native contexts.
Documentation/evidence links and committed-tree audit are recorded in the
[release audit](../evidence/m7/release-hardening-20261013/GOAL_AUDIT.md). This is an engineering
prototype qualification, not production, arbitrary CUDA application, PyTorch,
performance or physical two-host qualification. Runtime/protocol/lifetime
findings are retained in the [issue register](REMOTE_CORRECTNESS_ISSUES.md);
packaging/documentation findings belong in the release audit.
