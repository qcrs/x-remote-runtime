# API Schema and Codegen V2

`schema/corex_api_schema.json` is the reviewed source for simple, fixed-width
RPC plumbing. It is deliberately separate from `compat/cuda-api-ledger.csv`:
the ledger records human semantic judgement, while the schema records wire
shape and the backend binding that has already been reviewed.

## Schema

The schema is version 2 and each entry contains:

- an explicit opcode, capability, implementation class, ABI, and export flag;
- request and response fields with a closed wire-type vocabulary;
- optional object kind metadata for remote IDs; and
- a typed backend binding label for review and consistency checks.

Supported scalar/object wire types are `u32`, `i32`, `u64`, `i64`,
`size_u64`, `bool_u32`, and the typed remote IDs. Unknown types, duplicate
names/opcodes, invalid capabilities, and malformed fields fail generation.

## Generated boundary

`scripts/generate-api-schema.py` emits `include/generated/corex_api_schema.h`
and `tests/generated/corex_api_schema_test.c`. Generated files are marked
`DO NOT EDIT` and include:

- API and field metadata;
- request/response byte-size validators at the server trust boundary; and
- network-order fixed-width DTO codecs for each schema entry.

The codecs do not select CUDA error policy, pointer ownership, stream ordering,
or object lifetime rules. Those remain handwritten in the client/server
semantic layers.

Run `scripts/test-api-schema.sh` to prove deterministic regeneration, duplicate
API/opcode and unknown-field rejection, and the fail-closed invalid-schema path.
The same gate invokes `scripts/verify-api-contracts.py`, which checks that every
schema API has a ledger row, a protocol opcode, and the expected ABI/export
packaging entry.

## Adding an API

Probe CoreX first, classify the API, add one schema entry, add any typed backend
primitive/mapping, keep the semantic wrapper handwritten, regenerate, and add
one focused positive/negative test. Legacy opcodes 1--38 remain frozen; new
wire opcodes must be explicit and must not reuse an existing number.
