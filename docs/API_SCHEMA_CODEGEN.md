# API Schema and Codegen V2

`schema/corex_api_schema.json` is the reviewed source for simple scalar and
bounded-DTO RPC plumbing. It is deliberately separate from `compat/cuda-api-ledger.csv`:
the ledger records human semantic judgement, while the schema records wire
shape and the backend binding that has already been reviewed.

## Schema

The schema is version 2 and each entry contains:

- an explicit opcode symbol (`opcode_name`) and numeric opcode, capability,
  implementation class, ABI, and export flag;
- request and response fields with a closed wire-type vocabulary;
- optional object kind metadata for remote IDs; and
- a typed backend binding label for review and consistency checks.

Supported scalar/object wire types are `u32`, `i32`, `u64`, `i64`,
`size_u64`, `bool_u32`, and the typed remote IDs. `bounded_bytes` and
`bounded_string` carry an explicit length and schema maximum; strings are
NUL-reconstructed locally and embedded wire NULs are rejected. Unknown types,
duplicate names/opcode symbols/opcodes, invalid capabilities, and malformed
fields fail generation.

## Generated boundary

`scripts/generate-api-schema.py` emits `include/generated/corex_api_schema.h`
and `tests/generated/corex_api_schema_test.c`. Generated files are marked
`DO NOT EDIT` and include:

- API and field metadata;
- request/response size-bound validators and generated server registry entries;
- network-order fixed-width and bounded DTO codecs; and
- typed client RPC call helpers that keep encode/call/decode/free plumbing
  consistent.

The codecs do not select CUDA error policy, pointer ownership, stream ordering,
or object lifetime rules. Those remain handwritten in the client/server
semantic layers. Schema maximum constants are generated so semantic wrappers do
not repeat bounded-field limits.

Run `scripts/test-api-schema.sh` to prove deterministic regeneration, duplicate
API/opcode and unknown-field rejection, bounded-codec behavior, and the
fail-closed invalid-schema path.
The same gate invokes `scripts/verify-api-contracts.py`, which checks that every
schema API has a ledger row, that `opcode_name=opcode` matches the explicit
protocol enum and ledger `protocol_change`, and that the expected ABI/export
packaging entries exist. Schema-managed ledger classes must have a schema entry;
semantic state-machine classes remain handwritten by design.

## Adding an API

Probe CoreX first, classify the API, add one schema entry, add any typed backend
primitive/mapping, keep the semantic wrapper handwritten, regenerate, and add
one focused positive/negative test. Legacy opcodes 1--38 remain frozen; new
wire opcodes must be explicit and must not reuse an existing number.
