# Remote correctness issues

Real-workload qualification baseline: `2e8528ffc29452c8783934d837e572b58095732e`.
New evidence root: `evidence/m6/real-workloads-20261013/`.

Qualification baseline: `6538a582fa4ff2266d00c5b59af20316238ff018`.
Evidence root: `evidence/m6/remote-correctness-20261013/`.

Each record uses: ID; status; category; subsystem; observed → expected;
reproduction/evidence; root cause; resolution/limitation; fixing commit.
Categories: RUNTIME_BUG, PROTOCOL_BUG, LIFETIME_BUG, TEST_INFRA_BUG,
BACKEND_LIMITATION, DESIGN_LIMITATION, ENVIRONMENT_LIMITATION.
Statuses: OPEN, FIXED, ACCEPTED_LIMITATION, BACKEND_BLOCKED,
ENVIRONMENT_BLOCKED, NOT_REPRODUCED. Existing accurately documented API limits
are not new issues. Passing scenarios appear in the audit, not as issues.

## RC-001

- Status: FIXED; category: DESIGN_LIMITATION; subsystem: server TCP listener.
- Observed → expected: server always binds `127.0.0.1`, so clients across a veth
  cannot reach it → explicitly configurable non-loopback IPv4 listener.
- Reproduction: baseline `server/runtime_server.c` main uses
  `htonl(INADDR_LOOPBACK)` and prints a fixed loopback endpoint.
- Root cause: only the listening port is configurable.
- Resolution: `COREX_REMOTE_BIND` accepts a numeric IPv4 address; invalid values
  fail explicitly; loopback remains the default. `rc001-baseline/` records the
  refused veth connection; final namespace runs verify the configurable listener.
- Fixing commit: `dc28edb` (full hash in the final evidence audit).

## RC-002

- Status: FIXED; category: TEST_INFRA_BUG; subsystem: direct-wire family clients.
- Observed → expected: graph, pool, symbol, context and fill protocol clients
  hardcode loopback → honor `COREX_REMOTE_HOST` for remote validation.
- Reproduction: endpoint definitions in `tests/integration/{graphs,memory_pools,
  symbol_expansion,compat_expansion,execution_fill}_protocol.py`.
- Root cause: these tests assumed their local historical orchestrators.
- Resolution: use the existing endpoint override with the loopback default;
  final namespace logs cover all five affected wire clients.
- Fixing commit: `dc28edb` (full hash in the final evidence audit).

## RC-003

- Status: ACCEPTED_LIMITATION; category: ENVIRONMENT_LIMITATION;
  subsystem: same-host topology privileges.
- Observed → expected: uid 1002 has no CAP_NET_ADMIN in the initial namespace;
  `sudo -n true` requires a password → preferred initial-host server/veth setup.
- Evidence: `qualification/ENVIRONMENT.txt` and `qualification/topology-{server,client,route}.stdout.log`.
- Root cause: host administrative privileges unavailable to this session.
- Resolution: rootless user-owned server netns with a nested isolated client
  netns and veth; no mount/device namespace or GPU relocation. CoreX context
  creation and MR-V100 discovery work there. Runner also supports a privileged
  initial-host server namespace. This qualification claims the measured rootless
  server-netns variant, not initial-host-netns or physical two-host validation.
- Fixing commit: not applicable (environment).

## RC-004

- Status: FIXED; category: TEST_INFRA_BUG; subsystem: new HELLO assertion.
- Observed → expected: qualification expected backend_version=4400 and rejected
  HELLO's 10020 → HELLO must match the actual Driver version query; SDK release
  4.4.0 is a separate fact.
- Evidence: `rc004-before/negotiation-trust.stderr.log` and
  `rc004-status-before/negotiation-trust.stderr.log`; reproduce with the runner.
- Root cause: test conflated SDK release naming with `cuDriverGetVersion` and
  expected newer status 10 from a legacy setter whose existing wire status 3
  is mapped by the public client to NotSupported.
- Resolution: compare existing Driver version opcode 35 and assert deterministic
  legacy setter status 3; the public context-family test checks NotSupported.
- Fixing commit: `dc28edb` (full hash in the final evidence audit).

## RC-005

- Status: FIXED; category: TEST_INFRA_BUG; subsystem: workload assertion helper.
- Workload: numerical_pipeline (first normal compiler build).
- Observed → expected: compile rejects event helper because CUDA macro local
  `e` shadows the event variable inside its initializer → public API gets event pointer.
- Evidence: `evidence/m6/real-workloads-20261013/compiler-first/numerical_pipeline-compile.stderr.log`.
- Root cause: common workload macro identifier collision; no Runtime defect.
- Resolution: use a distinct macro-local error name; rebuild and execute normal
  compiler launch path passed through namespace/veth; final twelve-application
  build/run passed with the shared helper.
- Fixing commit: `e225f3f311875b06016ea5ab0278eb36ef10696d`.

## RC-006

- Status: FIXED; category: TEST_INFRA_BUG; subsystem: graph-memory workload error check.
- Workload: graph_memory.
- Observed → expected: four graph allocation/kernel/copy/free replays produce
  correct data, but the application rejects the expected stale-pointer last-error
  value → verify and clear cudaErrorInvalidDevicePointer after the negative call.
- Evidence: `evidence/m6/real-workloads-20261013/run-initial/graph_memory.stderr.log` and stdout.
- Root cause: workload incorrectly wrapped cudaGetLastError in a success-only macro;
  no Runtime or graph lifetime defect.
- Resolution: assert the expected last-error value; graph memory and mixed app
  regression passed, followed by the full committed namespace suite.
- Fixing commit: `e225f3f311875b06016ea5ab0278eb36ef10696d`.
