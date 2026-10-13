# Real-workload compatibility

Baseline: `2e8528ffc29452c8783934d837e572b58095732e` (clean).
Qualification in progress; evidence: `evidence/m6/real-workloads-20261013/`.

A real workload here is a compiled application with public CUDA/CoreX headers,
ordinary Runtime calls and compiler-generated device registration. All twelve
applications in [tests/workloads](../tests/workloads/README.md) contain normal
`<<<>>>` launch syntax or compiler-generated kernel stubs used by Graph APIs.
They contain no Driver, controlled-module, internal protocol or fabricated ID calls.
Existing integration tests do not count as workloads.

The installed aarch64 CoreX 4.4 `clang++ -x ivcore --cuda-path=/usr/local/corex-4.4.0`
compiles each `.cu` object, using the installed GCC C++ headers. `g++` links that
object with only the public `dist/lib/libcorex_remote_cudart.so.1` SDK library.
The dedicated build runner logs exact commands, ELF architecture, dynamic
dependencies, resolved libraries and undefined compiler hooks/launch symbols.
Numerical application loader diagnostics record actual binding of hooks,
configuration, launch and memory entry points to the remote library.

The existing remote correctness runner's `--workloads all` mode invokes only
these applications over isolated client `198.18.0.2` → veth/TCP → independent
server `198.18.0.1:50991` → real CoreX Driver → MR-V100. It reuses setup/cleanup
and the accepted rootless server namespace environment from RC-003. Client and
server are aarch64 with compatible host ABI and unchanged GPU device access.
This is same-host remote emulation; no physical two-host, cross-ISA or performance
claim is made.

| Application | What the numerical/order check proves | Status |
| --- | --- | --- |
| numerical_pipeline | 4096-element two-kernel affine/combine, copied pointer/scalar arguments; 4*x+5 | Pending final run |
| shared_reduction | 32 blocks, dynamic shared memory and barriers, second reduction; exact host sum | Pending final run |
| pitched_image | 53x29 image, two kernels and D2D intermediate, non-image padding untouched | Pending final run |
| volume_transform | 23x11x7 voxels plus row padding; pitched/sliced 3D async copies and 3D launch | Pending final run |
| stream_pipeline | six producer/consumer rounds, two nonblocking streams, events; deferred D2H and exact output | Pending final run |
| pool_pipeline | six custom/default pool alloc/use/free phases with cross-stream ordering and pending D2H | Pending final run |
| explicit_graph | fill/kernel/copy/event DAG; five launches with exec scalar parameter updates | Pending final run |
| capture_pipeline | ThreadLocal fill/two-kernel/device-copy capture; five replays on changing input | Pending final run |
| symbol_pipeline | naturally registered global/constant values, offset async writes, kernel reads, global readback | Pending final run |
| host_interaction | kernel/D2H/client function before subsequent GPU work; graph host sink and client process effects | Pending final run |
| graph_memory | native allocation/fill/in-place kernel/copy/free DAG; four launches after source graph destruction, stale reservation after exec destruction | Pending final run |
| mixed_iterations | twelve resource-creation rounds, two streams/events, custom pool, symbols, two-kernel capture twice per round; all 4096 values | Pending final run |

Known model bounds remain those in the API ledger and
[Graph model](GRAPH_CAPTURE_MODEL.md): device-only Graph copies; single-stream
ThreadLocal capture; process-serialized client host functions/one Graph host
sink; bounded graph identities; pointer-backed 2D/3D, staging instead of client
GPU DMA, device-zero pools, bounded native graph memory ownership. Workloads
exercise these supported semantics without claiming general CUDA source
compatibility. No controlled-module substitute is used for compiler registration.

Findings continue in [RC issue ledger](REMOTE_CORRECTNESS_ISSUES.md): RC-005 macro
shadowing and RC-006 expected last-error handling are workload infrastructure
bugs, not evidence of Runtime implementation failures. Original failure logs are
retained alongside their regression runs. Final audit will link committed source
hashes, build/provenance, each application run and static gates.
