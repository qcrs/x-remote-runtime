# CRX9/V3 optional HELLO

`OP_HELLO = 27` is an optional extension to CRX9/V3. Existing opcodes 1–26,
their payloads, and both V3 headers are unchanged. A HELLO request has no
payload. A supporting server returns `ST_OK` and a bounded sequence of
network-byte-order `uint32_t` values:

| Word | Meaning |
| --- | --- |
| 0 | `CRX_PROTOCOL_MAGIC` (`CRX9`) |
| 1 | `CRX_PROTOCOL_VERSION` (3) |
| 2 | HELLO schema version (1) |
| 3–5 | server implementation major, minor, patch |
| 6 | backend ID (`1` = CoreX) |
| 7 | backend driver version from `cuDriverGetVersion`, or `0` if unavailable |
| 8 | exposed logical device count (currently 1) |
| 9 | device profile ID (`1` = generic CoreX device; `0` = unknown) |
| 10 | capability count, at most 32 |
| 11 onward | stable capability IDs, exactly `count` words |

Capability IDs describe supported semantic service groups, not individual
exported symbols or native CoreX structs:

| ID | Service group |
| --- | --- |
| 1 | device information |
| 2 | linear device allocation and fake-VA translation |
| 3 | synchronous host/device copies |
| 4 | stream and event operations |
| 5 | asynchronous host/device transfers |
| 6 | module load and kernel launch |

IDs are never reused. Clients may ignore unknown nonzero IDs; zero and
duplicates are invalid. A new client classifies an empty `ST_BAD_REQUEST` for
HELLO as a legacy V3 peer with **unknown**, not absent, capabilities and keeps
the connection. Other failures reject initialization. A malformed `ST_OK`
payload (identity mismatch, bad length/count, zero or duplicate ID) fails the
session. Capability data is scoped to a connection generation and discarded
on failure or shutdown.
