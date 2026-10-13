# Real application workloads

Baseline: `2e8528ffc29452c8783934d837e572b58095732e`, clean worktree.
Ordinary CoreX CUDA source, public headers and compiler-generated registration;
no direct Driver, internal protocol or controlled module calls.

Applications (all deterministic integer checks, no timings):

| Application | Mechanisms / result |
| --- | --- |
| numerical_pipeline | two kernels, async copies, scalar/pointer arguments; 4*x+5 |
| shared_reduction | dynamic shared memory, block sync, two-stage reduction; exact sum |
| pitched_image | pitched 2D image kernels, D2D intermediate; pixels and padding |
| volume_transform | pitched 3D allocation, async copies, 3D kernel; full volume |
| stream_pipeline | producer/consumer kernels, events, async staging; deferred host visibility |
| pool_pipeline | custom pool, repeated async allocations, cross-stream dependencies; per-round data |
| explicit_graph | fill/kernel/device-copy/events, exec parameter update; replay output |
| capture_pipeline | ThreadLocal kernel/fill/device-copy capture; replay output |
| symbol_pipeline | compiler global/constant registration and offset updates; kernel-visible values |
| host_interaction | kernel/D2H/host function, graph host tail, subsequent kernel; process and order |
| graph_memory | alloc/kernel/copy/free DAG; replay output and stale reservation |
| mixed_iterations | multi-kernel, streams/events, async memory, capture/pool/symbols; repeated output/cleanup |

Build: `python3 scripts/build-workloads.py --out <build-log-directory>` after
`scripts/build.sh`. Individual names may follow `--out` to build a subset.
Run all: `python3 scripts/run-remote-correctness.py --out <run-log-directory> --workloads all`.
Replace `all` with names to run a subset. The first numerical application also
records dynamic loader binding diagnostics as real-call provenance.
Artifacts stay in `build/workloads`; evidence contains logs and hashes only.
Qualification status and limitations: [compatibility report](../../docs/REAL_WORKLOAD_COMPATIBILITY.md).
