# CoreX Remote CUDA-compatible Runtime

This repository contains the CoreX Remote CUDA-compatible Runtime for an
Iluvatar MR-V100 on CoreX 4.4.0. A CUDA-style client runtime translates the
application-facing calls into CRX9/V3 requests. The Runtime Server owns the
remote CoreX objects and uses the CoreX Driver API to access the device.

The current validated baseline supports allocation and copies, streams and
events, asynchronous transfers, automatic compiler registration, kernel ABI
acquisition, ordinary `kernel<<<...>>>` launches, device identity queries, a
versioned shared library, and an installable SDK. It also supports pitched/3D
pointer-backed memory and fills, function/occupancy queries, context
configuration, partial compiled-library loading and registered global/constant
symbol access in one server context, and partial client page-locked host staging
with remote copies. Session reset, pre-resource context flags, basic extended
launches and client host callbacks are supported within the documented limits.
Native memory pools and stream-ordered allocation share the existing remote
allocation, stream ordering and session lifetime machinery.
Native graphs support device operation DAGs, event query/mutation, executable
snapshots and replay. Bounded native stream capture supports existing graphs
and dependency updates; native graph memory controls expose backend accounting.
The capture layer reuses those graphs;
its scope and lifetime model are in [Graph/Capture](docs/GRAPH_CAPTURE_MODEL.md).
Array allocation, texture objects, and managed allocation are unavailable on the
measured CoreX backend.

## Build

From this directory:

```bash
COREX=/usr/local/corex-4.4.0 ./scripts/build-sdk.sh
./scripts/build-consumer.sh
```

The scripts produce ignored outputs in `build/`, `dist/`, `sdk-install/`, and
`clean-consumer/`. `COREX`, `CC`, `CXX`, `GXX`, `PREFIX`, and `WORK` can be
overridden through the environment.

## Local client/server run

Build first, then use two terminals on `server310p`:

```bash
# Terminal A
./dev/local-remote/start-server.sh
```

```bash
# Terminal B
./dev/local-remote/run-client.sh
```

The default endpoint is `127.0.0.1:50051`. Set `COREX_REMOTE_HOST` and
`COREX_REMOTE_PORT` to override the client endpoint; the server reads
`COREX_REMOTE_PORT` and binds its loopback listener on that port.

The integration runner records a validation bundle below
`evidence/gate8d/runs/` when no `OUT` is supplied. These generated runs are
ignored by Git.

## Layout

```text
src/                 client runtime implementation
include/             public and internal headers
server/              Runtime Server and CoreX backend boundary
tests/integration/   compiler and clean-consumer integration inputs
scripts/              build, install, verify, and run entrypoints
packaging/           ABI map, exported symbol list, and SDK metadata
dev/local-remote/    two-terminal localhost development helpers
docs/                current architecture, state, and Gate 8 history
evidence/             preserved validation evidence and evidence policy
```

## Validation

`scripts/build.sh` runs the ABI and scope verifiers and builds the compiler
integration application. With a Runtime Server running, the existing
integration checks can be run with:

```bash
./scripts/run-integration.sh
```

The clean external consumer check is run with:

```bash
./scripts/run-clean-consumer.sh
```

## Current limits and next work

This project is a CoreX-targeted CUDA compatibility layer. It does not claim
to be a full CUDA replacement, PyTorch support, production readiness, WSL
deployment, or cross-host x86_64-to-aarch64 validation.

Detailed API status and limitations are recorded in the
[compatibility ledger](compat/cuda-api-ledger.csv); validated development status
and evidence links are in [PROJECT_STATE](docs/PROJECT_STATE.md). IPC,
Peer/Multi-device, and External Interop remain excluded. Pool sharing/import/export also remains excluded.
