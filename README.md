# x-remote-runtime

An engineering prototype that runs ordinary CUDA-style applications through a
remote Runtime library and the CoreX Driver on Iluvatar MR-V100. Compiler-generated
registration and normal `kernel<<<...>>>` launches use the remote library.

```text
application/compiler ABI -> libcorex_remote_cudart -> RuntimeContext
 -> CRX9/V3 over TCP -> ServerSession -> CoreX backend -> Driver -> MR-V100
```

Verified environment: aarch64 Ubuntu 22.04, GCC/G++ 11.4, CoreX 4.4.0
(`clang++` 18.1.8), MR-V100. Client and server require the same CPU architecture
and compatible host ABI. Qualification uses independent processes and isolated
network namespaces/veth on one host; physical two-host deployment is unqualified.

## Build and install

Run from a clean checkout. Required tools: the CoreX SDK/compiler at
`/usr/local/corex-4.4.0`, GCC/G++, Python 3.10+, binutils, Bash, tar and ripgrep.
`pkg-config` is optional; the consumer helper has an explicit-prefix fallback.
Use a dedicated SDK prefix without spaces. The server requires the installed
CoreX Driver and accessible GPU device; the client library does not link native
`libcuda` or `libcudart`.

```bash
export COREX=/usr/local/corex-4.4.0
export PREFIX=/tmp/x-remote-sdk
./scripts/build-sdk.sh
python3 scripts/verify-sdk.py "$PREFIX"
./scripts/package-sdk.sh
```

`build-sdk.sh` cleans/rebuilds `build/` and `dist/`, runs ABI/export, scope,
protocol and generated-code checks, then calls `install-sdk.sh`. To reinstall
an existing build, use `PREFIX=/your/dedicated/prefix ./scripts/install-sdk.sh`.
The archive is `dist/corex-remote-sdk-1.1.0-aarch64.tar.gz`; extract it into a
new prefix with `tar -xzf <archive> -C <prefix>` after creating that directory.
Its pkg-config metadata follows the extracted location. `PREFIX` selects the
install/consumer directory, not a runtime endpoint.

The SDK contains the versioned library and symlinks, server, optional extension
header, pkg-config metadata and ABI contracts. Standard CUDA public headers
and device compilation come from the CoreX SDK; these vendor headers are not
copied into this package. Internal headers and build objects are not installed.

## Start the server

In a separate terminal (set `PREFIX` there too):

```bash
export PREFIX=/tmp/x-remote-sdk
COREX_REMOTE_BIND=127.0.0.1 COREX_REMOTE_PORT=50051 \
  "$PREFIX/bin/corex-remote-runtime-server"
```

Wait for `SERVER_READY`. The server uses CoreX from the build's `COREX/lib64`
path. Stop it with Ctrl-C. `COREX_REMOTE_BIND` is a numeric IPv4 listen address,
while `COREX_REMOTE_PORT` is the listen port; defaults are `127.0.0.1:50051`.
For a reachable interface, explicitly set its numeric address.

## Compile and run a client

In the build terminal, this compiles a normal CUDA application outside the
repository and links only the installed remote Runtime:

```bash
export WORK=/tmp/x-remote-consumer
./scripts/build-consumer.sh
COREX_REMOTE_HOST=127.0.0.1 COREX_REMOTE_PORT=50051 "$WORK/app"
```

Expected output includes `G8D_NUMERICAL failures=0` and
`G8D_CLEAN_CONSUMER_RESULT=PASS`. The helper copies
[the public consumer example](tests/integration/external_consumer.cu) to
`$WORK/main.cu`; replace that source in your own build for your application.
It compiles with CoreX `clang++ -x ivcore --cuda-path="$COREX"` and the installed
G++ system include paths, then links the object with G++,
`-L"$PREFIX/lib" -lcorex_remote_cudart -Wl,-rpath,"$PREFIX/lib"`.
With pkg-config, obtain flags via
`PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig" pkg-config --cflags --libs corex-remote-cudart`.
The client endpoint uses numeric IPv4 `COREX_REMOTE_HOST` and
`COREX_REMOTE_PORT`; both default to `127.0.0.1:50051`.

## Qualification

After `build-sdk.sh`, run these from the repository. The remote runner owns
server startup, topology and cleanup. It requires `iproute2`, `util-linux` and
working user/network namespaces (or host network administration privileges).
Ordinary users use the documented rootless server namespace variant.

```bash
export OUT="$PWD/build/qualification"
OUT="$OUT/static" ./scripts/qualify-static.sh
python3 scripts/run-remote-correctness.py --out "$OUT/correctness"
python3 scripts/build-workloads.py --out "$OUT/workload-build"
python3 scripts/run-remote-correctness.py --workloads all --out "$OUT/workloads"
python3 scripts/verify-evidence-links.py .
```

The static wrapper checks deterministic schema/contracts, registry uniqueness
and backend separation. Correctness covers representative memory, streams/events,
symbols, pools, Graph/Capture, failure/reconnect and protocol boundaries.
The workload command qualifies twelve compiled applications; it makes no
performance claim. Pass criteria and evidence are in
[release readiness](docs/RELEASE_READINESS.md),
[remote correctness](docs/REMOTE_CORRECTNESS_MATRIX.md) and
[real workloads](docs/REAL_WORKLOAD_COMPATIBILITY.md).
Historical capability runners and `dev/local-remote/` helpers remain available;
the commands above are the release entrypoints.

## Capabilities and limits

Supported families include linear/pointer-backed pitched and 3D memory,
streams/events, staged host transfers, compiler registration and kernel launches,
device/context/function queries, registered global/constant symbols, native pools
and stream-ordered allocation, bounded native Graphs and Stream Capture.
Partial support has explicit limits: single-stream ThreadLocal capture,
device-only Graph copies and process-serialized client host functions/Graph tails.
Arrays, texture objects, managed allocation and cooperative launch are unavailable
on the measured backend. IPC, peer/multi-device, graphics/external interop, pool
sharing and User Object architecture are outside the release boundary.

This prototype does not claim full CUDA compatibility, production readiness,
PyTorch support, cross-ISA support or performance qualification.
[The API ledger](compat/cuda-api-ledger.csv) is authoritative; its derived counts
are in [release readiness](docs/RELEASE_READINESS.md).

See [architecture](docs/ARCHITECTURE.md) for ownership and transport,
[current state](docs/PROJECT_STATE.md) for the latest result,
[compatibility closure](docs/API_COMPATIBILITY_CLOSURE.md) for the audited boundary,
[Graph/Capture model](docs/GRAPH_CAPTURE_MODEL.md) for detailed semantics,
[correctness issues](docs/REMOTE_CORRECTNESS_ISSUES.md) for accepted limitations,
[evidence policy](docs/EVIDENCE_POLICY.md) for audit format, and
[history](docs/HISTORY.md) for preserved development records.
