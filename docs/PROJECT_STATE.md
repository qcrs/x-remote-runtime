# Project State

Current autonomous roadmap position: **M2-S1 ready** (M1 PASS).

| Item | Current value |
| --- | --- |
| Hardware | Iluvatar MR-V100 / 智铠100 |
| CoreX | 4.4.0 |
| Architecture | aarch64 |
| Protocol | CRX9 / VERSION 3 |
| Public library | `libcorex_remote_cudart.so.1` |
| CUDA ABI | `COREX_REMOTE_CUDART_1.0`, `COREX_REMOTE_CUDART_1.1` |
| Extension ABI | `COREX_REMOTE_EXT_1.0` |
| Development repository | `/home/lvtong/corex-remote-runtime` |
| Roadmap baseline | `159e7f95057a7ff4b2b3d1ef24d579a2ab2b8d2c` |
| Last PASS commit | M1-S5 (`m1/s5: make transport failure a session event`, this state commit) |
| Current Slice | M2-S1 CoreX 4.4 API census and canonical compatibility ledger |
| Blockers | None |
| Next action | Build a deterministic CoreX-header census tool that preserves reviewed semantic annotations |

The repository migration preserves the validated Runtime behavior. Gate 8
history, patch scripts, and generated validation outputs are retained under
`docs/gate8/` and `evidence/gate8d/` for reference; they are not development
entrypoints.

M1-S1 is accepted and frozen. Its coarse, non-recursive RuntimeContext mutex
remains the correctness boundary. M1-S2 passed build, ABI/protocol audit,
baseline numerical, 8x100 multithread, lifecycle/stale-object, negative, and
local dual-process gates. Evidence: `evidence/m1/s2/20260922-133708/`.

M1-S3 moved all CRX9/V3 common headers, status/opcode definitions, stable
object-ID scalar types, endian helpers, and bounded decoders into
`include/internal/corex_protocol.h`. Golden byte fixtures and all M1 regression
gates passed without a version, opcode, payload, or public ABI change. Evidence:
`evidence/m1/s3/20260922-135005/`.

M1-S4 moved allocation, stream, event, transfer, module, and kernel tables plus
all next-ID counters into an explicit per-connection `ServerSession`. Cleanup
takes that session, retires live resources, and restores an empty structure.
Sequential-connection isolation and all common regressions passed. Evidence:
`evidence/m1/s4/20260922-140429/`.

M1-S5 promotes send, receive, and protocol-header failures to an explicit
failed-session state. The failed generation is invalidated without replay;
lazy reconnect creates a clean generation. Idle/live-object server kills and a
malformed peer were exercised, followed by fresh numerical work. M1 now passes
G0-G6 and G8. Evidence: `evidence/m1/s5/20260922-141618/`.
