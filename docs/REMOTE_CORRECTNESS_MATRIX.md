# Same-architecture remote correctness qualification

BASE_HEAD: `6538a582fa4ff2266d00c5b59af20316238ff018`; initial worktree clean.
Client and Server are built for the same CPU architecture and compatible host ABI:
aarch64 → TCP/veth → aarch64 runtime_server → CoreX 4.4 / MR-V100.
Same-host emulation only; no performance or cross-ISA claims.

The ordinary-user runner uses a rootless server network namespace and a nested
isolated client namespace (RC-003); both retain the host filesystem/devices.
With network-administrative privileges it can retain the initial server namespace.
Run `python3 scripts/run-remote-correctness.py --out <evidence-directory>` after
`scripts/build.sh`; setup, real CoreX server, clients, restart and cleanup are
contained in that one runner. `--topology-only` repeats negotiation/isolation and
protocol-boundary checks without the full capability matrix.

| Scenario | Representative coverage | Required observations |
| --- | --- | --- |
| N: topology/negotiation | isolated client netns, independent server; HELLO, version, capabilities | distinct netns, non-loopback addresses, real GPU identity; bad version/unknown capability errors, subsequent healthy session |
| C: core/memory | device/context readback; linear H2D/D2D/D2H, sync/async; fills; pointer 2D/3D; registered kernel and global/constant symbols | exact data/padding; event cross-stream ordering; deferred host visibility; staging release; stale resources rejected |
| P: pools | default/current/custom pools, malloc async/from pool, ordinary/async free | exact kernel/copy output; destroyed pool with live allocation; pending D2H retirement; reset identities; fresh default state |
| G: graph/capture | explicit kernel/copy/fill/event DAG, instantiate/repeated launch; bounded capture/replay; native alloc/free; exec/resource lifetime | exact output; snapshot independence; capture invalidation/recovery; graph storage/EOF cleanup |
| F: failures/isolation | exit with live allocation/stream/event/module/graph/exec/pool; server kill; reconnect/reset | cleanup order and empty session; fresh generation; all six stale identity families rejected; pending D2H invalidated without host writes; fresh numerical work |
| W: trust boundary | malformed/bounded/truncated requests, unsupported operations | deterministic errors; no crash; healthy request/session after rejection/EOF |
| S: static gates | build, ABI/exports, golden protocol, schema determinism/contracts, handler uniqueness, backend seam | all gates pass; evidence hashes match committed sources |

Reuse existing capability-family public clients and wire helpers; add only remote
orchestrator and missing failure assertions. No broad historical suite without a
shared-subsystem change or a finding that justifies it. The issue register is
[REMOTE_CORRECTNESS_ISSUES](REMOTE_CORRECTNESS_ISSUES.md). Evidence belongs in
`evidence/m6/remote-correctness-20261013/`; final audit maps each row to logs.
