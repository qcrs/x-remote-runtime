# M9 partial semantics completion

Clean baseline: `352a3f8360eb824a7a30639dac815caf6dff2e20`, 2026-10-13.
All **75** baseline public PARTIAL rows are classified individually in
[the review](../evidence/m9/partial-semantics-20261013/partial-review.csv).
The [ledger](../compat/cuda-api-ledger.csv) remains authoritative.

Five rows contained practical implementation gaps. They are completed:

| API | Completed semantics |
| --- | --- |
| cudaGetDriverEntryPoint | Twenty additional real Runtime-backed memory, stream, event and ordinary launch entries; 26 total. Non-stream entries accept PTDS query flags (RC-009). Stream-sensitive PTDS remains unavailable. |
| cudaLibraryLoadData | Normalize a compiler-generated CoreX fatbin containing one ELF with the existing extractor. Accept the documented optional BinaryIsPreserved hint while retaining an owned image. |
| cudaLibraryLoadFromFile | Load the same fatbin format with bounded file-span validation; reject truncated spans and the invalid file-only preserved-binary hint. |
| cudaLibraryEnumerateKernels | Match measured native null-output rejection, including capacity zero (RC-011); retain prefix enumeration, nonnull zero capacity and module lifetime. |
| cudaSetDeviceFlags | Protect live Graph, GraphExec and pending TransferID owners from context replacement (RC-010). Unchanged flags remain allowed; different flags work after owners retire. |

There are **no PARTIAL → IMPLEMENTED upgrades**. Completing the gaps above does
not complete these APIs' remaining contracts. Public counts remain 107 implemented,
75 partial, 63 blocked, 13 backend unsupported, eight out of scope and zero required.
This result removes unfinished implementation within the practical supported
model without relabeling its limits.

| Remaining primary reason | Rows | Why they stay partial |
| --- | ---: | --- |
| BACKEND_LIMIT | 22 | CoreX stream policies and carveout queries are unavailable; occupancy flags return 801; pool accounting/reuse/access behavior differs; graph upload/accounting retains measured defects; PTX load returns 200 and native multi-kernel library unload hangs. Device selection still has only one native device, and explicit context flags have no in-place setter. |
| REMOTE_ARCH_LIMIT | 16 | Client host staging and callbacks have cross-process ownership and process-wide ordering constraints. Runtime kernel/function identities and queried Driver entries share one session; general native Driver interoperability and context-independent library ownership are absent. |
| RELEASE_SCOPE_LIMIT | 7 | Six symbol APIs use bounded metadata-bearing, registered non-managed symbols; managed storage prerequisites are unsupported. Legacy ConfigureCall lacks the obsolete SetupArgument/Launch surface in the installed Runtime. These release bounds remain explicit. |
| INTENTIONAL_BOUNDED_MODEL | 30 | Graph DAG/DTO, child, memory, host endpoint, update, flags and diagnostics restrictions; one-stream ThreadLocal capture and conservative unsafe-call rules. No settled Graph/Capture state machine was reopened. |

Each row has a primary reason; some also have secondary limits (for example,
library lifetime combines backend and remote ownership constraints). The baseline
review found five IMPLEMENTATION_GAP, 18 BACKEND_LIMIT, 15 REMOTE_ARCH_LIMIT,
seven RELEASE_SCOPE_LIMIT and 30 INTENTIONAL_BOUNDED_MODEL rows. No practical
IMPLEMENTATION_GAP remains unaddressed.

The prioritized review also reconfirms cudaFuncGetAttributes queries every available
native field; carveout query 9 returns 801. Stream nonempty windows fail and newer
policy attributes return 801. Native cuLaunchKernelEx succeeds even for invalid
attribute IDs/cluster dimensions, fails to record completion events, and reports
success for cooperative attributes despite direct cooperative launch returning
801. Success-only forwarding would falsely claim attribute semantics; the remote
API keeps its explicit rejection. Kernel/library enumeration, ordinary launches,
symbol ownership and stream/capture models retain their existing registries.

Validation and final audit are in one
[evidence root](../evidence/m9/partial-semantics-20261013/): grouped native probes,
independent public localhost TCP/CRX9 clients, library protocol validation,
pending-transfer-only context ownership, and build/ABI/protocol/schema/registry/
backend gates. Affected explicit-graph, capture, numerical and symbol workloads
run in the existing independent namespace/veth topology. Unchanged historical
multithread/reconnect suites are not repeated. No new physical two-host or
performance claim is made.

PARTIAL delta by family is zero in every family: Graph 32; Other Runtime 12;
Stream eight; Host Memory seven; Memory Pool / Async Allocation six; Memcpy four;
Execution three; Device two; Function one.

Semantic sources are the installed CoreX headers and measured behavior, plus
[Driver entry-point contracts](https://docs.nvidia.com/cuda/cuda-programming-guide/04-special-topics/driver-entry-point-access.html)
and [library option contracts](https://docs.nvidia.com/cuda/archive/12.8.0/cuda-runtime-api/group__CUDART__LIBRARY.html).
